#pragma once

#include <cstdio>

extern bool patchcompile_verbose_enabled;

#define patchcompile_error(fmt, ...)                                          \
	do {                                                                  \
		std::fprintf(stderr, "[spslr::patchcompile] error: " fmt "\n", \
			     ##__VA_ARGS__);                                  \
	} while (0)

#define patchcompile_file_error(f, l, fmt, ...)                               \
	do {                                                                  \
		std::fprintf(                                                 \
			stderr,                                               \
			"[spslr::patchcompile] error in %s at line %llu: " fmt \
			"\n",                                                 \
			f, l, ##__VA_ARGS__);                                 \
	} while (0)

#define patchcompile_warn(fmt, ...)                                     \
	do {                                                            \
		std::fprintf(stderr,                                    \
			     "[spslr::patchcompile] warning: " fmt "\n", \
			     ##__VA_ARGS__);                            \
	} while (0)

#define patchcompile_info(fmt, ...)                                          \
	do {                                                                 \
		std::fprintf(stderr, "[spslr::patchcompile] info: " fmt "\n", \
			     ##__VA_ARGS__);                                 \
	} while (0)

#define patchcompile_debug(fmt, ...)                                          \
	do {                                                                  \
		if (patchcompile_verbose_enabled)                             \
			std::fprintf(stderr,                                  \
				     "[spslr::patchcompile] debug: " fmt "\n", \
				     ##__VA_ARGS__);                          \
	} while (0)
