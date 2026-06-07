# UI And Vimm Parsing

The UI module is in `user interface/ui.cpp`. The Vimm parsing module is in `user interface/vimms_lair.cpp`.

## Download Type Menu

`ShowDownloadTypeMenu()` displays four entries:

- Original Xbox
- Xbox 360
- XBLA, DLC and Title Updates
- Update X-Store

The menu uses `XInputGetState()` for controller input. It tracks `previousButtons` and computes `pressed = buttons & ~previousButtons`, so holding a button does not repeatedly trigger selection/movement every frame.

`RenderDownloadTypeMenu()` builds a complete text buffer and sends it with one `dprintf()` call. This is done to reduce console flicker.

If the user presses `B`, the function returns `0`. Since valid download types start at `1`, `showUI()` treats `0` as cancel/failure.

## Startup Button Release Guard

Before showing the menu, `showUI()` waits until controller 0 has no buttons pressed. This prevents an input used to launch or return to the app from accidentally selecting an option.

While buttons remain pressed, it prints:

```text
Please release all controller buttons
```

## Updater Selection

If the chosen download type is `AUTO_UPDATE`, `showUI()` calls `runUpdate()` and returns `-1`.

That means the updater path never returns a game URL to `main()`. A successful update launches the new XEX from inside updater code. If update fails or returns, `main()` goes to its failure prompt.

## Keyboard Input

`OpenKeyboardToString()` wraps `XShowKeyboardUI()`.

The Xbox keyboard returns `WCHAR` text. The helper converts it to a narrow `std::string` using `WideToCharSimple()`, which preserves characters up to `0xFF` and replaces anything wider with `?`.

The keyboard is opened with:

- title: `Search for a game`
- description: `Search for a game here`
- default text: `GTA VI`

The call uses an `XOVERLAPPED` event and blocks with `XGetOverlappedResult(..., TRUE)`. If the user cancels, the extended error is not `ERROR_SUCCESS`, and the function returns that error.

`showUI()` treats an empty search string as failure.

## Search URL Construction

`UrlEncodeQuery()` is a small query encoder:

- alphanumeric, `-`, `_`, `.`, and `~` are left unchanged,
- spaces become `+`,
- all other bytes become `%XX`.

The base search URL depends on download type:

- Original Xbox: `https://vimm.net/vault/?p=list&system=Xbox&q=`
- Xbox 360: `https://vimm.net/vault/?p=list&system=Xbox360&q=`
- XBLA/DLC/TU: `https://vimm.net/vault/?p=list&system=X360-D&q=`

The encoded keyboard text is appended to that base URL.

## Search HTML Download

`showUI()` allocates a 4 MB HTML buffer and calls:

```text
downloadFileHTTPS(searchURL, "", buffer, &OUTPUT_BUFFER_SIZE, false, dprintf)
```

Because `downloadIntoFile` is `false`, `downloadFileHTTPS()` stores the HTTP response body in memory and null-terminates it.

If the status is not 200, search fails.

## Search Result Parsing

`parse_vimm_search_results()` starts near the phrase `Search results for` if present. It stops before `id="showFilterTable"` if present. Inside that region, it scans each `<tr>...</tr>` row.

For each row, `parse_result_row()`:

- finds the first table cell,
- attempts to extract a content type from a `redBorder` marker/title,
- reads region information from the next cell,
- reads version information from a following cell when it looks numeric,
- finds valid game links inside the first cell.

A valid game link must be an anchor href shaped like:

```text
/vault/<digits>
```

The parser intentionally ignores anchors that do not match that pattern.

## Search Result Display Names

The parser decodes a limited set of HTML entities:

- `&amp;`
- `&quot;`
- `&#39;`
- `&#039;`
- `&lt;`
- `&gt;`
- `&nbsp;`

It strips HTML tags and trims whitespace.

The stored display name may include extra context:

```text
Name (Type) (Region) (Version X)
```

The type is recognized for values such as `Addon`, `DLC`, `Title Update`, and `Xbox Live Arcade`. The code also accepts the typo `Xbox Live Archade`.

`GameList` is a growable C array. `push_game()` starts at capacity 8 and doubles when full. `free_game_list()` frees every name/link and resets the struct.

## Search Results UI

`ShowSearchResultsUI()` renders up to 20 visible rows. It keeps separate `selected` and `scroll` values:

- D-pad up/down changes `selected`.
- `scroll` follows when `selected` moves outside the visible window.
- `A` returns the selected index.
- `B` returns `-1`.

If there are no results, the UI displays `No games found` and waits for `B`.

## Selected Game Page

Once the user picks a search result, `showUI()` builds:

```text
https://vimm.net + selected relative link
```

It copies the selected display name back to the caller, clears the console, and downloads the selected game page into the same HTML buffer.

## Media ID Parsing

`parse_vimm_media_ids()` primarily reads Vimm's JavaScript `let media = [...]` data. It scans each entry for:

- `"ID"`: required media ID digits,
- `"SortOrder"`: used as disc number,
- `"VersionString"`: preferred version text,
- `"Version"`: numeric fallback version.

If the media array path finds no entries, it falls back to a hidden input search around `mediaId` and `value`.

`MediaList` is a growable C array. `push_media()` starts at capacity 4 and doubles when full. It defaults missing disc to `1` and missing version to `1.0`.

## Media Selection UI

If there is exactly one media entry, `ShowMediaResultsUI()` automatically returns index 0.

If there are multiple entries, it displays:

```text
Selected Game Disc <disc> version <version>
```

The same edge-triggered D-pad/A/B logic is used.

## Final Download URL

The final game archive URL is:

```text
DOWNLOAD_DOMAIN "/?mediaId=" + selected media ID
```

With the current constants, that means:

```text
https://dl2.vimm.net/?mediaId=<id>
```

`main.cpp` later retries against `SECONDARY_DOWNLOAD_DOMAIN` for certain failed primary-domain responses.
