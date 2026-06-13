add_library(pinpoint SHARED pinpoint.cpp gc_preserve.cpp)

set_target_properties(pinpoint PROPERTIES PREFIX "")
target_compile_definitions(pinpoint PRIVATE _GNU_SOURCE)
target_compile_options(pinpoint PRIVATE -fno-rtti -fno-exceptions)

execute_process(COMMAND ${CMAKE_C_COMPILER} -print-file-name=plugin OUTPUT_VARIABLE GCC_PLUGIN_PATH OUTPUT_STRIP_TRAILING_WHITESPACE)
message(STATUS "GCC plugin path: ${GCC_PLUGIN_PATH}")
target_include_directories(pinpoint PRIVATE ${GCC_PLUGIN_PATH}/include ${CMAKE_CURRENT_SOURCE_DIR}/safegcc ${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(stage0)
add_subdirectory(stage1)
add_subdirectory(stage2)
add_subdirectory(final)
