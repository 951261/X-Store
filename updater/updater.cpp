#include "updater.h"

// Standard Libaries
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <xtl.h>

// Local libraries/code
#include <string>
#include <downloadFile.h>
#include <OutputConsole.h>
#include <settings.h>
#include <file-stuff.h>
#include <miniz.h>

// header files
#include "cJSON.h"

static bool IsUnsafeZipPath(const char *name)
{
    if (name == NULL || name[0] == '\0')
        return true;

    // Block absolute paths and drive/device paths.
    if (name[0] == '/' || name[0] == '\\' || strchr(name, ':') != NULL)
        return true;

    // Block zip-slip paths.
    if (strstr(name, "../") || strstr(name, "..\\") || strcmp(name, "..") == 0)
        return true;

    return false;
}

static void NormalizeZipPath(char *path)
{
    for (int i = 0; path[i] != '\0'; ++i)
    {
        if (path[i] == '/')
            path[i] = '\\';
    }
}

static bool EnsureParentFolderExists(const char *filePath)
{
    char folder[MAX_TEXT_LENGTH];
    const char *lastSlash = strrchr(filePath, '\\');

    if (lastSlash == NULL)
        return true;

    size_t folderLen = (size_t)(lastSlash - filePath);
    if (folderLen == 0 || folderLen >= sizeof(folder))
        return false;

    memcpy(folder, filePath, folderLen);
    folder[folderLen] = '\0';

    return customForceMkdir(folder) == 0;
}

int unzipFileToFolder(const char *zipPath, const char *outputFolder)
{
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));

    if (zipPath == NULL || outputFolder == NULL)
        return EXIT_FAILURE;

    if (customForceMkdir(outputFolder) != 0)
        return EXIT_FAILURE;

    if (!mz_zip_reader_init_file(&zip, zipPath, 0))
        return EXIT_FAILURE;

    mz_uint fileCount = mz_zip_reader_get_num_files(&zip);

    for (mz_uint i = 0; i < fileCount; ++i)
    {
        mz_zip_archive_file_stat stat;
        char outPath[MAX_TEXT_LENGTH];

        if (!mz_zip_reader_file_stat(&zip, i, &stat))
            goto fail;

        if (IsUnsafeZipPath(stat.m_filename))
            goto fail;

        _snprintf(outPath, sizeof(outPath), "%s\\%s", outputFolder, stat.m_filename);
        outPath[sizeof(outPath) - 1] = '\0';
        NormalizeZipPath(outPath);

        if (mz_zip_reader_is_file_a_directory(&zip, i))
        {
            if (customForceMkdir(outPath) != 0)
                goto fail;
            continue;
        }

        if (!EnsureParentFolderExists(outPath))
            goto fail;

        if (!mz_zip_reader_extract_to_file(&zip, i, outPath, 0))
            goto fail;
    }

    mz_zip_reader_end(&zip);
    return EXIT_SUCCESS;

fail:
    mz_zip_reader_end(&zip);
    return EXIT_FAILURE;
}

static bool parseVersionString(const char *versionString, int versionParts[3])
{
    if (versionString == NULL || versionParts == NULL)
        return false;

    versionParts[0] = 0;
    versionParts[1] = 0;
    versionParts[2] = 0;

    while (isspace((unsigned char)*versionString))
        versionString++;

    if (*versionString == 'v' || *versionString == 'V')
        versionString++;

    for (int part = 0; part < 3; part++)
    {
        char *end = NULL;
        unsigned long value = strtoul(versionString, &end, 10);

        if (end == versionString)
            return false;

        versionParts[part] = (int)value;
        versionString = end;

        if (*versionString != '.')
            break;

        versionString++;
    }

    return *versionString == '\0' ||
           *versionString == '-' ||
           *versionString == '+' ||
           isspace((unsigned char)*versionString);
}

static int compareVersionStrings(const char *availableVersion, const char *currentVersion)
{
    int availableParts[3];
    int currentParts[3];

    if (!parseVersionString(availableVersion, availableParts) ||
        !parseVersionString(currentVersion, currentParts))
    {
        return 0;
    }

    for (int i = 0; i < 3; i++)
    {
        if (availableParts[i] > currentParts[i])
            return 1;
        if (availableParts[i] < currentParts[i])
            return -1;
    }

    return 0;
}

static const unsigned long long UPDATE_INFO_BUFFER_SIZE = 4ULL * 1024ULL * 1024ULL;
static const size_t UPDATE_URL_BUFFER_SIZE = 1024 * 5;
static const int UPDATE_MAX_REDIRECTS = 5;
static const char *UPDATE_INFO_URL = "https://api.github.com/repos/951261/X-Store/releases/latest";
static const char *UPDATE_TEMP_XEX = "game:\\XStoreUp.xex";

static bool downloadLatestReleaseInfo(char *buffer, unsigned long long *bufferSize)
{
    if (downloadFileHTTPS(UPDATE_INFO_URL, "", buffer, bufferSize, false, dprintf) != 200)
    {
        dprintf("Download update info failed \n");
        return false;
    }

    return true;
}

static bool getFirstAssetDownloadURL(cJSON *json, std::string *updateURL)
{
    if (json == NULL || updateURL == NULL)
        return false;

    cJSON *assets = cJSON_GetObjectItemCaseSensitive(json, "assets");
    if (!cJSON_IsArray(assets))
    {
        log_printf("Error updating: Assets is not an array\n");
        return false;
    }

    cJSON *item = cJSON_GetArrayItem(assets, 0);
    if (!cJSON_IsObject(item))
    {
        log_printf("Error updating: item is not an object\n");
        return false;
    }

    cJSON *url = cJSON_GetObjectItemCaseSensitive(item, "browser_download_url");
    if (!cJSON_IsString(url) || url->valuestring == NULL)
    {
        log_printf("Error updating: url is not a string\n");
        return false;
    }

    updateURL->assign(url->valuestring);
    return true;
}

static bool makeGamePath(const char *fileName, char *outputPath, size_t outputPathSize)
{
    if (fileName == NULL || outputPath == NULL || outputPathSize == 0)
        return false;

    int written = _snprintf(outputPath, outputPathSize, "game:\\%s", fileName);
    if (written < 0 || (size_t)written >= outputPathSize)
    {
        outputPath[0] = '\0';
        return false;
    }

    return true;
}

static bool findMainXexPath(char *mainXexFile, size_t mainXexFileSize)
{
    char foundFile[MAX_TEXT_LENGTH];

    if (findFile("game:\\", foundFile, MAX_TEXT_LENGTH, "Store.xex") != EXIT_SUCCESS)
    {
        log_printf("Failed to find file ending in Store.xex\n");
        if (findFile("game:\\", foundFile, MAX_TEXT_LENGTH, ".xex") != EXIT_SUCCESS) // Find the first .xex file as a fallback
        {
            log_printf("Failed to find file ending in .xex\n");

            return false;
        }
    }

    if (!makeGamePath(foundFile, mainXexFile, mainXexFileSize))
    {
        log_printf("Error updating: main XEX path is too long\n");
        return false;
    }

    return true;
}

static bool makeBackupPath(const char *mainXexFile, char *backupXexFile, size_t backupXexFileSize)
{
    if (mainXexFile == NULL || backupXexFile == NULL)
        return false;

    size_t mainLength = strlen(mainXexFile);
    if (mainLength + strlen(".old") >= backupXexFileSize)
    {
        backupXexFile[0] = '\0';
        return false;
    }

    strcpy(backupXexFile, mainXexFile);
    strcat(backupXexFile, ".old");
    return true;
}

static int downloadUpdateAsset(std::string updateURL, const char *outputFile)
{
    const char tmpDownloadZipFile[MAX_TEXT_LENGTH] = "game:\\tmp_update\\X-Store_Update.zip";
    const char tmpUpdateFolder[MAX_TEXT_LENGTH] = "game:\\tmp_update";
    int httpStatusCode = 0;

    // create tmp folder
    if (customForceMkdir(tmpUpdateFolder) != EXIT_SUCCESS)
    {
        log_printf("Error: Cannot create tmp folder %s\n", tmpUpdateFolder);
        return EXIT_FAILURE;
    }

    log_printf("Downloading Update zip file\n");

    for (int redirectCount = 0; redirectCount <= UPDATE_MAX_REDIRECTS; redirectCount++)
    {
        char redirectURL[UPDATE_URL_BUFFER_SIZE];
        unsigned long long redirectURLSize = sizeof(redirectURL);
        redirectURL[0] = '\0';

        httpStatusCode = downloadFileHTTPS(updateURL, tmpDownloadZipFile, redirectURL, &redirectURLSize, true, dprintf);
        log_printf("Update download status code %d\n", httpStatusCode);

        if (httpStatusCode == 200)
            break;

        if (httpStatusCode != 302)
        {
            log_printf("Error updating: download failed with HTTP status %d\n", httpStatusCode);
            return EXIT_FAILURE;
        }

        if (redirectURL[0] == '\0')
        {
            log_printf("Error updating: redirect URL was empty\n");
            return EXIT_FAILURE;
        }

        updateURL.assign(redirectURL);
    }

    if (httpStatusCode != 200)
    {
        log_printf("Error updating: too many redirects\n");
        return EXIT_FAILURE;
    }

    log_printf("Unzipping update data\n");
    unzipFileToFolder(tmpDownloadZipFile, tmpUpdateFolder);

    char tmp[MAX_TEXT_LENGTH] = "";
    if (findFile(tmpUpdateFolder, tmp, MAX_TEXT_LENGTH, ".xex") != EXIT_SUCCESS)
    { // find .xex file
        log_printf("Error updating: cannot find .xex file\n");
        return EXIT_FAILURE;
    }

    char mainXexFile[MAX_TEXT_LENGTH] = "";
    sprintf(mainXexFile, "%s\\%s", tmpUpdateFolder, tmp);

    // move file
    log_printf("Creating new xex file\n");
    if (rename(mainXexFile, outputFile) != 0)
    { // find .xex file
        log_printf("Error updating: cannot move %s to %s\n", mainXexFile, outputFile);
        return EXIT_FAILURE;
    }

    // cleanup
    log_printf("Cleaning up update data\n");
    deleteDirectory(tmpUpdateFolder, MAX_TEXT_LENGTH);

    return EXIT_SUCCESS;
}

static int replaceMainXex(const char *mainXexFile, const char *tempXexFile, const char *backupXexFile)
{
    remove(backupXexFile);

    if (rename(mainXexFile, backupXexFile) != 0)
    {
        log_printf("Error updating: failed to back up %s\n", mainXexFile);
        return EXIT_FAILURE;
    }

    if (rename(tempXexFile, mainXexFile) != 0)
    {
        log_printf("Error updating: failed to install %s\n", tempXexFile);

        if (rename(backupXexFile, mainXexFile) != 0)
        {
            log_printf("Error updating: failed to restore backup %s\n", backupXexFile);
        }

        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

bool isUpdateAvailable()
{
    char *buffer = (char *)malloc(UPDATE_INFO_BUFFER_SIZE);

    if (buffer == NULL)
    {
        dprintf("ERROR: Malloc failed\n\n");
        return false;
    }

    unsigned long long bufferSize = UPDATE_INFO_BUFFER_SIZE;

    if (!downloadLatestReleaseInfo(buffer, &bufferSize))
    { // downloads into a null terminated buffer
        free(buffer);
        return false;
    }

    // Update data is now in buffer. Parse the JSON

    cJSON *json = cJSON_Parse(buffer);
    if (json == NULL)
    {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL)
        {
            dprintf("Error: %s\n", error_ptr);
        }
        cJSON_Delete(json);
        free(buffer);
        return false;
    }

    // access the JSON data
    cJSON *version = cJSON_GetObjectItemCaseSensitive(json, "tag_name");
    if (!cJSON_IsString(version) || (version->valuestring == NULL))
    {
        log_printf("Error updating: tag name is not a string\n");
        cJSON_Delete(json);
        free(buffer);
        return false;
    }

    char versionString[30] = "";

    strncpy(versionString, version->valuestring, sizeof(versionString) - 1);
    versionString[sizeof(versionString) - 1] = '\0';

    bool updateAvailable = compareVersionStrings(versionString, CURRENT_VERSION) > 0;

    // delete the JSON object
    cJSON_Delete(json);
    free(buffer);
    return updateAvailable;
}

int runUpdate()
{
    char *buffer = (char *)malloc(UPDATE_INFO_BUFFER_SIZE);
    char mainXexFile[MAX_TEXT_LENGTH];

    if (buffer == NULL)
    {
        dprintf("ERROR: Malloc failed\n\n");
        return EXIT_FAILURE;
    }

    unsigned long long bufferSize = UPDATE_INFO_BUFFER_SIZE;
    cJSON *json = NULL;
    int result = EXIT_FAILURE;

    do
    {
        if (!downloadLatestReleaseInfo(buffer, &bufferSize))
            break;

        json = cJSON_Parse(buffer);
        if (json == NULL)
        {
            const char *error_ptr = cJSON_GetErrorPtr();
            if (error_ptr != NULL)
            {
                dprintf("Error: %s\n", error_ptr);
            }
            break;
        }

        std::string updateURL;
        if (!getFirstAssetDownloadURL(json, &updateURL))
            break;

        if (!findMainXexPath(mainXexFile, sizeof(mainXexFile)))
            break;

        char backupXexFile[MAX_TEXT_LENGTH];
        if (!makeBackupPath(mainXexFile, backupXexFile, sizeof(backupXexFile)))
        {
            log_printf("Error updating: backup XEX path is too long\n");
            break;
        }

        remove(UPDATE_TEMP_XEX);

        if (downloadUpdateAsset(updateURL, UPDATE_TEMP_XEX) != EXIT_SUCCESS)
            break;

        if (replaceMainXex(mainXexFile, UPDATE_TEMP_XEX, backupXexFile) != EXIT_SUCCESS)
            break;

        log_printf("Update download complete\n\n");
        result = EXIT_SUCCESS;
    } while (false);

    if (result != EXIT_SUCCESS)
        remove(UPDATE_TEMP_XEX);

    if (json != NULL)
        cJSON_Delete(json);

    free(buffer);

    // now launch the updated version
    if (result == EXIT_SUCCESS)
    {
        XLaunchNewImage(mainXexFile, NULL);
    }

    return result;
}
