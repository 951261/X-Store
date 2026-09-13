#ifndef UI_H
#define UI_H

#include <string>
#include <vector>
#include <xtl.h>

// This .json file contains the download links for Xbox games
#define ORIGINAL_XBOX_GAMES_LIST "https://gist.githubusercontent.com/951261/4d8a70e45a0e1c2e9ef4d32c5f95a9d4/raw/"
#define XBOX360_GAMES_LIST "https://gist.githubusercontent.com/951261/6bab0fe66c22f7b5c1e0a1afc3719f1f/raw/"
#define XBLA_GAMES_LIST "https://gist.githubusercontent.com/951261/e24ffb4d7793f7e992f237425a502765/raw/"

enum DownloadType {
    ORIGINAL_XBOX = 1,
    XBOX_360,
    XBLA, 
    AUTO_UPDATE,
    DOWNLOAD_QUEUE
};

struct GameData
{
	int downloadType;
	char selectedGameURL[MAX_TEXT_LENGTH];
	char selectedGameName[MAX_TEXT_LENGTH];
	char safeGameFolderName[MAX_TEXT_LENGTH];
	char outputFolder[MAX_TEXT_LENGTH];
};

DWORD OpenKeyboardToString(
    DWORD userIndex,
    std::string *outputString,
    LPCWSTR title,
    LPCWSTR description,
    LPCWSTR defaultText
);

std::vector<GameData> showUI();

#endif
