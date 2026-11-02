// Enters real D3D11 fullscreen for about 3 seconds and logs what happens to the window and the display mode.
// Usage: fsprobe.exe <window x> [watchdog seconds] [any third argument skips fullscreen].
// The watchdog ends the process, which also restores the display.
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <stdio.h>
#include <stdlib.h>

static DWORD start, limit = 8;
static HWND window;
static IDXGISwapChain *swap;

#define NOW (GetTickCount() - start)

static LRESULT CALLBACK proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message)
    {
    case WM_ACTIVATEAPP: printf("[%5lu] WM_ACTIVATEAPP %d\n", NOW, (int)wparam); break;
    case WM_ACTIVATE: printf("[%5lu] WM_ACTIVATE %d\n", NOW, LOWORD(wparam)); break;
    case WM_SETFOCUS: printf("[%5lu] WM_SETFOCUS\n", NOW); break;
    case WM_KILLFOCUS: printf("[%5lu] WM_KILLFOCUS\n", NOW); break;
    case WM_SIZE: printf("[%5lu] WM_SIZE type %d %dx%d\n", NOW, (int)wparam, LOWORD(lparam), HIWORD(lparam)); break;
    case WM_DISPLAYCHANGE: printf("[%5lu] WM_DISPLAYCHANGE %dx%d\n", NOW, LOWORD(lparam), HIWORD(lparam)); break;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

static void pump(void)
{
    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

static void snapshot(const char *tag)
{
    BOOL fullscreen = FALSE;
    RECT rect;
    DEVMODEW one = {.dmSize = sizeof(one)}, two = {.dmSize = sizeof(two)};
    IDXGISwapChain_GetFullscreenState(swap, &fullscreen, NULL);
    GetWindowRect(window, &rect);
    EnumDisplaySettingsW(L"\\\\.\\DISPLAY1", ENUM_CURRENT_SETTINGS, &one);
    EnumDisplaySettingsW(L"\\\\.\\DISPLAY2", ENUM_CURRENT_SETTINGS, &two);
    printf("[%5lu] %-10s dxgi fullscreen=%d window (%ld,%ld)-(%ld,%ld) style=%08lx foreground=%d display1=%lux%lu@%lu display2=%lux%lu@%lu\n",
           NOW, tag, fullscreen, rect.left, rect.top, rect.right, rect.bottom, (unsigned long)GetWindowLongW(window, GWL_STYLE),
           GetForegroundWindow() == window, one.dmPelsWidth, one.dmPelsHeight, one.dmDisplayFrequency,
           two.dmPelsWidth, two.dmPelsHeight, two.dmDisplayFrequency);
}

static DWORD WINAPI watchdog(void *unused)
{
    Sleep(limit * 1000);
    printf("[%5lu] WATCHDOG\n", NOW);
    ExitProcess(3);
    return 0;
}

int main(int argc, char **argv)
{
    int x = argc > 1 ? atoi(argv[1]) : 100;
    if (argc > 2) limit = atoi(argv[2]);
    const UINT width = 1920, height = 1200;
    setvbuf(stdout, NULL, _IONBF, 0);
    start = GetTickCount();
    CreateThread(NULL, 0, watchdog, NULL, 0, NULL);

    WNDCLASSW cls = {.lpfnWndProc = proc, .hInstance = GetModuleHandleW(NULL), .lpszClassName = L"fsprobe"};
    RegisterClassW(&cls);
    window = CreateWindowExW(0, L"fsprobe", L"fsprobe", WS_OVERLAPPEDWINDOW, x, 80, 800, 450, NULL, NULL, cls.hInstance, NULL);
    ShowWindow(window, SW_SHOW);
    SetForegroundWindow(window);
    for (int i = 0; i < 30; i++)
    {
        pump();
        Sleep(10);
    }

    DXGI_SWAP_CHAIN_DESC desc = {0};
    desc.BufferDesc.Width = width;
    desc.BufferDesc.Height = height;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.OutputWindow = window;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

    ID3D11Device *device;
    ID3D11DeviceContext *context;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION,
                                               &desc, &swap, &device, NULL, &context);
    printf("[%5lu] create hr=%08lx\n", NOW, (unsigned long)hr);
    if (FAILED(hr))
        return 1;
    snapshot("windowed");

    printf("[%5lu] calling SetFullscreenState\n", NOW);
    hr = argc > 3 ? S_OK : IDXGISwapChain_SetFullscreenState(swap, TRUE, NULL);
    printf("[%5lu] SetFullscreenState(TRUE) hr=%08lx\n", NOW, (unsigned long)hr);
    hr = IDXGISwapChain_ResizeBuffers(swap, 0, width, height, DXGI_FORMAT_UNKNOWN, DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH);
    printf("[%5lu] ResizeBuffers hr=%08lx\n", NOW, (unsigned long)hr);

    ID3D11Texture2D *buffer;
    ID3D11RenderTargetView *view;
    IDXGISwapChain_GetBuffer(swap, 0, &IID_ID3D11Texture2D, (void **)&buffer);
    ID3D11Device_CreateRenderTargetView(device, (ID3D11Resource *)buffer, NULL, &view);
    const FLOAT green[4] = {0.0f, 0.45f, 0.2f, 1.0f};

    DWORD began = NOW, next = began + 500;
    while (NOW - began < 3000)
    {
        pump();
        ID3D11DeviceContext_OMSetRenderTargets(context, 1, &view, NULL);
        ID3D11DeviceContext_ClearRenderTargetView(context, view, green);
        IDXGISwapChain_Present(swap, 1, 0);
        if (NOW >= next)
        {
            snapshot("fullscreen");
            next += 900;
        }
    }

    hr = IDXGISwapChain_SetFullscreenState(swap, FALSE, NULL);
    printf("[%5lu] SetFullscreenState(FALSE) hr=%08lx\n", NOW, (unsigned long)hr);
    for (int i = 0; i < 20; i++)
    {
        pump();
        Sleep(10);
    }
    snapshot("after");
    printf("ALL DONE\n");
    return 0;
}
