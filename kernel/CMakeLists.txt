# kernel/CMakeLists.txt
# ---------------------------------------------------------------------------
# Obsidian Kernel — core low-level subsystems.
#
# Builds:
#   obsidian_kernel_memory      (OBJECT) — allocators, pools, ring buffers
#   obsidian_kernel_concurrency (OBJECT) — lock-free queues, thread pool, work stealing
#   obsidian_kernel_logging     (OBJECT) — structured logging
#   obsidian_kernel             (STATIC) — aggregate library linking all of the above
#
# The OBJECT libraries allow downstream consumers to cherry-pick subsystems
# without pulling the entire kernel.
# ---------------------------------------------------------------------------

# ── Memory subsystem ─────────────────────────────────────────────────────────

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

obsidian_set_warnings(obsidian_kernel_memory WARNINGS_AS_ERRORS OFF)

# ── Concurrency subsystem ───────────────────────────────────────────────────

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
    PUBLIC Threads::Threads
)

obsidian_set_warnings(obsidian_kernel_concurrency WARNINGS_AS_ERRORS OFF)

# ── Rust actor runtime (FFI) ────────────────────────────────────────────────
# The Rust actor_runtime crate produces a C-ABI static library.
# Corrosion imports it as a CMake target so C++ can link against it.

if(OBSIDIAN_ENABLE_RUST)
    obsidian_require(Corrosion)
    corrosion_import_crate(MANIFEST_PATH concurrency/Cargo.toml)

    # The crate name in Cargo.toml must match this target name.
    # After corrosion_import_crate(), 'obsidian_actor_runtime' becomes a
    # valid CMake target that produces a .a / .lib.
endif()

# ── Go channels bridge ──────────────────────────────────────────────────────
# concurrency/channels.go is compiled via cgo into a C-ABI archive.
# This is a future hook — Go build integration is not in scope for this phase.
# When implemented, it will use ExternalProject_Add with `go build -buildmode=c-archive`.

# ── Logging subsystem ───────────────────────────────────────────────────────

add_library(obsidian_kernel_logging OBJECT
    logging/formatter.cpp
    logging/logger.cpp
)

target_include_directories(obsidian_kernel_logging
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include/obsidian/kernel>
)

obsidian_set_warnings(obsidian_kernel_logging WARNINGS_AS_ERRORS OFF)

# ── Aggregate kernel library ────────────────────────────────────────────────

add_library(obsidian_kernel STATIC)

target_link_libraries(obsidian_kernel
    PUBLIC
        obsidian_kernel_memory
        obsidian_kernel_concurrency
        obsidian_kernel_logging
)

if(OBSIDIAN_ENABLE_RUST)
    target_link_libraries(obsidian_kernel PUBLIC obsidian_actor_runtime)
endif()

# FFI export header — manually generated for maximum compiler compatibility.
# GenerateExportHeader's internal test-compiles inherit CMAKE_CXX_STANDARD,
# which breaks on older compilers. For static libraries the macros are no-ops
# anyway; they become meaningful when building shared (.so/.dll) targets.
set(_export_header "${CMAKE_CURRENT_BINARY_DIR}/obsidian_kernel_export.h")
if(NOT EXISTS "${_export_header}")
    file(WRITE "${_export_header}"
"#ifndef OBSIDIAN_KERNEL_EXPORT_H
#define OBSIDIAN_KERNEL_EXPORT_H

#ifdef OBSIDIAN_KERNEL_STATIC_DEFINE
#  define OBSIDIAN_KERNEL_EXPORT
#  define OBSIDIAN_KERNEL_NO_EXPORT
#else
#  ifdef _WIN32
#    ifdef obsidian_kernel_EXPORTS
#      define OBSIDIAN_KERNEL_EXPORT __declspec(dllexport)
#    else
#      define OBSIDIAN_KERNEL_EXPORT __declspec(dllimport)
#    endif
#    define OBSIDIAN_KERNEL_NO_EXPORT
#  else
#    define OBSIDIAN_KERNEL_EXPORT __attribute__((visibility(\"default\")))
#    define OBSIDIAN_KERNEL_NO_EXPORT __attribute__((visibility(\"hidden\")))
#  endif
#endif

/* Deprecation macro */
#ifndef OBSIDIAN_KERNEL_DEPRECATED
#  if defined(__GNUC__) || defined(__clang__)
#    define OBSIDIAN_KERNEL_DEPRECATED __attribute__((deprecated))
#  elif defined(_MSC_VER)
#    define OBSIDIAN_KERNEL_DEPRECATED __declspec(deprecated)
#  else
#    define OBSIDIAN_KERNEL_DEPRECATED
#  endif
#endif

#endif /* OBSIDIAN_KERNEL_EXPORT_H */
")
endif()

target_include_directories(obsidian_kernel
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>  # for the generated export header
)
