#include <final.h>
#include <stage0.h>
#include <stage2.h>
#include <string>
#include <filesystem>
#include <vector>
#include <algorithm>

#include <safe-input.h>
#include <safe-output.h>
#include <pinpoint_config.h>
#include <pinpoint_error.h>

#include <layout_hash.h>
#include <serialize.h>

/*
 * Finish-unit emits the per-compilation-unit metadata into the object asm.
 *
 * Only information that survived all earlier filtering should be dumped here:
 * target layouts, data pins for static objects, and stage2 instruction pins
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
	UID uid;
	const TargetType *target;
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

static std::size_t field_index_for_offset(const TargetType &target,
					  std::size_t field_offset)
{
	std::size_t idx = 0;

	for (const auto &[off, field] : target.fields()) {
		if (off == field_offset)
			return idx;
		++idx;
	}

	pinpoint_fatal("finish-unit: field offset not found in target");
}

static std::vector<emitted_target> collect_all_targets()
{
	std::vector<emitted_target> out;
	out.reserve(TargetType::all().size());

	for (const auto &[uid, target] : TargetType::all()) {
		if (!target.valid())
			continue;

		if (!target.has_fields())
			pinpoint_fatal(
				"finish-unit: target has incomplete fields");

		out.push_back({
			.uid = uid,
			.target = &target,
			.hash = to_hash16(layout_hash(target)),
			.unit_target_idx = out.size(),
		});
	}

	return out;
}

static std::unordered_map<UID, std::size_t>
make_unit_target_idx_map(const std::vector<emitted_target> &targets)
{
	std::unordered_map<UID, std::size_t> out;

	for (const emitted_target &target : targets)
		out[target.uid] = target.unit_target_idx;

	return out;
}

static std::vector<emitted_dpin>
collect_dpins(const std::unordered_map<UID, std::size_t> &target_idx)
{
	std::vector<emitted_dpin> out;

	for (const DataPin &dpin : DataPin::all()) {
		if (dpin.symbol.empty())
			pinpoint_fatal(
				"finish-unit: data pin has empty symbol");

		std::vector<DataPin::Component> components(
			dpin.components.begin(), dpin.components.end());

		/*
		 * Data pins with deeper nesting level must be patched first.
		 */
		std::sort(components.begin(), components.end(),
			  [](const DataPin::Component &a,
			     const DataPin::Component &b) {
				  return a.level > b.level;
			  });

		for (const DataPin::Component &c : components) {
			const auto target_it = target_idx.find(c.target);

			if (target_it == target_idx.end())
				pinpoint_fatal(
					"finish-unit: dpin target not in unit target map");

			out.push_back({
				.addr_expr =
					add_offset_expr(dpin.symbol, c.offset),
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
		const TargetType &target = *ut.target;

		const std::string target_sym =
			selfpatch::target_symbol(ut.hash);
		const std::string layout_sym =
			selfpatch::target_layout_symbol(ut.hash);
		const std::string fields_sym =
			selfpatch::make_local_label("target_fields");

		const std::string target_name =
			selfpatch::emit_strtab_entry(out, target.name());

		selfpatch::emit_targets_section(out, ut.hash);
		selfpatch::emit_hidden_global_label(out, target_sym);

		selfpatch::target_desc target_desc{
			.hash = ut.hash,
			.name_label = target_name,
			.layout_symbol = layout_sym,
		};

		selfpatch::emit_target(out, target_desc);
		selfpatch::emit_pop_section(out);

		selfpatch::emit_target_layouts_section(out, ut.hash);
		selfpatch::emit_hidden_global_label(out, layout_sym);

		selfpatch::target_layout_desc layout_desc{
			.size = target.size(),
			.field_cnt = target.fields().size(),
			.fields_symbol = fields_sym,
		};

		selfpatch::emit_target_layout(out, layout_desc);

		selfpatch::emit_label(out, fields_sym);

		for (const auto &[off, field] : target.fields()) {
			const std::string field_name =
				selfpatch::emit_strtab_entry(out, field.name);

			selfpatch::target_field_desc field_desc{
				.name_label = field_name,
				.size = field.size,
				.offset = field.offset,
				.alignment = field.alignment,
				.flags = field.flags,
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
		       const std::unordered_map<UID, std::size_t> &target_idx,
		       const std::string &ipins_sym)
{
	std::vector<std::string> expr_syms;
	expr_syms.reserve(s2_pins().size());

	for (std::size_t i = 0; i < s2_pins().size(); ++i)
		expr_syms.push_back(selfpatch::make_local_label("ipin_expr"));

	selfpatch::emit_ipins_section(out);
	selfpatch::emit_label(out, ipins_sym);

	std::size_t i = 0;
	for (const auto &[uid, ipin] : s2_pins()) {
		selfpatch::ipin_desc desc{
			.addr_expr = ipin.symbol,
			.size = ipin.imm_size,
			.expr_symbol = expr_syms[i++],
		};

		selfpatch::emit_ipin(out, desc);
	}

	i = 0;
	for (const auto &[uid, ipin] : s2_pins()) {
		const auto target_it = target_idx.find(ipin.target);

		if (target_it == target_idx.end())
			pinpoint_fatal(
				"finish-unit: ipin target not in unit target map");

		const TargetType *target = TargetType::find(ipin.target);
		if (!target)
			pinpoint_fatal("finish-unit: ipin target missing");

		selfpatch::emit_label(out, expr_syms[i++]);

		selfpatch::ipin_expr_desc expr{
			.unit_target_idx = target_it->second,
			.field = field_index_for_offset(*target, ipin.offset),
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
		.ipin_cnt = s2_pins().size(),
		.ipins_symbol = ipins_sym,
		.dpin_cnt = dpins.size(),
		.dpins_symbol = dpins_sym,
	};

	selfpatch::emit_unit(out, unit);
	selfpatch::emit_pop_section(out);
}
