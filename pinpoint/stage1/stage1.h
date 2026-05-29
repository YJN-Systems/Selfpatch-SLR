#pragma once

#include <stage0.h>
#include <safe-gimple.h>

extern const char *s1_ipin_marker;

struct asm_offset_pass : gimple_opt_pass {
	asm_offset_pass(gcc::context *ctxt);
	unsigned int execute(function *fn) override;
};
