# cmake/StaticAnalysis.cmake
# ---------------------------------------------------------------------------
# Static analysis tool integration.
#
# Supported tools:
#   clang-tidy      — Clang-based linter (needs .clang-tidy config)
#   cppcheck        — Standalone static analyzer
#   iwyu            — Include-what-you-use (header dependency minimization)
#
# Usage:
#   cmake -DOBSIDIAN_ENABLE_CLANG_TIDY=ON ..
#   cmake -DOBSIDIAN_ENABLE_CPPCHECK=ON ..
#   cmake -DOBSIDIAN_ENABLE_IWYU=ON ..
# ---------------------------------------------------------------------------

include_guard(GLOBAL)

# ── clang-tidy ──────────────────────────────────────────────────────────────

option(OBSIDIAN_ENABLE_CLANG_TIDY "Run clang-tidy during compilation" OFF)

if(OBSIDIAN_ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXE NAMES clang-tidy clang-tidy-18 clang-tidy-17)
    if(CLANG_TIDY_EXE)
        # Use the project's .clang-tidy config file
        set(CMAKE_CXX_CLANG_TIDY
            "${CLANG_TIDY_EXE}"
            "--config-file=${CMAKE_SOURCE_DIR}/.clang-tidy"
            "--header-filter=${CMAKE_SOURCE_DIR}/(kernel|compute|storage|engines|runtime|terminal)/.*"
            "--warnings-as-errors=*"
        )
        message(STATUS "[obsidian/analysis] clang-tidy enabled: ${CLANG_TIDY_EXE}")
    else()
        message(WARNING "[obsidian/analysis] clang-tidy requested but not found")
    endif()
endif()

# ── cppcheck ────────────────────────────────────────────────────────────────

option(OBSIDIAN_ENABLE_CPPCHECK "Run cppcheck during compilation" OFF)

if(OBSIDIAN_ENABLE_CPPCHECK)
    find_program(CPPCHECK_EXE NAMES cppcheck)
    if(CPPCHECK_EXE)
        set(CMAKE_CXX_CPPCHECK
            "${CPPCHECK_EXE}"
            "--enable=warning,performance,portability"
            "--suppress=missingInclude"
            "--suppress=unmatchedSuppression"
            "--inline-suppr"
            "--inconclusive"
            "--std=c++20"
            "--quiet"
        )
        message(STATUS "[obsidian/analysis] cppcheck enabled: ${CPPCHECK_EXE}")
    else()
        message(WARNING "[obsidian/analysis] cppcheck requested but not found")
    endif()
endif()

# ── include-what-you-use ────────────────────────────────────────────────────

option(OBSIDIAN_ENABLE_IWYU "Run include-what-you-use during compilation" OFF)

if(OBSIDIAN_ENABLE_IWYU)
    find_program(IWYU_EXE NAMES include-what-you-use iwyu)
    if(IWYU_EXE)
        set(CMAKE_CXX_INCLUDE_WHAT_YOU_USE "${IWYU_EXE}")
        message(STATUS "[obsidian/analysis] IWYU enabled: ${IWYU_EXE}")
    else()
        message(WARNING "[obsidian/analysis] IWYU requested but not found")
    endif()
endif()
