/*
FILE : main.cpp
PROJECT : xstore
PROGRAMMER : 951261
DESCRIPTION : READ THE FILE NAME (Haha). This is the main file that coordinates everyting.
*/

#include "OutputConsole.h"
#include "AtgConsole.h"
#include "AtgInput.h"
#include "AtgUtil.h"
#include "Corona4G.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <iostream>
#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "downloadFile.h"
#include "decompress7z.h"
#include "xblaParsing.h"

#include <settings.h>
#include <extract-xiso.h>
#include <ui.h>

#include <win32/dirent.h>

#include <file-stuff.h>
#include <vector>
#include <algorithm>
#include <string>

#define SETTINGS_FILE "game:\\settings.txt"

struct Settings
{
	char originalXboxPath[MAX_TEXT_LENGTH];
	char xbox360Path[MAX_TEXT_LENGTH];
	char xblaPath[MAX_TEXT_LENGTH];
	char legacyPath[MAX_TEXT_LENGTH];
};

bool CheckGameMounted()
{
	FILE *fd;
	FILE *fd1;
	if (fopen_s(&fd, "game:\\test.tmp", "w") != 0)
	{
		dprintf("GAME_NOT_MOUNTED_TRYING_USB\n");
		// fclose(fd);
		if (mount("game:", "\\Device\\Mass0") != 0)
		{
			dprintf("GAME_NOT_MOUNTED_TRYING_HDD\n");
			if (mount("game:", "\\Device\\Harddisk0\\Partition1") != 0)
			{
				dprintf("GAME_NOT_MOUNTED\n");
				// return false;
			}
		}
	}
	else
	{
		fclose(fd);
		remove("game:\\test.tmp");
	}

	fd = NULL;
	fd1 = NULL;

	// Check USB0 is mounted
	if (fopen_s(&fd1, "Usb0:\\test.tmp", "w") != 0)
	{
		if (mount("Usb0:", "\\Device\\Mass0") != 0)
		{
			dprintf("Warning: USB0 not mounted\n");
			// return false;
		}
	}
	else
	{
		fclose(fd1);
		remove("Usb0:\\test.tmp");
	}

	fd = NULL;
	fd1 = NULL;

	// Check USB1 is mounted
	if (fopen_s(&fd1, "Usb1:\\test.tmp", "w") != 0)
	{
		if (mount("Usb1:", "\\Device\\Mass1") != 0)
		{
			dprintf("Warning: USB1 not mounted\n");
			// return false;
		}
	}
	else
	{
		fclose(fd1);
		remove("Usb1:\\test.tmp");
	}

	fd = NULL;
	fd1 = NULL;

	// Check HDD is mounted
	if (fopen_s(&fd1, "Hdd:\\test.tmp", "w") != 0)
	{
		if (mount("Hdd:", "\\Device\\Harddisk0\\Partition1") != 0)
		{
			dprintf("Warning: Hdd not mounted\n");
			// return false;
		}
	}
	else
	{
		fclose(fd1);
		remove("Hdd:\\test.tmp");
	}
	return true;
}

static bool IsDigitString(const char *text)
{
	if (!text || !*text)
		return false;

	for (int i = 0; text[i] != '\0'; ++i)
	{
		if (!isdigit((unsigned char)text[i]))
			return false;
	}

	return true;
}

static bool StartsWith(const char *text, const char *prefix)
{
	return strncmp(text, prefix, strlen(prefix)) == 0;
}

static int DeleteSplitFiles(const char *firstPartFile)
{
	char directory[512];
	char fileName[256];
	const char *lastSlash = strrchr(firstPartFile, '\\');
	const char *lastForwardSlash = strrchr(firstPartFile, '/');

	if (!lastSlash || (lastForwardSlash && lastForwardSlash > lastSlash))
		lastSlash = lastForwardSlash;

	if (lastSlash)
	{
		size_t directoryLen = (lastSlash - firstPartFile) + 1;
		if (directoryLen >= sizeof(directory))
			return EXIT_FAILURE;

		strncpy(directory, firstPartFile, directoryLen);
		directory[directoryLen] = '\0';
		strncpy(fileName, lastSlash + 1, sizeof(fileName) - 1);
		fileName[sizeof(fileName) - 1] = '\0';
	}
	else
	{
		strcpy(directory, ".\\");
		strncpy(fileName, firstPartFile, sizeof(fileName) - 1);
		fileName[sizeof(fileName) - 1] = '\0';
	}

	char *lastDot = strrchr(fileName, '.');
	if (!lastDot || !IsDigitString(lastDot + 1))
	{
		return remove(firstPartFile);
	}

	char splitPrefix[256];
	size_t splitPrefixLen = (lastDot - fileName) + 1;
	if (splitPrefixLen >= sizeof(splitPrefix))
		return EXIT_FAILURE;

	strncpy(splitPrefix, fileName, splitPrefixLen);
	splitPrefix[splitPrefixLen] = '\0';

	DIR *dir = opendir(directory);
	if (!dir)
		return remove(firstPartFile);

	int result = EXIT_SUCCESS;
	struct dirent *ent;
	while ((ent = readdir(dir)) != NULL)
	{
		if (!StartsWith(ent->d_name, splitPrefix))
			continue;

		if (!IsDigitString(ent->d_name + splitPrefixLen))
			continue;

		char fileToDelete[512];
		if (strlen(directory) + strlen(ent->d_name) >= sizeof(fileToDelete))
		{
			result = EXIT_FAILURE;
			continue;
		}

		strcpy(fileToDelete, directory);
		strcat(fileToDelete, ent->d_name);

		if (remove(fileToDelete) != 0)
			result = EXIT_FAILURE;
	}

	closedir(dir);
	return result;
}

std::vector<std::string> split(const std::string &str, char delimiter)
{
	std::vector<std::string> tokens;
	size_t start = 0;
	size_t end = str.find(delimiter);

	while (end != std::string::npos)
	{
		tokens.push_back(str.substr(start, end - start));
		start = end + 1;
		end = str.find(delimiter, start);
	}

	tokens.push_back(str.substr(start));
	return tokens;
}

static std::string removeTrailingSlash(std::string str)
{
	while (str.length() > 0 && (str[str.length() - 1] == '\\' || str[str.length() - 1] == '/'))
	{
		if (!str.empty())
		{
			str.resize(str.size() - 1);
		}
	}

	return str;
}

static std::string getPathNameIndex(std::string str, const int index)
{
	if (str.find("\\\\") != std::string::npos || str.find("//") != std::string::npos)
	{
		dprintf("ERROR: Failed to parse path\n");
		return std::string("");
	}

	std::replace(str.begin(), str.end(), '/', '\\'); // clean up the path to make splitting easier

	str = removeTrailingSlash(str);

	std::vector<std::string> pathSections = split(str, '\\');

	if (index < 0)
	{
		return pathSections[pathSections.size() + index];
	}
	else
	{
		return pathSections[index];
	}
}

int getGame(std::string URL, const std::string sevenZipFile, const std::string isoFolder, const std::string outputFolder, const int downloadType)
{
	std::string backupDomain = SECONDARY_DOWNLOAD_DOMAIN;
	std::string expectedDomain = DOWNLOAD_DOMAIN;

	// goto skipDownloadAndExtract;

	int httpStatus = downloadFileHTTPS(URL, sevenZipFile, NULL, NULL, true, dprintf);
	if (httpStatus < 200)
	{
		return EXIT_FAILURE;
	}
	else if (httpStatus >= 400 && URL.rfind(expectedDomain, 0) == 0)
	{ // page not found. Try different domain.
		URL.replace(0, expectedDomain.length(), backupDomain);

		dprintf("Trying alternate download link\n");

		if (downloadFileHTTPS(URL, sevenZipFile, NULL, NULL, true, dprintf) != 200)
		{
			return EXIT_FAILURE;
		}
	}
	else if (httpStatus == 200)
	{
	}
	else
	{
		return EXIT_FAILURE;
	}

	if (customForceMkdir(isoFolder.c_str()) != EXIT_SUCCESS)
	{
		dprintf("Warning, failed to create %s \n", isoFolder.c_str());
	}

	dprintf("Download complete, beginning extraction of %s \n", sevenZipFile.c_str());

	if (decompressSevenZipFile(sevenZipFile.c_str(), isoFolder.c_str(), (downloadType == XBLA)) == EXIT_FAILURE)
	{
		return EXIT_FAILURE;
	}

	if (DeleteSplitFiles(sevenZipFile.c_str()) != EXIT_SUCCESS)
	{
		dprintf("Warning: failed to delete all 7z parts after 7z extraction\n");
	}

	// skipDownloadAndExtract:

	if (downloadType == XBLA)
	{
		// copy to final DIR
		// When downloading XBLA, isoFolder is actually the output folder
		char *xblaDir = findXblaTitleIdDir(isoFolder.c_str());
		if (xblaDir == NULL)
		{
			dprintf("Failed to find XBLA directory\n");
			return EXIT_FAILURE;
		}

		std::string path = removeTrailingSlash(xblaDir);
		std::string gameID = getPathNameIndex(path, -1); // get final folder in path which SHOULD be the gameID

		free(xblaDir);

		std::string copyDestinationPath = outputFolder;
		copyDestinationPath = removeTrailingSlash(outputFolder) + "\\" + gameID;

		// if (deleteDirectory(copyDestinationPath.c_str(), copyDestinationPath.length() + 2) != EXIT_SUCCESS) { // attempt to delete any old files
		// 	log_printf("Warning: Failed to delete %s \n", copyDestinationPath.c_str());
		// }

		if (customForceMkdir(copyDestinationPath.c_str()) != 0)
		{
			log_printf("Warning: failed to create %s \n", copyDestinationPath.c_str());
		}

		dprintf("Copying files to final directory. This may take a while\n");

		if (copyDirectory(path.c_str(), copyDestinationPath.c_str()) != EXIT_SUCCESS)
		{
			dprintf("Error: directory copy failed to copy %s to %s \n", path.c_str(), copyDestinationPath.c_str());
			return EXIT_FAILURE;
		}

		if (deleteDirectory(isoFolder.c_str(), isoFolder.length() + 2) != EXIT_SUCCESS)
		{
			dprintf("Warning: Failed to remove directory %s \n", path.c_str());
		}

		return EXIT_SUCCESS;
	}

	char isoFile[MAX_TEXT_LENGTH];
	char temp[MAX_TEXT_LENGTH];

	if (findFile(isoFolder.c_str(), isoFile, sizeof(isoFile), ".iso.001") != EXIT_SUCCESS)
	{
		if (findFile(isoFolder.c_str(), isoFile, sizeof(isoFile), ".iso") != EXIT_SUCCESS)
		{
			dprintf("Failed to find .iso.001 or .iso file in %s\n", isoFolder.c_str());
			return EXIT_FAILURE;
		}
	}

	strncpy(temp, isoFolder.c_str(), MAX_TEXT_LENGTH);
	temp[MAX_TEXT_LENGTH - 1] = '\0'; // null terminate

	if (temp[strlen(temp) - 1] != '\\' && temp[strlen(temp) - 1] != '/')
	{
		strcat(temp, "\\");
	}

	strcat(temp, isoFile);

	deleteDirectory(outputFolder.c_str(), MAX_TEXT_LENGTH); // delete the old folder

	if (customForceMkdir(outputFolder.c_str()) != EXIT_SUCCESS)
	{
		dprintf("Warning! Failed to create %s \n", outputFolder.c_str());
	}

	if (extractIso(temp, outputFolder.c_str()) != 0)
	{
		return EXIT_FAILURE;
	}

	// if (DeleteSplitFiles(temp) != EXIT_SUCCESS)
	// {
	// 	dprintf("Warning: failed to delete all ISO parts after ISO extraction\n");
	// }

	char isoFolderToDelete[MAX_TEXT_LENGTH];
	strncpy(isoFolderToDelete, isoFolder.c_str(), sizeof(isoFolderToDelete) - 2);
	isoFolderToDelete[sizeof(isoFolderToDelete) - 2] = '\0';

	size_t isoFolderToDeleteLen = strlen(isoFolderToDelete);
	if (isoFolderToDeleteLen > 0 &&
		isoFolderToDelete[isoFolderToDeleteLen - 1] != '\\' &&
		isoFolderToDelete[isoFolderToDeleteLen - 1] != '/')
	{
		strcat(isoFolderToDelete, "\\");
		isoFolderToDeleteLen++;
	}

	if (deleteDirectory(isoFolderToDelete, isoFolderToDeleteLen) == EXIT_FAILURE)
	{
		dprintf("Warning: failed to delete %s after ISO extraction\n", isoFolder.c_str());
	}

	dprintf("Processing files Success! Your game has been downloaded and processed!\n");

	return EXIT_SUCCESS;
}

static void MakeSafeFolderName(const char *gameName, char *folderName, int folderNameLen)
{
	if (!folderName || folderNameLen <= 0)
		return;

	folderName[0] = '\0';

	if (!gameName || !*gameName)
	{
		strncpy(folderName, "Unknown Game", folderNameLen - 1);
		folderName[folderNameLen - 1] = '\0';
		return;
	}

	int out = 0;
	bool lastWasSpace = true;

	for (int i = 0; gameName[i] != '\0' && out < folderNameLen - 1; ++i)
	{
		unsigned char c = (unsigned char)gameName[i];

		if (IsInvalidFolderChar((char)c) || c < 32)
			continue;

		if (isspace(c))
		{
			if (!lastWasSpace && out < folderNameLen - 1)
			{
				folderName[out++] = ' ';
				lastWasSpace = true;
			}
			continue;
		}

		folderName[out++] = (char)c;
		lastWasSpace = false;
	}

	while (out > 0 && (folderName[out - 1] == ' ' || folderName[out - 1] == '.'))
		out--;

	if (out == 0)
	{
		strncpy(folderName, "Unknown Game", folderNameLen - 1);
		folderName[folderNameLen - 1] = '\0';
		return;
	}

	folderName[out] = '\0';
}

static int parseSettings(const char *setting, const char *settingsFileBuff, char *output)
{
	if (strncmp(settingsFileBuff, setting, strlen(setting)) == 0)
	{
		log_printf("Found original-xbox-path in settings.txt\n\n");

		char *value = strdup(&settingsFileBuff[strlen(setting)]);

		if (value == NULL)
		{
			return -1;
		}

		value[strcspn(value, "\r\n")] = '\0';

		strncpy(output, value, MAX_TEXT_LENGTH - 1);

		output[MAX_TEXT_LENGTH - 1] = '\0'; // ensure the string is NULL terminated

		free(value);
	}

	return EXIT_SUCCESS;
}

struct Settings getSettings()
{
	struct Settings settings;

	strcpy(settings.originalXboxPath, " "); // default back to where ever the xex file was launched from
	strcpy(settings.xbox360Path, " ");		// default back to where ever the xex file was launched from
	strcpy(settings.xblaPath, " ");			// default back to where ever the xex file was launched from
	strcpy(settings.legacyPath, " ");		// default back to where ever the xex file was launched from

	FILE *fd = fopen(SETTINGS_FILE, "r");

	if (fd == NULL)
	{
		dprintf("Failed to open settings file\n");
		return settings;
	}

	struct stat st;
	if (stat(SETTINGS_FILE, &st) == 0)
	{
		log_printf("Settings file size: %ld bytes\n", (long)st.st_size);
	}
	else
	{
		log_printf("Error getting file size");
	}

	const long fileSize = st.st_size;

	char *buff = (char *)malloc(fileSize + 5); // A few extra bytes just to be safe

	while (fgets(buff, fileSize + 5, fd) != NULL)
	{
		if (buff[0] == '#')
		{
			continue; // comment
		}

		parseSettings("original-xbox-path: ", buff, settings.originalXboxPath);
		parseSettings("xbox-360-path: ", buff, settings.xbox360Path);
		parseSettings("xbla-path: ", buff, settings.xblaPath);

		parseSettings("output-path: ", buff, settings.legacyPath);
	}

	if (strlen(settings.originalXboxPath) < 3)
	{
		dprintf("Decrepidation Warning: original-xbox-path not found in settings.txt. Using legacy path\n It is HIGHLY RECOMMENDED to update your settings.txt file. See https://github.com/951261/X-Store for details\n");

		if (strlen(settings.legacyPath) < 3)
		{
			dprintf("ERROR: No legacy path found in settings.txt \n\n");
		}
		else
		{
			strcpy(settings.originalXboxPath, settings.legacyPath);
		}
	}

	if (strlen(settings.xbox360Path) < 3)
	{
		dprintf("Decrepidation Warning: xbox-360-path not found in settings.txt. Using legacy path\n It is HIGHLY RECOMMENDED to update your settings.txt file. See https://github.com/951261/X-Store for details\n");

		if (strlen(settings.legacyPath) < 3)
		{
			dprintf("ERROR: No legacy path found in settings.txt \n\n");
		}
		else
		{
			strcpy(settings.xbox360Path, settings.legacyPath);
		}
	}

	if (strlen(settings.xblaPath) < 3)
	{
		dprintf("Decrepidation Warning: xbla-path not found in settings.txt. Using legacy path\n It is HIGHLY RECOMMENDED to update your settings.txt file. See https://github.com/951261/X-Store for details\n");

		if (strlen(settings.legacyPath) < 3)
		{
			dprintf("ERROR: No legacy path found in settings.txt \n\n");
		}
		else
		{
			strcpy(settings.xblaPath, settings.legacyPath);
		}
	}

	fclose(fd);

	free(buff);

	return settings;
}

int parseGameData(GameData gamesData)
{

	struct Settings settings = getSettings();

	MakeSafeFolderName(gamesData.selectedGameName, gamesData.safeGameFolderName, FATX_SAFE_FOLDER_NAME_LEN);

	switch (gamesData.downloadType)
	{
	case ORIGINAL_XBOX:
		_snprintf(gamesData.outputFolder, sizeof(gamesData.outputFolder), "%s\\%s", settings.originalXboxPath, gamesData.safeGameFolderName);
		break;

	case XBOX_360:
		_snprintf(gamesData.outputFolder, sizeof(gamesData.outputFolder), "%s\\%s", settings.xbox360Path, gamesData.safeGameFolderName);
		break;

	case XBLA:
		_snprintf(gamesData.outputFolder, sizeof(gamesData.outputFolder), "%s\\%s", settings.xblaPath, gamesData.safeGameFolderName);
		break;

	default:
		dprintf("ERROR: Unknown download type: %d \n", gamesData.downloadType);
		return -1;
	}

	gamesData.outputFolder[sizeof(gamesData.outputFolder) - 1] = '\0';

	dprintf("Extracting to: %s\n", gamesData.outputFolder);

	int downloadStatus = -1;

	if (gamesData.downloadType == XBLA)
	{
		downloadStatus = getGame(std::string(gamesData.selectedGameURL), "game:\\tmp.7z.001", gamesData.outputFolder, settings.xblaPath, gamesData.downloadType);
	}
	else
	{
		downloadStatus = getGame(std::string(gamesData.selectedGameURL), "game:\\tmp.7z.001", "game:\\tmp_output", gamesData.outputFolder, gamesData.downloadType);
	}

	if (downloadStatus == EXIT_SUCCESS)
	{
		return EXIT_SUCCESS;
	}

	return EXIT_FAILURE;
}

int main()
{
	remove(LOG_FILE_PATH);

	MakeConsole("embed:\\font", CONSOLE_COLOR_BLACK, CONSOLE_COLOR_WHITE);
	if (!CheckGameMounted())
		dprintf("Warning: Some paths may not be mounted\n");

	while (true)
	{

		dprintf("X Store store " CURRENT_VERSION " beta (https://github.com/951261)\n");

		const std::vector<GameData> gamesData = showUI();

		if (gamesData.size() <= 0)
		{
			dprintf("Failed to search for a game\n");
			goto exitFailed;
		}

		bool allDownloadsSucceeded = true;
		for (std::vector<GameData>::const_iterator game = gamesData.begin(); game != gamesData.end(); ++game)
		{
			dprintf("Downloading %s\n", game->selectedGameName);
			int status = parseGameData(*game);

			if (status != EXIT_SUCCESS)
			{
				allDownloadsSucceeded = false;
				break;
			}
		}

		if (allDownloadsSucceeded)
			return EXIT_SUCCESS;

	exitFailed:
		dprintf("Something went wrong! Press Y to search again, or B to exit\n");

		XINPUT_STATE state;
		ZeroMemory(&state, sizeof(state));

		while (XInputGetState(0, &state) != 0)
		{
			ZeroMemory(&state, sizeof(state));
		}

		Sleep(250);

		while (true)
		{
			ZeroMemory(&state, sizeof(state));

			if (XInputGetState(0, &state) == ERROR_SUCCESS)
			{
				if (state.Gamepad.wButtons & XINPUT_GAMEPAD_B)
				{
					return EXIT_FAILURE;
				}
				else if (state.Gamepad.wButtons & XINPUT_GAMEPAD_Y)
				{
					break;
				}
			}

			Sleep(50);
		}
	}

	return EXIT_FAILURE;
}
