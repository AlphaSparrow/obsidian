# cmake/toolchains/linux-x86_64.cmake
# ---------------------------------------------------------------------------
# Toolchain file for native Linux x86_64 builds.
#
# Usage:
#   cmake -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/linux-x86_64.cmake ..
# ---------------------------------------------------------------------------

set(CMAKE_SYSTEM_NAME    Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# Prefer system default compilers.  Override by setting CC / CXX env vars
# or by passing -DCMAKE_C_COMPILER= -DCMAKE_CXX_COMPILER= on the command line.
# find_program(CMAKE_C_COMPILER   NAMES gcc-13 gcc   clang)
# find_program(CMAKE_CXX_COMPILER NAMES g++-13 g++   clang++)

# ABI flags
set(CMAKE_C_FLAGS_INIT           "-fPIC")
set(CMAKE_CXX_FLAGS_INIT         "-fPIC")
set(CMAKE_EXE_LINKER_FLAGS_INIT  "-Wl,--as-needed")

# SIMD: let Platform.cmake auto-detect, or force a baseline here:
# set(CMAKE_C_FLAGS_INIT   "${CMAKE_C_FLAGS_INIT}   -march=x86-64-v3")  # AVX2 baseline
# set(CMAKE_CXX_FLAGS_INIT "${CMAKE_CXX_FLAGS_INIT} -march=x86-64-v3")
