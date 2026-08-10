add_executable(WindowsBackupToolTests
    app_state_tests.cpp
    database_tests.cpp
)
target_link_libraries(WindowsBackupToolTests PRIVATE
    Catch2::Catch2WithMain
    windows_backup_tool_core
)

include(Catch)
catch_discover_tests(WindowsBackupToolTests)

