#include <final.h>
#include <stage0.h>
#include <stage2.h>
#include <string>
#include <filesystem>
#include <fstream>
#include <unordered_set>

#include <safe-input.h>
#include <safe-output.h>
#include <safe-md5.h>
#include <pinpoint_config.h>
#include <pinpoint_error.h>

/*
 * Finish-unit emits the per-compilation-unit `.spslr` metadata consumed by
 * patchcompile.
 *
 * Only information that survived all earlier filtering should be dumped here:
 * target layouts, data pins for static objects, and stage2 instruction pins
 * whose immediates exist in the final object.
 */

static bool output_file_known = false;
static std::string src_file, output_file, cu_hash;

void set_output_file(const char *path)
{
	namespace fs = std::filesystem;

	if (!path) {
		output_file_known = false;
		return;
	}

	output_file = fs::weakly_canonical(path).generic_string();
	output_file_known = true;
}

bool has_output_file()
{
	return output_file_known;
}

const char *get_output_file()
{
	return output_file_known ? output_file.c_str() : nullptr;
}

bool init_src_file()
{
	namespace fs = std::filesystem;

	if (!main_input_filename)
		return false;

	src_file = fs::weakly_canonical(fs::absolute(main_input_filename))
			   .generic_string();
	return true;
}

const char *get_src_file()
{
	return src_file.c_str();
}

bool init_cu_hash()
{
	if (!has_output_file())
		return false;

	unsigned char md5_digest[16];
	const char *s = get_output_file();
	md5_buffer(s, strlen(s), md5_digest);

	char md5_digest_hex[33] = {};
	for (int i = 0; i < 16; i++)
		sprintf(md5_digest_hex + i * 2, "%02x", md5_digest[i]);

	cu_hash = std::string{ md5_digest_hex };
	return true;
}

const char *get_cu_hash()
{
	return cu_hash.c_str();
}

static void emit_dpin_alias_labels()
{
	for (const DataPin &dpin : DataPin::all()) {
		if (dpin.pin_symbol.empty() || dpin.symbol.empty())
			pinpoint_fatal(
				"emit_dpin_alias_labels got incomplete data pin");

		fprintf(asm_out_file, ".globl %s\n", dpin.pin_symbol.c_str());
		fprintf(asm_out_file, ".set %s, %s\n", dpin.pin_symbol.c_str(),
			dpin.symbol.c_str());
	}
}

static std::ofstream open_spslr_output_file()
{
	namespace fs = std::filesystem;

	fs::path meta_path{ get_output_file() };
	fs::create_directories(meta_path.parent_path());

	pinpoint_debug(
		"dumping metadata for source file '%s' (CU hash: %s) to '%s'",
		get_src_file(), get_cu_hash(), get_output_file());

	std::ofstream out(meta_path);
	if (!out)
		pinpoint_fatal(
			"open_spslr_output_file failed to open spslr dump file");

	return std::move(out);
}

void on_finish_unit(void *plugin_data, void *user_data)
{
	std::string cu_uid = get_cu_hash();

	// Emit globally unique data pin label for each static object to be randomized

	emit_dpin_alias_labels();

	// Dump all accumulated data to spslr file

	std::ofstream out = open_spslr_output_file();

	// Header associates data with compilation unit

	out << "SPSLR " << get_src_file() << " " << cu_uid << std::endl;

	// Construct set of all targets that are used by at least 1 ipin or dpin

	std::unordered_set<UID> used_targets;

	for (const DataPin &dpin : DataPin::all()) {
		for (const DataPin::Component &c : dpin.components)
			used_targets.insert(c.target);
	}

	for (const auto &[uid, ipin] : s2_pins())
		used_targets.insert(ipin.target);

	// Dump all USED target structs

	for (const auto &[uid, target] : TargetType::all()) {
		if (used_targets.find(uid) == used_targets.end())
			continue;

		// target <name> <local uid> <size> <field count>
		out << "target " << target.name() << " " << uid << " "
		    << target.size() << " " << target.fields().size()
		    << std::endl;

		if (!target.has_fields())
			pinpoint_fatal(
				"on_finish_unit encountered incomplete target type");

		for (const auto &[off, field] : target.fields()) {
			// f <offset> <size> <alignment> <flags>
			out << "f " << field.offset << " " << field.size << " "
			    << field.alignment << " " << field.flags
			    << std::endl;
		}
	}

	// Dump all data pins

	for (const DataPin &dpin : DataPin::all()) {
		for (const DataPin::Component &c : dpin.components) {
			// dpin <symbol> <offset> <level> <target uid>
			out << "dpin " << " " << dpin.pin_symbol << " "
			    << c.offset << " " << c.level << " " << c.target
			    << std::endl;
		}
	}

	// Dump all instruction pins

	for (const auto &[uid, ipin] : s2_pins()) {
		// ipin <symbol> <target uid> <field offset> <immediate size>
		out << "ipin " << ipin.symbol << " " << ipin.target << " "
		    << ipin.offset << " " << ipin.imm_size << std::endl;
	}
}
