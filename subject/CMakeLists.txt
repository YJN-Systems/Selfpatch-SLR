set(PINPOINT_COMMON_OPTIONS
	-O1
	-fplugin=$<TARGET_FILE:pinpoint>
	-fplugin-arg-pinpoint-verbose
)

set(SUBJECT_SRC
	main.c
)

add_executable(subject ${SUBJECT_SRC})
target_include_directories(subject PRIVATE .)
target_link_libraries(subject PRIVATE selfpatch dl sanemaker::api)
add_dependencies(subject pinpoint selfpatch)

set_property(TARGET subject PROPERTY POSITION_INDEPENDENT_CODE TRUE)
target_link_options(subject PRIVATE -pie)

target_compile_options(subject PRIVATE ${PINPOINT_COMMON_OPTIONS})

if(SANEMAKER)
	target_compile_definitions(subject PRIVATE SANEMAKER)
endif()



set(MODULE_SRC
	module.c
)

add_library(spslr_module SHARED ${MODULE_SRC})
target_include_directories(spslr_module PRIVATE .)
target_link_libraries(spslr_module PRIVATE sanemaker::api)
add_dependencies(spslr_module pinpoint)

target_compile_options(spslr_module PRIVATE -fPIC ${PINPOINT_COMMON_OPTIONS})

set_target_properties(spslr_module PROPERTIES
	OUTPUT_NAME "spslr_module"
	PREFIX "lib"
)

target_link_options(spslr_module PRIVATE
	"LINKER:-T,${CMAKE_CURRENT_SOURCE_DIR}/module.ld"
)

if(SANEMAKER)
	target_compile_definitions(spslr_module PRIVATE SANEMAKER)
endif()

