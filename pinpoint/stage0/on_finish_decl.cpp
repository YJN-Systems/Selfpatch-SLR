#include <passes.h>
#include <dpin_registry.h>

void on_finish_decl(void *plugin_data, void *user_data)
{
	tree decl = (tree)plugin_data;
	dpin::consider_static_var(decl);
}
