add_executable(SystemTrayAppTemplateTests
    app_state_tests.cpp
    database_tests.cpp
)
target_link_libraries(SystemTrayAppTemplateTests PRIVATE
    Catch2::Catch2WithMain
    system_tray_app_core
)

include(Catch)
catch_discover_tests(SystemTrayAppTemplateTests)

