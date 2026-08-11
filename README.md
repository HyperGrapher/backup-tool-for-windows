# BackItUpTool

A lightweight Windows tray application for configuring and running personal file backups.

## Included

- Resizable FLTK application window with a sidebar for the main sections.
- Working Manual Sources page with search, bulk removal, and native Windows pickers that can add many files or many folders at once.
- Working Destinations page that detects connected removable drives, tracks them by volume serial number, shows availability and free space, and also accepts folder destinations.
- Tray icon with left-click Open and right-click Open/Exit actions.
- Tray-only startup and hide-on-close behavior.
- Low-CPU message loop that continues to dispatch tray events while the FLTK window is hidden.
- Validated `config.json` under `%LOCALAPPDATA%\BackItUpTool`.
- SQLite operational state and history under `%LOCALAPPDATA%\BackItUpTool`.
- Rotating application log under `%LOCALAPPDATA%\BackItUpTool\logs` using spdlog.
- Catch2 unit tests.
- Static x64 MSVC build through a vcpkg manifest.

FLTK 1.4.5 is fetched directly from its GitHub release tag. SQLite, nlohmann-json, spdlog, and Catch2 are resolved through vcpkg.

## Build

Requirements:

- Windows 10 or 11
- Visual Studio 2022 with Desktop development with C++
- CMake 3.25 or newer
- vcpkg, with `VCPKG_ROOT` set

```powershell
cmake --preset windows-release
cmake --build --preset windows-release
ctest --preset windows-release
```

The executable is written to `build\Release\BackItUpTool.exe`.

## Run

The application starts in the notification area. Left-click the tray icon to open the window. Right-click it for the Open and Exit menu.
