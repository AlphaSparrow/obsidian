# Compiler and linker optimization settings
include_guard(GLOBAL)

# Fast linkers (mold / lld)
option(OBSIDIAN_ENABLE_FAST_LINKER "Use fast linker (mold or lld) if available" ON)

if(OBSIDIAN_ENABLE_FAST_LINKER AND NOT MSVC)
    find_program(MOLD_LINKER NAMES mold)
    find_program(LLD_LINKER  NAMES ld.lld lld)

    if(MOLD_LINKER)
        add_link_options("-fuse-ld=mold")
        message(STATUS "[obsidian/tuning] Linker: mold (${MOLD_LINKER})")
    elseif(LLD_LINKER)
        add_link_options("-fuse-ld=lld")
        message(STATUS "[obsidian/tuning] Linker: lld (${LLD_LINKER})")
    else()
        message(STATUS "[obsidian/tuning] Fast linker requested, but mold/lld not found; using system default")
    endif()
endif()

# Compiler cache (ccache / sccache)
option(OBSIDIAN_ENABLE_CCACHE "Use ccache or sccache for build caching" ON)

if(OBSIDIAN_ENABLE_CCACHE)
    find_program(CCACHE_EXE NAMES ccache sccache)
    if(CCACHE_EXE)
        set(CMAKE_C_COMPILER_LAUNCHER   "${CCACHE_EXE}" CACHE STRING "C compiler launcher" FORCE)
        set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_EXE}" CACHE STRING "CXX compiler launcher" FORCE)
        if(OBSIDIAN_ENABLE_CUDA)
            set(CMAKE_CUDA_COMPILER_LAUNCHER "${CCACHE_EXE}" CACHE STRING "CUDA compiler launcher" FORCE)
        endif()
        message(STATUS "[obsidian/tuning] Compiler cache: ${CCACHE_EXE}")
    endif()
endif()

# Interface target
add_library(obsidian_tuning INTERFACE)
add_library(obsidian::tuning ALIAS obsidian_tuning)

# Microarchitecture tuning
set(OBSIDIAN_CPU_TARGET "generic" CACHE STRING "CPU microarchitecture (native, x86-64-v3, x86-64-v4, generic)")
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

# Floating-point model
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

# Codegen and alignment
if(NOT MSVC)
    target_compile_options(obsidian_tuning INTERFACE
        -pipe
        -Wdate-time
        -frandom-seed=obsidian
        -fmacro-prefix-map=${CMAKE_SOURCE_DIR}=.
    )

    target_compile_options(obsidian_tuning INTERFACE
        $<$<CONFIG:Release,RelWithDebInfo>:
            -falign-functions=64
            -falign-loops=32
            -fno-plt
        >
    )

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(obsidian_tuning INTERFACE
            $<$<CONFIG:Release,RelWithDebInfo>:-fno-semantic-interposition>
        )
    endif()

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
    # MSVC parallel compilation, dead symbol removal, and deterministic output
    target_compile_options(obsidian_tuning INTERFACE
        /MP
        /Zc:inline
        /Brepro
    )

    target_compile_options(obsidian_tuning INTERFACE
        $<$<CONFIG:Release,RelWithDebInfo>:/Gy /Gw>
    )
    target_link_options(obsidian_tuning INTERFACE
        $<$<CONFIG:Release,RelWithDebInfo>:/OPT:REF /OPT:ICF>
    )
endif()

# Link-Time Optimization (LTO / ThinLTO)
if(OBSIDIAN_ENABLE_LTO)
    if(CMAKE_CXX_COMPILER_ID MATCHES ".*Clang")
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

# Optional vectorization remarks
option(OBSIDIAN_ENABLE_VEC_REPORT "Emit compiler diagnostic remarks on loop vectorization" OFF)

if(OBSIDIAN_ENABLE_VEC_REPORT)
    if(CMAKE_CXX_COMPILER_ID MATCHES ".*Clang")
        target_compile_options(obsidian_tuning INTERFACE
            -Rpass=loop-vectorize
            -Rpass-missed=loop-vectorize
            -Rpass-analysis=loop-vectorize
        )
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(obsidian_tuning INTERFACE -fopt-info-vec-all)
    elseif(MSVC)
        target_compile_options(obsidian_tuning INTERFACE /Qvec-report:2)
    endif()
endif()
