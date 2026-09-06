# BackItUpTool

A lightweight Windows tray application for configuring and running personal file backups.

## Included

- Resizable dark-only FLTK window with flat controls and an icon sidebar for Backup status, Sources, Projects, Destinations, Activity, and Settings.
- Working Manual Sources page with search, bulk removal, native Windows pickers, and a required Mirror or Zipped choice when Sources are added.
- Working Destinations page that detects removable-drive arrival and removal through Windows device notifications, tracks drives by volume serial number, shows availability and free space, and also accepts folder destinations. USB detection does not poll.
- Every Manual Source and Projects Root is backed up automatically to every folder and USB Destination configured on the Destinations page.
- `Run now` immediately processes pending changed Sources and opted-in Projects for every currently available Destination, including folder destinations when no USB drive is connected. It does not create another ZIP when nothing changed. Mirrors preserve their original path under `<destination>\BackItUpTool\Mirrors`, such as `C\Users\name\Documents`, and reject any Source/Destination overlap.
- Manual Sources are watched automatically. Changes are debounced, saved as pending work in SQLite, mirrored when the Destination is available, and remain queued across app restarts or removable-drive disconnections.
- Project Roots can be added from the Projects page with a required Mirror or Zipped choice. Folders at any depth opt in with `.backup-watch`; the app gives empty markers a stable UUID and uses recursive `ReadDirectoryChangesW` notifications to discover changes without polling.
- Project backups exclude `.git` repositories, `build`, `node_modules`, junctions, `.backup-watch`, `.backup-ignore`, and paths matched by `.backup-ignore`. Hidden loose files remain included.
- The first time a Project contains a file above the configured limit, an always-on-top approval window asks whether to always include large files or permanently ignore those large files. The Project itself remains watched and backed up, and the stored decision prevents repeated prompts for that Project.
- Mirror and Zipped are mutually exclusive per Source or Projects Root. Zipped backups read directly from the source, use ZIP compression level 9, are stored below `<destination>\BackItUpTool\Zipped`, and retain daily and monthly history.
- The Backup status page shows Destination availability, pending backups, the most recent successful backup and Zipped backup, automatic-watching status, and recent failures.
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
