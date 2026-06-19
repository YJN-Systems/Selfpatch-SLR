#include <spslr.h>

#include "spslr_randomizer.h"
#include "spslr_env.h"
#include "pinpoint.h"

/*
 * Runtime portion of SPSLR.
 *
 * This code consumes the metadata emitted by pinpoint, randomizes target
 * layouts, rewrites static data objects, and patches instruction immediates
 * that encode structure field offsets.
 */

#define SPSLR_SANITY_CHECK

struct target_map {
	spslr_u64 *map;
	spslr_u64 size;
};

static void init_spslr_meta(void);
static int spslr_targets_compatible(const struct spslr_target *begin,
				    const struct spslr_target *end);
static enum spslr_error spslr_patch_unit(const struct spslr_unit *unit,
					 spslr_u64 *tmap_buffer,
					 void *reorder_buffer);
static struct spslr_status spslr_patch(const struct spslr_ctx *ctx);
static spslr_u64 spslr_target_mapping_size(void);
static spslr_u64 *workspace_target_mapping(void *workspace);
static void *workspace_reorder_buffer(void *workspace);

static int spslr_patch_dpins(const struct spslr_dpin *dpins, spslr_u64 cnt,
			     const struct target_map *tmap,
			     void *reorder_buffer);
static int spslr_patch_dpin(void *addr, spslr_u64 target, void *reorder_buffer);
static int spslr_patch_ipins(const struct spslr_ipin *ipins, spslr_u64 cnt,
			     const struct target_map *tmap);

static int reorder_object(void *dst, const void *src, spslr_u64 target);
static int spslr_calculate_ipin_value(const struct spslr_ipin_expr *expr,
				      spslr_s64 *res,
				      const struct target_map *tmap);

static int spslr_map_target(const struct spslr_target *target, spslr_u64 *idx);
static int spslr_map_targets(const struct spslr_unit *unit,
			     struct target_map *tmap);

static int initialized = 0, patched = 0;
static enum spslr_viability viable = SPSLR_VIABLE;

spslr_u64 spslr_target_cnt = 0;
const struct spslr_target *spslr_targets = NULL;

/* Host image spslr metadata entry point */
extern const struct spslr_unit SPSLR_START_UNITS_SYM[];
extern const struct spslr_unit SPSLR_STOP_UNITS_SYM[];
extern const struct spslr_target SPSLR_START_TARGETS_SYM[];
extern const struct spslr_target SPSLR_STOP_TARGETS_SYM[];

/*
 * Initialize runtime randomization state.
 *
 * After this point target layouts are randomized, but code/data does still
 * contain original-layout offsets until the patching entry points run.
 */

static void __init init_spslr_meta(void)
{
	spslr_target_cnt =
		(spslr_u64)(SPSLR_STOP_TARGETS_SYM - SPSLR_START_TARGETS_SYM);
	spslr_targets = SPSLR_START_TARGETS_SYM;
}

struct spslr_status __init spslr_init(void)
{
	if (initialized)
		return (struct spslr_status){ .viability = viable,
					      .error = SPSLR_OK };

	init_spslr_meta();

	if (spslr_randomizer_init() < 0)
		return (struct spslr_status){
			.viability = viable,
			.error = SPSLR_ERROR_RANDOMIZER_INIT
		};

#ifdef SPSLR_SANITY_CHECK
	for (spslr_u64 tidx = 0; tidx < spslr_target_cnt; tidx++) {
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
	for (spslr_u64 tidx = 0; tidx < spslr_target_cnt; tidx++) {
		if (spslr_randomizer_validate_target(tidx) < 0)
			return (struct spslr_status){
				.viability = viable,
				.error = SPSLR_ERROR_RANDOMIZED_TARGET_LAYOUT
			};
	}
#endif

	initialized = 1;
	return (struct spslr_status){ .viability = viable, .error = SPSLR_OK };
}

/*
 * Calculate required workspace buffer size. This includes the reorder
 * buffer for data pins and the storage for the mapping of local to
 * global target indices.
 */

static spslr_u64 spslr_target_mapping_size(void)
{
	return spslr_target_cnt * sizeof(spslr_u64);
}

unsigned long spslr_workspace_size(const struct spslr_entry *entry)
{
	if (!entry || !entry->start_units || !entry->stop_units)
		return 0;

	const struct spslr_unit *start_units =
		(const struct spslr_unit *)entry->start_units;
	const struct spslr_unit *stop_units =
		(const struct spslr_unit *)entry->stop_units;

	spslr_u64 max_dpin_size = 0;
	for (const struct spslr_unit *unit = start_units; unit != stop_units;
	     unit++) {
		for (spslr_u64 dpin = 0; dpin < unit->dpin_cnt; dpin++) {
			const struct spslr_target *target =
				unit->target_refs[unit->dpins[dpin]
							  .unit_target_idx];

			if (target->layout->size > max_dpin_size)
				max_dpin_size = target->layout->size;
		}
	}

	return spslr_target_mapping_size() + max_dpin_size;
}

/*
 * Check if the given target space is compatible with that of the
 * host.
 */

static int spslr_meta_known_target(const struct spslr_target *t)
{
	for (spslr_u64 i = 0; i < spslr_target_cnt; i++) {
		if (spslr_env_memcmp(t->hash, spslr_targets[i].hash,
				     sizeof(t->hash)) == 0)
			return 1;
	}

	return 0;
}

static int spslr_targets_compatible(const struct spslr_target *begin,
				    const struct spslr_target *end)
{
	spslr_u64 cnt = (spslr_u64)(end - begin);
	if (cnt > spslr_target_cnt)
		return 0;

	for (spslr_u64 i = 0; i < cnt; i++) {
		if (!spslr_meta_known_target(begin + i))
			return 0;
	}

	return 1;
}

/*
 * For each CU, map local target indices to global target indices and then
 * to host indices. Afterwards, patch ipins and dpins.
 */

static spslr_u64 *workspace_target_mapping(void *workspace)
{
	return (spslr_u64 *)workspace;
}

static void *workspace_reorder_buffer(void *workspace)
{
	return (spslr_u8 *)workspace + spslr_target_mapping_size();
}

static int spslr_map_target(const struct spslr_target *target, spslr_u64 *idx)
{
	for (spslr_u64 i = 0; i < spslr_target_cnt; i++) {
		if (spslr_env_memcmp(target->hash, spslr_targets[i].hash,
				     sizeof(target->hash)) == 0) {
			*idx = i;
			return 0;
		}
	}

	return -1;
}

static int spslr_map_targets(const struct spslr_unit *unit,
			     struct target_map *tmap)
{
	tmap->size = 0;

	for (spslr_u64 i = 0; i < unit->target_cnt; i++) {
		if (spslr_map_target(unit->target_refs[i], tmap->map + i) < 0)
			return -1;
	}

	tmap->size = unit->target_cnt;
	return 0;
}

static enum spslr_error spslr_patch_unit(const struct spslr_unit *unit,
					 spslr_u64 *tmap_buffer,
					 void *reorder_buffer)
{
	struct target_map tmap = { .map = tmap_buffer, .size = 0 };

	if (spslr_map_targets(unit, &tmap) < 0)
		return SPSLR_ERROR_MAP_TARGETS;

	if (spslr_patch_dpins(unit->dpins, unit->dpin_cnt, &tmap,
			      reorder_buffer) < 0)
		return SPSLR_ERROR_PATCH_DPINS;

	if (spslr_patch_ipins(unit->ipins, unit->ipin_cnt, &tmap) < 0)
		return SPSLR_ERROR_PATCH_IPINS;

	return SPSLR_OK;
}

static struct spslr_status spslr_patch(const struct spslr_ctx *ctx)
{
	enum spslr_error err = SPSLR_OK;
	enum spslr_viability via = SPSLR_VIABLE;

	spslr_u64 *target_map_buffer = NULL;
	void *reorder_buffer = NULL;

	const struct spslr_unit *start_units = NULL;
	const struct spslr_unit *stop_units = NULL;
	const struct spslr_target *start_targets = NULL;
	const struct spslr_target *stop_targets = NULL;

	if (!ctx || !ctx->entry.start_units || !ctx->entry.stop_units ||
	    !ctx->entry.start_targets || !ctx->entry.stop_targets ||
	    !ctx->workspace) {
		err = SPSLR_ERROR_INCOMPLETE_CTX;
		goto finish;
	}

	start_units = (const struct spslr_unit *)ctx->entry.start_units;
	stop_units = (const struct spslr_unit *)ctx->entry.stop_units;
	start_targets = (const struct spslr_target *)ctx->entry.start_targets;
	stop_targets = (const struct spslr_target *)ctx->entry.stop_targets;

	target_map_buffer = workspace_target_mapping(ctx->workspace);
	reorder_buffer = workspace_reorder_buffer(ctx->workspace);

	if (!spslr_targets_compatible(start_targets, stop_targets)) {
		err = SPSLR_ERROR_INCOMPATIBLE_CTX;
		goto finish;
	}

	via = SPSLR_NONVIABLE;

	for (const struct spslr_unit *unit = start_units; unit != stop_units;
	     unit++) {
		err = spslr_patch_unit(unit, target_map_buffer, reorder_buffer);
		if (err != SPSLR_OK)
			goto finish;
	}

	via = SPSLR_VIABLE;

finish:
	return (struct spslr_status){ .viability = via, .error = err };
}

/*
 * Patch the main executable.
 *
 * Instruction pins rewrite immediate operands in text, while data pins rewrite
 * existing static objects from original layout into randomized layout.
 */

struct spslr_status __init spslr_selfpatch(void)
{
	enum spslr_error err = SPSLR_OK;

	spslr_u64 host_workspace_size;

	struct spslr_ctx host_ctx;
	host_ctx.entry.start_units = SPSLR_START_UNITS_SYM;
	host_ctx.entry.stop_units = SPSLR_STOP_UNITS_SYM;
	host_ctx.entry.start_targets = SPSLR_START_TARGETS_SYM;
	host_ctx.entry.stop_targets = SPSLR_STOP_TARGETS_SYM;
	host_ctx.workspace = NULL;

	struct spslr_status internal_patch_status;

	if (patched) {
		err = SPSLR_ERROR_ALREADY_PATCHED;
		goto finish;
	}

	if (!initialized) {
		err = SPSLR_ERROR_UNINITIALIZED;
		goto finish;
	}

	host_workspace_size = spslr_workspace_size(&host_ctx.entry);
	host_ctx.workspace = spslr_env_malloc(host_workspace_size);

	if (!host_ctx.workspace) {
		err = SPSLR_ERROR_MEMORY;
		goto finish;
	}

	internal_patch_status = spslr_patch(&host_ctx);
	if (internal_patch_status.error == SPSLR_OK)
		patched = 1;

	viable = internal_patch_status.viability;
	err = internal_patch_status.error;

finish:
	if (host_ctx.workspace)
		spslr_env_free(host_ctx.workspace, host_workspace_size);

	return (struct spslr_status){ .viability = viable, .error = err };
}

/*
 * Patch metadata belonging to a separately loaded module.
 *
 * Modules reuse the target randomization state created by the main executable;
 * they contribute only their own instruction and data patch sites.
 */

struct spslr_status spslr_patch_module(const struct spslr_ctx *m)
{
	if (!initialized)
		return (struct spslr_status){
			.viability = SPSLR_VIABLE,
			.error = SPSLR_ERROR_UNINITIALIZED
		};

	if (!m || !m->entry.start_units || !m->entry.stop_units ||
	    !m->entry.start_targets || !m->entry.stop_targets || !m->workspace)
		return (struct spslr_status){
			.viability = SPSLR_VIABLE,
			.error = SPSLR_ERROR_INCOMPLETE_CTX
		};

	struct spslr_status s = spslr_patch(m);
	return (struct spslr_status){ .viability = (s.error == SPSLR_OK ?
							    SPSLR_VIABLE :
							    SPSLR_NONVIABLE),
				      .error = s.error };
}

/*
 * Rewrite one object instance from original layout into randomized layout.
 *
 * A temporary buffer is used so overlapping source/destination field ranges do
 * not corrupt data while fields are moved.
 */

static int reorder_object(void *dst, const void *src, spslr_u64 target)
{
	spslr_u64 field_count;
	if (spslr_randomizer_get_target(target, NULL, &field_count))
		return -1;

	const spslr_u8 *src_countable = (const spslr_u8 *)src;
	spslr_u8 *dst_countable = (spslr_u8 *)dst;

	for (spslr_u64 i = 0; i < field_count; i++) {
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
 * Apply data pin patches.
 *
 * Each pin's address already points at an existing object in original layout. Patching
 * converts that storage in-place to the target's randomized layout.
 */

static int spslr_patch_dpins(const struct spslr_dpin *dpins, spslr_u64 cnt,
			     const struct target_map *tmap,
			     void *reorder_buffer)
{
	for (spslr_u64 dpidx = 0; dpidx < cnt; dpidx++) {
		const struct spslr_dpin *dp = &dpins[dpidx];

		if (dp->unit_target_idx >= tmap->size)
			return -1;

		if (spslr_patch_dpin((void *)dp->addr,
				     tmap->map[dp->unit_target_idx],
				     reorder_buffer) < 0)
			return -1;
	}

	return 0;
}

static int spslr_patch_dpin(void *addr, spslr_u64 target, void *reorder_buffer)
{
	if (target >= spslr_target_cnt)
		return -1;

	const struct spslr_target *t = &spslr_targets[target];

	spslr_env_memset(reorder_buffer, 0, t->layout->size);

	if (reorder_object(reorder_buffer, addr, target) < 0)
		return -1;

	if (spslr_env_poke_data(addr, reorder_buffer, t->layout->size) < 0)
		return -1;

	return 0;
}

static int spslr_patch_ipins(const struct spslr_ipin *ipins, spslr_u64 cnt,
			     const struct target_map *tmap)
{
	for (spslr_u64 ipidx = 0; ipidx < cnt; ipidx++) {
		const struct spslr_ipin *ip = &ipins[ipidx];

		spslr_s64 value;
		if (spslr_calculate_ipin_value(ip->expr, &value, tmap) < 0)
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

static int spslr_calculate_ipin_value(const struct spslr_ipin_expr *expr,
				      spslr_s64 *res,
				      const struct target_map *tmap)
{
	if (!res)
		return -1;

	*res = 0;

	if (expr->unit_target_idx >= tmap->size)
		return -1;

	spslr_u64 global_target_idx = tmap->map[expr->unit_target_idx];

	struct spslr_randomizer_field_info finfo;
	if (spslr_randomizer_get_field(global_target_idx, expr->field_idx,
				       SPSLR_RANDOMIZER_FIELD_IDX_MODE_ORIGINAL,
				       &finfo) != 0)
		return -1;

	*res = finfo.offset;
	return 0;
}
