# Obsidian Quantitative Finance Library
add_library(obsidian_quant STATIC
    volatility_model.cpp
    volatility_model.hpp
    risk_engine.cpp
    risk_engine.hpp
    portfolio_allocator.cpp
    portfolio_allocator.hpp
)

target_include_directories(obsidian_quant
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../..>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(obsidian_quant
    PUBLIC
        obsidian_storage
        obsidian_kernel
        obsidian::tuning
    PRIVATE
        obsidian::warnings
)

message(STATUS "[obsidian/compute] Quantitative models library configured")
