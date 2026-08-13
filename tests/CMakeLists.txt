add_executable(BackItUpToolTests
    backup_config_tests.cpp
    backup_engine_tests.cpp
    config_store_tests.cpp
    ignore_rules_tests.cpp
    projects_scanner_tests.cpp
    source_watcher_tests.cpp
    state_store_tests.cpp
)
target_link_libraries(BackItUpToolTests PRIVATE
    Catch2::Catch2WithMain
    back_it_up_tool_core
)

include(Catch)
catch_discover_tests(BackItUpToolTests)
