// Prints what a game sees through DXGI: adapters, outputs and matching modes. Changes nothing on screen.
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <dxgi.h>
#include <stdio.h>

static void list_modes(IDXGIOutput *output, DXGI_FORMAT format, const char *name)
{
    UINT count = 0;
    HRESULT hr = IDXGIOutput_GetDisplayModeList(output, format, 0, &count, NULL);
    printf("    %s: hr=%08lx modes=%u", name, (unsigned long)hr, count);
    if (SUCCEEDED(hr) && count)
    {
        DXGI_MODE_DESC *modes = calloc(count, sizeof(*modes));
        IDXGIOutput_GetDisplayModeList(output, format, 0, &count, modes);
        printf(" first %ux%u@%u last %ux%u@%u", modes[0].Width, modes[0].Height,
               modes[0].RefreshRate.Numerator / (modes[0].RefreshRate.Denominator ? modes[0].RefreshRate.Denominator : 1),
               modes[count - 1].Width, modes[count - 1].Height,
               modes[count - 1].RefreshRate.Numerator / (modes[count - 1].RefreshRate.Denominator ? modes[count - 1].RefreshRate.Denominator : 1));
        free(modes);
    }
    printf("\n");
}

static void closest(IDXGIOutput *output, UINT width, UINT height)
{
    DXGI_MODE_DESC want = {0}, got = {0};
    want.Width = width;
    want.Height = height;
    want.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    HRESULT hr = IDXGIOutput_FindClosestMatchingMode(output, &want, &got, NULL);
    printf("    closest to %ux%u: hr=%08lx -> %ux%u@%u\n", width, height, (unsigned long)hr, got.Width, got.Height,
           got.RefreshRate.Numerator / (got.RefreshRate.Denominator ? got.RefreshRate.Denominator : 1));
}

int main(void)
{
    IDXGIFactory *factory;
    HRESULT hr = CreateDXGIFactory(&IID_IDXGIFactory, (void **)&factory);
    printf("factory hr=%08lx\n", (unsigned long)hr);
    if (FAILED(hr))
        return 1;
    IDXGIAdapter *adapter;
    for (UINT a = 0; IDXGIFactory_EnumAdapters(factory, a, &adapter) == S_OK; a++)
    {
        DXGI_ADAPTER_DESC desc;
        IDXGIAdapter_GetDesc(adapter, &desc);
        printf("adapter %u: %ls\n", a, desc.Description);
        IDXGIOutput *output;
        UINT o = 0;
        for (; IDXGIAdapter_EnumOutputs(adapter, o, &output) == S_OK; o++)
        {
            DXGI_OUTPUT_DESC out;
            IDXGIOutput_GetDesc(output, &out);
            printf("  output %u: %ls attached=%d rect (%ld,%ld)-(%ld,%ld) monitor %p\n", o, out.DeviceName, out.AttachedToDesktop,
                   out.DesktopCoordinates.left, out.DesktopCoordinates.top,
                   out.DesktopCoordinates.right, out.DesktopCoordinates.bottom, out.Monitor);
            list_modes(output, DXGI_FORMAT_R8G8B8A8_UNORM, "RGBA8");
            list_modes(output, DXGI_FORMAT_B8G8R8A8_UNORM, "BGRA8");
            list_modes(output, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, "RGBA8 sRGB");
            list_modes(output, DXGI_FORMAT_R10G10B10A2_UNORM, "RGB10A2");
            closest(output, 1920, 1200);
            closest(output, 2048, 1330);
            closest(output, 1920, 1080);
            IDXGIOutput_Release(output);
        }
        printf("  %u outputs\n", o);
        IDXGIAdapter_Release(adapter);
    }
    printf("ALL DONE\n");
    return 0;
}
