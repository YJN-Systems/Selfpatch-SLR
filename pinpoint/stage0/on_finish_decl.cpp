#include <stage0.h>

#include <unordered_set>
#include <string>

#include <pinpoint_error.h>
#include <pinpoint_config.h>

static std::list<DataPin> pins;
static std::unordered_set<std::string> seen_dpin_symbols;

void DataPin::reset()
{
	pins.clear();
}

const std::list<DataPin> &DataPin::all()
{
	return pins;
}

static std::list<DataPin::Component>
compile_datapin_record_components(tree type);
static std::list<DataPin::Component>
compile_datapin_array_components(tree type);

/*
 * Build the list of randomized objects contained in a static object.
 *
 * A dpin may describe the object itself, nested target structs, arrays of
 * targets, or targets embedded inside non-target containers. The level field
 * records nesting depth so patchcompile/runtime can patch inner objects before
 * their containing objects.
 */

static std::list<DataPin::Component> compile_datapin_components(tree type)
{
	std::list<DataPin::Component> components;

	const TargetType *relevant = TargetType::find(type);
	if (relevant) {
		components.push_back(DataPin::Component{
			.offset = 0, .level = 0, .target = relevant->uid() });
	}

	std::list<DataPin::Component> sub_components;
	if (TREE_CODE(type) == RECORD_TYPE) {
		sub_components = compile_datapin_record_components(type);
	} else if (TREE_CODE(type) == ARRAY_TYPE) {
		sub_components = compile_datapin_array_components(type);
	}

	for (const DataPin::Component &sc : sub_components) {
		components.push_back(DataPin::Component{ .offset = sc.offset,
							 .level = sc.level + 1,
							 .target = sc.target });
	}

	// Note -> should probably make sure that randomized structs are never used in unions!
	return components;
}

std::list<DataPin::Component> compile_datapin_record_components(tree type)
{
	std::list<DataPin::Component> components;

	if (TREE_CODE(type) != RECORD_TYPE)
		return components;

	if (!COMPLETE_TYPE_P(type))
		return components;

	for (tree field = TYPE_FIELDS(type); field; field = TREE_CHAIN(field)) {
		if (TREE_CODE(field) != FIELD_DECL)
			continue;

		std::size_t field_offset;
		bool field_bitfield;

		if (!field_info(field, &field_offset, nullptr, nullptr,
				&field_bitfield)) {
			// pinpoint_fatal("compile_datapin_record_components failed to get field info");
			continue; // NOTE -> Happens e.g. on trailing arrays of dynamic size (allowed only in non-target structs)
		}

		if (field_bitfield)
			continue;

		tree field_type = TREE_TYPE(field);

		std::list<DataPin::Component> field_components =
			compile_datapin_components(field_type);
		for (const DataPin::Component &fc : field_components) {
			components.push_back(DataPin::Component{
				.offset = field_offset + fc.offset,
				.level = fc.level,
				.target = fc.target });
		}
	}

	return components;
}

std::list<DataPin::Component> compile_datapin_array_components(tree type)
{
	std::list<DataPin::Component> components;

	if (TREE_CODE(type) != ARRAY_TYPE)
		return components;

	tree elem_type = TREE_TYPE(type);
	if (!elem_type)
		return components;

	std::list<DataPin::Component> elem_components =
		compile_datapin_components(elem_type);
	if (elem_components.empty())
		return components;

	tree domain = TYPE_DOMAIN(type);
	if (!domain)
		pinpoint_fatal(
			"compile_datapin_array_components failed to get domain for relevant element type");

	tree min_t = TYPE_MIN_VALUE(domain);
	tree max_t = TYPE_MAX_VALUE(domain);
	if (!min_t || !max_t || TREE_CODE(min_t) != INTEGER_CST ||
	    TREE_CODE(max_t) != INTEGER_CST)
		pinpoint_fatal(
			"compile_datapin_array_components failed to get array dimensions for relevant element type");

	HOST_WIDE_INT min_i = tree_to_shwi(min_t);
	HOST_WIDE_INT max_i = tree_to_shwi(max_t);

	tree elem_size_t = TYPE_SIZE_UNIT(elem_type);
	if (!elem_size_t || TREE_CODE(elem_size_t) != INTEGER_CST)
		pinpoint_fatal(
			"compile_datapin_array_components failed to get constant element size for relevant element type");

	std::size_t elem_size = tree_to_uhwi(elem_size_t);

	for (HOST_WIDE_INT i = min_i; i <= max_i; ++i) {
		std::size_t element_offset =
			(std::size_t)(i - min_i) * elem_size;

		for (const DataPin::Component &ec : elem_components) {
			components.push_back(DataPin::Component{
				.offset = element_offset + ec.offset,
				.level = ec.level,
				.target = ec.target });
		}
	}

	return components;
}

static bool compile_datapin(tree type, DataPin &pin)
{
	pin.components = compile_datapin_components(type);
	return !pin.components.empty();
}

static void on_static_var(tree var)
{
	tree type = TREE_TYPE(var);
	if (!type)
		return;

	tree symbol_tree = DECL_ASSEMBLER_NAME(var);
	const char *symbol;
	if (!symbol_tree || !(symbol = IDENTIFIER_POINTER(symbol_tree)))
		pinpoint_fatal(
			"on_static_var failed to get symbol of static variable");

	std::string sym{ symbol };

	/*
	 * Multiple VAR_DECLs can name the same emitted object, for example through
	 * export or alias machinery. Emit at most one dpin per assembler symbol.
	 */
	if (!seen_dpin_symbols.insert(sym).second)
		return;

	DataPin pin;
	if (!compile_datapin(type, pin))
		return;

	DECL_PRESERVE_P(var) = 1;
	// pin.global = static_cast<bool>(TREE_PUBLIC(var));

	pin.symbol = sym;
	pins.emplace_back(std::move(pin));
}

void on_finish_decl(void *plugin_data, void *user_data)
{
	tree decl = (tree)plugin_data;

	if (TREE_CODE(decl) != VAR_DECL)
		return;

	if (!TREE_STATIC(decl) || DECL_EXTERNAL(decl))
		return;

	on_static_var(decl);
}
