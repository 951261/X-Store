#ifndef XUI_H
#define XUI_H

#include <xtl.h>

#include "ui.h"
#include "vimms_lair.h"

typedef BOOL (*XuiStatusTaskProc)(void *taskContext);

DownloadType ShowDownloadTypeMenuXUI();
int ShowSearchResultsXUI(const GameList *list);
int ShowMediaResultsXUI(const MediaList *list, const char *gameName);
BOOL RunStatusTaskXUI(
    LPCWSTR title,
    LPCWSTR message,
    XuiStatusTaskProc task,
    void *taskContext);

#endif
