#include <stage0.h>
#include <pinpoint_error.h>

void on_finish_type(void *plugin_data, void *user_data)
{
	tree t = (tree)plugin_data;

	TargetType *type = TargetType::find_mutable(t);
	if (!type)
		return;

	/*
	 * Flexible array members or other special cases that can not (yet) be handled
	 * for SPSLR targets may cause fetch_fields() to fail. Since this code is only
	 * run for structs marked as SPSLR targets, compilation should fail.
	 */
	if (!type->fetch_fields())
		pinpoint_fatal(
			"on_finish_type failed to fetch fields of target \"%s\" or the field composition is not (yet) supported",
			type->name().c_str());
}
