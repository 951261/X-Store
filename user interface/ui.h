#ifndef UI_H
#define UI_H

#include <string>
#include <vector>
#include <xtl.h>

#include "settings.h"

#define XBLA_DOWNLOAD_BASE_URL "https://archive.org"

const std::string XBOX_URL_PATHS[] = {
    "microsoft_xbox_numberssymbols",
    "microsoft_xbox_a",
    "microsoft_xbox_b",
    "microsoft_xbox_c_part1",
    "microsoft_xbox_c_part2",
    "microsoft_xbox_d_part1",
    "microsoft_xbox_d_part2",
    "microsoft_xbox_e",
    "microsoft_xbox_f",
    "microsoft_xbox_g",
    "microsoft_xbox_h",
    "microsoft_xbox_i",
    "microsoft_xbox_j",
    "microsoft_xbox_k",
    "microsoft_xbox_l",
    "microsoft_xbox_m_part1",
    "microsoft_xbox_m_part2",
    "microsoft_xbox_n_part1",
    "microsoft_xbox_n_part2",
    "microsoft_xbox_o_part1",
    "microsoft_xbox_o_part2",
    "microsoft_xbox_p",
    "microsoft_xbox_q",
    "microsoft_xbox_r",
    "microsoft_xbox_s_part1",
    "microsoft_xbox_s_part2",
    "microsoft_xbox_t_part1",
    "microsoft_xbox_t_part2",
    "microsoft_xbox_u",
    "microsoft_xbox_v",
    "microsoft_xbox_w",
    "microsoft_xbox_x",
    "microsoft_xbox_y",
    "microsoft_xbox_z",
};

const std::string X360_URL_PATHS[] = {
    "microsoft_xbox360_numberssymbols",
    "microsoft_xbox360_a_part1",
    "microsoft_xbox360_a_part2",
    "microsoft_xbox360_b_part1",
    "microsoft_xbox360_b_part2",
    "microsoft_xbox360_c_part1",
    "microsoft_xbox360_c_part2",
    "microsoft_xbox360_d_part1",
    "microsoft_xbox360_d_part2",
    "microsoft_xbox360_d_part3",
    "microsoft_xbox360_e",
    "microsoft_xbox360_f_part1",
    "microsoft_xbox360_f_part2",
    "microsoft_xbox360_g",
    "microsoft_xbox360_h",
    "microsoft_xbox360_i",
    "microsoft_xbox360_j",
    "microsoft_xbox360_k",
    "microsoft_xbox360_l",
    "microsoft_xbox360_m_part1",
    "microsoft_xbox360_m_part2",
    "microsoft_xbox360_n_part1",
    "microsoft_xbox360_n_part2",
    "microsoft_xbox360_o",
    "microsoft_xbox360_p",
    "microsoft_xbox360_q",
    "microsoft_xbox360_r",
    "microsoft_xbox360_s_part1",
    "microsoft_xbox360_s_part2",
    "microsoft_xbox360_t_part1",
    "microsoft_xbox360_t_part2",
    "microsoft_xbox360_u",
    "microsoft_xbox360_v",
    "microsoft_xbox360_w",
    "microsoft_xbox360_x_part1",
    "microsoft_xbox360_x_part2",
    "microsoft_xbox360_y",
    "microsoft_xbox360_z"
};

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
    DOWNLOAD_QUEUE,
    IA_LOGIN
};

struct GameData
{
	int downloadType;
	char selectedGameURL[MAX_TEXT_LENGTH];
	char selectedGameName[MAX_TEXT_LENGTH];
	char safeGameFolderName[MAX_TEXT_LENGTH];
	char outputFolder[MAX_TEXT_LENGTH];
    char fileFormat[MAX_TEXT_LENGTH];
};

DWORD OpenKeyboardToString(
    DWORD userIndex,
    std::string *outputString,
    LPCWSTR title,
    LPCWSTR description,
    LPCWSTR defaultText
);

std::string UrlEncodeQuery(const std::string &value); // needed in JSON_parser.cpp

bool isCached(const std::string path);
bool loadCache(std::string path, char * buffer);
bool writeCache(std::string path, const char * buffer);
bool clearCache();

std::vector<GameData> showUI();

#endif
