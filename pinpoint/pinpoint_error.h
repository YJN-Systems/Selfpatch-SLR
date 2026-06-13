#pragma once

#include <cstdio>
#include <safe-diagnostic.h>

extern bool pinpoint_verbose_enabled;

#define plugin_print_early_error(fmt, ...)                                 \
	do {                                                               \
		std::fprintf(stderr, "[spslr::pinpoint] error: " fmt "\n", \
			     ##__VA_ARGS__);                               \
	} while (0)

#define pinpoint_debug_loc(loc, fmt, ...)                       \
	do {                                                    \
		if (pinpoint_verbose_enabled)                   \
			inform((loc), "[spslr::pinpoint] " fmt, \
			       ##__VA_ARGS__);                  \
	} while (0)

#define pinpoint_debug(fmt, ...) \
	pinpoint_debug_loc(UNKNOWN_LOCATION, fmt, ##__VA_ARGS__)

#define pinpoint_fatal_loc(loc, fmt, ...) \
	fatal_error((loc), "[spslr::pinpoint] " fmt, ##__VA_ARGS__)

#define pinpoint_fatal(fmt, ...) \
	pinpoint_fatal_loc(UNKNOWN_LOCATION, fmt, ##__VA_ARGS__)
