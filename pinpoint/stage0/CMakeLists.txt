target_sources(pinpoint PRIVATE on_register_attributes.cpp on_finish_type.cpp on_preserve_component_ref.cpp
	on_finish_decl.cpp on_start_unit.cpp target.cpp separator.cpp separate_offset_pass.cpp)
target_include_directories(pinpoint PRIVATE .)
