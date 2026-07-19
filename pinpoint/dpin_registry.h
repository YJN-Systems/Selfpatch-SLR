#pragma once

#include <cstddef>
#include <list>
#include <string>

#include <safe-tree.h>

struct dpin {
	struct component {
		std::size_t offset = 0;
		std::size_t level = 0;
		tree target = NULL_TREE;
	};

	std::string symbol;
	std::list<component> components;

	static void consider_static_var(tree var);
	static void reset();
	static const std::list<dpin> &inspect();
};
