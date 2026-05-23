/*
 * decompress7z.c
 *
 * Extracts .7z archives compressed with LZMA or LZMA2 using the official
 * LZMA SDK (https://www.7-zip.org/sdk.html).
 *
 * Assumptions:
 *   - No encryption
 *   - No solid-archive spanning (single-volume only)
 *   - Compression method: LZMA or LZMA2 only
 *
 * Build: see Makefile
 * Usage: decompress7z <archive.7z> [output_dir]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#include "../OutputConsole.h"

#ifdef _WIN32
#  include <direct.h>
#  include <io.h>
#  define PATH_SEP '\\'
#  define mkdir(p, m) _mkdir(p)
#else
#  include <sys/stat.h>
#  include <unistd.h>
#  define PATH_SEP '/'
#endif

/* LZMA SDK headers (from the C/ subdirectory of the SDK) */
#include "7z.h"
#include "7zAlloc.h"
#include "7zBuf.h"
#include "7zCrc.h"
#include "7zFile.h"
#include "7zTypes.h"
#include "LzmaDec.h"
#include "Lzma2Dec.h"

// Input buffer should be large than output buffer (for some reason????)
#define kInputBufSize ((size_t)1024 * (size_t)1024 * (size_t)14) // 10 MB
#define kOutputBufSize ((size_t)1024 * (size_t)1024 * 8)

// #define kInputBufSize ((size_t)1 << 23)
// #define kOutputBufSize ((size_t)1 << 21)

// #define kInputBufSize ((size_t)1 << 21)
// #define kOutputBufSize ((size_t)1 << 19)

#define kMethodCopy 0
#define kMethodLzma 0x030101
#define kMethodLzma2 0x21
#define kProgressBarWidth 30
#define kFat32SplitThreshold ((UInt64)0xF0000000)
#define kMaxPathBuffer 8192
#define kMaxFatxNameLen 40


#define snprintf _snprintf


typedef struct
{
    UInt64 total_bytes;
    UInt64 completed_bytes;
    UInt64 next_update_at;
    UInt64 update_step;
    const char *current_file;
    clock_t last_render;
} CProgressInfo;

typedef struct
{
    CSzFile file;
    UInt64 start_offset;
    UInt64 size;
} CArchivePart;

typedef struct
{
    ISeekInStream vt;
    WRes wres;
    UInt64 pos;
    UInt64 total_size;
    UInt32 num_parts;
    UInt32 current_part;
    CArchivePart *parts;
} CJoinedInStream;

typedef struct
{
    CSzFile file;
    char base_path[kMaxPathBuffer];
    char current_path[kMaxPathBuffer];
    UInt64 current_part_size;
    UInt32 part_index;
    int split_output;
} CSplitOutFile;


unsigned long startTime = 0;

/* -------------------------------------------------------------------------
 * Helpers
 * ---------------------------------------------------------------------- */

static void print_usage(const char *prog)
{
    log_printf( "Usage: %s <archive.7z> [output_dir]\n", prog);
    log_printf( "  archive.7z  - path to the 7z archive\n");
    log_printf( "  output_dir  - destination directory (default: current dir)\n");
}

static void format_size(UInt64 size, char *buf, size_t buf_size)
{
    static const char *const units[] = { "B", "KiB", "MiB", "GiB", "TiB" };
    double value = (double)size;
    unsigned unit_index = 0;

    while (value >= 1024.0 && unit_index + 1 < sizeof(units) / sizeof(units[0])) {
        value /= 1024.0;
        unit_index++;
    }

    snprintf(buf, buf_size, "%.1f %s", value, units[unit_index]);
}

static void progress_render(CProgressInfo *progress, int force)
{
    UInt64 filled;
    char done_buf[32];
    char total_buf[32];
    char file_buf[49];
    double percent;
    clock_t now;

    if (progress->total_bytes == 0)
        return;

    now = clock();
    if (!force) {
        const clock_t min_interval = CLOCKS_PER_SEC * 3;
        if (progress->completed_bytes < progress->next_update_at &&
                now - progress->last_render < min_interval) {
            return;
        }
    }

    if (progress->completed_bytes > progress->total_bytes)
        progress->completed_bytes = progress->total_bytes;

    percent = (100.0 * (double)progress->completed_bytes) /
              (double)progress->total_bytes;
    filled = (progress->completed_bytes * kProgressBarWidth) /
             progress->total_bytes;

    format_size(progress->completed_bytes, done_buf, sizeof(done_buf));
    format_size(progress->total_bytes, total_buf, sizeof(total_buf));

    if (progress->current_file && progress->current_file[0] != '\0') {
        snprintf(file_buf, sizeof(file_buf), "  %-.46s", progress->current_file);
    } else {
        file_buf[0] = '\0';
    }

    //calculate time remaining
    unsigned long elapsedTime = (unsigned long)time(NULL) - startTime;
    unsigned long timeRemaining = (unsigned long)((double)elapsedTime / (percent / 100.00)) - elapsedTime;

    int timeHours = timeRemaining / (60*60);
    int timeMinutes = (timeRemaining % (60*60)) / 60; //minutes
    int timeSec = (timeRemaining % (60*60)) % 60; //seconds

    if(elapsedTime == 0) {
        elapsedTime++;
    }

    // Extract speed in KiB/sec
    unsigned long extractSpeed = (progress->completed_bytes / (1024) ) / elapsedTime;

    // log_printf(
    //         "\r[%.*s%*s] %5.1f%%  %s / %s%s    Time Remaining: %dh, %dm, %ds\n",
    //         (int)filled, "##############################",
    //         (int)(kProgressBarWidth - filled), "",
    //         percent, done_buf, total_buf, file_buf, timeHours, timeMinutes, timeSec);
    // // fflush(stderr);

    //print to display
    dprintf("\r[%.*s%*s] %5.1f%%  %s / %s, %lu KiB/sec, Time Remaining: %dh, %dm, %ds\n",
            (int)filled, "##############################",
            (int)(kProgressBarWidth - filled), "",
            percent, done_buf, total_buf, extractSpeed, timeHours, timeMinutes, timeSec);

    progress->last_render = now;
    progress->next_update_at = progress->completed_bytes + progress->update_step;
}

static void progress_init(CProgressInfo *progress, UInt64 total_bytes)
{
    progress->total_bytes = total_bytes;
    progress->completed_bytes = 0;
    progress->current_file = NULL;
    progress->last_render = 0;
    progress->update_step = total_bytes / 50;
    if (progress->update_step < (UInt64)(16 << 20))
        progress->update_step = (UInt64)(16 << 20);
    progress->next_update_at = 0;
    progress_render(progress, 1);
}

static void progress_advance(CProgressInfo *progress, UInt64 bytes_written,
                             const char *current_file)
{
    if (!progress || progress->total_bytes == 0)
        return;

    progress->current_file = current_file;
    progress->completed_bytes += bytes_written;
    progress_render(progress, 0);
}

static void progress_finish(CProgressInfo *progress, int completed)
{
    if (!progress || progress->total_bytes == 0)
        return;

    if (completed)
        progress->completed_bytes = progress->total_bytes;
    progress_render(progress, 1);
    fputc('\n', stderr);
}

static int is_path_sep(char c)
{
    return c == '/' || c == '\\';
}

static int path_is_current_dir(const char *path)
{
    if (!path || path[0] == '\0')
        return 1;

    if (path[0] == '.' && path[1] == '\0')
        return 1;

    if (path[0] == '.' && is_path_sep(path[1]) && path[2] == '\0')
        return 1;

    return 0;
}

static int has_numeric_volume_suffix(const char *path, char *base_path,
                                     size_t base_path_size)
{
    const char *suffix;
    const char *p;
    size_t base_len;

    if (!path)
        return 0;

    suffix = strrchr(path, '.');
    if (!suffix || suffix == path || suffix[1] == '\0')
        return 0;

    for (p = suffix + 1; *p != '\0'; p++) {
        if (*p < '0' || *p > '9')
            return 0;
    }

    if ((size_t)(p - (suffix + 1)) < 3)
        return 0;

    base_len = (size_t)(suffix - path);
    if (base_len + 1 > base_path_size)
        return 0;

    memcpy(base_path, path, base_len);
    base_path[base_len] = '\0';
    return 1;
}

static int build_numbered_path(const char *base_path, UInt32 part_index,
                               char *path, size_t path_size)
{
    int written = snprintf(path, path_size, "%s.%03u", base_path,
                           (unsigned)part_index);

    if (written < 0 || (size_t)written >= path_size) {
        errno = ENAMETOOLONG;
        return -1;
    }

    return 0;
}

static size_t path_root_len(const char *path)
{
    size_t i;

    if (!path || path[0] == '\0')
        return 0;

    if (is_path_sep(path[0]))
        return 1;

    for (i = 0; path[i] != '\0'; i++) {
        if (path[i] == ':') {
            size_t root_len = i + 1;
            while (is_path_sep(path[root_len]))
                root_len++;
            return root_len;
        }

        if (is_path_sep(path[i]))
            break;
    }

    return 0;
}

static int make_single_dir(const char *path)
{
#ifdef _WIN32
    return _mkdir(path);
#else
    return mkdir(path, 0755);
#endif
}

/*
 * Recursively create every component of `path`.
 * Returns 0 on success, -1 on failure (sets errno).
 */
static int make_dirs(const char *path)
{
    char tmp[4096];
    char *p;
    size_t len;
    size_t root_len;

    if (path_is_current_dir(path))
        return 0;

    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);
    root_len = path_root_len(tmp);

    /* Strip trailing separators, but keep a rooted prefix such as "game:\" intact. */
    while (len > root_len && is_path_sep(tmp[len - 1]))
        tmp[--len] = '\0';

    if (len == 0 || len <= root_len || path_is_current_dir(tmp))
        return 0;

    for (p = tmp + root_len; *p; p++) {
        if (is_path_sep(*p)) {
            *p = '\0';

            if (!path_is_current_dir(tmp) &&
                    make_single_dir(tmp) != 0 &&
                    errno != EEXIST) {
                *p = PATH_SEP;
                return -1;
            }

            *p = PATH_SEP;
        }
    }

    if (make_single_dir(tmp) != 0 && errno != EEXIST)
        return -1;

    return 0;
}

/*
 * Convert a UTF-16LE name (as stored in 7z) to a UTF-8 path string and
 * replace any archive-internal path separators with the OS separator.
 *
 * dst must be at least dst_size bytes.
 * Returns 0 on success.
 */
static int utf16_to_path(const UInt16 *src, char *dst, size_t dst_size, bool isXBLA)
{
    size_t i;
    size_t out = 0;
    size_t component_len = 0;

    for (i = 0; src[i] != 0 && out + 4 < dst_size; i++) {
        UInt16 c = src[i];

        /* Replace '/' and '\' with the OS path separator */
        if (c == '/' || c == '\\') {
            dst[out++] = PATH_SEP;
            component_len = 0;
            continue;
        }

        if (component_len >= kMaxFatxNameLen && isXBLA == true) //truncate file and folder names IF it is an XBLA game
            continue;

        component_len++;

        /* Encode to UTF-8 */
        if (c < 0x80) {
            dst[out++] = (char)c;
        } else if (c < 0x800) {
            dst[out++] = (char)(0xC0 | (c >> 6));
            dst[out++] = (char)(0x80 | (c & 0x3F));
        } else {
            dst[out++] = (char)(0xE0 | (c >> 12));
            dst[out++] = (char)(0x80 | ((c >> 6) & 0x3F));
            dst[out++] = (char)(0x80 | (c & 0x3F));
        }
    }

    dst[out] = '\0';
    return (src[i] == 0) ? 0 : -1; /* -1 if truncated */
}

static int prepare_output_path(const char *path)
{
    char dir[kMaxPathBuffer];
    char *last_sep;
    size_t path_len;

    if (!path) {
        errno = EINVAL;
        return -1;
    }

    path_len = strlen(path);
    if (path_len >= sizeof(dir)) {
        log_printf( "Error: output path is too long: '%s'\n", path);
        errno = ENAMETOOLONG;
        return -1;
    }

    memcpy(dir, path, path_len + 1);
    last_sep = strrchr(dir, PATH_SEP);
    if (last_sep && last_sep != dir) {
        *last_sep = '\0';
        if (make_dirs(dir) != 0 && errno != EEXIST) {
            log_printf( "Error: cannot create directory '%s': %s\n",
                    dir, strerror(errno));
            return -1;
        }
    }

    return 0;
}

static int open_output_path(CSzFile *file, const char *path)
{
    WRes wres;

    if (prepare_output_path(path) != 0)
        return -1;

    wres = OutFile_Open(file, path);
    if (wres != 0) {
        log_printf( "Error: cannot open '%s' for writing (WRes=%u)\n",
                path, (unsigned)wres);
        errno = (int)wres;
        return -1;
    }

    return 0;
}

static int file_exists(const char *path)
{
#ifdef USE_WINDOWS_FILE
    DWORD attrs = GetFileAttributesA(path);
    return attrs != (DWORD)0xFFFFFFFF &&
           (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
#else
    FILE *f = fopen(path, "rb");
    if (!f)
        return 0;
    fclose(f);
    return 1;
#endif
}

static int split_output_open_current(CSplitOutFile *out)
{
    if (out->split_output) {
        if (build_numbered_path(out->base_path, out->part_index,
                                out->current_path, sizeof(out->current_path)) != 0) {
            log_printf( "Error: output path is too long: '%s'\n", out->base_path);
            return -1;
        }
    } else {
        size_t path_len = strlen(out->base_path);
        if (path_len >= sizeof(out->current_path)) {
            log_printf( "Error: output path is too long: '%s'\n", out->base_path);
            errno = ENAMETOOLONG;
            return -1;
        }
        memcpy(out->current_path, out->base_path, path_len + 1);
    }

    if (open_output_path(&out->file, out->current_path) != 0)
        return -1;

    out->current_part_size = 0;
    return 0;
}

static int split_output_open(CSplitOutFile *out, const char *path,
                             UInt64 expected_size)
{
    size_t path_len = strlen(path);

    memset(out, 0, sizeof(*out));
    File_Construct(&out->file);

    if (path_len >= sizeof(out->base_path)) {
        log_printf( "Error: output path is too long: '%s'\n", path);
        errno = ENAMETOOLONG;
        return -1;
    }

    memcpy(out->base_path, path, path_len + 1);
    out->split_output = (expected_size > kFat32SplitThreshold);
    out->part_index = out->split_output ? 1 : 0;

    return split_output_open_current(out);
}

static int split_output_close(CSplitOutFile *out)
{
    WRes wres;

    wres = File_Close(&out->file);
    if (wres != 0) {
        log_printf( "Error: cannot close '%s' (WRes=%u)\n",
                out->current_path, (unsigned)wres);
        File_Construct(&out->file);
        return -1;
    }

    File_Construct(&out->file);
    return 0;
}

static int split_output_advance(CSplitOutFile *out)
{
    if (split_output_close(out) != 0)
        return -1;

    out->part_index++;
    return split_output_open_current(out);
}

static int write_chunk(CSplitOutFile *out, const Byte *data, size_t size)
{
    while (size > 0) {
        size_t chunk = size;

        if (out->split_output) {
            UInt64 remaining = kFat32SplitThreshold - out->current_part_size;

            if (remaining == 0) {
                if (split_output_advance(out) != 0)
                    return -1;
                remaining = kFat32SplitThreshold;
            }

            if ((UInt64)chunk > remaining)
                chunk = (size_t)remaining;
        }

        if (chunk == 0)
            return -1;

        {
            size_t processed = chunk;
            WRes wres = File_Write(&out->file, data, &processed);
            if (wres != 0 || processed != chunk) {
                log_printf( "Error: write failed for '%s' (WRes=%u)\n",
                        out->current_path, (unsigned)wres);
                return -1;
            }
        }

        out->current_part_size += chunk;
        data += chunk;
        size -= chunk;
    }

    return 0;
}

static UInt32 joined_stream_find_part(const CJoinedInStream *stream, UInt64 pos)
{
    UInt32 i;

    if (stream->num_parts == 0)
        return 0;

    if (stream->current_part < stream->num_parts) {
        const CArchivePart *part = &stream->parts[stream->current_part];
        if (pos >= part->start_offset && pos < part->start_offset + part->size)
            return stream->current_part;
    }

    if (pos >= stream->total_size)
        return stream->num_parts - 1;

    for (i = stream->current_part; i < stream->num_parts; i++) {
        UInt64 part_end = stream->parts[i].start_offset + stream->parts[i].size;
        if (pos >= stream->parts[i].start_offset && pos < part_end)
            return i;
    }

    for (i = 0; i < stream->current_part; i++) {
        UInt64 part_end = stream->parts[i].start_offset + stream->parts[i].size;
        if (pos >= stream->parts[i].start_offset && pos < part_end)
            return i;
    }

    return stream->num_parts - 1;
}

static SRes joined_stream_read(ISeekInStreamPtr pp, void *buf, size_t *size)
{
    CJoinedInStream *stream = (CJoinedInStream *)pp;
    Byte *dst = (Byte *)buf;
    size_t remaining = *size;

    *size = 0;

    while (remaining > 0 && stream->pos < stream->total_size) {
        CArchivePart *part;
        UInt64 local_offset;
        UInt64 available;
        size_t chunk;
        size_t processed;
        Int64 seek_pos;

        stream->current_part = joined_stream_find_part(stream, stream->pos);
        part = &stream->parts[stream->current_part];
        local_offset = stream->pos - part->start_offset;
        available = part->size - local_offset;
        chunk = remaining;

        if ((UInt64)chunk > available)
            chunk = (size_t)available;

        seek_pos = (Int64)local_offset;
        stream->wres = File_Seek(&part->file, &seek_pos, SZ_SEEK_SET);
        if (stream->wres != 0)
            return SZ_ERROR_READ;

        processed = chunk;
        stream->wres = File_Read(&part->file, dst, &processed);
        if (stream->wres != 0)
            return SZ_ERROR_READ;

        if (processed == 0)
            break;

        dst += processed;
        remaining -= processed;
        *size += processed;
        stream->pos += processed;
    }

    return SZ_OK;
}

static SRes joined_stream_seek(ISeekInStreamPtr pp, Int64 *pos, ESzSeek origin)
{
    CJoinedInStream *stream = (CJoinedInStream *)pp;
    Int64 new_pos;

    switch ((int)origin) {
        case SZ_SEEK_SET:
            new_pos = *pos;
            break;
        case SZ_SEEK_CUR:
            new_pos = (Int64)stream->pos + *pos;
            break;
        case SZ_SEEK_END:
            new_pos = (Int64)stream->total_size + *pos;
            break;
        default:
            stream->wres = EINVAL;
            return SZ_ERROR_READ;
    }

    if (new_pos < 0) {
        stream->wres = EINVAL;
        return SZ_ERROR_READ;
    }

    stream->pos = (UInt64)new_pos;
    stream->current_part = joined_stream_find_part(stream, stream->pos);
    *pos = new_pos;
    stream->wres = 0;
    return SZ_OK;
}

static void joined_stream_init(CJoinedInStream *stream)
{
    memset(stream, 0, sizeof(*stream));
    stream->vt.Read = joined_stream_read;
    stream->vt.Seek = joined_stream_seek;
}

static int joined_stream_add_part(CJoinedInStream *stream, const char *path)
{
    CArchivePart *parts;
    CArchivePart *part;
    WRes wres;

    parts = (CArchivePart *)realloc(stream->parts,
                                    (stream->num_parts + 1) * sizeof(*parts));
    if (!parts) {
        errno = ENOMEM;
        return -1;
    }
    stream->parts = parts;

    part = &stream->parts[stream->num_parts];
    memset(part, 0, sizeof(*part));
    File_Construct(&part->file);

    wres = InFile_Open(&part->file, path);
    if (wres != 0) {
        log_printf( "Error: cannot open archive part '%s' (WRes=%u)\n",
                path, (unsigned)wres);
        return -1;
    }

    wres = File_GetLength(&part->file, &part->size);
    if (wres != 0) {
        log_printf( "Error: cannot get size of archive part '%s' (WRes=%u)\n",
                path, (unsigned)wres);
        File_Close(&part->file);
        File_Construct(&part->file);
        return -1;
    }

    part->start_offset = stream->total_size;
    stream->total_size += part->size;
    stream->num_parts++;
    return 0;
}

static void joined_stream_close(CJoinedInStream *stream)
{
    UInt32 i;

    for (i = 0; i < stream->num_parts; i++)
        File_Close(&stream->parts[i].file);

    free(stream->parts);
    memset(stream, 0, sizeof(*stream));
    stream->vt.Read = joined_stream_read;
    stream->vt.Seek = joined_stream_seek;
}

static int joined_stream_open(CJoinedInStream *stream, const char *archive_path)
{
    char base_path[kMaxPathBuffer];

    joined_stream_init(stream);

    if (has_numeric_volume_suffix(archive_path, base_path, sizeof(base_path))) {
        UInt32 part_index;

        for (part_index = 1; ; part_index++) {
            char volume_path[kMaxPathBuffer];

            if (build_numbered_path(base_path, part_index,
                                    volume_path, sizeof(volume_path)) != 0)
                return -1;

            if (!file_exists(volume_path)) {
                if (part_index == 1)
                    return -1;
                break;
            }

            if (joined_stream_add_part(stream, volume_path) != 0)
                return -1;
        }

        return (stream->num_parts > 0) ? 0 : -1;
    }

    return joined_stream_add_part(stream, archive_path);
}

static SRes decode_copy_to_file(ILookInStreamPtr in_stream, UInt64 in_size,
                                CSplitOutFile *out_file, UInt32 *crc,
                                CProgressInfo *progress,
                                const char *current_file)
{
    while (in_size > 0) {
        const void *in_buf = NULL;
        size_t chunk = kInputBufSize;

        if ((UInt64)chunk > in_size)
            chunk = (size_t)in_size;

        RINOK(ILookInStream_Look(in_stream, &in_buf, &chunk));
        if (chunk == 0)
            return SZ_ERROR_INPUT_EOF;

        if (write_chunk(out_file, (const Byte *)in_buf, chunk) != 0)
            return SZ_ERROR_WRITE;

        *crc = CrcUpdate(*crc, in_buf, chunk);
        progress_advance(progress, chunk, current_file);
        in_size -= chunk;
        RINOK(ILookInStream_Skip(in_stream, chunk));
    }

    return SZ_OK;
}

static SRes decode_lzma_to_file(const Byte *props, unsigned props_size,
                                ILookInStreamPtr in_stream, UInt64 in_size,
                                UInt64 out_size, CSplitOutFile *out_file,
                                ISzAllocPtr alloc_main, UInt32 *crc,
                                CProgressInfo *progress,
                                const char *current_file)
{
    CLzmaDec state;
    Byte *out_buf = NULL;
    SRes res = SZ_OK;

    LzmaDec_CONSTRUCT(&state);
    RINOK(LzmaDec_Allocate(&state, props, props_size, alloc_main));
    LzmaDec_Init(&state);

    out_buf = (Byte *)ISzAlloc_Alloc(alloc_main, kOutputBufSize);
    if (!out_buf) {
        LzmaDec_Free(&state, alloc_main);
        return SZ_ERROR_MEM;
    }

    for (;;) {
        const void *in_buf = NULL;
        size_t lookahead = kInputBufSize;
        SizeT in_processed;
        SizeT out_processed = kOutputBufSize;
        ELzmaFinishMode finish_mode = LZMA_FINISH_ANY;
        ELzmaStatus status;

        if ((UInt64)lookahead > in_size)
            lookahead = (size_t)in_size;

        RINOK(ILookInStream_Look(in_stream, &in_buf, &lookahead));

        in_processed = (SizeT)lookahead;
        if ((UInt64)out_processed > out_size) {
            out_processed = (SizeT)out_size;
            finish_mode = LZMA_FINISH_END;
        }

        res = LzmaDec_DecodeToBuf(&state, out_buf, &out_processed,
                                  (const Byte *)in_buf, &in_processed,
                                  finish_mode, &status);

        if (out_processed != 0) {
            if (write_chunk(out_file, out_buf, out_processed) != 0) {
                res = SZ_ERROR_WRITE;
                break;
            }
            *crc = CrcUpdate(*crc, out_buf, out_processed);
            progress_advance(progress, out_processed, current_file);
            out_size -= out_processed;
        }

        in_size -= in_processed;

        if (in_processed != 0) {
            SRes skip_res = ILookInStream_Skip(in_stream, in_processed);
            if (skip_res != SZ_OK) {
                res = skip_res;
                break;
            }
        }

        if (res != SZ_OK)
            break;

        if (status == LZMA_STATUS_FINISHED_WITH_MARK) {
            if (out_size != 0 || in_size != 0)
                res = SZ_ERROR_DATA;
            break;
        }

        if (out_size == 0 && in_size == 0 &&
                status == LZMA_STATUS_MAYBE_FINISHED_WITHOUT_MARK)
            break;

        if (in_processed == 0 && out_processed == 0) {
            res = SZ_ERROR_DATA;
            break;
        }
    }

    ISzAlloc_Free(alloc_main, out_buf);
    LzmaDec_Free(&state, alloc_main);
    return res;
}

static SRes decode_lzma2_to_file(const Byte *props, unsigned props_size,
                                 ILookInStreamPtr in_stream, UInt64 in_size,
                                 UInt64 out_size, CSplitOutFile *out_file,
                                 ISzAllocPtr alloc_main, UInt32 *crc,
                                 CProgressInfo *progress,
                                 const char *current_file)
{
    CLzma2Dec state;
    Byte *out_buf = NULL;
    SRes res = SZ_OK;

    if (props_size != 1)
        return SZ_ERROR_DATA;

    Lzma2Dec_CONSTRUCT(&state);
    RINOK(Lzma2Dec_Allocate(&state, props[0], alloc_main));
    Lzma2Dec_Init(&state);

    out_buf = (Byte *)ISzAlloc_Alloc(alloc_main, kOutputBufSize);
    if (!out_buf) {
        Lzma2Dec_Free(&state, alloc_main);
        return SZ_ERROR_MEM;
    }

    for (;;) {
        const void *in_buf = NULL;
        size_t lookahead = kInputBufSize;
        SizeT in_processed;
        SizeT out_processed = kOutputBufSize;
        ELzmaFinishMode finish_mode = LZMA_FINISH_ANY;
        ELzmaStatus status;

        if ((UInt64)lookahead > in_size)
            lookahead = (size_t)in_size;

        RINOK(ILookInStream_Look(in_stream, &in_buf, &lookahead));

        in_processed = (SizeT)lookahead;
        if ((UInt64)out_processed > out_size) {
            out_processed = (SizeT)out_size;
            finish_mode = LZMA_FINISH_END;
        }

        res = Lzma2Dec_DecodeToBuf(&state, out_buf, &out_processed,
                                   (const Byte *)in_buf, &in_processed,
                                   finish_mode, &status);

        if (out_processed != 0) {
            if (write_chunk(out_file, out_buf, out_processed) != 0) {
                res = SZ_ERROR_WRITE;
                break;
            }
            *crc = CrcUpdate(*crc, out_buf, out_processed);
            progress_advance(progress, out_processed, current_file);
            out_size -= out_processed;
        }

        in_size -= in_processed;

        if (in_processed != 0) {
            SRes skip_res = ILookInStream_Skip(in_stream, in_processed);
            if (skip_res != SZ_OK) {
                res = skip_res;
                break;
            }
        }

        if (res != SZ_OK)
            break;

        if (status == LZMA_STATUS_FINISHED_WITH_MARK) {
            if (out_size != 0 || in_size != 0)
                res = SZ_ERROR_DATA;
            break;
        }

        if (in_processed == 0 && out_processed == 0) {
            res = SZ_ERROR_DATA;
            break;
        }

        if (out_size == 0 && in_size == 0)
            break;
    }

    ISzAlloc_Free(alloc_main, out_buf);
    Lzma2Dec_Free(&state, alloc_main);
    return res;
}

static SRes extract_file_streaming(const CSzArEx *db, ILookInStreamPtr in_stream,
                                   UInt32 file_index, const char *full_path,
                                   const char *display_name,
                                   ISzAllocPtr alloc_main,
                                   CProgressInfo *progress)
{
    const UInt32 folder_index = db->FileToFolder[file_index];
    CSplitOutFile out_file;
    UInt32 crc = CRC_INIT_VAL;
    SRes res = SZ_OK;

    memset(&out_file, 0, sizeof(out_file));

    if (folder_index == (UInt32)-1) {
        if (split_output_open(&out_file, full_path, 0) != 0)
            return SZ_ERROR_WRITE;
        if (split_output_close(&out_file) != 0)
            return SZ_ERROR_WRITE;
        return SZ_OK;
    }

    {
        const UInt32 folder_file_index = db->FolderToFile[folder_index];
        const UInt32 next_folder_file_index = db->FolderToFile[folder_index + 1];
        const Byte *folder_data;
        const UInt64 *pack_positions;
        const CSzCoderInfo *coder;
        CSzFolder folder;
        CSzData sd;
        UInt64 out_size = SzArEx_GetFileSize(db, file_index);
        UInt64 in_size;
        UInt64 offset;

        if (folder_file_index != file_index || next_folder_file_index != file_index + 1)
            return SZ_ERROR_UNSUPPORTED;

        folder_data = db->db.CodersData + db->db.FoCodersOffsets[folder_index];
        sd.Data = folder_data;
        sd.Size = db->db.FoCodersOffsets[(size_t)folder_index + 1] -
                  db->db.FoCodersOffsets[folder_index];
        RINOK(SzGetNextFolderItem(&folder, &sd));

        if (sd.Size != 0 ||
                folder.UnpackStream != db->db.FoToMainUnpackSizeIndex[folder_index] ||
                folder.NumCoders != 1 ||
                folder.NumPackStreams != 1)
            return SZ_ERROR_UNSUPPORTED;

        coder = &folder.Coders[0];
        pack_positions = db->db.PackPositions + db->db.FoStartPackStreamIndex[folder_index];
        offset = pack_positions[0];
        in_size = pack_positions[1] - offset;

        if (split_output_open(&out_file, full_path, out_size) != 0)
            return SZ_ERROR_WRITE;

        RINOK(LookInStream_SeekTo(in_stream, db->dataPos + offset));

        switch (coder->MethodID) {
            case kMethodCopy:
                if (in_size != out_size)
                    res = SZ_ERROR_DATA;
                else
                    res = decode_copy_to_file(in_stream, in_size, &out_file, &crc,
                                              progress, display_name);
                break;
            case kMethodLzma:
                res = decode_lzma_to_file(folder_data + coder->PropsOffset,
                                          coder->PropsSize, in_stream, in_size,
                                          out_size, &out_file, alloc_main, &crc,
                                          progress, display_name);
                break;
            case kMethodLzma2:
                res = decode_lzma2_to_file(folder_data + coder->PropsOffset,
                                           coder->PropsSize, in_stream, in_size,
                                           out_size, &out_file, alloc_main, &crc,
                                           progress, display_name);
                break;
            default:
                res = SZ_ERROR_UNSUPPORTED;
                break;
        }
    }

    if (split_output_close(&out_file) != 0 && res == SZ_OK)
        res = SZ_ERROR_WRITE;

    if (res == SZ_OK && SzBitWithVals_Check(&db->CRCs, file_index)) {
        crc = CRC_GET_DIGEST(crc);
        if (crc != db->CRCs.Vals[file_index])
            res = SZ_ERROR_CRC;
    }

    return res;
}


static int ascii_tolower(int c)
{
    if (c >= 'A' && c <= 'Z')
        return c + ('a' - 'A');

    return c;
}

static int ends_with_case_insensitive(const char *text, const char *suffix)
{
    size_t text_len;
    size_t suffix_len;
    size_t offset;
    size_t i;

    if (!text || !suffix)
        return 0;

    text_len = strlen(text);
    suffix_len = strlen(suffix);

    if (suffix_len > text_len)
        return 0;

    offset = text_len - suffix_len;

    for (i = 0; i < suffix_len; i++) {
        if (ascii_tolower((unsigned char)text[offset + i]) !=
            ascii_tolower((unsigned char)suffix[i])) {
            return 0;
        }
    }

    return 1;
}

/* -------------------------------------------------------------------------
 * Main extraction logic
 * ---------------------------------------------------------------------- */

int decompressSevenZipFile(const char *inputFile, const char *outputPath, const bool isXBLA)
{
    const char *archive_path;
    const char *out_dir;

    CJoinedInStream archive_stream; /* Handles single and split archives */
    CLookToRead2   look_stream;   /* Buffered look-ahead stream      */
    CSzArEx        db;            /* 7z archive database             */

    ISzAlloc alloc_imp      = { SzAlloc,     SzFree     };
    ISzAlloc alloc_temp_imp = { SzAllocTemp, SzFreeTemp };

    SRes res;
    UInt32 i;
    UInt64 total_bytes = 0;

    int errors = 0;
    CProgressInfo progress;

    // /* ---- argument parsing ---- */
    // if (argc < 2 || argc > 3) {
    //     print_usage(argv[0]);
    //     return EXIT_FAILURE;
    // }

    //get the start time of the code
    
    startTime = (unsigned long)time(NULL);
    
    archive_path = inputFile; //argv[1];

    out_dir = (outputPath && outputPath[0] != '\0') ? outputPath : ".";

    /* ---- initialise CRC tables (required by the SDK) ---- */
    CrcGenerateTable();

    /* ---- open the archive file ---- */
    joined_stream_init(&archive_stream);
    if (joined_stream_open(&archive_stream, archive_path) != 0)
        return EXIT_FAILURE;

    /*
     * Wrap the raw file stream in a look-ahead buffer.
     */
    LookToRead2_CreateVTable(&look_stream, False);
    look_stream.buf = NULL; /* will be allocated by Init */
    {
        /* Allocate the look-ahead buffer manually so we can free it later */
        look_stream.buf = (Byte *)ISzAlloc_Alloc(&alloc_imp, kInputBufSize);
        if (!look_stream.buf) {
            log_printf( "Error: out of memory\n");
            joined_stream_close(&archive_stream);
            return EXIT_FAILURE;
        }
        look_stream.bufSize = kInputBufSize;
    }
    look_stream.realStream = &archive_stream.vt;
    LookToRead2_INIT(&look_stream);

    /* ---- parse the 7z header ---- */
    SzArEx_Init(&db);
    res = SzArEx_Open(&db, &look_stream.vt, &alloc_imp, &alloc_temp_imp);
    if (res != SZ_OK) {
        log_printf( "Error: SzArEx_Open failed (SRes=%d).\n"
                        "  The file may not be a valid .7z archive, or it may use\n"
                        "  encryption / a compression method other than LZMA/LZMA2.\n",
                res);
        errors = 1;
        goto cleanup;
    }

    log_printf("Archive : %s\n", archive_path);
    log_printf("Files   : %u\n", (unsigned)db.NumFiles);
    log_printf("Output  : %s\n\n", out_dir);

    for (i = 0; i < db.NumFiles; i++) {
        if (!SzArEx_IsDir(&db, i))
            total_bytes += SzArEx_GetFileSize(&db, i);
    }

    progress_init(&progress, total_bytes);

    /* ---- iterate over every entry in the archive ---- */
    for (i = 0; i < db.NumFiles; i++) {

        char    name_buf[4096];
        char    full_path[8192];
        int     is_dir;
        size_t  utf16_len;
        UInt16 *utf16_name = NULL;
        UInt64  file_size;

        /* Build the output path from the UTF-16 name stored in the archive */
        utf16_len = SzArEx_GetFileNameUtf16(&db, i, NULL);
        utf16_name = (UInt16 *)ISzAlloc_Alloc(&alloc_imp,
                                              utf16_len * sizeof(*utf16_name));
        if (!utf16_name) {
            log_printf( "Error: out of memory while reading file name\n");
            errors++;
            continue;
        }

        SzArEx_GetFileNameUtf16(&db, i, utf16_name);
        if (utf16_to_path(utf16_name, name_buf, sizeof(name_buf), isXBLA) != 0) {
            log_printf( "Error: file name is too long to extract entry %u\n",
                    (unsigned)i);
            ISzAlloc_Free(&alloc_imp, utf16_name);
            errors++;
            continue;
        }

        ISzAlloc_Free(&alloc_imp, utf16_name);

        is_dir = SzArEx_IsDir(&db, i);
        file_size = SzArEx_GetFileSize(&db, i);

        if (!is_dir && ends_with_case_insensitive(name_buf, ".iso")) {
            static int isoNum = 0;
            sprintf(name_buf, "tmp_%d.iso", isoNum);
            isoNum++;
        }

        snprintf(full_path, sizeof(full_path), "%s%c%s", out_dir, PATH_SEP, name_buf);

        if (is_dir) {
            /* Create directory entry */
            log_printf("  d  %s\n", name_buf);

            if (make_dirs(full_path) != 0 && errno != EEXIST) {
                log_printf( "Warning: cannot create directory '%s': %s\n",
                        full_path, strerror(errno));
            }
            continue;
        }

        /* ---- decompress the file directly to disk ---- */
        res = extract_file_streaming(&db, &look_stream.vt, i, full_path,
                                     name_buf, &alloc_imp, &progress);
        if (res != SZ_OK) {
            log_printf( "Error: failed to extract '%s' (SRes=%d)\n",
                    name_buf, res);
            if (res == SZ_ERROR_UNSUPPORTED) {
                log_printf(
                        "  Streaming mode supports archives where each file has its own\n"
                        "  folder/block and uses Copy, LZMA, or LZMA2 with no extra filters.\n");
            }
            errors++;
            continue;
        }

        printf("  x  %s  (%llu bytes)\n",
               name_buf, (unsigned long long)file_size);
    }

    progress_finish(&progress, errors == 0);

    printf("\n%s  (%d error%s)\n",
           errors ? "Done with errors" : "Done",
           errors,
           errors == 1 ? "" : "s");

cleanup:
    /* Free the archive database */
    SzArEx_Free(&db, &alloc_imp);

    /* Free the look-ahead buffer */
    ISzAlloc_Free(&alloc_imp, look_stream.buf);

    /* Close the file */
    joined_stream_close(&archive_stream);

    return errors ? EXIT_FAILURE : EXIT_SUCCESS;
}
