/*
FILE : file-stuff.c
PROJECT : xstore
PROGRAMMER : 951261
DESCRIPTION : wrappers around several file functions to make .iso.XXX files appear as one file
*/

#include "file-stuff.h"
#include <stdio.h>
#include <io.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

#include <sys/stat.h>
#include <stdlib.h>
#include "win32/dirent.h"

#include "FAT32.h"
#include <settings.h>

#ifdef WIN32
// #include <io.h>
#define F_OK 0
#define access _access
#endif

// #include <stdbool.h>

// List of needed functions:
/*
open: store FD, etc
close: close ALL related files
read: Transparent read across files
lseek: place file pointer in correct location
chdir: set CWD
getcwd: duh!
*/

#ifndef false
#define false 0
#endif

#ifndef true
#define true 1
#endif

#define arrayLength(x) (sizeof(x) / sizeof(x[0]))

#include <xtl.h>
// #include <io.h>
#include <fcntl.h>
// #include <errno.h>
#include <stdint.h>

static int xbox_open_set_errno(DWORD err)
{
    switch (err)
    {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
        errno = ENOENT;
        break;

    case ERROR_ACCESS_DENIED:
    case ERROR_SHARING_VIOLATION:
        errno = EACCES;
        break;

    case ERROR_FILE_EXISTS:
    case ERROR_ALREADY_EXISTS:
        errno = EEXIST;
        break;

    case ERROR_INVALID_PARAMETER:
    case ERROR_INVALID_NAME:
        errno = EINVAL;
        break;

    default:
        errno = EIO;
        break;
    }

    return -1;
}

void touch_file(const char *filename)
{
    extern bool CheckGameMounted();
    CheckGameMounted();

    printf("Creating %s\n", filename);
    int fp = open(filename, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0644); // "a" opens for appending; creates if doesn't exist
    if (fp > NULL)
    {
        close(fp);
    }
    else
    {
        printf("Failed to create %s, ERROR: %d\n", filename, errno);
    }
}

bool needsFat32LongNameWorkaround(const char *fileName)
{
    const char *firstDot = strchr(fileName, '.');
    const char *lastDot = strrchr(fileName, '.');
    return firstDot && lastDot && firstDot != lastDot && (firstDot - fileName) <= 9;
}

int xbox_open(const char *filepath, int flags, int mode)
{
    int fd = NULL;

    printf("Opening %s\n", filepath);

    char folderPath[MAX_TEXT_LENGTH] = "";
    char fileName[MAX_TEXT_LENGTH] = "";

    if (splitPathFolderFile(filepath, folderPath, MAX_TEXT_LENGTH, fileName, MAX_TEXT_LENGTH) != EXIT_SUCCESS)
    {
        printf("ERROR SPLITTING FILE PATH THING\n\n\n");
    }

    if (!needsFat32LongNameWorkaround(fileName))
    {
        return open(filepath, flags, mode); // pass on the call
    }

    if (access(filepath, 0) != -1)
    { // from official microsoft docs
        // file exists, pass on the open call
        printf("\n%s Exists\n\n", filepath);

        return open(filepath, flags, mode);
    }

    printf("\n%s does not Exist\n\n", filepath);

    // now for the difficult part. Because Microsoft, in all of their wisdom, decided to keep DOS 8.3
    // file compatibility on the Xbox 360, 12 character file names in the form "file.aaa.bbb" will be
    // converted to "file.bbb". To overcome this, this section of code directly writes to \\device\\MassX

    char tempFilePath[MAX_TEXT_LENGTH] = "";
    char finalFilePath[MAX_TEXT_LENGTH] = "";

    if (strcmp(folderPath, ".\\") == 0)
    { // strings are the same
        getcwd(folderPath, MAX_TEXT_LENGTH);

        const int pathLength = strlen(folderPath);
        if (folderPath[pathLength - 1] != '\\') // if the path does NOT end with "\"
        {
            strcat(folderPath, "\\");
        }
    }

    char lowerCaseFolderName[MAX_TEXT_LENGTH] = "";

    for (int i = 0; folderPath[i] && i < MAX_TEXT_LENGTH; i++)
    {
        lowerCaseFolderName[i] = tolower(folderPath[i]);
    }

    if(strstr(lowerCaseFolderName, "usb") != lowerCaseFolderName) { // does NOT begin with "usb"
        printf("WARNING: %s%s NOT on a FAT32 USB device\n\n", folderPath, fileName);
        return open(filepath, flags, mode);
    }

    static int tmpFileIndex = 0; // different temp file name each time

    _snprintf(tempFilePath, MAX_TEXT_LENGTH, "%sTMP%05d.TMP", folderPath, tmpFileIndex);
    _snprintf(finalFilePath, MAX_TEXT_LENGTH, "%s%s", folderPath, fileName);

    tmpFileIndex++;

    printf("Creating %s\n", tempFilePath);
    touch_file(tempFilePath); // Create a temp file

    printf("Force renaming to %s\n", finalFilePath);
    if (Fat32RenameUsb1(tempFilePath, finalFilePath) != EXIT_SUCCESS)
    {
        printf("Failed to rename %s\n\n", filepath);
        return -1;
    }

    fd = open(filepath, flags, mode);

    return fd;
}

int splitPathFolderFile(const char *path,
                        char *folder,
                        int folderLen,
                        char *file,
                        int fileLen)
{
    const char *lastSlash = NULL;
    const char *p;
    size_t folderSize;
    size_t fileSize;

    if (!path || !folder || !file || folderLen <= 0 || fileLen <= 0)
        return EXIT_FAILURE;

    folder[0] = '\0';
    file[0] = '\0';

    for (p = path; *p; ++p)
    {
        if (*p == '\\' || *p == '/')
            lastSlash = p;
    }

    if (lastSlash)
    {
        folderSize = (size_t)(lastSlash - path) + 1;
        fileSize = strlen(lastSlash + 1);

        if (folderSize >= (size_t)folderLen || fileSize >= (size_t)fileLen)
            return EXIT_FAILURE;

        memcpy(folder, path, folderSize);
        folder[folderSize] = '\0';
        strncpy(file, lastSlash + 1, fileLen - 1);
        file[fileLen - 1] = '\0';
    }
    else
    {
        const char *colon = strchr(path, ':');

        if (colon)
        {
            folderSize = (size_t)(colon - path) + 2;
            fileSize = strlen(colon + 1);

            if (folderSize + 1 >= (size_t)folderLen || fileSize >= (size_t)fileLen)
                return EXIT_FAILURE;

            memcpy(folder, path, folderSize);
            folder[folderSize] = '\\';
            folder[folderSize + 1] = '\0';
            strncpy(file, colon + 1, fileLen - 1);
            file[fileLen - 1] = '\0';
        }
        else
        {
            if (folderLen < 3 || strlen(path) >= (size_t)fileLen)
                return EXIT_FAILURE;

            strcpy(folder, ".\\");
            strncpy(file, path, fileLen - 1);
            file[fileLen - 1] = '\0';
        }
    }

    if (file[0] == '\0')
        return EXIT_FAILURE;

    return EXIT_SUCCESS;
}

typedef struct fileInfo
{
    int fd;
    bool isOpen;
} fileInfo;

static char currentWorkingDir[250] = "game:\\";

static fileInfo openFiles[10] = {
    {-1, false},
};

static long long splitFilePointer = 0;

static bool isAbsolutePath(const char *path)
{
    return path != NULL && (strchr(path, ':') != NULL || path[0] == '\\' || path[0] == '/');
}

static void normalizePath(const char *inputPath, char *outputPath, size_t outputPathSize)
{
    char tempPath[300];
    char prefix[16] = "";
    char *segments[32];
    int segmentCount = 0;
    char *context = NULL;
    char *token = NULL;
    size_t prefixLength = 0;

    if (inputPath == NULL || outputPath == NULL || outputPathSize == 0)
    {
        return;
    }

    strncpy(tempPath, inputPath, sizeof(tempPath) - 1);
    tempPath[sizeof(tempPath) - 1] = '\0';

    for (size_t i = 0; tempPath[i] != '\0'; i++)
    {
        if (tempPath[i] == '/')
        {
            tempPath[i] = '\\';
        }
    }

    if (tempPath[0] != '\0' && tempPath[1] == ':')
    {
        prefix[0] = tempPath[0];
        prefix[1] = tempPath[1];
        prefix[2] = '\\';
        prefix[3] = '\0';
        prefixLength = 3;
    }
    else if (tempPath[0] == '\\')
    {
        if (currentWorkingDir[0] != '\0' && currentWorkingDir[1] == ':')
        {
            prefix[0] = currentWorkingDir[0];
            prefix[1] = currentWorkingDir[1];
            prefix[2] = '\\';
            prefix[3] = '\0';
            prefixLength = 3;
        }
        else
        {
            prefix[0] = '\\';
            prefix[1] = '\0';
            prefixLength = 1;
        }
    }

    token = strtok_s(tempPath + prefixLength, "\\", &context);
    while (token != NULL)
    {
        if (strcmp(token, ".") == 0 || token[0] == '\0')
        {
            token = strtok_s(NULL, "\\", &context);
            continue;
        }

        if (strcmp(token, "..") == 0)
        {
            if (segmentCount > 0)
            {
                segmentCount--;
            }
            token = strtok_s(NULL, "\\", &context);
            continue;
        }

        if (segmentCount < (int)arrayLength(segments))
        {
            segments[segmentCount++] = token;
        }
        token = strtok_s(NULL, "\\", &context);
    }

    outputPath[0] = '\0';
    if (prefix[0] != '\0')
    {
        strncpy(outputPath, prefix, outputPathSize - 1);
        outputPath[outputPathSize - 1] = '\0';
    }

    for (int i = 0; i < segmentCount; i++)
    {
        if (strlen(outputPath) > 0 && outputPath[strlen(outputPath) - 1] != '\\')
        {
            strncat(outputPath, "\\", outputPathSize - strlen(outputPath) - 1);
        }
        strncat(outputPath, segments[i], outputPathSize - strlen(outputPath) - 1);
    }

    if (outputPath[0] == '\0')
    {
        strncpy(outputPath, prefix[0] != '\0' ? prefix : ".", outputPathSize - 1);
        outputPath[outputPathSize - 1] = '\0';
    }
}

static void resolvePath(const char *inputPath, char *outputPath, size_t outputPathSize)
{
    char combinedPath[MAX_TEXT_LENGTH];

    if (inputPath == NULL || outputPath == NULL || outputPathSize == 0)
    {
        return;
    }

    if (isAbsolutePath(inputPath) || currentWorkingDir[0] == '\0')
    {
        normalizePath(inputPath, outputPath, outputPathSize);
        return;
    }

    if (currentWorkingDir[strlen(currentWorkingDir) - 1] == '\\' || currentWorkingDir[strlen(currentWorkingDir) - 1] == '/')
    {
        _snprintf(combinedPath, MAX_TEXT_LENGTH, "%s%s", currentWorkingDir, inputPath);
    }
    else
    {
        _snprintf(combinedPath, MAX_TEXT_LENGTH, "%s\\%s", currentWorkingDir, inputPath);
    }

    normalizePath(combinedPath, outputPath, outputPathSize);
}

int customOpen(const char *filename,
               int oflag,
               int pmode)
{
    char baseFileName[MAX_TEXT_LENGTH];
    char tempFileName[MAX_TEXT_LENGTH];

    resolvePath(filename, tempFileName, sizeof(tempFileName));

    if (strlen(tempFileName) <= 4)
    { // obviously not a split ISO
        return xbox_open(tempFileName, oflag, pmode);
    }

    strcpy(baseFileName, tempFileName);
    baseFileName[strlen(baseFileName) - 4] = '\0'; // remove last 4 digits (.001)

    if (strcmp(&tempFileName[strlen(tempFileName) - 4], ".001") == 0) // check for split file
    {
        int partIndex = 1;
        // Now do the hard bit
        while (partIndex <= 10) // ok ChatGPT, I will add a bounds check
        {
            char tempBuff[MAX_TEXT_LENGTH];
            _snprintf(tempBuff, MAX_TEXT_LENGTH, "%s.%03d", baseFileName, partIndex);
            // if ((openFiles[partIndex - 1].fd = open(tempBuff, oflag, pmode)) < 0)
            if ((openFiles[partIndex - 1].fd = xbox_open(tempBuff, oflag, pmode)) < 0)
            {
                openFiles[partIndex - 1].isOpen = false;
                printf("WARNING! failed to open %s\n", tempBuff);
                break;
            }
            openFiles[partIndex - 1].isOpen = true;

            partIndex++;
        }
        splitFilePointer = 0;   // reset FP
        return openFiles[0].fd; // return file descriptor of first file. This can then be used for customRead, and customLseek
    }
    else
    {
        // return open(tempFileName, oflag, pmode); // pass on the call, nothing special
        return xbox_open(tempFileName, oflag, pmode); // pass on the call, nothing special
    }
}

int customMkdir(const char *dirname)
{
    char tempDirName[300];

    resolvePath(dirname, tempDirName, sizeof(tempDirName));
    return mkdir(tempDirName);
}

// Source - https://stackoverflow.com/a/32496721
// Posted by Superlokkus, modified by community. See post 'Timeline' for change history
// Retrieved 2026-04-20, License - CC BY-SA 4.0

char *replace_char(char *str, char find, char replace)
{
    char *current_pos = strchr(str, find);
    while (current_pos)
    {
        *current_pos = replace;
        current_pos = strchr(current_pos, find);
    }
    return str;
}

// Will recersively create directories
int customForceMkdir(const char *dir)
{
    char tempDir[300] = "\0";
    char currentPath[300] = "\0";
    char *context = NULL;
    char *token = NULL;
    char *deviceSeparator = NULL;
    size_t prefixLength = 0;

    resolvePath(dir, tempDir, sizeof(tempDir));

    deviceSeparator = strchr(tempDir, ':');
    if (deviceSeparator != NULL)
    {
        prefixLength = (size_t)(deviceSeparator - tempDir) + 1;
        if (tempDir[prefixLength] == '\\' || tempDir[prefixLength] == '/')
        {
            prefixLength++;
        }

        strncpy(currentPath, tempDir, prefixLength);
        currentPath[prefixLength] = '\0';
    }
    else if (tempDir[0] == '\\')
    {
        currentPath[0] = '\\';
        currentPath[1] = '\0';
        prefixLength = 1;
    }

    token = strtok_s(tempDir + prefixLength, "\\", &context);
    while (token != NULL)
    {
        if (strlen(currentPath) > 0 && currentPath[strlen(currentPath) - 1] != '\\')
        {
            strncat(currentPath, "\\", sizeof(currentPath) - strlen(currentPath) - 1);
        }
        strncat(currentPath, token, sizeof(currentPath) - strlen(currentPath) - 1);

        if (mkdir(currentPath) != 0 && errno != EEXIST)
        {
            return -1;
        }

        token = strtok_s(NULL, "\\", &context);
    }

    return 0;
}

int customClose(int fd)
{
    if (openFiles[0].fd == fd)
    {
        for (int i = 1; i < 10; i++)
        {
            if (openFiles[i].isOpen)
            {
                close(openFiles[i].fd);
                openFiles[i].isOpen = false;
                openFiles[i].fd = -1;
            }
        }
        openFiles[0].isOpen = false;
        splitFilePointer = 0;                   // clear all global variables
        int returnVal = close(openFiles[0].fd); // return error code
        openFiles[0].fd = -1;
        return returnVal;
    }
    else
    {
        return close(fd);
    }
}

static int fpToFd(long long fp, long long *fpForSelectedFile, int *currentFile)
{
    struct _stat64 fileStat;

    int tempFp = 0;

    int currentFd = 0; // index of the current working file
    while (true)
    {
        if (currentFd >= 10)
        {
            return -2;
        }
        if (!openFiles[currentFd].isOpen)
        {
            *fpForSelectedFile = -1;
            *currentFile = currentFd;
            return -1; // fp must be past the end of the file.
        }

        _fstat64(openFiles[currentFd].fd, &fileStat);
        if (fp >= fileStat.st_size)
        {
            fp = fp - fileStat.st_size; // ready for next file
        }
        else
        {
            *currentFile = currentFd;
            *fpForSelectedFile = fp;
            return openFiles[currentFd].fd;
        }
        currentFd++;
    }
}

long long customRead(int const fd, void *const buffer, long long const buffer_size)
{
    if (fd == openFiles[0].fd)
    { // split file detected
        if (!openFiles[0].isOpen)
        {
            return -1; // file (descriptor) not found
        }

        long long newFp = 0;
        int currentFile = 0;
        int fileDescriptor = fpToFd(splitFilePointer, &newFp, &currentFile);
        if (fileDescriptor < 0)
        {
            return 0; // read past end of file
        }
        // int bytesRead = read(fileDescriptor, buffer, buffer_size);
        long long bytesRead = 0;
        _lseeki64(fileDescriptor, newFp, SEEK_SET);
        while (bytesRead < buffer_size && currentFile < (int)arrayLength(openFiles) && openFiles[currentFile].isOpen)
        {
            long long tempReadVal = read(openFiles[currentFile].fd, (char *)buffer + bytesRead, buffer_size - bytesRead);
            if (tempReadVal < 0)
            {
                return tempReadVal; // read error
            }

            bytesRead += tempReadVal;
            currentFile++;
            if (currentFile < (int)arrayLength(openFiles) && openFiles[currentFile].isOpen)
            {
                _lseeki64(openFiles[currentFile].fd, 0, SEEK_SET); // when moving to a new file, reset the file pointer to the start
            }
        }
        splitFilePointer += bytesRead;
        return bytesRead;
    }
    else
    {
        return read(fd, buffer, buffer_size);
    }
}

long long customLseek(int fd,
                      long long offset,
                      int origin)
{
    if (fd == openFiles[0].fd && openFiles[0].isOpen)
    {

        struct _stat64 fileStat;
        long long fileSize = 0;

        for (int i = 0; i < 10 && openFiles[i].isOpen == true; i++)
        {
            _fstat64(openFiles[i].fd, &fileStat);
            fileSize += fileStat.st_size; // calculate size of ALL files in split group
        }
        if (offset > fileSize || offset < (0 - fileSize))
        {
            printf("ERROR: offset out of bounds. max: %lli, given: %lli\n", fileSize, offset);
            return -1;
        }

        switch (origin)
        {
        case SEEK_SET: // start of file
            if (offset < 0)
            {
                printf("ERROR: invalid offset, given: %lli\n", offset);
                return -1; // invalid offset
            }
            splitFilePointer = offset;
            break;
        case SEEK_CUR: // add to current fp
            if (splitFilePointer + offset > fileSize || (splitFilePointer + offset) < 0)
            {
                printf("ERROR: seek past end/start of file\n");
                return -1; // past end or before start of file
            }
            splitFilePointer += offset;
            break;
        case SEEK_END:
            if (offset > 0)
            {
                printf("ERROR: invalid offset, given: %lli\n", offset);
                return -1; // invalid offset
            }
            splitFilePointer = fileSize + offset; // calculate distance from end of fileS
            break;
        default:
            printf("ERROR: invalid origin\n");
            return -1; // invalid origin
            break;
        }
        return splitFilePointer;
    }
    else
    {
        return _lseeki64(fd, offset, origin); // just the normal
    }
}

int chdir(const char *dirname)
{
    char tempDirName[300];

    resolvePath(dirname, tempDirName, sizeof(tempDirName));

    if (tempDirName == NULL)
    {
        return -1;
    }

    DIR *dir = opendir(tempDirName);
    if (dir)
    {
        /* Directory exists. */
        closedir(dir);
    }
    else
    {
        /* opendir() failed for some other reason. */
        printf("Warning: opendir failed to open %s\n\n", tempDirName);
    }

    strncpy(currentWorkingDir, tempDirName, sizeof(currentWorkingDir) - 1);
    currentWorkingDir[sizeof(currentWorkingDir) - 1] = '\0';
    return 0; // success
}

char *getcwd(
    char *buffer,
    int maxlen)
{
    if (buffer == NULL && maxlen == 0)
    {
        size_t cwdLength = strlen(currentWorkingDir) + 1;
        char *cwdBuffer = (char *)malloc(cwdLength);
        if (cwdBuffer == NULL)
        {
            return NULL;
        }
        memcpy(cwdBuffer, currentWorkingDir, cwdLength);
        return cwdBuffer;
    }
    if (maxlen <= strlen(currentWorkingDir))
    {
        return NULL; // buffer too small
    }
    return strncpy(buffer, currentWorkingDir, maxlen);
}

int deleteDirectory(const char *directoryToDelete, const size_t len)
{
    DIR *dir;
    struct dirent *entity;
    size_t baseLen;
    int result = EXIT_SUCCESS;

    (void)len;

    if (directoryToDelete == NULL)
    {
        return EXIT_FAILURE;
    }

    baseLen = strlen(directoryToDelete);
    if (baseLen == 0 || baseLen >= MAX_TEXT_LENGTH - 1)
    {
        return EXIT_FAILURE;
    }

    dir = opendir(directoryToDelete);
    if (dir == NULL)
    {
        printf("Failed to open %s\n", directoryToDelete);
        return EXIT_FAILURE;
    }

    entity = readdir(dir);
    while (entity != NULL)
    {
        char path[MAX_TEXT_LENGTH];
        size_t nameLen;
        size_t needsSlash;

        if (strcmp(entity->d_name, ".") == 0 || strcmp(entity->d_name, "..") == 0)
        {
            entity = readdir(dir);
            continue;
        }

        nameLen = strlen(entity->d_name);
        needsSlash = directoryToDelete[baseLen - 1] != '\\' && directoryToDelete[baseLen - 1] != '/';

        if (baseLen + needsSlash + nameLen >= sizeof(path))
        {
            printf("Path too long while deleting %s%s\n", directoryToDelete, entity->d_name);
            result = EXIT_FAILURE;
            entity = readdir(dir);
            continue;
        }

        strcpy(path, directoryToDelete);
        if (needsSlash)
        {
            strcat(path, "\\");
        }
        strcat(path, entity->d_name);

        if (remove(path) != 0)
        {
            if (deleteDirectory(path, strlen(path)) != EXIT_SUCCESS)
            {
                printf("Failed to delete %s\n", path);
                result = EXIT_FAILURE;
            }
        }
        entity = readdir(dir);
    }

    closedir(dir);

    if (rmdir(directoryToDelete) != 0)
    {
        printf("Failed to remove directory %s\n", directoryToDelete);
        result = EXIT_FAILURE;
    }

    return result;
}
