# Safe Windows Mirror implementation options

Research completed 2026-08-10 for the v1 backup-tool decision map.

## Finding

Direct `robocopy /MIR` is not the best fit for the agreed safety model. Microsoft defines `/MIR` as `/E` plus `/PURGE`, so Robocopy owns destination deletion and exposes no application-controlled healthy-scan or atomic plan/commit boundary.

Two credible designs remain for the later mechanism decision:

1. Let the application enumerate Eligible Items and own the deletion plan, while Robocopy performs only non-deleting transfers with explicit bounded retries and restartability.
2. Own the complete engine in C++ using `FindFirstFileExW`/`FindNextFileW`, `CopyFile2`, verification, and application-controlled deletion.

Robocopy is mature copy transport, but its exclusions are not gitignore-style, its output is not documented as structured, exit codes 0–7 are nonfailure outcomes, and the default retry policy must be overridden. A canceled or failed run must leave the Backup Route dirty for reconciliation.

## Primary sources

- [Robocopy syntax, `/MIR`, options, and exit codes](https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/robocopy)
- [FindFirstFileExW](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-findfirstfileexw)
- [FindNextFileW](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-findnextfilew)
- [CopyFile2](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-copyfile2)
- [Windows long-path behavior](https://learn.microsoft.com/en-us/windows/win32/fileio/maximum-file-path-limitation)
- [Windows Job Objects](https://learn.microsoft.com/en-us/windows/win32/procthread/job-objects)
