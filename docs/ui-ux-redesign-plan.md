# BackItUpTool UI/UX redesign proposal

Date: 7 September 2026. Status: **Design review — no implementation approved or started.**

The direction is a calm, distinctive desktop backup tool: warm ink surfaces, a sea-glass accent, clear status language, and a small source-to-copy visual motif. Its personality comes from typography, composition, and useful feedback. Keep the native Windows frame, compact controls, and fast FLTK rendering.

## 1. Current application audit

### Scope and evidence

This is a source-based audit of all six panels, the main window, dialogs, notification, tray, native pickers, theme, and relevant backup flows. I also checked the bundled FLTK 1.4.5 sources. No application code or widgets were changed. The running interface was not visually inspected; clipping and appearance observations below follow from widget definitions and must be confirmed in the baseline visual review before implementation. Native desktop inspection is unavailable through the enabled UI tool in this session.

Implementation evidence: `src/main.cpp`, `ui_theme.hpp`, `window_theme.cpp`, every `*_panel.cpp`, `size_approval_dialog.cpp`, `backup_notification.cpp`, `clear_history_button.hpp`, `native_file_dialog.cpp`, and the backup configuration, engine, and scanner. These sources are more current than feature descriptions in the README.

### Existing visual language

- Windows-only application, C++20, FLTK 1.4.5. Initial window 1040 × 680, minimum 860 × 560. A 52-unit header, 156-unit sidebar, and 26-unit footer surround six stacked pages.
- Already dark: background `#1E1E1E`, surface `#262626`, navigation `#181818`, border `#3F3F3F`; near-white text and muted gray secondary text. Windows blue is the main action color; red removal buttons, amber/green approval buttons, cyan copying status.
- Segoe UI and Segoe UI Semibold are registered, with Consolas for some paths, timestamps, and numeric fields. Titles are 18, section labels 13, body/control labels 12, and many hints and table headings 11.
- Most controls are flat rectangles, 28–30 units high. Page margins are usually 16, with many hardcoded offsets and independently positioned table headers. Styling helpers are repeated across files.
- Sidebar icons and the trash icon are drawn with FLTK primitives. Status graphics are Unicode sequences such as `●—●` and `●→○`. Most table status text has the same foreground color as other cells.
- Hover treatment exists for navigation and the trash control. Most buttons only define normal/pressed colors. Many controls explicitly suppress visible keyboard focus.

This is already a styled interface. The redesign should address its dense composition and inconsistent behavior as well as its palette.

### Where stock styling remains

`Fl::scheme("none")` plus global background colors darkens much of the toolkit, but does not form a complete component system. Inputs and browsers retain `FL_BORDER_BOX`; browser scrollbars, choice arrows/popups, the Settings checkbox indicator, and message dialogs retain stock widget drawing. Pause styles its button label but does not explicitly style its menu text. Tooltips and `fl_alert`/`fl_choice` message fonts retain FLTK defaults; the bundled sources initialize those fonts to `FL_HELVETICA`. Global recoloring does not guarantee appropriate contrast for every inset, border, disabled label, or popup state.

Windows file pickers, tray menus, UAC prompts, and fatal startup MessageBox dialogs are separate OS surfaces. Their appearance is not controlled by the FLTK palette. The main window and approval dialog explicitly request dark title bars; general FLTK alerts do not use that same wrapper.

### Screen and window inventory

| Surface and current purpose | Current friction and restructuring opportunity |
|---|---|
| Main window / navigation: global status, configuration counts, Pause, Run now, six pages, Open data folder, footer feedback | Three places compete to explain status. Pause does not show its end time or expose Resume directly. Open data folder sounds like the backup location but opens application configuration/logs. Counts and terminology such as “routes” need explanation. |
| Backup status: destination availability, pending route count, configured watching count, last success/archive, recent failures | Five equally weighted rows precede a large failure browser. Zero destinations produces green “0 of 0 available”; zero pending produces “Current”; watching reflects configured count rather than proven watcher health. A root with no opted-in projects can also look current. Historical failures are presented as requiring attention without proving they remain unresolved. |
| Sources: search, add files/folders, choose mode, bulk removal, five-column browser | Path competes with state/type/mode/count. Fixed columns consume about 778 units before the trailing column, while the minimum window leaves roughly 672 for the browser. Separate headings cannot track browser horizontal scrolling. Empty configuration and zero search matches look like an empty list. Refresh rebuilds rows and loses selection. Mode is chosen in a stock dialog after picking paths. |
| Projects: add roots, choose mode, discover opted-in and eligible folders, watch selected, remove roots | Roots, watched projects, and eligible folders share a flat browser, distinguished by indentation and `>`/`+`. Watched rows are deselected by code; selection meaning varies by row. “Root,” “eligible,” and marker-file instructions assume technical knowledge. Discovery runs synchronously during refresh. Bulk watch reports only the first error. |
| Destinations: enumerate USB drives, refresh, add USB/folder/network location, remove, view pending/free space | A wide toolbar gives permanent red prominence to removal. Offline and dirty destinations both say “Waiting.” Free space is shown for connected removable volumes, otherwise `--`. Device refresh resets the dropdown to its first drive and list selection is rebuilt. Duplicate USB entries are detected only after Add. |
| Activity: latest 250 records, result/source/destination/details, clear history | Fixed columns and raw UTC strings reduce readable details. Every non-warning/non-error record is called “Completed,” which may misrepresent informational events. No filter, detail expansion, or copy action. Trash clears immediately without confirmation. |
| Settings: settle delay, large-file threshold, Windows startup, explicit Save | Developer-facing `ReadDirectoryChangesW` and `config.json` copy distracts from decisions. Footer can imply immediate saving although Save is required. Returning to this page reloads values, losing an unfinished edit. Validation uses an alert, does not identify the field visually, and its message omits the 100000 upper bound. |
| Source/root mode dialogs | Mirror and Zipped explanations omit the practical deletion/history distinction. Folder mirrors use `/MIR`; deletion propagation must be explained. There is no explicit review of selected items and destination scope. |
| Source/destination/root removal confirmations | Existing wording correctly says backup files remain. Preserve that. Clarify exactly what stops being watched or receiving backups. |
| Large-file approval: 720 × 430 modal, topmost window, file list, Ignore permanently / Always allow | Close is intentionally ignored; two lasting decisions are the only exits. Long paths compete for space. The engine stores a project-wide decision: ignore applies a size limit on later backups, not just the displayed filenames. Current copy understates that scope. |
| Backup notification: borderless 360 × 96 topmost panel | Shown while preparing/running, positioned on screen 0; no dismiss/open action. It can be redundant while the main window is visible. No byte-level progress is available in the current UI flow. |
| Native pickers, error alerts, fatal startup dialog | Pickers are correctly native and support multiselect; titles are generic even for destinations. Raw exceptions/HRESULTs do not provide a clear next step. Normal UI errors use modal alerts. Startup failure must retain a reliable fallback before theming is available. |
| Tray and first run | Starts hidden, closes to tray, Open/Exit menu. Explorer badge registration is attempted before the UI, potentially prompting for elevation without context; failure is logged. No guided initial setup. Exit hides the window before waiting for worker completion, so apparent exit can precede actual completion. |

## 2. Design direction and system

### Character and hierarchy

Working direction: **Quiet Harbor**. Use deep blue-green ink, slightly warm readable text, crisp lines, and restrained 4–6-unit corner rounding. A pair of simple folder/copy marks connected by a line becomes the small identity motif: linked when current, separated when waiting, a short moving segment while working. Always pair it with words. Use it in the header and empty state, not in every row.

Keep all six destinations in navigation, ordered Overview, Sources, Projects, Destinations, Activity, Settings. Rename only “Backup status” to “Overview.” Place a small app name above the navigation and Settings at the bottom. Introduce concepts in page subtitles: Sources are what to back up; Destinations are where copies go. Explain once, near configuration actions, that **every configured source and opted-in project goes to every destination**.

### Palette

| Role | Proposed value | Usage |
|---|---|---|
| Window / navigation | `#141D22` / `#10181D` | Warm ink base and deeper sidebar |
| Surface / raised surface | `#1D2A30` / `#26373F` | Sections, inputs, menus, selected details |
| Divider / control outline | `#354950` / `#66858D` | Quiet separators; stronger interactive boundaries |
| Main / secondary text | `#EDF2EC` / `#B2C3C5` | Reading and explanatory text |
| Accent | `#82D8C5` | Primary fill, active marker, links |
| Accent hover / pressed | `#9AE5D4` / `#65BBAA` | Dark `#102824` text on all primary fills |
| Selected row | `#294A4B` | Main text plus a visible selection marker |
| Success / warning / error | `#A8D998` / `#F0C477` / `#F29B9B` | Icon + explicit status label; restrained tinted background |
| Working / focus | `#99CBEE` / `#B2E9DD` | Working indicator; two-unit keyboard focus ring |
| Disabled foreground / fill | `#8D9FA3` / `#263237` | No hover or pressed response; nearby reason when needed |

These are candidate tokens, not a claim of rendered accessibility certification. Phase 1 must measure actual pairings: at least 4.5:1 for normal text and 3:1 for large text and meaningful control/focus boundaries. Do not use the subtle divider as an input's sole boundary. States must remain understandable without color.

### Typography and spacing

Keep Segoe UI / Segoe UI Semibold on supported Windows 10/11 systems; use Consolas only for raw paths or technical details where alignment helps. Check font availability; fall back to FLTK's standard sans/monospace slots if necessary. No font download or additional dependency.

Type scale in FLTK logical sizes: page title 24 semibold, main health statement 20 semibold, section heading 16 semibold, body/input 14 regular, button/navigation 13 semibold, caption/table heading 12. Avoid long all-caps labels. Measure wrapped text instead of assuming one-line height.

Use a four-unit spacing base: 4, 8, 12, 16, 24, 32. Page padding 24 (16 in compact layout), section gap 24, row padding 12, label-to-input gap 8. Standard controls 36 high; navigation 40; compact table rows 36; name/path rows 52–56. Aim for a 176-unit sidebar and 64-unit header while retaining the existing initial/minimum window sizes. At the minimum size, wrap toolbars, stack summary sections, and move optional table data into a detail panel. Never shrink text to make columns fit. Settings and dialogs scroll vertically when required.

### Icons and component states

Extend the existing primitive-drawn icon family: 16/20-unit folder, file, drive, archive, check, clock, pause, warning, search, add, disclosure, and trash. Consistent stroke and alignment, no emoji or icon font. Icons support text for navigation and actions. Only familiar ancillary controls such as search-clear may be icon-only, with tooltip and visible focus.

| Component | Hover / pressed | Focus / selected | Disabled / loading |
|---|---|---|---|
| Buttons and inline actions | Lighter surface; darker pressed fill | Persistent two-unit ring; Enter/Space behavior retained | Muted label, no hover; meaningful busy label and duplicate activation blocked |
| Navigation | Subtle filled row | Ring independent of selected rail and semibold label | Usually remains usable during backup; page scanning never blocks navigation |
| Inputs and search | Stronger border; normal text-selection behavior | Ring and visible caret; label always present | Readable value; retain query during loading; inline invalid message and error border |
| Checkbox / mode radio choices | Highlight indicator and label area | Ring; Space toggles; checked mark independent of color | Keep checked state visible; no changes during its save |
| Dropdowns and menus | Highlight active item; pressed opener | Arrow keys, Enter, Escape; focus returns to opener | Unavailable options explained; show “Finding drives…” during enumeration |
| Table/tree rows | Gentle hover; retain native selection gestures | Focus outline distinct from selection; arrows, Ctrl/Shift multiselect; explicit disclosure | Non-actionable rows remain readable; preserve selected IDs and scroll position through refresh |
| Scrollbars | Brighter thumb / darker drag | Keyboard scrolling through focused content | Hide/disable only when there is no overflow; never invisible while usable |
| Dialog actions / notification actions | Shared button behavior | Predictable initial focus and return focus; Escape meaning explicit | No accidental default for permanent choices; progress never traps focus |

Tooltips use the same font/palette and add detail, not essential instructions. Loading labels must state the operation; never replace a populated list with a blank surface while refreshing.

### FLTK implementation strategy and tradeoffs

Keep `Fl::scheme("none")`. Create one small shared theme layer for tokens, initialization, text styles, tooltip/message defaults, and common widget styling; remove duplicated style helpers as callers migrate. Register a few application boxtypes for consistent surfaces and outlines. Boxtypes can draw shared borders but cannot independently manage hover/loading state, so use narrow `final` subclasses only where behavior or drawing requires them (buttons, navigation, status display). Preserve FLTK event handling and focus semantics.

Use nested `Fl_Flex` for shell, toolbars, and form groups; simple explicit resizing for the few compact-layout changes. Its existing `margin`, `gap`, and `fixed` methods are present in the pinned dependency. Avoid a general-purpose layout framework.

Use a small `Fl_Table_Row`-derived presentation widget for source/destination/activity lists where proper headers, flexible columns, selection, and status drawing justify it. Keep each panel's row data explicit; share drawing functions rather than inventing a generic data-binding system. `Fl_Table` delegates cell painting to `draw_cell()`, so clipping, selection visuals, and keyboard behavior require deliberate verification. [FLTK table reference](https://test1.fltk.org/doc-1.4/classFl__Table.html).

For Projects, prefer a root selector and separate project list over building an elaborate interactive tree. Inputs, checkboxes, menus, and scrolling continue to use FLTK controls with consistent styling. Custom checkbox drawing is warranted only if shared boxtypes cannot meet contrast/state requirements.

No image-based skinning: it adds scaling and state assets without improving this interface. No replacement window frame or renderer. Keep Windows integration behind its existing narrow functions. FLTK owns child widgets through their parent groups; retain clear owning versus non-owning references and remove timers before owners are destroyed.

### Feedback and motion

Instant hover/pressed feedback and one subtle 8–12 fps indeterminate working indicator are sufficient. Start its FLTK timeout only during visible work; remove it on hide/completion/destruction. No idle animation, page transitions, or fabricated percentage/ETA. Current preparation and run summaries support truthful stage labels, not granular copy progress. Keep all FLTK updates on the UI thread through the existing `Fl::awake` pattern.

Successful user actions show a nearby receipt (“Added 3 folders · 1 already configured”), with routine background success staying quiet. Errors remain visible until resolved or dismissed. Animation may stop without losing any information.

## 3. Proposed screens and flows

### Shell and Overview

Header: compact global state on the left, Pause/Resume and **Back up now** on the right. During a run, use “Backing up…” and show whether automatic work is also paused. Paused state includes a local resume time; manual runs remain available and Pause does not imply cancellation of current work. Move “Open data folder” into Settings as “Open app logs and configuration.” Footer holds the last action result, not a competing health summary.

Overview order: (1) one health statement with supporting facts, (2) actionable problems, (3) compact Sources / Destinations / Last backup summary, (4) recent activity. Empty setup uses two inline steps: Add files or folders, then Add a destination, followed by “Backups start automatically when both are ready.” Offer Projects as an alternative to manual sources. No forced wizard.

Example composition:

    [small connection mark] Changes are waiting       [Pause] [Back up now]
    Overview
    Connect Travel SSD to finish 2 pending backups.   [View destination]
    Sources: 6             Destinations: 1 of 2 available
    Last successful backup: Today, 14:32
    Recent activity                                      [View all]

Centralize display status calculation using existing configured routes and runtime facts. Distinguish Not set up, Never backed up, Current, Waiting for destination, Source unavailable, Needs approval, Backing up, and Action needed. “Current” requires successful current configuration coverage, not just zero dirty rows; it means no known pending changes, not content verification. Display pause and partial failure as additional facts rather than hiding them under one status priority. Unknown discovery/watcher health must remain unknown. Count source-to-destination jobs as “pending backups,” not changed files. Historical errors belong in Activity; clearing them never repairs a route.

### Sources and mode selection

Lead with file/folder name and secondary path, then status and mode. Move the redundant all-destinations count into a shared explanatory line. Keep Add files and Add folders equally discoverable. Selection reveals “Remove 3 sources…” in a contextual toolbar with neutral styling until confirmation. Provide Copy path and Open containing folder through visible details.

Empty: “Choose the files and folders you want copied.” Search-empty: “No sources match this search” with Clear search. Loading: retain rows and show a small refresh label. Missing source: identify its path and offer location/details, with Retry only where the existing run flow supports it.

After the native picker, present a compact mode review with selected-item count, expandable paths, and two explicit radio choices. **Mirror:** “A browsable copy of the latest state; deleted files in mirrored folders are also removed from the backup.” **Zipped:** “Compressed snapshots with the app's daily/monthly retention.” State that a no-change run does not create another archive. Require a deliberate selection before Add; cancel writes nothing. Do not introduce per-destination routing or mode changes to existing sources in this pass.

### Destinations

Show configured destinations first as readable rows: name, Connected/Disconnected/Unavailable, path, pending backups, and available free-space data. Use “Not measured” rather than implying zero free space for folder locations. Keep USB identity tied to volume serial, not drive letter.

An Add destination area provides USB drive and Folder/network location choices. Automatically update available devices; preserve the chosen serial across updates. Mark already-added devices before submission. Empty/no-USB copy offers folder selection rather than a dead end. On enumeration failure, keep known destinations visible with “Could not refresh drive availability” and Retry. Explain that disconnected destinations keep their pending work. A successful add explicitly says all sources will also back up here.

### Projects

Two levels: select a “Project parent folder” (root), then inspect its project list. Root summary shows backup mode and watched project count. Separate filters for Watched and Available to watch, with search. Rows have a name, relative path, and readable state; actions refer to the selected row type. Keep Add parent folder and Watch selected folders; do not imply removing a root deletes markers or existing copies.

Empty root: “Choose a folder containing your projects.” Empty watched list: “No projects watched here yet,” followed by available choices. If none are eligible, explain the actual exclusions, including entire Git repository folders, generated folders, and junctions; do not silently change discovery rules to make the UI look populated. Explain the marker only beside Watch: “Creates a .backup-watch file so this folder stays selected.”

Scanning keeps old results with “Finding project folders…” and stale-state indication. If refresh proves slow, move discovery off the UI thread with versioned results so obsolete scans cannot replace newer configuration. Partial watch success reports all failed paths in details plus the successful count. This is an explicit threading/feedback change, not a paint-only task.

### Activity

Use local readable times, result icon/text, source → destination summary, and expandable/selectable full details. Show exact UTC in details. Add All / Errors / Waiting filters and search over the currently loaded 250 records; label that scope honestly. Empty history and no matching results get distinct copy. Informational entries say “Info” unless the recorded event establishes completion.

Replace the trash-only action with “Clear history…” in a secondary menu and a confirmation explaining that it removes records, not files or current problems. Keep useful failure text copyable, with the relevant source/destination link when still configured. No fabricated “repair” action; retry uses the existing backup mechanism and states its all-pending-work scope.

### Settings

Three short sections: Automatic backups, Large project files, Windows integration. Rename Settle delay to “Wait after a file changes” with seconds; explain the existing default of 8 seconds. Threshold shows MiB and the existing default of 50 MiB. Retain current validation limits and make 1–100000 explicit where applicable. Remove API/config-file vocabulary from ordinary instructions.

Maintain an edit draft across page navigation and unrelated refreshes. Show Unsaved changes, Save changes, and Discard; disable Save until changed and valid. Field errors sit below the input, focus the first invalid field, and preserve all values. Save success appears next to the action. Startup-registration failure must not look like an unchecked preference successfully saved; report the actual outcome, preserving existing rollback intent.

### Approval, first run, tray, and errors

Large-file dialog leads with project name, threshold, file count, total size, and a scrollable file/size list. Choices explicitly describe **future project-wide policy**: Include large files in this project, or Skip files above the configured limit in this project. No green “safe” styling that favors one policy and no implicit permanent default. Explain that changing the threshold affects later skipping.

Recommend **Decide later**, including Close/Escape: save no permanent decision, leave affected project work pending, and allow unrelated eligible work to proceed. This requires a real deferred outcome in the preparation/scheduling flow, with suppressed repeat prompting until a user-triggered reconsideration or defined later session. It must not be implemented by mapping Close to Ignore. Treat this as a separate higher-risk slice; existing always-allow/ignore decisions remain intact.

On an unconfigured first launch, show the setup overview. Subsequent launches retain tray startup. Explain close-to-tray once. Move Explorer badge setup behind an explained “Enable Explorer folder badges” action in Settings; approval denial leaves backups usable and the integration visibly disabled/unavailable. This deliberately changes first-run behavior and needs review as part of this plan.

Keep the native tray menu and add concise current-state tooltip text. Preserve Open/Exit. If exiting must wait for an active backup, report “Finishing backup before exit” visibly; do not add force cancellation without engine support.

Use in-window progress while the main window is visible. While hidden, the lightweight notification may show work without taking focus; add Open app and Dismiss, and choose the relevant monitor work area. Dismiss hides the notice only. Failures remain discoverable on reopening even if the notice is dismissed.

Routine recoverable errors become inline messages: what failed, what was saved or not saved, and the next action, with expandable technical details. Keep modal confirmation for destructive history/config removal and explicit lasting policy decisions. Retain an OS error fallback for startup failures.

## 4. Phased implementation and verification

Estimates are focused engineering days including local verification, not delivery promises. Total base scope: approximately **13–21 days**. The separate approval/first-run behavior slice adds **3–5 days**. Discovery responsiveness work, if required by measured latency, adds **1–3 days**.

| Phase | Changes and completion gate | Effort / risk | Regressions and required checks |
|---|---|---|---|
| 0. Baseline after plan approval | Capture every current screen and dialog with disposable configuration and backup fixtures; record empty, populated, running, offline, and failure states. Verify mirror/archive semantics for final microcopy. | 1 day / low | Keep real backup configuration untouched. Use the existing `build` folder for all builds. |
| 1. Global foundation | Shared tokens/fonts, toolkit popup/tooltip defaults, boxtypes, common button/input states, icons, visible focus. Apply globally before moving layouts. Delete duplicated styling as it is replaced. | 2–3 days / medium | Contrast, popup readability, caret/selection, disabled states, title bars, font fallback, 100/125/150/200% DPI. Confirm callbacks, parent ownership, and shortcuts still work. |
| 2. Shell and Overview | Responsive shell, truthful shared status summaries, setup empty state, contextual problem links, pause-until time and direct Resume. | 2–3 days / medium–high | Empty configuration, never-run routes, zero opted-in projects, stale records, partial availability/failure, running while paused, manual no-op, automatic resume, close/reopen. Unit-test status aggregation independently of drawing. |
| 3. Sources and Destinations | Shared list presentation, adaptive columns/details, contextual bulk actions, mode review, selection preservation, device feedback. | 3–4 days / medium | Multiselect/filter mapping by stable IDs, duplicate and mixed additions, cancel, Unicode/long/UNC paths, overlap rejection, device removal during selection/run, changing drive letter, config-save failure. Mirror and ZIP outputs must match baseline. |
| 4. Projects | Parent-folder selection, watched/available lists, explanations, partial-success receipts, persisted UI selection. | 2–4 days / high | Nested folders, markers/UUIDs, exclusions, zero eligible folders, root removal, failed marker writes, discovery refresh during backup. Add asynchronous discovery only if responsiveness measurements justify it; verify cancellation/lifetime and stale-result rejection. |
| 5. Settings, Activity, ordinary dialogs | Draft settings, inline validation, readable/local event times, loaded-history filters/details, safe clear confirmation, consistent error dialogs. | 2–3 days / medium | Save/discard/navigation, unrelated refresh during edit, startup rollback, local time conversion, historical deleted entities, clear history versus runtime state, focus return. |
| 6. Polish and release gate | Final spacing/icon/microcopy consistency, notification refinement, keyboard traversal, minimum-size layout and long-content pass. | 1–3 days / medium | Multi-monitor/DPI movement, hidden-window CPU, animation timer cleanup, repeated open/close, dialog overflow, keyboard-only completion of every flow. |
| Separate behavior slice | Deferred large-file decisions, first-run visibility, explained optional Explorer setup, truthful exit-wait feedback. Implement as independent reviewable changes. | +3–5 days / high | No permanent decision on dismiss; no prompt loop; unaffected jobs continue; deferred work survives as pending; existing decision persistence unchanged; UAC accept/deny; tray-only later launches; worker shutdown remains safe. |

### Stability boundaries

Preserve automatic all-source-to-all-destination routing, changed-only scheduling, pause semantics, USB identity, marker/exclusion rules, folder-mirror deletion behavior, ZIP retention/compression, and existing backup locations. The design introduces no restore engine, new backup mode, per-destination routing editor, or fake progress reporting.

Display models read existing state; drawing must not perform disk scans or database mutations. Keep refresh state keyed by stable IDs, not row positions. Separate config changes from visual refresh so an action receipt cannot claim success before persistence succeeds. Remove obsolete UI helpers instead of keeping a parallel legacy theme.

Run the existing configure/build/test presets from `build` for implementation phases. Add focused tests only for changed behavior: state aggregation, settings draft/persistence outcomes, and deferred approval scheduling. Visual-only changes need screenshot/manual checks rather than tests that duplicate pixel constants. Backup/scanner/config/state tests are the regression baseline; no build was needed for this documentation-only audit.

### Review acceptance criteria

- A first-time user can identify what is backed up, where copies go, and their next action from Overview.
- Every operation provides a useful outcome, including duplicates, cancellation, no work, partial success, and failure.
- No empty, unknown, offline, or never-backed-up state is presented as proven protection.
- Every interactive control is keyboard reachable with visible focus; no approval window forces a permanent choice through dismissal.
- Minimum-size and 200% DPI layouts preserve readable controls, full-path access, and scrollable content.
- Routine hidden operation stays quiet and event-driven; visual polish does not create idle work.
- The same test fixtures produce the same backup files and persisted routing decisions, except for explicitly approved deferred-approval behavior.

**Review decision:** approve or revise the visual direction, six-page layouts, and phased base scope; separately confirm whether the higher-risk behavior slice is included. No widget changes should begin before that review.
