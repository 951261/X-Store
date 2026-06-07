# Project Layout

The repository is a Visual Studio 2010 Xbox 360 solution named `free60 store.sln`. The main project is `XboxTLS2.vcxproj`, which builds the X Store XEX and references several helper projects.

## Solution Projects

`XboxTLS2.vcxproj` is the main executable project. It contains the app entry point, settings handling, HTTPS download logic, XboxTLS wrapper, DNS helper, logging, XBLA post-processing, and embedded BearSSL sources.

`Common/AtgFramework2010.vcxproj` builds the ATG framework. This is Microsoft helper/sample code. The app uses it for the on-screen console and some XDK convenience behavior, but the internals are not part of this documentation.

`user interface/user interface.vcxproj` builds the text-mode UI and Vimm's Lair HTML parsing logic.

`7zip/7z decompress.vcxproj` builds the custom 7z streaming extractor. Most files are LZMA SDK / 7-Zip SDK code, with `decompress7z.c` acting as the app-specific integration layer.

`xiso extract/xiso extract.vcxproj` builds the ISO extraction layer. `extract-xiso.c` is a ported and heavily modified extract-xiso implementation. `file-stuff.c` is the app's custom filesystem wrapper layer for Xbox paths, split ISO reads, recursive directory operations, and filename workarounds.

`updater/updater.vcxproj` builds the self-update logic. It uses `cJSON` for GitHub release JSON and `miniz` for ZIP extraction.

`miniz/miniz.vcxproj` builds miniz, used only by the updater ZIP extraction path.

## App-Owned Source Areas

`main.cpp` coordinates the whole user-facing flow: mount checks, console setup, UI selection, settings parsing, output path selection, download/extract processing, and retry prompts.

`downloadFile.cpp` implements HTTPS downloads and HTTP response handling on top of XboxTLS. It can stream to a file or store an HTML/JSON response in a caller-provided memory buffer.

`XboxTLS.cpp` and `XboxTLS.h` wrap BearSSL into a small Xbox-friendly TLS 1.2 client API. This code owns context allocation, trust anchor insertion, socket setup, BearSSL engine setup, encrypted reads/writes, and cleanup.

`dns.cpp` wraps `XNetDnsLookup()` with guarded failure handling and includes a tiny static fallback cache.

`parsing.cpp` contains low-level parsing helpers for URLs, HTTP headers, transfer-encoding detection, chunk sizes, and split filename incrementing.

`OutputConsole.cpp` and `Debug.cpp` provide logging to the ATG console, stdout/debug output, and `game:\DebugInfo.txt`.

`xblaParsing.cpp` contains two XBLA helpers: recursive directory copy and recursive scanning for a valid title ID content folder.

`Corona4G.c` / `Corona4G.h` come from a broader Simple 360 NAND Flasher-style helper set. X Store uses the `mount()` wrapper from this file to create symbolic drive aliases such as `game:`, `Usb0:`, `Usb1:`, and `Hdd:`. The NAND read/write helpers are not part of the X Store download pipeline.

`settings.h` stores global constants: current version, buffer/path limits, Vimm download domains, and FATX-safe folder name length.

## Embedded Libraries And Vendor Code

`SSL/` is BearSSL source. The project compiles BearSSL directly into the main binary. The docs explain how `XboxTLS.cpp` uses BearSSL, not BearSSL internals.

`Common/` is ATG/XDK sample framework code. It is treated as a black box here.

`7zip/` is mostly LZMA SDK / 7-Zip SDK code. The app-specific behavior to understand is concentrated in `decompress7z.c`.

`xiso extract/extract-xiso.c` started as extract-xiso and is modified for Xbox 360. The docs cover its integration points and custom behavior, not every AVL/tree or ISO-format routine.

`xiso extract/FAT32.cpp` is a low-level FAT32 rename helper used by `file-stuff.c` for a USB filename edge case. It reads/writes FAT32 directory entries directly and remounts the drive alias after a rename.

`miniz/` and `updater/cJSON.*` are third-party libraries used by the updater.

`xiso extract/win32/` contains portability helpers for dirent/getopt/asprintf behavior.

## Build Artifacts

The repository includes generated/debug/build output under places such as `Common/Release_LTCG/`. Those files are not part of the source-level architecture. They should not be used as documentation sources unless diagnosing a build issue.

## Runtime Files And Paths

`game:\settings.txt` is read by `main.cpp`.

`game:\DebugInfo.txt` is the main log file.

`game:\tmp.7z.001`, `game:\tmp.7z.002`, and so on are temporary downloaded archive parts.

`game:\tmp_output` is the temporary extraction folder for Original Xbox and Xbox 360 ISO workflows.

`game:\tmp_update` is the updater's temporary folder.

`game:\XStoreUp.xex` is the temporary downloaded replacement XEX used during update installation.
