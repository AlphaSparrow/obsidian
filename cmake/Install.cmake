# Installation and packaging rules
include_guard(GLOBAL)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# RPATH configuration
set(CMAKE_SKIP_BUILD_RPATH FALSE)
set(CMAKE_BUILD_WITH_INSTALL_RPATH FALSE)
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

if(APPLE)
    set(CMAKE_INSTALL_RPATH "@loader_path/../${CMAKE_INSTALL_LIBDIR};@loader_path")
elseif(UNIX)
    set(CMAKE_INSTALL_RPATH "\$ORIGIN/../${CMAKE_INSTALL_LIBDIR}:\$ORIGIN")
endif()

# Target installation
install(TARGETS
        obsidian_kernel
        obsidian_simd
        obsidian_tuning
        obsidian_warnings
        obsidian_sanitizers
        obsidian_malloc
    EXPORT ObsidianTargets
    ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}     COMPONENT development
    LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}     COMPONENT development
    RUNTIME  DESTINATION ${CMAKE_INSTALL_BINDIR}     COMPONENT runtime
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# Public headers installation
install(DIRECTORY kernel/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/obsidian/kernel
    COMPONENT development
    FILES_MATCHING PATTERN "*.hpp" PATTERN "*.h"
)

install(DIRECTORY compute/simd/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/obsidian/compute/simd
    COMPONENT development
    FILES_MATCHING PATTERN "*.hpp" PATTERN "*.h"
)

install(FILES
    "${PROJECT_BINARY_DIR}/kernel/obsidian_kernel_export.h"
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/obsidian/kernel
    COMPONENT development
    OPTIONAL
)

# CMake export configuration
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/ObsidianConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(EXPORT ObsidianTargets
    FILE ObsidianTargets.cmake
    NAMESPACE Obsidian::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Obsidian
    COMPONENT development
)

file(GENERATE
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/ObsidianConfig.cmake"
    CONTENT "include(CMakeFindDependencyMacro)\ninclude(\"\${CMAKE_CURRENT_LIST_DIR}/ObsidianTargets.cmake\")\n"
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/ObsidianConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/ObsidianConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Obsidian
    COMPONENT development
)

# CPack packaging
set(CPACK_PACKAGE_NAME "obsidian")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "High-performance quantitative finance platform")
set(CPACK_PACKAGE_VENDOR "AlphaSparrow")
set(CPACK_PACKAGE_LICENSE "Apache-2.0")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")

# Parallel archive compression
set(CPACK_ARCHIVE_THREADS 0)

if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CPACK_STRIP_FILES TRUE)
endif()

set(CPACK_COMPONENTS_ALL runtime development)

set(CPACK_COMPONENT_RUNTIME_DISPLAY_NAME "Obsidian Application & Runtime")
set(CPACK_COMPONENT_RUNTIME_DESCRIPTION  "Desktop terminal binary and core runtime engine.")
set(CPACK_COMPONENT_RUNTIME_REQUIRED     ON)

set(CPACK_COMPONENT_DEVELOPMENT_DISPLAY_NAME "Obsidian C++ SDK")
set(CPACK_COMPONENT_DEVELOPMENT_DESCRIPTION  "Static libraries, exported headers, and CMake config modules.")
set(CPACK_COMPONENT_DEVELOPMENT_DEPENDS      runtime)

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

set(CPACK_SOURCE_GENERATOR "TGZ")
set(CPACK_SOURCE_IGNORE_FILES
    "/build/"
    "/install/"
    "/\\.git/"
    "/\\.idea/"
    "/\\.vscode/"
    "/\\.vs/"
    "/\\.cache/"
    "CMakeFiles"
    "CMakeCache\\.txt"
    "\\.cmake_install\\.cmake"
    "/__pycache__/"
    "\\.pyc$"
    "/target/"
    "\\.DS_Store$"
    "\\.o$"
    "\\.obj$"
    "\\.a$"
    "\\.lib$"
    "\\.so$"
    "\\.dylib$"
    "\\.dll$"
    "\\.pdb$"
    "\\.suo$"
    "\\.user$"
)

include(CPack)
