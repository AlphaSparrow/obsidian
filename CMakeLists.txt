# ===========================================================================
#  Obsidian — Quantitative Finance Platform
#  Root CMakeLists.txt
# ===========================================================================
#
#  Quick start:
#    cmake --preset dev
#    cmake --build --preset dev
#
#  Cross-compile:
#    cmake -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/linux-aarch64.cmake \
#          -B build-aarch64 .
# ===========================================================================

cmake_minimum_required(VERSION 3.25)

project(obsidian
    VERSION     0.1.0
    LANGUAGES   C CXX
    DESCRIPTION "High-performance quantitative finance platform"
    HOMEPAGE_URL "https://github.com/obsidian"
)

# ── C++ standard ─────────────────────────────────────────────────────────────

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)       # -std=c++20, not -std=gnu++20

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ── Build type defaults ──────────────────────────────────────────────────────
# Single-config generators (Make, Ninja) need a default.

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "[obsidian] Defaulting CMAKE_BUILD_TYPE to 'Debug'")
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()

# ── Feature toggles ─────────────────────────────────────────────────────────
# These control which optional subsystems are compiled.

option(OBSIDIAN_ENABLE_CUDA   "Build CUDA GPU compute backend"       OFF)
option(OBSIDIAN_ENABLE_MPI    "Build MPI distributed compute layer"  OFF)
option(OBSIDIAN_ENABLE_GRPC   "Build gRPC service layer"             OFF)
option(OBSIDIAN_ENABLE_RUST   "Build Rust FFI components (requires cargo)" OFF)
option(OBSIDIAN_ENABLE_TESTS  "Build test suite"                     ON)
option(OBSIDIAN_ENABLE_LTO    "Enable link-time optimisation"        OFF)

# ── Export compile_commands.json ─────────────────────────────────────────────
# Required by clangd, clang-tidy, and most LSP-based editors.

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Include CMake modules ───────────────────────────────────────────────────

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(Platform)            # OS / arch / ABI / SIMD detection
include(CompilerWarnings)    # Per-compiler warning policy
include(Dependencies)        # FetchContent + find_package declarations

# ── Threading ────────────────────────────────────────────────────────────────

find_package(Threads REQUIRED)

# ── Link-Time Optimisation ──────────────────────────────────────────────────

if(OBSIDIAN_ENABLE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT _lto_supported OUTPUT _lto_error)
    if(_lto_supported)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
        message(STATUS "[obsidian] LTO enabled")
    else()
        message(WARNING "[obsidian] LTO requested but not supported: ${_lto_error}")
    endif()
endif()

# ── Sanitizers (Debug builds) ───────────────────────────────────────────────

if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND NOT MSVC)
    # Address + Undefined Behavior sanitizers for development builds
    option(OBSIDIAN_ENABLE_SANITIZERS "Enable ASan + UBSan in Debug" ON)
    if(OBSIDIAN_ENABLE_SANITIZERS)
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address,undefined)
        message(STATUS "[obsidian] Sanitizers enabled (ASan + UBSan)")
    endif()
endif()

# ── Subdirectories ──────────────────────────────────────────────────────────

# Core kernel — always built
add_subdirectory(kernel)

# Compute layer — conditionally includes CUDA, SIMD, MPI, gRPC
add_subdirectory(compute)

# Storage layer — future: Arrow, Parquet, DuckDB, ClickHouse
# add_subdirectory(storage)

# Engines — future: options pricing, risk, execution, alpha models
# add_subdirectory(engines)

# Terminal / UI — future: renderer, charts, widgets
# add_subdirectory(terminal)

# Tests
# if(OBSIDIAN_ENABLE_TESTS)
#     enable_testing()
#     add_subdirectory(tests)
# endif()

# ── Configuration summary ──────────────────────────────────────────────────

message(STATUS "")
message(STATUS "╔══════════════════════════════════════════════════════════╗")
message(STATUS "║  Obsidian ${PROJECT_VERSION} — Configuration Summary")
message(STATUS "╠══════════════════════════════════════════════════════════╣")
message(STATUS "║  Platform       : ${OBSIDIAN_PLATFORM} / ${OBSIDIAN_ARCH_CANONICAL}")
message(STATUS "║  C++ Standard   : ${CMAKE_CXX_STANDARD}")
message(STATUS "║  Build Type     : ${CMAKE_BUILD_TYPE}")
message(STATUS "║  Compiler       : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "║  SIMD Level     : ${OBSIDIAN_SIMD_LEVEL}")
message(STATUS "║  CUDA           : ${OBSIDIAN_ENABLE_CUDA}")
message(STATUS "║  MPI            : ${OBSIDIAN_ENABLE_MPI}")
message(STATUS "║  gRPC           : ${OBSIDIAN_ENABLE_GRPC}")
message(STATUS "║  Rust FFI       : ${OBSIDIAN_ENABLE_RUST}")
message(STATUS "║  LTO            : ${OBSIDIAN_ENABLE_LTO}")
message(STATUS "╚══════════════════════════════════════════════════════════╝")
message(STATUS "")
