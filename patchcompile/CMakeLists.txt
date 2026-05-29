add_executable(patchcompile patchcompile.cpp accumulation.cpp emit.cpp)
set_target_properties(patchcompile PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS NO)
