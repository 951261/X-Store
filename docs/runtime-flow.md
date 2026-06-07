# Runtime Flow

This document follows one normal download from process startup to extracted output.

## Startup

`main()` begins by deleting `game:\DebugInfo.txt`, then creates the text console:

```text
MakeConsole("embed:\font", black, white)
```

After the console exists, `CheckGameMounted()` verifies or attempts to mount several device aliases:

- `game:` is tested by creating `game:\test.tmp`. If that fails, it tries `\Device\Mass0`, then `\Device\Harddisk0\Partition1`.
- `Usb0:` is tested and, if needed, mounted to `\Device\Mass0`.
- `Usb1:` is tested and, if needed, mounted to `\Device\Mass1`.
- `Hdd:` is tested and, if needed, mounted to `\Device\Harddisk0\Partition1`.

The function currently returns `true` even when some mount attempts fail. Failures are warnings because the user's configured output path may still point somewhere that works.

## Main Loop

The main loop prints the current version from `CURRENT_VERSION`, then initializes output buffers:

- `selectedGameURL`
- `selectedGameName`
- `safeGameFolderName`
- `outputFolder`

It calls:

```text
showUI(selectedGameURL, ..., selectedGameName, ...)
```

If `showUI()` returns a negative value, the flow jumps to the failure prompt. Otherwise, the returned integer is treated as a `DownloadType`.

## Choosing The Output Folder

After UI selection, `getSettings()` reads `game:\settings.txt`.

The selected display name is passed through `MakeSafeFolderName()`. This helper:

- skips invalid FATX/path characters,
- skips control characters,
- collapses whitespace,
- removes trailing spaces and dots,
- returns `Unknown Game` if the name becomes empty,
- respects `FATX_SAFE_FOLDER_NAME_LEN`.

`main()` then appends the safe folder name to the type-specific setting:

- Original Xbox: `settings.originalXboxPath\safeGameFolderName`
- Xbox 360: `settings.xbox360Path\safeGameFolderName`
- XBLA: `settings.xblaPath\safeGameFolderName`

The final path is logged as `Extracting to: ...`.

## Calling `getGame()`

Disc games and XBLA pass different temporary path meanings:

Original Xbox and Xbox 360:

```text
getGame(URL, "game:\tmp.7z.001", "game:\tmp_output", outputFolder, downloadType)
```

XBLA:

```text
getGame(URL, "game:\tmp.7z.001", outputFolder, settings.xblaPath, XBLA)
```

For XBLA, `isoFolder` is not really an ISO folder. It is the temporary extraction folder, which is built from the selected game folder under the XBLA root.

## Archive Download

`getGame()` downloads the final Vimm URL to `game:\tmp.7z.001` with:

```text
downloadFileHTTPS(URL, sevenZipFile, NULL, NULL, true, dprintf)
```

The returned value is treated as an HTTP status code:

- below 200: failure,
- 200: continue,
- 400 or higher from the primary download domain: replace the primary domain with `SECONDARY_DOWNLOAD_DOMAIN` and retry once,
- other non-200 statuses: failure.

The downloader itself can return `302` for redirect responses. The updater has explicit redirect-following logic, but the game download path only has the primary/secondary domain fallback.

## 7z Extraction

After download, `getGame()` creates the temporary extraction folder with `customForceMkdir()`, then calls:

```text
decompressSevenZipFile(sevenZipFile, isoFolder, downloadType == XBLA)
```

The 7z extractor opens split inputs as one joined stream. If the archive contains an ISO, the extractor renames `.iso` entries to `tmp_0.iso`, `tmp_1.iso`, and so on before writing them. If an extracted file is larger than the split threshold, output is split near the filesystem-safe limit.

After successful 7z extraction, `DeleteSplitFiles()` removes the downloaded `tmp.7z.001`, `tmp.7z.002`, etc.

## XBLA Post-Processing

If `downloadType == XBLA`, `getGame()` does not search for an ISO. It calls:

```text
findXblaTitleIdDir(isoFolder)
```

The scanner looks recursively for exactly one directory whose basename is eight hex characters and which contains at least one known Xbox content type folder such as `000D0000`, `00007000`, `00004000`, `00000002`, and similar.

Once found:

1. The title ID folder basename becomes `gameID`.
2. The final destination becomes `settings.xblaPath\gameID`.
3. The destination directory is created.
4. `copyDirectory()` recursively copies the title ID folder into place.
5. The temporary extraction folder is deleted.

The selected game display folder is therefore only a temporary staging location for XBLA. The final installed folder is the title ID.

## Disc Game Post-Processing

For Original Xbox and Xbox 360 downloads, `getGame()` searches the temporary extraction folder for:

1. a file ending in `.iso.001`,
2. otherwise a file ending in `.iso`.

The found filename is appended to the temporary folder path. The final output folder is deleted first, then recreated, then the ISO extractor runs:

```text
extractIso(tempIsoPath, outputFolder)
```

After ISO extraction, the temporary extraction folder is deleted. The ISO split parts are not currently removed in the active code path; there is commented-out cleanup for that.

## Success And Failure

If `getGame()` returns `EXIT_SUCCESS`, `main()` exits with success.

If anything fails, the user sees:

```text
Something went wrong! Press Y to search again, or B to exit
```

The code waits for controller 0 to be available, then polls:

- `B`: exit failure,
- `Y`: break out of the failure prompt and restart the main loop.

## Important Temporary Files

`game:\tmp.7z.001` starts every archive download.

`game:\tmp.7z.002` and later are created automatically if the download crosses the split threshold.

`game:\tmp_output` is used for disc-game 7z extraction.

For XBLA, the computed game-named folder under the XBLA root is used as extraction staging, then deleted after copying the title ID folder.
