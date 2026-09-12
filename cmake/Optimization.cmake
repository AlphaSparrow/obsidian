# cmake/Optimization.cmake
# ---------------------------------------------------------------------------
# High-Performance Compiler & Linker Optimization Policy for Obsidian.
#
# Tailored for low-latency quantitative finance systems:
#   - Fast Linkers (mold, lld) for instant iteration and LTO scalability
#   - Compiler Caching (ccache, sccache)
#   - Microarchitecture Tuning (-march=native, x86-64-v3, x86-64-v4)
#   - Controlled Floating-Point Semantics (strict vs relaxed)
#   - ThinLTO / LTO Configuration
#   - Code and Cache-Line Alignment (-falign-functions=64)
#   - Vectorization Diagnostics Reporting
#
# Exports:
#   obsidian::tuning (INTERFACE target)
# ---------------------------------------------------------------------------

include_guard(GLOBAL)

# ── 1. Fast Linker Integration (mold / lld) ─────────────────────────────────

option(OBSIDIAN_ENABLE_FAST_LINKER "Use ultra-fast modern linker (mold or lld) if available" ON)

if(OBSIDIAN_ENABLE_FAST_LINKER AND NOT MSVC)
    find_program(MOLD_LINKER NAMES mold)
    find_program(LLD_LINKER  NAMES ld.lld lld)

    if(MOLD_LINKER)
        # mold is the fastest ELF linker in existence
        add_link_options("-fuse-ld=mold")
        message(STATUS "[obsidian/tuning] Fast linker enabled: mold (${MOLD_LINKER})")
    elseif(LLD_LINKER)
        add_link_options("-fuse-ld=lld")
        message(STATUS "[obsidian/tuning] Fast linker enabled: lld (${LLD_LINKER})")
    else()
        message(STATUS "[obsidian/tuning] Fast linker requested, but neither mold nor lld was found; using system default")
    endif()
endif()

# ── 2. Compiler Launcher (ccache / sccache) ──────────────────────────────────

option(OBSIDIAN_ENABLE_CCACHE "Use compiler cache (ccache or sccache) to accelerate rebuilds" ON)

if(OBSIDIAN_ENABLE_CCACHE)
    find_program(CCACHE_EXE NAMES ccache sccache)
    if(CCACHE_EXE)
        set(CMAKE_C_COMPILER_LAUNCHER   "${CCACHE_EXE}" CACHE STRING "C compiler launcher" FORCE)
        set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_EXE}" CACHE STRING "CXX compiler launcher" FORCE)
        if(OBSIDIAN_ENABLE_CUDA)
            set(CMAKE_CUDA_COMPILER_LAUNCHER "${CCACHE_EXE}" CACHE STRING "CUDA compiler launcher" FORCE)
        endif()
        message(STATUS "[obsidian/tuning] Compiler cache enabled: ${CCACHE_EXE}")
    else()
        message(STATUS "[obsidian/tuning] Compiler cache requested, but ccache/sccache was not found")
    endif()
endif()

# ── 3. Interface Target Definition ──────────────────────────────────────────

add_library(obsidian_tuning INTERFACE)
add_library(obsidian::tuning ALIAS obsidian_tuning)

# ── 4. Target Microarchitecture Selection ───────────────────────────────────

# CPU Target options:
#   "native"    — tune specifically for the host machine (best for local benchmarks & production nodes)
#   "x86-64-v3" — AVX, AVX2, BMI1, BMI2, FMA (standard modern server baseline)
#   "x86-64-v4" — AVX-512F, BW, CD, DQ, VL (modern Xeon/EPYC)
#   "generic"   — baseline portable x86_64 / aarch64
set(OBSIDIAN_CPU_TARGET "generic" CACHE STRING "CPU microarchitecture target (native, x86-64-v3, x86-64-v4, generic)")
set_property(CACHE OBSIDIAN_CPU_TARGET PROPERTY STRINGS native x86-64-v3 x86-64-v4 generic)

if(MSVC)
    if(OBSIDIAN_CPU_TARGET STREQUAL "native" OR OBSIDIAN_CPU_TARGET STREQUAL "x86-64-v3")
        target_compile_options(obsidian_tuning INTERFACE /arch:AVX2)
    elseif(OBSIDIAN_CPU_TARGET STREQUAL "x86-64-v4")
        target_compile_options(obsidian_tuning INTERFACE /arch:AVX512)
    endif()
else()
    if(OBSIDIAN_CPU_TARGET STREQUAL "native")
        target_compile_options(obsidian_tuning INTERFACE -march=native -mtune=native)
    elseif(OBSIDIAN_CPU_TARGET STREQUAL "x86-64-v3")
        target_compile_options(obsidian_tuning INTERFACE -march=x86-64-v3)
    elseif(OBSIDIAN_CPU_TARGET STREQUAL "x86-64-v4")
        target_compile_options(obsidian_tuning INTERFACE -march=x86-64-v4)
    endif()
endif()

# ── 5. Floating-Point Model (Numerical Precision Policy) ─────────────────────

# In quantitative finance, blind -ffast-math breaks NaN/Inf checks, which can be
# disastrous for pricing and risk engines.
#   "strict"  — Full IEEE-754 compliance (preserves NaN, Inf, signed zeros)
#   "relaxed" — Allows reciprocal math, disables errno and trapping math, but keeps NaN/Inf intact
#   "fast"    — Aggressive fast-math (only for isolated compute kernels)
set(OBSIDIAN_FP_MODEL "strict" CACHE STRING "Floating point model (strict, relaxed, fast)")
set_property(CACHE OBSIDIAN_FP_MODEL PROPERTY STRINGS strict relaxed fast)

if(MSVC)
    if(OBSIDIAN_FP_MODEL STREQUAL "strict")
        target_compile_options(obsidian_tuning INTERFACE /fp:precise)
    elseif(OBSIDIAN_FP_MODEL STREQUAL "fast")
        target_compile_options(obsidian_tuning INTERFACE /fp:fast)
    endif()
else()
    if(OBSIDIAN_FP_MODEL STREQUAL "relaxed")
        # Safe performance flags: eliminate math errno overhead and reciprocal division without NaN corruption
        target_compile_options(obsidian_tuning INTERFACE
            -fno-math-errno
            -fno-trapping-math
            -freciprocal-math
            -fno-signed-zeros
        )
    elseif(OBSIDIAN_FP_MODEL STREQUAL "fast")
        target_compile_options(obsidian_tuning INTERFACE -ffast-math)
    endif()
endif()

# ── 6. Codegen & Cache Alignment Optimization ────────────────────────────────

if(NOT MSVC)
    # Align functions to 64-byte cache line boundaries to avoid instruction cache line splits
    # Align loops to 32 bytes for efficient branch target buffering
    target_compile_options(obsidian_tuning INTERFACE
        $<$<CONFIG:Release,RelWithDebInfo>:
            -falign-functions=64
            -falign-loops=32
            -fno-plt                     # Call functions directly without Procedure Linkage Table jumps
        >
    )

    # In ELF, allow compiler to inline exported symbols within the same binary
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(obsidian_tuning INTERFACE
            $<$<CONFIG:Release,RelWithDebInfo>:-fno-semantic-interposition>
        )
    endif()

    # Dead code and unused section elimination
    target_compile_options(obsidian_tuning INTERFACE
        $<$<CONFIG:Release,RelWithDebInfo>:-ffunction-sections -fdata-sections>
    )
    if(APPLE)
        target_link_options(obsidian_tuning INTERFACE
            $<$<CONFIG:Release,RelWithDebInfo>:-Wl,-dead_strip>
        )
    else()
        target_link_options(obsidian_tuning INTERFACE
            $<$<CONFIG:Release,RelWithDebInfo>:-Wl,--gc-sections>
        )
    endif()
else()
    # MSVC equivalent for dead code stripping and function-level linking
    target_compile_options(obsidian_tuning INTERFACE
        $<$<CONFIG:Release,RelWithDebInfo>:/Gy /Gw>
    )
    target_link_options(obsidian_tuning INTERFACE
        $<$<CONFIG:Release,RelWithDebInfo>:/OPT:REF /OPT:ICF>
    )
endif()

# ── 7. Link-Time Optimization (LTO / ThinLTO) ───────────────────────────────

if(OBSIDIAN_ENABLE_LTO)
    if(CMAKE_CXX_COMPILER_ID MATCHES ".*Clang")
        # ThinLTO provides ~98% of full LTO performance with scalable multi-threaded link times
        target_compile_options(obsidian_tuning INTERFACE $<$<CONFIG:Release,RelWithDebInfo>:-flto=thin>)
        target_link_options(obsidian_tuning    INTERFACE $<$<CONFIG:Release,RelWithDebInfo>:-flto=thin>)
        message(STATUS "[obsidian/tuning] ThinLTO enabled for Clang")
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(obsidian_tuning INTERFACE $<$<CONFIG:Release,RelWithDebInfo>:-flto=auto -fno-fat-lto-objects>)
        target_link_options(obsidian_tuning    INTERFACE $<$<CONFIG:Release,RelWithDebInfo>:-flto=auto>)
        message(STATUS "[obsidian/tuning] LTO (auto) enabled for GCC")
    elseif(MSVC)
        target_compile_options(obsidian_tuning INTERFACE $<$<CONFIG:Release,RelWithDebInfo>:/GL>)
        target_link_options(obsidian_tuning    INTERFACE $<$<CONFIG:Release,RelWithDebInfo>:/LTCG>)
        message(STATUS "[obsidian/tuning] LTCG enabled for MSVC")
    else()
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
        message(STATUS "[obsidian/tuning] Generic IPO/LTO enabled")
    endif()
endif()

# ── 8. Vectorization Diagnostics (Optional) ─────────────────────────────────

option(OBSIDIAN_ENABLE_VEC_REPORT "Emit compiler diagnostic remarks on loop vectorization" OFF)

if(OBSIDIAN_ENABLE_VEC_REPORT)
    if(CMAKE_CXX_COMPILER_ID MATCHES ".*Clang")
        target_compile_options(obsidian_tuning INTERFACE
            -Rpass=loop-vectorize
            -Rpass-missed=loop-vectorize
            -Rpass-analysis=loop-vectorize
        )
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(obsidian_tuning INTERFACE
            -fopt-info-vec-all
        )
    elseif(MSVC)
        target_compile_options(obsidian_tuning INTERFACE /Qvec-report:2)
    endif()
    message(STATUS "[obsidian/tuning] Vectorizer diagnostics reporting enabled")
endif()
