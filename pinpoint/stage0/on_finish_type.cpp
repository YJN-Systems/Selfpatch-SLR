#include <stage0.h>
#include <pinpoint_error.h>

void on_finish_type(void *plugin_data, void *user_data)
{
	tree t = (tree)plugin_data;

	TargetType *type = TargetType::find_mutable(t);
	if (!type)
		return;

	if (!type->fetch_fields())
		pinpoint_fatal(
			"on_finish_type failed to fetch fields of target \"%s\"",
			type->name().c_str());
}
