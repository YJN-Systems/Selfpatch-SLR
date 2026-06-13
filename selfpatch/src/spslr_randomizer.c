#include "spslr_randomizer.h"

#include "spslr_list_link.h"
#include "spslr_env.h"

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
	spslr_u32 offset; /* Final field offset -> fields[i].offset = offset of field i in final layout */
	spslr_u32 oidx; /* Original field idx -> fields[i].oidx = original position of field i in final layout */
	spslr_u32 fidx; /* Final field idx -> fields[i].fidx = randomized/final position of original field i */
};

static struct Field *fields;

int __init spslr_randomizer_init(void)
{
	fields = (struct Field *)spslr_env_malloc(sizeof(struct Field) *
						  spslr_target_field_cnt);
	if (!fields)
		return -1;

	for (spslr_u32 tidx = 0; tidx < spslr_target_cnt; tidx++) {
		const struct spslr_target *t = &spslr_targets[tidx];

		for (spslr_u32 fidx = 0; fidx < t->fieldcnt; fidx++) {
			spslr_u32 gfidx = t->fieldoff + fidx;

			const struct spslr_target_field *srcf =
				&spslr_target_fields[gfidx];
			struct Field *dstf = &fields[gfidx];

			dstf->offset = srcf->offset;
			dstf->oidx = fidx;
			dstf->fidx = fidx;
		}
	}

	return 0;
}

int spslr_randomizer_get_target(spslr_u32 target, spslr_u32 *size,
				spslr_u32 *fieldcnt)
{
	if (target >= spslr_target_cnt)
		return -1;

	const struct spslr_target *t = &spslr_targets[target];

	if (size)
		*size = t->size;

	if (fieldcnt)
		*fieldcnt = t->fieldcnt;

	return 0;
}

int spslr_randomizer_get_field(spslr_u32 target, spslr_u32 field,
			       int field_idx_mode,
			       struct spslr_randomizer_field_info *info)
{
	if (target >= spslr_target_cnt)
		return -1;

	if (!info)
		return 0;

	const struct spslr_target *t = &spslr_targets[target];

	if (field >= t->fieldcnt)
		return -1;

	const struct spslr_target_field *of = NULL;
	const struct Field *rf = NULL;

	switch (field_idx_mode) {
	case SPSLR_RANDOMIZER_FIELD_IDX_MODE_ORIGINAL:
		of = &spslr_target_fields[t->fieldoff + field];
		rf = &fields[t->fieldoff + fields[t->fieldoff + field].fidx];
		break;
	case SPSLR_RANDOMIZER_FIELD_IDX_MODE_FINAL:
		of = &spslr_target_fields[t->fieldoff +
					  fields[t->fieldoff + field].oidx];
		rf = &fields[t->fieldoff + field];
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

int __init spslr_randomizer_validate_target(spslr_u32 target)
{
	spslr_u32 tsize, fieldcnt;

	if (spslr_randomizer_get_target(target, &tsize, &fieldcnt) < 0)
		return -1;

	spslr_u32 cur_end = 0;

	for (spslr_u32 i = 0; i < fieldcnt; i++) {
		struct spslr_randomizer_field_info finfo;
		if (spslr_randomizer_get_field(
			    target, i, SPSLR_RANDOMIZER_FIELD_IDX_MODE_FINAL,
			    &finfo) < 0)
			return -1;

		if (finfo.offset < cur_end)
			return -1;

		if (finfo.offset % finfo.alignment != 0)
			return -1;

		if ((finfo.flags & SPSLR_FLAG_FIELD_FIXED) &&
		    finfo.offset != finfo.initial_offset)
			return -1;

		if (finfo.offset + finfo.size > tsize)
			return -1;

		cur_end = finfo.offset + finfo.size;
	}

	return 0;
}

// RANDOMIZATION CODE

struct ShuffleRegion {
	spslr_u32 begin;
	spslr_u32 end;
	spslr_u32 fill_begin;
	spslr_u32 fill_end;
};

static spslr_u32 rand_u32(void);
static struct Field *get_rfield(spslr_u32 target, spslr_u32 final_idx);
static const struct spslr_target_field *get_ofield(spslr_u32 target,
						   spslr_u32 orig_idx);
static void get_origin_region(spslr_u32 target, spslr_u32 final_idx,
			      struct ShuffleRegion *region);
static int pick_shuffle_option(spslr_u32 target, spslr_u32 origin_final_idx,
			       const struct ShuffleRegion *origin,
			       spslr_u32 alignment, spslr_u32 *selected);
static void do_swap(spslr_u32 target, spslr_u32 origin_final_idx,
		    const struct ShuffleRegion *origin_region,
		    spslr_u32 new_offset);
static void shuffle_one_target(spslr_u32 target);
static void shuffle_target(spslr_u32 target);

static spslr_u32 rand_u32(void)
{
	return spslr_env_random_u32();
}

static struct Field *__init get_rfield(spslr_u32 target, spslr_u32 final_idx)
{
	const struct spslr_target *t = &spslr_targets[target];
	return &fields[t->fieldoff + final_idx];
}

static const struct spslr_target_field *__init get_ofield(spslr_u32 target,
							  spslr_u32 orig_idx)
{
	const struct spslr_target *t = &spslr_targets[target];
	return &spslr_target_fields[t->fieldoff + orig_idx];
}

static void __init get_origin_region(spslr_u32 target, spslr_u32 final_idx,
				     struct ShuffleRegion *region)
{
	const struct spslr_target *t = &spslr_targets[target];
	const struct Field *rf = get_rfield(target, final_idx);
	const struct spslr_target_field *of = get_ofield(target, rf->oidx);

	region->fill_begin = rf->offset;
	region->fill_end = region->fill_begin + of->size;

	if (final_idx == 0) {
		region->begin = 0;
	} else {
		const struct Field *pred_rf = get_rfield(target, final_idx - 1);
		const struct spslr_target_field *pred_of =
			get_ofield(target, pred_rf->oidx);
		region->begin = pred_rf->offset + pred_of->size;
	}

	if (final_idx + 1 >= t->fieldcnt) {
		region->end = t->size;
	} else {
		const struct Field *succ_rf = get_rfield(target, final_idx + 1);
		region->end = succ_rf->offset;
	}
}

/*
 * Check whether a proposed field move preserves layout constraints.
 *
 * A move is valid only if displaced fields can be packed into the freed region
 * without violating alignment or moving fields marked fixed.
 */

static int __init option_is_valid(spslr_u32 target, spslr_u32 origin_final_idx,
				  const struct ShuffleRegion *origin,
				  spslr_u32 offset)
{
	const struct spslr_target *t = &spslr_targets[target];
	const struct spslr_target_field *origin_of =
		get_ofield(target, get_rfield(target, origin_final_idx)->oidx);

	// When placed at offset, field will occupy [offset, option_would_end)
	spslr_u32 option_would_end = offset + origin_of->size;
	if (option_would_end > t->size)
		return 0;

	// Field may overlap with origin region. Moving field to offset truly frees:
	// [true_origin_region_begin, true_origin_region_end)
	spslr_u32 true_origin_region_begin = origin->begin;
	spslr_u32 true_origin_region_end = origin->end;

	if (offset <= origin->fill_begin &&
	    option_would_end > true_origin_region_begin)
		true_origin_region_begin = option_would_end;

	if (offset >= origin->fill_begin && offset < true_origin_region_end)
		true_origin_region_end = offset;

	// Iterate over fields in target region [offset, option_would_end] and see if they fit into true origin region
	spslr_u32 origin_region_ptr = true_origin_region_begin;
	for (spslr_u32 it = 0; it < t->fieldcnt; it++) {
		const struct Field *rf = get_rfield(target, it);
		const struct spslr_target_field *of =
			get_ofield(target, rf->oidx);

		// The field being moved does not need to go into origin region
		if (it == origin_final_idx)
			continue;

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

static int __init pick_shuffle_option(spslr_u32 target,
				      spslr_u32 origin_final_idx,
				      const struct ShuffleRegion *origin,
				      spslr_u32 alignment, spslr_u32 *selected)
{
	const struct spslr_target *t = &spslr_targets[target];
	spslr_u32 seen = 0;

	/*
	Note: Instead of looping over entire field array for each option, loops can be merged into one.
	*/

	for (spslr_u32 offset = 0; offset < t->size; offset += alignment) {
		if (!option_is_valid(target, origin_final_idx, origin, offset))
			continue;

		// Reservoir sampling -> uniform distribution with O(1) memory consumption
		seen++;
		if ((rand_u32() % seen) == 0)
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

static void __init do_swap(spslr_u32 target, spslr_u32 origin_idx,
			   const struct ShuffleRegion *origin_region,
			   spslr_u32 new_offset)
{
	const struct spslr_target *t = &spslr_targets[target];
	int pulled = 0;

	spslr_u32 option_fill_end = new_offset + (origin_region->fill_end -
						  origin_region->fill_begin);

	spslr_u32 true_origin_region_begin = origin_region->begin;
	if (new_offset <= origin_region->fill_begin &&
	    option_fill_end > true_origin_region_begin)
		true_origin_region_begin = option_fill_end;

	spslr_u32 origin_oidx = get_rfield(target, origin_idx)->oidx;

	spslr_u32 origin_region_ptr = true_origin_region_begin;
	for (spslr_u32 it = 0; it < t->fieldcnt; it++) {
		struct Field *itf = get_rfield(target, it);

		if (itf->oidx == origin_oidx)
			continue;

		const struct spslr_target_field *itof =
			get_ofield(target, itf->oidx);

		if (itf->offset + itof->size <= new_offset)
			continue;

		if (itf->offset >= option_fill_end)
			break;

		spslr_u32 falign = itof->alignment;
		if (origin_region_ptr % falign != 0)
			origin_region_ptr +=
				falign - (origin_region_ptr % falign);

		if (!pulled) {
			pulled = 1;

			struct Field tmp = *get_rfield(target, it);
			*get_rfield(target, it) =
				*get_rfield(target, origin_idx);
			*get_rfield(target, origin_idx) = tmp;

			get_rfield(target, it)->offset = new_offset;

			get_rfield(target, origin_idx)->offset =
				origin_region_ptr;
			origin_region_ptr +=
				get_ofield(target,
					   get_rfield(target, origin_idx)->oidx)
					->size;
			continue;
		}

		{
			struct Field tmp = *get_rfield(target, it);

			if (origin_idx >= it) {
				for (spslr_u32 pull_it = it + 1;
				     pull_it <= origin_idx; pull_it++)
					*get_rfield(target, pull_it - 1) =
						*get_rfield(target, pull_it);

				*get_rfield(target, origin_idx) = tmp;
				get_rfield(target, origin_idx)->offset =
					origin_region_ptr;
				origin_region_ptr +=
					get_ofield(target,
						   get_rfield(target,
							      origin_idx)
							   ->oidx)
						->size;

				it--; // Must still look at the element now at it
			} else {
				for (spslr_u32 pull_it = it;
				     pull_it > origin_idx + (spslr_u32)pulled;
				     pull_it--)
					*get_rfield(target, pull_it) =
						*get_rfield(target,
							    pull_it - 1);

				*get_rfield(target,
					    origin_idx + (spslr_u32)pulled) =
					tmp;
				get_rfield(target,
					   origin_idx + (spslr_u32)pulled)
					->offset = origin_region_ptr;
				origin_region_ptr +=
					get_ofield(
						target,
						get_rfield(
							target,
							origin_idx +
								(spslr_u32)
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
		struct Field origin = *get_rfield(target, origin_idx);
		spslr_u32 insert_idx = t->fieldcnt;

		for (spslr_u32 it = 0; it < t->fieldcnt; it++) {
			if (it == origin_idx)
				continue;

			if (get_rfield(target, it)->offset >= new_offset) {
				insert_idx = it;
				break;
			}
		}

		if (insert_idx > origin_idx)
			insert_idx--;

		if (origin_idx < insert_idx) {
			for (spslr_u32 it = origin_idx + 1; it <= insert_idx;
			     it++)
				*get_rfield(target, it - 1) =
					*get_rfield(target, it);
		} else if (origin_idx > insert_idx) {
			for (spslr_u32 it = origin_idx; it > insert_idx; it--)
				*get_rfield(target, it) =
					*get_rfield(target, it - 1);
		}

		*get_rfield(target, insert_idx) = origin;
		get_rfield(target, insert_idx)->offset = new_offset;
	}

	/*
	 * Rebuild original->final mapping for this target.
	 */
	for (spslr_u32 final_idx = 0; final_idx < t->fieldcnt; final_idx++) {
		struct Field *rf = get_rfield(target, final_idx);
		fields[t->fieldoff + rf->oidx].fidx = final_idx;
	}
}

/*
Note: final version should not shuffle random fields but try to shuffle each original field idx once
*/
static void __init shuffle_one_target(spslr_u32 target)
{
	const struct spslr_target *t = &spslr_targets[target];
	if (t->fieldcnt == 0)
		return;

	spslr_u32 origin_final_idx = rand_u32() % t->fieldcnt;
	struct Field *origin_rf = get_rfield(target, origin_final_idx);
	const struct spslr_target_field *origin_of =
		get_ofield(target, origin_rf->oidx);

	if (origin_of->flags & SPSLR_FLAG_FIELD_FIXED)
		return;

	struct ShuffleRegion origin_region;
	spslr_u32 selected_option;

	get_origin_region(target, origin_final_idx, &origin_region);

	if (pick_shuffle_option(target, origin_final_idx, &origin_region,
				origin_of->alignment, &selected_option) < 0)
		return;

	do_swap(target, origin_final_idx, &origin_region, selected_option);
}

static void __init shuffle_target(spslr_u32 target)
{
	const struct spslr_target *t = &spslr_targets[target];
	spslr_u32 shuffle_count = t->fieldcnt * 2;

	for (spslr_u32 i = 0; i < shuffle_count; i++)
		shuffle_one_target(target);
}

int __init spslr_randomize(void)
{
	if (!fields)
		return -1;

	for (spslr_u32 tidx = 0; tidx < spslr_target_cnt; tidx++)
		shuffle_target(tidx);

	return 0;
}
