# Obsidian Storage Subsystem

add_library(obsidian_storage STATIC
    common/file_system.hpp
    common/file_system.cpp
    common/status.hpp
    schema/market_data_schema.hpp
    arrow/columnar_batch.hpp
    lakehouse/lakehouse_structures.hpp
    lakehouse/partition_spec.hpp
    lakehouse/manifest_manager.hpp
    lakehouse/manifest_manager.cpp
    lakehouse/transaction.hpp
    lakehouse/transaction.cpp
    lakehouse/lakehouse_table.hpp
    lakehouse/lakehouse_table.cpp
    lakehouse/compaction_engine.hpp
    lakehouse/compaction_engine.cpp
    lakehouse/catalog.hpp
    lakehouse/catalog.cpp
)

target_include_directories(obsidian_storage
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include/obsidian/storage>
)

target_link_libraries(obsidian_storage
    PUBLIC
        obsidian_kernel
        obsidian::tuning
    PRIVATE
        obsidian::warnings
        obsidian::sanitizers
)
