// Lists the displays Wine reports and the modes each one offers, as a game would see them.
#include <windows.h>
#include <stdio.h>

int main(void)
{
    DISPLAY_DEVICEW device = {.cb = sizeof(device)};
    for (DWORD index = 0; EnumDisplayDevicesW(NULL, index, &device, 0); index++)
    {
        if (!(device.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP))
            continue;
        DEVMODEW current = {.dmSize = sizeof(current)};
        EnumDisplaySettingsW(device.DeviceName, ENUM_CURRENT_SETTINGS, &current);
        printf("%ls%s current %lux%lu @%lu at (%ld,%ld)\n", device.DeviceName,
               device.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE ? " PRIMARY" : "",
               current.dmPelsWidth, current.dmPelsHeight, current.dmDisplayFrequency,
               current.dmPosition.x, current.dmPosition.y);
        DEVMODEW mode = {.dmSize = sizeof(mode)};
        for (DWORD n = 0; EnumDisplaySettingsW(device.DeviceName, n, &mode); n++)
            if (mode.dmBitsPerPel == 32)
                printf("  %lux%lu @%lu\n", mode.dmPelsWidth, mode.dmPelsHeight, mode.dmDisplayFrequency);
    }
    return 0;
}
