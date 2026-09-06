# BackupTray — Product Requirements & Implementation Plan

## 1. Summary

A Windows background tray application (C++/FLTK) that watches user-specified files and folders for changes and incrementally backs them up to one or more destinations (removable flash drive, another local folder, or a network path). It supports two independent source-discovery mechanisms — manually added paths via the GUI, and an auto-discovering mode for a "Projects" parent folder that intelligently skips git-managed codebases and backs up only the surrounding project artifacts. Backups run as a live `robocopy`-based mirror plus a periodic dated zip snapshot for basic version history.

## 2. Goals

- Zero-babysitting background operation: runs on login, sits in the tray, requires no manual intervention for day-to-day use.
- Near-real-time protection: changes to watched sources should reach the destination(s) within seconds of the watcher going idle, not on a fixed slow schedule.
- Safe handling of removable/network destinations that may be disconnected at any time — no data loss, no crash, no silent permanent skip.
- Smart, low-maintenance handling of the Projects folder use case without requiring per-project configuration in the common case.
- Lightweight footprint — single process, no installed service, no elevated privileges required.

## 3. Non-Goals (v1)

- Not a full versioned backup system (no per-file history browsing, no restore UI beyond "open the snapshot zip").
- Not a sync tool (one-directional: source → destination only; destination is never the source of truth).
- No cloud destinations in v1 (S3, OneDrive, etc.) — local paths and removable drives only. Architecture should not preclude adding this later.
- No cross-machine coordination / multiple watchers writing to the same destination.

## 4. Users & Use Cases

- **Primary user (Burak):** single-user, single-machine. Needs to protect (a) arbitrary files/folders added ad hoc via GUI, and (b) a large `Projects/` directory containing many project folders, each of which may contain one or more git-managed codebases plus non-git project artifacts (design files, notes, assets) that need protecting but are not covered by git/GitHub.

## 5. Architecture Overview

Single process, no elevation required, starts via Startup-folder shortcut on login.

```
┌─────────────────────────────────────────────────────────┐
│                      BackupTray.exe                      │
│                                                           │
│  ┌───────────────┐   ┌──────────────────┐                │
│  │  FLTK Tray UI  │   │  Config Manager   │               │
│  │ (menu, dialogs)│◄─►│ (config.json I/O) │               │
│  └───────┬───────┘   └──────────────────┘                │
│          │                                                │
│  ┌───────▼────────────────────────────────────────────┐  │
│  │              Source Registry                        │  │
│  │  - Manual sources (static, user-managed)             │  │
│  │  - Project sources (dynamic, updated by folder events)│  │
│  └───────┬───────────────────────────────────────────┘  │
│          │                                                │
│  ┌───────▼───────┐   ┌─────────────────┐                 │
│  │ Change Watcher │   │ Dirty-Flag Store │                │
│  │ (ReadDirectory-│──►│  (persisted JSON)│                │
│  │  ChangesW +    │   └────────┬────────┘                 │
│  │  debounce)     │            │                          │
│  └────────────────┘   ┌────────▼────────┐                │
│                        │  Backup Runner  │                │
│  ┌────────────────┐    │ - robocopy /MIR │                │
│  │ Device Watcher │───►│   per dirty pair│                │
│  │ (WM_DEVICECHANGE│   │ - snapshot timer│                │
│  │  / RegisterDevice│   │   (zip)         │                │
│  │  Notification) │    └─────────────────┘                │
│  └────────────────┘                                       │
└─────────────────────────────────────────────────────────┘
```

## 6. Functional Requirements

### 6.1 Source Management — Manual (GUI)

- User can add a file or folder as a watch source via a native folder/file picker.
- Every source is backed up to every configured destination automatically.
- User can remove a source; removal stops watching but does not delete existing backup data.
- Sources list shown in a management window: path, type (file/folder), assigned destinations, last sync time, current status (synced / pending / dirty / error).

### 6.2 Source Management — Projects Auto-Discovery

- User can configure one or more Projects Root paths.
- The app discovers opted-in folders recursively when a Root is added and when `ReadDirectoryChangesW` reports marker or Git-boundary changes. It does not poll Projects Roots.
- A folder at any depth is included as a watched project only if it contains a `.backup-watch` marker file (empty file, presence = opt-in).
- Within an opted-in project root, any immediate subfolder containing a `.git` directory is treated as a codebase and excluded automatically — not watched, not backed up.
- An optional `.backup-ignore` file (gitignore pattern syntax) inside a project root can exclude additional files/folders the git-detection heuristic doesn't catch (e.g., a large renders/cache folder).
- Re-scan must correctly handle: a project folder's marker being removed (stop watching, do not delete prior backups), a subfolder becoming a git repo mid-session (git init) (exclude it going forward), and new project folders appearing.

### 6.3 Change Detection

- One recursive `ReadDirectoryChangesW` watch handle per top-level watched root (manual root, or auto-discovered non-git subtree within a project).
- Events are debounced per `(source root, destination)` pair: a timer resets on each new event; after N seconds (default 8s, configurable) of quiet, the pair is marked dirty and queued for sync.
- Debouncing must not miss changes that occur while a robocopy pass is already running for that pair — a change during an active sync re-dirties the pair for a follow-up pass.

### 6.4 Backup Engine — Live Mirror

- On a dirty `(source, destination)` pair, run: `robocopy /MIR /XJ /FFT /R:1 /W:2 /NFL /NDL /NP /LOG+:<logfile> <source> <destination>`.
- `/FFT` is required for FAT32/exFAT destination compatibility (coarser timestamp granularity than NTFS).
- `/XJ` excludes junctions/symlinks to avoid infinite loops or copying reparse points incorrectly.
- Exit code handling: robocopy uses a bitmask exit code where values 0–7 indicate success variants; only treat ≥8 as failure. Log and surface non-fatal mismatches (code 4+) distinctly from real errors.
- Sync runs sequentially per destination to avoid saturating a single removable drive; syncs to different destinations can run in parallel.

### 6.5 Backup Engine — Dated Snapshots

- Independent scheduled job per source root, default interval configurable (e.g., every 24h), reads from the **source**, not from the mirrored destination, to avoid slow round-trips through USB.
- Produces `Snapshots/<root-name>_<yyyy-MM-dd_HHmm>.zip` on each assigned destination.
- Retention policy: keep last N snapshots or last N days (configurable per destination or globally); prune oldest on each successful new snapshot.
- Snapshot job skips a destination that isn't currently connected; it does not queue (next scheduled run will pick it up naturally).

### 6.6 Destination Management

- Destination types: **Removable** (identified by volume serial number + label, not drive letter) and **Path** (fixed local folder or UNC path).
- Removable-drive detection uses `RegisterDeviceNotification` on a hidden message-only window listening for `DBT_DEVICEARRIVAL`/`DBT_DEVICEREMOVECOMPLETE`; on arrival, match by volume serial via `GetVolumeInformation` against configured destinations.
- Dirty flags for a `(source, destination)` pair persist to disk (`dirty-state.json`) so pending work survives an app restart, not just a drive reconnect.
- On destination reconnect, all persisted dirty pairs for that destination are flushed immediately.
- Tray/UI must clearly show per-destination connection state (connected / not connected / syncing / N pending).

### 6.7 Tray UI

- Tray icon reflects overall state (idle/synced, syncing, pending changes, error).
- Right-click menu: Open dashboard, Sync now (all), Pause/Resume watching, Open log folder, Exit.
- Dashboard window: sources list (6.1), destinations list with connection status, settings (Projects parent path, debounce interval, snapshot interval/retention).

### 6.8 Configuration

- Single `config.json` under `%APPDATA%\BackupTray\`.
- Schema (illustrative):
```json
{
  "manualSources": [
    { "path": "C:\\Users\\Burak\\Documents\\Notes", "destinations": ["dest-flash", "dest-nas"] }
  ],
  "projectsParent": "D:\\Projects",
  "destinations": [
    { "id": "dest-flash", "kind": "removable", "volumeSerial": "1A2B-3C4D", "label": "BACKUP", "root": "\\Backups" },
    { "id": "dest-nas",   "kind": "path", "path": "\\\\NAS\\backups" }
  ],
  "settings": {
    "debounceSeconds": 8,
    "snapshotIntervalHours": 24,
    "snapshotRetentionDays": 30
  }
}
```

## 7. Non-Functional Requirements

- No admin/elevated privileges required for normal operation.
- Must not lock or hold open files it is not actively copying (avoid interfering with the user's normal editing).
- CPU/idle-friendly: no polling loops where an event-driven Windows API exists (`ReadDirectoryChangesW`, `RegisterDeviceNotification`).
- Logging: rolling log file per destination/sync-log, human-readable, capped size with rotation.

## 8. Open Questions / Future Considerations

- Should snapshot zips be encrypted/password-protected given they may leave the machine on a flash drive?
- Multi-machine scenario: if the same Projects folder is ever accessed from two machines, dirty-state and destination matching need a machine identifier — out of scope for v1 but worth keeping in mind in the data model.
- Cloud destination support (v2): would slot in as a new `kind` in the destinations schema; sync mechanism would differ from robocopy (likely a separate uploader module) but source/watcher layer is unaffected.

---

# Phased Implementation Plan

## Phase 0 — Project Scaffolding
- Set up C++ project (CMake), FLTK dependency (vcpkg or bundled), basic Win32 tray icon (Shell_NotifyIcon) with a placeholder context menu.
- Establish logging utility and config load/save (nlohmann/json, matching prior project conventions).
- Deliverable: app starts, sits in tray, loads/saves an empty config, quits cleanly.

## Phase 1 — Manual Source Management + Config UI
- FLTK dashboard window: add/remove manual sources via native folder/file dialog, list view with status placeholders.
- Destination management UI: add a Path-type destination; add a Removable-type destination (pick a currently-connected drive, capture its volume serial + label).
- Persist all of the above to `config.json`.
- Deliverable: user can fully configure sources and destinations; nothing syncs yet.

## Phase 2 — Change Watching + Debounce
- Implement `ReadDirectoryChangesW`-based recursive watcher per manual source root.
- Implement debounce timer per `(source, destination)` pair; on quiet period, mark dirty and log (no actual sync yet — stub it out).
- Deliverable: tray/dashboard shows "pending sync" status accurately in response to real file changes.

## Phase 3 — Backup Engine (Live Mirror)
- Implement robocopy invocation wrapper (process spawn, capture exit code, parse log for summary).
- Wire dirty-flag consumption to actual robocopy runs; update status to synced/error accordingly.
- Persist dirty-state to disk (`dirty-state.json`) so it survives app restarts.
- Deliverable: manual sources fully back up incrementally and reliably to Path-type destinations.

## Phase 4 — Removable Drive Handling
- Implement device-change window + `RegisterDeviceNotification`.
- Volume serial matching on arrival; flush queued dirty pairs for that destination.
- Handle destination unavailable mid-sync (drive pulled during robocopy) gracefully — mark pair dirty again, no crash.
- Deliverable: full manual-source backup flow working end-to-end with a real flash drive, survives disconnect/reconnect cycles.

## Phase 5 — Dated Snapshots
- Implement snapshot scheduler (per-source-root timer).
- Implement zip creation (bundle miniz or similar) from source directly.
- Implement retention pruning.
- Deliverable: snapshots appear on schedule per destination, old ones pruned per retention policy.

## Phase 6 — Projects Auto-Discovery
- Implement Projects-parent scanner: marker file detection (`.backup-watch`), git-folder exclusion, optional `.backup-ignore` gitignore-pattern parsing.
- Wire discovered project sources into the same Source Registry used by manual sources (Phases 2–5 apply unchanged).
- Implement periodic + event-driven rescanning of the Projects parent to catch added/removed markers and newly git-init'd subfolders.
- Deliverable: pointing the app at the real `Projects/` folder correctly watches and backs up only the intended non-codebase content.

## Phase 7 — Polish
- Tray icon state variants (idle/syncing/pending/error), notification balloons for errors.
- Log viewer in dashboard.
- Pause/resume all watching.
- Startup-folder shortcut installation on first run (with opt-out).
- Deliverable: v1 ready for daily use.
