# Architecture

The app is a pipeline with a text UI on the front and streamed file processing on the back. The high-level shape is:

```text
main.cpp
  -> showUI()
       -> downloadFileHTTPS(search page into memory)
       -> parse_vimm_search_results()
       -> downloadFileHTTPS(selected game page into memory)
       -> parse_vimm_media_ids()
       -> return final download URL, game name, download type
  -> getSettings()
  -> getGame()
       -> downloadFileHTTPS(archive into game:\tmp.7z.001)
       -> decompressSevenZipFile()
       -> XBLA copy OR extractIso()
```

## `main.cpp`

`main.cpp` is the orchestration layer. It does not perform TLS, HTML parsing, 7z decompression, or ISO traversal directly. Instead, it glues those modules together and handles the user-facing retry loop.

Important responsibilities:

- Deletes the previous log file at startup.
- Creates the ATG-backed text console with `MakeConsole("embed:\font", ...)`.
- Tries to ensure `game:`, `Usb0:`, `Usb1:`, and `Hdd:` are mounted.
- Calls `showUI()` to collect the selected download type, final archive URL, and selected game display name.
- Reads `game:\settings.txt` into a `Settings` struct.
- Sanitizes the selected game name into a FATX-safe folder name.
- Chooses an output root based on download type.
- Calls `getGame()` with the selected URL and computed output paths.
- If something fails, waits for `Y` to restart the search or `B` to exit.

The main loop returns success immediately after a successful download/extract. It only repeats after failure and user confirmation.

## Settings Model

`main.cpp` defines a local `Settings` struct with four paths:

- `originalXboxPath`
- `xbox360Path`
- `xblaPath`
- `legacyPath`

`getSettings()` reads `game:\settings.txt` line by line and recognizes:

- `original-xbox-path: `
- `xbox-360-path: `
- `xbla-path: `
- `output-path: `

Lines beginning with `#` are skipped. If a type-specific path is missing, the code tries to fall back to the legacy `output-path`. Missing paths are logged but do not stop startup immediately.

## Download Type

`user interface/ui.h` defines:

- `ORIGINAL_XBOX = 1`
- `XBOX_360 = 2`
- `XBLA = 3`
- `AUTO_UPDATE = 4`

The value returned from `showUI()` drives both the search URL and the later extraction path.

## UI Layer

The UI is text-mode, not XUI, despite the project containing `.xur` / `.xui` files. It uses ATG console output plus XInput polling.

`showUI()` is the public entry point. It:

- waits for controller buttons to be released,
- shows the download type menu,
- optionally runs the updater,
- opens the Xbox software keyboard,
- builds a Vimm search URL,
- downloads the search results into memory,
- parses game result rows,
- lets the user select a result,
- downloads that result page,
- parses media IDs / disc versions,
- lets the user select media when more than one exists,
- writes the final archive URL to the caller's buffer.

## Download Layer

`downloadFileHTTPS()` is the single HTTPS download function used by search, metadata, game archives, and updater JSON/assets.

It has two output modes:

- file mode: pass `downloadIntoFile = true`; response body streams to `fileName`.
- memory mode: pass `downloadIntoFile = false`; response body is copied to `dataBuffer`, and `outputBufferSize` is updated to the amount written.

For archive downloads, the first output path is normally `game:\tmp.7z.001`. Because the name ends in `.001`, `DumpResponse()` treats it as a split-file target and creates `.002`, `.003`, etc. as needed.

For HTML and JSON requests, the response goes into a caller-allocated 4 MB buffer.

## TLS Layer

`XboxTLS.cpp` exposes a small C API:

- `XboxTLS_CreateContext()`
- `XboxTLS_AddTrustAnchor_RSA()`
- `XboxTLS_AddTrustAnchor_EC()`
- `XboxTLS_Connect()`
- `XboxTLS_Write()`
- `XboxTLS_Read()`
- `XboxTLS_Free()`

The implementation stores BearSSL contexts in an internal heap-allocated struct. Callers do not see BearSSL directly.

`downloadFile.cpp` owns the app's current trust anchor list. It calls `addTrustAnchors()` after context creation and before connecting.

## Extraction Layer

There are two extraction steps for disc games:

1. `decompressSevenZipFile()` extracts the downloaded 7z archive into a temporary folder. Large extracted files can be split.
2. `extractIso()` extracts the resulting Xbox ISO into the final output folder.

XBLA content skips ISO extraction. Instead, after 7z extraction, the code scans for an 8-character hex title ID folder that contains a known Xbox content type folder, copies that title ID folder to the XBLA output root, and deletes the temporary extracted folder.

## Updater Layer

`runUpdate()` is called when the user selects `Update X-Store`. It:

- downloads GitHub's latest release JSON,
- reads `assets[0].browser_download_url`,
- finds the currently running main XEX under `game:\`,
- downloads the release asset ZIP,
- extracts it with miniz,
- finds a `.xex` inside the extracted update,
- moves it into `game:\XStoreUp.xex`,
- backs up the current XEX to `.old`,
- replaces the current XEX,
- launches the updated image.

If replacement fails after backup, it attempts to restore the old XEX.

## Logging

`dprintf()` writes to:

- the ATG console,
- stdout/debug output,
- `game:\DebugInfo.txt`.

`log_printf()` writes to stdout/debug output and `game:\DebugInfo.txt`, but does not display through the ATG console.

`debug_tls()` writes TLS/debug messages to stdout and `game:\DebugInfo.txt`.

For on-console user progress, use `dprintf()`. For quieter internal logs, use `log_printf()` or `debug_tls()`.
