#ifndef STORE_QUEUE_PERSIST_H
#define STORE_QUEUE_PERSIST_H

#define STORE_QUEUE_FILE "game:\\store_queue.txt"

static void SaveStoreQueueState(const std::vector<GameData> &games, const std::vector<bool> &completed)
{
    FILE *f = fopen(STORE_QUEUE_FILE, "w");
    if (!f) return;
    for (size_t i = 0; i < games.size(); ++i)
    {
        fprintf(f, "%d|%d|%d|%d|%s|%s|%s\n",
                completed[i] ? 1 : 0,
                (int)games[i].downloadType,
                games[i].convertToGod ? 1 : 0,
                games[i].deleteSourceAfterGod ? 1 : 0,
                games[i].fileFormat,
                games[i].selectedGameName,
                games[i].selectedGameURL);
    }
    fclose(f);
}

static bool LoadSavedStoreQueue(std::vector<GameData> &outGames, std::vector<bool> &outCompleted)
{
    FILE *f = fopen(STORE_QUEUE_FILE, "r");
    if (!f) return false;

    char line[2048];
    bool hasPending = false;
    while (fgets(line, sizeof(line), f))
    {
        line[strcspn(line, "\r\n")] = '\0';
        char *parts[7] = {0};
        char *cur = line;
        int idx = 0;
        parts[idx++] = cur;
        while (*cur && idx < 7)
        {
            if (*cur == '|') { *cur = '\0'; parts[idx++] = cur + 1; }
            cur++;
        }
        if (idx < 7) continue;

        bool done = atoi(parts[0]) == 1;
        GameData gd;
        ZeroMemory(&gd, sizeof(gd));
        gd.downloadType = (enum DownloadType)atoi(parts[1]);
        gd.convertToGod = atoi(parts[2]) == 1;
        gd.deleteSourceAfterGod = atoi(parts[3]) == 1;
        strncpy(gd.fileFormat, parts[4], sizeof(gd.fileFormat) - 1);
        strncpy(gd.selectedGameName, parts[5], sizeof(gd.selectedGameName) - 1);
        strncpy(gd.selectedGameURL, parts[6], sizeof(gd.selectedGameURL) - 1);

        outGames.push_back(gd);
        outCompleted.push_back(done);
        if (!done) hasPending = true;
    }
    fclose(f);
    if (!hasPending) { remove(STORE_QUEUE_FILE); return false; }
    return true;
}

static bool PromptResumeStoreQueue(size_t doneCount, size_t totalCount, const char *nextTitle, bool isGod, const char *resumeStage)
{
    ClearConsole();
    dprintf("Interrupted Download/Conversion Queue Found!\n\n");
    dprintf("Completed:     %u of %u items\n", (unsigned int)doneCount, (unsigned int)totalCount);
    dprintf("Next Item (#%u): %s\n", (unsigned int)(doneCount + 1), nextTitle);
    dprintf("Output Format: %s\n", isGod ? "GOD (Games on Demand)" : "XEX (Extracted Files)");
    dprintf("Resume Action: %s\n\n", resumeStage);
    dprintf("  A: Resume Queue\n");
    dprintf("  B: Discard Saved Queue\n");

    XINPUT_STATE st;
    while (XInputGetState(0, &st) == ERROR_SUCCESS && st.Gamepad.wButtons != 0) Sleep(30);
    while (true)
    {
        ZeroMemory(&st, sizeof(st));
        if (XInputGetState(0, &st) == ERROR_SUCCESS)
        {
            if (st.Gamepad.wButtons & XINPUT_GAMEPAD_A) { while (XInputGetState(0, &st) == ERROR_SUCCESS && st.Gamepad.wButtons != 0) Sleep(30); return true; }
            if (st.Gamepad.wButtons & XINPUT_GAMEPAD_B) { while (XInputGetState(0, &st) == ERROR_SUCCESS && st.Gamepad.wButtons != 0) Sleep(30); return false; }
        }
        Sleep(50);
    }
}
#endif


