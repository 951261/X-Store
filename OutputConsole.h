#pragma once

#define CONSOLE_COLOR_BLACK 0xFF000000
#define CONSOLE_COLOR_WHITE 0xFFFFFFFF
#define CONSOLE_COLOR_RED 0xFFFF0000
#define CONSOLE_COLOR_GREEN 0xFF008000
#define CONSOLE_COLOR_YELLOW 0xFFFFF700
#define CONSOLE_COLOR_BLUE 0xFF0000FF
#define CONSOLE_COLOR_GRAY 0xFF808080
#define CONSOLE_COLOR_GREY 0xFF808080

#define CONSOLE_COLOR_ORANGE 0xFFFF6600
#define CONSOLE_COLOR_MAGENTA 0xFFFF00FF
#define CONSOLE_COLOR_CYAN 0xFF00FFFF
#define CONSOLE_COLOR_INDIGO 0xFF4B0082
#define CONSOLE_COLOR_BRIGHT_PINK 0xFFFF007F
#define CONSOLE_COLOR_VIOLET 0xFF8F00FF
#define CONSOLE_COLOR_PURPLE 0xFF800080

#define CONSOLE_COLOR_GOLD 0xFFFFD700
#define CONSOLE_COLOR_SILVER 0xFFC0C0C0
#define CONSOLE_COLOR_AMBER 0xFFFF7E00
#define CONSOLE_COLOR_EMERALD 0xFF50C878
#define CONSOLE_COLOR_RUBY 0xFF9B111E
#define CONSOLE_COLOR_SAPPHIRE 0xFF0F52BA

#define MSG_PROCESSING_BLOCK "\rprocessing block 0x%X of 0x%X (%3.2f %%)"
#define MSG_PROCESSED_OF_BYTES "\rProcessed 0x%X of 0x%X bytes (%3.2f %%)"
#define MSG_PROCESSING_START "\rprocessing block"
#define MSG_PROCESSED_START "\rProcessed"

#define LOG_FILE_PATH "game:\\DebugInfo.txt"

#ifdef __cplusplus
extern "C"
{
#endif
#ifdef USE_UNICODE
	void __cdecl dprintf(const wchar_t *strFormat, ...);
#else
void __cdecl dprintf(const char *strFormat, ...);
#endif

	void __cdecl log_printf(const char *strFormat, ...);

#ifdef __cplusplus
}
#endif

void MakeConsole(const char *font, unsigned long BackgroundColor, unsigned long TextColor);

void ClearConsole();
void ClearConsoleOnNextPrint();

// #include "Language.h"