#include <stage0.h>
#include <pinpoint_config.h>
#include <pinpoint_error.h>

static tree separator_decl = NULL_TREE;

/*
 * Stage0 separators are compiler-internal marker calls, not runtime calls.
 *
 * They carry two constants: target UID and original field offset. The call is
 * declared pure/no-vops so GCC treats it as having no memory side effects; a
 * later pinpoint pass must remove every separator before code generation.
 */

static tree make_separator_decl()
{
	if (separator_decl)
		return separator_decl;

	tree args = tree_cons(NULL_TREE, sizetype,
			      tree_cons(NULL_TREE, sizetype, NULL_TREE));
	tree type = build_function_type(sizetype, args);

	tree tmp_decl = build_fn_decl(SPSLR_PINPOINT_STAGE0_SEPARATOR, type);
	if (!tmp_decl)
		return NULL_TREE;

	DECL_EXTERNAL(tmp_decl) = 1;
	TREE_PUBLIC(tmp_decl) = 1;
	DECL_ARTIFICIAL(tmp_decl) = 1;

	/* Prevent VOP problems later when removing calls (VOPs mark memory
	   side-effects, which these calls have none of anyways */
	DECL_PURE_P(tmp_decl) = 1;
	DECL_IS_NOVOPS(tmp_decl) = 1;

	return (separator_decl = tmp_decl);
}

tree make_stage0_ast_separator(UID target, std::size_t offset)
{
	tree decl = make_separator_decl();
	if (!decl)
		return NULL_TREE;

	tree arg0 = size_int(target);
	tree arg1 = size_int(offset);

	if (!arg0 || !arg1)
		return NULL_TREE;

	return build_call_expr(decl, 2, arg0, arg1);
}

gimple *make_stage0_gimple_separator(tree lhs, UID target, std::size_t offset)
{
	if (!lhs)
		return nullptr;

	tree decl = make_separator_decl();
	if (!decl)
		return nullptr;

	tree arg0 = size_int(target);
	tree arg1 = size_int(offset);

	if (!arg0 || !arg1)
		return nullptr;

	gimple *call = gimple_build_call(decl, 2, arg0, arg1);
	if (!call)
		return nullptr;

	gimple_call_set_lhs(call, lhs);
	return call;
}

static bool decl_is_separator(tree fndecl)
{
	if (!fndecl)
		return false;

	tree name_tree = DECL_NAME(fndecl);
	if (!name_tree)
		return false;

	const char *name = IDENTIFIER_POINTER(name_tree);
	if (!name)
		return false;

	return strcmp(name, SPSLR_PINPOINT_STAGE0_SEPARATOR) == 0;
}

bool is_stage0_separator(gimple *stmt, UID &target, std::size_t &offset)
{
	if (!stmt || !is_gimple_call(stmt))
		return false;

	tree fndecl = gimple_call_fndecl(stmt);
	if (!decl_is_separator(fndecl))
		return false;

	tree arg0 = gimple_call_arg(stmt, 0);
	tree arg1 = gimple_call_arg(stmt, 1);

	if (!arg0 || TREE_CODE(arg0) != INTEGER_CST)
		pinpoint_fatal(
			"is_state0_separator failed to get target UID from separator");

	if (!arg1 || TREE_CODE(arg1) != INTEGER_CST)
		pinpoint_fatal(
			"is_state0_separator failed to get field offset from separator");

	target = static_cast<UID>(tree_to_uhwi(arg0));
	offset = static_cast<std::size_t>(tree_to_uhwi(arg1));
	return true;
}
