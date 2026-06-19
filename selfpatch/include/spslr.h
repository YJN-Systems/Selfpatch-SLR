#ifndef SPSLR_SELFPATCH_H
#define SPSLR_SELFPATCH_H

#define SPSLR_START_UNITS_SYM __start_spslr_units
#define SPSLR_STOP_UNITS_SYM __stop_spslr_units
#define SPSLR_START_TARGETS_SYM __start_spslr_targets
#define SPSLR_STOP_TARGETS_SYM __stop_spslr_targets

enum spslr_viability { SPSLR_VIABLE, SPSLR_NONVIABLE };

enum spslr_error {
	SPSLR_OK,
	SPSLR_ERROR_INCOMPLETE_CTX,
	SPSLR_ERROR_INCOMPATIBLE_CTX,
	SPSLR_ERROR_RANDOMIZER_INIT,
	SPSLR_ERROR_INITIAL_TARGET_LAYOUT,
	SPSLR_ERROR_RANDOMIZED_TARGET_LAYOUT,
	SPSLR_ERROR_RANDOMIZE,
	SPSLR_ERROR_MEMORY,
	SPSLR_ERROR_UNINITIALIZED,
	SPSLR_ERROR_ALREADY_PATCHED,
	SPSLR_ERROR_PATCH_DPINS,
	SPSLR_ERROR_PATCH_IPINS,
	SPSLR_ERROR_MAP_TARGETS
};

struct spslr_status {
	enum spslr_viability viability;
	enum spslr_error error;
};

struct spslr_entry {
	const void *start_units; // Address of SPSLR_START_UNITS_SYM
	const void *stop_units; // Address of SPSLR_STOP_UNITS_SYM
	const void *start_targets; // Address of SPSLR_START_TARGETS_SYM
	const void *stop_targets; // Address of SPSLR_STOP_TARGETS_SYM
};

struct spslr_ctx {
	struct spslr_entry entry;
	void *workspace; // Temporary buffer of spslr_workspace_size(&entry) bytes
};

/*
 * Runtime entry points are intentionally split:
 *
 * spslr_init() creates randomized layouts.
 * spslr_selfpatch() patches the main executable.
 * spslr_patch_module() patches with module-local metadata using host layouts.
 */

struct spslr_status spslr_init(void);
struct spslr_status spslr_selfpatch(void);
unsigned long spslr_workspace_size(const struct spslr_entry *entry);
struct spslr_status spslr_patch_module(const struct spslr_ctx *m);

#endif
