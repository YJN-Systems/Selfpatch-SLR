#include <stage0.h>
#include <stage1.h>
#include <stage2.h>

#include <pinpoint_error.h>

void on_start_unit(void *plugin_data, void *user_data)
{
	TargetType::reset();
	DataPin::reset();
	s2_pins_reset();
}
