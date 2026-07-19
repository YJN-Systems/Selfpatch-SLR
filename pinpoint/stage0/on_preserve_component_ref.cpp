#include <pinpoint.h>
#include <passes.h>
#include <ipin_registry.h>
#include <target_registry.h>
#include <safe-tree.h>

static tree materialize_c_rvalue(location_t loc, tree expr)
{
	gcc_assert(!lvalue_p(expr));

	tree type = TREE_TYPE(expr);

	tree tmp = build_decl(loc, VAR_DECL, create_tmp_var_name("spslr_rval"),
			      type);

	DECL_CONTEXT(tmp) = current_function_decl;
	DECL_ARTIFICIAL(tmp) = 1;
	DECL_IGNORED_P(tmp) = 1;
	TREE_USED(tmp) = 1;
	TREE_ADDRESSABLE(tmp) = 1;
	DECL_CHAIN(tmp) = NULL_TREE;

	// layout_decl(tmp, 0); - not available to plugins

	tree init = build2_loc(loc, INIT_EXPR, type, tmp, expr);

	tree body = build2(COMPOUND_EXPR, type, init, tmp);

	SET_EXPR_LOCATION(body, loc);

	tree bind = build3(BIND_EXPR, type, tmp, body, NULL_TREE);

	SET_EXPR_LOCATION(bind, loc);
	TREE_SIDE_EFFECTS(bind) = 1;

	return bind;
}

/*
 * Preserve an early COMPONENT_REF before GCC folds it into a plain constant
 * offset.
 *
 * The custom GCC hook calls this while the frontend still knows that an
 * expression is "base.field". We rewrite it into pointer arithmetic whose
 * offset comes from a synthetic separator call:
 *
 *     base.field  ->  *(typeof(field) *)((char *)&base + separator(uid))
 *
 * Later passes replace the separator with an instruction pin.
 */

static tree ast_separate_offset(tree ref, ipin::handle pin)
{
	tree separator = ipin::make_ast_separator(pin);
	if (!separator)
		pinpoint_fatal(
			"ast_separate_offset failed to generate AST separator");

	tree base = TREE_OPERAND(ref, 0);

	// ADDR_EXPR can never be a valid base, but such trees can happen during parsing before checks
	if (TREE_CODE(base) == ADDR_EXPR)
		pinpoint_fatal(
			"ast_separate_offset encountered ADDR_EXPR as COMPONENT_REF base");

	// Turn rvalue objects into adressable lvalues
	if (!lvalue_p(base))
		base = materialize_c_rvalue(EXPR_LOCATION(base), base);

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

	tree field;
	if (!target::component_ref(*ref, &field))
		return;

	if (target::field_is_fixed(field))
		return;

	ipin::handle pin = ipin::make(field);
	tree separated = ast_separate_offset(*ref, pin);
	if (separated)
		*ref = separated;
}
