#include <unordered_map>

#include <pinpoint.h>
#include <passes.h>
#include <ipin_registry.h>

/*
 * gsi_replace() changes the statement, but SSA names that were defined by the
 * separator call still point at the old def statement. Retarget those SSA defs
 * so later GCC passes see the asm marker as the producer of the offset value.
 */

static void pin_update_ssa_def(function *fn, gimple *old_def, gimple *new_def)
{
	if (!fn || !old_def)
		return;

	// Stage 0 separator call was definition statement of temporary variable

	unsigned i;
	tree name;
	FOR_EACH_SSA_NAME(i, name, fn)
	{
		if (!name)
			continue;

		if (SSA_NAME_DEF_STMT(name) != old_def)
			continue;

		SSA_NAME_DEF_STMT(name) = new_def;
	}
}

static void pin_assemble_maybe(function *fn, gimple_stmt_iterator *gsi)
{
	if (!gsi)
		return;

	gimple *stmt = gsi_stmt(*gsi);
	if (!stmt)
		return;

	ipin::handle pin = ipin::identify_gimple_separator(stmt);
	if (pin == ipin::invalid)
		return;

	gimple *replacement = ipin::make_gimple_pin(gimple_call_lhs(stmt), pin);
	if (!replacement)
		pinpoint_fatal("failed to construct ipin");

	gsi_replace(gsi, replacement, true);
	pin_update_ssa_def(fn, stmt, replacement);
}

static const pass_data asm_offset_pass_data = {
	GIMPLE_PASS, "asm_offset",   OPTGROUP_NONE, TV_NONE, 0, 0, 0,
	0,	     TODO_update_ssa
};

asm_offset_pass::asm_offset_pass(gcc::context *ctxt)
	: gimple_opt_pass(asm_offset_pass_data, ctxt)
{
}

unsigned int asm_offset_pass::execute(function *fn)
{
	if (!fn)
		return 0;

	basic_block bb;
	FOR_EACH_BB_FN(bb, fn)
	{
		for (gimple_stmt_iterator gsi = gsi_start_bb(bb);
		     !gsi_end_p(gsi); gsi_next(&gsi))
			pin_assemble_maybe(fn, &gsi);
	}

	return 0;
}
