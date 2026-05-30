/*
FILE : OutputConsole.cpp
PROJECT : xstore
PROGRAMMER : 951261
DESCRIPTION : Writes text to the screen. Also writes to debug file
*/

#include "AtgConsole.h"
#include "AtgUtil.h"
#include "OutputConsole.h"
#include <stdio.h>

#define TEXTBUFFER_SIZE (1024 * 10) // 2KB buffer

ATG::Console g_console;// console for output
#ifdef USE_UNICODE
WCHAR buf[TEXTBUFFER_SIZE]; // Text buffer
void  __cdecl dprintf(const wchar_t* strFormat, ...)
#else
char buf[TEXTBUFFER_SIZE]; // Text buffer
void  __cdecl dprintf(const char* strFormat, ...)
#endif
{
	FILE* flog;
	va_list pArglist;
	va_start(pArglist, strFormat);
#ifdef USE_UNICODE
	_vsnwprintf_s(buf, TEXTBUFFER_SIZE, strFormat, pArglist);
#else
	vsnprintf_s(buf, TEXTBUFFER_SIZE, strFormat, pArglist);
#endif
	va_end(pArglist);
	g_console.Display(buf);
 	// g_console.Format("%s", buf);
#ifdef USE_UNICODE
	if (!fexists("game:\\Simple 360 NAND Flasher.log"))
	{
		fopen_s(&flog, "game:\\Simple 360 NAND Flasher.log", "wb");
		char* cStart = "\xfe\xff";
		fwrite(cStart, strlen(cStart), 1, flog);
		fclose(flog);
	}
	if (wcsncmp(strFormat, MSG_PROCESSING_START, wcslen(MSG_PROCESSING_START)) != 0 && wcsncmp(strFormat, MSG_PROCESSED_START, wcslen(MSG_PROCESSED_START)) != 0)
	{
		fopen_s(&flog, "game:\\Simple 360 NAND Flasher.log", "ab");
		if (flog != NULL)
		{
			fwrite(buf, wcslen(buf) * sizeof(wchar_t), 1, flog);
			fclose(flog);
		}
	}
#else

	printf("%s", buf);

	FILE* fp = NULL;
    errno_t err = fopen_s(&fp, LOG_FILE_PATH, "a+");
    if(fp) {
        fprintf(fp, "%s", buf);
        fclose(fp);
    } else {
		printf("Failed to open log file\n");
	}

#endif
}

void MakeConsole(const char* font, unsigned long BackgroundColor, unsigned long TextColor)
{
	g_console.Create(font, BackgroundColor, TextColor);
	g_console.SendOutputToDebugChannel(TRUE);
}

void ClearConsole()
{
	g_console.Clear();
}

void __cdecl log_printf(const char* strFormat, ...) {
	va_list pArglist;
	va_start(pArglist, strFormat);
#ifdef USE_UNICODE
	_vsnwprintf_s(buf, TEXTBUFFER_SIZE, strFormat, pArglist);
#else
	vsnprintf_s(buf, TEXTBUFFER_SIZE, strFormat, pArglist);
#endif
	va_end(pArglist);

	printf("%s", buf);
	
	FILE* fp = NULL;
    errno_t err = fopen_s(&fp, LOG_FILE_PATH, "a+");
    if(fp) {
        fprintf(fp, "%s", buf);
        fclose(fp);
    }

}
