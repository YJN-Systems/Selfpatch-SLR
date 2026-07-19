#pragma once

#include <array>
#include <cstddef>

#include <safe-tree.h>

std::array<std::byte, 16> compute_layout_hash(tree target_type);
