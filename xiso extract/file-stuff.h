#ifndef FILE_STUFF_H
#define FILE_STUFF_H

// List of needed functions:
/*
open: store FD, etc
close: close ALL related files
read: Transparent read across files
lseek: place file pointer in correct location
chdir: set CWD
getcwd: duh!
*/

#include <direct.h>
#include <malloc.h>

int customOpen(const char *filename,
               int oflag,
               int pmode);

int customClose(int fd);

long long customRead(int const fd, void *const buffer, long long const buffer_size);

long long customLseek(int fd,
                      long long offset,
                      int origin);

int customMkdir(const char *dirname);

int customForceMkdir(const char *dir);

int chdir(const char *dirname);

char *getcwd(
    char *buffer,
    int maxlen);

// Deletes a directory and all of its contents
int deleteDirectory(const char *directoryToDelete, const size_t len);

int xbox_open(const char *filename, int flags, int mode);

int splitPathFolderFile(const char *path,
                        char *folder,
                        int folderLen,
                        char *file,
                        int fileLen);

int findFile(const char *folder, char *isoFile, int len, const char *suffix);

#endif
