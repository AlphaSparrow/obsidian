# cmake/toolchains/windows-x64-msvc.cmake
# ---------------------------------------------------------------------------
# Toolchain file for Windows x64 with MSVC (Visual Studio 2022+).
#
# Usually not needed when building natively on Windows with VS, but useful
# for CI and preset-driven builds to lock down the target architecture.
#
# Usage:
#   cmake -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/windows-x64-msvc.cmake ..
#   (or use a CMakePreset that references this file)
# ---------------------------------------------------------------------------

set(CMAKE_SYSTEM_NAME    Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

# Force the x64 host and target architecture.
# This avoids the common mistake of building x86 on a 64-bit host.
set(CMAKE_GENERATOR_PLATFORM x64)

# CRT linkage — static (/MT) by default.  Override via CMAKE_MSVC_RUNTIME_LIBRARY.
if(NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# MSVC-specific flags
set(CMAKE_C_FLAGS_INIT   "/utf-8 /DWIN32 /D_WINDOWS")
set(CMAKE_CXX_FLAGS_INIT "/utf-8 /DWIN32 /D_WINDOWS /EHsc /Zc:__cplusplus /Zc:preprocessor")

# Linker
set(CMAKE_EXE_LINKER_FLAGS_INIT    "/INCREMENTAL:NO")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "/INCREMENTAL:NO")
