#ifndef GOD_UI_H
#define GOD_UI_H

#include <algorithm>
#include <stdio.h>
#include <string>
#include <vector>
#include "win32/dirent.h"

#define GOD_MODE_CFG "game:\\god_mode.cfg"

bool isGodFormatEnabled()
{
    FILE *f = fopen(GOD_MODE_CFG, "r");
    if (!f) return false;
    int val = 0;
    fscanf(f, "%d", &val);
    fclose(f);
    return val == 1;
}

static void setGodFormatEnabled(bool enabled)
{
    FILE *f = fopen(GOD_MODE_CFG, "w");
    if (!f) return;
    fprintf(f, "%d\n", enabled ? 1 : 0);
    fclose(f);
}

static bool PromptDeleteSourceAfterGod(const char *name)
{
    WaitForControllerButtonsRelease();
    ClearConsole();
    dprintf("Convert to GOD: %s\n\n", name);
    dprintf("Delete original source files after successful conversion?\n\n");
    dprintf("  A: Yes (Free up space after converting)\n");
    dprintf("  B: No  (Keep original ISO / extracted folder)\n");

    XINPUT_STATE st;
    while (true)
    {
        ZeroMemory(&st, sizeof(st));
        if (XInputGetState(0, &st) == ERROR_SUCCESS)
        {
            if (st.Gamepad.wButtons & XINPUT_GAMEPAD_A) { WaitForControllerButtonsRelease(); return true; }
            if (st.Gamepad.wButtons & XINPUT_GAMEPAD_B) { WaitForControllerButtonsRelease(); return false; }
        }
        Sleep(50);
    }
}

static bool ShowLocalGodBrowser(GameData &outGame)
{
    std::vector<std::string> pathStack;
    int selected = 0;
    WORD prevButtons = 0;
    int prevDir = 0;
    DWORD nextRepeat = 0;
    InitializeMenuInput(&prevButtons, &prevDir, &nextRepeat);

    while (true)
    {
        std::vector<std::string> labels;
        std::vector<std::string> fullPaths;
        std::vector<bool> isDirList;

        if (pathStack.empty())
        {
            const char *roots[] = { "Hdd1:\\", "Usb0:\\", "Usb1:\\" };
            for (int r = 0; r < 3; ++r)
            {
                DIR *d = opendir(roots[r]);
                if (d)
                {
                    closedir(d);
                    labels.push_back(std::string("[Drive] ") + roots[r]);
                    fullPaths.push_back(roots[r]);
                    isDirList.push_back(true);
                }
            }
        }
        else
        {
            std::string cur = pathStack.back();
            DIR *d = opendir(cur.c_str());
            if (d)
            {
                struct dirent *ent;
                while ((ent = readdir(d)) != NULL)
                {
                    if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
                    std::string child = cur;
                    if (!child.empty() && child[child.size() - 1] != '\\') child += "\\";
                    child += ent->d_name;

                    DIR *sub = opendir(child.c_str());
                    if (sub)
                    {
                        closedir(sub);
                        labels.push_back(std::string("[DIR] ") + ent->d_name);
                        fullPaths.push_back(child);
                        isDirList.push_back(true);
                    }
                    else
                    {
                        size_t len = strlen(ent->d_name);
                        if ((len > 4 && _stricmp(ent->d_name + len - 4, ".iso") == 0) ||
                            (len > 8 && _stricmp(ent->d_name + len - 8, ".iso.001") == 0))
                        {
                            labels.push_back(std::string("[ISO] ") + ent->d_name);
                            fullPaths.push_back(child);
                            isDirList.push_back(false);
                        }
                    }
                }
                closedir(d);
            }
        }

        if (selected >= (int)labels.size()) selected = labels.empty() ? 0 : (int)labels.size() - 1;
        if (selected < 0) selected = 0;

        ClearConsole();
        dprintf("Local Game to GOD Converter\n");
        dprintf("Location: %s\n\n", pathStack.empty() ? "Select Storage Device" : pathStack.back().c_str());

        if (labels.empty())
            dprintf("  (No folders or .iso files found here)\n");
        else
        {
            int start = selected > 12 ? selected - 12 : 0;
            int end = std::min<int>((int)labels.size(), start + 16);
            for (int i = start; i < end; ++i)
                dprintf("%c %s\n", i == selected ? '>' : ' ', labels[i].c_str());
        }

        dprintf("\nA: Open Dir / Select ISO   X: Convert Highlighted Folder to GOD   B: Back\n");

        XINPUT_STATE state;
        while (true)
        {
            ZeroMemory(&state, sizeof(state));
            WORD buttons = (XInputGetState(0, &state) == ERROR_SUCCESS) ? state.Gamepad.wButtons : 0;
            WORD pressed = buttons & ~prevButtons;
            int dir = GetVerticalNavigationDirection(state);

            if (pressed & XINPUT_GAMEPAD_B)
            {
                WaitForControllerButtonsRelease();
                if (pathStack.empty()) return false;
                pathStack.pop_back();
                selected = 0;
                break;
            }

            if (!labels.empty() && ((pressed & XINPUT_GAMEPAD_A) || (!pathStack.empty() && isDirList[selected] && (pressed & XINPUT_GAMEPAD_X))))
            {
                bool forceConvertDir = (pressed & XINPUT_GAMEPAD_X) != 0;
                WaitForControllerButtonsRelease();
                if (isDirList[selected] && !forceConvertDir)
                {
                    pathStack.push_back(fullPaths[selected]);
                    selected = 0;
                    break;
                }
                ZeroMemory(&outGame, sizeof(outGame));
                outGame.downloadType = LOCAL_TO_GOD;
                outGame.convertToGod = true;
                strncpy(outGame.selectedGameURL, fullPaths[selected].c_str(), sizeof(outGame.selectedGameURL) - 1);
                const char *baseName = strrchr(fullPaths[selected].c_str(), '\\');
                strncpy(outGame.selectedGameName, baseName ? baseName + 1 : fullPaths[selected].c_str(), sizeof(outGame.selectedGameName) - 1);
                outGame.deleteSourceAfterGod = PromptDeleteSourceAfterGod(outGame.selectedGameName);
                return true;
            }

            if (ShouldMoveSelection(dir, &prevDir, &nextRepeat) && !labels.empty())
            {
                selected = (selected + dir + (int)labels.size()) % (int)labels.size();
                prevButtons = buttons;
                break;
            }

            prevButtons = buttons;
            Sleep(50);
        }
    }
}

#endif
