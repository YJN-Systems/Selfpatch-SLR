add_library(randomizer STATIC
	src/spslr_randomizer.c
)

target_include_directories(randomizer PUBLIC
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)

target_compile_options(randomizer PRIVATE
	-Wall
	-Wextra
	-Werror
	-Wmissing-prototypes
	-Wstrict-prototypes
	-Wold-style-definition
	-Wmissing-declarations
)

set_target_properties(randomizer PROPERTIES
	C_STANDARD 11
	C_STANDARD_REQUIRED YES
	C_EXTENSIONS YES
)

add_library(selfpatch STATIC src/spslr.c src/spslr_env.c)

target_link_libraries(selfpatch PUBLIC randomizer)

target_include_directories(selfpatch PUBLIC
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
	$<INSTALL_INTERFACE:include>
)

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

