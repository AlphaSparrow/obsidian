# cmake/Install.cmake
# ---------------------------------------------------------------------------
# Install rules and CPack packaging configuration.
#
# Targets:
#   make install         — Install to CMAKE_INSTALL_PREFIX
#   cpack                — Generate platform packages (tar.gz, .deb, .rpm, .zip)
# ---------------------------------------------------------------------------

include_guard(GLOBAL)
include(GNUInstallDirs)

# ── Install targets ─────────────────────────────────────────────────────────

# Libraries
install(TARGETS
        obsidian_kernel
        obsidian_simd
    EXPORT ObsidianTargets
    ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME  DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# Headers
install(DIRECTORY kernel/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/obsidian/kernel
    FILES_MATCHING PATTERN "*.hpp" PATTERN "*.h"
)

install(DIRECTORY compute/simd/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/obsidian/compute/simd
    FILES_MATCHING PATTERN "*.hpp" PATTERN "*.h"
)

# CMake package config for find_package(Obsidian)
install(EXPORT ObsidianTargets
    FILE ObsidianTargets.cmake
    NAMESPACE Obsidian::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Obsidian
)

# ── CPack ────────────────────────────────────────────────────────────────────

set(CPACK_PACKAGE_NAME "obsidian")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "High-performance quantitative finance platform")
set(CPACK_PACKAGE_VENDOR "AlphaSparrow")
set(CPACK_PACKAGE_LICENSE "Apache-2.0")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")

# Platform-specific generators
if(OBSIDIAN_PLATFORM STREQUAL "linux")
    set(CPACK_GENERATOR "TGZ;DEB;RPM")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "AlphaSparrow")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6 (>= 2.31)")
    set(CPACK_RPM_PACKAGE_LICENSE "ASL 2.0")
elseif(OBSIDIAN_PLATFORM STREQUAL "darwin")
    set(CPACK_GENERATOR "TGZ;productbuild")
elseif(OBSIDIAN_PLATFORM STREQUAL "windows")
    set(CPACK_GENERATOR "ZIP;NSIS")
    set(CPACK_NSIS_DISPLAY_NAME "Obsidian")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
endif()

# Source package
set(CPACK_SOURCE_GENERATOR "TGZ")
set(CPACK_SOURCE_IGNORE_FILES
    "/build/"
    "/install/"
    "/\\.git/"
    "/\\.idea/"
    "/\\.vscode/"
    "/__pycache__/"
    "\\.pyc$"
    "/target/"     # Rust build output
)

include(CPack)
