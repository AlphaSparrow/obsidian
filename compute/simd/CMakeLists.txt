# Obsidian SIMD dispatch library
include(CheckCXXCompilerFlag)

# Per-ISA object libraries
if(OBSIDIAN_ARCH_FAMILY STREQUAL "x86")

    # SSE 4.2
    add_library(obsidian_simd_sse42 OBJECT simd_dispatch.cpp)
    if(NOT MSVC)
        target_compile_options(obsidian_simd_sse42 PRIVATE -msse4.2)
    endif()
    target_compile_definitions(obsidian_simd_sse42 PRIVATE OBSIDIAN_SIMD_ISA=SSE42)
    target_link_libraries(obsidian_simd_sse42 PRIVATE obsidian::tuning obsidian::warnings)

    # AVX2
    add_library(obsidian_simd_avx2 OBJECT simd_dispatch.cpp)
    if(MSVC)
        target_compile_options(obsidian_simd_avx2 PRIVATE /arch:AVX2)
    else()
        target_compile_options(obsidian_simd_avx2 PRIVATE -mavx2 -mfma)
    endif()
    target_compile_definitions(obsidian_simd_avx2 PRIVATE OBSIDIAN_SIMD_ISA=AVX2)
    target_link_libraries(obsidian_simd_avx2 PRIVATE obsidian::tuning obsidian::warnings)

    # AVX-512
    if(OBSIDIAN_SIMD_FLAGS_AVX512)
        add_library(obsidian_simd_avx512 OBJECT simd_dispatch.cpp)
        if(MSVC)
            target_compile_options(obsidian_simd_avx512 PRIVATE /arch:AVX512)
        else()
            target_compile_options(obsidian_simd_avx512 PRIVATE
                -mavx512f -mavx512bw -mavx512dq -mavx512vl
            )
        endif()
        target_compile_definitions(obsidian_simd_avx512 PRIVATE OBSIDIAN_SIMD_ISA=AVX512)
        target_link_libraries(obsidian_simd_avx512 PRIVATE obsidian::tuning obsidian::warnings)
    endif()

elseif(OBSIDIAN_ARCH_FAMILY STREQUAL "arm")

    # NEON
    add_library(obsidian_simd_neon OBJECT simd_dispatch.cpp)
    target_compile_definitions(obsidian_simd_neon PRIVATE OBSIDIAN_SIMD_ISA=NEON)
    target_link_libraries(obsidian_simd_neon PRIVATE obsidian::tuning obsidian::warnings)

endif()

# Aggregate SIMD library
add_library(obsidian_simd STATIC simd_dispatch.cpp)

if(OBSIDIAN_ARCH_FAMILY STREQUAL "x86")
    target_sources(obsidian_simd PRIVATE
        $<TARGET_OBJECTS:obsidian_simd_sse42>
        $<TARGET_OBJECTS:obsidian_simd_avx2>
    )
    if(TARGET obsidian_simd_avx512)
        target_sources(obsidian_simd PRIVATE $<TARGET_OBJECTS:obsidian_simd_avx512>)
        target_compile_definitions(obsidian_simd PUBLIC OBSIDIAN_HAS_AVX512=1)
    endif()
elseif(OBSIDIAN_ARCH_FAMILY STREQUAL "arm")
    target_sources(obsidian_simd PRIVATE $<TARGET_OBJECTS:obsidian_simd_neon>)
endif()

target_include_directories(obsidian_simd
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include/obsidian/compute/simd>
)

target_link_libraries(obsidian_simd
    PUBLIC
        obsidian::tuning
    PRIVATE
        obsidian::warnings
)

message(STATUS "[obsidian/simd] Architecture family: ${OBSIDIAN_ARCH_FAMILY}, detected ISA: ${OBSIDIAN_SIMD_LEVEL}")
