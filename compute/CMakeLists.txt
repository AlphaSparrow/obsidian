# compute/CMakeLists.txt
# ---------------------------------------------------------------------------
# Obsidian Compute Layer — optional high-performance compute backends.
#
# Each subdirectory is conditionally included based on feature toggles
# set in the root CMakeLists.txt. Missing SDKs gracefully disable
# themselves in Dependencies.cmake.
# ---------------------------------------------------------------------------

# ── CUDA backend ─────────────────────────────────────────────────────────────

if(OBSIDIAN_ENABLE_CUDA)
    add_subdirectory(cuda)
    message(STATUS "[obsidian/compute] CUDA backend enabled")
else()
    message(STATUS "[obsidian/compute] CUDA backend disabled (OBSIDIAN_ENABLE_CUDA=OFF)")
endif()

# ── SIMD dispatch library ───────────────────────────────────────────────────
# Always built — SIMD intrinsics don't require external SDKs.

add_subdirectory(simd)

# ── MPI distributed compute ─────────────────────────────────────────────────

if(OBSIDIAN_ENABLE_MPI)
    # add_subdirectory(mpi)  — stub: MPI sources not yet present
    message(STATUS "[obsidian/compute] MPI backend enabled (sources pending)")
else()
    message(STATUS "[obsidian/compute] MPI backend disabled (OBSIDIAN_ENABLE_MPI=OFF)")
endif()

# ── gRPC service layer ──────────────────────────────────────────────────────

if(OBSIDIAN_ENABLE_GRPC)
    # add_subdirectory(grpc)  — stub: proto files and service impls not yet present
    message(STATUS "[obsidian/compute] gRPC layer enabled (sources pending)")
else()
    message(STATUS "[obsidian/compute] gRPC layer disabled (OBSIDIAN_ENABLE_GRPC=OFF)")
endif()

# ── Ray / Dask / Workers ────────────────────────────────────────────────────
# These are Python-ecosystem compute backends.  They do not require CMake
# build targets — they are orchestrated via Python entry points.
# Build system hooks for them will live in a future pyproject.toml.
