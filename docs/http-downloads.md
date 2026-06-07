# HTTP Downloads

`downloadFile.cpp` is responsible for transferring HTTP response bodies after a TLS connection is established.

## Entry Point

The public function is:

```text
downloadFileHTTPS(URL, fileName, dataBuffer, outputBufferSize, downloadIntoFile, printFunction)
```

Parameters:

- `URL`: full HTTPS URL.
- `fileName`: output path when writing to disk.
- `dataBuffer`: either the memory output buffer or a redirect URL output buffer, depending on mode.
- `outputBufferSize`: input capacity and output length for memory/redirect buffers.
- `downloadIntoFile`: `true` streams body to disk; `false` stores body in memory.
- `printFunction`: logging/progress callback, normally `dprintf`.

Return value is an HTTP status code when available. A status of `-1` means a 200 response started but later failed during transfer/write/validation.

## Response Reader

`DumpResponse()` reads decrypted bytes from `XboxTLS_Read()` and handles:

- header buffering,
- HTTP status parsing,
- `Content-Length`,
- `Location` redirects,
- `Transfer-Encoding: chunked`,
- file output,
- memory output,
- split archive output,
- progress reporting,
- final size validation.

It allocates:

- a 4 MB network buffer,
- a 4 MB file buffer for `setvbuf()`,
- a 16 KB header buffer on the stack.

## Header End Detection

`FindHeaderEnd()` in `parsing.cpp` recognizes both:

- CRLF headers ending in `\r\n\r\n`,
- LF-only headers ending in `\n\n`.

`DumpResponse()` copies received bytes into `headerBuffer` until that marker is found. The bytes after the marker are treated as the first body bytes.

If headers exceed 16 KB, the download fails.

## Status And Headers

`parseStatus()` finds the first space in the status line and converts the following digits to an integer.

`parseContentLength()` searches for `Content-Length: ` and converts the following number to `unsigned long long`.

`HeaderContainsToken()` scans header lines case-insensitively for a named header containing a token. `DumpResponse()` uses it to detect:

```text
Transfer-Encoding: chunked
```

The header token parser is case-insensitive but not a full RFC parser; it is tailored to the headers this app needs.

## Redirects

For status `302`, `DumpResponse()` extracts `Location: ` with `parseLocation()`.

Depending on caller mode, it writes the redirect URL to:

- `redirectBuffer`, if provided,
- otherwise `outputBuffer`, when in memory mode,
- otherwise `file`, if file mode supplied an open file.

It then returns `302` as a successful redirect discovery rather than a failed transfer.

The updater uses this behavior to follow up to 5 redirects while downloading release assets. The normal game download path does not follow arbitrary redirects; it mainly relies on the Vimm media URL and primary/secondary domain fallback in `getGame()`.

## Non-200 Status Handling

For non-200 status codes, `DumpResponse()` logs the status. Special handling:

- `429`: explains that Vimm allows only one download at a time and gives the cancel URL.
- `404`: logs page not found.
- `302`: handled as redirect as described above.

Other non-200 statuses fail the download.

## Memory Mode

When `outputBuffer` is non-null, `DumpResponse()` writes response body bytes into memory instead of a file.

`outputBufferPointer` is a global offset reset at the start of each response. `WriteBody()` copies bytes into `outputBuffer + outputBufferPointer` and increments the offset.

Before copying a read block, `DumpResponse()` checks that `totalWritten + r` fits inside the original buffer capacity. After success:

- `*outputBufferSize` becomes the total body bytes written,
- `outputBuffer[totalWritten]` is set to `'\0'`.

This is why HTML/JSON callers can parse the response as a C string.

## File Mode

When memory mode is not used, `DumpResponse()` opens `fileName` in binary write mode and applies the 4 MB file buffer with `setvbuf()`.

The first body bytes that arrived with the headers are written before the function enters the longer read loop.

## Split Download Files

If `fileName` ends in `.001`, file mode enables split output. This is used for the downloaded 7z archive:

```text
game:\tmp.7z.001
```

The split threshold is:

```text
0xF0000000
```

This is below the practical 4 GB FATX/FAT32 limit, leaving margin instead of writing exactly to the maximum.

When a read would overflow the current part:

1. Write only enough bytes to fill the current part.
2. Close it.
3. Increment the numeric suffix with `IncrementSplitFilename()`.
4. Open the next part.
5. Write the remaining bytes from the same read block to the new part.

`IncrementSplitFilename()` walks the trailing digits and increments them with carry. It turns `.001` into `.002`, `.009` into `.010`, etc. If there are no trailing digits or all digits overflow to zero, it fails.

## Chunked Transfer Decoding

`ChunkedDecodeState` has three active states:

- `0`: reading the chunk size line,
- `1`: writing chunk data,
- `2`: consuming the CRLF after chunk data,
- `3`: terminal state after a zero-size chunk.

`ParseChunkSize()` reads a hex number and stops at chunk extensions or whitespace.

`WriteChunkedBody()` feeds decoded body bytes through `WriteBody()`.

Chunked downloads are supported for memory/file output in general, but chunked split-file downloads are explicitly rejected after header processing:

```text
ERROR: chunked split file not supported yet
```

The Vimm archive request asks for identity encoding, so normal large archive downloads are expected to use content length plus connection close, not chunked transfer.

## Progress Reporting

`DumpResponse()` reports rough speed early, then for non-chunked file downloads reports:

- KB downloaded,
- total KB from `Content-Length`,
- recent speed,
- average speed,
- elapsed time,
- remaining time.

The code guards against divide-by-zero cases by falling back to a simpler progress line.

## Completion Validation

At the end, if `Content-Length` was present and `totalWritten != totalContentLength`, the download is considered likely failed.

For a response that initially had status 200, the failure return code becomes `-1`.

If no `Content-Length` is present, the size mismatch check is skipped.
