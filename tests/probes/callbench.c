/* Times the Windows calls a game makes often, to find the ones that are slow under Wine.
 * Each line is the mean cost of one call, or of one round trip between two threads. */
#define _WIN32_WINNT 0x0602
#include <windows.h>
#include <synchapi.h>
#include <stdio.h>

static double tick_ns;

static double now_ns(void)
{
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart * tick_ns;
}

#define BENCH(name, count, ...) do { \
        double begin = now_ns(); \
        for (int i = 0; i < (count); i++) { __VA_ARGS__; } \
        printf("%-34s %12.0f ns\n", name, (now_ns() - begin) / (count)); \
    } while (0)

static HANDLE event_a, event_b, sem_a, sem_b;
static SRWLOCK srw = SRWLOCK_INIT;
static CONDITION_VARIABLE cv_a = CONDITION_VARIABLE_INIT, cv_b = CONDITION_VARIABLE_INIT;
static volatile LONG turn, address_turn;
static CRITICAL_SECTION contended;
static volatile LONG running;
static const int rounds = 20000;

static DWORD WINAPI event_partner(void *unused)
{
    for (int i = 0; i < rounds; i++)
    {
        WaitForSingleObject(event_a, INFINITE);
        SetEvent(event_b);
    }
    return 0;
}

static DWORD WINAPI semaphore_partner(void *unused)
{
    for (int i = 0; i < rounds; i++)
    {
        WaitForSingleObject(sem_a, INFINITE);
        ReleaseSemaphore(sem_b, 1, NULL);
    }
    return 0;
}

static DWORD WINAPI condition_partner(void *unused)
{
    AcquireSRWLockExclusive(&srw);
    for (int i = 0; i < rounds; i++)
    {
        while (turn != 1) SleepConditionVariableSRW(&cv_a, &srw, INFINITE, 0);
        turn = 0;
        WakeConditionVariable(&cv_b);
    }
    ReleaseSRWLockExclusive(&srw);
    return 0;
}

static DWORD WINAPI address_partner(void *unused)
{
    for (int i = 0; i < rounds; i++)
    {
        LONG expected = 0;
        while (address_turn == 0) WaitOnAddress(&address_turn, &expected, sizeof(expected), INFINITE);
        address_turn = 0;
        WakeByAddressSingle((void *)&address_turn);
    }
    return 0;
}

static DWORD WINAPI lock_partner(void *unused)
{
    while (running)
    {
        EnterCriticalSection(&contended);
        LeaveCriticalSection(&contended);
    }
    return 0;
}

static void two_threads(const char *name, LPTHREAD_START_ROUTINE partner, void (*one_round)(void))
{
    HANDLE thread = CreateThread(NULL, 0, partner, NULL, 0, NULL);
    double begin = now_ns();
    for (int i = 0; i < rounds; i++) one_round();
    printf("%-34s %12.0f ns\n", name, (now_ns() - begin) / rounds);
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
}

static void event_round(void) { SetEvent(event_a); WaitForSingleObject(event_b, INFINITE); }
static void semaphore_round(void) { ReleaseSemaphore(sem_a, 1, NULL); WaitForSingleObject(sem_b, INFINITE); }

static void condition_round(void)
{
    AcquireSRWLockExclusive(&srw);
    turn = 1;
    WakeConditionVariable(&cv_a);
    while (turn != 0) SleepConditionVariableSRW(&cv_b, &srw, INFINITE, 0);
    ReleaseSRWLockExclusive(&srw);
}

static void address_round(void)
{
    LONG expected = 1;
    address_turn = 1;
    WakeByAddressSingle((void *)&address_turn);
    while (address_turn == 1) WaitOnAddress(&address_turn, &expected, sizeof(expected), INFINITE);
}

int main(void)
{
    static char page[4096];
    LARGE_INTEGER frequency;
    MSG msg;
    POINT point;
    RECT rect;
    DWORD old;
    FILETIME filetime;
    CRITICAL_SECTION section;
    SRWLOCK lock = SRWLOCK_INIT;

    QueryPerformanceFrequency(&frequency);
    tick_ns = 1e9 / frequency.QuadPart;
    setvbuf(stdout, NULL, _IONBF, 0);
    InitializeCriticalSection(&section);
    InitializeCriticalSection(&contended);
    event_a = CreateEventW(NULL, FALSE, FALSE, NULL);
    event_b = CreateEventW(NULL, FALSE, FALSE, NULL);
    sem_a = CreateSemaphoreW(NULL, 0, 1, NULL);
    sem_b = CreateSemaphoreW(NULL, 0, 1, NULL);
    HANDLE manual = CreateEventW(NULL, TRUE, TRUE, NULL);
    HANDLE unset = CreateEventW(NULL, TRUE, FALSE, NULL);
    HANDLE automatic = CreateEventW(NULL, FALSE, FALSE, NULL);
    HANDLE mutex = CreateMutexW(NULL, FALSE, NULL);
    HANDLE semaphore = CreateSemaphoreW(NULL, 0, 1, NULL);
    HANDLE pair[2] = {unset, automatic};
    HWND window = CreateWindowExW(0, L"STATIC", L"callbench", WS_POPUP, 0, 0, 16, 16, NULL, NULL, NULL, NULL);

    puts("-- time");
    BENCH("QueryPerformanceCounter", 2000000, LARGE_INTEGER t; QueryPerformanceCounter(&t));
    BENCH("GetTickCount64", 2000000, GetTickCount64());
    BENCH("GetSystemTimePreciseAsFileTime", 2000000, GetSystemTimePreciseAsFileTime(&filetime));
    puts("-- locks in the process");
    BENCH("critical section, free", 5000000, EnterCriticalSection(&section); LeaveCriticalSection(&section));
    BENCH("SRW lock, free", 5000000, AcquireSRWLockExclusive(&lock); ReleaseSRWLockExclusive(&lock));
    puts("-- objects that go through wineserver");
    BENCH("SetEvent, already set", 200000, SetEvent(manual));
    BENCH("ResetEvent, already clear", 200000, ResetEvent(unset));
    BENCH("wait on a set event", 200000, WaitForSingleObject(manual, 0));
    BENCH("wait on a clear event, no timeout", 200000, WaitForSingleObject(unset, 0));
    BENCH("wait for 2 clear events, no timeout", 200000, WaitForMultipleObjects(2, pair, FALSE, 0));
    BENCH("SetEvent then wait, same thread", 100000, SetEvent(automatic); WaitForSingleObject(automatic, INFINITE));
    BENCH("release semaphore then wait", 100000, ReleaseSemaphore(semaphore, 1, NULL); WaitForSingleObject(semaphore, INFINITE));
    BENCH("mutex wait then release", 100000, WaitForSingleObject(mutex, INFINITE); ReleaseMutex(mutex));
    BENCH("create and close an event", 50000, CloseHandle(CreateEventW(NULL, FALSE, FALSE, NULL)));
    puts("-- one round trip between two threads");
    two_threads("event", event_partner, event_round);
    two_threads("semaphore", semaphore_partner, semaphore_round);
    two_threads("condition variable", condition_partner, condition_round);
    two_threads("WaitOnAddress", address_partner, address_round);
    running = 1;
    {
        HANDLE thread = CreateThread(NULL, 0, lock_partner, NULL, 0, NULL);
        BENCH("critical section, contended", 2000000, EnterCriticalSection(&contended); LeaveCriticalSection(&contended));
        running = 0;
        WaitForSingleObject(thread, INFINITE);
    }
    puts("-- scheduling");
    BENCH("Sleep(0)", 200000, Sleep(0));
    BENCH("SwitchToThread", 200000, SwitchToThread());
    BENCH("Sleep(1)", 200, Sleep(1));
    puts("-- window system");
    BENCH("PeekMessage, empty queue", 200000, PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE));
    BENCH("GetCursorPos", 100000, GetCursorPos(&point));
    BENCH("GetAsyncKeyState", 100000, GetAsyncKeyState(VK_SPACE));
    BENCH("GetForegroundWindow", 100000, GetForegroundWindow());
    BENCH("GetClientRect", 100000, GetClientRect(window, &rect));
    puts("-- memory");
    BENCH("VirtualProtect", 100000, VirtualProtect(page, sizeof(page), i & 1 ? PAGE_READWRITE : PAGE_READONLY, &old));
    VirtualProtect(page, sizeof(page), PAGE_READWRITE, &old);
    BENCH("VirtualAlloc and free, 64 KB", 50000, VirtualFree(VirtualAlloc(NULL, 65536, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE), 0, MEM_RELEASE));
    puts("ALL DONE");
    fflush(stdout);
    ExitProcess(0);
}
