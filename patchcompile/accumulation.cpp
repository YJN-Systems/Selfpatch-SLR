#include "accumulation.h"

#include <filesystem>
#include <fstream>

#include "patchcompile_error.h"

namespace fs = std::filesystem;

/*
 * Accumulated metadata model.
 *
 * targets is indexed by global target UID. units is indexed by the per-CU UID
 * symbol emitted by pinpoint and preserves each CU's local-to-global target map.
 */
static std::size_t next_global_target_uid = 0;
std::unordered_map<std::size_t, TARGET> targets;
std::unordered_map<std::string, CU> units;

/*
 * Decide whether two target layouts can share one runtime randomization.
 *
 * Same-named types from different compilation units are merged only if their
 * byte layout is identical. If the layouts differ, they must get detached
 * randomization state because one permutation could not safely describe both.
 */
static bool global_target_field_cmp(const TARGET &a, const TARGET &b)
{
	if (a.fields.size() != b.fields.size())
		return false;

	auto ita = a.fields.begin();
	auto itb = b.fields.begin();

	for (; ita != a.fields.end() && itb != b.fields.end(); ++ita, ++itb) {
		const FIELD &fa = ita->second;
		const FIELD &fb = itb->second;

		if (fa.offset != fb.offset)
			return false;
		if (fa.size != fb.size)
			return false;
		if (fa.alignment != fb.alignment)
			return false;
		if (fa.flags != fb.flags)
			return false;
	}

	return true;
}

static bool global_target_cmp(const TARGET &a, const TARGET &b)
{
	if (a.name != b.name)
		return false;

	if (a.size != b.size || !global_target_field_cmp(a, b)) {
		patchcompile_warn(
			"got different definitions of '%s' -> detached randomization",
			a.name.c_str());
		return false;
	}

	return true;
}

/*
 * Intern a CU-local target into the global target table.
 *
 * The returned UID is stable for the rest of this patchcompile invocation and
 * becomes the runtime target index used by emitted ipin/dpin descriptors.
 */
static std::size_t accumulate_global_target(TARGET &&target, bool &was_new)
{
	was_new = false;
	for (const auto &[guid, gtarget] : targets) {
		if (global_target_cmp(gtarget, target))
			return guid;
	}

	was_new = true;

	std::size_t guid = next_global_target_uid++;
	targets.emplace(guid, std::move(target));
	return guid;
}

/*
 * pinpoint emits one textual `.spslr` file per compilation unit:
 *
 *   SPSLR <source-file> <cu-uid-symbol>
 *
 *   target <name> <local-target-uid> <sizeof(target)> <field-count>
 *   f      <offset> <size> <alignment> <flags>
 *
 *   ipin   <immediate-symbol> <local-target-uid> <field-offset> <imm-size>
 *   dpin   <object-symbol> <object-offset> <nesting-level> <local-target-uid>
 *
 * Target UIDs in this file are local to the originating CU. This parser keeps
 * that local namespace inside CU::local_targets and resolves it to global UIDs
 * only after all compatible targets have been merged.
 */
static bool accumulate_file(const fs::path &path, bool no_new_targets)
{
	std::ifstream infile(path);
	if (!infile)
		return false;

	std::string err_file_cppstr = path.generic_string();
	const char *err_file = err_file_cppstr.c_str();
	std::size_t err_line = 1; // One header line

	std::string hdr_line;
	if (!std::getline(infile, hdr_line))
		return false;

	std::istringstream hdr_iss(hdr_line);

	std::string hdr_magic, hdr_cu_file, hdr_cu_uid;

	if (!(hdr_iss >> hdr_magic) || hdr_magic != "SPSLR")
		return false;

	if (!(hdr_iss >> hdr_cu_file) || !(hdr_iss >> hdr_cu_uid))
		return false;

	if (units.contains(hdr_cu_uid)) {
		patchcompile_file_error(err_file, err_line,
					"duplicate compilation unit UID '%s'",
					hdr_cu_uid.c_str());
		return false;
	}

	patchcompile_debug("parsing meta data from %s ...", err_file);

	units.emplace(hdr_cu_uid, CU{});
	CU &cu = units.at(hdr_cu_uid);

	std::string line;
	while (std::getline(infile, line)) {
		err_line++;

		if (line.empty())
			continue;

		std::istringstream iss(line);
		std::string type;

		if (!(iss >> type))
			return false;

		if (type == "target") {
			std::size_t err_line_target_base = err_line;

			TARGET target;

			std::size_t local_uid, field_count;

			if (!(iss >> target.name) || !(iss >> local_uid) ||
			    !(iss >> target.size) || !(iss >> field_count)) {
				patchcompile_file_error(
					err_file, err_line,
					"invalid target line: \"%s\"",
					line.c_str());
				return false;
			}

			if (cu.local_targets.contains(local_uid)) {
				patchcompile_file_error(
					err_file, err_line_target_base,
					"local target uid %llu has already been claimed",
					local_uid);
				return false;
			}

			for (std::size_t i = 0; i < field_count; i++) {
				err_line++;

				std::string fline;
				if (!std::getline(infile, fline)) {
					patchcompile_file_error(
						err_file, err_line,
						"missing field declaration, expected %llu fields",
						field_count);
					return false;
				}

				std::istringstream fiss(fline);

				std::string ftype;
				if (!(fiss >> ftype) || ftype != "f") {
					patchcompile_file_error(
						err_file, err_line,
						"invalid field declaration line: \"%s\"",
						fline.c_str());
					return false;
				}

				FIELD field;
				if (!(fiss >> field.offset) ||
				    !(fiss >> field.size) ||
				    !(fiss >> field.alignment) ||
				    !(fiss >> field.flags)) {
					patchcompile_file_error(
						err_file, err_line,
						"invalid field declaration line: \"%s\"",
						fline.c_str());
					return false;
				}

				// Note -> could do sanity checks here

				if (target.fields.contains(field.offset)) {
					patchcompile_file_error(
						err_file, err_line,
						"duplicate field offset %llu",
						field.offset);
					return false;
				}

				target.fields.emplace(field.offset, field);
			}

			/*
			 * Runtime metadata addresses fields by compact field index, not by byte offset.
			 * The map is ordered by offset, so this assigns deterministic original-layout
			 * indices.
			 */
			auto fit = target.fields.begin();
			for (std::size_t i = 0; i < field_count; i++) {
				fit->second.idx = i;
				fit++;
			}

			bool was_new = false;
			std::size_t global_target_uid =
				accumulate_global_target(std::move(target),
							 was_new);

			if (no_new_targets && was_new) {
				patchcompile_file_error(
					err_file, err_line_target_base,
					"encountered new target but --no-new-targets is set");
				return false;
			}

			cu.local_targets.emplace(local_uid, global_target_uid);
			continue;
		} else if (type == "ipin") {
			IPIN ipin;
			if (!(iss >> ipin.symbol) ||
			    !(iss >> ipin.local_target) ||
			    !(iss >> ipin.field_offset) ||
			    !(iss >> ipin.imm_size)) {
				patchcompile_file_error(
					err_file, err_line,
					"invalid ipin declaration: \"%s\"",
					line.c_str());
				return false;
			}

			if (!cu.local_targets.contains(ipin.local_target)) {
				patchcompile_file_error(
					err_file, err_line,
					"ipin references local target %llu which has not yet been parsed",
					ipin.local_target);
				return false;
			}

			/*
			 * An instruction immediate has exactly one patch descriptor. Duplicate symbols
			 * would mean two metadata records are trying to patch the same bytes.
			 */
			if (cu.ipins.contains(ipin.symbol)) {
				patchcompile_file_error(
					err_file, err_line,
					"duplicate ipin for symbol '%s'",
					ipin.symbol.c_str());
				return false;
			}

			cu.ipins.emplace(ipin.symbol, ipin);
			continue;
		} else if (type == "dpin") {
			std::string symbol;
			DPIN::COMPONENT comp;

			if (!(iss >> symbol) || !(iss >> comp.offset) ||
			    !(iss >> comp.level) || !(iss >> comp.target)) {
				patchcompile_file_error(
					err_file, err_line,
					"invalid dpin declaration: \"%s\"",
					line.c_str());
				return false;
			}

			/*
			 * Dpins are grouped by linker symbol because one static object can contain
			 * several randomized target instances. Emission later expands the components
			 * into individual runtime dpin records in patch order.
			 */
			if (!cu.dpins.contains(symbol))
				cu.dpins.emplace(symbol,
						 DPIN{ .symbol = symbol });

			DPIN &dpin = cu.dpins.at(symbol);

			if (!cu.local_targets.contains(comp.target)) {
				patchcompile_file_error(
					err_file, err_line,
					"dpin references local target %llu which has not yet been parsed",
					comp.target);
				return false;
			}

			dpin.components.push_back(comp);
			continue;
		} else {
			patchcompile_file_error(
				err_file, err_line,
				"invalid meta data file line of type '%s': \"%s\"",
				type.c_str(), line.c_str());
			return false;
		}
	}

	patchcompile_debug(
		"metadata summary for CU %s (source %s): %zu targets, %zu ipins, %zu dpins",
		hdr_cu_uid.c_str(), hdr_cu_file.c_str(),
		cu.local_targets.size(), cu.ipins.size(), cu.dpins.size());

	for (const auto &[local_uid, global_uid] : cu.local_targets) {
		const TARGET &target = targets.at(global_uid);

		patchcompile_debug(
			"  target local=%zu global=%zu name=%s size=%zu fields=%zu",
			local_uid, global_uid, target.name.c_str(), target.size,
			target.fields.size());
	}

	return true;
}

bool accumulate(const std::vector<std::string> &spslr_files,
		bool no_new_targets)
{
	for (const std::string &spslr_file : spslr_files) {
		fs::path p{ spslr_file };

		if (!fs::exists(p) || !fs::is_regular_file(p)) {
			patchcompile_error(
				"failed to open meta data file at %s",
				spslr_file.c_str());
			return false;
		}

		if (!accumulate_file(p, no_new_targets)) {
			patchcompile_error(
				"failed to parse meta data file at %s",
				spslr_file.c_str());
			return false;
		}
	}

	return true;
}

/*
 * Persist the executable's global target namespace.
 *
 * Module patchcompile loads this map so module metadata can refer to exactly
 * the same target IDs and randomization state as the main executable.
 */
bool dump_target_map(const std::string &path)
{
	std::filesystem::path p{ path };
	if (p.has_parent_path()) {
		std::error_code ec;
		std::filesystem::create_directories(p.parent_path(), ec);
		if (ec) {
			patchcompile_error(
				"failed to create target map directory at %s: %s",
				p.parent_path().generic_string().c_str(),
				ec.message().c_str());
			return false;
		}
	}

	std::ofstream out(p);
	if (!out)
		return false;

	out << "SPSLR_TARGETS 1\n";

	for (const auto &[uid, t] : targets) {
		out << "target " << t.name << " " << uid << " " << t.size << " "
		    << t.fields.size() << "\n";

		for (const auto &[off, f] : t.fields) {
			(void)off;
			out << "f " << f.offset << " " << f.size << " "
			    << f.alignment << " " << f.flags << "\n";
		}
	}

	return !!out;
}

/*
 * Restore a previously emitted global target namespace.
 *
 * This is used for modules: they may reference existing randomized types, but
 * --no-new-targets prevents them from silently introducing layouts unknown to
 * the already-running executable.
 */
bool load_target_map(const std::string &path)
{
	std::ifstream in(path);
	if (!in)
		return false;

	std::string err_file_cppstr = path;
	const char *err_file = err_file_cppstr.c_str();
	std::size_t err_line = 1; // Header is one line

	std::string magic;
	std::size_t version = 0;
	if (!(in >> magic >> version) || magic != "SPSLR_TARGETS" ||
	    version != 1) {
		patchcompile_file_error(err_file, err_line,
					"invalid target map header");
		return false;
	}

	std::string line;
	std::getline(in, line); // consume rest of header line

	std::size_t max_uid = 0;
	bool have_any = false;

	while (std::getline(in, line)) {
		err_line++;

		if (line.empty())
			continue;

		std::istringstream iss(line);
		std::string tag;
		iss >> tag;

		if (tag != "target") {
			patchcompile_file_error(
				err_file, err_line,
				"target map file can only contain target and field entries");
			return false;
		}

		TARGET t{};
		std::size_t uid = 0;
		std::size_t field_count = 0;

		if (!(iss >> t.name >> uid >> t.size >> field_count)) {
			patchcompile_file_error(
				err_file, err_line,
				"invalid target declaration: \"%s\"",
				line.c_str());
			return false;
		}

		std::size_t err_line_target_base = err_line;

		for (std::size_t i = 0; i < field_count; ++i) {
			err_line++;

			std::string fline;
			if (!std::getline(in, fline)) {
				patchcompile_file_error(
					err_file, err_line,
					"missing field entry, expected %llu fields",
					field_count);
				return false;
			}

			std::istringstream fiss(fline);
			std::string ftag;
			FIELD f{};

			if (!(fiss >> ftag) || ftag != "f" ||
			    !(fiss >> f.offset >> f.size >> f.alignment >>
			      f.flags)) {
				patchcompile_file_error(
					err_file, err_line,
					"invalid field declaration: \"%s\"",
					fline.c_str());
				return false;
			}

			f.idx = i;
			if (t.fields.contains(f.offset)) {
				patchcompile_file_error(
					err_file, err_line,
					"duplicate field offset %llu",
					f.offset);
				return false;
			}
			t.fields.emplace(f.offset, f);
		}

		if (targets.contains(uid)) {
			patchcompile_file_error(
				err_file, err_line_target_base,
				"target uid %llu has already been claimed",
				uid);
			return false;
		}

		targets.emplace(uid, std::move(t));

		if (!have_any || uid > max_uid) {
			max_uid = uid;
			have_any = true;
		}
	}

	if (have_any)
		next_global_target_uid = (max_uid + 1);

	return true;
}
