cmake_minimum_required(VERSION 3.25)

project(BackItUpTool VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "" FORCE)
    add_compile_options(/Zc:__cplusplus)
endif()

include(FetchContent)
include(CTest)

find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

if(BUILD_TESTING)
    find_package(Catch2 3 CONFIG REQUIRED)
endif()

set(FLTK_BUILD_TEST OFF CACHE BOOL "" FORCE)
set(FLTK_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(FLTK_BUILD_FLUID OFF CACHE BOOL "" FORCE)
set(FLTK_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(FLTK_MSVC_RUNTIME_DLL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    fltk
    GIT_REPOSITORY https://github.com/fltk/fltk.git
    GIT_TAG release-1.4.5
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(fltk)

add_library(back_it_up_tool_core STATIC
    src/activity_panel.cpp
    src/backup_config.cpp
    src/backup_engine.cpp
    src/config_store.cpp
    src/connected_volume.cpp
    src/destinations_panel.cpp
    src/ignore_rules.cpp
    src/native_file_dialog.cpp
    src/overview_panel.cpp
    src/projects_panel.cpp
    src/projects_scanner.cpp
    src/settings_panel.cpp
    src/sources_panel.cpp
    src/source_watcher.cpp
    src/state_store.cpp
    src/size_approval_dialog.cpp
    src/window_theme.cpp
)
target_include_directories(back_it_up_tool_core PUBLIC src)
target_link_libraries(back_it_up_tool_core PUBLIC
    fltk::fltk
    unofficial::sqlite3::sqlite3
    nlohmann_json::nlohmann_json
    dwmapi
)

add_executable(BackItUpTool WIN32
    src/main.cpp
    src/app.rc
)
target_link_libraries(BackItUpTool PRIVATE
    back_it_up_tool_core
    spdlog::spdlog
    comctl32
    shell32
    user32
    gdi32
    ole32
)

if(MSVC)
    target_compile_options(back_it_up_tool_core PRIVATE /W4 /permissive- /utf-8)
    target_compile_options(BackItUpTool PRIVATE /W4 /permissive- /utf-8)
endif()

set_target_properties(BackItUpTool PROPERTIES
    OUTPUT_NAME "BackItUpTool"
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:BackItUpTool>"
)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
