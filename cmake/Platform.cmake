# cmake/Platform.cmake
# ---------------------------------------------------------------------------
# Cross-platform ABI detection and configuration.
#
# Exported variables:
#   OBSIDIAN_PLATFORM       — "linux" | "darwin" | "windows"
#   OBSIDIAN_ARCH           — "x86_64" | "aarch64" | "arm64"
#   OBSIDIAN_ARCH_CANONICAL — normalised to "x86_64" or "aarch64"
#   OBSIDIAN_SIMD_LEVEL     — highest detected SIMD ISA extension
#   OBSIDIAN_PIC_FLAG       — position-independent code flag (empty on MSVC)
# ---------------------------------------------------------------------------

include_guard(GLOBAL)

# ── 1. Operating system ─────────────────────────────────────────────────────

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

# ── 2. Architecture ─────────────────────────────────────────────────────────

string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _arch)

if(_arch MATCHES "^(x86_64|amd64|x64)$")
    set(OBSIDIAN_ARCH "x86_64")
    set(OBSIDIAN_ARCH_CANONICAL "x86_64")
elseif(_arch MATCHES "^(aarch64|arm64)$")
    set(OBSIDIAN_ARCH "${_arch}")
    set(OBSIDIAN_ARCH_CANONICAL "aarch64")
else()
    message(WARNING "[obsidian] Unrecognised architecture: ${CMAKE_SYSTEM_PROCESSOR}")
    set(OBSIDIAN_ARCH "${_arch}")
    set(OBSIDIAN_ARCH_CANONICAL "unknown")
endif()

# ── 3. Endianness ────────────────────────────────────────────────────────────

include(TestBigEndian)
test_big_endian(OBSIDIAN_BIG_ENDIAN)

# ── 4. Position-independent code ─────────────────────────────────────────────

if(MSVC)
    set(OBSIDIAN_PIC_FLAG "")
else()
    set(OBSIDIAN_PIC_FLAG "-fPIC")
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

# ── 5. Symbol visibility ────────────────────────────────────────────────────
# Default-hide on ELF/Mach-O so only explicitly exported symbols appear in .so/.dylib.

if(NOT MSVC)
    set(CMAKE_C_VISIBILITY_PRESET hidden)
    set(CMAKE_CXX_VISIBILITY_PRESET hidden)
    set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
endif()

# ── 6. MSVC runtime linkage ─────────────────────────────────────────────────
# Default to static CRT (/MT) for release, dynamic (/MD) for debug.
# Consumers can override via CMAKE_MSVC_RUNTIME_LIBRARY.

if(MSVC AND NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# ── 7. Struct packing / alignment notes ──────────────────────────────────────
# Cross-ABI data structures that go over FFI boundaries MUST use:
#   - #pragma pack(push, 1) / __attribute__((packed))  on GCC/Clang
#   - #pragma pack(push, 1)                            on MSVC
# This is enforced in code, not here. This comment exists so you know the
# policy lives at the source level, not at the build-system level.

# ── 8. SIMD capability detection ────────────────────────────────────────────
# Sets OBSIDIAN_SIMD_LEVEL and per-level compile flags.
#
# On x86_64 the hierarchy is: SSE4.2 → AVX2 → AVX-512
# On aarch64 the baseline is: NEON (always available on ARMv8-A)
#
# NOTE: This detects the *build host* capability for -march=native builds.
# For cross-compilation the toolchain file overrides these.

set(OBSIDIAN_SIMD_LEVEL "NONE")

if(OBSIDIAN_ARCH_CANONICAL STREQUAL "x86_64")
    # Check from highest to lowest. CMake does not have intrinsic CPUID
    # support, so we probe via compiler feature flags and test-compile.
    #
    # NOTE: check_cxx_compiler_flag inherits CMAKE_CXX_STANDARD. If the
    # compiler doesn't support the requested standard (e.g. GCC 6 + C++20)
    # the test-compile will fail even though the flag itself is valid. We
    # temporarily drop the standard requirement for these probes.

    include(CheckCXXCompilerFlag)

    set(_saved_cxx_standard ${CMAKE_CXX_STANDARD})
    set(_saved_cxx_standard_required ${CMAKE_CXX_STANDARD_REQUIRED})
    unset(CMAKE_CXX_STANDARD)
    unset(CMAKE_CXX_STANDARD_REQUIRED)

    check_cxx_compiler_flag("-mavx512f" _has_avx512)
    check_cxx_compiler_flag("-mavx2"    _has_avx2)
    check_cxx_compiler_flag("-msse4.2"  _has_sse42)

    set(CMAKE_CXX_STANDARD ${_saved_cxx_standard})
    set(CMAKE_CXX_STANDARD_REQUIRED ${_saved_cxx_standard_required})

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

    # MSVC equivalent flags
    if(MSVC)
        set(OBSIDIAN_SIMD_FLAGS_AVX512 "/arch:AVX512")
        set(OBSIDIAN_SIMD_FLAGS_AVX2   "/arch:AVX2")
        set(OBSIDIAN_SIMD_FLAGS_SSE42  "")  # SSE4.2 is baseline on x64 MSVC
    endif()

elseif(OBSIDIAN_ARCH_CANONICAL STREQUAL "aarch64")
    set(OBSIDIAN_SIMD_LEVEL "NEON")
    # NEON is mandatory on ARMv8-A, no extra flags needed.
    set(OBSIDIAN_SIMD_FLAGS_NEON "")
endif()

# ── 9. Summary ───────────────────────────────────────────────────────────────

message(STATUS "[obsidian] Platform     : ${OBSIDIAN_PLATFORM}")
message(STATUS "[obsidian] Architecture : ${OBSIDIAN_ARCH} (canonical: ${OBSIDIAN_ARCH_CANONICAL})")
message(STATUS "[obsidian] Big endian   : ${OBSIDIAN_BIG_ENDIAN}")
message(STATUS "[obsidian] SIMD level   : ${OBSIDIAN_SIMD_LEVEL}")
