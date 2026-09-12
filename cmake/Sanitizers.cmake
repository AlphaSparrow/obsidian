# cmake/Sanitizers.cmake
# ---------------------------------------------------------------------------
# Dynamic Sanitizer Configuration for Obsidian.
#
# Supported modes:
#   OBSIDIAN_SANITIZER = "asan"       — AddressSanitizer
#   OBSIDIAN_SANITIZER = "ubsan"      — UndefinedBehaviorSanitizer
#   OBSIDIAN_SANITIZER = "tsan"       — ThreadSanitizer (incompatible with ASan)
#   OBSIDIAN_SANITIZER = "msan"       — MemorySanitizer (Clang-only, incompatible with ASan)
#   OBSIDIAN_SANITIZER = "asan,ubsan" — Combined Address + Undefined Behavior
#   OBSIDIAN_SANITIZER = "none"       — Disabled
#
# Exports:
#   obsidian::sanitizers (INTERFACE target)
# ---------------------------------------------------------------------------

include_guard(GLOBAL)

add_library(obsidian_sanitizers INTERFACE)
add_library(obsidian::sanitizers ALIAS obsidian_sanitizers)

set(OBSIDIAN_SANITIZER "none" CACHE STRING "Sanitizer configuration (none, asan, ubsan, tsan, msan, asan,ubsan)")

if(OBSIDIAN_SANITIZER STREQUAL "none" OR OBSIDIAN_SANITIZER STREQUAL "")
    return()
endif()

if(MSVC)
    if(OBSIDIAN_SANITIZER MATCHES "asan|address")
        target_compile_options(obsidian_sanitizers INTERFACE /fsanitize=address)
        message(STATUS "[obsidian/sanitizers] MSVC ASan enabled on obsidian::sanitizers")
    else()
        message(WARNING "[obsidian/sanitizers] MSVC only supports ASan. Requested '${OBSIDIAN_SANITIZER}' will be ignored.")
    endif()
    return()
endif()

# GCC / Clang
set(_san_compile "")
set(_san_link "")

string(REPLACE "," ";" _san_list "${OBSIDIAN_SANITIZER}")

foreach(_san IN LISTS _san_list)
    string(STRIP "${_san}" _san)
    if(_san STREQUAL "asan" OR _san STREQUAL "address")
        list(APPEND _san_compile -fsanitize=address)
        list(APPEND _san_link    -fsanitize=address)
    elseif(_san STREQUAL "ubsan" OR _san STREQUAL "undefined")
        list(APPEND _san_compile -fsanitize=undefined)
        list(APPEND _san_link    -fsanitize=undefined)
    elseif(_san STREQUAL "tsan" OR _san STREQUAL "thread")
        list(APPEND _san_compile -fsanitize=thread)
        list(APPEND _san_link    -fsanitize=thread)
    elseif(_san STREQUAL "msan" OR _san STREQUAL "memory")
        if(NOT CMAKE_CXX_COMPILER_ID MATCHES ".*Clang")
            message(WARNING "[obsidian/sanitizers] MSan is Clang-only. Skipping.")
            continue()
        endif()
        list(APPEND _san_compile -fsanitize=memory -fsanitize-memory-track-origins=2)
        list(APPEND _san_link    -fsanitize=memory)
    elseif(_san STREQUAL "leak")
        list(APPEND _san_compile -fsanitize=leak)
        list(APPEND _san_link    -fsanitize=leak)
    else()
        message(WARNING "[obsidian/sanitizers] Unknown sanitizer: ${_san}")
    endif()
endforeach()

if(_san_compile)
    list(APPEND _san_compile -fno-omit-frame-pointer -fno-optimize-sibling-calls)

    target_compile_options(obsidian_sanitizers INTERFACE ${_san_compile})
    target_link_options(obsidian_sanitizers INTERFACE ${_san_link})

    message(STATUS "[obsidian/sanitizers] Enabled '${OBSIDIAN_SANITIZER}' on obsidian::sanitizers")
endif()
