# Platform, architecture, and SIMD capability detection
include_guard(GLOBAL)

# Operating system detection
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(OBSIDIAN_PLATFORM "linux")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(OBSIDIAN_PLATFORM "darwin")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(OBSIDIAN_PLATFORM "windows")
else()
    message(WARNING "[obsidian] Unrecognised platform: ${CMAKE_SYSTEM_NAME}")
    set(OBSIDIAN_PLATFORM "unknown")
endif()

# Architecture detection (supports x86, x86_64, arm64)
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _arch)

if(_arch MATCHES "^(x86_64|amd64|x64)$")
    set(OBSIDIAN_ARCH "x86_64")
    set(OBSIDIAN_ARCH_CANONICAL "x86_64")
    set(OBSIDIAN_ARCH_FAMILY "x86")
elseif(_arch MATCHES "^(i[3-6]86|x86)$")
    set(OBSIDIAN_ARCH "x86")
    set(OBSIDIAN_ARCH_CANONICAL "x86")
    set(OBSIDIAN_ARCH_FAMILY "x86")
elseif(_arch MATCHES "^(aarch64|arm64)$")
    set(OBSIDIAN_ARCH "${_arch}")
    set(OBSIDIAN_ARCH_CANONICAL "aarch64")
    set(OBSIDIAN_ARCH_FAMILY "arm")
else()
    message(WARNING "[obsidian] Unrecognised architecture: ${CMAKE_SYSTEM_PROCESSOR}")
    set(OBSIDIAN_ARCH "${_arch}")
    set(OBSIDIAN_ARCH_CANONICAL "unknown")
    set(OBSIDIAN_ARCH_FAMILY "unknown")
endif()

# Endianness
include(TestBigEndian)
test_big_endian(OBSIDIAN_BIG_ENDIAN)

# Position-independent code
if(MSVC)
    set(OBSIDIAN_PIC_FLAG "")
else()
    set(OBSIDIAN_PIC_FLAG "-fPIC")
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

# Symbol visibility
if(NOT MSVC)
    set(CMAKE_C_VISIBILITY_PRESET hidden)
    set(CMAKE_CXX_VISIBILITY_PRESET hidden)
    set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
endif()

# MSVC runtime linkage
if(MSVC AND NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Cache line size
if(OBSIDIAN_PLATFORM STREQUAL "darwin" AND OBSIDIAN_ARCH_CANONICAL STREQUAL "aarch64")
    set(OBSIDIAN_CACHE_LINE_SIZE 128)
else()
    set(OBSIDIAN_CACHE_LINE_SIZE 64)
endif()

# SIMD capability detection
set(OBSIDIAN_SIMD_LEVEL "NONE")

if(OBSIDIAN_ARCH_FAMILY STREQUAL "x86")
    include(CheckCXXCompilerFlag)

    set(_saved_cxx_std ${CMAKE_CXX_STANDARD})
    set(_saved_cxx_req ${CMAKE_CXX_STANDARD_REQUIRED})
    unset(CMAKE_CXX_STANDARD)
    unset(CMAKE_CXX_STANDARD_REQUIRED)

    if(MSVC)
        # MSVC architecture flags
        check_cxx_compiler_flag("/arch:AVX512" _has_msvc_avx512)
        check_cxx_compiler_flag("/arch:AVX2"   _has_msvc_avx2)

        if(_has_msvc_avx512)
            set(OBSIDIAN_SIMD_LEVEL "AVX512")
            set(OBSIDIAN_SIMD_FLAGS_AVX512 "/arch:AVX512")
            set(OBSIDIAN_SIMD_FLAGS_AVX2   "/arch:AVX2")
        elseif(_has_msvc_avx2)
            set(OBSIDIAN_SIMD_LEVEL "AVX2")
            set(OBSIDIAN_SIMD_FLAGS_AVX2   "/arch:AVX2")
        else()
            set(OBSIDIAN_SIMD_LEVEL "SSE42")
        endif()
        set(OBSIDIAN_SIMD_FLAGS_SSE42 "")
    else()
        # GCC / Clang
        check_cxx_compiler_flag("-mavx512f" _has_avx512)
        check_cxx_compiler_flag("-mavx2"    _has_avx2)
        check_cxx_compiler_flag("-msse4.2"  _has_sse42)

        if(_has_avx512)
            set(OBSIDIAN_SIMD_LEVEL "AVX512")
            set(OBSIDIAN_SIMD_FLAGS_AVX512 "-mavx512f;-mavx512bw;-mavx512dq;-mavx512vl")
        endif()
        if(_has_avx2)
            if(OBSIDIAN_SIMD_LEVEL STREQUAL "NONE")
                set(OBSIDIAN_SIMD_LEVEL "AVX2")
            endif()
            set(OBSIDIAN_SIMD_FLAGS_AVX2 "-mavx2;-mfma")
        endif()
        if(_has_sse42)
            if(OBSIDIAN_SIMD_LEVEL STREQUAL "NONE")
                set(OBSIDIAN_SIMD_LEVEL "SSE42")
            endif()
            set(OBSIDIAN_SIMD_FLAGS_SSE42 "-msse4.2")
        endif()
    endif()

    set(CMAKE_CXX_STANDARD ${_saved_cxx_std})
    set(CMAKE_CXX_STANDARD_REQUIRED ${_saved_cxx_req})

elseif(OBSIDIAN_ARCH_FAMILY STREQUAL "arm")
    set(OBSIDIAN_SIMD_LEVEL "NEON")
    set(OBSIDIAN_SIMD_FLAGS_NEON "")
endif()

# Summary
message(STATUS "[obsidian] Platform        : ${OBSIDIAN_PLATFORM}")
message(STATUS "[obsidian] Architecture    : ${OBSIDIAN_ARCH} (family: ${OBSIDIAN_ARCH_FAMILY})")
message(STATUS "[obsidian] Cache line size : ${OBSIDIAN_CACHE_LINE_SIZE} bytes")
message(STATUS "[obsidian] Big endian      : ${OBSIDIAN_BIG_ENDIAN}")
message(STATUS "[obsidian] SIMD level      : ${OBSIDIAN_SIMD_LEVEL}")
