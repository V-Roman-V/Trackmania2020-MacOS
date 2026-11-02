/* Check QueryPerformanceCounter against the system call behind it: what one read costs,
 * that the two agree, that it never goes back, and that another process sees the same
 * time. Prints FAIL and returns 1 if something is wrong. Set WINE_QPC_NO_TSC=1 to compare
 * with the old path. Run with: wine qpccheck.exe */
#include <windows.h>
#include <stdio.h>

typedef LONG (WINAPI *nt_qpc_func)( LARGE_INTEGER *, LARGE_INTEGER * );

static nt_qpc_func nt_qpc;
static volatile LONG stop;
static volatile LONGLONG last_seen;
static volatile LONG went_back;
static int failed;

static void check( const char *what, BOOL ok )
{
    printf( "%-4s %s\n", ok ? "ok" : "FAIL", what );
    if (!ok) failed = 1;
}

/* Being preempted can only widen the gap between the two system calls, so unlike comparing
 * two reads directly, this can't fail by bad luck. */
static void agreement( const char *what )
{
    LARGE_INTEGER before, now, after;
    LONGLONG early = LLONG_MAX, late = LLONG_MAX;
    int i, outside = 0;

    for (i = 0; i < 100000; i++)
    {
        nt_qpc( &before, NULL );
        QueryPerformanceCounter( &now );
        nt_qpc( &after, NULL );
        if (now.QuadPart < before.QuadPart - 1 || now.QuadPart > after.QuadPart + 1) outside++;
        if (now.QuadPart - before.QuadPart < early) early = now.QuadPart - before.QuadPart;
        if (after.QuadPart - now.QuadPart < late) late = after.QuadPart - now.QuadPart;
    }
    printf( "     read is at least %lld ticks after the call before it, %lld before the one after, "
            "%d of 100000 outside\n", early, late, outside );
    check( what, !outside );
}

static DWORD WINAPI reader( void *arg )
{
    LARGE_INTEGER now;
    LONGLONG own = 0, seen;

    while (!stop)
    {
        seen = last_seen;
        QueryPerformanceCounter( &now );
        if (now.QuadPart < seen || now.QuadPart < own) InterlockedIncrement( &went_back );
        own = now.QuadPart;
        last_seen = own;
    }
    return 0;
}

static int child( void )
{
    LARGE_INTEGER now;

    QueryPerformanceCounter( &now );
    printf( "%lld\n", now.QuadPart );
    return 0;
}

static LONGLONG read_child( const char *self )
{
    SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, TRUE};
    PROCESS_INFORMATION pi;
    STARTUPINFOA si = {sizeof(si)};
    HANDLE read_end, write_end;
    char cmd[MAX_PATH + 16], buffer[64] = {0};
    DWORD got = 0;

    CreatePipe( &read_end, &write_end, &sa, 0 );
    SetHandleInformation( read_end, HANDLE_FLAG_INHERIT, 0 );
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = write_end;
    snprintf( cmd, sizeof(cmd), "\"%s\" child", self );
    if (!CreateProcessA( NULL, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi )) return 0;
    CloseHandle( write_end );
    ReadFile( read_end, buffer, sizeof(buffer) - 1, &got, NULL );
    WaitForSingleObject( pi.hProcess, INFINITE );
    return _atoi64( buffer );
}

int main( int argc, char **argv )
{
    LARGE_INTEGER freq, a, b, t0, t1;
    LONGLONG child_time;
    HANDLE threads[4];
    int i;

    if (argc > 1) return child();
    nt_qpc = (nt_qpc_func)GetProcAddress( GetModuleHandleA( "ntdll.dll" ), "NtQueryPerformanceCounter" );

    QueryPerformanceFrequency( &freq );
    check( "frequency is 10 MHz", freq.QuadPart == 10000000 );

    QueryPerformanceCounter( &a );
    for (i = 0; i < 10000000; i++) QueryPerformanceCounter( &b );
    printf( "     one read takes %.1f ns\n", (b.QuadPart - a.QuadPart) * 100.0 / 10000000 );

    agreement( "agrees with the system call" );
    Sleep( 5000 );
    agreement( "still agrees 5 s later" );

    for (i = 0; i < ARRAYSIZE(threads); i++) threads[i] = CreateThread( NULL, 0, reader, NULL, 0, NULL );
    Sleep( 3000 );
    stop = 1;
    WaitForMultipleObjects( ARRAYSIZE(threads), threads, TRUE, INFINITE );
    check( "never goes back across 4 threads", !went_back );

    QueryPerformanceCounter( &t0 );
    child_time = read_child( argv[0] );
    QueryPerformanceCounter( &t1 );
    printf( "     child read %lld ticks after we started it, %lld before it ended\n",
            child_time - t0.QuadPart, t1.QuadPart - child_time );
    check( "another process sees the same time", t0.QuadPart <= child_time && child_time <= t1.QuadPart );

    return failed;
}
