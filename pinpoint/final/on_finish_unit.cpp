#include <string>
#include <filesystem>
#include <vector>
#include <algorithm>
#include <unordered_map>

#include <pinpoint.h>
#include <passes.h>
#include <ipin_registry.h>
#include <dpin_registry.h>
#include <serialize.h>
#include <target_registry.h>
#include <safe-input.h>
#include <safe-output.h>

/*
 * Finish-unit emits the per-compilation-unit metadata into the object asm.
 *
 * Only information that survived all earlier filtering should be dumped here:
 * target layouts, data pins for static objects, and live instruction pins
 * whose immediates exist in the final object.
 */

namespace
{

static std::string src_filename()
{
	namespace fs = std::filesystem;

	if (!main_input_filename)
		return {};

	return fs::weakly_canonical(main_input_filename).generic_string();
}

using selfpatch::hash16_t;

struct emitted_target {
	tree target;
	hash16_t hash;
	std::size_t unit_target_idx;
};

struct emitted_dpin {
	std::string addr_expr;
	std::size_t unit_target_idx;
};

static hash16_t to_hash16(const std::array<std::byte, 16> &in)
{
	hash16_t out{};

	for (std::size_t i = 0; i < out.size(); ++i)
		out[i] = static_cast<std::uint8_t>(in[i]);

	return out;
}

static std::string add_offset_expr(const std::string &symbol, std::size_t off)
{
	if (off == 0)
		return symbol;

	return symbol + " + " + std::to_string(off);
}

static std::vector<emitted_target> collect_all_targets()
{
	std::vector<emitted_target> out;
	out.reserve(target::target_count());

	target::iterate_targets([&](tree t) {
		out.push_back({
			.target = t,
			.hash = to_hash16(target::layout_hash(t)),
			.unit_target_idx = out.size(),
		});
	});

	return out;
}

static std::unordered_map<tree, std::size_t>
make_unit_target_idx_map(const std::vector<emitted_target> &targets)
{
	std::unordered_map<tree, std::size_t> out;

	for (const emitted_target &target : targets)
		out[target.target] = target.unit_target_idx;

	return out;
}

static std::vector<emitted_dpin>
collect_dpins(const std::unordered_map<tree, std::size_t> &target_idx)
{
	std::vector<emitted_dpin> out;

	for (const dpin &pin : dpin::inspect()) {
		if (pin.symbol.empty())
			pinpoint_fatal(
				"finish-unit: data pin has empty symbol");

		std::vector<dpin::component> components(pin.components.begin(),
							pin.components.end());

		/*
		 * Data pins with deeper nesting level must be patched first.
		 */
		std::sort(components.begin(), components.end(),
			  [](const dpin::component &a,
			     const dpin::component &b) {
				  return a.level > b.level;
			  });

		for (const dpin::component &c : components) {
			const auto target_it = target_idx.find(c.target);

			if (target_it == target_idx.end())
				pinpoint_fatal(
					"finish-unit: dpin target not in unit target map");

			out.push_back({
				.addr_expr =
					add_offset_expr(pin.symbol, c.offset),
				.unit_target_idx = target_it->second,
			});
		}
	}

	return out;
}

static void emit_target_metadata(FILE *out,
				 const std::vector<emitted_target> &targets)
{
	for (const emitted_target &ut : targets) {
		const std::string target_sym =
			selfpatch::target_symbol(ut.hash);
		const std::string hash_sym =
			selfpatch::target_hash_symbol(ut.hash);
		const std::string layout_sym =
			selfpatch::target_layout_symbol(ut.hash);
		const std::string fields_sym =
			selfpatch::make_local_label("target_fields");

		const std::string target_name = selfpatch::emit_strtab_entry(
			out, target::qualified_name(ut.target));

		selfpatch::emit_targets_section(out, ut.hash);
		selfpatch::emit_hidden_global_label(out, target_sym);

		/* This can only be done here, because the hash is the first
		 * component of the target metadata. */
		selfpatch::emit_hidden_global_label(out, hash_sym);

		selfpatch::target_desc target_desc{
			.hash = ut.hash,
			.name_label = target_name,
			.layout_symbol = layout_sym,
		};

		selfpatch::emit_target(out, target_desc);
		selfpatch::emit_pop_section(out);

		selfpatch::emit_target_layouts_section(out, ut.hash);
		selfpatch::emit_hidden_global_label(out, layout_sym);

		const std::vector<target::compressed_field> &fields =
			target::compressed_fields(ut.target);

		selfpatch::target_layout_desc layout_desc{
			.size = target::size(ut.target),
			.field_cnt = fields.size(),
			.fields_symbol = fields_sym,
		};

		selfpatch::emit_target_layout(out, layout_desc);

		selfpatch::emit_label(out, fields_sym);

		for (const target::compressed_field &field : fields) {
			const std::string field_name =
				selfpatch::emit_strtab_entry(out, field.name);

			std::size_t field_flags = field.fixed ? 1 : 0;

			selfpatch::target_field_desc field_desc{
				.name_label = field_name,
				.size = field.size,
				.offset = field.offset,
				.alignment = field.alignment,
				.flags = field_flags,
			};

			selfpatch::emit_target_field(out, field_desc);
		}

		selfpatch::emit_pop_section(out);
	}
}

static void emit_unit_target_refs(FILE *out,
				  const std::vector<emitted_target> &targets,
				  const std::string &target_refs_sym)
{
	selfpatch::emit_cu_target_refs_section(out);
	selfpatch::emit_label(out, target_refs_sym);

	for (const emitted_target &target : targets) {
		selfpatch::target_ref_desc ref{
			.target_symbol = selfpatch::target_symbol(target.hash),
		};

		selfpatch::emit_target_ref(out, ref);
	}

	selfpatch::emit_pop_section(out);
}

static void emit_ipins(FILE *out,
		       const std::unordered_map<tree, std::size_t> &target_idx,
		       const std::string &ipins_sym)
{
	std::map<ipin::handle, std::string> expr_syms;

	selfpatch::emit_ipins_section(out);
	selfpatch::emit_label(out, ipins_sym);

	for (const auto &[h, pin] : ipin::inspect()) {
		if (pin.status != ipin::state::live)
			continue;

		if (pin.symbol.empty() || pin.width_symbol.empty())
			pinpoint_fatal(
				"finish-unit: live ipin is missing field symbols");

		std::string expr_sym = selfpatch::make_local_label("ipin_expr");
		expr_syms.emplace(h, expr_sym);

		selfpatch::ipin_desc desc{
			.addr_expr = pin.symbol,
			.size_expr = pin.width_symbol,
			.expr_symbol = expr_sym,
		};

		selfpatch::emit_ipin(out, desc);
	}

	for (const auto &[h, pin] : ipin::inspect()) {
		if (pin.status != ipin::state::live)
			continue;

		tree pin_target = target::from_field(pin.field);
		const auto target_it = target_idx.find(pin_target);

		if (target_it == target_idx.end())
			pinpoint_fatal(
				"finish-unit: ipin target not in unit target map");

		auto expr_sym_it = expr_syms.find(h);
		if (expr_sym_it == expr_syms.end())
			pinpoint_fatal("finish-unit: lost ipin expr symbol");

		selfpatch::emit_label(out, expr_sym_it->second);

		selfpatch::ipin_expr_desc expr{
			.unit_target_idx = target_it->second,
			.field = target::field_index(pin.field),
		};

		selfpatch::emit_ipin_expr(out, expr);
	}

	selfpatch::emit_pop_section(out);
}

static void emit_dpins(FILE *out, const std::vector<emitted_dpin> &dpins,
		       const std::string &dpins_sym)
{
	selfpatch::emit_dpins_section(out);
	selfpatch::emit_label(out, dpins_sym);

	for (const emitted_dpin &pin : dpins) {
		selfpatch::dpin_desc desc{
			.addr_expr = pin.addr_expr,
			.unit_target_idx = pin.unit_target_idx,
		};

		selfpatch::emit_dpin(out, desc);
	}

	selfpatch::emit_pop_section(out);
}

} // namespace

void on_finish_unit(void *plugin_data, void *user_data)
{
	FILE *out = asm_out_file;

	const std::vector<emitted_target> targets = collect_all_targets();
	const auto target_idx = make_unit_target_idx_map(targets);
	const std::vector<emitted_dpin> dpins = collect_dpins(target_idx);

	const std::string source_label =
		selfpatch::emit_strtab_entry(out, src_filename());

	const std::string target_refs_sym =
		selfpatch::make_local_label("target_refs");
	const std::string ipins_sym = selfpatch::make_local_label("ipins");
	const std::string dpins_sym = selfpatch::make_local_label("dpins");

	selfpatch::emit_comment(out, "SPSLR runtime metadata");

	emit_target_metadata(out, targets);
	emit_unit_target_refs(out, targets, target_refs_sym);
	emit_ipins(out, target_idx, ipins_sym);
	emit_dpins(out, dpins, dpins_sym);

	selfpatch::emit_units_section(out);

	selfpatch::unit_desc unit{
		.source_label = source_label,
		.target_ref_cnt = targets.size(),
		.target_refs_symbol = target_refs_sym,
		.ipin_cnt = ipin::live_count(),
		.ipins_symbol = ipins_sym,
		.dpin_cnt = dpins.size(),
		.dpins_symbol = dpins_sym,
	};

	selfpatch::emit_unit(out, unit);
	selfpatch::emit_pop_section(out);
}
