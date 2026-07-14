#ifndef UI_H
#define UI_H

#include <string>
#include <vector>
#include <xtl.h>

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
