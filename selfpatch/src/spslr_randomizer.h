#ifndef SPSLR_RANDOMIZER_H
#define SPSLR_RANDOMIZER_H

#include "spslr_env.h"
#include "pinpoint.h"

#define SPSLR_RANDOMIZER_FIELD_IDX_MODE_ORIGINAL 1
#define SPSLR_RANDOMIZER_FIELD_IDX_MODE_FINAL 2

struct spslr_randomizer_field_info {
	spslr_u64 size;
	spslr_u64 offset;
	spslr_u64 initial_offset;
	spslr_u64 alignment;
	spslr_u64 flags;
};

int spslr_randomizer_init(void);
int spslr_randomize(void);

int spslr_randomizer_get_target(spslr_u64 target, spslr_u64 *size,
				spslr_u64 *fieldcnt);
int spslr_randomizer_get_field(spslr_u64 target, spslr_u64 field,
			       int field_idx_mode,
			       struct spslr_randomizer_field_info *info);

int spslr_randomizer_validate_target(spslr_u64 target);

#endif
