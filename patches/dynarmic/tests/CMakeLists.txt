cmake_minimum_required(VERSION 3.16)
project(dh2_dynarmic_t32_tests CXX)

# Builds the patched (or pristine) Dynarmic from source and links the T32 ARMv8 test against
# it. Configure with:
#   -DDYNARMIC_SRC=<dynarmic checkout>
#   -DT32_GENERATED_DIR=<dir containing t32_armv8_seq.inc>
# See build_and_run.sh for the complete flow.

if(NOT DEFINED DYNARMIC_SRC)
    message(FATAL_ERROR "Pass -DDYNARMIC_SRC=<path to a Dynarmic checkout>")
endif()
if(NOT DEFINED T32_GENERATED_DIR)
    set(T32_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
endif()
file(MAKE_DIRECTORY "${T32_GENERATED_DIR}")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(DYNARMIC_USE_BUNDLED_EXTERNALS ON CACHE BOOL "" FORCE)
set(DYNARMIC_TESTS OFF CACHE BOOL "" FORCE)
set(DYNARMIC_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
set(DYNARMIC_USE_PRECOMPILED_HEADERS OFF CACHE BOOL "" FORCE)

add_subdirectory("${DYNARMIC_SRC}" "${CMAKE_BINARY_DIR}/dynarmic" EXCLUDE_FROM_ALL)

add_executable(t32_armv8_test t32_armv8_test.cpp)
target_include_directories(t32_armv8_test PRIVATE "${T32_GENERATED_DIR}")
target_link_libraries(t32_armv8_test PRIVATE dynarmic)
