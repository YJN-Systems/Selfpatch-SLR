set(SUBJECT_SRC
	main.c
)

add_executable(subject ${SUBJECT_SRC})
target_include_directories(subject PRIVATE .)
target_link_libraries(subject PRIVATE selfpatch dl)
add_dependencies(subject pinpoint selfpatch)

target_compile_options(subject PRIVATE
	-O1
	-fplugin=$<TARGET_FILE:pinpoint>
	-fplugin-arg-pinpoint-verbose
)



set(MODULE_SRC
	module.c
)

add_library(spslr_module SHARED ${MODULE_SRC})
target_include_directories(spslr_module PRIVATE .)
add_dependencies(spslr_module pinpoint)

target_compile_options(spslr_module PRIVATE
	-O1
	-fPIC
	-fplugin=$<TARGET_FILE:pinpoint>
	-fplugin-arg-pinpoint-verbose
)

set_target_properties(spslr_module PROPERTIES
	OUTPUT_NAME "spslr_module"
	PREFIX "lib"
)

target_link_options(spslr_module PRIVATE
	"LINKER:-T,${CMAKE_CURRENT_SOURCE_DIR}/module.ld"
)
