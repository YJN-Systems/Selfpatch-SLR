#include "serialize.h"

#include <cctype>

namespace selfpatch
{

namespace
{

constexpr std::string_view section_entry = "spslr_entry";
constexpr std::string_view section_units = "spslr_units";
constexpr std::string_view section_targets = "spslr_targets";
constexpr std::string_view section_target_layouts = "spslr_target_layouts";
constexpr std::string_view section_ipins = "spslr_ipins";
constexpr std::string_view section_dpins = "spslr_dpins";
constexpr std::string_view section_strtab = "spslr_strtab";
constexpr std::string_view section_cu_target_refs = "spslr_cu_target_refs";

std::size_t next_local_label_id = 0;

std::string quote_asm_string(std::string_view s)
{
	std::string out;
	out.reserve(s.size() + 8);
	out.push_back('"');

	for (unsigned char c : s) {
		switch (c) {
		case '\\':
			out += "\\\\";
			break;
		case '"':
			out += "\\\"";
			break;
		case '\n':
			out += "\\n";
			break;
		case '\r':
			out += "\\r";
			break;
		case '\t':
			out += "\\t";
			break;
		case '\0':
			out += "\\000";
			break;
		default:
			if (std::isprint(c)) {
				out.push_back(static_cast<char>(c));
			} else {
				char buf[5];
				std::snprintf(buf, sizeof(buf), "\\%03o", c);
				out += buf;
			}
			break;
		}
	}

	out.push_back('"');
	return out;
}

} // namespace

void emit_comment(FILE *out, std::string_view text)
{
	std::fprintf(out, "\n/* %.*s */\n", static_cast<int>(text.size()),
		     text.data());
}

void emit_push_section(FILE *out, std::string_view name)
{
	/* Future implementation should differentiate between module and host
	 * section permissions. Module relocations may cause DT_TEXTREL issues
	 * if the metadata sections are read-only.  */
	std::fprintf(out, ".pushsection %.*s,\"aw\",@progbits\n",
		     static_cast<int>(name.size()), name.data());
}

void emit_push_comdat_section(FILE *out, std::string_view name,
			      std::string_view group_symbol)
{
	/* Future implementation should differentiate between module and host
	 * section permissions. Module relocations may cause DT_TEXTREL issues
	 * if the metadata sections are read-only.  */
	std::fprintf(out, ".pushsection %.*s,\"awG\",@progbits,%.*s,comdat\n",
		     static_cast<int>(name.size()), name.data(),
		     static_cast<int>(group_symbol.size()),
		     group_symbol.data());
}

void emit_pop_section(FILE *out)
{
	std::fprintf(out, ".popsection\n");
}

void emit_units_section(FILE *out)
{
	emit_push_section(out, section_units);
}

void emit_targets_section(FILE *out, const hash16_t &hash)
{
	emit_push_comdat_section(out, section_targets,
				 comdat_target_symbol(hash));
}

void emit_target_layouts_section(FILE *out, const hash16_t &hash)
{
	emit_push_comdat_section(out, section_target_layouts,
				 comdat_target_symbol(hash));
}

void emit_ipins_section(FILE *out)
{
	emit_push_section(out, section_ipins);
}

void emit_dpins_section(FILE *out)
{
	emit_push_section(out, section_dpins);
}

void emit_strtab_section(FILE *out)
{
	emit_push_section(out, section_strtab);
}

void emit_cu_target_refs_section(FILE *out)
{
	emit_push_section(out, section_cu_target_refs);
}

void emit_label(FILE *out, std::string_view label)
{
	std::fprintf(out, "%.*s:\n", static_cast<int>(label.size()),
		     label.data());
}

void emit_hidden_global_label(FILE *out, std::string_view label)
{
	std::fprintf(out, ".globl %.*s\n", static_cast<int>(label.size()),
		     label.data());
	std::fprintf(out, ".hidden %.*s\n", static_cast<int>(label.size()),
		     label.data());
	emit_label(out, label);
}

std::string make_local_label(std::string_view stem)
{
	std::string out = ".Lspslr_";
	out.append(stem);
	out.push_back('_');
	out.append(std::to_string(next_local_label_id++));
	return out;
}

std::string emit_strtab_entry(FILE *out, std::string_view value)
{
	const std::string label = make_local_label("str");

	emit_strtab_section(out);
	emit_label(out, label);
	emit_c_string(out, value);
	emit_pop_section(out);

	return label;
}

void emit_unit(FILE *out, const unit_desc &unit)
{
	emit_quad_symbol(out, unit.source_label);
	emit_quad(out, unit.target_ref_cnt);
	emit_quad_symbol(out, unit.target_refs_symbol);
	emit_quad(out, unit.ipin_cnt);
	emit_quad_symbol(out, unit.ipins_symbol);
	emit_quad(out, unit.dpin_cnt);
	emit_quad_symbol(out, unit.dpins_symbol);
}

void emit_target(FILE *out, const target_desc &target)
{
	emit_bytes(out, target.hash.data(), target.hash.size());
	emit_quad_symbol(out, target.name_label);
	emit_quad_symbol(out, target.layout_symbol);
}

void emit_target_layout(FILE *out, const target_layout_desc &layout)
{
	emit_quad(out, layout.size);
	emit_quad(out, layout.field_cnt);
	emit_quad_symbol(out, layout.fields_symbol);
}

void emit_target_field(FILE *out, const target_field_desc &field)
{
	emit_quad_symbol(out, field.name_label);
	emit_quad(out, field.size);
	emit_quad(out, field.offset);
	emit_quad(out, field.alignment);
	emit_quad(out, field.flags);
}

void emit_target_ref(FILE *out, const target_ref_desc &target)
{
	emit_quad_symbol(out, target.target_symbol);
}

void emit_ipin(FILE *out, const ipin_desc &ipin)
{
	emit_quad_expr(out, ipin.addr_expr);
	emit_quad_expr(out, ipin.size_expr);
	emit_quad_symbol(out, ipin.expr_symbol);
}

void emit_dpin(FILE *out, const dpin_desc &dpin)
{
	emit_quad_expr(out, dpin.addr_expr);
	emit_quad(out, dpin.unit_target_idx);
}

void emit_ipin_expr(FILE *out, const ipin_expr_desc &expr)
{
	emit_quad(out, expr.unit_target_idx);
	emit_quad(out, expr.field);
}

void emit_quad(FILE *out, std::size_t value)
{
	std::fprintf(out, ".quad %zu\n", value);
}

void emit_quad_symbol(FILE *out, std::string_view symbol)
{
	emit_quad_expr(out, symbol);
}

void emit_quad_expr(FILE *out, std::string_view expr)
{
	std::fprintf(out, ".quad %.*s\n", static_cast<int>(expr.size()),
		     expr.data());
}

void emit_bytes(FILE *out, const void *data, std::size_t size)
{
	const auto *bytes = static_cast<const std::uint8_t *>(data);

	for (std::size_t i = 0; i < size; ++i) {
		if (i % 16 == 0)
			std::fprintf(out, ".byte ");
		else
			std::fprintf(out, ",");

		std::fprintf(out, "0x%02x", bytes[i]);

		if (i % 16 == 15 || i + 1 == size)
			std::fprintf(out, "\n");
	}
}

void emit_c_string(FILE *out, std::string_view value)
{
	const std::string quoted = quote_asm_string(value);
	std::fprintf(out, ".asciz %s\n", quoted.c_str());
}

std::string hash_hex(const hash16_t &hash)
{
	static constexpr char digits[] = "0123456789abcdef";

	std::string out;
	out.resize(hash.size() * 2);

	for (std::size_t i = 0; i < hash.size(); ++i) {
		out[i * 2] = digits[(hash[i] >> 4) & 0x0f];
		out[i * 2 + 1] = digits[hash[i] & 0x0f];
	}

	return out;
}

std::string comdat_target_symbol(const hash16_t &hash)
{
	return "__comdat_spslr_target_" + hash_hex(hash);
}

std::string target_symbol(const hash16_t &hash)
{
	return "__spslr_target_" + hash_hex(hash);
}

std::string target_hash_symbol(const hash16_t &hash)
{
	return "__spslr_target_hash_" + hash_hex(hash);
}

std::string target_layout_symbol(const hash16_t &hash)
{
	return "__spslr_target_layout_" + hash_hex(hash);
}

} // namespace selfpatch
