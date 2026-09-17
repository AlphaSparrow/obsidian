# Obsidian terminal desktop target
option(OBSIDIAN_GUI_APP "Build desktop application without persistent console window" ON)

set(_terminal_sources
    desktop/main.cpp
    desktop/app.hpp
    desktop/app.cpp
)

# Windows resources
if(WIN32)
    set(_rc_file "${CMAKE_CURRENT_BINARY_DIR}/obsidian.rc")
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
)

# Installation rules
include(GNUInstallDirs)

install(TARGETS obsidian
    BUNDLE  DESTINATION .                           COMPONENT runtime
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}      COMPONENT runtime
)
