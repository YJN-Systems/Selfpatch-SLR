#include "gc_preserve.h"

void pinpoint_gc_preserve(void *, void *)
{
	for (pinpoint_gc_preserve_fn *p = PINPOINT_GC_SECTION_START;
	     p != PINPOINT_GC_SECTION_END; ++p) {
		if (*p)
			(*p)();
	}
}
