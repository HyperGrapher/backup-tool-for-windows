# Windows change and Destination-presence signals

Research completed 2026-08-10 for the v1 backup-tool decision map.

## Finding

Use `ReadDirectoryChangesW`, volume/device broadcasts, power broadcasts, and optional network-change notifications only as latency-reducing hints. Every hint should debounce or enqueue reconciliation; enumeration and attempted Destination access remain authoritative.

`ReadDirectoryChangesW` can watch a subtree, but buffer overflow discards details and requires enumeration. Network buffers above 64 KiB fail, cached writes can delay notifications, and rename halves should not be treated as a durable transaction. Overlapped cancellation can race completion.

A hidden top-level Win32 window can receive `WM_DEVICECHANGE` and `WM_POWERBROADCAST`. Drive letters are current mount points rather than durable identity, so removable Destinations should combine volume GUID observations with an application-owned identity marker. Network Destinations should use UNC paths and direct access probes; network notifications do not prove share reachability.

Startup, overflow, watcher failure, reconnect, resume, and a low-frequency periodic deadline should all cause reconciliation. USN journals are unnecessary for the non-elevated v1 design.

## Primary sources

- [ReadDirectoryChangesW](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-readdirectorychangesw)
- [FILE_NOTIFY_INFORMATION](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-file_notify_information)
- [CancelIoEx](https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-cancelioex)
- [WM_DEVICECHANGE](https://learn.microsoft.com/en-us/windows/win32/devio/wm-devicechange)
- [Volume naming and GUID paths](https://learn.microsoft.com/en-us/windows/win32/fileio/naming-a-volume)
- [WM_POWERBROADCAST](https://learn.microsoft.com/en-us/windows/win32/power/wm-powerbroadcast)
