#include <stage0.h>
#include <functional>

#include <safe-langhooks.h>
#include <safe-attribs.h>

#include <pinpoint_config.h>
#include <pinpoint_error.h>
#include <gc_preserve.h>

/*
 * TargetType is the compiler-side model of an SPSLR-randomized record type.
 *
 * A target is keyed by GCC's main record variant, not by spelling alone, so
 * typedefs and compatible variants resolve to the same SPSLR target. The UID
 * is local to this compilation unit; patchcompile later merges compatible
 * targets across objects.
 */

static UID next_uid = 0;
static std::unordered_map<UID, TargetType> targets;

static tree get_record_main_variant(tree t)
{
	if (!t || TREE_CODE(t) != RECORD_TYPE)
		return NULL_TREE;

	return TYPE_MAIN_VARIANT(t);
}

TargetType::TargetType(tree t)
	: m_uid{ UID_INVALID }
	, m_flags{ 0 }
	, m_size{ 0 }
{
	if (!(m_main_variant = get_record_main_variant(t)))
		return;

	m_flags |= FLAG_MAIN_VARIANT;
}

TargetType::~TargetType()
{
}

bool TargetType::valid() const
{
	return (m_flags & FLAG_MAIN_VARIANT) != 0;
}

bool TargetType::has_fields() const
{
	return (m_flags & FLAG_FIELDS) != 0;
}

const std::map<std::size_t, TargetType::Field> &TargetType::fields() const
{
	return m_fields;
}

std::string TargetType::name() const
{
	const char *error_name = "<error>";
	const char *anonymous_name = "<anonymous>";

	if (!valid())
		return { error_name };

	tree name_tree = TYPE_NAME(m_main_variant);
	if (!name_tree)
		return { anonymous_name };

	if (TREE_CODE(name_tree) == TYPE_DECL && DECL_NAME(name_tree))
		return { IDENTIFIER_POINTER(DECL_NAME(name_tree)) };
	else if (TREE_CODE(name_tree) == IDENTIFIER_NODE)
		return { IDENTIFIER_POINTER(name_tree) };

	return { anonymous_name };
}

const TargetType::Field *TargetType::field(std::size_t off, bool exact) const
{
	if (!valid() || !(m_flags & FLAG_FIELDS))
		return nullptr;

	auto it = m_fields.upper_bound(off); // Next element
	if (it == m_fields.begin())
		return nullptr;

	--it; // Element of interest

	const TargetType::Field &maybe = it->second;

	if (off >= maybe.offset + maybe.size)
		return nullptr;

	if (exact && maybe.offset != off)
		return nullptr;

	return &maybe;
}

UID TargetType::uid() const
{
	return m_uid;
}

std::size_t TargetType::size() const
{
	if (!valid() || !(m_flags & FLAG_FIELDS))
		return 0;

	return m_size;
}

PINPOINT_GC_PRESERVE_CALLBACK()
{
	for (const auto &[uid, target] : targets)
		target.gc_preserve();
}

void TargetType::gc_preserve() const
{
	PINPOINT_GC_MARK_TREE(m_main_variant);
}

void TargetType::add(tree t)
{
	if (find(t) != nullptr)
		return;

	TargetType tmp{ t };
	if (!tmp.valid())
		return;

	tmp.m_uid = next_uid++;
	targets.emplace(tmp.m_uid, tmp);
}

std::size_t TargetType::count()
{
	return targets.size();
}

const TargetType *TargetType::find(tree t)
{
	tree main_variant = get_record_main_variant(t);
	if (!main_variant)
		return nullptr;

	for (const auto &[uid, target] : targets) {
		if (lang_hooks.types_compatible_p(main_variant,
						  target.m_main_variant))
			return &target;
	}

	return nullptr;
}

TargetType *TargetType::find_mutable(tree t)
{
	tree main_variant = get_record_main_variant(t);
	if (!main_variant)
		return nullptr;

	for (auto &[uid, target] : targets) {
		if (lang_hooks.types_compatible_p(main_variant,
						  target.m_main_variant))
			return &target;
	}

	return nullptr;
}

const TargetType *TargetType::find(UID uid)
{
	auto it = targets.find(uid);
	if (it == targets.end())
		return nullptr;

	return &it->second;
}

/*
 * Convert GCC's byte + bit field layout into the byte-oriented layout model
 * used by SPSLR metadata.
 *
 * Bitfields and sub-byte/overlapping storage are marked dangerous because the
 * current runtime randomizer can only move independently addressable byte ranges.
 */

bool field_info(tree field_decl, std::size_t *offset, std::size_t *size,
		std::size_t *alignment, bool *bitfield)
{
	if (!field_decl || TREE_CODE(field_decl) != FIELD_DECL)
		return false;

	tree field_off_t = DECL_FIELD_OFFSET(field_decl);
	if (!field_off_t || TREE_CODE(field_off_t) != INTEGER_CST)
		return false;

	tree field_bit_off_t = DECL_FIELD_BIT_OFFSET(field_decl);
	if (!field_bit_off_t || TREE_CODE(field_bit_off_t) != INTEGER_CST)
		return false;

	tree field_size_t = DECL_SIZE(field_decl);
	if (!field_size_t || TREE_CODE(field_size_t) != INTEGER_CST)
		return false;

	HOST_WIDE_INT tmp_byte_offset = tree_to_uhwi(field_off_t);
	HOST_WIDE_INT tmp_bit_offset = tree_to_uhwi(field_bit_off_t);
	HOST_WIDE_INT tmp_bit_size = tree_to_uhwi(field_size_t);

	HOST_WIDE_INT bit_offset_bytes = tmp_bit_offset / 8;
	tmp_byte_offset += bit_offset_bytes;
	tmp_bit_offset -= bit_offset_bytes * 8;

	bool tmp_bitfield = (DECL_BIT_FIELD_TYPE(field_decl) != NULL_TREE);
	tmp_bitfield |= !(tmp_bit_size % 8 == 0 && tmp_bit_offset == 0);

	tmp_bit_size += tmp_bit_offset;

	HOST_WIDE_INT tmp_bit_overhang = tmp_bit_size % 8;
	if (tmp_bit_overhang != 0)
		tmp_bit_size += (8 - tmp_bit_overhang);

	HOST_WIDE_INT tmp_align_bits = DECL_ALIGN(field_decl);
	if (tmp_align_bits <= 0 && TREE_TYPE(field_decl))
		tmp_align_bits = TYPE_ALIGN(TREE_TYPE(field_decl));
	if (tmp_align_bits <= 0)
		tmp_align_bits = BITS_PER_UNIT;

	std::size_t tmp_alignment = static_cast<std::size_t>(
		(tmp_align_bits + BITS_PER_UNIT - 1) / BITS_PER_UNIT);
	if (tmp_alignment == 0)
		tmp_alignment = 1;

	if (offset)
		*offset = static_cast<std::size_t>(tmp_byte_offset);

	if (size)
		*size = static_cast<std::size_t>(tmp_bit_size / 8);

	if (alignment)
		*alignment = tmp_alignment;

	if (bitfield)
		*bitfield = tmp_bitfield;

	return true;
}

bool TargetType::reference(tree ref, UID &target, std::size_t &offset)
{
	if (!ref || TREE_CODE(ref) != COMPONENT_REF)
		return false;

	tree base = TREE_OPERAND(ref, 0);
	if (!base)
		return false;

	tree base_type = TREE_TYPE(base);
	if (!base_type)
		return false;

	const TargetType *base_target = TargetType::find(base_type);
	if (!base_target)
		return false;

	target = base_target->uid();

	tree field_decl = TREE_OPERAND(ref, 1);

	if (!field_info(field_decl, &offset, nullptr, nullptr, nullptr))
		return false;

	const Field *f = base_target->field(offset, false);
	if (!f || (f->flags & Field::FLAG_DANGEROUS))
		return false;

	return true;
}

const std::unordered_map<UID, TargetType> &TargetType::all()
{
	return targets;
}

void TargetType::reset()
{
	targets.clear();
	next_uid = 0;
}

static bool
foreach_record_field(tree t,
		     std::function<bool(const TargetType::Field &)> callback)
{
	if (!t || TREE_CODE(t) != RECORD_TYPE)
		return false;

	if (!COMPLETE_TYPE_P(t))
		return false;

	for (tree field_decl = TYPE_FIELDS(t); field_decl;
	     field_decl = DECL_CHAIN(field_decl)) {
		if (TREE_CODE(field_decl) != FIELD_DECL)
			continue;

		TargetType::Field field;
		bool is_bitfield;

		if (!field_info(field_decl, &field.offset, &field.size,
				&field.alignment, &is_bitfield))
			return false;

		tree attrs = DECL_ATTRIBUTES(field_decl);
		bool is_fixed = lookup_attribute(SPSLR_FIELD_FIXED_ATTRIBUTE,
						 attrs) != NULL_TREE;

		field.flags = ((is_fixed || is_bitfield) ?
				       TargetType::Field::FLAG_DANGEROUS :
				       0);

		if (!callback(field))
			return false;
	}

	return true;
}

/*
 * Merge overlapping fields into one dangerous region.
 *
 * GCC can expose fields that overlap through bitfields, zero-sized members,
 * padding-sensitive constructs, or frontend-specific layout artifacts. SPSLR
 * keeps the containing byte range fixed rather than pretending those pieces
 * can be randomized independently.
 */

static bool field_map_add(std::map<std::size_t, TargetType::Field> &map,
			  const TargetType::Field &field)
{
	TargetType::Field tmp_field;
	tmp_field.offset = field.offset;
	tmp_field.size = (field.size == 0 ? 1 : field.size);
	tmp_field.alignment = (field.alignment == 0 ? 1 : field.alignment);
	tmp_field.flags =
		(field.size == 0 ? TargetType::Field::FLAG_DANGEROUS : 0) |
		field.flags;

	// Overlaps are dangerous -> remove and integrate into member
	auto overlap_end = map.lower_bound(tmp_field.offset + tmp_field.size);
	for (auto it = std::make_reverse_iterator(overlap_end);
	     it != map.rend();) {
		const TargetType::Field &existing_field = it->second;

		if (existing_field.offset + existing_field.size <=
		    tmp_field.offset)
			break;

		auto combined_end = std::max<decltype(tmp_field.offset)>(
			tmp_field.offset + tmp_field.size,
			existing_field.offset + existing_field.size);

		auto combined_offset = std::min<decltype(tmp_field.offset)>(
			tmp_field.offset, existing_field.offset);
		auto combined_size = combined_end - combined_offset;

		tmp_field.flags |= (existing_field.flags |
				    TargetType::Field::FLAG_DANGEROUS);
		tmp_field.offset = combined_offset;

		std::size_t required_alignment =
			std::max(tmp_field.alignment, existing_field.alignment);

		if (tmp_field.offset % required_alignment != 0)
			pinpoint_fatal(
				"encountered invalid overlapping fields");

		tmp_field.alignment = required_alignment;
		tmp_field.size = combined_size;

		// Erase overlapping member
		auto tmp_forward = std::prev(it.base());
		tmp_forward = map.erase(tmp_forward);
		it = std::make_reverse_iterator(tmp_forward);
	}

	map.emplace(tmp_field.offset, tmp_field);
	return true;
}

bool TargetType::fetch_fields(bool redo)
{
	if (!valid())
		return false;

	if ((m_flags & FLAG_FIELDS) != 0 && !redo)
		return true;

	m_flags &= ~FLAG_FIELDS;
	m_fields.clear();

	std::map<std::size_t, Field> tmp_fields;

	auto per_field_callback = [&tmp_fields](const Field &field) -> bool {
		return field_map_add(tmp_fields, field);
	};

	if (!foreach_record_field(m_main_variant, per_field_callback))
		return false;

	// Get struct size

	tree size_tree = TYPE_SIZE(m_main_variant);
	if (!size_tree || TREE_CODE(size_tree) != INTEGER_CST)
		return false;

	HOST_WIDE_INT size_bits = tree_to_uhwi(size_tree);
	if (size_bits < 0 || size_bits % 8 != 0)
		return false;

	m_size = static_cast<std::size_t>(size_bits / 8);

	// Everything done

	m_fields = std::move(tmp_fields);
	m_flags |= FLAG_FIELDS;
	return true;
}
