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

#include <settings.h>
#include <extract-xiso.h>
#include <ui.h>

#include <win32/dirent.h>

#include <file-stuff.h>

#define SETTINGS_FILE "game:\\settings.txt"

struct Settings
{
	char outputPath[MAX_TEXT_LENGTH];
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

int findIso(const char *folder, char *isoFile, int len, const char *prefix)
{
	// Source - https://stackoverflow.com/a/612176
	// Posted by Peter Parker, modified by community. See post 'Timeline' for change history
	// Retrieved 2026-04-26, License - CC BY-SA 4.0

	std::cout << "\nSearching for ISO in " << folder << "\n";

	DIR *dir;
	struct dirent *ent;
	if ((dir = opendir(folder)) != NULL)
	{
		/* print all the files and directories within directory */
		while ((ent = readdir(dir)) != NULL)
		{
			std::cout << ent->d_name << "\n";
			int nameLength = strlen(ent->d_name);

			if (strlen(ent->d_name) > strlen(prefix) && strcmp(&(ent->d_name[nameLength - strlen(prefix)]), prefix) == 0)
			{ // identical strings
				strncpy(isoFile, ent->d_name, len);
				closedir(dir);
				return (strlen(ent->d_name) < len) ? EXIT_SUCCESS : EXIT_FAILURE;
			}
		}
		closedir(dir);
	}
	else
	{
		/* could not open directory */
		std::cout << "Directory not found\n";
		perror("");
		return EXIT_FAILURE;
	}
	return -1;
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

	if (customForceMkdir(isoFolder.c_str()) != EXIT_SUCCESS) {
		dprintf("Warning, failed to create %s \n", isoFolder.c_str());
	}

	dprintf("Download complete, beginning extraction of %s \n", sevenZipFile.c_str());

	if (decompressSevenZipFile(sevenZipFile.c_str(), isoFolder.c_str(), (downloadType == XBLA) ) == EXIT_FAILURE)
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
		return EXIT_SUCCESS;
	}

	char isoFile[MAX_TEXT_LENGTH];
	char temp[MAX_TEXT_LENGTH];

	if (findIso(isoFolder.c_str(), isoFile, sizeof(isoFile), ".iso.001") != EXIT_SUCCESS)
	{
		if (findIso(isoFolder.c_str(), isoFile, sizeof(isoFile), ".iso") != EXIT_SUCCESS)
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

	if (customForceMkdir(outputFolder.c_str()) != EXIT_SUCCESS) {
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

static bool IsInvalidFolderChar(char c)
{
	return c == '<' || c == '>' || c == ':' || c == '"' ||
		   c == '/' || c == '\\' || c == '|' || c == '?' || c == '*' || c == '+' || c == ',' || c == '=';
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

struct Settings getSettings()
{
	struct Settings settings;

	strcpy(settings.outputPath, "Usb0:"); // default back to where ever the xex file was launched from

	FILE *fd = fopen(SETTINGS_FILE, "r");

	if (fd == NULL)
	{
		dprintf("Failed to open settings file\n");
		return settings;
	}

	char buff[MAX_TEXT_LENGTH];

	while (fgets(buff, sizeof(buff), fd) != NULL)
	{
		if (buff[0] == '#')
		{
			continue; // comment
		}

		if (strncmp(buff, "output-path: ", strlen("output-path: ")) == 0)
		{
			char *value = &buff[strlen("output-path: ")];

			value[strcspn(value, "\r\n")] = '\0';

			strncpy(settings.outputPath, value, MAX_TEXT_LENGTH - 1);

			settings.outputPath[MAX_TEXT_LENGTH - 1] = '\0'; // ensure the string is NULL terminated
		}
	}

	fclose(fd);

	return settings;
}

int main()
{
	remove(LOG_FILE_PATH);

	MakeConsole("embed:\\font", CONSOLE_COLOR_BLACK, CONSOLE_COLOR_WHITE);
	if (!CheckGameMounted())
		dprintf("Warning: Some paths may not be mounted\n");

	while (true)
	{

		dprintf("X Store store 0.2.2 beta\n");

		char selectedGameURL[MAX_TEXT_LENGTH] = "";
		char selectedGameName[MAX_TEXT_LENGTH] = "";
		char safeGameFolderName[MAX_TEXT_LENGTH] = "";
		char outputFolder[MAX_TEXT_LENGTH] = "";

		const int downloadType = showUI(selectedGameURL, sizeof(selectedGameURL), selectedGameName, sizeof(selectedGameName));

		if (downloadType >= 0)
		{
			dprintf("Selected game URL: %s\n", selectedGameURL);
			dprintf("Selected game: %s\n", selectedGameName);
		}
		else
		{
			dprintf("Failed to search for a game\n");
			goto exitFailed;
		}

		struct Settings settings = getSettings();

		MakeSafeFolderName(selectedGameName, safeGameFolderName, FATX_SAFE_FOLDER_NAME_LEN);
		_snprintf(outputFolder, sizeof(outputFolder), "%s\\%s", settings.outputPath, safeGameFolderName);
		outputFolder[sizeof(outputFolder) - 1] = '\0';

		dprintf("Extracting to: %s\n", outputFolder);

		int downloadStatus = -1;

		if (downloadType == XBLA)
		{
			downloadStatus = getGame(std::string(selectedGameURL), "game:\\tmp.7z.001", outputFolder, "", downloadType);
		}
		else
		{
			downloadStatus = getGame(std::string(selectedGameURL), "game:\\tmp.7z.001", "game:\\tmp_output", outputFolder, downloadType);
		}

		if (downloadStatus == EXIT_SUCCESS)
		{
			return EXIT_SUCCESS;
		}

	exitFailed:
		dprintf("Something went wrong! Press Y to search again, or B to exit");

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
