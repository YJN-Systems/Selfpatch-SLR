add_executable(spslr_randomizer_test
	test.cpp
	${CMAKE_CURRENT_LIST_DIR}/../src/spslr_randomizer.c
)

target_link_libraries(spslr_randomizer_test PRIVATE sanemaker::api)

target_include_directories(spslr_randomizer_test PRIVATE
	${CMAKE_CURRENT_LIST_DIR}/../src
)

target_compile_options(spslr_randomizer_test PRIVATE
	$<$<COMPILE_LANGUAGE:C>:-Wall;-Wextra;-Werror>
	$<$<COMPILE_LANGUAGE:CXX>:-Wall;-Wextra;-Werror>
)

set_target_properties(spslr_randomizer_test PROPERTIES
	C_STANDARD 11
	C_STANDARD_REQUIRED YES
	C_EXTENSIONS YES
	CXX_STANDARD 17
	CXX_STANDARD_REQUIRED YES
	CXX_EXTENSIONS NO
)

