#include <string>
#include <unordered_set>

#include <pinpoint.h>
#include <dpin_registry.h>
#include <target_registry.h>

static std::list<dpin> pins;
static std::unordered_set<std::string> seen_dpin_symbols;

PINPOINT_GC_PRESERVE_CALLBACK()
{
	for (const dpin &pin : pins)
		for (const dpin::component &c : pin.components)
			PINPOINT_GC_MARK_TREE(c.target);
}

void dpin::reset()
{
	pins.clear();
	seen_dpin_symbols.clear();
}

const std::list<dpin> &dpin::inspect()
{
	return pins;
}

static std::list<dpin::component> compile_datapin_components(tree type);
static std::list<dpin::component> compile_datapin_record_components(tree type);
static std::list<dpin::component> compile_datapin_array_components(tree type);

/*
 * Build the list of randomized objects contained in a static object.
 *
 * A dpin may describe the object itself, nested target structs, arrays of
 * targets, or targets embedded inside non-target containers. The level field
 * records nesting depth so the runtime can patch inner objects before
 * their containing objects.
 */

static std::list<dpin::component> compile_datapin_components(tree type)
{
	std::list<dpin::component> components;

	tree relevant = target::main_variant(type);
	if (target::is_target(relevant)) {
		components.push_back(dpin::component{
			.offset = 0,
			.level = 0,
			.target = relevant,
		});
	}

	std::list<dpin::component> sub_components;
	if (TREE_CODE(type) == RECORD_TYPE)
		sub_components = compile_datapin_record_components(type);
	else if (TREE_CODE(type) == ARRAY_TYPE)
		sub_components = compile_datapin_array_components(type);

	for (const dpin::component &sc : sub_components) {
		components.push_back(dpin::component{
			.offset = sc.offset,
			.level = sc.level + 1,
			.target = sc.target,
		});
	}

	// Note -> should probably make sure that randomized structs are never used in unions!
	return components;
}

static std::list<dpin::component> compile_datapin_record_components(tree type)
{
	std::list<dpin::component> components;

	if (TREE_CODE(type) != RECORD_TYPE || !COMPLETE_TYPE_P(type))
		return components;

	for (tree field = TYPE_FIELDS(type); field; field = TREE_CHAIN(field)) {
		if (TREE_CODE(field) != FIELD_DECL)
			continue;

		if (!target::field_has_size(field))
			continue; // flexible array member / dynamic-size trailing array

		if (target::field_is_bitfield(field))
			continue;

		std::size_t field_offset = target::field_offset(field);
		tree field_type = TREE_TYPE(field);

		std::list<dpin::component> field_components =
			compile_datapin_components(field_type);

		for (const dpin::component &fc : field_components) {
			components.push_back(dpin::component{
				.offset = field_offset + fc.offset,
				.level = fc.level,
				.target = fc.target,
			});
		}
	}

	return components;
}

static std::list<dpin::component> compile_datapin_array_components(tree type)
{
	std::list<dpin::component> components;

	if (TREE_CODE(type) != ARRAY_TYPE)
		return components;

	tree elem_type = TREE_TYPE(type);
	if (!elem_type)
		return components;

	std::list<dpin::component> elem_components =
		compile_datapin_components(elem_type);
	if (elem_components.empty())
		return components;

	tree domain = TYPE_DOMAIN(type);
	if (!domain)
		pinpoint_fatal("dpin: failed to get array domain");

	tree min_t = TYPE_MIN_VALUE(domain);
	tree max_t = TYPE_MAX_VALUE(domain);
	if (!min_t || !max_t || TREE_CODE(min_t) != INTEGER_CST ||
	    TREE_CODE(max_t) != INTEGER_CST)
		pinpoint_fatal("dpin: failed to get constant array bounds");

	HOST_WIDE_INT min_i = tree_to_shwi(min_t);
	HOST_WIDE_INT max_i = tree_to_shwi(max_t);

	tree elem_size_t = TYPE_SIZE_UNIT(elem_type);
	if (!elem_size_t || TREE_CODE(elem_size_t) != INTEGER_CST)
		pinpoint_fatal("dpin: failed to get constant element size");

	std::size_t elem_size = tree_to_uhwi(elem_size_t);

	for (HOST_WIDE_INT i = min_i; i <= max_i; ++i) {
		std::size_t element_offset =
			static_cast<std::size_t>(i - min_i) * elem_size;

		for (const dpin::component &ec : elem_components) {
			components.push_back(dpin::component{
				.offset = element_offset + ec.offset,
				.level = ec.level,
				.target = ec.target,
			});
		}
	}

	return components;
}

static bool compile_datapin(tree type, dpin &pin)
{
	pin.components = compile_datapin_components(type);
	return !pin.components.empty();
}

void dpin::consider_static_var(tree var)
{
	if (!var || TREE_CODE(var) != VAR_DECL)
		return;

	if (!TREE_STATIC(var) || DECL_EXTERNAL(var))
		return;

	tree type = TREE_TYPE(var);
	if (!type)
		return;

	tree symbol_tree = DECL_ASSEMBLER_NAME(var);
	const char *symbol = symbol_tree ? IDENTIFIER_POINTER(symbol_tree) :
					   nullptr;
	if (!symbol)
		pinpoint_fatal("dpin: failed to get static variable symbol");

	std::string sym{ symbol };

	/*
	 * Multiple VAR_DECLs can name the same emitted object, for example through
	 * export or alias machinery. Emit at most one dpin per assembler symbol.
	 */
	if (!seen_dpin_symbols.insert(sym).second)
		return;

	dpin pin;
	if (!compile_datapin(type, pin))
		return;

	DECL_PRESERVE_P(var) = 1;
	pin.symbol = sym;
	pins.emplace_back(std::move(pin));
}
