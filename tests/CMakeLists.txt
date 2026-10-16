# Obsidian Test Suite

add_executable(obsidian_lakehouse_tests
    test_lakehouse.cpp
)

target_include_directories(obsidian_lakehouse_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(obsidian_lakehouse_tests
    PRIVATE
        obsidian_storage
        obsidian_kernel
        obsidian::tuning
        obsidian::warnings
        obsidian::sanitizers
)

enable_testing()
add_test(NAME LakehouseTests COMMAND obsidian_lakehouse_tests)

add_executable(obsidian_ddia_tests
    test_ddia_internals.cpp
)

target_include_directories(obsidian_ddia_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(obsidian_ddia_tests
    PRIVATE
        obsidian_storage
        obsidian_kernel
        obsidian::tuning
        obsidian::warnings
        obsidian::sanitizers
)

add_test(NAME DDIA_InternalsTests COMMAND obsidian_ddia_tests)
