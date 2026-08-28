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
#include <updater.h>

#include "ui.h"
#include "vimms_lair.h"
#include <vector>

#define TEXTBUFFER_SIZE (1024 * 10)
#define HTML_BUFFER_CAPACITY (1024 * 1024 * 4);

static const SHORT NAVIGATION_STICK_THRESHOLD = 16000;
static const DWORD NAVIGATION_REPEAT_DELAY_MS = 350;
static const DWORD NAVIGATION_REPEAT_INTERVAL_MS = 150;

static const char *GetDownloadTypeName(enum DownloadType type)
{
	switch (type)
	{
	case ORIGINAL_XBOX:
		return "Original Xbox";

	case XBOX_360:
		return "Xbox 360";

	case XBLA:
		return "XBLA, DLC and Title Updates";

	case AUTO_UPDATE:
		return "Update X-Store";

	case DOWNLOAD_QUEUE:
		return "Download Queue";

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

	case 3:
		return AUTO_UPDATE;

	case 4:
		return DOWNLOAD_QUEUE;

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

static void WaitForControllerButtonsRelease()
{
	XINPUT_STATE state;
	while (true)
	{
		ZeroMemory(&state, sizeof(state));
		if (XInputGetState(0, &state) != ERROR_SUCCESS || state.Gamepad.wButtons == 0)
			return;

		Sleep(50);
	}
}

static int GetVerticalNavigationDirection(const XINPUT_STATE &state)
{
	bool up = (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_UP) != 0 ||
		state.Gamepad.sThumbLY > NAVIGATION_STICK_THRESHOLD;
	bool down = (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) != 0 ||
		state.Gamepad.sThumbLY < -NAVIGATION_STICK_THRESHOLD;

	if (up == down)
		return 0;

	return up ? -1 : 1;
}

static bool ShouldMoveSelection(
	int direction,
	int *previousDirection,
	DWORD *nextRepeatTick)
{
	if (!previousDirection || !nextRepeatTick)
		return false;

	DWORD now = GetTickCount();
	bool shouldMove = false;

	if (direction == 0)
	{
		*nextRepeatTick = 0;
	}
	else if (direction != *previousDirection)
	{
		shouldMove = true;
		*nextRepeatTick = now + NAVIGATION_REPEAT_DELAY_MS;
	}
	else if (*nextRepeatTick != 0 && (LONG)(now - *nextRepeatTick) >= 0)
	{
		shouldMove = true;
		*nextRepeatTick = now + NAVIGATION_REPEAT_INTERVAL_MS;
	}

	*previousDirection = direction;
	return shouldMove;
}

static void InitializeMenuInput(
	WORD *previousButtons,
	int *previousDirection,
	DWORD *nextRepeatTick)
{
	if (!previousButtons || !previousDirection || !nextRepeatTick)
		return;

	XINPUT_STATE state;
	ZeroMemory(&state, sizeof(state));
	if (XInputGetState(0, &state) == ERROR_SUCCESS)
	{
		*previousButtons = state.Gamepad.wButtons;
		*previousDirection = GetVerticalNavigationDirection(state);
		*nextRepeatTick = *previousDirection == 0
			? 0
			: GetTickCount() + NAVIGATION_REPEAT_DELAY_MS;
	}
	else
	{
		*previousButtons = 0;
		*previousDirection = 0;
		*nextRepeatTick = 0;
	}
}

static void RenderDownloadTypeMenu(int selected, const std::vector<GameData> &gamesInfo)
{
	char outputTextBuffer[TEXTBUFFER_SIZE] = " ";

	ClearConsole();
	_snprintf(outputTextBuffer, TEXTBUFFER_SIZE - strlen(outputTextBuffer), "Download Queue (%u items)\n\n", (unsigned int)gamesInfo.size());

	for (int i = 0; i < 5; ++i)
	{
		enum DownloadType type = GetDownloadTypeFromIndex(i);
		if (type == DOWNLOAD_QUEUE)
		{
			_snprintf(outputTextBuffer + strlen(outputTextBuffer), TEXTBUFFER_SIZE - strlen(outputTextBuffer), "%c Download all queued items%s\n",
				i == selected ? '>' : ' ',
				gamesInfo.empty() ? " (queue is empty)" : "");
		}
		else
		{
			_snprintf(outputTextBuffer + strlen(outputTextBuffer), TEXTBUFFER_SIZE - strlen(outputTextBuffer), "%c %s\n",
				i == selected ? '>' : ' ',
				GetDownloadTypeName(type));
		}
	}

	if (!gamesInfo.empty())
	{
		_snprintf(outputTextBuffer + strlen(outputTextBuffer), TEXTBUFFER_SIZE - strlen(outputTextBuffer), "\nQueued:\n");
		const size_t visibleQueueItems = 12;
		size_t firstItem = gamesInfo.size() > visibleQueueItems
			? gamesInfo.size() - visibleQueueItems
			: 0;
		if (firstItem > 0)
		{
			_snprintf(outputTextBuffer + strlen(outputTextBuffer), TEXTBUFFER_SIZE - strlen(outputTextBuffer), "... %u earlier items ...\n", (unsigned int)firstItem);
		}

		for (size_t i = firstItem; i < gamesInfo.size(); ++i)
		{
			_snprintf(outputTextBuffer + strlen(outputTextBuffer), TEXTBUFFER_SIZE - strlen(outputTextBuffer), "%u. [%s] %.240s\n",
				(unsigned int)(i + 1),
				GetDownloadTypeName((enum DownloadType)gamesInfo[i].downloadType),
				gamesInfo[i].selectedGameName);
		}
	}

	_snprintf(outputTextBuffer + strlen(outputTextBuffer), TEXTBUFFER_SIZE - strlen(outputTextBuffer), "\nA: Select   B: Cancel   D-Pad/Left Stick: Move\n");

	dprintf("%s", outputTextBuffer);
}

static enum DownloadType ShowDownloadTypeMenu(const std::vector<GameData> &gamesInfo)
{
	int selected = 0;
	WORD previousButtons = 0;
	int previousDirection = 0;
	DWORD nextRepeatTick = 0;

	RenderDownloadTypeMenu(selected, gamesInfo);
	InitializeMenuInput(&previousButtons, &previousDirection, &nextRepeatTick);

	XINPUT_STATE state;

	while (true)
	{
		ZeroMemory(&state, sizeof(state));

		if (XInputGetState(0, &state) != ERROR_SUCCESS)
		{
			Sleep(50);
			continue;
		}

		WORD buttons = state.Gamepad.wButtons;
		WORD pressed = buttons & ~previousButtons;
		int direction = GetVerticalNavigationDirection(state);

		if (pressed & XINPUT_GAMEPAD_A)
		{
			enum DownloadType selectedType = GetDownloadTypeFromIndex(selected);
			if (selectedType != DOWNLOAD_QUEUE || !gamesInfo.empty())
				return selectedType;
		}

		if (pressed & XINPUT_GAMEPAD_B)
			return (enum DownloadType)0;

		bool moved = ShouldMoveSelection(direction, &previousDirection, &nextRepeatTick);
		if (moved)
			selected += direction;

		if (selected < 0)
			selected = 0;

		if (selected >= 5)
			selected = 4;

		if (moved)
			RenderDownloadTypeMenu(selected, gamesInfo);

		previousButtons = buttons;
		Sleep(50);
	}
}

enum SearchResultAction
{
	SEARCH_RESULT_CANCEL,
	SEARCH_RESULT_DOWNLOAD_NOW,
	SEARCH_RESULT_ADD_TO_QUEUE
};

struct SearchResultSelection
{
	int index;
	SearchResultAction action;
};

static SearchResultSelection MakeSearchResultSelection(
	int index,
	SearchResultAction action)
{
	SearchResultSelection selection;
	selection.index = index;
	selection.action = action;
	return selection;
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

	_snprintf(outputTextBuffer + strlen(outputTextBuffer), TEXTBUFFER_SIZE - strlen(outputTextBuffer), "\nA: Download Now   Y: Add to Queue   B: Back   D-Pad/Left Stick: Move LB/RB: Next/Previous Page\n");

	dprintf("%s", outputTextBuffer); // draw in one go to prevent flickering
}

static SearchResultSelection ShowSearchResultsUI(const GameList *list)
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
				return MakeSearchResultSelection(-1, SEARCH_RESULT_CANCEL);
			}

			Sleep(50);
		}
	}

	const int visibleRows = 20;
	int selected = 0;
	int scroll = 0;
	WORD previousButtons = 0;
	int previousDirection = 0;
	DWORD nextRepeatTick = 0;

	RenderSearchResults(list, selected, scroll);
	InitializeMenuInput(&previousButtons, &previousDirection, &nextRepeatTick);

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
		int direction = GetVerticalNavigationDirection(state);

		if (pressed & XINPUT_GAMEPAD_A)
			return MakeSearchResultSelection(selected, SEARCH_RESULT_DOWNLOAD_NOW);

		if (pressed & XINPUT_GAMEPAD_Y)
			return MakeSearchResultSelection(selected, SEARCH_RESULT_ADD_TO_QUEUE);

		if (pressed & XINPUT_GAMEPAD_B)
			return MakeSearchResultSelection(-1, SEARCH_RESULT_CANCEL);

		bool moved = ShouldMoveSelection(
			direction,
			&previousDirection,
			&nextRepeatTick);

		if (moved)
			selected += direction;
		if (pressed & XINPUT_GAMEPAD_LEFT_SHOULDER)
		{
			selected -= 20;

			if (selected < 0)
				selected = 0;

			moved = true;
		}
		if (pressed & XINPUT_GAMEPAD_RIGHT_SHOULDER)
		{
			if (selected < 20) selected +=18;
			selected += 20;

			if (selected >= (int)list->count)
				selected = (int)list->count - 1;

			moved = true;
		}

		if (selected < 0)
			selected = 0;

		if (selected >= (int)list->count)
			selected = (int)list->count - 1;

		if (selected < scroll)
			scroll = selected;

		if (selected >= scroll + visibleRows)
			scroll = selected - visibleRows + 1;

		if (moved)
			RenderSearchResults(list, selected, scroll);

		previousButtons = buttons;

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

	_snprintf(outputTextBuffer + strlen(outputTextBuffer), TEXTBUFFER_SIZE - strlen(outputTextBuffer), "\nA: Select   B: Back   D-Pad/Left Stick: Move\n");

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
	int previousDirection = 0;
	DWORD nextRepeatTick = 0;

	RenderMediaResults(list, gameName, selected);
	InitializeMenuInput(&previousButtons, &previousDirection, &nextRepeatTick);

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
		int direction = GetVerticalNavigationDirection(state);

		if (pressed & XINPUT_GAMEPAD_A)
			return selected;

		if (pressed & XINPUT_GAMEPAD_B)
			return -1;

		bool moved = ShouldMoveSelection(direction, &previousDirection, &nextRepeatTick);
		if (moved)
			selected += direction;

		if (selected < 0)
			selected = 0;

		if (selected >= (int)list->count)
			selected = (int)list->count - 1;

		if (moved)
			RenderMediaResults(list, gameName, selected);

		previousButtons = buttons;

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
	if (!overlapped.hEvent)
	{
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

	if (result != ERROR_SUCCESS)
	{
		dprintf("Failed to get search string from keyboard\n");
		dprintf("XGetOverlappedResult result: %lu / 0x%08X\n", result, result);
		dprintf("Keyboard extended: %lu / 0x%08X\n", extended, extended);
		return result;
	}

	// ERROR_CANCELLED means the user backed out.
	if (extended != ERROR_SUCCESS)
	{
		dprintf("Search canceled by user\n");
		return extended;
	}

	WideToCharSimple(wideResult, finalResult, MAX_KEYBOARD_CHARS);

	outputString->assign(finalResult);

	return ERROR_SUCCESS;
}

std::vector<GameData> showUI()
{
	Sleep(1000);

	std::vector<GameData> gamesInfo;

	while (true)
	{
		XINPUT_STATE state;
		ZeroMemory(&state, sizeof(state));

		if (XInputGetState(0, &state) == ERROR_SUCCESS &&
			(state.Gamepad.wButtons == 0))
		{
			break;
		}
		else
		{
			dprintf("Please release all controller buttons\n");
		}

		Sleep(800);
	}

	Sleep(250);

	while (true)
	{
		WaitForControllerButtonsRelease();
		enum DownloadType downloadType = ShowDownloadTypeMenu(gamesInfo);
		if (!downloadType)
			return std::vector<GameData>();

		if (downloadType == DOWNLOAD_QUEUE)
			return gamesInfo;

		if (downloadType == AUTO_UPDATE)
		{
			if (runUpdate() != EXIT_FAILURE)
				dprintf("Update Succeeded!\n");
			Sleep(500);
			continue;
		}

		unsigned long long outputBufferSize = HTML_BUFFER_CAPACITY;
		size_t bufferSize = (size_t)outputBufferSize;
		char *buffer = (char *)malloc(bufferSize);
		if (!buffer)
		{
			dprintf("Malloc failed\n");
			return std::vector<GameData>();
		}

		std::string gameSearchString;
		DWORD keyboardResult = OpenKeyboardToString(
			XUSER_INDEX_ANY,
			&gameSearchString,
			L"Search for a game",
			L"Search for a game here",
			L"");
		if (keyboardResult != ERROR_SUCCESS || gameSearchString.empty())
		{
			free(buffer);
			continue;
		}

		GameList list = {0};

		if (gameSearchString == "*")
		{
			std::string searchURL;

			const char *vimmSystem;

			switch (downloadType)
			{
			case ORIGINAL_XBOX:
				vimmSystem = "Xbox";
				break;

			case XBOX_360:
				vimmSystem = "Xbox360";
				break;

			case XBLA:
				vimmSystem = "X360-D";
				break;

			default:
				free(buffer);
				continue;
			}

			// Numbers
			searchURL =
				"https://vimm.net/vault/?p=list&system=";
			searchURL += vimmSystem;
			searchURL += "&section=number";

			outputBufferSize = HTML_BUFFER_CAPACITY;

			if (downloadFileHTTPS(
				searchURL,
				"",
				buffer,
				&outputBufferSize,
				false,
				dprintf) == 200)
			{
				if (!parse_vimm_search_results(buffer, &list))
				{
					dprintf("Failed to parse number results.\n");
					free_game_list(&list);
					free(buffer);
					Sleep(500);
					continue;
				}
			}
			else
			{
				dprintf("Failed to download number results.\n");
				free_game_list(&list);
				free(buffer);
				Sleep(500);
				continue;
			}

			// A-Z
			for (char letter = 'A'; letter <= 'Z'; ++letter)
			{
				char letterURL[128];

				_snprintf(
					letterURL,
					sizeof(letterURL),
					"https://vimm.net/vault/%s/%c",
					vimmSystem,
					letter);

				outputBufferSize = HTML_BUFFER_CAPACITY;

				int status = downloadFileHTTPS(
					letterURL,
					"",
					buffer,
					&outputBufferSize,
					false,
					dprintf);

				if (status == 429)
				{
					dprintf("Vimm rate limit reached (HTTP 429).\n");

					free_game_list(&list);
					free(buffer);
					Sleep(500);
					continue;
				}

				if (status != 200)
				{
					dprintf(
						"Failed to download Vault page %c (HTTP %d).\n",
						letter,
						status);
					free_game_list(&list);
					free(buffer);
					Sleep(500);
					continue;
				}

				if (!parse_vimm_search_results(buffer, &list))
				{
					dprintf("Failed to parse Vault page %c.\n", letter);
					free_game_list(&list);
					free(buffer);
					Sleep(500);
					continue;
				}
				//not sleeping will eventually hit us with a limit so yeah
				Sleep(2000);
			}
		}
		else
		{
			std::string searchURL;

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
				free(buffer);
				continue;
			}

			searchURL.append(UrlEncodeQuery(gameSearchString));

			if (downloadFileHTTPS(
				searchURL,
				"",
				buffer,
				&outputBufferSize,
				false,
				dprintf) != 200)
			{
				dprintf("Download game list failed.\n");
				free(buffer);
				Sleep(500);
				continue;
			}

			if (!parse_vimm_search_results(buffer, &list))
			{
				dprintf("Failed to parse search results.\n");
				free_game_list(&list);
				free(buffer);
				Sleep(500);
				continue;
			}
		}

		SearchResultSelection searchSelection = ShowSearchResultsUI(&list);
		if (searchSelection.action == SEARCH_RESULT_CANCEL || searchSelection.index < 0)
		{
			free_game_list(&list);
			free(buffer);
			continue;
		}

		int selected = searchSelection.index;
		bool downloadImmediately =
			searchSelection.action == SEARCH_RESULT_DOWNLOAD_NOW;

		std::string selectedURL = "https://vimm.net";
		selectedURL.append(list.items[selected].link);

		outputBufferSize = HTML_BUFFER_CAPACITY;
		if (downloadFileHTTPS(selectedURL, "", buffer, &outputBufferSize, false, dprintf) != 200)
		{
			dprintf("Download game version list failed.\n");
			free_game_list(&list);
			free(buffer);
			Sleep(500);
			continue;
		}

		MediaList mediaList = {0};
		if (!parse_vimm_media_ids(buffer, &mediaList) || mediaList.count == 0)
		{
			dprintf("Failed to parse media ID from selected game page.\n");
			free_media_list(&mediaList);
			free_game_list(&list);
			free(buffer);
			Sleep(500);
			continue;
		}

		int selectedMedia = ShowMediaResultsUI(&mediaList, list.items[selected].name);
		if (selectedMedia < 0)
		{
			free_media_list(&mediaList);
			free_game_list(&list);
			free(buffer);
			continue;
		}

		std::string finalDownloadURL = DOWNLOAD_DOMAIN "/?mediaId=";
		finalDownloadURL.append(mediaList.items[selectedMedia].id);

		GameData gameData;
		ZeroMemory(&gameData, sizeof(gameData));
		gameData.downloadType = downloadType;
		strncpy(gameData.selectedGameName, list.items[selected].name, sizeof(gameData.selectedGameName) - 1);
		strncpy(gameData.selectedGameURL, finalDownloadURL.c_str(), sizeof(gameData.selectedGameURL) - 1);
		if (downloadImmediately)
			gamesInfo.clear();

		gamesInfo.push_back(gameData);

		ClearConsole();
		dprintf(downloadImmediately
			? "Preparing to download %s Disc %s version %s\n"
			: "Queued %s Disc %s version %s\n",
			gameData.selectedGameName,
			mediaList.items[selectedMedia].disc,
			mediaList.items[selectedMedia].version);

		free_media_list(&mediaList);
		free_game_list(&list);
		free(buffer);

		if (downloadImmediately)
			return gamesInfo;

		Sleep(500);
	}
}
