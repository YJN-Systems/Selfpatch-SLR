#include <passes.h>
#include <ipin_registry.h>
#include <dpin_registry.h>
#include <target_registry.h>

void on_start_unit(void *plugin_data, void *user_data)
{
	target::reset();
	dpin::reset();
	ipin::reset();
}
