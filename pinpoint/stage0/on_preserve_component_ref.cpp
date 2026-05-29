#include <stage0.h>
#include <pinpoint_error.h>

/*
 * Preserve an early COMPONENT_REF before GCC folds it into a plain constant
 * offset.
 *
 * The custom GCC hook calls this while the frontend still knows that an
 * expression is "base.field". We rewrite it into pointer arithmetic whose
 * offset comes from a synthetic separator call:
 *
 *     base.field  ->  *(typeof(field) *)((char *)&base + separator(target, off))
 *
 * Later passes replace the separator with an instruction pin.
 */

static tree ast_separate_offset(tree ref, UID target, std::size_t offset)
{
	tree separator = make_stage0_ast_separator(target, offset);
	if (!separator)
		pinpoint_fatal(
			"ast_separate_offset failed to generate AST separator for target %u at offset %u",
			(unsigned)target, (unsigned)offset);

	tree base = TREE_OPERAND(ref, 0);

	// ADDR_EXPR can never be a valid base, but such trees can happen during parsing before checks
	if (TREE_CODE(base) == ADDR_EXPR)
		pinpoint_fatal(
			"ast_separate_offset encountered ADDR_EXPR as COMPONENT_REF base");

	tree base_ptr = build_fold_addr_expr(base);

	tree field_type = TREE_TYPE(
		ref); // Type of COMPONENT_REF is type of the accessed field
	tree field_ptr_type = build_pointer_type(field_type);
	tree field_ptr =
		build2(POINTER_PLUS_EXPR, field_ptr_type, base_ptr, separator);

	tree field_ref = build1(INDIRECT_REF, field_type, field_ptr);
	return field_ref;
}

void on_preserve_component_ref(void *plugin_data, void *user_data)
{
	tree *ref = (tree *)plugin_data;
	if (!ref)
		return;

	UID target;
	std::size_t offset;
	if (!TargetType::reference(*ref, target, offset))
		return;

	tree separated = ast_separate_offset(*ref, target, offset);
	if (separated)
		*ref = separated;
}
