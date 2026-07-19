#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

namespace selfpatch
{

using hash16_t = std::array<std::uint8_t, 16>;

/*
 * Entry point is:
 *   __start_spslr_units
 *   __stop_spslr_units
 *   __start_spslr_targets
 *   __stop_spslr_targets
 */

struct unit_desc {
	std::string source_label;
	std::size_t target_ref_cnt = 0;
	std::string target_refs_symbol;
	std::size_t ipin_cnt = 0;
	std::string ipins_symbol;
	std::size_t dpin_cnt = 0;
	std::string dpins_symbol;
};

struct target_desc {
	hash16_t hash{};
	std::string name_label;
	std::string layout_symbol;
};

struct target_layout_desc {
	std::size_t size = 0;
	std::size_t field_cnt = 0;
	std::string fields_symbol;
};

struct target_field_desc {
	std::string name_label;
	std::size_t size = 0;
	std::size_t offset = 0;
	std::size_t alignment = 0;
	std::uint64_t flags = 0;
};

struct target_ref_desc {
	std::string target_symbol;
};

struct ipin_desc {
	std::string addr_expr;
	std::string size_expr;
	std::string expr_symbol;
};

struct dpin_desc {
	std::string addr_expr;
	std::size_t unit_target_idx = 0;
};

struct ipin_expr_desc {
	std::size_t unit_target_idx = 0;
	std::size_t field = 0;
};

void emit_comment(FILE *out, std::string_view text);

void emit_push_section(FILE *out, std::string_view name);
void emit_push_comdat_section(FILE *out, std::string_view name,
			      std::string_view group_symbol);
void emit_pop_section(FILE *out);

void emit_units_section(FILE *out);
void emit_targets_section(FILE *out, const hash16_t &hash);
void emit_target_layouts_section(FILE *out, const hash16_t &hash);
void emit_ipins_section(FILE *out);
void emit_dpins_section(FILE *out);
void emit_strtab_section(FILE *out);
void emit_cu_target_refs_section(FILE *out);

void emit_label(FILE *out, std::string_view label);
void emit_hidden_global_label(FILE *out, std::string_view label);

std::string make_local_label(std::string_view stem);
std::string emit_strtab_entry(FILE *out, std::string_view value);

void emit_unit(FILE *out, const unit_desc &unit);
void emit_target(FILE *out, const target_desc &target);
void emit_target_layout(FILE *out, const target_layout_desc &layout);
void emit_target_field(FILE *out, const target_field_desc &field);
void emit_target_ref(FILE *out, const target_ref_desc &target);
void emit_ipin(FILE *out, const ipin_desc &ipin);
void emit_dpin(FILE *out, const dpin_desc &dpin);
void emit_ipin_expr(FILE *out, const ipin_expr_desc &expr);

void emit_quad(FILE *out, std::size_t value);
void emit_quad_symbol(FILE *out, std::string_view symbol);
void emit_quad_expr(FILE *out, std::string_view expr);
void emit_bytes(FILE *out, const void *data, std::size_t size);
void emit_c_string(FILE *out, std::string_view value);

std::string hash_hex(const hash16_t &hash);
std::string comdat_target_symbol(const hash16_t &hash);
std::string target_symbol(const hash16_t &hash);
std::string target_hash_symbol(const hash16_t &hash);
std::string target_layout_symbol(const hash16_t &hash);

} // namespace selfpatch
