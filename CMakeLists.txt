# Obsidian root CMake configuration
cmake_minimum_required(VERSION 3.25)

project(obsidian
    VERSION     0.1.0
    LANGUAGES   C CXX
    DESCRIPTION "High-performance quantitative finance platform"
    HOMEPAGE_URL "https://github.com/obsidian"
)

# C++ and C standards
if(NOT DEFINED CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 20)
endif()
if(NOT DEFINED CMAKE_CXX_STANDARD_REQUIRED)
    set(CMAKE_CXX_STANDARD_REQUIRED OFF)
endif()
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT DEFINED CMAKE_C_STANDARD)
    set(CMAKE_C_STANDARD 11)
endif()
set(CMAKE_C_STANDARD_REQUIRED ON)

# Build type defaults
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "[obsidian] Defaulting CMAKE_BUILD_TYPE to 'RelWithDebInfo'")
    set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "Build type" FORCE)
endif()

# Feature toggles
option(OBSIDIAN_ENABLE_CUDA   "Build CUDA GPU compute backend"       OFF)
option(OBSIDIAN_ENABLE_MPI    "Build MPI distributed compute layer"  OFF)
option(OBSIDIAN_ENABLE_GRPC   "Build gRPC service layer"             OFF)
option(OBSIDIAN_ENABLE_RUST   "Build Rust FFI components"            OFF)
option(OBSIDIAN_ENABLE_TESTS  "Build test suite"                     ON)
option(OBSIDIAN_ENABLE_LTO    "Enable link-time optimisation"        OFF)

# Compile commands export
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Include modules
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(Platform)
include(Optimization)
include(CompilerWarnings)
include(Sanitizers)
include(Dependencies)
include(StaticAnalysis)

# Threading
find_package(Threads REQUIRED)

# Subdirectories
add_subdirectory(kernel)
add_subdirectory(compute)
add_subdirectory(terminal)

# Packaging and installation
include(Install)

# Configuration summary
message(STATUS "")
message(STATUS "--- Obsidian ${PROJECT_VERSION} Build Configuration ---")
message(STATUS "Platform       : ${OBSIDIAN_PLATFORM} / ${OBSIDIAN_ARCH_CANONICAL}")
message(STATUS "Cache Line     : ${OBSIDIAN_CACHE_LINE_SIZE} bytes")
message(STATUS "CPU Target     : ${OBSIDIAN_CPU_TARGET}")
message(STATUS "FP Model       : ${OBSIDIAN_FP_MODEL}")
message(STATUS "C++ Standard   : ${CMAKE_CXX_STANDARD}")
message(STATUS "Build Type     : ${CMAKE_BUILD_TYPE}")
message(STATUS "Compiler       : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "SIMD Baseline  : ${OBSIDIAN_SIMD_LEVEL}")
message(STATUS "LTO / ThinLTO  : ${OBSIDIAN_ENABLE_LTO}")
message(STATUS "Fast Linker    : ${OBSIDIAN_ENABLE_FAST_LINKER}")
message(STATUS "Compiler Cache : ${OBSIDIAN_ENABLE_CCACHE}")
message(STATUS "Malloc Backend : ${OBSIDIAN_MALLOC_BACKEND}")
message(STATUS "Sanitizers     : ${OBSIDIAN_SANITIZER}")
message(STATUS "CUDA           : ${OBSIDIAN_ENABLE_CUDA}")
message(STATUS "MPI            : ${OBSIDIAN_ENABLE_MPI}")
message(STATUS "gRPC           : ${OBSIDIAN_ENABLE_GRPC}")
message(STATUS "Rust FFI       : ${OBSIDIAN_ENABLE_RUST}")
message(STATUS "------------------------------------------------------")
message(STATUS "")
