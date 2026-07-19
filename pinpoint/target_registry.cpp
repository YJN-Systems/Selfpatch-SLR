#include <algorithm>
#include <map>
#include <set>
#include <vector>

#include <pinpoint.h>
#include <target_registry.h>
#include <layout_hash.h>

#include <safe-attribs.h>
#include <safe-langhooks.h>

struct validated_target {
	std::vector<target::compressed_field> fields{};
	std::map<tree, std::size_t> field_indices{};
	target::layout_hash_t hash{};
};

static std::map<tree, validated_target> validated_targets;

PINPOINT_GC_PRESERVE_CALLBACK()
{
	for (const auto &[t, info] : validated_targets)
		PINPOINT_GC_MARK_TREE(t);
}

using field_callback = std::function<void(tree field_decl)>;

static void iterate_fields(tree type, const field_callback &cb)
{
	type = target::main_variant(type);

	if (!type || !COMPLETE_TYPE_P(type))
		pinpoint_fatal("target::iterate_fields: incomplete target");

	for (tree f = TYPE_FIELDS(type); f; f = DECL_CHAIN(f)) {
		if (TREE_CODE(f) == FIELD_DECL)
			cb(f);
	}
}

static void build_compressed_fields(tree type, validated_target &vt)
{
	vt.fields.clear();

	std::size_t compressed_idx = 0;

	iterate_fields(type, [&](tree field) {
		std::string name = target::field_name(field);
		std::size_t off = target::field_offset(field);
		std::size_t sz = target::field_size(field);
		std::size_t end = off + sz;
		bool fixed = target::field_is_fixed(field);

		if (vt.fields.empty()) {
			vt.fields.push_back({
				.name = name,
				.offset = off,
				.size = sz,
				.alignment = target::field_alignment(field),
				.fixed = fixed,
			});
			vt.field_indices[field] = compressed_idx;
			return;
		}

		target::compressed_field &prev = vt.fields.back();
		std::size_t prev_end = prev.offset + prev.size;

		if (off < prev.offset)
			pinpoint_fatal(
				"target::build_compressed_fields: invalid field order in target \"%s\"",
				target::qualified_name(type).c_str());

		if (off >= prev_end) {
			vt.fields.push_back({
				.name = name,
				.offset = off,
				.size = sz,
				.alignment = target::field_alignment(field),
				.fixed = fixed,
			});
			vt.field_indices[field] = ++compressed_idx;
			return;
		}

		if (!prev.fixed || !fixed) {
			pinpoint_fatal(
				"target::build_compressed_fields: overlapping non-fixed field in target \"%s\": \"%s\"",
				target::qualified_name(type).c_str(),
				target::field_name(field).c_str());
		}

		/*
		 * Fixed overlapping fields are represented as one immovable byte
		 * range in the runtime metadata. Alignment is irrelevant because
		 * the randomizer will never move this synthetic field.
		 */
		if (end > prev_end)
			prev.size = end - prev.offset;

		prev.alignment = 1;
		prev.fixed = true;
		prev.name = prev.name + "+" + name;

		vt.field_indices[field] = compressed_idx;
	});
}

static void remember_target(tree type)
{
	type = target::main_variant(type);
	if (!type)
		return;

	if (validated_targets.find(type) != validated_targets.end())
		return;

	auto new_vt = validated_targets.emplace(type, validated_target{});
	if (!new_vt.second)
		pinpoint_fatal(
			"remember_target failed to log new validated target");

	validated_target &vt = new_vt.first->second;

	/* Must build compressed fields before hash, because hash queries them */
	build_compressed_fields(type, vt);
	vt.hash = compute_layout_hash(type);
}

bool target::is_validated_target(tree type)
{
	type = main_variant(type);
	return type && validated_targets.find(type) != validated_targets.end();
}

const target::layout_hash_t &target::layout_hash(tree type)
{
	type = main_variant(type);

	auto it = validated_targets.find(type);
	if (it == validated_targets.end())
		pinpoint_fatal(
			"target::layout_hash: type is not a validated SPSLR target");

	return it->second.hash;
}

tree target::main_variant(tree type)
{
	if (!type || TREE_CODE(type) != RECORD_TYPE)
		return NULL_TREE;

	return TYPE_MAIN_VARIANT(type);
}

tree target::from_field(tree field_decl)
{
	if (!field_decl || TREE_CODE(field_decl) != FIELD_DECL)
		return NULL_TREE;

	return main_variant(DECL_CONTEXT(field_decl));
}

bool target::is_target(tree type)
{
	type = main_variant(type);

	if (!type || TREE_CODE(type) != RECORD_TYPE)
		return false;

	return lookup_attribute(SPSLR_ATTRIBUTE, TYPE_ATTRIBUTES(type));
}

std::string target::field_name(tree field_decl)
{
	if (!field_decl || TREE_CODE(field_decl) != FIELD_DECL)
		return "<error>";

	tree name = DECL_NAME(field_decl);
	if (!name)
		return "<anonymous>";

	return IDENTIFIER_POINTER(name);
}

std::string target::name(tree type)
{
	type = main_variant(type);
	if (!type)
		return "<error>";

	tree name_tree = TYPE_NAME(type);
	if (!name_tree)
		return "<anonymous>";

	if (TREE_CODE(name_tree) == TYPE_DECL && DECL_NAME(name_tree))
		return IDENTIFIER_POINTER(DECL_NAME(name_tree));

	if (TREE_CODE(name_tree) == IDENTIFIER_NODE)
		return IDENTIFIER_POINTER(name_tree);

	return "<anonymous>";
}

static std::string decl_context_name(tree decl)
{
	if (!decl)
		return "<anonymous>";

	tree name = DECL_NAME(decl);
	if (!name)
		return "<anonymous>";

	return IDENTIFIER_POINTER(name);
}

std::vector<std::string> target::context_chain(tree type)
{
	std::vector<std::string> out;

	type = main_variant(type);
	if (!type)
		return out;

	tree type_name = TYPE_NAME(type);
	tree ctx = NULL_TREE;

	if (type_name && TREE_CODE(type_name) == TYPE_DECL)
		ctx = DECL_CONTEXT(type_name);

	if (!ctx)
		ctx = TYPE_CONTEXT(type);

	for (; ctx;) {
		if (TREE_CODE(ctx) == TRANSLATION_UNIT_DECL)
			break;

		if (TREE_CODE(ctx) == RECORD_TYPE) {
			out.push_back(name(ctx));
			ctx = TYPE_CONTEXT(ctx);
			continue;
		}

		if (DECL_P(ctx)) {
			out.push_back(decl_context_name(ctx));
			ctx = DECL_CONTEXT(ctx);
			continue;
		}

		if (TYPE_P(ctx)) {
			out.push_back(name(ctx));
			ctx = TYPE_CONTEXT(ctx);
			continue;
		}

		break;
	}

	std::reverse(out.begin(), out.end());
	return out;
}

std::string target::qualified_name(tree type)
{
	std::string out;

	for (const std::string &ctx : context_chain(type)) {
		if (!out.empty())
			out += "::";
		out += ctx;
	}

	if (!out.empty())
		out += "::";

	out += name(type);
	return out;
}

std::size_t target::size(tree type)
{
	type = main_variant(type);

	tree size_tree = type ? TYPE_SIZE(type) : NULL_TREE;
	if (!size_tree || TREE_CODE(size_tree) != INTEGER_CST)
		pinpoint_fatal("target::size: non-constant target size");

	HOST_WIDE_INT bits = tree_to_uhwi(size_tree);
	if (bits < 0 || bits % BITS_PER_UNIT)
		pinpoint_fatal("target::size: target size is not byte-aligned");

	return static_cast<std::size_t>(bits / BITS_PER_UNIT);
}

std::size_t target::field_offset(tree field_decl)
{
	if (!field_decl || TREE_CODE(field_decl) != FIELD_DECL)
		pinpoint_fatal(
			"target::field_offset can only be applied to FIELD_DECL trees");

	tree field_byte_offset_tree = DECL_FIELD_OFFSET(field_decl);
	tree field_bit_offset_tree = DECL_FIELD_BIT_OFFSET(field_decl);

	if (!field_byte_offset_tree ||
	    TREE_CODE(field_byte_offset_tree) != INTEGER_CST)
		pinpoint_fatal(
			"target::field_offset was unable to fetch byte offset");

	if (!field_bit_offset_tree ||
	    TREE_CODE(field_bit_offset_tree) != INTEGER_CST)
		pinpoint_fatal(
			"target::field_offset was unable to fetch bit offset");

	HOST_WIDE_INT byte_offset = tree_to_uhwi(field_byte_offset_tree);
	HOST_WIDE_INT bit_offset = tree_to_uhwi(field_bit_offset_tree);
	HOST_WIDE_INT bit_offset_bytes = bit_offset / BITS_PER_UNIT;

	return byte_offset + bit_offset_bytes;
}

bool target::field_has_size(tree field_decl)
{
	if (!field_decl || TREE_CODE(field_decl) != FIELD_DECL)
		return false;

	tree bit_offset = DECL_FIELD_BIT_OFFSET(field_decl);
	tree bit_size = DECL_SIZE(field_decl);

	return bit_offset && TREE_CODE(bit_offset) == INTEGER_CST && bit_size &&
	       TREE_CODE(bit_size) == INTEGER_CST;
}

std::size_t target::field_size(tree field_decl)
{
	if (!field_decl || TREE_CODE(field_decl) != FIELD_DECL)
		pinpoint_fatal(
			"target::field_size can only be applied to FIELD_DECL trees");

	tree field_bit_offset_tree = DECL_FIELD_BIT_OFFSET(field_decl);
	tree field_bit_size_tree = DECL_SIZE(field_decl);

	if (!field_bit_offset_tree ||
	    TREE_CODE(field_bit_offset_tree) != INTEGER_CST)
		pinpoint_fatal(
			"target::field_size was unable to fetch bit offset");

	if (!field_bit_size_tree ||
	    TREE_CODE(field_bit_size_tree) != INTEGER_CST)
		pinpoint_fatal(
			"target::field_size was unable to fetch bit size");

	HOST_WIDE_INT bit_offset =
		tree_to_uhwi(field_bit_offset_tree) % BITS_PER_UNIT;
	HOST_WIDE_INT bit_size = tree_to_uhwi(field_bit_size_tree) + bit_offset;

	HOST_WIDE_INT bit_overhang = bit_size % BITS_PER_UNIT;
	if (bit_overhang != 0)
		bit_size += (8 - bit_overhang);

	return static_cast<std::size_t>(bit_size / BITS_PER_UNIT);
}

std::size_t target::field_alignment(tree field_decl)
{
	if (!field_decl || TREE_CODE(field_decl) != FIELD_DECL)
		pinpoint_fatal(
			"target::field_alignment can only be applied to FIELD_DECL trees");

	HOST_WIDE_INT alignment_bits = DECL_ALIGN(field_decl);
	if (alignment_bits <= 0 && TREE_TYPE(field_decl))
		alignment_bits = TYPE_ALIGN(TREE_TYPE(field_decl));
	if (alignment_bits <= 0)
		alignment_bits = BITS_PER_UNIT;

	std::size_t alignment = static_cast<std::size_t>(
		(alignment_bits + BITS_PER_UNIT - 1) / BITS_PER_UNIT);
	if (alignment == 0)
		alignment = 1;

	return alignment;
}

bool target::field_is_bitfield(tree field_decl)
{
	if (!field_decl || TREE_CODE(field_decl) != FIELD_DECL)
		pinpoint_fatal(
			"target::field_is_bitfield can only be applied to FIELD_DECL trees");

	tree field_bit_offset_tree = DECL_FIELD_BIT_OFFSET(field_decl);
	tree field_bit_size_tree = DECL_SIZE(field_decl);

	if (!field_bit_offset_tree ||
	    TREE_CODE(field_bit_offset_tree) != INTEGER_CST)
		pinpoint_fatal(
			"target::field_is_bitfield was unable to fetch bit offset");

	if (!field_bit_size_tree ||
	    TREE_CODE(field_bit_size_tree) != INTEGER_CST)
		pinpoint_fatal(
			"target::field_is_bitfield was unable to fetch bit size");

	HOST_WIDE_INT bit_offset =
		tree_to_uhwi(field_bit_offset_tree) % BITS_PER_UNIT;
	HOST_WIDE_INT bit_size = tree_to_uhwi(field_bit_size_tree);

	bool decl_bitfield = DECL_BIT_FIELD_TYPE(field_decl) != NULL_TREE;
	bool extra_bitfield = bit_size % 8 != 0 || bit_offset != 0;

	return decl_bitfield || extra_bitfield;
}

bool target::field_is_fixed(tree field_decl)
{
	return field_is_bitfield(field_decl) ||
	       lookup_attribute(SPSLR_FIELD_FIXED_ATTRIBUTE,
				DECL_ATTRIBUTES(field_decl));
}

const std::vector<target::compressed_field> &
target::compressed_fields(tree type)
{
	type = main_variant(type);

	auto it = validated_targets.find(type);
	if (it == validated_targets.end())
		pinpoint_fatal(
			"target::compressed_fields: type is not a validated SPSLR target");

	return it->second.fields;
}

void target::iterate_targets(const target_callback &cb)
{
	for (const auto &[t, info] : validated_targets)
		cb(t);
}

std::size_t target::target_count()
{
	return validated_targets.size();
}

void target::validate(tree type)
{
	type = main_variant(type);
	remember_target(type);
}

bool target::component_ref(tree ref, tree *field_decl)
{
	if (!ref || TREE_CODE(ref) != COMPONENT_REF)
		return false;

	tree field = TREE_OPERAND(ref, 1);
	if (!field || TREE_CODE(field) != FIELD_DECL)
		return false;

	tree type = from_field(field);
	if (!is_target(type))
		return false;

	if (field_decl)
		*field_decl = field;

	return true;
}

std::size_t target::field_index(tree field_decl)
{
	tree type = from_field(field_decl);
	if (!type)
		pinpoint_fatal(
			"target::field_index: field does not belong to a target");

	auto vt = validated_targets.find(type);
	if (vt == validated_targets.end())
		pinpoint_fatal(
			"target::field_index: field does not belong to a validated target");

	auto it = vt->second.field_indices.find(field_decl);
	if (it == vt->second.field_indices.end())
		pinpoint_fatal(
			"target::field_index: field does not belong to a validated target");

	return it->second;
}

void target::reset()
{
	validated_targets.clear();
}
