#ifndef UI_H
#define UI_H

#include <string>
#include <vector>
#include <xtl.h>

#include "settings.h"

#define XBLA_DOWNLOAD_BASE_URL "https://archive.org"

const std::string XBLA_URL_PATHS[] = {
    "microsoft_xbox360_digital_part1",
    "microsoft_xbox360_digital_part2",
    "microsoft_xbox360_digital_part3",
    "microsoft_xbox360_digital_part4",
    "microsoft_xbox360_digital_part5",
    "microsoft_xbox360_digital_part6",
    "microsoft_xbox360_digital_part7",

    "microsoft_xbox360_title-updates", // Title Updates

    // DLC
    "XBOX_360_DLC_1",
    "XBOX_360_DLC_2",
    "XBOX_360_DLC_3",
    "XBOX_360_DLC_4",
    "XBOX_360_DLC_5",
    "XBOX_360_DLC_6",

    // XBLIG
    "XBOX_360_XBLIG_1",
    "XBOX_360_XBLIG_2",
    "XBOX_360_XBLIG_3",
    "XBOX_360_XBLIG_4"
    
};

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

std::string UrlEncodeQuery(const std::string &value); // needed in JSON_parser.cpp

std::vector<GameData> showUI();

#endif
