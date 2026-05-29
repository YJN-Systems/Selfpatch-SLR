set(SUBJECT_SRC
	main.c
)

set(SUBJECT_SPSLR_METADIR "${CMAKE_CURRENT_BINARY_DIR}/spslr")
set(SUBJECT_SPSLR_SRCROOT "${CMAKE_CURRENT_SOURCE_DIR}")

file(MAKE_DIRECTORY "${SUBJECT_SPSLR_METADIR}")

add_library(subject_objs OBJECT ${SUBJECT_SRC})
target_include_directories(subject_objs PRIVATE .)
target_link_libraries(subject_objs PRIVATE selfpatch dl)
add_dependencies(subject_objs pinpoint)

target_compile_options(subject_objs PRIVATE
	-O1
	-fplugin=$<TARGET_FILE:pinpoint>
	-fplugin-arg-pinpoint-verbose
)

set(SUBJECT_SPSLR_FILES "")
foreach(src IN LISTS SUBJECT_SRC)
	get_filename_component(abs_src "${src}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
	file(RELATIVE_PATH rel_src "${SUBJECT_SPSLR_SRCROOT}" "${abs_src}")
	file(TO_CMAKE_PATH "${rel_src}" rel_src)

	set(spslr_file "${SUBJECT_SPSLR_METADIR}/${rel_src}.spslr")
	list(APPEND SUBJECT_SPSLR_FILES "${spslr_file}")

	set_property(SOURCE "${src}" APPEND PROPERTY COMPILE_OPTIONS "-fplugin-arg-pinpoint-out=${spslr_file}")
	set_property(SOURCE "${src}" APPEND PROPERTY OBJECT_OUTPUTS "${spslr_file}")
endforeach()

set(SUBJECT_SPSLR_ASM "${CMAKE_CURRENT_BINARY_DIR}/subject_spslr_section.S")
set(SUBJECT_SPSLR_OBJ "${CMAKE_CURRENT_BINARY_DIR}/subject_spslr_section.o")

set(SUBJECT_TARGET_MAP "${CMAKE_CURRENT_BINARY_DIR}/subject.spslr_targets")

set(SUBJECT_SPSLR_STAMP "${CMAKE_CURRENT_BINARY_DIR}/subject_spslr_metadata.stamp")

add_custom_command(
	OUTPUT "${SUBJECT_SPSLR_STAMP}"
	BYPRODUCTS "${SUBJECT_SPSLR_ASM}" "${SUBJECT_TARGET_MAP}"
	COMMAND $<TARGET_FILE:patchcompile>
		--out=${SUBJECT_SPSLR_ASM}
		--dump-targets=${SUBJECT_TARGET_MAP}
		--verbose
		${SUBJECT_SPSLR_FILES}
	COMMAND ${CMAKE_COMMAND} -E touch "${SUBJECT_SPSLR_STAMP}"
	DEPENDS
		patchcompile
		subject_objs
		${SUBJECT_SPSLR_FILES}
	VERBATIM
)

add_custom_target(subject_spslr_metadata
	DEPENDS "${SUBJECT_SPSLR_STAMP}"
)

add_custom_command(
	OUTPUT "${SUBJECT_SPSLR_OBJ}"
	COMMAND ${CMAKE_C_COMPILER}
		-c "${SUBJECT_SPSLR_ASM}"
		-o "${SUBJECT_SPSLR_OBJ}"
	DEPENDS
		subject_spslr_metadata
		"${SUBJECT_SPSLR_ASM}"
	VERBATIM
)

set_source_files_properties("${SUBJECT_SPSLR_OBJ}" PROPERTIES
	GENERATED TRUE
	EXTERNAL_OBJECT TRUE
)

add_executable(subject
	$<TARGET_OBJECTS:subject_objs>
	"${SUBJECT_SPSLR_OBJ}"
)

target_include_directories(subject PRIVATE .)
target_link_libraries(subject PRIVATE selfpatch)
add_dependencies(subject selfpatch)



set(MODULE_SRC
	module.c
)

set(MODULE_SPSLR_METADIR "${CMAKE_CURRENT_BINARY_DIR}/spslr_module")
set(MODULE_SPSLR_SRCROOT "${CMAKE_CURRENT_SOURCE_DIR}")

file(MAKE_DIRECTORY "${MODULE_SPSLR_METADIR}")

add_library(spslr_module_objs OBJECT ${MODULE_SRC})
target_include_directories(spslr_module_objs PRIVATE .)
add_dependencies(spslr_module_objs pinpoint)

target_compile_options(spslr_module_objs PRIVATE
	-O1
	-fPIC
	-fplugin=$<TARGET_FILE:pinpoint>
	-fplugin-arg-pinpoint-verbose
)

set(MODULE_SPSLR_FILES "")
foreach(src IN LISTS MODULE_SRC)
	get_filename_component(abs_src "${src}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
	file(RELATIVE_PATH rel_src "${MODULE_SPSLR_SRCROOT}" "${abs_src}")
	file(TO_CMAKE_PATH "${rel_src}" rel_src)

	set(spslr_file "${MODULE_SPSLR_METADIR}/${rel_src}.spslr")
	list(APPEND MODULE_SPSLR_FILES "${spslr_file}")

	set_property(SOURCE "${src}" APPEND PROPERTY COMPILE_OPTIONS "-fplugin-arg-pinpoint-out=${spslr_file}")
	set_property(SOURCE "${src}" APPEND PROPERTY OBJECT_OUTPUTS "${spslr_file}")
endforeach()

set(MODULE_SPSLR_ASM "${CMAKE_CURRENT_BINARY_DIR}/spslr_module_section.S")
set(MODULE_SPSLR_OBJ "${CMAKE_CURRENT_BINARY_DIR}/spslr_module_section.o")

add_custom_command(
	OUTPUT "${MODULE_SPSLR_ASM}"
	COMMAND $<TARGET_FILE:patchcompile>
		--out=${MODULE_SPSLR_ASM}
		--load-targets=${SUBJECT_TARGET_MAP}
		--no-new-targets
		--module
		--verbose
		${MODULE_SPSLR_FILES}
	DEPENDS
		patchcompile
		subject_spslr_metadata
		spslr_module_objs
		${MODULE_SPSLR_FILES}
	VERBATIM
)

add_custom_command(
	OUTPUT "${MODULE_SPSLR_OBJ}"
	COMMAND ${CMAKE_C_COMPILER}
		-fPIC
		-c "${MODULE_SPSLR_ASM}"
		-o "${MODULE_SPSLR_OBJ}"
	DEPENDS "${MODULE_SPSLR_ASM}"
	VERBATIM
)

set_source_files_properties("${MODULE_SPSLR_OBJ}" PROPERTIES
	GENERATED TRUE
	EXTERNAL_OBJECT TRUE
)

add_library(spslr_module SHARED
	$<TARGET_OBJECTS:spslr_module_objs>
	"${MODULE_SPSLR_OBJ}"
)

target_include_directories(spslr_module PRIVATE .)
set_target_properties(spslr_module PROPERTIES
	OUTPUT_NAME "spslr_module"
	PREFIX "lib"
)
