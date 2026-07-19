#include <cstring>
#include <map>
#include <string>

#include <pinpoint.h>
#include <passes.h>
#include <serialize.h>
#include <target_registry.h>

#include <safe-gimple.h>
#include <safe-tree.h>

namespace
{

static std::map<std::string, tree> hash_symbol_decls;

static selfpatch::hash16_t to_hash16(const target::layout_hash_t &in)
{
	selfpatch::hash16_t out{};

	for (std::size_t i = 0; i < out.size(); ++i)
		out[i] = static_cast<std::uint8_t>(in[i]);

	return out;
}

static bool called_decl_name_is(tree fndecl, const char *wanted)
{
	if (!fndecl || !DECL_NAME(fndecl))
		return false;

	const char *name = IDENTIFIER_POINTER(DECL_NAME(fndecl));
	return name && std::strcmp(name, wanted) == 0;
}

static tree call_argument_pointee_type(gimple *stmt)
{
	if (!stmt || !is_gimple_call(stmt) || gimple_call_num_args(stmt) != 1)
		return NULL_TREE;

	tree arg = gimple_call_arg(stmt, 0);
	if (!arg)
		return NULL_TREE;

	/* Case: &__spslr_target_hash_type_anchor_N */
	if (TREE_CODE(arg) == ADDR_EXPR) {
		tree obj = TREE_OPERAND(arg, 0);
		if (obj) {
			tree obj_type = TREE_TYPE(obj);
			if (obj_type)
				return target::main_variant(obj_type);
		}
	}

	/* Fallback: argument still has pointer type T *. */
	tree arg_type = TREE_TYPE(arg);
	if (arg_type && POINTER_TYPE_P(arg_type))
		return target::main_variant(TREE_TYPE(arg_type));

	return NULL_TREE;
}

static tree make_hash_symbol_decl(const std::string &symbol)
{
	auto it = hash_symbol_decls.find(symbol);
	if (it != hash_symbol_decls.end())
		return it->second;

	tree byte_type =
		build_qualified_type(unsigned_char_type_node, TYPE_QUAL_CONST);
	tree array_type = build_array_type_nelts(byte_type, 16);

	tree decl = build_decl(UNKNOWN_LOCATION, VAR_DECL,
			       get_identifier(symbol.c_str()), array_type);

	DECL_EXTERNAL(decl) = 1;
	TREE_PUBLIC(decl) = 1;
	TREE_READONLY(decl) = 1;
	DECL_ARTIFICIAL(decl) = 1;
	DECL_IGNORED_P(decl) = 1;

	hash_symbol_decls.emplace(symbol, decl);
	return decl;
}

static tree make_hash_pointer_expr(tree target_type, tree result_type)
{
	const selfpatch::hash16_t hash =
		to_hash16(target::layout_hash(target_type));

	const std::string symbol = selfpatch::target_hash_symbol(hash);

	tree decl = make_hash_symbol_decl(symbol);
	tree addr = build_fold_addr_expr(decl);

	return fold_convert(result_type, addr);
}

static tree make_null_pointer_expr(tree result_type)
{
	return fold_convert(result_type, null_pointer_node);
}

} // namespace

PINPOINT_GC_PRESERVE_CALLBACK()
{
	for (const auto &[symbol, decl] : hash_symbol_decls)
		PINPOINT_GC_MARK_TREE(decl);
}

static const pass_data target_hash_builtin_pass_data = {
	GIMPLE_PASS, "spslr_target_hash", OPTGROUP_NONE, TV_NONE, 0, 0, 0,
	0,	     TODO_update_ssa
};

target_hash_builtin_pass::target_hash_builtin_pass(gcc::context *ctxt)
	: gimple_opt_pass(target_hash_builtin_pass_data, ctxt)
{
}

unsigned int target_hash_builtin_pass::execute(function *fn)
{
	if (!fn)
		return 0;

	basic_block bb;
	FOR_EACH_BB_FN(bb, fn)
	{
		for (gimple_stmt_iterator gsi = gsi_start_bb(bb);
		     !gsi_end_p(gsi);) {
			gimple *stmt = gsi_stmt(gsi);

			if (!is_gimple_call(stmt) ||
			    !called_decl_name_is(gimple_call_fndecl(stmt),
						 SPSLR_TARGET_HASH_BUILTIN)) {
				gsi_next(&gsi);
				continue;
			}

			tree lhs = gimple_call_lhs(stmt);
			if (!lhs) {
				gsi_remove(&gsi, true);
				continue;
			}

			tree result_type = TREE_TYPE(lhs);
			tree target_type = call_argument_pointee_type(stmt);

			tree rhs = NULL_TREE;

			if (target_type &&
			    target::is_validated_target(target_type))
				rhs = make_hash_pointer_expr(target_type,
							     result_type);
			else
				rhs = make_null_pointer_expr(result_type);

			gimple *replacement = gimple_build_assign(lhs, rhs);
			gsi_replace(&gsi, replacement, true);
			gsi_next(&gsi);
		}
	}

	return 0;
}
