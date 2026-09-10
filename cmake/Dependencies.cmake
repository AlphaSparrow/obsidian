# cmake/Dependencies.cmake
# ---------------------------------------------------------------------------
# Third-party dependency declarations.
#
# Policy:
#   - Lightweight / header-only deps → FetchContent (vendored at configure time)
#   - System SDKs (CUDA, MPI, gRPC)  → find_package (must be pre-installed)
#   - Rust crates                    → Corrosion (CMake ↔ Cargo bridge)
#
# Nothing is fetched unless actually consumed by a target. FetchContent only
# downloads when FetchContent_MakeAvailable() is called.
# ---------------------------------------------------------------------------

include_guard(GLOBAL)
include(FetchContent)

# ── C++ Libraries ────────────────────────────────────────────────────────────

# fmt — Modern formatting library (std::format backport)
FetchContent_Declare(
    fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG        11.1.4
    GIT_SHALLOW    TRUE
)

# spdlog — Fast structured logging (uses fmt)
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.15.1
    GIT_SHALLOW    TRUE
)
set(SPDLOG_FMT_EXTERNAL ON CACHE BOOL "" FORCE)

# Abseil — Google's C++ common library (flat containers, strings, time, etc.)
FetchContent_Declare(
    abseil
    GIT_REPOSITORY https://github.com/abseil/abseil-cpp.git
    GIT_TAG        20240722.0
    GIT_SHALLOW    TRUE
)
set(ABSL_PROPAGATE_CXX_STD ON CACHE BOOL "" FORCE)

# ── Rust Integration (Corrosion) ─────────────────────────────────────────────
# Corrosion bridges CMake and Cargo so Rust static libraries appear as
# regular CMake imported targets.
#
# Usage downstream:
#   corrosion_import_crate(MANIFEST_PATH path/to/Cargo.toml)
#   target_link_libraries(my_cpp_target PRIVATE my_rust_crate)

FetchContent_Declare(
    Corrosion
    GIT_REPOSITORY https://github.com/corrosion-rs/corrosion.git
    GIT_TAG        v0.5.1
    GIT_SHALLOW    TRUE
)

# ── System SDKs (optional — guarded by feature toggles) ─────────────────────

# CUDA
if(OBSIDIAN_ENABLE_CUDA)
    # CMAKE_CUDA_COMPILER must be set or nvcc must be on PATH
    include(CheckLanguage)
    check_language(CUDA)
    if(CMAKE_CUDA_COMPILER)
        enable_language(CUDA)
        message(STATUS "[obsidian] CUDA compiler: ${CMAKE_CUDA_COMPILER}")
    else()
        message(WARNING "[obsidian] OBSIDIAN_ENABLE_CUDA=ON but no CUDA compiler found. Disabling.")
        set(OBSIDIAN_ENABLE_CUDA OFF CACHE BOOL "" FORCE)
    endif()
endif()

# MPI
if(OBSIDIAN_ENABLE_MPI)
    find_package(MPI QUIET)
    if(NOT MPI_FOUND)
        message(WARNING "[obsidian] OBSIDIAN_ENABLE_MPI=ON but MPI not found. Disabling.")
        set(OBSIDIAN_ENABLE_MPI OFF CACHE BOOL "" FORCE)
    else()
        message(STATUS "[obsidian] MPI: ${MPI_CXX_COMPILER}")
    endif()
endif()

# gRPC + Protobuf
if(OBSIDIAN_ENABLE_GRPC)
    find_package(Protobuf QUIET)
    find_package(gRPC QUIET)
    if(NOT Protobuf_FOUND OR NOT gRPC_FOUND)
        message(WARNING "[obsidian] OBSIDIAN_ENABLE_GRPC=ON but Protobuf/gRPC not found. Disabling.")
        set(OBSIDIAN_ENABLE_GRPC OFF CACHE BOOL "" FORCE)
    else()
        message(STATUS "[obsidian] Protobuf: ${Protobuf_VERSION}")
        message(STATUS "[obsidian] gRPC:     ${gRPC_VERSION}")
    endif()
endif()

# ── Helper: Make available on demand ─────────────────────────────────────────
# Call this macro from component CMakeLists.txt files that actually need deps.
#
# Example:
#   obsidian_require(fmt spdlog)
#
macro(obsidian_require)
    foreach(_dep ${ARGN})
        FetchContent_MakeAvailable(${_dep})
    endforeach()
endmacro()
