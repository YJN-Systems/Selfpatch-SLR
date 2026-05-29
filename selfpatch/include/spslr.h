#ifndef SPSLR_SELFPATCH_H
#define SPSLR_SELFPATCH_H

#define SPSLR_MODULE_SYM_IPIN_CNT "spslr_ipin_cnt"
#define SPSLR_MODULE_SYM_IPINS "spslr_ipins"
#define SPSLR_MODULE_SYM_IPIN_OP_CNT "spslr_ipin_op_cnt"
#define SPSLR_MODULE_SYM_IPIN_OPS "spslr_ipin_ops"
#define SPSLR_MODULE_SYM_DPIN_CNT "spslr_dpin_cnt"
#define SPSLR_MODULE_SYM_DPINS "spslr_dpins"

enum spslr_viability { SPSLR_VIABLE, SPSLR_NONVIABLE };

enum spslr_error {
	SPSLR_OK,
	SPSLR_ERROR_RANDOMIZER_INIT,
	SPSLR_ERROR_INITIAL_TARGET_LAYOUT,
	SPSLR_ERROR_RANDOMIZED_TARGET_LAYOUT,
	SPSLR_ERROR_RANDOMIZE,
	SPSLR_ERROR_REORDER_BUFFER,
	SPSLR_ERROR_UNINITIALIZED,
	SPSLR_ERROR_ALREADY_PATCHED,
	SPSLR_ERROR_PATCH_DPINS,
	SPSLR_ERROR_PATCH_IPINS,
	SPSLR_ERROR_META_INCOMPLETE
};

struct spslr_status {
	enum spslr_viability viability;
	enum spslr_error error;
};

struct spslr_module {
	const void *ipin_cnt;
	const void *ipins;
	const void *ipin_op_cnt;
	const void *ipin_ops;
	const void *dpin_cnt;
	const void *dpins;
};

/*
 * Runtime entry points are intentionally split:
 *
 * spslr_init() creates randomized layouts.
 * spslr_selfpatch() patches the main executable.
 * spslr_patch_module() patches module-local metadata using existing layouts.
 */

struct spslr_status spslr_init(void);
struct spslr_status spslr_selfpatch(void);
struct spslr_status spslr_patch_module(const struct spslr_module *m);

#endif
