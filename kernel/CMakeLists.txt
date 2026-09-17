# Obsidian Kernel core subsystems
# Memory subsystem
add_library(obsidian_kernel_memory OBJECT
    memory/allocator.hpp
    memory/arena_allocator.cpp
    memory/cache_aligned.cpp
    memory/mmap_storage.cpp
    memory/object_pool.cpp
    memory/ring_buffer.cpp
    memory/shared_memory.cpp
)

target_include_directories(obsidian_kernel_memory
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include/obsidian/kernel>
)

target_link_libraries(obsidian_kernel_memory
    PUBLIC
        obsidian::tuning
        obsidian::warnings
        obsidian::sanitizers
        obsidian::malloc
)

# Concurrency subsystem
add_library(obsidian_kernel_concurrency OBJECT
    concurrency/atomic_utils.hpp
    concurrency/lockfree_queue.cpp
    concurrency/task_scheduler.cpp
    concurrency/thread_pool.cpp
    concurrency/work_stealing.cpp
)

target_include_directories(obsidian_kernel_concurrency
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include/obsidian/kernel>
)

target_link_libraries(obsidian_kernel_concurrency
    PUBLIC
        Threads::Threads
        obsidian::tuning
        obsidian::warnings
        obsidian::sanitizers
        obsidian::malloc
)

# Rust actor runtime (FFI)
if(OBSIDIAN_ENABLE_RUST)
    obsidian_require(Corrosion)
    corrosion_import_crate(MANIFEST_PATH concurrency/Cargo.toml)
endif()

# Logging subsystem
add_library(obsidian_kernel_logging OBJECT
    logging/formatter.cpp
    logging/logger.cpp
)

target_include_directories(obsidian_kernel_logging
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include/obsidian/kernel>
)

target_link_libraries(obsidian_kernel_logging
    PUBLIC
        obsidian::tuning
        obsidian::warnings
        obsidian::sanitizers
)

# Aggregate kernel library
add_library(obsidian_kernel STATIC)

target_sources(obsidian_kernel PRIVATE
    $<TARGET_OBJECTS:obsidian_kernel_memory>
    $<TARGET_OBJECTS:obsidian_kernel_concurrency>
    $<TARGET_OBJECTS:obsidian_kernel_logging>
)

target_link_libraries(obsidian_kernel
    PUBLIC
        obsidian::tuning
        obsidian::malloc
    PRIVATE
        obsidian::warnings
        obsidian::sanitizers
)

if(OBSIDIAN_ENABLE_RUST)
    target_link_libraries(obsidian_kernel PUBLIC obsidian_actor_runtime)
endif()

# Precompiled headers
target_precompile_headers(obsidian_kernel_memory PRIVATE
    <atomic>
    <chrono>
    <cstdint>
    <memory>
    <vector>
    <string>
    <string_view>
    <type_traits>
)

target_precompile_headers(obsidian_kernel_concurrency REUSE_FROM obsidian_kernel_memory)
target_precompile_headers(obsidian_kernel_logging REUSE_FROM obsidian_kernel_memory)

# Export header generation
set(EXPORT_TARGET obsidian_kernel)
set(EXPORT_MACRO_PREFIX OBSIDIAN_KERNEL)
configure_file(
    "${CMAKE_SOURCE_DIR}/cmake/export_header.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/obsidian_kernel_export.h"
    @ONLY
)

target_compile_definitions(obsidian_kernel PUBLIC OBSIDIAN_KERNEL_STATIC_DEFINE)

target_include_directories(obsidian_kernel
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
)
