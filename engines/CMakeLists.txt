# Obsidian Engines Layer
add_library(obsidian_strategy STATIC
    strategy/cpt_breakout_strategy.cpp
    strategy/cpt_breakout_strategy.hpp
    strategy/pair_trading_strategy.cpp
    strategy/pair_trading_strategy.hpp
    strategy/strategy_base.hpp
)

target_include_directories(obsidian_strategy
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(obsidian_strategy
    PUBLIC
        obsidian_simd
        obsidian_storage
        obsidian_kernel
        obsidian::tuning
    PRIVATE
        obsidian::warnings
)

message(STATUS "[obsidian/engines] Strategy engine library configured")
