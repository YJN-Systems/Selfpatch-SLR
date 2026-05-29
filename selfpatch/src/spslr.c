#include <spslr.h>

#include "spslr_randomizer.h"
#include "spslr_env.h"
#include "spslr_list_link.h"

/*
 * Runtime portion of SPSLR.
 *
 * This code consumes the metadata emitted by patchcompile, randomizes target
 * layouts, rewrites static data objects, and patches instruction immediates
 * that encode structure field offsets.
 */

#define SPSLR_SANITY_CHECK

static int spslr_patch_dpins(const struct spslr_dpin *dpins, spslr_u32 cnt);
static int spslr_patch_dpin(void *addr, spslr_u32 target);
static int spslr_patch_ipins(const struct spslr_ipin *ipins, spslr_u32 cnt,
			     const struct spslr_ipin_op *ipin_ops,
			     spslr_u32 op_cnt);

static int reorder_object(void *dst, const void *src, spslr_u32 target);
static int spslr_calculate_ipin_value(const struct spslr_ipin_op *ipin_ops,
				      spslr_u32 op_cnt, spslr_u32 start,
				      spslr_s64 *res);

static void *reorder_buffer = NULL;

static int allocate_reorder_buffer(void);

static int initialized = 0, patched = 0;
static enum spslr_viability viable = SPSLR_VIABLE;

/*
 * Initialize runtime randomization state.
 *
 * After this point target layouts are randomized, but code/data does still
 * contain original-layout offsets until the patching entry points run.
 */

struct spslr_status __init spslr_init(void)
{
	if (initialized)
		return (struct spslr_status){ .viability = viable,
					      .error = SPSLR_OK };

	if (spslr_randomizer_init() < 0)
		return (struct spslr_status){
			.viability = viable,
			.error = SPSLR_ERROR_RANDOMIZER_INIT
		};

#ifdef SPSLR_SANITY_CHECK
	for (spslr_u32 tidx = 0; tidx < spslr_target_cnt; tidx++) {
		if (spslr_randomizer_validate_target(tidx) < 0)
			return (struct spslr_status){
				.viability = viable,
				.error = SPSLR_ERROR_INITIAL_TARGET_LAYOUT
			};
	}
#endif

	if (spslr_randomize() < 0)
		return (struct spslr_status){ .viability = viable,
					      .error = SPSLR_ERROR_RANDOMIZE };

#ifdef SPSLR_SANITY_CHECK
	for (spslr_u32 tidx = 0; tidx < spslr_target_cnt; tidx++) {
		if (spslr_randomizer_validate_target(tidx) < 0)
			return (struct spslr_status){
				.viability = viable,
				.error = SPSLR_ERROR_RANDOMIZED_TARGET_LAYOUT
			};
	}
#endif

	if (allocate_reorder_buffer() < 0)
		return (struct spslr_status){
			.viability = viable, .error = SPSLR_ERROR_REORDER_BUFFER
		};

	initialized = 1;
	return (struct spslr_status){ .viability = viable, .error = SPSLR_OK };
}

/*
 * Patch the main executable.
 *
 * Instruction pins rewrite immediate operands in text, while data pins rewrite
 * existing static objects from original layout into randomized layout.
 */

struct spslr_status __init spslr_selfpatch(void)
{
	if (patched)
		return (struct spslr_status){
			.viability = viable,
			.error = SPSLR_ERROR_ALREADY_PATCHED
		};

	if (!initialized)
		return (struct spslr_status){
			.viability = viable, .error = SPSLR_ERROR_UNINITIALIZED
		};

	viable = SPSLR_NONVIABLE;

	if (spslr_patch_dpins(spslr_dpins, spslr_dpin_cnt) < 0)
		return (struct spslr_status){
			.viability = viable, .error = SPSLR_ERROR_PATCH_DPINS
		};

	if (spslr_patch_ipins(spslr_ipins, spslr_ipin_cnt, spslr_ipin_ops,
			      spslr_ipin_op_cnt) < 0)
		return (struct spslr_status){
			.viability = viable, .error = SPSLR_ERROR_PATCH_IPINS
		};

	viable = SPSLR_VIABLE;

	patched = 1;
	return (struct spslr_status){ .viability = viable, .error = SPSLR_OK };
}

/*
 * Patch metadata belonging to a separately loaded module.
 *
 * Modules reuse the target randomization state created by the main executable;
 * they contribute only their own instruction and data patch sites.
 */

struct spslr_status spslr_patch_module(const struct spslr_module *m)
{
	if (!initialized)
		return (struct spslr_status){
			.viability = SPSLR_VIABLE,
			.error = SPSLR_ERROR_UNINITIALIZED
		};

	if (!m || !m->ipin_cnt || !m->ipins || !m->ipin_op_cnt ||
	    !m->ipin_ops || !m->dpin_cnt || !m->dpins)
		return (struct spslr_status){
			.viability = SPSLR_VIABLE,
			.error = SPSLR_ERROR_META_INCOMPLETE
		};

	spslr_u32 module_ipin_cnt = *(const spslr_u32 *)m->ipin_cnt;
	const struct spslr_ipin *module_ipins =
		(const struct spslr_ipin *)m->ipins;
	spslr_u32 module_ipin_op_cnt = *(const spslr_u32 *)m->ipin_op_cnt;
	const struct spslr_ipin_op *module_ipin_ops =
		(const struct spslr_ipin_op *)m->ipin_ops;
	spslr_u32 module_dpin_cnt = *(const spslr_u32 *)m->dpin_cnt;
	const struct spslr_dpin *module_dpins =
		(const struct spslr_dpin *)m->dpins;

	if (spslr_patch_dpins(module_dpins, module_dpin_cnt) < 0)
		return (struct spslr_status){ .viability = SPSLR_NONVIABLE,
					      .error =
						      SPSLR_ERROR_PATCH_DPINS };

	if (spslr_patch_ipins(module_ipins, module_ipin_cnt, module_ipin_ops,
			      module_ipin_op_cnt) < 0)
		return (struct spslr_status){ .viability = SPSLR_NONVIABLE,
					      .error =
						      SPSLR_ERROR_PATCH_IPINS };

	return (struct spslr_status){ .viability = SPSLR_VIABLE,
				      .error = SPSLR_OK };
}

static int __init allocate_reorder_buffer(void)
{
	if (reorder_buffer)
		return 0;

	spslr_u32 max_target_size = 0;
	for (spslr_u32 i = 0; i < spslr_target_cnt; i++) {
		if (spslr_targets[i].size > max_target_size)
			max_target_size = spslr_targets[i].size;
	}

	reorder_buffer = spslr_env_malloc(max_target_size);
	if (!reorder_buffer)
		return -1;

	return 0;
}

static int spslr_patch_dpins(const struct spslr_dpin *dpins, spslr_u32 cnt)
{
	for (spslr_u32 dpidx = 0; dpidx < cnt; dpidx++) {
		const struct spslr_dpin *dp = &dpins[dpidx];
		if (spslr_patch_dpin((void *)dp->addr, dp->target) < 0)
			return -1;
	}

	return 0;
}

/*
 * Rewrite one object instance from original layout into randomized layout.
 *
 * A temporary buffer is used so overlapping source/destination field ranges do
 * not corrupt data while fields are moved.
 */

static int reorder_object(void *dst, const void *src, spslr_u32 target)
{
	spslr_u32 field_count;
	if (spslr_randomizer_get_target(target, NULL, &field_count))
		return -1;

	const spslr_u8 *src_countable = (const spslr_u8 *)src;
	spslr_u8 *dst_countable = (spslr_u8 *)dst;

	for (spslr_u32 i = 0; i < field_count; i++) {
		struct spslr_randomizer_field_info finfo;
		if (spslr_randomizer_get_field(
			    target, i, SPSLR_RANDOMIZER_FIELD_IDX_MODE_FINAL,
			    &finfo))
			return -1;

		spslr_env_memcpy(dst_countable + finfo.offset,
				 src_countable + finfo.initial_offset,
				 finfo.size);
	}

	return 0;
}

/*
 * Apply one data pin patch.
 *
 * The address already points at an existing object in original layout. Patching
 * converts that storage in-place to the target's randomized layout.
 */

static int spslr_patch_dpin(void *addr, spslr_u32 target)
{
	if (target >= spslr_target_cnt)
		return -1;

	const struct spslr_target *t = &spslr_targets[target];

	spslr_env_memset(reorder_buffer, 0, t->size);

	if (reorder_object(reorder_buffer, addr, target) < 0)
		return -1;

	if (spslr_env_poke_data(addr, reorder_buffer, t->size) < 0)
		return -1;

	return 0;
}

static int spslr_patch_ipins(const struct spslr_ipin *ipins, spslr_u32 cnt,
			     const struct spslr_ipin_op *ipin_ops,
			     spslr_u32 op_cnt)
{
	for (spslr_u32 ipidx = 0; ipidx < cnt; ipidx++) {
		const struct spslr_ipin *ip = &ipins[ipidx];

		spslr_s64 value;
		if (spslr_calculate_ipin_value(ipin_ops, op_cnt, ip->program,
					       &value) < 0)
			return -1;

		/*
		 * Text patching is deliberately scoped to the immediate field only.
		 * The surrounding instruction bytes were fixed by pinpoint/patchcompile and
		 * must not change at runtime.
		 */

		switch (ip->size) {
		case 1:
			if (spslr_env_poke_text_8((void *)ip->addr,
						  (spslr_u8)value) < 0)
				return -1;
			break;
		case 2:
			if (spslr_env_poke_text_16((void *)ip->addr,
						   (spslr_u16)value) < 0)
				return -1;
			break;
		case 4:
			if (spslr_env_poke_text_32((void *)ip->addr,
						   (spslr_u32)value) < 0)
				return -1;
			break;
		case 8:
			if (spslr_env_poke_text_64((void *)ip->addr,
						   (spslr_u64)value) < 0)
				return -1;
			break;
		default:
			return -1;
		}
	}

	return 0;
}

/*
 * Interpret one ipin program and compute the replacement immediate value.
 *
 * The program describes original target/field references; this function maps
 * them through the randomized runtime layout and returns the value written into
 * the instruction stream.
 */

static int spslr_calculate_ipin_value(const struct spslr_ipin_op *ipin_ops,
				      spslr_u32 op_cnt, spslr_u32 start,
				      spslr_s64 *res)
{
	if (!res)
		return -1;

	*res = 0;

	spslr_u32 pc = start;
	while (1) {
		if (pc >= op_cnt)
			return -1;

		int end_flag = 0;

		const struct spslr_ipin_op *op = &ipin_ops[pc++];
		switch (op->code) {
		case SPSLR_IPIN_OP_PATCH:
			end_flag = 1;
			break;
		case SPSLR_IPIN_OP_ADD_INITIAL_OFFSET: {
			struct spslr_randomizer_field_info finfo;
			if (spslr_randomizer_get_field(
				    op->op0.add_initial_offset_target,
				    op->op1.add_initial_offset_field,
				    SPSLR_RANDOMIZER_FIELD_IDX_MODE_ORIGINAL,
				    &finfo))
				return -1;

			*res += finfo.initial_offset;
		} break;
		case SPSLR_IPIN_OP_ADD_OFFSET: {
			struct spslr_randomizer_field_info finfo;
			if (spslr_randomizer_get_field(
				    op->op0.add_offset_target,
				    op->op1.add_offset_field,
				    SPSLR_RANDOMIZER_FIELD_IDX_MODE_ORIGINAL,
				    &finfo))
				return -1;

			*res += finfo.offset;
		} break;
		case SPSLR_IPIN_OP_SUB_INITIAL_OFFSET: {
			struct spslr_randomizer_field_info finfo;
			if (spslr_randomizer_get_field(
				    op->op0.sub_initial_offset_target,
				    op->op1.sub_initial_offset_field,
				    SPSLR_RANDOMIZER_FIELD_IDX_MODE_ORIGINAL,
				    &finfo))
				return -1;

			*res -= finfo.initial_offset;
		} break;
		case SPSLR_IPIN_OP_SUB_OFFSET: {
			struct spslr_randomizer_field_info finfo;
			if (spslr_randomizer_get_field(
				    op->op0.sub_offset_target,
				    op->op1.sub_offset_field,
				    SPSLR_RANDOMIZER_FIELD_IDX_MODE_ORIGINAL,
				    &finfo))
				return -1;

			*res -= finfo.offset;
		} break;
		case SPSLR_IPIN_OP_ADD_CONST:
			*res += op->op0.add_const_value;
			break;
		default:
			return -1;
		}

		if (end_flag)
			break;
	}

	return 0;
}
