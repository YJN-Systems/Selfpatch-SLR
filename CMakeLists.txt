cmake_minimum_required(VERSION 3.16)

project(SelfPatchSLR LANGUAGES C CXX)

option(SANEMAKER "Enable sanemaker instrumentation" ON)

set(SANEMAKER_BUILD_QEMU_PLUGIN OFF CACHE BOOL "" FORCE)
set(SANEMAKER_BUILD_SANETRAPS ${SANEMAKER} CACHE BOOL "" FORCE)

add_subdirectory(sanemaker)

add_subdirectory(pinpoint)
add_subdirectory(selfpatch)
add_subdirectory(subject)

