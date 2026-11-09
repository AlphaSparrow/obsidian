# Obsidian Runtime Subsystem

add_library(obsidian_runtime STATIC
    event_bus/event.hpp
    event_bus/event_bus.hpp
    event_bus/event_bus.cpp
    registry/component_lifecycle.hpp
    registry/service_registry.hpp
    registry/service_registry.cpp
    dependency_graph/task_node.hpp
    dependency_graph/dependency_graph.hpp
    dependency_graph/dependency_graph.cpp
    scheduler/task_scheduler.hpp
    scheduler/task_scheduler.cpp
    pipeline/pipeline_stage.hpp
    pipeline/execution_pipeline.hpp
    pipeline/execution_pipeline.cpp
    plugins/plugin_interface.hpp
    plugins/plugin_manifest.hpp
    plugins/plugin_manager.hpp
    plugins/plugin_manager.cpp
    orchestration/runtime_engine.hpp
    orchestration/runtime_engine.cpp
)

target_include_directories(obsidian_runtime
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include/obsidian/runtime>
)

target_link_libraries(obsidian_runtime
    PUBLIC
        obsidian_kernel
        obsidian_storage
        obsidian::tuning
    PRIVATE
        obsidian::warnings
        obsidian::sanitizers
)
