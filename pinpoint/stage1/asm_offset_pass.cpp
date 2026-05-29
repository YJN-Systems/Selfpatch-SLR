#include <stage1.h>
#include <unordered_map>

#include <pinpoint_error.h>
#include <pinpoint_config.h>

/*
 * Stage1 marker asm is a carrier for target/offset constants.
 *
 * It intentionally does not contain final machine code yet. GCC still needs
 * freedom to allocate the destination register, and stage2 later observes the
 * final RTL register choice before emitting bytes with a labeled immediate.
 */

const char *s1_ipin_marker = "/*spslr_s1_ipin_marker*/";

static tree make_asm_operand(const char *constraint_text, tree operand_tree)
{
	tree constraint_str =
		build_string(strlen(constraint_text) + 1, constraint_text);
	tree inner_list = build_tree_list(integer_zero_node, constraint_str);
	tree outer_list = build_tree_list(inner_list, operand_tree);
	return outer_list;
}

static gimple *make_stage1_pin(tree lhs, UID target, std::size_t offset)
{
	if (!lhs)
		return nullptr;

	const char *asm_str =
		s1_ipin_marker; // Asm is inserted at a later stage

	tree arg0 = build_int_cst(size_type_node, target);
	tree arg1 = build_int_cst(size_type_node, offset);

	vec<tree, va_gc> *outputs = NULL;
	vec<tree, va_gc> *inputs = NULL;

	vec_safe_push(outputs, make_asm_operand("=r", lhs));
	vec_safe_push(inputs, make_asm_operand("i", arg0));
	vec_safe_push(inputs, make_asm_operand("i", arg1));

	gasm *new_gasm = gimple_build_asm_vec(ggc_strdup(asm_str), inputs,
					      outputs, NULL, NULL);
	if (!new_gasm)
		return nullptr;

	/*
	 * Non-volatile is intentional: unused field-offset computations should die
	 * normally. Only offsets that survive optimization become instruction pins.
	 */
	gimple_asm_set_volatile(new_gasm, false);

	return new_gasm;
}

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

	UID target;
	std::size_t offset;

	if (!is_stage0_separator(stmt, target, offset))
		return;

	gimple *replacement =
		make_stage1_pin(gimple_call_lhs(stmt), target, offset);
	if (!replacement)
		pinpoint_fatal();

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
