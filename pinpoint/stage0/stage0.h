#pragma once
#include <cstddef>
#include <map>
#include <unordered_map>
#include <string>
#include <list>
#include <limits>

#include <safe-tree.h>
#include <safe-gimple.h>

using UID = std::size_t;
constexpr UID UID_INVALID = std::numeric_limits<UID>::max();

class TargetType {
    public:
	struct Field {
		static constexpr std::size_t FLAG_DANGEROUS = 1;

		std::size_t offset;
		std::size_t size;
		std::size_t alignment;
		std::size_t flags;
	};

    private:
	static constexpr std::size_t FLAG_MAIN_VARIANT = 1;
	static constexpr std::size_t FLAG_FIELDS = 2;

	UID m_uid;
	std::size_t m_flags;
	tree m_main_variant;
	std::size_t m_size;

	// Fields are identified by their offsets
	std::map<std::size_t, Field> m_fields;

    public:
	TargetType(const TargetType &other) = default;
	TargetType &operator=(const TargetType &other) = default;
	TargetType(TargetType &&other) = default;
	TargetType &operator=(TargetType &&other) = default;

	TargetType(tree t); // Does NOT fetch fields
	~TargetType();

	bool valid() const;
	bool has_fields() const;
	const std::map<std::size_t, Field> &fields() const;
	std::string name() const;
	const Field *field(std::size_t off, bool exact = true) const;
	UID uid() const;
	std::size_t size() const;
	void gc_preserve() const;

	static void add(tree t);
	static std::size_t count();
	static const TargetType *find(tree t); // O(n)
	static const TargetType *find(UID uid); // O(1)
	static bool reference(tree ref, UID &target, std::size_t &offset);
	static const std::unordered_map<UID, TargetType> &all();
	static void reset();

    private:
	friend void on_finish_type(void *, void *);
	bool fetch_fields(bool redo = false);
	static TargetType *find_mutable(tree t);
};

bool field_info(tree field_decl, std::size_t *offset, std::size_t *size,
		std::size_t *alignment, bool *bitfield);

/* Stage 0 offsetof separators are function calls, such as:
   SPSLR_PINPOINT_STAGE0_SEPARATOR(target, member offset) */

tree make_stage0_ast_separator(UID target, std::size_t offset);
gimple *make_stage0_gimple_separator(tree lhs, UID target, std::size_t offset);
bool is_stage0_separator(gimple *stmt, UID &target, std::size_t &offset);

struct DataPin {
	struct Component {
		std::size_t offset;
		std::size_t level;
		UID target;
	};

	std::string symbol; // potentially local object symbol
	std::string pin_symbol; // global alias symbol
	std::list<Component> components;

	static void reset();
	static const std::list<DataPin> &all();
};

void on_register_attributes(void *plugin_data, void *user_data);
void on_finish_type(void *plugin_data, void *user_data);
void on_preserve_component_ref(void *plugin_data, void *user_data);
void on_finish_decl(void *plugin_data, void *user_data);
void on_start_unit(void *plugin_data, void *user_data);

struct separate_offset_pass : gimple_opt_pass {
	separate_offset_pass(gcc::context *ctxt);
	unsigned int execute(function *fn) override;
};
