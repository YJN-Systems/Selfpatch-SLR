#pragma once
#include <string>
#include <cstddef>
#include <unordered_map>
#include <map>
#include <list>
#include <optional>
#include <cstdint>
#include <vector>

/*
 * Per-CU instruction pin parsed from pinpoint metadata.
 *
 * local_target is the target UID as seen by the originating compilation unit.
 * patchcompile resolves it to a global target UID before emission.
 */
struct IPIN {
	std::string symbol;
	std::size_t local_target;
	std::size_t field_offset;
	std::size_t imm_size;
};

/*
 * Per-CU data pin for one linker-visible object symbol.
 *
 * A single symbol may contain multiple randomized target instances, for example
 * nested structs or array elements. Each component describes one such instance.
 */
struct DPIN {
	struct COMPONENT {
		std::size_t offset;

		/*
		 * Nesting depth within the containing object. Larger values are patched first
		 * so embedded target objects are rewritten before their containers.
		 */
		std::size_t level;
		std::size_t target; // local pin -> local target
	};

	std::string symbol;
	std::list<COMPONENT> components;
};

struct FIELD {
	std::size_t offset;
	std::size_t size;
	std::size_t alignment;
	std::size_t flags;
	std::size_t idx;
};

struct TARGET {
	std::string name;
	std::size_t size;
	std::map<std::size_t, FIELD> fields;
};

/*
 * Metadata collected from one `.spslr` file.
 *
 * local_targets maps the CU-local target namespace emitted by pinpoint to the
 * global target namespace constructed by patchcompile.
 */
struct CU {
	std::unordered_map<std::size_t, std::size_t> local_targets;
	std::unordered_map<std::string, IPIN> ipins;
	std::unordered_map<std::string, DPIN> dpins;
};

extern std::unordered_map<std::size_t, TARGET> targets;
extern std::unordered_map<std::string, CU> units;

bool accumulate(const std::vector<std::string> &spslr_files,
		bool no_new_targets);
bool dump_target_map(const std::string &path);
bool load_target_map(const std::string &path);
