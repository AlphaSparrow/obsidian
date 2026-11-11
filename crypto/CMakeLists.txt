# Obsidian Crypto Market Ingestion & Telemetry Layer
add_library(obsidian_crypto STATIC
    feed_monitor.cpp
    feed_monitor.hpp
)

target_include_directories(obsidian_crypto
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(obsidian_crypto
    PUBLIC
        obsidian_storage
        obsidian_kernel
        obsidian::tuning
    PRIVATE
        obsidian::warnings
)

message(STATUS "[obsidian/crypto] Crypto feed telemetry library configured")

