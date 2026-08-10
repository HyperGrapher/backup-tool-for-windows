# Snapshot archive implementation options

Research completed 2026-08-10 for the v1 backup-tool decision map.

## Finding

`libzip` 1.11.4, available at the repository's pinned vcpkg baseline and compatible with the existing static triplet, is the leading v1 candidate. It supports ZIP64, Windows Unicode sources, custom streaming sources, progress and cancellation, structured errors, and temporary-file replacement during `zip_close()`.

The application should enumerate Eligible Items itself, register UTF-8 archive paths with forward slashes, prefer wide-path or handle-backed Windows sources, use ordinary Store or Deflate compression, and record success only after close plus consistency verification. Compatibility with current Windows Explorer and 7-Zip, extended UNC paths, destination removal during close, more than 65,535 entries, and ZIP64 need explicit acceptance tests.

`minizip-ng` is the strongest alternative but requires application-owned same-directory partial-file publication. `libarchive` is broader than needed. Bundled 7-Zip adds a supervised helper executable. PowerShell `Compress-Archive` is unsuitable because Microsoft documents a 2 GB per-file limit and omission of hidden items.

## Primary and upstream sources

- [libzip](https://libzip.org/)
- [libzip documentation](https://libzip.org/documentation/libzip/)
- [zip_close](https://libzip.org/documentation/zip_close/)
- [Windows Unicode source support](https://libzip.org/documentation/zip_source_win32w/)
- [minizip-ng](https://github.com/zlib-ng/minizip-ng)
- [Compress-Archive limitations](https://learn.microsoft.com/en-us/powershell/module/microsoft.powershell.archive/compress-archive)
- [Windows long-path behavior](https://learn.microsoft.com/en-us/windows/win32/fileio/maximum-file-path-limitation)
