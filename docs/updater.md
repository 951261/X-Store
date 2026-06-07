# Updater

The updater lives in `updater/updater.cpp`. It uses:

- `downloadFileHTTPS()` for GitHub JSON and release assets,
- `cJSON` for parsing the GitHub release response,
- `miniz` for extracting the downloaded update ZIP,
- `file-stuff.c` helpers for directory creation, recursive delete, and file search.

## User Entry Point

The UI exposes `Update X-Store` as a download type. If selected, `showUI()` calls:

```text
runUpdate()
```

and returns `-1` to `main()`.

`runUpdate()` launches the updated XEX itself on success, so the normal game-download flow does not continue after update selection.

## Latest Release Lookup

The updater downloads:

```text
https://api.github.com/repos/951261/X-Store/releases/latest
```

into a 4 MB memory buffer.

`downloadLatestReleaseInfo()` expects status 200. The JSON is parsed with `cJSON_Parse()`.

## Version Check

`isUpdateAvailable()` reads the release `tag_name`, copies it into a small local version buffer, and compares it against `CURRENT_VERSION` from `settings.h`.

`parseVersionString()` accepts versions with:

- optional leading `v` or `V`,
- up to three numeric parts,
- suffixes beginning with `-`, `+`, whitespace, or end of string.

`compareVersionStrings()` compares the three numeric parts in order. If parsing fails, it returns `0`, meaning no update.

`isUpdateAvailable()` is implemented but not currently part of the main startup flow shown in `main.cpp`; the visible flow updates only when the user selects the updater menu item.

## Choosing The Asset

`getFirstAssetDownloadURL()` looks at the first item in the GitHub release `assets` array and reads:

```text
browser_download_url
```

There is no asset-name filtering. The latest release's first asset must be the intended X Store update ZIP.

## Finding The Current XEX

`findMainXexPath()` searches `game:\` for a file ending in `Store.xex`. If that fails, it falls back to the first `.xex` file.

The found filename is converted into a full path with:

```text
game:\<filename>
```

The backup path is the same full path plus `.old`.

## Downloading The Update Asset

`downloadUpdateAsset()` downloads the update ZIP to:

```text
game:\tmp_update\X-Store_Update.zip
```

It first ensures:

```text
game:\tmp_update
```

exists.

The asset download follows up to 5 redirects. It relies on `DumpResponse()` returning `302` and placing the `Location` value in a caller-provided redirect buffer.

If any redirect lacks a URL, or if there are too many redirects, update fails.

## ZIP Extraction

`unzipFileToFolder(zipPath, outputFolder)` uses miniz.

Before extracting each file, it rejects unsafe ZIP paths:

- empty names,
- absolute paths,
- names containing `:`,
- names containing `../` or `..\`,
- exactly `..`.

This blocks common zip-slip paths.

Folder separators inside ZIP names are normalized to backslashes. Parent folders are created with `customForceMkdir()` before files are extracted.

## Picking The New XEX

After unzipping, `downloadUpdateAsset()` searches the temporary update folder for the first `.xex`.

It renames that extracted XEX to the requested output path, normally:

```text
game:\XStoreUp.xex
```

Then it deletes `game:\tmp_update`.

## Replacement And Rollback

`replaceMainXex(mainXexFile, tempXexFile, backupXexFile)` performs the final swap:

1. Remove any previous backup.
2. Rename current main XEX to `.old`.
3. Rename temporary update XEX into the main XEX path.
4. If step 3 fails, try to rename `.old` back to the main XEX path.

If restoring the backup also fails, the error is logged, but the function still returns failure to the caller.

## Relaunch

When `runUpdate()` succeeds, it calls:

```text
XLaunchNewImage(mainXexFile, NULL)
```

This starts the updated app image.

## Failure Cleanup

If update fails after `game:\XStoreUp.xex` may have been created, `runUpdate()` removes that temp XEX.

The JSON object and memory buffer are freed on all paths.

Temporary ZIP extraction data is cleaned by `downloadUpdateAsset()` after a successful move of the extracted XEX. Some early failures inside that function may leave `game:\tmp_update` behind for debugging or manual cleanup.
