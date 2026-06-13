#include <string>
#include <vector>
#include <cstdint>
#include <getopt.h>
#include <filesystem>

#include "accumulation.h"
#include "emit.h"

#include "patchcompile_error.h"

bool patchcompile_verbose_enabled;

/*
 * spslr_patchcompile is the bridge between compiler metadata and runtime
 * selfpatch descriptors.
 *
 * It consumes textual `.spslr` files emitted by pinpoint, merges compatible
 * targets across compilation units, resolves CU-local target IDs to global
 * runtime target IDs, and emits assembly defining the descriptor symbols read
 * by selfpatch.
 *
 * For module builds, --load-targets imports the executable's target namespace
 * and --no-new-targets ensures the module does not introduce randomization
 * targets unknown to the already-running executable.
 */

struct OPTIONS {
	std::string out_file;
	std::string load_targets_file;
	std::string dump_targets_file;
	std::vector<std::string> spslr_files;
	bool no_new_targets = false;
	bool is_module = false;
};

int main(int argc, char **argv)
{
	static option long_options[] = {
		{ "help", no_argument, 0, 0 },
		{ "verbose", no_argument, 0, 0 },
		{ "out", required_argument, 0, 0 },
		{ "load-targets", required_argument, 0, 0 },
		{ "dump-targets", required_argument, 0, 0 },
		{ "no-new-targets", no_argument, 0, 0 },
		{ "module", no_argument, 0, 0 },
		{ 0, 0, 0, 0 }
	};

	OPTIONS opts{};
	int option_index = 0;
	int c;

	patchcompile_verbose_enabled = false;

	while ((c = getopt_long(argc, argv, "", long_options, &option_index)) !=
	       -1) {
		const option &opt = long_options[option_index];
		std::string optname{ opt.name };

		if (optname == "help") {
			patchcompile_info(
				"\nUsage:\n"
				"  spslr_patchcompile --out=<file> [options] <file> ...\n\n"
				"Options:\n"
				"  --help\n"
				"  --verbose\n"
				"  --load-targets=<file>\n"
				"  --dump-targets=<file>\n"
				"  --no-new-targets\n"
				"  --module\n");
			return 0;
		} else if (optname == "verbose") {
			patchcompile_verbose_enabled = true;
		} else if (optname == "out") {
			opts.out_file = optarg;
		} else if (optname == "load-targets") {
			opts.load_targets_file = optarg;
		} else if (optname == "dump-targets") {
			opts.dump_targets_file = optarg;
		} else if (optname == "no-new-targets") {
			opts.no_new_targets = true;
		} else if (optname == "module") {
			opts.is_module = true;
		} else {
			patchcompile_error("invalid option '%s', try '--help'",
					   optname.c_str());
			return 1;
		}
	}

	for (int i = optind; i < argc; ++i)
		opts.spslr_files.emplace_back(argv[i]);

	if (opts.out_file.empty()) {
		patchcompile_error(
			"missing output file path, supply it via '--out=<file>'");
		return 1;
	}

	if (opts.spslr_files.empty()) {
		patchcompile_error(
			"missing spslr meta data files, pass them as positional arguments");
		return 1;
	}

	/*
	 * --no-new-targets only makes sense with a preloaded target map; otherwise
	 * there is no existing namespace to validate against.
	 */
	if (opts.no_new_targets && opts.load_targets_file.empty()) {
		patchcompile_error("--no-new-targets requires --load-targets");
		return 1;
	}

	/*
	 * Modules should always be based on a host target map and not add any new
	 * targets.
	 */
	if ((opts.load_targets_file.empty() || !opts.no_new_targets) &&
	    opts.is_module) {
		patchcompile_error(
			"--module requires --load-targets and --no-new-targets");
		return 1;
	}

	if (!opts.load_targets_file.empty()) {
		if (!load_target_map(opts.load_targets_file)) {
			patchcompile_error("failed to load target map from %s",
					   opts.load_targets_file.c_str());
			return 1;
		}
	}

	if (!accumulate(opts.spslr_files, opts.no_new_targets)) {
		patchcompile_error("failed to accumulate meta data");
		return 1;
	}

	std::filesystem::path out_path{ opts.out_file };
	if (out_path.has_parent_path()) {
		std::error_code ec;
		std::filesystem::create_directories(out_path.parent_path(), ec);
		if (ec) {
			patchcompile_error(
				"failed to create output directory '%s' with error '%s'",
				out_path.parent_path().c_str(),
				ec.message().c_str());
			return 1;
		}
	}

	std::ofstream out(out_path);
	if (!out) {
		patchcompile_error("failed to open output file '%s'",
				   opts.out_file.c_str());
		return 1;
	}

	/*
	 * The output is assembly rather than binary data so the normal assembler/linker
	 * can resolve symbols referenced by ipin and dpin records.
	 */
	if (!emit_patcher_program_asm(out, opts.is_module)) {
		patchcompile_error("failed to write spslr section");
		return 1;
	}

	if (!opts.dump_targets_file.empty()) {
		if (!dump_target_map(opts.dump_targets_file)) {
			patchcompile_error("failed to write target map to '%s'",
					   opts.dump_targets_file.c_str());
			return 1;
		}
	}

	return 0;
}
