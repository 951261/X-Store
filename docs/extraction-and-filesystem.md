# Extraction And Filesystem

This area exists because the app processes very large files on a constrained Xbox 360 runtime. The design is stream-first: avoid holding full archives, ISOs, or extracted files in memory.

## 7z Decompression Entry Point

`decompressSevenZipFile(inputFile, outputPath, isXBLA)` is declared in `7zip/decompress7z.h` and implemented in `7zip/decompress7z.c`.

It:

1. Initializes CRC tables.
2. Opens the archive as a joined stream.
3. Wraps it in a `CLookToRead2` look-ahead stream.
4. Opens the 7z database with `SzArEx_Open()`.
5. Calculates total output size for progress.
6. Iterates through every archive entry.
7. Creates directories or streams file contents to disk.
8. Verifies CRC for extracted files.
9. Frees the 7z database and closes streams.

## Joined Split Archive Input

`joined_stream_open()` handles both single-file and split archive inputs.

If the archive path has a numeric suffix like `.001`, it derives the base path and tries to open sequential parts:

```text
tmp.7z.001
tmp.7z.002
tmp.7z.003
...
```

The joined stream exposes those parts as one seekable stream to the 7z SDK. Reads and seeks translate a logical archive offset into the correct part file and offset.

This is how a download split by `DumpResponse()` can still be decompressed as one 7z archive.

## Supported 7z Methods

The streaming extraction path supports:

- Copy/store,
- LZMA,
- LZMA2.

If an archive entry uses an unsupported coder layout, `extract_file_streaming()` returns `SZ_ERROR_UNSUPPORTED` and logs that streaming mode only supports simple per-file blocks using those methods.

This matters because the extractor is intentionally not the full desktop 7z extraction model. It is specialized for stream-to-disk extraction on limited memory.

## 7z Path Conversion

7z stores names as UTF-16. `utf16_to_path()` converts them into path strings and replaces archive separators with the platform path separator.

When `isXBLA` is true, each path component is capped to the FATX-safe name length used by the app. That avoids overly long XBLA paths/files during extraction.

If a non-directory archive entry name ends in `.iso`, the extractor renames it to:

```text
tmp_0.iso
tmp_1.iso
...
```

This gives the later ISO search code predictable filenames and avoids depending on the archive's original ISO name.

## 7z Split Output

`CSplitOutFile` wraps output file writing during 7z extraction.

If an extracted file's expected size is greater than:

```text
0xF0000000
```

then output is split into numbered parts. `write_chunk()` writes up to the remaining space in the current part, advances to the next numbered part, then continues writing.

This is separate from the download split logic. A large downloaded 7z can split into `tmp.7z.001`, and after decompression a large ISO can split into `.iso.001` parts.

## Disc ISO Processing

After 7z extraction, `main.cpp` searches the extraction folder for:

- `.iso.001`,
- otherwise `.iso`.

The ISO extractor entry point is:

```text
extractIso(isoPath, outputFolder)
```

This is a wrapper around the modified extract-xiso code. The active mode is extraction, not list/rewrite/create.

## extract-xiso Flow

At a high level, `extractIso()` and its helpers:

1. Open the ISO through `customOpen()`.
2. Verify the Xbox media header.
3. Detect the correct disc offset.
4. Read the root directory sector and size.
5. Traverse the Xbox ISO directory tree.
6. Create folders and extract files to the current output directory.

`verify_xiso()` checks for `MICROSOFT*XBOX*MEDIA` at the normal header offset first. If that fails, it tries additional offsets used for different disc layouts:

- `GLOBAL_LSEEK_OFFSET`
- `XGD3_LSEEK_OFFSET`
- `XGD1_LSEEK_OFFSET`

The selected offset is stored in `s_xbox_disc_lseek` and applied to later sector seeks.

`traverse_xiso()` walks directory entries. For directories, it creates/enters folders and recurses. For files, it calls `extract_file()`, which seeks to the file sector and streams bytes into an output file.

## Custom File API

`xiso extract/file-stuff.c` provides replacements/wrappers used by extract-xiso:

- `customOpen()`
- `customClose()`
- `customRead()`
- `customLseek()` (used by extract-xiso even though it is not declared in the small summary header)
- `customMkdir()`
- `customForceMkdir()`
- `chdir()`
- `getcwd()`
- `deleteDirectory()`
- `findFile()`

These wrappers adapt desktop-style file behavior to Xbox paths and split files.

## Path Normalization

`resolvePath()` decides whether a path is absolute or relative to the custom current working directory.

`normalizePath()`:

- preserves device prefixes such as `game:`,
- converts separators,
- collapses `.` and `..` segments,
- avoids building paths longer than the fixed buffers.

`chdir()` does not call a native process-wide current directory. It updates a static `currentWorkingDir` used by the custom file API.

## Split ISO Input

`customOpen()` detects paths ending in `.001`. When it sees one, it attempts to open sequential parts:

```text
base.001
base.002
...
base.010
```

It stores descriptors in a small `openFiles` table and returns the first descriptor.

`customRead()` and `customLseek()` then treat the split set as one logical file. The extract-xiso code can read a large split ISO without knowing it is split.

The current implementation supports a bounded number of parts through the fixed `openFiles` table.

## FAT32 Long Name Workaround

`xbox_open()` wraps normal `open()` to handle a specific Xbox/FAT32 filename issue described in the root `how_it_works.md`: paths shaped like `file.abc.def` can be mishandled by the kernel under some circumstances.

`needsFat32LongNameWorkaround()` detects the risky pattern. If needed, `xbox_open()` creates/opens with a temporary name and renames through the FAT32 helper code rather than relying only on the normal kernel path.

The FAT32 helper implementation lives in `xiso extract/FAT32.cpp`. It opens the raw USB mass-storage device, parses the FAT32 boot sector, walks FAT and directory entries, writes long-filename directory entries for the corrected name, and remounts the drive alias. `Fat32RenameUsb1()` selects `Usb1:` / `\Device\Mass1` or `Usb0:` / `\Device\Mass0` based on the path prefix. This helper is not used for normal files; it exists for that narrow rename workaround.

## Recursive Directory Helpers

`customForceMkdir()` creates every missing path segment. It understands device prefixes and normalizes slashes.

`deleteDirectory()` recursively deletes all entries under a directory, then removes the directory itself.

`findFile(folder, out, len, suffix)` scans one directory level and returns the first filename ending in the requested suffix. It is used to find extracted ISOs and updater XEX files.

`IsInvalidFolderChar()` rejects characters that cannot safely appear in generated output folder names:

```text
< > : " / \ | ? * + , =
```

## XBLA Directory Handling

`findXblaTitleIdDir()` in `xblaParsing.cpp` searches an extracted folder tree for exactly one likely Xbox content title ID directory.

A candidate must:

- have a basename of exactly eight hex characters,
- contain at least one known Xbox content type folder.

Known content type folder examples:

- `000D0000`
- `00007000`
- `00004000`
- `00000002`
- `00020000`
- `000B0000`
- `000E0000`
- `02000000`
- `00030000`
- `00009000`

If more than one candidate is found, the scan fails as ambiguous.

`copyDirectory()` recursively copies the chosen title ID folder to the final destination. It refuses to copy if the destination is inside the source, which prevents accidental infinite self-copy.

## Cleanup Expectations

After successful 7z extraction, `main.cpp` deletes downloaded `tmp.7z.xxx` parts.

After XBLA copy, it deletes the XBLA temporary extraction folder.

After disc ISO extraction, it deletes the temporary extraction folder such as `game:\tmp_output`.

The code currently has ISO split-file cleanup commented out after ISO extraction, so if temporary ISO parts remain, that is consistent with the active source.
