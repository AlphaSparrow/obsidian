# cmake/CompilerWarnings.cmake
# ---------------------------------------------------------------------------
# Compiler warning policy.
#
# Usage:
#   obsidian_set_warnings(target_name)
#   obsidian_set_warnings(target_name WARNINGS_AS_ERRORS OFF)
# ---------------------------------------------------------------------------

include_guard(GLOBAL)

function(obsidian_set_warnings _target)
    # Parse optional WARNINGS_AS_ERRORS flag (default: ON)
    cmake_parse_arguments(ARG "" "WARNINGS_AS_ERRORS" "" ${ARGN})
    if(NOT DEFINED ARG_WARNINGS_AS_ERRORS)
        set(ARG_WARNINGS_AS_ERRORS ON)
    endif()

    set(_msvc_warnings
        /W4
        /permissive-          # strict standards conformance
        /w14242               # narrowing conversion
        /w14254               # operator conversion
        /w14263               # member function does not override
        /w14265               # class has virtual functions but dtor is not virtual
        /w14287               # unsigned/negative constant mismatch
        /w14296               # expression is always true/false
        /w14311               # pointer truncation
        /w14545               # expression before comma evaluates to function
        /w14546               # function call before comma missing arg list
        /w14547               # operator before comma has no effect
        /w14549               # operator before comma has no effect
        /w14555               # expression has no effect
        /w14619               # pragma warning: unknown warning number
        /w14640               # thread-unsafe static member init
        /w14826               # narrowing conversion
        /w14905               # wide string literal cast
        /w14906               # string literal cast
        /w14928               # illegal copy-init
    )

    set(_clang_warnings
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wnon-virtual-dtor
        -Wold-style-cast
        -Wcast-align
        -Wunused
        -Woverloaded-virtual
        -Wconversion
        -Wsign-conversion
        -Wnull-dereference
        -Wdouble-promotion
        -Wformat=2
        -Wimplicit-fallthrough
    )

    set(_gcc_warnings
        ${_clang_warnings}
        -Wmisleading-indentation
        -Wduplicated-cond
        -Wduplicated-branches
        -Wlogical-op
        -Wuseless-cast
    )

    # Warnings-as-errors flag
    if(ARG_WARNINGS_AS_ERRORS)
        list(APPEND _msvc_warnings /WX)
        list(APPEND _clang_warnings -Werror)
        list(APPEND _gcc_warnings -Werror)
    endif()

    # Apply per compiler ID
    if(MSVC)
        set(_warnings ${_msvc_warnings})
    elseif(CMAKE_CXX_COMPILER_ID MATCHES ".*Clang")
        set(_warnings ${_clang_warnings})
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        set(_warnings ${_gcc_warnings})
    else()
        message(AUTHOR_WARNING "[obsidian] Unknown compiler '${CMAKE_CXX_COMPILER_ID}' — no warning flags set.")
        return()
    endif()

    target_compile_options(${_target} PRIVATE ${_warnings})
endfunction()
