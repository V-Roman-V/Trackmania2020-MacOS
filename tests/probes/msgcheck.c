/* What MsgWaitForMultipleObjectsEx answers when it isn't asked to wait, which is what a game
 * does many times per frame. Standard output has to be the same with and without
 * WINE_MSGWAIT_ASK_SERVER=1, and the cost of one call goes to standard error.
 * Run with: wine msgcheck.exe */
#include <windows.h>
#include <stdio.h>

static const char *name( DWORD ret )
{
    switch (ret)
    {
    case WAIT_OBJECT_0: return "signaled";
    case WAIT_OBJECT_0 + 1: return "second handle";
    case WAIT_TIMEOUT: return "timeout";
    case WAIT_IO_COMPLETION: return "apc";
    default: return "other";
    }
}

static DWORD poll_queue( DWORD mask, DWORD flags )
{
    return MsgWaitForMultipleObjectsEx( 0, NULL, 0, mask, flags );
}

static void show( const char *what, DWORD ret )
{
    printf( "%-48s %s\n", what, name( ret ) );
}

static void drain( void )
{
    MSG msg;
    while (PeekMessageW( &msg, NULL, 0, 0, PM_REMOVE )) DispatchMessageW( &msg );
}

/* poll without ever blocking, the way a game loop does */
static BOOL poll_until_signaled( void )
{
    DWORD start = GetTickCount();
    while (GetTickCount() - start < 2000)
        if (poll_queue( QS_ALLINPUT, 0 ) == WAIT_OBJECT_0) return TRUE;
    return FALSE;
}

static HWND window;
static volatile LONG sent_handled, timer_fired, apc_ran;

static LRESULT CALLBACK proc( HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam )
{
    if (msg == WM_APP) { InterlockedIncrement( &sent_handled ); return 42; }
    if (msg == WM_TIMER) { InterlockedIncrement( &timer_fired ); return 0; }
    return DefWindowProcW( hwnd, msg, wparam, lparam );
}

static DWORD WINAPI poster( void *arg )
{
    Sleep( 50 );
    PostThreadMessageW( (DWORD)(UINT_PTR)arg, WM_USER, 0, 0 );
    return 0;
}

static DWORD WINAPI sender( void *arg )
{
    Sleep( 50 );
    return (DWORD)SendMessageW( window, WM_APP, 0, 0 );
}

static void CALLBACK apc( ULONG_PTR arg )
{
    InterlockedIncrement( &apc_ran );
}

int main( void )
{
    WNDCLASSW cls = {.lpfnWndProc = proc, .lpszClassName = L"msgcheck"};
    LARGE_INTEGER freq, start, end;
    HANDLE thread, event, handles[1];
    DWORD result = 0;
    int i;

    RegisterClassW( &cls );
    window = CreateWindowW( L"msgcheck", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, NULL, NULL );
    drain();

    show( "empty queue", poll_queue( QS_ALLINPUT, 0 ) );
    show( "empty queue, input available", poll_queue( QS_ALLINPUT, MWMO_INPUTAVAILABLE ) );

    PostThreadMessageW( GetCurrentThreadId(), WM_USER, 0, 0 );
    show( "posted message", poll_queue( QS_ALLINPUT, 0 ) );
    show( "posted message, second look", poll_queue( QS_ALLINPUT, 0 ) );
    show( "posted message, mask without it", poll_queue( QS_KEY, 0 ) );
    GetQueueStatus( QS_ALLINPUT );
    show( "after GetQueueStatus", poll_queue( QS_ALLINPUT, 0 ) );
    show( "after GetQueueStatus, input available", poll_queue( QS_ALLINPUT, MWMO_INPUTAVAILABLE ) );
    drain();
    show( "after removing it", poll_queue( QS_ALLINPUT, 0 ) );

    thread = CreateThread( NULL, 0, poster, (void *)(UINT_PTR)GetCurrentThreadId(), 0, NULL );
    printf( "%-48s %s\n", "posted from another thread", poll_until_signaled() ? "seen" : "missed" );
    WaitForSingleObject( thread, INFINITE );
    drain();

    thread = CreateThread( NULL, 0, sender, NULL, 0, NULL );
    printf( "%-48s %s\n", "sent from another thread", poll_until_signaled() ? "seen" : "missed" );
    drain();
    WaitForSingleObject( thread, INFINITE );
    GetExitCodeThread( thread, &result );
    printf( "%-48s %s\n", "the sender got its answer", sent_handled == 1 && result == 42 ? "yes" : "no" );

    SetTimer( window, 1, 10, NULL );
    printf( "%-48s %s\n", "timer", poll_until_signaled() ? "seen" : "missed" );
    drain();
    KillTimer( window, 1 );
    printf( "%-48s %s\n", "the timer ran", timer_fired ? "yes" : "no" );
    drain();

    QueueUserAPC( apc, GetCurrentThread(), 0 );
    show( "alertable with an APC queued", poll_queue( QS_ALLINPUT, MWMO_ALERTABLE ) );
    printf( "%-48s %s\n", "the APC ran", apc_ran == 1 ? "yes" : "no" );
    QueueUserAPC( apc, GetCurrentThread(), 0 );
    show( "not alertable with an APC queued", poll_queue( QS_ALLINPUT, 0 ) );
    printf( "%-48s %s\n", "the APC waited", apc_ran == 1 ? "yes" : "no" );
    SleepEx( 0, TRUE );

    event = CreateEventW( NULL, TRUE, FALSE, NULL );
    handles[0] = event;
    show( "with a handle, not set", MsgWaitForMultipleObjectsEx( 1, handles, 0, QS_ALLINPUT, 0 ) );
    SetEvent( event );
    show( "with a handle, set", MsgWaitForMultipleObjectsEx( 1, handles, 0, QS_ALLINPUT, 0 ) );

    QueryPerformanceFrequency( &freq );
    QueryPerformanceCounter( &start );
    for (i = 0; i < 200000; i++) poll_queue( 0x1cff, 0 );
    QueryPerformanceCounter( &end );
    fprintf( stderr, "one look takes %.0f ns\n", (end.QuadPart - start.QuadPart) * 1e9 / freq.QuadPart / 200000 );
    return 0;
}
