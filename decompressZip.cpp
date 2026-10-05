#include "stdafx.h"
#include "decompressZip.h"
#include "SplitFile.h"
#include "OutputConsole.h"
#include "miniz/miniz.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

namespace
{
    const unsigned __int64 FATX_SAFE_FILE_SIZE = 0xF0000000ULL;
    const size_t XBLA_MAX_NAME_LENGTH = 40;
    const int ZIP_PROGRESS_BAR_WIDTH = 30;
    const DWORD ZIP_PROGRESS_INTERVAL_MS = 3000;

    enum ZipProgressState
    {
        ZIP_PROGRESS_RUNNING,
        ZIP_PROGRESS_COMPLETE,
        ZIP_PROGRESS_FAILED
    };

    struct ZipProgress
    {
        unsigned __int64 totalBytes;
        unsigned __int64 completedBytes;
        unsigned __int64 elapsedMs;
        DWORD lastTick;
        DWORD lastRenderTick;
    };

    void FormatZipSize(double bytes, char *buffer, size_t bufferSize)
    {
        static const char *const units[] = {
            "B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB"
        };
        size_t unit = 0;
        while (bytes >= 1024.0 && unit + 1 < sizeof(units) / sizeof(units[0]))
        {
            bytes /= 1024.0;
            ++unit;
        }
        _snprintf(buffer, bufferSize, "%.1f %s", bytes, units[unit]);
        buffer[bufferSize - 1] = '\0';
    }

    void RenderZipProgress(ZipProgress *progress, bool force,
                           ZipProgressState state = ZIP_PROGRESS_RUNNING)
    {
        DWORD now = GetTickCount();
        // Accumulate unsigned tick differences so the 32-bit clock can wrap.
        progress->elapsedMs += (DWORD)(now - progress->lastTick);
        progress->lastTick = now;
        if (!force && (DWORD)(now - progress->lastRenderTick) <
                      ZIP_PROGRESS_INTERVAL_MS)
            return;

        unsigned __int64 remaining = progress->totalBytes -
                                     progress->completedBytes;
        double percent = progress->totalBytes ?
            100.0 * (double)progress->completedBytes /
                    (double)progress->totalBytes : 0.0;
        // CRC validation and file flushes must succeed before showing 100%.
        if (state == ZIP_PROGRESS_COMPLETE)
            percent = 100.0;
        else if (percent > 99.9)
            percent = 99.9;
        int filled = (int)(percent * ZIP_PROGRESS_BAR_WIDTH / 100.0);

        double speed = progress->elapsedMs ?
            (double)progress->completedBytes * 1000.0 /
            (double)progress->elapsedMs : 0.0;
        char extractedText[32], totalText[32], remainingText[32], speedText[32];
        FormatZipSize((double)progress->completedBytes, extractedText,
                      sizeof(extractedText));
        FormatZipSize((double)progress->totalBytes, totalText, sizeof(totalText));
        FormatZipSize((double)remaining, remainingText, sizeof(remainingText));
        FormatZipSize(speed, speedText, sizeof(speedText));

        char etaText[32] = "--:--:--";
        if (state == ZIP_PROGRESS_COMPLETE)
            strcpy(etaText, "00:00:00");
        else if (state == ZIP_PROGRESS_RUNNING && remaining == 0)
            strcpy(etaText, "finishing");
        else if (state == ZIP_PROGRESS_RUNNING && speed > 0.0)
        {
            double seconds = (double)remaining / speed;
            // Bound the conversion to the XDK's 32-bit unsigned long.
            if (seconds > 99999.0 * 3600.0)
                strcpy(etaText, ">99999 h");
            else
            {
                unsigned long etaSeconds = (unsigned long)seconds;
                if ((double)etaSeconds < seconds)
                    ++etaSeconds;
                _snprintf(etaText, sizeof(etaText), "%lu:%02lu:%02lu",
                          etaSeconds / 3600UL, (etaSeconds / 60UL) % 60UL,
                          etaSeconds % 60UL);
                etaText[sizeof(etaText) - 1] = '\0';
            }
        }

        const char *status = state == ZIP_PROGRESS_COMPLETE ? " Complete" :
                             state == ZIP_PROGRESS_FAILED ? " Failed" : "";
        dprintf("[%.*s%*s] %5.1f%%%s\n"
                "Extracted: %s / %s  Remaining: %s\n"
                "Speed: %s/s  ETA: %s\n",
                filled, "##############################",
                ZIP_PROGRESS_BAR_WIDTH - filled, "", percent, status,
                extractedText, totalText, remainingText, speedText, etaText);
        progress->lastRenderTick = now;
    }

    void InitializeZipProgress(ZipProgress *progress, unsigned __int64 total)
    {
        progress->totalBytes = total;
        progress->completedBytes = 0;
        progress->elapsedMs = 0;
        progress->lastTick = GetTickCount();
        progress->lastRenderTick = progress->lastTick;
        RenderZipProgress(progress, true);
    }

    void AdvanceZipProgress(ZipProgress *progress, size_t written)
    {
        unsigned __int64 remaining = progress->totalBytes -
                                     progress->completedBytes;
        progress->completedBytes += (unsigned __int64)written < remaining ?
                                   (unsigned __int64)written : remaining;
        RenderZipProgress(progress, false);
    }

    bool ReportError(const char *message, const char *path)
    {
        dprintf("ZIP extraction failed: %s (%s)\n", message, path);
        return false;
    }

    bool IsSeparator(char c)
    {
        return c == '/' || c == '\\';
    }

    size_t LimitedNameLength(const char *name, size_t length, size_t limit)
    {
        if (length <= limit)
            return length;
        length = limit;
        // Avoid cutting through a UTF-8 sequence or leaving an ambiguous
        // trailing dot/space when shortening an otherwise valid component.
        while (length && ((unsigned char)name[length] & 0xC0) == 0x80)
            --length;
        while (length && (name[length - 1] == '.' || name[length - 1] == ' '))
            --length;
        return length;
    }

    // Validate the entire original component before optionally shortening it.
    // Normalize redundant separators and '.'; reject '..' and device paths.
    bool AppendRelativePath(char path[MAX_PATH], const char *name, bool isXBLA)
    {
        if (IsSeparator(name[0]) || strchr(name, ':'))
            return false;
        size_t used = strlen(path);
        const char *p = name;
        while (*p)
        {
            const char *component = p;
            while (*p && !IsSeparator(*p))
            {
                if ((unsigned char)*p < 32 || strchr("\"<>|*?", *p))
                    return false;
                ++p;
            }
            size_t length = (size_t)(p - component);
            while (IsSeparator(*p))
                ++p;
            if (!length || (length == 1 && component[0] == '.'))
                continue;
            // Reject trailing dots/spaces too, since filesystem normalization
            // can otherwise turn a seemingly harmless component into '..'.
            if (component[length - 1] == '.' || component[length - 1] == ' ')
                return false;
            if (isXBLA)
            {
                length = LimitedNameLength(component, length, XBLA_MAX_NAME_LENGTH);
                if (!length)
                    return false;
            }
            bool separator = used != 0 && path[used - 1] != '\\';
            if (length >= MAX_PATH - used - (separator ? 1 : 0))
                return false;
            if (separator)
                path[used++] = '\\';
            memcpy(path + used, component, length);
            used += length;
            path[used] = '\0';
        }
        return true;
    }

    bool NormalizeDevicePath(const char *source, char path[MAX_PATH])
    {
        if (!source || !source[0])
            return false;
        const char *colon = strchr(source, ':');
        if (!colon || colon == source || !IsSeparator(colon[1]))
            return false;
        size_t deviceLength = (size_t)(colon - source);
        if (deviceLength > MAX_PATH - 3)
            return false;
        for (size_t i = 0; i < deviceLength; ++i)
        {
            char c = source[i];
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '_' || c == '-'))
                return false;
        }
        memcpy(path, source, deviceLength + 1);
        path[deviceLength + 1] = '\\';
        path[deviceLength + 2] = '\0';
        source = colon + 1;
        while (IsSeparator(*source))
            ++source;
        return AppendRelativePath(path, source, false);
    }

    bool EnsureDirectory(const char *path)
    {
        DWORD attributes = GetFileAttributesA(path);
        if (attributes != (DWORD)-1)
            return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (CreateDirectoryA(path, NULL))
            return true;
        // Another caller may have created it; an existing file is not a folder.
        attributes = GetFileAttributesA(path);
        return attributes != (DWORD)-1 &&
               (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }

    bool EnsureDirectories(char path[MAX_PATH], bool includeLast)
    {
        // Normalized paths always begin with a device followed by ':\'.
        // Skip that root so we never try to create a directory named 'hdd:'.
        char *p = strchr(path, ':') + 2;
        for (; *p; ++p)
        {
            if (*p != '\\')
                continue;
            *p = '\0';
            bool ok = EnsureDirectory(path);
            *p = '\\';
            if (!ok)
                return false;
        }
        return !includeLast || EnsureDirectory(path);
    }

    bool OverwritesInput(const char *path, const char *firstPart, bool split)
    {
        // Extraction into the archive's own directory must not truncate any
        // source chunk, including a chunk whose handle is currently closed.
        size_t inputLength = strlen(firstPart); // openRead verified the .001.
        size_t outputLength = strlen(path);
        if (split)
            return outputLength == inputLength - 4 &&
                   _strnicmp(path, firstPart, outputLength) == 0;
        if (outputLength != inputLength ||
            _strnicmp(path, firstPart, inputLength - 3) != 0)
            return false;
        const char *digits = path + outputLength - 3;
        return digits[0] >= '0' && digits[0] <= '9' &&
               digits[1] >= '0' && digits[1] <= '9' &&
               digits[2] >= '0' && digits[2] <= '9' &&
               strcmp(digits, "000") != 0;
    }

    size_t MinizSplitRead(void *opaque, mz_uint64 offset,
                         void *buffer, size_t size)
    {
        return static_cast<SplitFile *>(opaque)->readAt(
            (unsigned __int64)offset, buffer, size);
    }

    struct ZipOutput
    {
        SplitFile *file;
        unsigned __int64 expectedSize;
        ZipProgress *progress;
    };

    size_t MinizSplitWrite(void *opaque, mz_uint64 offset,
                          const void *buffer, size_t size)
    {
        ZipOutput *output = static_cast<ZipOutput *>(opaque);
        // Bound writes before miniz's post-write size check, so malformed data
        // cannot grow a normal output beyond the size used to choose its mode.
        if (offset > output->expectedSize ||
            (unsigned __int64)size > output->expectedSize - offset)
            return 0;
        size_t written = output->file->writeAt((unsigned __int64)offset,
                                             buffer, size);
        // Count the bytes actually written, including a partial failed write.
        AdvanceZipProgress(output->progress, written);
        return written;
    }

    bool ReadZipEntryName(mz_zip_archive *zip, mz_uint index,
                          const char *firstPart, char **entryName,
                          mz_uint *entryNameBytes)
    {
        // Read the complete name, even when the original exceeds MAX_PATH:
        // XBLA component truncation may make the resulting path fit.
        mz_uint nameBytes = mz_zip_reader_get_filename(zip, index, NULL, 0);
        if (nameBytes <= 1)
            return ReportError("Invalid or overlong entry name", firstPart);
        char *name = (char *)malloc(nameBytes);
        if (!name)
            return ReportError("Out of memory for entry name", firstPart);
        if (mz_zip_reader_get_filename(zip, index, name, nameBytes) != nameBytes ||
            strlen(name) + 1 != nameBytes)
        {
            free(name);
            return ReportError("Invalid entry name", firstPart);
        }
        *entryName = name;
        *entryNameBytes = nameBytes;
        return true;
    }

    bool CalculateZipTotal(mz_zip_archive *zip, mz_uint count,
                           const char *firstPart, unsigned __int64 *total)
    {
        *total = 0;
        const unsigned __int64 maxTotal = ~(unsigned __int64)0;
        for (mz_uint i = 0; i < count; ++i)
        {
            mz_zip_archive_file_stat stat;
            if (!mz_zip_reader_file_stat(zip, i, &stat))
                return ReportError(mz_zip_get_error_string(mz_zip_get_last_error(zip)),
                                   firstPart);
            if (stat.m_is_directory || stat.m_uncomp_size == 0)
                continue;
            char *name;
            mz_uint nameBytes;
            if (!ReadZipEntryName(zip, i, firstPart, &name, &nameBytes))
                return false;
            // Match ExtractEntry's handling of both '/' and '\\' directories.
            bool directory = IsSeparator(name[nameBytes - 2]);
            free(name);
            if (directory)
                continue;
            if (stat.m_uncomp_size > maxTotal - *total)
                return ReportError("Total extracted size overflows 64 bits", firstPart);
            *total += (unsigned __int64)stat.m_uncomp_size;
        }
        return true;
    }

    bool ExtractEntry(mz_zip_archive *zip, mz_uint index,
                      const char *root, const char *firstPart, bool isXBLA,
                      ZipProgress *progress)
    {
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(zip, index, &stat))
            return ReportError(mz_zip_get_error_string(mz_zip_get_last_error(zip)),
                               firstPart);
        char *name;
        mz_uint nameBytes;
        if (!ReadZipEntryName(zip, index, firstPart, &name, &nameBytes))
            return false;

        bool directory = stat.m_is_directory != 0 ||
                 IsSeparator(name[nameBytes - 2]);

        bool split = !directory &&
                    stat.m_uncomp_size > FATX_SAFE_FILE_SIZE;

        size_t nameLength = nameBytes - 1;
        bool isIso = !directory && nameLength >= 4 &&
                    _stricmp(name + nameLength - 4, ".iso") == 0;

        char shortName[32];
        const char *outputName = name;

        if (isIso)
        {
            // Each ZIP entry index is unique within this archive.
            _snprintf(shortName, sizeof(shortName),
                    "tmp_%u.iso", (unsigned int)index);
            shortName[sizeof(shortName) - 1] = '\0';
            outputName = shortName;
        }

        char path[MAX_PATH];
        strcpy(path, root);
        bool validPath = AppendRelativePath(path, outputName, isXBLA);
        free(name);
        if (!validPath ||
            (!directory && strlen(path) == strlen(root)))
            return ReportError("Unsafe or overlong entry path", path);
        if (isXBLA && split)
        {
            // The physical filename includes the four-character .NNN suffix.
            char *filename = strrchr(path, '\\') + 1;
            size_t length = LimitedNameLength(filename, strlen(filename),
                                             XBLA_MAX_NAME_LENGTH - 4);
            if (!length)
                return ReportError("Empty filename after truncation", path);
            filename[length] = '\0';
        }
        if (stat.m_is_encrypted || (!directory && !stat.m_is_supported))
            return ReportError("Encrypted or unsupported entry", path);
        if (directory)
            return EnsureDirectories(path, true) ||
                   ReportError("Cannot create directory", path);

        if (stat.m_uncomp_size > FATX_SAFE_FILE_SIZE * 999 ||
            (split && strlen(path) > MAX_PATH - 5))
            return ReportError("Output exceeds the split size or path limit", path);
        if (OverwritesInput(path, firstPart, split))
            return ReportError("Output would overwrite an input chunk", path);
        if (!EnsureDirectories(path, false))
            return ReportError("Cannot create parent directory", path);

        SplitFile output;
        bool opened = split ? output.openWriteSplit(path, FATX_SAFE_FILE_SIZE) :
                              output.openWriteNormal(path);
        if (!opened)
            return ReportError(output.getLastError(), path);
        ZipOutput context;
        context.file = &output;
        context.expectedSize = (unsigned __int64)stat.m_uncomp_size;
        context.progress = progress;
        bool extracted = mz_zip_reader_extract_to_callback(
            zip, index, MinizSplitWrite, &context, 0) != 0;
        bool sizeMatches = output.size() == context.expectedSize;
        bool closed = output.close(); // Always flush, even after extraction fails.
        if (!closed)
            return ReportError(output.getLastError(), path);
        if (!extracted)
            return ReportError(mz_zip_get_error_string(mz_zip_get_last_error(zip)),
                               path);
        if (!sizeMatches)
            return ReportError("Extracted size does not match the ZIP entry", path);
        return true;
    }
}

int decompressZipFile(const char *inputFile, const char *outputPath, bool isXBLA)
{
    char firstPart[MAX_PATH];
    char root[MAX_PATH];
    if (!NormalizeDevicePath(inputFile, firstPart) ||
        !NormalizeDevicePath(outputPath, root))
    {
        ReportError("Invalid, relative, or overlong device path",
                    "inputFile/outputPath");
        return EXIT_FAILURE;
    }

    SplitFile input;
    if (!input.openRead(firstPart))
    {
        ReportError(input.getLastError(), firstPart);
        return EXIT_FAILURE;
    }
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    zip.m_pRead = MinizSplitRead;
    zip.m_pIO_opaque = &input;
    if (!mz_zip_reader_init(&zip, (mz_uint64)input.size(), 0))
    {
        ReportError(mz_zip_get_error_string(mz_zip_get_last_error(&zip)), firstPart);
        return EXIT_FAILURE; // miniz cleans up failed init; input closes via RAII.
    }

    mz_uint count = mz_zip_reader_get_num_files(&zip);
    unsigned __int64 totalBytes;
    if (!CalculateZipTotal(&zip, count, firstPart, &totalBytes))
    {
        mz_zip_reader_end(&zip);
        input.close();
        return EXIT_FAILURE;
    }
    ZipProgress progress;
    InitializeZipProgress(&progress, totalBytes);

    bool ok = EnsureDirectories(root, true);
    if (!ok)
        ReportError("Cannot create extraction directory", root);
    for (mz_uint i = 0; ok && i < count; ++i)
        ok = ExtractEntry(&zip, i, root, firstPart, isXBLA, &progress);

    // End miniz before releasing the callback's input object.
    if (!mz_zip_reader_end(&zip))
        ok = ReportError("Cannot finalize ZIP reader", firstPart);
    if (!input.close())
        ok = ReportError(input.getLastError(), firstPart);
    RenderZipProgress(&progress, true,
                      ok ? ZIP_PROGRESS_COMPLETE : ZIP_PROGRESS_FAILED);
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
