add_library(selfpatch STATIC src/spslr.c src/spslr_env.c src/spslr_randomizer.c)

target_link_libraries(selfpatch PRIVATE sanemaker::api)

if(SANEMAKER)
	target_link_libraries(selfpatch PRIVATE sanemaker::traps)
	target_compile_definitions(selfpatch PRIVATE SANEMAKER)
endif()

target_include_directories(selfpatch PUBLIC
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
	$<INSTALL_INTERFACE:include>
)

set_property(TARGET selfpatch PROPERTY POSITION_INDEPENDENT_CODE TRUE)

target_compile_options(selfpatch PRIVATE
	-Wall
	-Wextra
	-Werror
	-Wmissing-prototypes
	-Wstrict-prototypes
	-Wold-style-definition
	-Wmissing-declarations
)

set_target_properties(selfpatch PROPERTIES
	C_STANDARD 11
	C_STANDARD_REQUIRED YES
	C_EXTENSIONS YES
)

add_subdirectory(test)

