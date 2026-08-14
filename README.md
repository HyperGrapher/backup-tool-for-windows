# BackItUpTool

A lightweight Windows tray application for configuring and running personal file backups.

## Included

- Resizable dark-only FLTK window with flat controls and an icon sidebar for Backup status, Sources, Projects, Destinations, Activity, and Settings.
- Working Manual Sources page with search, bulk removal, and native Windows pickers that can add many files or many folders at once.
- Working Destinations page that detects connected removable drives, tracks them by volume serial number, shows availability and free space, and also accepts folder destinations.
- Manual Sources can be connected to or disconnected from a selected Destination in bulk; new routes enable Mirror and Snapshots by default.
- `Run now` performs configured Mirrors sequentially in the background. Mirrors preserve their original path under `<destination>\BackItUpTool\Mirrors`, such as `C\Users\name\Documents`, and reject any Source/Destination overlap.
- Manual Sources are watched automatically. Changes are debounced, saved as pending work in SQLite, mirrored when the Destination is available, and remain queued across app restarts or removable-drive disconnections.
- Project Roots can be added and connected from the Projects page. Immediate child folders opt in with `.backup-watch`; the app gives empty markers a stable UUID and uses `ReadDirectoryChangesW` to discover new opted-in folders when the Root changes.
- Project backups exclude `.git` repositories, `build`, `node_modules`, junctions, `.backup-watch`, `.backup-ignore`, and paths matched by `.backup-ignore`. Hidden loose files remain included.
- Before a Project backup, an always-on-top approval window lists any eligible file over 50 MiB or Project total over 150 MiB. You can approve once, permanently allow that Project, or skip it. A skipped Project stays pending without asking again until that Project changes or you select `Run now`.
- Each changed Source also receives a ZIP Snapshot after its Mirror succeeds. Snapshots are stored below `<destination>\BackItUpTool\Snapshots` using the original readable path, then kept daily for 30 days and monthly for 12 months by default.
- The Backup status page shows Destination availability, pending Mirrors, the most recent successful Mirror and Snapshot, automatic-watching status, and recent failures.
- The Activity page shows recent backup work and errors. Settings can change the watcher settle delay and large-Project warning limits without editing `config.json` by hand.
- The always-visible status bar provides `Run now` and Pause for 1, 3, or 5 hours. Routine success stays quiet; failures and unavailable Destinations remain visible.
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
