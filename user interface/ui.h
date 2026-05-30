#ifndef UI_H
#define UI_H

#include <string>
#include <xtl.h>

enum DownloadType {
    ORIGINAL_XBOX = 1,
    XBOX_360,
    XBLA, 
    AUTO_UPDATE
};

DWORD OpenKeyboardToString(
    DWORD userIndex,
    std::string *outputString,
    LPCWSTR title,
    LPCWSTR description,
    LPCWSTR defaultText
);

int showUI(char *gameURL, int len, char *gameName, int gameNameLen);

#endif
