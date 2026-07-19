#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>
#include <array>

#include <safe-tree.h>

struct target {
	struct compressed_field {
		std::string name{};
		std::size_t offset{};
		std::size_t size{};
		std::size_t alignment{};
		bool fixed{};
	};

	static tree main_variant(tree type);
	static tree from_field(tree field_decl);

	static bool is_target(tree type);

	static std::string name(tree type);
	static std::vector<std::string> context_chain(tree type);
	static std::string qualified_name(tree type);

	static std::size_t size(tree type);

	static std::string field_name(tree field_decl);
	static std::size_t field_offset(tree field_decl);
	static bool field_has_size(tree field_decl);
	static std::size_t field_size(tree field_decl);
	static std::size_t field_alignment(tree field_decl);
	static bool field_is_bitfield(tree field_decl);
	static bool field_is_fixed(tree field_decl);

	static bool component_ref(tree ref, tree *field_decl);

	static const std::vector<compressed_field> &
	compressed_fields(tree type);

	using target_callback = std::function<void(tree target_type)>;
	static void iterate_targets(const target_callback &cb);
	static std::size_t target_count();

	static void validate(tree type);

	/* THe field index is into compressed_fields */
	static std::size_t field_index(tree field_decl);

	using layout_hash_t = std::array<std::byte, 16>;

	static bool is_validated_target(tree type);
	static const layout_hash_t &layout_hash(tree type);

	static void reset();
};
