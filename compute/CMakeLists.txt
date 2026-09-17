# Obsidian Compute Layer
# CUDA backend
if(OBSIDIAN_ENABLE_CUDA)
    add_subdirectory(cuda)
    message(STATUS "[obsidian/compute] CUDA backend enabled")
else()
    message(STATUS "[obsidian/compute] CUDA backend disabled")
endif()

# SIMD dispatch library
add_subdirectory(simd)

# MPI distributed compute
if(OBSIDIAN_ENABLE_MPI)
    message(STATUS "[obsidian/compute] MPI backend enabled (sources pending)")
else()
    message(STATUS "[obsidian/compute] MPI backend disabled")
endif()

# gRPC service layer
if(OBSIDIAN_ENABLE_GRPC)
    message(STATUS "[obsidian/compute] gRPC layer enabled (sources pending)")
else()
    message(STATUS "[obsidian/compute] gRPC layer disabled")
endif()
