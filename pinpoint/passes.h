#pragma once

#include <safe-gimple.h>
#include <safe-rtl.h>

void on_register_attributes(void *plugin_data, void *user_data);
void on_finish_type(void *plugin_data, void *user_data);
void on_preserve_component_ref(void *plugin_data, void *user_data);
void on_finish_decl(void *plugin_data, void *user_data);
void on_start_unit(void *plugin_data, void *user_data);
void on_finish_unit(void *plugin_data, void *user_data);

struct separate_offset_pass : gimple_opt_pass {
	separate_offset_pass(gcc::context *ctxt);
	unsigned int execute(function *fn) override;
};

struct asm_offset_pass : gimple_opt_pass {
	asm_offset_pass(gcc::context *ctxt);
	unsigned int execute(function *fn) override;
};

struct rtl_ipin_survival_scan_pass : rtl_opt_pass {
	rtl_ipin_survival_scan_pass(gcc::context *ctxt);
	unsigned int execute(function *fn) override;
};

struct target_hash_builtin_pass : gimple_opt_pass {
	target_hash_builtin_pass(gcc::context *ctxt);
	unsigned int execute(function *fn) override;
};
