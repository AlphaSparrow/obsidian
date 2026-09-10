# cmake/Sanitizers.cmake
# ---------------------------------------------------------------------------
# Granular sanitizer configuration.
#
# Supports:
#   OBSIDIAN_SANITIZER = "asan"   — AddressSanitizer
#   OBSIDIAN_SANITIZER = "ubsan"  — UndefinedBehaviorSanitizer
#   OBSIDIAN_SANITIZER = "tsan"   — ThreadSanitizer (incompatible with ASan)
#   OBSIDIAN_SANITIZER = "msan"   — MemorySanitizer (Clang-only, incompatible with ASan)
#   OBSIDIAN_SANITIZER = "asan,ubsan" — Combined (default for Debug)
#   OBSIDIAN_SANITIZER = "none"   — Disable all
#
# Usage:
#   cmake -DOBSIDIAN_SANITIZER=tsan ..
# ---------------------------------------------------------------------------

include_guard(GLOBAL)

if(MSVC)
    # MSVC supports /fsanitize=address only (no UBSan, TSan, MSan)
    if(OBSIDIAN_SANITIZER AND NOT OBSIDIAN_SANITIZER STREQUAL "none")
        if(OBSIDIAN_SANITIZER MATCHES "asan|address")
            add_compile_options(/fsanitize=address)
            message(STATUS "[obsidian/sanitizers] MSVC ASan enabled")
        else()
            message(WARNING "[obsidian/sanitizers] MSVC only supports ASan. Ignoring: ${OBSIDIAN_SANITIZER}")
        endif()
    endif()
    return()
endif()

# GCC / Clang
if(NOT DEFINED OBSIDIAN_SANITIZER OR OBSIDIAN_SANITIZER STREQUAL "")
    set(OBSIDIAN_SANITIZER "none")
endif()

if(OBSIDIAN_SANITIZER STREQUAL "none")
    message(STATUS "[obsidian/sanitizers] Disabled")
    return()
endif()

set(_san_flags "")
set(_san_link "")

# Parse comma-separated sanitizer list
string(REPLACE "," ";" _san_list "${OBSIDIAN_SANITIZER}")

foreach(_san IN LISTS _san_list)
    string(STRIP "${_san}" _san)
    if(_san STREQUAL "asan" OR _san STREQUAL "address")
        list(APPEND _san_flags -fsanitize=address)
        list(APPEND _san_link  -fsanitize=address)
    elseif(_san STREQUAL "ubsan" OR _san STREQUAL "undefined")
        list(APPEND _san_flags -fsanitize=undefined)
        list(APPEND _san_link  -fsanitize=undefined)
    elseif(_san STREQUAL "tsan" OR _san STREQUAL "thread")
        list(APPEND _san_flags -fsanitize=thread)
        list(APPEND _san_link  -fsanitize=thread)
    elseif(_san STREQUAL "msan" OR _san STREQUAL "memory")
        if(NOT CMAKE_CXX_COMPILER_ID MATCHES ".*Clang")
            message(WARNING "[obsidian/sanitizers] MSan is Clang-only. Skipping.")
            continue()
        endif()
        list(APPEND _san_flags -fsanitize=memory -fsanitize-memory-track-origins=2)
        list(APPEND _san_link  -fsanitize=memory)
    elseif(_san STREQUAL "leak")
        list(APPEND _san_flags -fsanitize=leak)
        list(APPEND _san_link  -fsanitize=leak)
    else()
        message(WARNING "[obsidian/sanitizers] Unknown sanitizer: ${_san}")
    endif()
endforeach()

# Common sanitizer flags
if(_san_flags)
    list(APPEND _san_flags -fno-omit-frame-pointer -fno-optimize-sibling-calls)

    add_compile_options(${_san_flags})
    add_link_options(${_san_link})

    message(STATUS "[obsidian/sanitizers] Enabled: ${OBSIDIAN_SANITIZER}")
    message(STATUS "[obsidian/sanitizers] Compile: ${_san_flags}")
endif()
