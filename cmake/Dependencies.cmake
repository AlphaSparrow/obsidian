# Third-party dependency declarations
include_guard(GLOBAL)
include(FetchContent)

# C++ libraries
FetchContent_Declare(
    fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG        11.1.4
    GIT_SHALLOW    TRUE
    SYSTEM
)

FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.15.1
    GIT_SHALLOW    TRUE
    SYSTEM
)
set(SPDLOG_FMT_EXTERNAL ON CACHE BOOL "" FORCE)

FetchContent_Declare(
    abseil
    GIT_REPOSITORY https://github.com/abseil/abseil-cpp.git
    GIT_TAG        20240722.0
    GIT_SHALLOW    TRUE
    SYSTEM
)
set(ABSL_PROPAGATE_CXX_STD ON CACHE BOOL "" FORCE)

# Rust integration (Corrosion)
FetchContent_Declare(
    Corrosion
    GIT_REPOSITORY https://github.com/corrosion-rs/corrosion.git
    GIT_TAG        v0.5.1
    GIT_SHALLOW    TRUE
    SYSTEM
)

# Allocator configuration
set(OBSIDIAN_MALLOC_BACKEND "system" CACHE STRING "Memory allocator backend (system, mimalloc, jemalloc)")
set_property(CACHE OBSIDIAN_MALLOC_BACKEND PROPERTY STRINGS system mimalloc jemalloc)

add_library(obsidian_malloc INTERFACE)
add_library(obsidian::malloc ALIAS obsidian_malloc)

if(OBSIDIAN_MALLOC_BACKEND STREQUAL "mimalloc")
    FetchContent_Declare(
        mimalloc
        GIT_REPOSITORY https://github.com/microsoft/mimalloc.git
        GIT_TAG        v2.1.7
        GIT_SHALLOW    TRUE
        SYSTEM
    )
    set(MI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(MI_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(MI_BUILD_OBJECT OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(mimalloc)
    target_link_libraries(obsidian_malloc INTERFACE mimalloc-static)
    target_compile_definitions(obsidian_malloc INTERFACE OBSIDIAN_USE_MIMALLOC=1)
    message(STATUS "[obsidian/deps] Allocator: mimalloc")
elseif(OBSIDIAN_MALLOC_BACKEND STREQUAL "jemalloc")
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(JEMALLOC jemalloc)
    endif()
    if(JEMALLOC_FOUND)
        target_include_directories(obsidian_malloc INTERFACE ${JEMALLOC_INCLUDE_DIRS})
        target_link_libraries(obsidian_malloc INTERFACE ${JEMALLOC_LIBRARIES})
        target_compile_definitions(obsidian_malloc INTERFACE OBSIDIAN_USE_JEMALLOC=1)
        message(STATUS "[obsidian/deps] Allocator: jemalloc")
    else()
        message(WARNING "[obsidian/deps] jemalloc not found; falling back to system malloc")
    endif()
else()
    message(STATUS "[obsidian/deps] Allocator: system malloc")
endif()

# System SDKs
if(OBSIDIAN_ENABLE_CUDA)
    include(CheckLanguage)
    check_language(CUDA)
    if(CMAKE_CUDA_COMPILER)
        enable_language(CUDA)
        message(STATUS "[obsidian] CUDA compiler: ${CMAKE_CUDA_COMPILER}")
    else()
        message(WARNING "[obsidian] OBSIDIAN_ENABLE_CUDA=ON but no CUDA compiler found. Disabling.")
        set(OBSIDIAN_ENABLE_CUDA OFF CACHE BOOL "" FORCE)
    endif()
endif()

if(OBSIDIAN_ENABLE_MPI)
    find_package(MPI QUIET)
    if(NOT MPI_FOUND)
        message(WARNING "[obsidian] OBSIDIAN_ENABLE_MPI=ON but MPI not found. Disabling.")
        set(OBSIDIAN_ENABLE_MPI OFF CACHE BOOL "" FORCE)
    else()
        message(STATUS "[obsidian] MPI: ${MPI_CXX_COMPILER}")
    endif()
endif()

if(OBSIDIAN_ENABLE_GRPC)
    find_package(Protobuf QUIET)
    find_package(gRPC QUIET)
    if(NOT Protobuf_FOUND OR NOT gRPC_FOUND)
        message(WARNING "[obsidian] OBSIDIAN_ENABLE_GRPC=ON but Protobuf/gRPC not found. Disabling.")
        set(OBSIDIAN_ENABLE_GRPC OFF CACHE BOOL "" FORCE)
    else()
        message(STATUS "[obsidian] Protobuf: ${Protobuf_VERSION}")
        message(STATUS "[obsidian] gRPC:     ${gRPC_VERSION}")
    endif()
endif()

# Helper: make available on demand
macro(obsidian_require)
    foreach(_dep ${ARGN})
        FetchContent_MakeAvailable(${_dep})
    endforeach()
endmacro()
