# Compiler warning policies
include_guard(GLOBAL)

# MSVC warning configuration
set(OBSIDIAN_MSVC_WARNINGS
    /W4
    /permissive-          # Standards conformance
    /utf-8                # UTF-8 source and execution character set
    /Zc:__cplusplus       # Proper __cplusplus macro
    /w14242               # Narrowing conversion
    /w14254               # Operator conversion
    /w14263               # Member function does not override
    /w14265               # Virtual functions without virtual dtor
    /w14287               # Unsigned/negative constant mismatch
    /w14296               # Expression is always true/false
    /w14311               # Pointer truncation
    /w14545               # Expression before comma evaluates to function
    /w14546               # Function call before comma missing arg list
    /w14547               # Operator before comma has no effect
    /w14549               # Operator before comma has no effect
    /w14555               # Expression has no effect
    /w14619               # Pragma warning: unknown warning number
    /w14640               # Thread-unsafe static member init
    /w14826               # Narrowing conversion
    /w14905               # Wide string literal cast
    /w14906               # String literal cast
    /w14928               # Illegal copy-init
)

# Clang warning configuration
set(OBSIDIAN_CLANG_WARNINGS
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
)

if(CMAKE_CXX_COMPILER_ID MATCHES ".*Clang")
    list(APPEND OBSIDIAN_CLANG_WARNINGS -Wimplicit-fallthrough)
endif()

# GCC warning configuration
set(OBSIDIAN_GCC_WARNINGS
    ${OBSIDIAN_CLANG_WARNINGS}
    -Wmisleading-indentation
    -Wlogical-op
    -Wuseless-cast
)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL "7.0")
    list(APPEND OBSIDIAN_GCC_WARNINGS
        -Wduplicated-cond
        -Wduplicated-branches
        -Wimplicit-fallthrough
    )
endif()

# Warnings interface target
add_library(obsidian_warnings INTERFACE)
add_library(obsidian::warnings ALIAS obsidian_warnings)

if(MSVC)
    target_compile_options(obsidian_warnings INTERFACE ${OBSIDIAN_MSVC_WARNINGS})
elseif(CMAKE_CXX_COMPILER_ID MATCHES ".*Clang")
    target_compile_options(obsidian_warnings INTERFACE ${OBSIDIAN_CLANG_WARNINGS})
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(obsidian_warnings INTERFACE ${OBSIDIAN_GCC_WARNINGS})
endif()

# Helper function to configure target warnings
function(obsidian_set_warnings _target)
    cmake_parse_arguments(ARG "" "WARNINGS_AS_ERRORS" "" ${ARGN})
    if(NOT DEFINED ARG_WARNINGS_AS_ERRORS)
        set(ARG_WARNINGS_AS_ERRORS ON)
    endif()

    target_link_libraries(${_target} PRIVATE obsidian::warnings)

    if(ARG_WARNINGS_AS_ERRORS)
        if(MSVC)
            target_compile_options(${_target} PRIVATE /WX)
        else()
            target_compile_options(${_target} PRIVATE -Werror)
        endif()
    endif()
endfunction()
