# cmake/toolchains/linux-aarch64.cmake
# ---------------------------------------------------------------------------
# Cross-compilation toolchain for Linux aarch64 (ARM64).
#
# Requires an aarch64 cross-compiler installed:
#   sudo apt install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu
#
# Usage:
#   cmake -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/linux-aarch64.cmake ..
# ---------------------------------------------------------------------------

set(CMAKE_SYSTEM_NAME    Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# Cross-compiler
set(CMAKE_C_COMPILER   aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)

# Sysroot — uncomment and set if cross-compiling with a custom sysroot
# set(CMAKE_SYSROOT /path/to/aarch64-sysroot)

# Search paths
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# ABI flags
set(CMAKE_C_FLAGS_INIT   "-fPIC")
set(CMAKE_CXX_FLAGS_INIT "-fPIC")

# NEON is mandatory on ARMv8-A — no extra flags needed.
# Optionally target a specific micro-architecture:
# set(CMAKE_C_FLAGS_INIT   "${CMAKE_C_FLAGS_INIT}   -mcpu=neoverse-v2")
# set(CMAKE_CXX_FLAGS_INIT "${CMAKE_CXX_FLAGS_INIT} -mcpu=neoverse-v2")
