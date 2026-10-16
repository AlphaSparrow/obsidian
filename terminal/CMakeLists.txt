# Obsidian terminal desktop target
option(OBSIDIAN_GUI_APP "Build desktop application without persistent console window" ON)

if(NOT WIN32)
    message(STATUS "[obsidian/terminal] Desktop GUI terminal is Win32-only; skipping GUI target on ${CMAKE_SYSTEM_NAME}")
    return()
endif()

set(_terminal_sources
    desktop/main.cpp
    desktop/app.hpp
    desktop/app.cpp
)

# Windows resources
if(WIN32)
    set(_rc_file "${CMAKE_CURRENT_BINARY_DIR}/obsidian.rc")
    if(MSVC)
        # On MSVC, CMake vs_link_exe automatically invokes mt.exe to merge .manifest
        # source files into manifest.res. Embedding it in .rc causes CVT1100 duplicate resource.
        set(OBSIDIAN_RC_MANIFEST_DIRECTIVE "// Manifest merged and embedded by CMake via mt.exe")
        list(APPEND _terminal_sources "${CMAKE_CURRENT_SOURCE_DIR}/desktop/resources/obsidian.manifest")
    else()
        set(OBSIDIAN_RC_MANIFEST_DIRECTIVE "1 RT_MANIFEST \"${CMAKE_CURRENT_SOURCE_DIR}/desktop/resources/obsidian.manifest\"")
    endif()
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/desktop/resources/obsidian.rc.in"
        "${_rc_file}"
        @ONLY
    )
    list(APPEND _terminal_sources "${_rc_file}")
endif()

# Executable target
if(OBSIDIAN_GUI_APP)
    add_executable(obsidian WIN32 MACOSX_BUNDLE ${_terminal_sources})
else()
    add_executable(obsidian ${_terminal_sources})
    target_compile_definitions(obsidian PRIVATE _CONSOLE=1)
endif()

add_executable(obsidian::terminal ALIAS obsidian)

target_include_directories(obsidian
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/desktop
)

target_link_libraries(obsidian
    PRIVATE
        obsidian_kernel
        obsidian_simd
        obsidian::tuning
        obsidian::warnings
        obsidian::sanitizers
        obsidian::malloc
        $<$<PLATFORM_ID:Windows>:gdiplus>
        $<$<PLATFORM_ID:Windows>:dwmapi>
)

# Installation rules
include(GNUInstallDirs)

install(TARGETS obsidian
    BUNDLE  DESTINATION .                           COMPONENT runtime
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}      COMPONENT runtime
)
