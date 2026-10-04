# DH2Work Track A -- our own compatibility host.
#
# This tree is the replacement for the ZettaBridge layer: it loads 32-bit ARM guest ELFs into
# its own guest address space and executes them under our own Dynarmic build. It contains no
# ZettaBridge code and does not link libzbridge.so.
#
# The Dynarmic source tree is supplied out of band (scripts/fetch-dynarmic.sh) because it is a
# separate 0BSD project; point DH2_DYNARMIC_DIR at it.
cmake_minimum_required(VERSION 3.20)
project(dh2host LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

if(NOT DH2_DYNARMIC_DIR)
  set(DH2_DYNARMIC_DIR "$ENV{DH2_DYNARMIC_DIR}" CACHE PATH "Dynarmic source tree ({git clone, pin in third_party/dynarmic.pin)")
endif()
if(NOT DH2_DYNARMIC_DIR)
  message(FATAL_ERROR "DH2_DYNARMIC_DIR is not set. Run scripts/fetch-dynarmic.sh first, then pass -DDH2_DYNARMIC_DIR=<path>.")
endif()
if(NOT EXISTS "${DH2_DYNARMIC_DIR}/CMakeLists.txt")
  message(FATAL_ERROR "DH2_DYNARMIC_DIR=${DH2_DYNARMIC_DIR} does not look like a Dynarmic checkout")
endif()

# The same options the project's proven host build used. Bundled externals so the build is
# offline and reproducible; tests and warnings-as-errors stay off here (the patch tree's own
# test build is separate).
set(DYNARMIC_USE_BUNDLED_EXTERNALS ON CACHE BOOL "" FORCE)
set(DYNARMIC_TESTS OFF CACHE BOOL "" FORCE)
set(DYNARMIC_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
set(DYNARMIC_USE_PRECOMPILED_HEADERS OFF CACHE BOOL "" FORCE)
add_subdirectory("${DH2_DYNARMIC_DIR}" "${CMAKE_BINARY_DIR}/dynarmic" EXCLUDE_FROM_ALL)

add_library(dh2core STATIC
  src/guest_memory.cpp
  src/cp15.cpp
  src/loader.cpp
  src/initial_stack.cpp
  src/cpu.cpp
  src/gl.cpp
  src/jni.cpp
  src/loader_if.cpp
  src/linker.cpp
  src/syscalls.cpp
)
target_include_directories(dh2core PUBLIC include)
target_compile_options(dh2core PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_link_libraries(dh2core PUBLIC dynarmic)

add_executable(dh2run app/dh2run.cpp)
target_link_libraries(dh2run PRIVATE dh2core)
target_compile_options(dh2run PRIVATE -Wall -Wextra)

add_executable(dh2selftest app/dh2selftest.cpp)
target_link_libraries(dh2selftest PRIVATE dh2core)
target_compile_options(dh2selftest PRIVATE -Wall -Wextra)

add_executable(dh2link app/dh2link.cpp)
target_link_libraries(dh2link PRIVATE dh2core)
target_compile_options(dh2link PRIVATE -Wall -Wextra)

add_executable(dh2boot app/dh2boot.cpp)
target_link_libraries(dh2boot PRIVATE dh2core)
target_compile_options(dh2boot PRIVATE -Wall -Wextra)

enable_testing()
add_test(NAME dh2selftest COMMAND dh2selftest)
