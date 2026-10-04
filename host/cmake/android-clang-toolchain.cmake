# Build the host for Android/arm64 with the system clang and the NDK's sysroot.
#
# Why not the NDK's own toolchain file: NDK r29 ships clang 21, which does not compile the fmt 10
# bundled with Dynarmic ("call to consteval function ... is not a constant expression"), and fmt's
# documented escape hatch does not apply to that version. Rather than pin an older NDK or patch a
# vendored library, this toolchain keeps the NDK's sysroot and headers -- so the result is a real
# Android binary, bionic and all -- and uses a compiler that builds the tree.
#
# Usage:
#   cmake -S host -B <build> -G Ninja \
#     -DCMAKE_TOOLCHAIN_FILE=host/cmake/android-clang-toolchain.cmake \
#     -DANDROID_NDK_ROOT=/path/to/ndk -DANDROID_API=24

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# Read from the environment: this file is re-included for CMake's own compiler-probe
# projects, which do not inherit command-line cache variables, so a cache-only lookup makes the
# probe fail even when the top-level configure was given the path.
set(_ndk "$ENV{ANDROID_NDK_ROOT}")
if(NOT _ndk AND DEFINED ANDROID_NDK_ROOT)
  set(_ndk "${ANDROID_NDK_ROOT}")
endif()
if(NOT _ndk)
  message(FATAL_ERROR "export ANDROID_NDK_ROOT=/path/to/ndk before configuring")
endif()
set(ANDROID_NDK_ROOT "${_ndk}" CACHE PATH "Android NDK" FORCE)
if(NOT ANDROID_API)
  set(ANDROID_API 24)
endif()

set(ANDROID_SYSROOT "${ANDROID_NDK_ROOT}/toolchains/llvm/prebuilt/linux-x86_64/sysroot")
set(ANDROID_TRIPLE "aarch64-linux-android${ANDROID_API}")
# The system clang drives the *system* ld, which only knows x86 emulations and fails with
# "unrecognised emulation mode: aarch64linux". The NDK ships ld.lld, which does know aarch64, so
# the toolchain points clang at it. Its directory must be on PATH (see scripts/build-android.sh).
set(ANDROID_LINK_FLAGS "--target=${ANDROID_TRIPLE} --sysroot=${ANDROID_SYSROOT} -fuse-ld=lld")
set(ANDROID_FLAGS "--target=${ANDROID_TRIPLE} --sysroot=${ANDROID_SYSROOT}")

set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_C_FLAGS_INIT "${ANDROID_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${ANDROID_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${ANDROID_LINK_FLAGS}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${ANDROID_LINK_FLAGS}")

# The sysroot is the only place to look for headers and libraries; host tools are still host tools.
set(CMAKE_FIND_ROOT_PATH "${ANDROID_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
