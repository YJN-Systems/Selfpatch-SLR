#ifndef SPSLR_LIST_H
#define SPSLR_LIST_H

#include "spslr_env.h"

#define SPSLR_IPIN_OP_PATCH 1
#define SPSLR_IPIN_OP_ADD_INITIAL_OFFSET 2
#define SPSLR_IPIN_OP_SUB_INITIAL_OFFSET 3
#define SPSLR_IPIN_OP_ADD_OFFSET 4
#define SPSLR_IPIN_OP_SUB_OFFSET 5
#define SPSLR_IPIN_OP_ADD_CONST 6

#define SPSLR_FLAG_FIELD_FIXED 1

struct spslr_target {
	spslr_u32 size;
	spslr_u32 fieldcnt;
	spslr_u32 fieldoff; // Offset into spslr_target_field array
} __packed;

struct spslr_target_field {
	spslr_u32 offset;
	spslr_u32 size;
	spslr_u32 alignment;
	spslr_u32 flags;
} __packed;

struct spslr_ipin {
	spslr_u64 addr;
	spslr_u32 size;
	spslr_u32 program; // Index in spslr_ipin_op array
} __packed;

struct spslr_ipin_op {
	spslr_u32 code;

	union {
		spslr_u32 patch_unused;
		spslr_u32 add_initial_offset_target;
		spslr_u32 sub_initial_offset_target;
		spslr_u32 add_offset_target;
		spslr_u32 sub_offset_target;
		spslr_s32 add_const_value;
	} op0;

	union {
		spslr_u32 patch_unused;
		spslr_u32 add_initial_offset_field;
		spslr_u32 sub_initial_offset_field;
		spslr_u32 add_offset_field;
		spslr_u32 sub_offset_field;
		spslr_u32 add_const_unused;
	} op1;
} __packed;

struct spslr_dpin {
	spslr_u64 addr;
	spslr_u32 target; // Index in spslr_target array
} __packed;

#undef __packed

#endif
