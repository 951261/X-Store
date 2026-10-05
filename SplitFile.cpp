// Yes, this was AI generated

#if defined(_XBOX)
#include "stdafx.h"
#endif
#include "SplitFile.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

namespace
{
    const unsigned __int64 MAX_U64 = ~(unsigned __int64)0;
    const unsigned __int64 MAX_SEEK = MAX_U64 >> 1;
}

SplitFile::SplitFile()
    : m_mode(CLOSED), m_file(NULL), m_activePart(-1), m_path(NULL),
      m_prefixLength(0), m_parts(NULL), m_partCount(0), m_size(0),
      m_maxPartSize(0), m_error("")
{
}

SplitFile::~SplitFile()
{
    close();
}

bool SplitFile::fail(const char *message)
{
    if (!m_error[0])
        m_error = message;
    return false;
}

bool SplitFile::failOpen(const char *message)
{
    fail(message);
    close();
    return false;
}

bool SplitFile::closeActive()
{
    FILE *file = m_file;
    m_file = NULL;
    m_activePart = -1;
    if (file && fclose(file) != 0)
        return fail("Failed to close/flush physical file");
    return true;
}

bool SplitFile::close()
{
    closeActive();
    free(m_path);
    free(m_parts);
    m_path = NULL;
    m_parts = NULL;
    m_prefixLength = 0;
    m_partCount = 0;
    m_size = 0;
    m_maxPartSize = 0;
    m_mode = CLOSED;
    return !m_error[0];
}

bool SplitFile::prepare(const char *path, bool numbered)
{
    // A new open clears prior errors, but must report a new close/flush failure.
    m_error = "";
    if (!close())
        return false;
    if (!path || !path[0])
        return fail("Empty file path");
    size_t length = strlen(path);
    size_t extra = numbered ? 5 : 1; // Optional dot + three digits + terminator.
    if (length > (size_t)-1 - extra)
        return fail("File path is too long");
    m_path = (char *)malloc(length + extra);
    if (!m_path)
        return fail("Out of memory for file path");
    memcpy(m_path, path, length + 1);
    if (numbered)
    {
        m_path[length] = '.';
        m_prefixLength = length + 1;
    }
    return true;
}

void SplitFile::setPartPath(unsigned int index)
{
    unsigned int number = index + 1; // Index 0 names .001, index 998 names .999.
    char *digits = m_path + m_prefixLength;
    digits[0] = (char)('0' + number / 100);
    digits[1] = (char)('0' + (number / 10) % 10);
    digits[2] = (char)('0' + number % 10);
    digits[3] = '\0';
}

bool SplitFile::activate(unsigned int index)
{
    if (m_file && m_activePart == (int)index)
        return true;
    if (!closeActive())
        return false;
    if (m_mode != WRITE_NORMAL)
        setPartPath(index);
    // Created parts are always reopened without truncation. Gap filling makes
    // creation sequential even when the caller writes at an arbitrary offset.
    const char *mode = m_mode == READ ? "rb" :
                      (index < m_partCount ? "r+b" : "w+b");
    m_file = fopen(m_path, mode);
    if (!m_file)
        return fail("Cannot open physical part");
    m_activePart = (int)index;
    if (m_mode != READ && index == m_partCount)
        ++m_partCount;
    return true;
}

bool SplitFile::openRead(const char *firstPart)
{
    if (!prepare(firstPart, false))
        return false;
    size_t length = strlen(m_path);
    if (length < 5 || strcmp(m_path + length - 4, ".001") != 0 ||
        m_path[length - 5] == '\\' || m_path[length - 5] == '/' ||
        m_path[length - 5] == ':')
        return failOpen("First split filename must have a basename and end in .001");
    m_prefixLength = length - 3;
    // Checked C allocation works with the XDK project's exceptions disabled.
    m_parts = (Part *)malloc(MAX_PARTS * sizeof(Part));
    if (!m_parts)
        return failOpen("Out of memory for part table");

    for (unsigned int i = 0; i < MAX_PARTS; ++i)
    {
        setPartPath(i);
        errno = 0;
        m_file = fopen(m_path, "rb");
        if (!m_file)
        {
            if (i != 0 && errno == ENOENT)
                break; // Never skip a missing number to concatenate later parts.
            return failOpen("Cannot open input part");
        }
        if (_fseeki64(m_file, 0, SEEK_END) != 0)
            return failOpen("Cannot seek to end of input part");
        __int64 length64 = _ftelli64(m_file);
        if (length64 < 0)
            return failOpen("Cannot determine input part size");
        unsigned __int64 partSize = (unsigned __int64)length64;
        if (partSize > MAX_U64 - m_size)
            return failOpen("Logical input size overflows 64 bits");
        m_parts[i].virtualStart = m_size;
        m_parts[i].size = partSize;
        m_size += partSize;
        ++m_partCount;
        if (!closeActive())
            return failOpen("Cannot close input part");
    }
    m_mode = READ;
    return true;
}

bool SplitFile::openWriteNormal(const char *path)
{
    if (!prepare(path, false))
        return false;
    m_mode = WRITE_NORMAL;
    if (!activate(0))
        return failOpen("Cannot create output file");
    return true;
}

bool SplitFile::openWriteSplit(const char *basePath, unsigned __int64 maxPartSize)
{
    if (!prepare(basePath, true))
        return false;
    if (!maxPartSize || maxPartSize > MAX_SEEK)
        return failOpen("Invalid maximum part size");
    m_maxPartSize = maxPartSize;
    // A shorter replacement must not leave old trailing chunks to be discovered.
    // This is a fresh-output operation, not a resume operation or a transaction.
    for (unsigned int i = 0; i < MAX_PARTS; ++i)
    {
        setPartPath(i);
        errno = 0;
        if (remove(m_path) != 0 && errno != ENOENT)
            return failOpen("Cannot remove existing output part");
    }
    m_mode = WRITE_SPLIT;
    if (!activate(0))
        return failOpen("Cannot create first output part");
    return true;
}

size_t SplitFile::readAt(unsigned __int64 offset, void *buffer, size_t bytes)
{
    if (!bytes || m_error[0])
        return 0;
    if (m_mode != READ || !buffer)
    {
        fail("readAt requires read mode and a non-null buffer");
        return 0;
    }
    if (offset >= m_size)
        return 0;

    // Find the first exclusive part end greater than offset. This also skips
    // empty parts and maps an exact boundary to the next nonempty part.
    unsigned int low = 0, high = m_partCount;
    while (low < high)
    {
        unsigned int mid = low + (high - low) / 2;
        if (offset >= m_parts[mid].virtualStart + m_parts[mid].size)
            low = mid + 1;
        else
            high = mid;
    }

    size_t done = 0;
    for (unsigned int i = low; done < bytes && i < m_partCount; ++i)
    {
        if (!m_parts[i].size)
            continue;
        unsigned __int64 within = offset - m_parts[i].virtualStart;
        unsigned __int64 available = m_parts[i].size - within;
        size_t chunk = bytes - done;
        if (available < (unsigned __int64)chunk)
            chunk = (size_t)available; // Narrow only after proving it fits.
        if (!activate(i))
            break;
        if (_fseeki64(m_file, (__int64)within, SEEK_SET) != 0)
        {
            fail("Cannot seek in input part");
            break;
        }
        size_t got = fread((unsigned char *)buffer + done, 1, chunk, m_file);
        done += got;
        offset += (unsigned __int64)got;
        if (got != chunk || ferror(m_file))
        {
            fail("Input read failed or part became shorter after discovery");
            break;
        }
        // At a physical boundary, continue into the same caller buffer.
    }
    return done;
}

size_t SplitFile::writeBytes(unsigned __int64 offset,
                            const unsigned char *buffer, size_t bytes)
{
    size_t done = 0;
    while (done < bytes)
    {
        unsigned int index = 0;
        unsigned __int64 within = offset;
        size_t chunk = bytes - done;
        if (m_mode == WRITE_SPLIT)
        {
            // writeAt has validated the entire range before this narrowing.
            index = (unsigned int)(offset / m_maxPartSize);
            within = offset % m_maxPartSize;
            unsigned __int64 available = m_maxPartSize - within;
            if (available < (unsigned __int64)chunk)
                chunk = (size_t)available;
        }
        if (!activate(index))
            break;
        // Physical offsets are bounded by the signed 64-bit CRT seek limit.
        if (_fseeki64(m_file, (__int64)within, SEEK_SET) != 0)
        {
            fail("Cannot seek in output part");
            break;
        }
        size_t put = fwrite(buffer + done, 1, chunk, m_file);
        done += put;
        offset += (unsigned __int64)put;
        if (offset > m_size)
            m_size = offset;
        if (put != chunk || ferror(m_file))
        {
            fail("Output write failed");
            break;
        }
        // Recompute index/within so a single request can cross many parts.
    }
    return done;
}

size_t SplitFile::writeAt(unsigned __int64 offset, const void *buffer, size_t bytes)
{
    if (!bytes || m_error[0])
        return 0;
    if ((m_mode != WRITE_NORMAL && m_mode != WRITE_SPLIT) || !buffer)
    {
        fail("writeAt requires write mode and a non-null buffer");
        return 0;
    }
    unsigned __int64 limit = MAX_SEEK;
    if (m_mode == WRITE_SPLIT)
        limit = m_maxPartSize > MAX_U64 / MAX_PARTS ? MAX_U64 :
                m_maxPartSize * MAX_PARTS;
    if (offset > limit || (unsigned __int64)bytes > limit - offset)
    {
        fail("Output range exceeds the part count or 64-bit size limit");
        return 0;
    }

    // Materialize holes: every preceding split part must be full so that
    // concatenation on a later openRead preserves all logical offsets.
    static const unsigned char zeros[4096] = { 0 };
    while (m_size < offset)
    {
        unsigned __int64 gap = offset - m_size;
        size_t chunk = sizeof(zeros);
        if (gap < (unsigned __int64)chunk)
            chunk = (size_t)gap;
        if (writeBytes(m_size, zeros, chunk) != chunk || m_error[0])
            return 0; // Gap bytes are not part of the caller's byte count.
    }
    return writeBytes(offset, (const unsigned char *)buffer, bytes);
}
