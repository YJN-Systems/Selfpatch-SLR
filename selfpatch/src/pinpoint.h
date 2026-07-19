#ifndef SPSLR_PINPOINT_H
#define SPSLR_PINPOINT_H

#include "spslr_env.h"

/* Field must remain at its original offset during layout randomization. */
#define SPSLR_FLAG_FIELD_FIXED 1

struct spslr_unit;
struct spslr_ipin;
struct spslr_ipin_expr;
struct spslr_dpin;
struct spslr_target;
struct spslr_target_layout;
struct spslr_target_field;

/* CU-local target reference; points into the global deduplicated target table. */
typedef const struct spslr_target *spslr_target_ref;

/*
 * Metadata for one compilation unit. The target array is CU-local and maps
 * unit_target_idx values used by pins to deduplicated global target headers.
 */
struct spslr_unit {
	const char *source; // Source file name
	spslr_u64 target_cnt;
	const spslr_target_ref *target_refs; // CU-local target ref array
	spslr_u64 ipin_cnt;
	const struct spslr_ipin *ipins;
	spslr_u64 dpin_cnt;
	const struct spslr_dpin *dpins;
} __packed;

/* Instruction patch site: address of patchable immediate/displacement bytes. */
struct spslr_ipin {
	void *addr;
	spslr_u64 size;
	const struct spslr_ipin_expr *expr;
} __packed;

/* Data patch site: address of an object/subobject whose layout must be adjusted. */
struct spslr_dpin {
	void *addr;
	spslr_u64 unit_target_idx;
} __packed;

/* Current simple expression: randomized offset of one field in one CU-local target. */
struct spslr_ipin_expr {
	spslr_u64 unit_target_idx;
	spslr_u64 field_idx;
} __packed;

/* Deduplicated target type descriptor, keyed by deterministic layout hash. */
struct spslr_target {
	unsigned char hash[16];
	const char *name;
	const struct spslr_target_layout *layout;
} __packed;

/* Physical layout of a target type before runtime randomization. */
struct spslr_target_layout {
	spslr_u64 size;
	spslr_u64 field_cnt;
	const struct spslr_target_field *fields;
} __packed;

/* One randomizable or fixed field/range within a target layout. */
struct spslr_target_field {
	const char *name;
	spslr_u64 size;
	spslr_u64 offset;
	spslr_u64 alignment;
	spslr_u64 flags;
} __packed;

#endif
