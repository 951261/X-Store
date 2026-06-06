#include <xtl.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <string>

#include <OutputConsole.h>

#ifndef INVALID_FILE_ATTRIBUTES
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)
#endif


static bool isPathSep(char c)
{
    return c == '\\' || c == '/';
}

static std::string joinPath(const char *left, const char *right)
{
    std::string result = left ? left : "";

    if (!result.empty() && !isPathSep(result[result.size() - 1])) {
        result += "\\";
    }

    result += right ? right : "";
    return result;
}

static bool isDotDir(const char *name)
{
    return name &&
           (strcmp(name, ".") == 0 || strcmp(name, "..") == 0);
}

static bool pathIsDirectory(const char *path)
{
    DWORD attrs = GetFileAttributesA(path);

    return attrs != INVALID_FILE_ATTRIBUTES &&
           (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

static bool ensureDirectoryExists(const char *path)
{
    DWORD attrs = GetFileAttributesA(path);

    if (attrs != INVALID_FILE_ATTRIBUTES) {
        return (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }

    return CreateDirectoryA(path, NULL) != 0 ||
           GetLastError() == ERROR_ALREADY_EXISTS;
}

static std::string fullPath(const char *path)
{
    if (!path || !*path) {
        return "";
    }

    std::string out = path;
    for (size_t i = 0; i < out.size(); ++i) {
        if (out[i] == '/') {
            out[i] = '\\';
        }
    }

    while (!out.empty() && isPathSep(out[out.size() - 1])) {
        out.resize(out.size() - 1);
    }

    return out;
}

static bool destinationIsInsideSource(const char *src, const char *dst)
{
    std::string s = fullPath(src);
    std::string d = fullPath(dst);

    if (s.empty() || d.empty()) {
        return true;
    }

    for (size_t i = 0; i < s.size(); ++i) {
        s[i] = (char)tolower((unsigned char)s[i]);
    }

    for (size_t i = 0; i < d.size(); ++i) {
        d[i] = (char)tolower((unsigned char)d[i]);
    }

    return d.size() > s.size() &&
           d.compare(0, s.size(), s) == 0 &&
           isPathSep(d[s.size()]);
}

static int copyDirectoryInner(const char *srcDir, const char *dstDir)
{
    dprintf("Copying %s to %s \n", srcDir, dstDir);
    std::string search = joinPath(srcDir, "*");

    WIN32_FIND_DATAA data;
    HANDLE handle = FindFirstFileA(search.c_str(), &data);
	
    if (handle == INVALID_HANDLE_VALUE) {
        return -1;
    }

    do {
        if (isDotDir(data.cFileName)) {
            continue;
        }

        std::string srcPath = joinPath(srcDir, data.cFileName);
        std::string dstPath = joinPath(dstDir, data.cFileName);

        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!ensureDirectoryExists(dstPath.c_str())) {
                FindClose(handle);
                return -1;
            }

            if (copyDirectoryInner(srcPath.c_str(), dstPath.c_str()) != EXIT_SUCCESS) {
                FindClose(handle);
                return -1;
            }
        } else {
            if (!CopyFileA(srcPath.c_str(), dstPath.c_str(), FALSE)) {
                FindClose(handle);
                return -1;
            }

            SetFileAttributesA(dstPath.c_str(), data.dwFileAttributes);
        }
    } while (FindNextFileA(handle, &data));

    DWORD findError = GetLastError();
    FindClose(handle);

    return ( findError == ERROR_NO_MORE_FILES ? EXIT_SUCCESS : -1 );
}

int copyDirectory(const char *srcDir, const char *dstDir)
{
    if (!srcDir || !*srcDir || !dstDir || !*dstDir) {
        return -1;
    }

    if (!pathIsDirectory(srcDir)) {
        return -1;
    }

    /* Prevent recursive self-copy like: copyDirectory("A", "A\\backup") */
    if (destinationIsInsideSource(srcDir, dstDir)) {
        return -1;
    }

    if (!ensureDirectoryExists(dstDir)) {
        return -1;
    }

    return copyDirectoryInner(srcDir, dstDir);
}

static int is_path_sep(char c)
{
    return c == '\\' || c == '/';
}

static const char *path_basename_c(const char *path)
{
    const char *last = path;

    if (!path || !*path) {
        return "";
    }

    for (const char *p = path; *p; ++p) {
        if (is_path_sep(*p) && p[1] != '\0') {
            last = p + 1;
        }
    }

    return last;
}

static int is_eight_hex_chars_c(const char *value)
{
    size_t i;

    if (!value) {
        return 0;
    }

    for (i = 0; value[i]; ++i) {
        if (i >= 8 || !isxdigit((unsigned char)value[i])) {
            return 0;
        }
    }

    return i == 8;
}

static char *dup_string_c(const char *value)
{
    size_t len;
    char *copy;

    if (!value) {
        return NULL;
    }

    len = strlen(value);
    copy = (char *)malloc(len + 1);
    if (!copy) {
        return NULL;
    }

    memcpy(copy, value, len + 1);
    return copy;
}

static char *join_path_c(const char *left, const char *right)
{
    size_t left_len;
    size_t right_len;
    int needs_sep;
    char *result;

    if (!left || !right) {
        return NULL;
    }

    left_len = strlen(left);
    right_len = strlen(right);
    needs_sep = left_len > 0 && !is_path_sep(left[left_len - 1]);

    result = (char *)malloc(left_len + needs_sep + right_len + 1);
    if (!result) {
        return NULL;
    }

    memcpy(result, left, left_len);

    if (needs_sep) {
        result[left_len] = '\\';
        memcpy(result + left_len + 1, right, right_len);
        result[left_len + 1 + right_len] = '\0';
    } else {
        memcpy(result + left_len, right, right_len);
        result[left_len + right_len] = '\0';
    }

    return result;
}

static int path_is_directory_c(const char *path)
{
    DWORD attrs = GetFileAttributesA(path);

    return attrs != INVALID_FILE_ATTRIBUTES &&
           (attrs & FILE_ATTRIBUTE_DIRECTORY);
}

static int has_xbla_content_folder_c(const char *title_id_dir)
{
    char *content_dir = join_path_c(title_id_dir, "000D0000");
    int exists;

    if (!content_dir) {
        return 0;
    }

    exists = path_is_directory_c(content_dir);
    free(content_dir);

    return exists;
}

static int check_xbla_candidate_c(const char *dir, char **match)
{
    const char *name = path_basename_c(dir);

    if (!is_eight_hex_chars_c(name)) {
        return 1;
    }

    if (!has_xbla_content_folder_c(dir)) {
        return 1;
    }

    /* More than one XBLA title folder found. Treat as unsafe/ambiguous. */
    if (*match) {
        return 0;
    }

    *match = dup_string_c(dir);
    return *match != NULL;
}

static int scan_for_xbla_title_id_dir_c(const char *dir, char **match)
{
    char *search_pattern;
    WIN32_FIND_DATAA data;
    HANDLE handle;

    if (!check_xbla_candidate_c(dir, match)) {
        return 0;
    }

    search_pattern = join_path_c(dir, "*");
    if (!search_pattern) {
        return 0;
    }

    handle = FindFirstFileA(search_pattern, &data);
    free(search_pattern);

    if (handle == INVALID_HANDLE_VALUE) {
        return 1;
    }

    do {
        char *child_path;

        if (!strcmp(data.cFileName, ".") || !strcmp(data.cFileName, "..")) {
            continue;
        }

        if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            continue;
        }

        child_path = join_path_c(dir, data.cFileName);
        if (!child_path) {
            FindClose(handle);
            return 0;
        }

        if (!scan_for_xbla_title_id_dir_c(child_path, match)) {
            free(child_path);
            FindClose(handle);
            return 0;
        }

        free(child_path);
    } while (FindNextFileA(handle, &data));

    FindClose(handle);
    return 1;
}

char *findXblaTitleIdDir(const char *extractedRoot)
{
    char *match = NULL;

    if (!extractedRoot || !*extractedRoot || !path_is_directory_c(extractedRoot)) {
        return NULL;
    }

    if (!scan_for_xbla_title_id_dir_c(extractedRoot, &match)) {
        free(match);
        return NULL;
    }

    return match;
}
