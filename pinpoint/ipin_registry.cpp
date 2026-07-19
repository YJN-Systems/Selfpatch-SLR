#include <cstring>
#include <algorithm>

#include <pinpoint.h>
#include <ipin_registry.h>
#include <target_registry.h>

#define PINPOINT_SEPARATOR "__spslr_offsetof"
#define PINPOINT_IPIN_MARKER "spslr_ipin_marker"
#define PINPOINT_IPIN_SYMBOL_PREFIX "spslr_ipin_" /* suffixed with "<uid>" */
#define PINPOINT_IPIN_WIDTH_SYMBOL_PREFIX \
	"spslr_ipin_width_" /* suffixed with "<uid>" */

static ipin::handle next_ipin_handle = 0;
static std::map<ipin::handle, ipin> ipins;

static tree separator_decl = NULL_TREE;

PINPOINT_GC_PRESERVE_CALLBACK()
{
	PINPOINT_GC_MARK_TREE(separator_decl);

	for (const auto &[h, pin] : ipins)
		PINPOINT_GC_MARK_TREE(pin.field);
}

/*
 * Separators are compiler-internal marker calls, not runtime calls.
 *
 * They carry a unique ID with which metadata is associated. The call is
 * declared pure/no-vops so GCC treats it as having no memory side effects; a
 * later pinpoint pass must remove every separator before code generation.
 */

static tree make_separator_decl()
{
	if (separator_decl)
		return separator_decl;

	tree args = tree_cons(NULL_TREE, sizetype, NULL_TREE);
	tree type = build_function_type(sizetype, args);

	tree tmp_decl = build_fn_decl(PINPOINT_SEPARATOR, type);
	if (!tmp_decl)
		return NULL_TREE;

	DECL_EXTERNAL(tmp_decl) = 1;
	TREE_PUBLIC(tmp_decl) = 1;
	DECL_ARTIFICIAL(tmp_decl) = 1;

	/* Prevent VOP problems later when removing calls (VOPs mark memory
       side-effects, which these calls have none of anyways) */
	DECL_PURE_P(tmp_decl) = 1;
	DECL_IS_NOVOPS(tmp_decl) = 1;

	return (separator_decl = tmp_decl);
}

static ipin *get_pin(ipin::handle pin)
{
	auto it = ipins.find(pin);
	return it == ipins.end() ? nullptr : &it->second;
}

ipin::handle ipin::make(tree field)
{
	handle h = next_ipin_handle++;

	ipin pin;
	pin.status = state::pending;
	pin.field = field;

	ipins.emplace(h, std::move(pin));
	return h;
}

tree ipin::make_ast_separator(ipin::handle pin)
{
	ipin *p = get_pin(pin);
	if (!p)
		pinpoint_fatal("ipin: inknown pin in make_ast_separator");

	tree decl = make_separator_decl();
	if (!decl)
		return NULL_TREE;

	tree arg0 = size_int(pin);
	if (!arg0)
		return NULL_TREE;

	p->status = state::separator;
	return build_call_expr(decl, 1, arg0);
}

gimple *ipin::make_gimple_separator(tree lhs, ipin::handle pin)
{
	if (!lhs)
		return nullptr;

	ipin *p = get_pin(pin);
	if (!p)
		pinpoint_fatal("ipin: unknown pin in make_gimple_separator");

	tree decl = make_separator_decl();
	if (!decl)
		return nullptr;

	tree arg0 = size_int(pin);
	if (!arg0)
		return nullptr;

	gimple *call = gimple_build_call(decl, 1, arg0);
	if (!call)
		return nullptr;

	gimple_call_set_lhs(call, lhs);
	p->status = state::separator;
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

	return strcmp(name, PINPOINT_SEPARATOR) == 0;
}

ipin::handle ipin::identify_gimple_separator(gimple *stmt)
{
	if (!stmt || !is_gimple_call(stmt))
		return invalid;

	tree fndecl = gimple_call_fndecl(stmt);
	if (!decl_is_separator(fndecl))
		return invalid;

	tree arg0 = gimple_call_arg(stmt, 0);
	if (!arg0 || TREE_CODE(arg0) != INTEGER_CST)
		pinpoint_fatal("ipin: separator has invalid ipin handle");

	return static_cast<handle>(tree_to_uhwi(arg0));
}

static bool lhs_type_is_64bit(tree lhs)
{
	if (!lhs)
		return false;

	tree type = TREE_TYPE(lhs);
	if (!type)
		return false;

	tree size = TYPE_SIZE(type);
	if (!size || TREE_CODE(size) != INTEGER_CST)
		return false;

	return tree_to_uhwi(size) == 64;
}

static tree make_asm_operand(const char *constraint_text, tree operand_tree)
{
	tree constraint_str =
		build_string(strlen(constraint_text) + 1, constraint_text);
	tree inner_list = build_tree_list(NULL_TREE, constraint_str);
	tree outer_list = build_tree_list(inner_list, operand_tree);
	return outer_list;
}

/*
 * The symbol does not denote executable code as a callable function; it denotes
 * the four-byte immediate field inside the instruction stream.
 */
static std::string make_final_x86_64_asm(const std::string &field_symbol,
					 const std::string &width_symbol,
					 std::size_t imm)
{
	char buf[512];

	std::snprintf(buf, sizeof(buf),
		      "# %s\n"
		      " movq $fieldlabel(%zu, %s, %s), %%0",
		      PINPOINT_IPIN_MARKER, imm, field_symbol.c_str(),
		      width_symbol.c_str());

	return std::string(buf);
}

gimple *ipin::make_gimple_pin(tree lhs, ipin::handle pin)
{
	if (!lhs)
		return nullptr;

	ipin *p = get_pin(pin);
	if (!p)
		pinpoint_fatal("ipin: unknown pin in make_gimple_pin");

	if (!lhs_type_is_64bit(lhs))
		pinpoint_fatal("ipin: expected 64-bit destination type");

	/*
	 * Do not emit the final label here. GCC may duplicate this asm later.
	 * The original pin id is carried only as an input operand.
	 */
	std::string asm_str = "# " PINPOINT_IPIN_MARKER;

	tree arg0 = build_int_cst(size_type_node, pin);

	vec<tree, va_gc> *outputs = NULL;
	vec<tree, va_gc> *inputs = NULL;

	vec_safe_push(outputs, make_asm_operand("=r", lhs));
	vec_safe_push(inputs, make_asm_operand("i", arg0));

	gasm *new_gasm = gimple_build_asm_vec(ggc_strdup(asm_str.c_str()),
					      inputs, outputs, NULL, NULL);
	if (!new_gasm)
		return nullptr;

	/*
	 * Non-volatile is intentional: unused field-offset computations should die
	 * normally. Only offsets that survive optimization become instruction pins.
	 */
	gimple_asm_set_volatile(new_gasm, false);

	p->status = state::pin;
	return new_gasm;
}

static bool extract_asm_operands(rtx x, rtx &asm_out)
{
	if (!x)
		return false;

	if (GET_CODE(x) == ASM_OPERANDS) {
		asm_out = x;
		return true;
	}

	if (GET_CODE(x) == SET)
		return extract_asm_operands(SET_SRC(x), asm_out);

	if (GET_CODE(x) == PARALLEL) {
		for (int i = 0; i < XVECLEN(x, 0); ++i) {
			if (extract_asm_operands(XVECEXP(x, 0, i), asm_out))
				return true;
		}
	}

	return false;
}

ipin::handle ipin::identify_rtl_pin(rtx x)
{
	rtx asm_rtx = nullptr;
	if (!extract_asm_operands(x, asm_rtx))
		return invalid;

	if (!asm_rtx || GET_CODE(asm_rtx) != ASM_OPERANDS)
		return invalid;

	const char *templ = ASM_OPERANDS_TEMPLATE(asm_rtx);
	if (!templ || !std::strstr(templ, PINPOINT_IPIN_MARKER))
		return invalid;

	if (ASM_OPERANDS_INPUT_LENGTH(asm_rtx) != 1)
		pinpoint_fatal(
			"ipin: RTL pin asm has invalid number of inputs");

	rtx in0 = ASM_OPERANDS_INPUT(asm_rtx, 0);
	if (!CONST_INT_P(in0))
		pinpoint_fatal("ipin: RTL ipin id is not CONST_INT");

	return static_cast<handle>(INTVAL(in0));
}

void ipin::mark_live(ipin::handle pin, rtx at)
{
	ipin::handle found = identify_rtl_pin(at);

	if (found != pin) {
		pinpoint_fatal(
			"ipin: mark_live called with mismatching RTL pin");
	}

	rtx asm_rtx = nullptr;

	if (!extract_asm_operands(at, asm_rtx) || !asm_rtx ||
	    GET_CODE(asm_rtx) != ASM_OPERANDS) {
		pinpoint_fatal(
			"ipin: mark_live could not recover ASM_OPERANDS");
	}

	ipin *p = get_pin(pin);
	if (!p) {
		pinpoint_fatal("ipin: tried to mark unknown pin live");
	}

	ipin::handle live_handle = pin;
	ipin *live_pin = p;

	if (p->status == state::live) {
		live_handle = next_ipin_handle++;

		ipin clone = *p;
		clone.status = state::pin;
		clone.symbol.clear();
		clone.width_symbol.clear();

		auto inserted = ipins.emplace(live_handle, std::move(clone));
		live_pin = &inserted.first->second;
	}

	live_pin->status = state::live;

	const std::string handle_suffix = std::to_string(live_handle);

	live_pin->symbol =
		".L" + std::string(PINPOINT_IPIN_SYMBOL_PREFIX) + handle_suffix;

	live_pin->width_symbol =
		".L" + std::string(PINPOINT_IPIN_WIDTH_SYMBOL_PREFIX) +
		handle_suffix;

	std::size_t offset = target::field_offset(live_pin->field);

	std::string final_asm = make_final_x86_64_asm(
		live_pin->symbol, live_pin->width_symbol, offset);

	XSTR(asm_rtx, 0) = ggc_strdup(final_asm.c_str());
}

std::size_t ipin::live_count()
{
	std::size_t n = 0;

	for (const auto &[h, pin] : ipins) {
		if (pin.status == state::live)
			++n;
	}

	return n;
}

const std::map<ipin::handle, ipin> &ipin::inspect()
{
	return ipins;
}

const ipin *ipin::inspect(ipin::handle pin)
{
	return get_pin(pin);
}

void ipin::reset()
{
	ipins.clear();
	next_ipin_handle = 0;
}
