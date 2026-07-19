#include <passes.h>
#include <target_registry.h>

void on_finish_type(void *plugin_data, void *user_data)
{
	tree t = target::main_variant((tree)plugin_data);

	if (!target::is_target(t))
		return;

	target::validate(t);
}
