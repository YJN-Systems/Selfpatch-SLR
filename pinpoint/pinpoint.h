#pragma once
#include <cstdio>
#include <safe-diagnostic.h>
#include <safe-ggc.h>

#define SPSLR_ATTRIBUTE "spslr"
#define SPSLR_FIELD_FIXED_ATTRIBUTE "spslr_field_fixed"
#define SPSLR_TARGET_HASH_BUILTIN "__spslr_target_hash"

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

using pinpoint_gc_preserve_fn = void (*)();

#define PINPOINT_GC_CONCAT2(a, b) a##b
#define PINPOINT_GC_CONCAT(a, b) PINPOINT_GC_CONCAT2(a, b)

#define PINPOINT_GC_USED __attribute__((used))

#define PINPOINT_GC_SECTION "pinpoint_gc_mark"
#define PINPOINT_GC_SECTION_START __start_pinpoint_gc_mark
#define PINPOINT_GC_SECTION_END __stop_pinpoint_gc_mark

#define PINPOINT_GC_SECTION_ATTRIB __attribute__((section(PINPOINT_GC_SECTION)))

#define PINPOINT_GC_PRESERVE_CALLBACK() \
	PINPOINT_GC_PRESERVE_CALLBACK_IMPL(__COUNTER__)

#define PINPOINT_GC_PRESERVE_CALLBACK_IMPL(id)                          \
	static void PINPOINT_GC_CONCAT(pinpoint_gc_preserve_cb_, id)(); \
	static pinpoint_gc_preserve_fn PINPOINT_GC_CONCAT(              \
		pinpoint_gc_preserve_reg_, id)                          \
	PINPOINT_GC_USED PINPOINT_GC_SECTION_ATTRIB =                   \
		PINPOINT_GC_CONCAT(pinpoint_gc_preserve_cb_, id);       \
	static void PINPOINT_GC_CONCAT(pinpoint_gc_preserve_cb_, id)()

#define PINPOINT_GC_MARK_TREE(t)      \
	do {                          \
		if ((t) != NULL_TREE) \
			ggc_mark(t);  \
	} while (0)

#define PINPOINT_GC_MARK(p)          \
	do {                         \
		if (p)               \
			ggc_mark(p); \
	} while (0)

extern "C" {
extern pinpoint_gc_preserve_fn PINPOINT_GC_SECTION_START[];
extern pinpoint_gc_preserve_fn PINPOINT_GC_SECTION_END[];
}

void pinpoint_gc_preserve(void *, void *);
