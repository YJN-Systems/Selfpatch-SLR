#pragma once
#include <cstddef>
#include <string>
#include <limits>
#include <map>

#include <safe-tree.h>
#include <safe-gimple.h>
#include <safe-rtl.h>

struct ipin {
	enum class state { pending, separator, pin, live };

	state status = state::pending;

	/* Field that the ipin refers to */
	tree field = NULL_TREE;

	/*
	 * The field-address and field-width symbols are available once the pin is
	 * marked live and its final inline assembly has been constructed.
	 */
	std::string symbol;
	std::string width_symbol;

	using handle = std::size_t;
	static constexpr handle invalid = std::numeric_limits<handle>::max();

	/* Register new ipin to track through compilation */
	static handle make(tree field);

	/* Construct an AST separator tree refering to an ipin */
	static tree make_ast_separator(handle pin);

	/* Construct a GIMPLE separator statement refering to an ipin */
	static gimple *make_gimple_separator(tree lhs, handle pin);

	/* Check if a GIMPLE statement is a separator refering to an ipin */
	static handle identify_gimple_separator(gimple *stmt);

	/* Construct a GIMPLE ipin - currently as ASM statement */
	static gimple *make_gimple_pin(tree lhs, handle pin);

	/* Check if an RTL instruction is an ipin */
	static handle identify_rtl_pin(rtx x);

	/* Mark an ipin as being present in the final asm */
	static void mark_live(handle pin, rtx at);
	static std::size_t live_count();

	/* Inspect the state of one or more ipins */
	static const std::map<handle, ipin> &inspect();
	static const ipin *inspect(handle pin);

	/* Reset ipin registry */
	static void reset();
};
