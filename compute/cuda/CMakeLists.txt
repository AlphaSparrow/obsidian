# compute/cuda/CMakeLists.txt
# ---------------------------------------------------------------------------
# Obsidian CUDA compute backend.
#
# Builds GPU kernels as a static library that the C++ runtime links against.
#
# Supported GPU architectures:
#   70 — Volta      (V100)
#   75 — Turing     (T4, RTX 2000 series)
#   80 — Ampere     (A100, RTX 3000 series)
#   86 — Ampere     (RTX 3090, A6000)
#   89 — Ada        (L40, RTX 4000 series)
#   90 — Hopper     (H100)
#
# To target a specific GPU only:
#   cmake -DCMAKE_CUDA_ARCHITECTURES=86 ..
# ---------------------------------------------------------------------------

# GPU architecture targets — sensible defaults covering datacenter + consumer
if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
    set(CMAKE_CUDA_ARCHITECTURES "70;75;80;86;89;90")
endif()

# CUDA standard — match the host C++ standard
set(CMAKE_CUDA_STANDARD ${CMAKE_CXX_STANDARD})
set(CMAKE_CUDA_STANDARD_REQUIRED ON)

# Separable compilation — required for device-side linking across TUs
set(CMAKE_CUDA_SEPARABLE_COMPILATION ON)

# ── Target definition ────────────────────────────────────────────────────────
# No source files yet — this is the build scaffolding.
# When .cu files are added, list them here.

add_library(obsidian_compute_cuda STATIC)

# Placeholder: uncomment and add .cu files as they are created
# target_sources(obsidian_compute_cuda PRIVATE
#     kernels/matrix_ops.cu
#     kernels/risk_monte_carlo.cu
# )

target_include_directories(obsidian_compute_cuda
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include/obsidian/compute/cuda>
)

# Host-side code can call into this library
target_link_libraries(obsidian_compute_cuda
    PUBLIC CUDA::cudart
)

# Compile flags for nvcc
target_compile_options(obsidian_compute_cuda PRIVATE
    $<$<COMPILE_LANGUAGE:CUDA>:
        --expt-relaxed-constexpr    # allow constexpr in device code
        --extended-lambda           # allow __device__ lambdas
        -lineinfo                   # embed line info for profiling
    >
)
