/* Minimizes Ubisoft Connect's window while the game runs, and brings it back afterwards.
 * Connect keeps redrawing behind the game, which costs more than a core here, and it stops
 * when its window is minimized. The game needs Connect running, so closing it is no option. */
#include <windows.h>
#include <tlhelp32.h>
#include <wchar.h>

#define POLL_MS 1000
/* Connect restarts itself after an update, so its going away is not the end. */
#define POLLS_WITHOUT_CONNECT 120

static DWORD find_process(const WCHAR *name)
{
    PROCESSENTRY32W entry = {.dwSize = sizeof(entry)};
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    DWORD pid = 0;

    if (snapshot == INVALID_HANDLE_VALUE)
        return 0;
    if (Process32FirstW(snapshot, &entry))
    {
        do
        {
            if (!_wcsicmp(entry.szExeFile, name))
                pid = entry.th32ProcessID;
        }
        while (!pid && Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return pid;
}

struct search
{
    DWORD connect;
    HWND window;
};

static BOOL CALLBACK check_window(HWND window, LPARAM param)
{
    struct search *search = (struct search *)param;
    WCHAR name[32];
    DWORD pid;

    GetWindowThreadProcessId(window, &pid);
    if (pid == search->connect && IsWindowVisible(window) && !IsIconic(window)
        && GetClassNameW(window, name, ARRAYSIZE(name)) && !wcscmp(name, L"Chrome_WidgetWin_1"))
    {
        search->window = window;
        return FALSE;
    }
    return TRUE;
}

static void rest_during_game(DWORD game, HANDLE connect_process, DWORD connect)
{
    struct search search = {connect, NULL};
    HANDLE game_process = OpenProcess(SYNCHRONIZE, FALSE, game);
    HANDLE ends[2] = {game_process, connect_process};

    if (!game_process)
        return;
    /* This doesn't activate anything, so it can't take focus from the game. If the window is
     * already minimized there is nothing to do now or to bring back later. */
    EnumWindows(check_window, (LPARAM)&search);
    if (search.window)
        ShowWindow(search.window, SW_SHOWMINNOACTIVE);
    WaitForMultipleObjects(ARRAYSIZE(ends), ends, FALSE, INFINITE);
    CloseHandle(game_process);
    if (search.window && IsWindow(search.window) && IsIconic(search.window))
        ShowWindow(search.window, SW_RESTORE);
}

int main(void)
{
    int polls = 0;

    while (polls < POLLS_WITHOUT_CONNECT)
    {
        DWORD connect = find_process(L"upc.exe"), game;
        HANDLE process = connect ? OpenProcess(SYNCHRONIZE, FALSE, connect) : NULL;

        if (!process)
        {
            polls++;
            Sleep(POLL_MS);
            continue;
        }
        polls = 0;
        while (WaitForSingleObject(process, POLL_MS) == WAIT_TIMEOUT)
        {
            if ((game = find_process(L"Trackmania.exe")))
                rest_during_game(game, process, connect);
        }
        CloseHandle(process);
    }
    return 0;
}
