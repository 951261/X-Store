/*
FILE : ui.cpp
PROJECT : xstore
PROGRAMMER : 951261
DESCRIPTION : Manages and displays the UI
MAIN FUNCTION : showUI
*/

#include <xtl.h>

#include <stdio.h>
#include <ctype.h>
#include <iostream>
#include <string>

#include <OutputConsole.h>
#include <downloadFile.h>
#include <settings.h>

#include "ui.h"
#include "vimms_lair.h"

#define TEXTBUFFER_SIZE (1024 * 10)
#define HTML_BUFFER_CAPACITY (1024 * 1024 * 4);


static const char *GetDownloadTypeName(enum DownloadType type)
{
    switch (type)
    {
    case ORIGINAL_XBOX:
        return "Original Xbox";

    case XBOX_360:
        return "Xbox 360";

    case XBLA:
        return "XBLA";

    default:
        return "Unknown";
    }
}

static enum DownloadType GetDownloadTypeFromIndex(int index)
{
    switch (index)
    {
    case 0:
        return ORIGINAL_XBOX;

    case 1:
        return XBOX_360;

    case 2:
        return XBLA;

    default:
        return XBLA;
    }
}

static std::string UrlEncodeQuery(const std::string &value)
{
    static const char hex[] = "0123456789ABCDEF";
    std::string encoded;

    for (size_t i = 0; i < value.length(); ++i)
    {
        unsigned char c = (unsigned char)value[i];

        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
        {
            encoded += (char)c;
        }
        else if (c == ' ')
        {
            encoded += '+';
        }
        else
        {
            encoded += '%';
            encoded += hex[c >> 4];
            encoded += hex[c & 0x0F];
        }
    }

    return encoded;
}

static void RenderDownloadTypeMenu(int selected)
{
    char outputTextBuffer[TEXTBUFFER_SIZE] = " ";

    ClearConsole();
    _snprintf(outputTextBuffer, TEXTBUFFER_SIZE - strlen(outputTextBuffer), "Choose system\n\n");

    for (int i = 0; i < 3; ++i)
    {
        enum DownloadType type = GetDownloadTypeFromIndex(i);
        _snprintf(outputTextBuffer + strlen(outputTextBuffer), TEXTBUFFER_SIZE - strlen(outputTextBuffer), "%c %s\n",
                i == selected ? '>' : ' ',
                GetDownloadTypeName(type));
    }

    _snprintf(outputTextBuffer + strlen(outputTextBuffer), TEXTBUFFER_SIZE - strlen(outputTextBuffer), "\nA: Select   B: Back   D-Pad: Move\n");

    dprintf("%s", outputTextBuffer);
}

static enum DownloadType ShowDownloadTypeMenu()
{
    int selected = 0;
    WORD previousButtons = 0;

    RenderDownloadTypeMenu(selected);

    while (true)
    {
        XINPUT_STATE state;
        ZeroMemory(&state, sizeof(state));

        if (XInputGetState(0, &state) != ERROR_SUCCESS)
        {
            Sleep(50);
            continue;
        }

        WORD buttons = state.Gamepad.wButtons;
        WORD pressed = buttons & ~previousButtons;
        previousButtons = buttons;

        if (pressed & XINPUT_GAMEPAD_A)
            return GetDownloadTypeFromIndex(selected);

        if (pressed & XINPUT_GAMEPAD_B)
            return (enum DownloadType)0;

        if (pressed & XINPUT_GAMEPAD_DPAD_UP)
            selected--;

        if (pressed & XINPUT_GAMEPAD_DPAD_DOWN)
            selected++;

        if (selected < 0)
            selected = 0;

        if (selected >= 3)
            selected = 2;

        if (pressed & (XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_DOWN))
            RenderDownloadTypeMenu(selected);

        Sleep(50);
    }
}

static void RenderSearchResults(const GameList *list, int selected, int scroll)
{
    const int visibleRows = 20;
    char outputTextBuffer[TEXTBUFFER_SIZE] = " ";

    ClearConsole();
    _snprintf(outputTextBuffer, TEXTBUFFER_SIZE - strlen(outputTextBuffer), "Vimm's Lair search results\n\n");

    if (!list || list->count == 0)
    {
        _snprintf(outputTextBuffer + strlen(outputTextBuffer), TEXTBUFFER_SIZE - strlen(outputTextBuffer), "No games found.\n\n");
        _snprintf(outputTextBuffer + strlen(outputTextBuffer), TEXTBUFFER_SIZE - strlen(outputTextBuffer), "B: Back\n");
        return;
    }

    for (int row = 0; row < visibleRows; ++row)
    {
        int index = scroll + row;
        if (index >= (int)list->count)
            break;

        _snprintf(outputTextBuffer + strlen(outputTextBuffer), TEXTBUFFER_SIZE - strlen(outputTextBuffer), "%c %2d. %s\n",
                index == selected ? '>' : ' ',
                index + 1,
                list->items[index].name);
    }

    _snprintf(outputTextBuffer + strlen(outputTextBuffer), TEXTBUFFER_SIZE - strlen(outputTextBuffer), "\nA: Select   B: Back   D-Pad: Move\n");

    dprintf("%s", outputTextBuffer); // draw in one go to prevent flickering
}

static int ShowSearchResultsUI(const GameList *list)
{
    if (!list || list->count == 0)
    {
        RenderSearchResults(list, 0, 0);

        while (true)
        {
            XINPUT_STATE state;
            ZeroMemory(&state, sizeof(state));

            if (XInputGetState(0, &state) == ERROR_SUCCESS &&
                (state.Gamepad.wButtons & XINPUT_GAMEPAD_B))
            {
                return -1;
            }

            Sleep(50);
        }
    }

    const int visibleRows = 20;
    int selected = 0;
    int scroll = 0;
    WORD previousButtons = 0;

    RenderSearchResults(list, selected, scroll);

    while (true)
    {
        XINPUT_STATE state;
        ZeroMemory(&state, sizeof(state));

        if (XInputGetState(0, &state) != ERROR_SUCCESS)
        {
            Sleep(50);
            continue;
        }

        WORD buttons = state.Gamepad.wButtons;
        WORD pressed = buttons & ~previousButtons;
        previousButtons = buttons;

        if (pressed & XINPUT_GAMEPAD_A)
            return selected;

        if (pressed & XINPUT_GAMEPAD_B)
            return -1;

        if (pressed & XINPUT_GAMEPAD_DPAD_UP)
            selected--;

        if (pressed & XINPUT_GAMEPAD_DPAD_DOWN)
            selected++;

        if (selected < 0)
            selected = 0;

        if (selected >= (int)list->count)
            selected = (int)list->count - 1;

        if (selected < scroll)
            scroll = selected;

        if (selected >= scroll + visibleRows)
            scroll = selected - visibleRows + 1;

        if (pressed & (XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_DOWN))
            RenderSearchResults(list, selected, scroll);

        Sleep(50);
    }
}

static void RenderMediaResults(const MediaList *list, const char *gameName, int selected)
{
    char outputTextBuffer[TEXTBUFFER_SIZE] = " ";

    ClearConsole();
    _snprintf(outputTextBuffer + strlen(outputTextBuffer), TEXTBUFFER_SIZE - strlen(outputTextBuffer), "Choose download media\n\n");

    if (!list || list->count == 0)
    {
        _snprintf(outputTextBuffer + strlen(outputTextBuffer), TEXTBUFFER_SIZE - strlen(outputTextBuffer), "No media IDs found.\n\n");
        _snprintf(outputTextBuffer + strlen(outputTextBuffer), TEXTBUFFER_SIZE - strlen(outputTextBuffer), "B: Back\n");
        return;
    }

    for (int i = 0; i < (int)list->count; ++i)
    {
        _snprintf(outputTextBuffer + strlen(outputTextBuffer), TEXTBUFFER_SIZE - strlen(outputTextBuffer), "%c %2d. %s Disc %s version %s\n",
                i == selected ? '>' : ' ',
                i + 1,
                gameName ? gameName : "Selected game",
                list->items[i].disc,
                list->items[i].version);
    }

    _snprintf(outputTextBuffer + strlen(outputTextBuffer), TEXTBUFFER_SIZE - strlen(outputTextBuffer), "\nA: Select   B: Back   D-Pad: Move\n");

    dprintf("%s", outputTextBuffer);
}

static int ShowMediaResultsUI(const MediaList *list, const char *gameName)
{
    if (!list || list->count == 0)
    {
        RenderMediaResults(list, gameName, 0);
        while (true)
        {
            XINPUT_STATE state;
            ZeroMemory(&state, sizeof(state));

            if (XInputGetState(0, &state) == ERROR_SUCCESS &&
                (state.Gamepad.wButtons & XINPUT_GAMEPAD_B))
            {
                return -1;
            }

            Sleep(50);
        }
    }

    if (list->count == 1)
        return 0;

    int selected = 0;
    WORD previousButtons = 0;

    RenderMediaResults(list, gameName, selected);

    while (true)
    {
        XINPUT_STATE state;
        ZeroMemory(&state, sizeof(state));

        if (XInputGetState(0, &state) != ERROR_SUCCESS)
        {
            Sleep(50);
            continue;
        }

        WORD buttons = state.Gamepad.wButtons;
        WORD pressed = buttons & ~previousButtons;
        previousButtons = buttons;

        if (pressed & XINPUT_GAMEPAD_A)
            return selected;

        if (pressed & XINPUT_GAMEPAD_B)
            return -1;

        if (pressed & XINPUT_GAMEPAD_DPAD_UP)
            selected--;

        if (pressed & XINPUT_GAMEPAD_DPAD_DOWN)
            selected++;

        if (selected < 0)
            selected = 0;

        if (selected >= (int)list->count)
            selected = (int)list->count - 1;

        if (pressed & (XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_DOWN))
            RenderMediaResults(list, gameName, selected);

        Sleep(50);
    }
}

static void WideToCharSimple(const WCHAR *src, char *dst, DWORD dstSize)
{
    if (!dst || dstSize == 0)
        return;

    dst[0] = '\0';

    if (!src)
        return;

    DWORD i = 0;

    for (; i < dstSize - 1 && src[i] != L'\0'; ++i)
    {
        // Simple narrow conversion.
        // Characters outside 8-bit range become '?'.
        dst[i] = (src[i] <= 0xFF) ? (char)src[i] : '?';
    }

    dst[i] = '\0';
}

DWORD OpenKeyboardToString(
    DWORD userIndex,
    std::string *outputString,
    LPCWSTR title,
    LPCWSTR description,
    LPCWSTR defaultText)
{

    // XShowKeyboardUI writes WCHARs, so use a wide temp buffer.
    const DWORD MAX_KEYBOARD_CHARS = 256;
    WCHAR wideResult[MAX_KEYBOARD_CHARS];
    char finalResult[MAX_KEYBOARD_CHARS];
    ZeroMemory(wideResult, sizeof(wideResult));
    finalResult[0] = '\0';

    XOVERLAPPED overlapped;
    ZeroMemory(&overlapped, sizeof(overlapped));

    overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!overlapped.hEvent) {
        dprintf("CreateEvent failed\n");
        return GetLastError();
    }

    DWORD result = XShowKeyboardUI(
        userIndex,
        VKBD_LATIN_FULL | VKBD_HIGHLIGHT_TEXT,
        defaultText,
        title,
        description,
        wideResult,
        MAX_KEYBOARD_CHARS,
        &overlapped);

    if (result != ERROR_IO_PENDING)
    {
        dprintf("Keyboad Error: Failed to open keyboard\n");
        CloseHandle(overlapped.hEvent);
        return result;
    }

    // Blocking wait until the user presses Done or Cancel.
    result = XGetOverlappedResult(&overlapped, NULL, TRUE);

    DWORD extended = XGetOverlappedExtendedError(&overlapped);

    CloseHandle(overlapped.hEvent);

    if (result != ERROR_SUCCESS) {
        dprintf("Failed to get search string from keyboard\n");
        dprintf("XGetOverlappedResult result: %lu / 0x%08X\n", result, result);
        dprintf("Keyboard extended: %lu / 0x%08X\n", extended, extended);
        return result;
    }

    // ERROR_CANCELLED means the user backed out.
    if (extended != ERROR_SUCCESS) {
        dprintf("Search canceled by user\n");
        return extended;
    }

    WideToCharSimple(wideResult, finalResult, MAX_KEYBOARD_CHARS);

    outputString->assign(finalResult);

    return ERROR_SUCCESS;
}

int showUI(char *gameURL, int len, char *gameName, int gameNameLen)
{
    Sleep(1000);

    while (true)
	{
		XINPUT_STATE state;
		ZeroMemory(&state, sizeof(state));

		if (XInputGetState(0, &state) == ERROR_SUCCESS &&
			(state.Gamepad.wButtons == 0))
		{
			break;
		} else {
            dprintf("Please release all controller buttons\n");
        }
        
        Sleep(800);
	}

    Sleep(250);

    enum DownloadType downloadType = ShowDownloadTypeMenu();
    if (!downloadType)
        return -1;

    Sleep(250);

    unsigned long long OUTPUT_BUFFER_SIZE = HTML_BUFFER_CAPACITY;

    char *buffer = (char *)malloc(OUTPUT_BUFFER_SIZE); // 4MB buffer just to be safe

    if (buffer == NULL)
    {
        dprintf("Malloc failed\n");
        return -1;
    }

    std::string gameSearchString = "";
    DWORD keyboardResult = OpenKeyboardToString(XUSER_INDEX_ANY, &gameSearchString, L"Search for a game", L"Search for a game here", L"GTA VI");
    if (keyboardResult != ERROR_SUCCESS || gameSearchString.empty())
    {
        dprintf("Failed to get search string from keyboard\n");
        if(gameSearchString.empty()) {
            dprintf("Search request was emtpy\n");
        }

        free(buffer);
        return -1;
    }

    std::cout << "Text output: " << gameSearchString << "\n";

    std::string searchURL = "https://vimm.net/vault/?p=list&system=Xbox360&q="; //default

    switch (downloadType)
    {
    case ORIGINAL_XBOX:
        searchURL = "https://vimm.net/vault/?p=list&system=Xbox&q=";
        break;
    
    case XBOX_360:
        searchURL = "https://vimm.net/vault/?p=list&system=Xbox360&q=";
        break;
    
    case XBLA:
        searchURL = "https://vimm.net/vault/?p=list&system=X360-D&q=";
        break;
    
    default:
        break;
    }
    
    searchURL.append(UrlEncodeQuery(gameSearchString));

    if (downloadFileHTTPS(searchURL, "", buffer, &OUTPUT_BUFFER_SIZE, false, dprintf) != 200)
    { // downloads into a null terminated buffer
        dprintf("Download game list failed. Ensure you searched for an actual game. Ensure your search request was EXACTLY correct. \n");
        free(buffer);
        return -1;
    }

    GameList list = {0};

    printf("Parsing search results\n");

    if (!parse_vimm_search_results(buffer, &list)) // this strlen operation assumes that there were no NULL character before the end, which is fine for HTML, but not for other files
    {
        dprintf("Failed to parse HTML.\n");
        free(buffer);
        free_game_list(&list);
        return -1;
    }

    int selected = ShowSearchResultsUI(&list);

    if (selected < 0)
    {
        dprintf("Game not found. Ensure you searched for a real Xbox 360 game\n\n");
        free_game_list(&list);
        free(buffer);
        return -1;
    }

    std::string selectedURL = "https://vimm.net";
    selectedURL.append(list.items[selected].link);

    if (gameName && gameNameLen > 0)
    {
        strncpy(gameName, list.items[selected].name, gameNameLen - 1);
        gameName[gameNameLen - 1] = '\0';
    }

    ClearConsole();
    dprintf("Selected:\n%s\n\n%s\n", list.items[selected].name, selectedURL.c_str());


	// Now download the selected game

    OUTPUT_BUFFER_SIZE = HTML_BUFFER_CAPACITY;
    if (downloadFileHTTPS(selectedURL, "", buffer, &OUTPUT_BUFFER_SIZE, false, dprintf) != 200)
    { // downloads into a null terminated buffer
        dprintf("Download game version list failed\n");
        free_game_list(&list);
        free(buffer);
        return -1;
    }

    MediaList mediaList = {0};
    if (!parse_vimm_media_ids(buffer, &mediaList) || mediaList.count == 0)
    {
        dprintf("Failed to parse media ID from selected game page.\n");
        free_media_list(&mediaList);
        free_game_list(&list);
        free(buffer);
        return -1;
    }

    int selectedMedia = ShowMediaResultsUI(&mediaList, list.items[selected].name);
    if (selectedMedia < 0)
    {
        dprintf("Failed to find game version\n\n");
        free_media_list(&mediaList);
        free_game_list(&list);
        free(buffer);
        return -1;
    }

    std::string finalDownloadURL = (DOWNLOAD_DOMAIN "/?mediaId=");

    finalDownloadURL.append(mediaList.items[selectedMedia].id);

    if (gameURL && len > 0)
    {
        strncpy(gameURL, finalDownloadURL.c_str(), len - 1);
        gameURL[len - 1] = '\0';
    }

    ClearConsole();
    dprintf("Download URL:\n%s\n\n%s Disc %s version %s\n",
            finalDownloadURL.c_str(),
            list.items[selected].name,
            mediaList.items[selectedMedia].disc,
            mediaList.items[selectedMedia].version);

    free_media_list(&mediaList);
    free_game_list(&list);
    free(buffer);
    return downloadType;
}
