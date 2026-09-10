# cmake/toolchains/macos-universal.cmake
# ---------------------------------------------------------------------------
# Toolchain file for macOS universal (fat) binaries — x86_64 + arm64.
#
# Produces a single binary that runs natively on both Intel and Apple Silicon.
#
# Usage:
#   cmake -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/macos-universal.cmake ..
# ---------------------------------------------------------------------------

set(CMAKE_SYSTEM_NAME    Darwin)
# CMAKE_SYSTEM_PROCESSOR is intentionally left unset for universal builds.

# Build a universal binary
set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "macOS universal build")

# Minimum deployment target
set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum macOS version")

# ABI flags
set(CMAKE_C_FLAGS_INIT   "-fPIC")
set(CMAKE_CXX_FLAGS_INIT "-fPIC")

# NOTE: SIMD dispatch is compile-time per-slice in a fat binary.
# Platform.cmake will see "x86_64" or "arm64" depending on which
# slice is being compiled, and will set SIMD flags accordingly.
# You do NOT need -march flags here — each arch slice uses its own defaults.
