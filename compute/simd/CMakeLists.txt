# compute/simd/CMakeLists.txt
# ---------------------------------------------------------------------------
# Obsidian SIMD dispatch library.
#
# Strategy: compile the SAME source file multiple times with different arch
# flags, then dispatch at runtime via CPUID (x86) or feature detection (ARM).
#
# This file sets up the multi-arch compilation framework.  Actual SIMD
# kernel sources (.cpp) will be added to the target_sources() calls below
# as they are written.
#
# Usage from downstream:
#   target_link_libraries(my_target PRIVATE obsidian_simd)
#
# At runtime, call obsidian::simd::dispatch() to get function pointers
# for the best available ISA.
# ---------------------------------------------------------------------------

include(CheckCXXCompilerFlag)

# ── Per-ISA object libraries ────────────────────────────────────────────────
# Each ISA level is its own OBJECT library so the compiler flags don't leak
# across translation units.

# x86_64 paths
if(OBSIDIAN_ARCH_CANONICAL STREQUAL "x86_64")

    # --- SSE 4.2 (baseline for all x86_64) ---
    add_library(obsidian_simd_sse42 OBJECT simd_dispatch.cpp)
    # TODO: add per-ISA kernel sources here
    # target_sources(obsidian_simd_sse42 PRIVATE kernels/vec_ops_sse42.cpp)
    if(MSVC)
        # SSE4.2 is baseline on x64 MSVC — no flag needed
    else()
        target_compile_options(obsidian_simd_sse42 PRIVATE -msse4.2)
    endif()
    target_compile_definitions(obsidian_simd_sse42 PRIVATE OBSIDIAN_SIMD_ISA=SSE42)

    # --- AVX2 ---
    add_library(obsidian_simd_avx2 OBJECT simd_dispatch.cpp)
    # TODO: add per-ISA kernel sources here
    # target_sources(obsidian_simd_avx2 PRIVATE kernels/vec_ops_avx2.cpp)
    if(MSVC)
        target_compile_options(obsidian_simd_avx2 PRIVATE /arch:AVX2)
    else()
        target_compile_options(obsidian_simd_avx2 PRIVATE -mavx2 -mfma)
    endif()
    target_compile_definitions(obsidian_simd_avx2 PRIVATE OBSIDIAN_SIMD_ISA=AVX2)

    # --- AVX-512 (optional — not all x86_64 CPUs support this) ---
    if(OBSIDIAN_SIMD_FLAGS_AVX512)
        add_library(obsidian_simd_avx512 OBJECT simd_dispatch.cpp)
        # TODO: add per-ISA kernel sources here
        # target_sources(obsidian_simd_avx512 PRIVATE kernels/vec_ops_avx512.cpp)
        if(MSVC)
            target_compile_options(obsidian_simd_avx512 PRIVATE /arch:AVX512)
        else()
            target_compile_options(obsidian_simd_avx512 PRIVATE
                -mavx512f -mavx512bw -mavx512dq -mavx512vl
            )
        endif()
        target_compile_definitions(obsidian_simd_avx512 PRIVATE OBSIDIAN_SIMD_ISA=AVX512)
    endif()

# aarch64 path
elseif(OBSIDIAN_ARCH_CANONICAL STREQUAL "aarch64")

    # --- NEON (mandatory on ARMv8-A) ---
    add_library(obsidian_simd_neon OBJECT simd_dispatch.cpp)
    # TODO: add per-ISA kernel sources here
    # target_sources(obsidian_simd_neon PRIVATE kernels/vec_ops_neon.cpp)
    target_compile_definitions(obsidian_simd_neon PRIVATE OBSIDIAN_SIMD_ISA=NEON)
    # No extra flags needed — NEON is always on for aarch64.

endif()

# ── Aggregate SIMD library ──────────────────────────────────────────────────
# Links all ISA-specific object libraries together.  The runtime dispatcher
# (to be implemented in dispatch.cpp) selects the best code path via CPUID.

add_library(obsidian_simd STATIC simd_dispatch.cpp)

target_include_directories(obsidian_simd
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include/obsidian/compute/simd>
)

if(OBSIDIAN_ARCH_CANONICAL STREQUAL "x86_64")
    target_link_libraries(obsidian_simd PRIVATE
        obsidian_simd_sse42
        obsidian_simd_avx2
    )
    if(TARGET obsidian_simd_avx512)
        target_link_libraries(obsidian_simd PRIVATE obsidian_simd_avx512)
        target_compile_definitions(obsidian_simd PUBLIC OBSIDIAN_HAS_AVX512=1)
    endif()
elseif(OBSIDIAN_ARCH_CANONICAL STREQUAL "aarch64")
    target_link_libraries(obsidian_simd PRIVATE obsidian_simd_neon)
endif()

message(STATUS "[obsidian/simd] Architecture: ${OBSIDIAN_ARCH_CANONICAL}, max ISA: ${OBSIDIAN_SIMD_LEVEL}")
