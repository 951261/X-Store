#ifndef XSTORE_SPLITFILE_H
#define XSTORE_SPLITFILE_H

#include <stddef.h>
#include <stdio.h>

// Raw consecutive chunks, not ZIP multi-volume archives. Not thread-safe.
// Only one physical file is open at a time. Instances cannot be copied.
class SplitFile
{
public:
    SplitFile();
    ~SplitFile();

    // Requires .001; discovers consecutive parts through .999, stopping at
    // the first missing name. Other open/seek/size errors fail the whole open.
    bool openRead(const char *firstPart);

    // Starts a new file, truncating any existing file at this path.
    bool openWriteNormal(const char *path);

    // Starts a new sequence. Removes existing basePath.001 through .999,
    // including stale trailing parts; basePath itself is not modified.
    // Creates an empty .001 immediately. maxPartSize must be 1..2^63-1.
    bool openWriteSplit(const char *basePath, unsigned __int64 maxPartSize);

    size_t readAt(unsigned __int64 offset, void *buffer, size_t size);
    // Gaps beyond EOF are explicitly zero-filled, including intervening parts.
    // Split output is limited to 999 parts. Normal output to 2^63-1 bytes.
    size_t writeAt(unsigned __int64 offset, const void *buffer, size_t size);

    unsigned __int64 size() const { return m_size; }
    bool isOpen() const { return m_mode != CLOSED; }

    // Errors are latched until the next open. EOF is not an error. After an
    // error, readAt/writeAt return zero; close still releases all resources.
    const char *getLastError() const { return m_error; }

    // Check this after extraction: fclose can report delayed write failures.
    // Safe to repeat; preserves the error message and resets size() to zero.
    bool close();

private:
    enum { MAX_PARTS = 999 };
    enum Mode { CLOSED, READ, WRITE_NORMAL, WRITE_SPLIT };
    struct Part
    {
        // Paths are derived from the shared prefix and this part's index.
        unsigned __int64 virtualStart;
        unsigned __int64 size;
    };

    SplitFile(const SplitFile &);
    SplitFile &operator=(const SplitFile &);

    bool prepare(const char *path, bool numbered);
    bool fail(const char *message);
    bool failOpen(const char *message);
    void setPartPath(unsigned int index);
    bool closeActive();
    bool activate(unsigned int index);
    size_t writeBytes(unsigned __int64 offset, const unsigned char *buffer,
                      size_t size);

    Mode m_mode;
    FILE *m_file;
    int m_activePart;
    char *m_path;
    size_t m_prefixLength;
    Part *m_parts;
    unsigned int m_partCount;
    unsigned __int64 m_size;
    unsigned __int64 m_maxPartSize;
    const char *m_error;
};

#endif
