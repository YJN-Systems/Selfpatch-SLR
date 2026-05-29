#include <safe-gcc-plugin.h>
#include <safe-plugin-version.h>

#include <stage0.h>
#include <stage1.h>
#include <stage2.h>
#include <final.h>

#include <pinpoint_error.h>

int plugin_is_GPL_compatible;

bool pinpoint_verbose_enabled;

int plugin_init(struct plugin_name_args *plugin_info,
		struct plugin_gcc_version *version)
{
	if (!plugin_default_version_check(version, &gcc_version)) {
		plugin_print_early_error(
			"incompatible GCC/plugin versions: plugin built for GCC %s, loaded by GCC %s",
			gcc_version.basever, version->basever);
		return 1;
	}

	pinpoint_verbose_enabled = false;

	for (int i = 0; i < plugin_info->argc; ++i) {
		if (!strcmp(plugin_info->argv[i].key, "out"))
			set_output_file(plugin_info->argv[i].value);
		else if (!strcmp(plugin_info->argv[i].key, "verbose"))
			pinpoint_verbose_enabled = true;
	}

	if (!has_output_file()) {
		plugin_print_early_error("missing output file argument");
		return 1;
	}

	/*
	 * Pinpoint runs as a staged GCC plugin because no single GCC IR level has
	 * all information SPSLR needs.
	 *
	 * Stage 0 runs while COMPONENT_REF trees are built or still available and records
	 * which structure field offsets are randomization-sensitive.
	 *
	 * Stage 1 replaces synthetic separator calls with asm markers so GCC keeps
	 * the offset value as a real data dependency through later optimization.
	 *
	 * Stage 2 runs on final RTL and lowers those markers into concrete architecture-specific
	 * instructions whose immediate operands are labeled for runtime patching.
	 *
	 * The final callback emits the collected metadata for patchcompile.
	 */

	register_callback(plugin_info->base_name, PLUGIN_START_UNIT,
			  on_start_unit, NULL);
	register_callback(plugin_info->base_name, PLUGIN_ATTRIBUTES,
			  on_register_attributes, NULL);
	register_callback(plugin_info->base_name, PLUGIN_FINISH_TYPE,
			  on_finish_type, NULL);
	register_callback(plugin_info->base_name, PLUGIN_BUILD_COMPONENT_REF,
			  on_preserve_component_ref, NULL);
	register_callback(plugin_info->base_name, PLUGIN_FINISH_DECL,
			  on_finish_decl, NULL);

	struct register_pass_info separate_offset_pass_info;
	separate_offset_pass_info.pass = new separate_offset_pass(nullptr);
	separate_offset_pass_info.ref_pass_instance_number = 1;
	separate_offset_pass_info.reference_pass_name = "cfg";
	separate_offset_pass_info.pos_op = PASS_POS_INSERT_AFTER;
	register_callback(plugin_info->base_name, PLUGIN_PASS_MANAGER_SETUP,
			  nullptr, &separate_offset_pass_info);

	struct register_pass_info asm_offset_pass_info;
	asm_offset_pass_info.pass = new asm_offset_pass(nullptr);
	asm_offset_pass_info.ref_pass_instance_number = 1;
	asm_offset_pass_info.reference_pass_name = "separate_offset";
	asm_offset_pass_info.pos_op = PASS_POS_INSERT_AFTER;
	register_callback(plugin_info->base_name, PLUGIN_PASS_MANAGER_SETUP,
			  nullptr, &asm_offset_pass_info);

	struct register_pass_info rtl_pin_lower_pass_info;
	rtl_pin_lower_pass_info.pass = new rtl_pin_lower_pass(nullptr);
	rtl_pin_lower_pass_info.ref_pass_instance_number = 1;
	rtl_pin_lower_pass_info.reference_pass_name = "final";
	rtl_pin_lower_pass_info.pos_op = PASS_POS_INSERT_BEFORE;
	register_callback(plugin_info->base_name, PLUGIN_PASS_MANAGER_SETUP,
			  nullptr, &rtl_pin_lower_pass_info);

	register_callback(plugin_info->base_name, PLUGIN_FINISH_UNIT,
			  on_finish_unit, NULL);

	return 0;
}
