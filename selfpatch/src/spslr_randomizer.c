#include "spslr_randomizer.h"

#include "spslr_env.h"
#include "pinpoint.h"

#include <sanemaker/traps.h>

/*
 * Target layout randomizer.
 *
 * The randomizer builds a permutation from original field order to randomized
 * field order while preserving field size, alignment, and fixed-field
 * constraints.
 */

/*
 * Field tracks both directions of the permutation:
 *
 *   original index -> randomized position
 *   randomized position -> original index
 *
 * The runtime needs both: data patching copies from original offsets to new
 * offsets, while ipin patching maps an original field offset to its randomized
 * offset.
 */
struct Field {
	spslr_u64 offset; /* Final field offset -> fields[i].offset = offset of field i in final layout */
	spslr_u64 oidx; /* Original field idx -> fields[i].oidx = original position of field i in final layout */
	spslr_u64 fidx; /* Final field idx -> fields[i].fidx = randomized/final position of original field i */
};

extern spslr_u64 spslr_target_cnt;
extern const struct spslr_target *spslr_targets;

static spslr_u64 *field_base_indices = NULL;
static struct Field *fields = NULL;

static int init_field_base_indices(void);
static int init_fields_buffer(void);

static const struct spslr_target_field *meta_original_field(spslr_u64 target,
							    spslr_u64 field);
static struct Field *state_current_field_base(spslr_u64 target);
static struct Field *state_current_field(spslr_u64 target, spslr_u64 field);

static int __init init_field_base_indices(void)
{
	field_base_indices = (spslr_u64 *)spslr_env_malloc(sizeof(spslr_u64) *
							   spslr_target_cnt);
	if (!field_base_indices)
		return -1;

	spslr_u64 current_field_base_idx = 0;
	for (spslr_u64 i = 0; i < spslr_target_cnt; i++) {
		field_base_indices[i] = current_field_base_idx;
		current_field_base_idx += spslr_targets[i].layout->field_cnt;
	}

	return 0;
}

static const struct spslr_target_field *meta_original_field(spslr_u64 target,
							    spslr_u64 field)
{
	if (target >= spslr_target_cnt)
		return NULL;

	const struct spslr_target_layout *layout = spslr_targets[target].layout;

	if (field >= layout->field_cnt)
		return NULL;

	return layout->fields + field;
}

static struct Field *state_current_field_base(spslr_u64 target)
{
	if (target >= spslr_target_cnt)
		return NULL;

	spslr_u64 field_base_idx = field_base_indices[target];
	return fields + field_base_idx;
}

static struct Field *state_current_field(spslr_u64 target, spslr_u64 field)
{
	if (target >= spslr_target_cnt)
		return NULL;

	struct Field *field_base = state_current_field_base(target);
	const struct spslr_target_layout *layout = spslr_targets[target].layout;

	if (!field_base || field >= layout->field_cnt)
		return NULL;

	return field_base + field;
}

static int __init init_fields_buffer(void)
{
	spslr_u64 total_field_count = 0;
	for (spslr_u64 i = 0; i < spslr_target_cnt; i++)
		total_field_count += spslr_targets[i].layout->field_cnt;

	fields = (struct Field *)spslr_env_malloc(sizeof(struct Field) *
						  total_field_count);
	if (!fields)
		return -1;

	for (spslr_u64 i = 0; i < spslr_target_cnt; i++) {
		spslr_u64 field_base_idx = field_base_indices[i];

		for (spslr_u64 field_idx = 0;
		     field_idx < spslr_targets[i].layout->field_cnt;
		     field_idx++) {
			const struct spslr_target_field *src_field =
				spslr_targets[i].layout->fields + field_idx;
			struct Field *dst_field =
				&fields[field_base_idx + field_idx];

			dst_field->offset = src_field->offset;
			dst_field->oidx = field_idx;
			dst_field->fidx = field_idx;
		}
	}

	return 0;
}

int __init spslr_randomizer_init(void)
{
	if (init_field_base_indices() != 0)
		return -1;

	if (init_fields_buffer() != 0)
		return -1;

	return 0;
}

int spslr_randomizer_get_target(spslr_u64 target, spslr_u64 *size,
				spslr_u64 *fieldcnt)
{
	if (target >= spslr_target_cnt)
		return -1;

	const struct spslr_target *t = &spslr_targets[target];

	if (size)
		*size = t->layout->size;

	if (fieldcnt)
		*fieldcnt = t->layout->field_cnt;

	return 0;
}

int spslr_randomizer_get_field(spslr_u64 target, spslr_u64 field,
			       int field_idx_mode,
			       struct spslr_randomizer_field_info *info)
{
	if (target >= spslr_target_cnt)
		return -1;

	if (!info)
		return 0;

	const struct spslr_target *t = &spslr_targets[target];

	if (field >= t->layout->field_cnt)
		return -1;

	const struct spslr_target_field *of = NULL;
	const struct Field *rf = NULL;

	switch (field_idx_mode) {
	case SPSLR_RANDOMIZER_FIELD_IDX_MODE_ORIGINAL:
		of = meta_original_field(target, field);
		rf = state_current_field(
			target, state_current_field(target, field)->fidx);
		break;
	case SPSLR_RANDOMIZER_FIELD_IDX_MODE_FINAL:
		of = meta_original_field(
			target, state_current_field(target, field)->oidx);
		rf = state_current_field(target, field);
		break;
	default:
		return -1;
	}

	info->size = of->size;
	info->offset = rf->offset;
	info->initial_offset = of->offset;
	info->alignment = of->alignment;
	info->flags = of->flags;

	return 0;
}

int __init spslr_randomizer_validate_target(spslr_u64 target)
{
	spslr_u64 tsize, fieldcnt;

	if (spslr_randomizer_get_target(target, &tsize, &fieldcnt) < 0)
		return -1;

	spslr_u64 cur_end = 0;

	for (spslr_u64 i = 0; i < fieldcnt; i++) {
		struct spslr_randomizer_field_info finfo;
		if (spslr_randomizer_get_field(
			    target, i, SPSLR_RANDOMIZER_FIELD_IDX_MODE_FINAL,
			    &finfo) < 0)
			return -1;

		if (finfo.alignment == 0)
			return -1;

		if (finfo.offset % finfo.alignment != 0)
			return -1;

		if ((finfo.flags & SPSLR_FLAG_FIELD_FIXED) &&
		    finfo.offset != finfo.initial_offset)
			return -1;

		if (finfo.offset > tsize)
			return -1;

		/* Zero-sized metadata entries occupy no storage. */
		if (finfo.size == 0)
			continue;

		if (finfo.offset < cur_end)
			return -1;

		/* Avoid overflow in offset + size. */
		if (finfo.size > tsize - finfo.offset)
			return -1;

		cur_end = finfo.offset + finfo.size;
	}

	return 0;
}

// RANDOMIZATION CODE

struct ShuffleRegion {
	spslr_u64 begin;
	spslr_u64 end;
	spslr_u64 fill_begin;
	spslr_u64 fill_end;
};

static spslr_u64 rand_u64(void);
static void get_origin_region(spslr_u64 target, spslr_u64 final_idx,
			      struct ShuffleRegion *region);
static int option_is_valid(spslr_u64 target, spslr_u64 origin_final_idx,
			   const struct ShuffleRegion *origin,
			   spslr_u64 offset);
static int pick_shuffle_option(spslr_u64 target, spslr_u64 origin_final_idx,
			       const struct ShuffleRegion *origin,
			       spslr_u64 alignment, spslr_u64 *selected);
static void do_swap(spslr_u64 target, spslr_u64 origin_final_idx,
		    const struct ShuffleRegion *origin_region,
		    spslr_u64 new_offset);
static void shuffle_one_target(spslr_u64 target);
static void shuffle_target(spslr_u64 target);

static spslr_u64 __init rand_u64(void)
{
	return spslr_env_random_u64();
}

static void __init get_origin_region(spslr_u64 target, spslr_u64 final_idx,
				     struct ShuffleRegion *region)
{
	const struct spslr_target *t = &spslr_targets[target];
	const struct Field *rf = state_current_field(target, final_idx);
	const struct spslr_target_field *of =
		meta_original_field(target, rf->oidx);

	region->fill_begin = rf->offset;
	region->fill_end = region->fill_begin + of->size;

	if (final_idx == 0) {
		region->begin = 0;
	} else {
		const struct Field *pred_rf =
			state_current_field(target, final_idx - 1);
		const struct spslr_target_field *pred_of =
			meta_original_field(target, pred_rf->oidx);
		region->begin = pred_rf->offset + pred_of->size;
	}

	if (final_idx + 1 >= t->layout->field_cnt) {
		region->end = t->layout->size;
	} else {
		const struct Field *succ_rf =
			state_current_field(target, final_idx + 1);
		region->end = succ_rf->offset;
	}
}

/*
 * Check whether a proposed field move preserves layout constraints.
 *
 * A move is valid only if displaced fields can be packed into the freed region
 * without violating alignment or moving fields marked fixed.
 */

static int __init option_is_valid(spslr_u64 target, spslr_u64 origin_final_idx,
				  const struct ShuffleRegion *origin,
				  spslr_u64 offset)
{
	const struct spslr_target *t = &spslr_targets[target];
	const struct spslr_target_field *origin_of = meta_original_field(
		target, state_current_field(target, origin_final_idx)->oidx);

	// When placed at offset, field will occupy [offset, option_would_end)
	spslr_u64 option_would_end = offset + origin_of->size;
	if (option_would_end > t->layout->size)
		return 0;

	// Field may overlap with origin region. Moving field to offset truly frees:
	// [true_origin_region_begin, true_origin_region_end)
	spslr_u64 true_origin_region_begin = origin->begin;
	spslr_u64 true_origin_region_end = origin->end;

	if (offset <= origin->fill_begin &&
	    option_would_end > true_origin_region_begin)
		true_origin_region_begin = option_would_end;

	if (offset >= origin->fill_begin && offset < true_origin_region_end)
		true_origin_region_end = offset;

	// Iterate over fields in target region [offset, option_would_end] and see if they fit into true origin region
	spslr_u64 origin_region_ptr = true_origin_region_begin;
	for (spslr_u64 it = 0; it < t->layout->field_cnt; it++) {
		const struct Field *rf = state_current_field(target, it);
		const struct spslr_target_field *of =
			meta_original_field(target, rf->oidx);

		// The field being moved does not need to go into origin region
		if (it == origin_final_idx)
			continue;

		/*
		 * Zero-sized metadata entries occupy no storage, but they still
		 * mark an ordering boundary. A moved field must neither straddle
		 * one nor start at one: equal-offset entries have an ordering
		 * relationship that do_swap() does not preserve while displacing
		 * fields.
         */
		if (of->size == 0) {
			if (rf->offset >= offset &&
			    rf->offset < option_would_end)
				return 0;

			continue;
		}

		// Field ends before target region -> must not be moved to origin region
		if (rf->offset + of->size <= offset)
			continue;

		// Field starts after target region -> must not be moved to origin region
		if (rf->offset >= option_would_end)
			break;

		// Fixed fields in target region unconditionally deny option
		if (of->flags & SPSLR_FLAG_FIELD_FIXED)
			return 0;

		// Field from target region must be moved to aligned position in origin region
		if (origin_region_ptr % of->alignment != 0)
			origin_region_ptr +=
				of->alignment -
				(origin_region_ptr % of->alignment);

		origin_region_ptr += of->size;

		// Field does not fit into origin region -> option not possible
		if (origin_region_ptr > true_origin_region_end)
			return 0;
	}

	return 1;
}

static int __init pick_shuffle_option(spslr_u64 target,
				      spslr_u64 origin_final_idx,
				      const struct ShuffleRegion *origin,
				      spslr_u64 alignment, spslr_u64 *selected)
{
	const struct spslr_target *t = &spslr_targets[target];
	spslr_u64 seen = 0;

	/*
	Note: Instead of looping over entire field array for each option, loops can be merged into one.
	*/

	for (spslr_u64 offset = 0; offset < t->layout->size;
	     offset += alignment) {
		if (!option_is_valid(target, origin_final_idx, origin, offset))
			continue;

		// Reservoir sampling -> uniform distribution with O(1) memory consumption
		seen++;
		if ((rand_u64() % seen) == 0)
			*selected = offset;
	}

	return seen ? 0 : -1;
}

/*
 * Move one field into a new slot and repack the fields it displaced.
 *
 * This is not a simple pairwise swap: structure layout has byte ranges and
 * alignment holes, so the displaced region may contain several fields.
 */

static void __init do_swap(spslr_u64 target, spslr_u64 origin_idx,
			   const struct ShuffleRegion *origin_region,
			   spslr_u64 new_offset)
{
	const struct spslr_target *t = &spslr_targets[target];
	int pulled = 0;

	spslr_u64 option_fill_end = new_offset + (origin_region->fill_end -
						  origin_region->fill_begin);

	spslr_u64 true_origin_region_begin = origin_region->begin;
	if (new_offset <= origin_region->fill_begin &&
	    option_fill_end > true_origin_region_begin)
		true_origin_region_begin = option_fill_end;

	spslr_u64 origin_oidx = state_current_field(target, origin_idx)->oidx;

	spslr_u64 origin_region_ptr = true_origin_region_begin;
	for (spslr_u64 it = 0; it < t->layout->field_cnt; it++) {
		struct Field *itf = state_current_field(target, it);

		if (itf->oidx == origin_oidx)
			continue;

		const struct spslr_target_field *itof =
			meta_original_field(target, itf->oidx);

		// Zero-sized metadata entries occupy no storage.
		if (itof->size == 0)
			continue;

		if (itf->offset + itof->size <= new_offset)
			continue;

		if (itf->offset >= option_fill_end)
			break;

		spslr_u64 falign = itof->alignment;
		if (origin_region_ptr % falign != 0)
			origin_region_ptr +=
				falign - (origin_region_ptr % falign);

		if (!pulled) {
			pulled = 1;

			struct Field tmp = *state_current_field(target, it);
			*state_current_field(target, it) =
				*state_current_field(target, origin_idx);
			*state_current_field(target, origin_idx) = tmp;

			state_current_field(target, it)->offset = new_offset;

			state_current_field(target, origin_idx)->offset =
				origin_region_ptr;
			origin_region_ptr +=
				meta_original_field(
					target,
					state_current_field(target, origin_idx)
						->oidx)
					->size;
			continue;
		}

		{
			struct Field tmp = *state_current_field(target, it);

			if (origin_idx >= it) {
				for (spslr_u64 pull_it = it + 1;
				     pull_it <= origin_idx; pull_it++)
					*state_current_field(target,
							     pull_it - 1) =
						*state_current_field(target,
								     pull_it);

				*state_current_field(target, origin_idx) = tmp;
				state_current_field(target, origin_idx)->offset =
					origin_region_ptr;
				origin_region_ptr +=
					meta_original_field(
						target,
						state_current_field(target,
								    origin_idx)
							->oidx)
						->size;

				it--; // Must still look at the element now at it
			} else {
				for (spslr_u64 pull_it = it;
				     pull_it > origin_idx + (spslr_u64)pulled;
				     pull_it--)
					*state_current_field(target, pull_it) =
						*state_current_field(
							target, pull_it - 1);

				*state_current_field(
					target,
					origin_idx + (spslr_u64)pulled) = tmp;
				state_current_field(
					target, origin_idx + (spslr_u64)pulled)
					->offset = origin_region_ptr;
				origin_region_ptr +=
					meta_original_field(
						target,
						state_current_field(
							target,
							origin_idx +
								(spslr_u64)
									pulled)
							->oidx)
						->size;
			}
		}

		pulled++;
	}

	/*
	 * The selected destination may not overlap any other field. It may be an
	 * empty padding gap, or it may partially overlap the origin field itself
	 * when the field slides into adjacent padding.
	 *
	 * In either case the loop above never displaces another field and
	 * `pulled` remains zero. We still need to update the origin field's
	 * offset and reinsert it at the correct position in final-offset order so
	 * that the field array remains sorted.
	 */
	if (!pulled) {
		struct Field origin = *state_current_field(target, origin_idx);
		spslr_u64 insert_idx = t->layout->field_cnt;

		for (spslr_u64 it = 0; it < t->layout->field_cnt; it++) {
			if (it == origin_idx)
				continue;

			if (state_current_field(target, it)->offset >=
			    new_offset) {
				insert_idx = it;
				break;
			}
		}

		if (insert_idx > origin_idx)
			insert_idx--;

		if (origin_idx < insert_idx) {
			for (spslr_u64 it = origin_idx + 1; it <= insert_idx;
			     it++)
				*state_current_field(target, it - 1) =
					*state_current_field(target, it);
		} else if (origin_idx > insert_idx) {
			for (spslr_u64 it = origin_idx; it > insert_idx; it--)
				*state_current_field(target, it) =
					*state_current_field(target, it - 1);
		}

		*state_current_field(target, insert_idx) = origin;
		state_current_field(target, insert_idx)->offset = new_offset;
	}

	/*
	 * Rebuild original->final mapping for this target.
	 */
	for (spslr_u64 final_idx = 0; final_idx < t->layout->field_cnt;
	     final_idx++) {
		struct Field *rf = state_current_field(target, final_idx);
		state_current_field(target, rf->oidx)->fidx = final_idx;
	}
}

/*
Note: final version should not shuffle random fields but try to shuffle each original field idx once
*/
static void __init shuffle_one_target(spslr_u64 target)
{
	const struct spslr_target *t = &spslr_targets[target];
	if (t->layout->field_cnt == 0)
		return;

	spslr_u64 origin_final_idx = rand_u64() % t->layout->field_cnt;
	struct Field *origin_rf = state_current_field(target, origin_final_idx);
	const struct spslr_target_field *origin_of =
		meta_original_field(target, origin_rf->oidx);

	/* Zero-sized entries are metadata markers, not shuffleable storage. */
	if (origin_of->size == 0)
		return;

	if (origin_of->flags & SPSLR_FLAG_FIELD_FIXED)
		return;

	struct ShuffleRegion origin_region;
	spslr_u64 selected_option;

	get_origin_region(target, origin_final_idx, &origin_region);

	if (pick_shuffle_option(target, origin_final_idx, &origin_region,
				origin_of->alignment, &selected_option) < 0)
		return;

	do_swap(target, origin_final_idx, &origin_region, selected_option);
}

static void __init shuffle_target(spslr_u64 target)
{
	const struct spslr_target *t = &spslr_targets[target];
	spslr_u64 shuffle_count = t->layout->field_cnt * 2;

	for (spslr_u64 i = 0; i < shuffle_count; i++)
		shuffle_one_target(target);

	sanemaker_finish_layout(state_current_field_base(target), t->hash);
}

int __init spslr_randomize(void)
{
	if (!fields)
		return -1;

	for (spslr_u64 tidx = 0; tidx < spslr_target_cnt; tidx++)
		shuffle_target(tidx);

	return 0;
}
