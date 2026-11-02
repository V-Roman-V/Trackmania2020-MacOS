# Runtime

The runtime starts from the Wine 11.17 runtime of [TrackMania-MacOS](https://github.com/vakesz/TrackMania-MacOS), which is x86_64 and runs under Rosetta 2 with new WoW64 and MoltenVK. This project adds five Wine patches and builds DXVK for 32-bit and 64-bit programs. [Building](building.md) explains how the pieces are put together.

## Patches

| Patch | Purpose | How it was checked |
| --- | --- | --- |
| Wine 0001 | Enter 64-bit code with a far call under Rosetta, because a far jump can leave the translator in 32-bit mode | Connect's renderer processes stop crashing |
| Wine 0002 | Forward drawing on another process's window to the process that owns it, so Connect's software-composited window appears | Connect's main window draws |
| Wine 0003 | Keep WebGL on D3D11 in Connect's GPU process, since Connect ignores Chromium switches | Sign-in passes; a test program read the WebGL renderer string through Connect's own ANGLE libraries |
| D9VK 0001 | Copy the whole adapter name, which was cut in half | Same test program; sign-in passes |
| D9VK 0002 | Avoid two shader forms that Metal rejects: `SV_Coverage` passed to a function, and a mip query on a multisampled texture | `tests/probes/msprobe.c` before and after; 33 failed pipelines in play before, 0 after |
| D9VK 0003 | Replace two geometry shaders with vertex shaders that pick the render target layer | Reading back every layer with the game's own shaders before and after; shadows correct in play |
| Wine 0004 | Queue Vulkan calls that return nothing and send them across together | `tests/probes/drawbench.c` before and after; `tests/probes/drawcheck.c` gives the same picture and compute result, and dropping any queued call changes them |
| D9VK 0004 | Fetch descriptor sets several at a time | Same two probes |
| Wine 0005 | Read the performance counter with `rdtsc` under Rosetta, without crossing to the Unix side | `tests/probes/qpccheck.c`, which fails if the counter starts wrong, stops catching up, or runs at the wrong rate |

Sign-in started to pass after D9VK 0001 and a fresh browser profile for Connect were tried together, so it isn't known which of the two mattered. If sign-in breaks after a Connect update, look at both first.

The 32-bit DXVK only has D9VK 0001. Only Connect uses it, and that is the build that passed sign-in. The 64-bit DXVK has all four.

The stand-in shaders are stored as bytecode in `patches/d9vk/0003`. Their HLSL source is in `tools/standin-shaders.c`.

## Why Connect and the game use different graphics paths

Connect's interface is Chromium, and its GPU process can't reach `upc.exe`'s window through OpenGL, because winemac has no cross-process GL. So `bin/tm2020` turns hardware acceleration off in Connect's settings and the interface composites in software through GDI, which Wine 0002 forwards to the window. This costs two copies per frame, which is fine for a launcher and wrong for a game.

WebGL draws offscreen, so it doesn't need the window. It runs on DXVK in Connect's GPU process, because Ubisoft's sign-in reads the WebGL renderer. See [the README](../README.md#sign-in-and-the-reported-gpu).

The game is a normal 64-bit D3D11 program. It gets DXVK from `bin/tm2020`, which sets DLL overrides for `UplayWebCore.exe` and `Trackmania.exe` only. Everything else in the prefix keeps Wine's own D3D, which stops at feature level 9_3 on this GPU.

## What a switch into Wine costs

DXVK is Windows code and MoltenVK is Unix code, so every Vulkan call between them crosses from one side to the other. On macOS each direction has to change GSBASE, because Windows code finds its thread data through `%gs` and macOS does too, in a different place. That takes a system call, `_thread_set_tsd_base`, and 175 ns under Rosetta. A whole crossing takes about 390 ns, so this one step is about 90% of it. `wrgsbase` would be the fast way, and it faults under Rosetta.

A draw took four such calls: `vkAllocateDescriptorSets`, `vkUpdateDescriptorSetWithTemplate`, `vkCmdBindDescriptorSets` and `vkCmdDraw`. That was about 1.4 µs of the 2 µs that DXVK's render thread spent on it. Wine 0004 takes care of the three that return nothing, and D9VK 0004 of the one that does.

Calls that go through wineserver cost more. In `tests/probes/callbench.c`, setting or waiting on an event takes 7 to 9 µs, and waking another thread with an event takes 22 µs. Locks stay in the process and take 4 to 13 ns.

`TM2020_TRACE=trackmania.exe:+syscall bin/tm2020 diag` records which calls the game makes, for the game alone. Each call adds about 130 bytes to `connect.log` and about 3.7 µs, so keep that run short and don't read its frame rate as normal. Two minutes of driving made 2.9 GB. In a race the game made about 2,370 calls per frame, and about 1,830 of them were on its busiest thread:

| Call | Per frame, busiest thread | Each | Per frame |
| --- | --- | --- | --- |
| `QueryPerformanceCounter` | 1,590 | 406 ns | 0.65 ms |
| `MsgWaitForMultipleObjectsEx` with no wait | 27 | 8.8 µs | 0.24 ms |
| `ReadProcessMemory` on itself, mostly 1 byte | 121 | 815 ns | 0.10 ms |

These counts come from one traced run on one map. The clock count follows frames and not time: it stays near 1,000 per frame in menus at 95 and at 127 FPS. Wine 0005 removes the first row. The second goes through wineserver for a plain "nothing here" answer. The third stays, because moving it to the Windows side would turn a failed read into an exception that the game would see.

## Connect during play

Connect uses more than a core with its window open, even with nothing happening, because each frame goes through software compositing and two copies. Minimizing the window takes that from about 120% of a core to 1%. `tools/connect-idle.c` does this when `Trackmania.exe` starts and restores the window when it exits, without activating anything, so it can't take focus from the game. It ends by itself two minutes after Connect is gone. In a 36-minute session Connect's median CPU use was 0%.

macOS doesn't turn Game Mode on for this setup. RunningBoard sees each Wine process as `anon<wine>`, with no app identity, and Connect starts the game, so an app bundle around Wine doesn't change that. `xcrun gamepolicyctl game-mode set on` forces it on for the whole system, and `set auto` goes back. That has not been measured.

## Settings

`config/dxvk.conf` has one section for each program. `UplayWebCore.exe` reports an AMD Radeon RX 6800 XT and has the state cache off, since Connect's folder isn't ours to write in. `Trackmania.exe` sees the real GPU and has `dxvk.enableAsync` on, so pipelines compile in the background and do not stall the render thread. A draw that needs an unfinished pipeline is skipped, so geometry can be missing for a moment on a first visit.

`bin/tm2020` keeps the state cache in `~/Games/TM2020/dxvk-cache`. It lists every pipeline the game has used, which helps when working out whether a shader is really used in play.

## Known limits

**Fullscreen and a second display.** With a second display connected, the game never calls `SetFullscreenState`, stays windowed, and picking a resolution in its settings does nothing. With only the laptop panel, it switched to real fullscreen about 15 seconds after starting. Both were seen once, and the two runs also had different saved display settings, so this still needs a clean comparison. Why a second display stops the game from asking is not known. The mode lists look fine in both cases, and DXVK reports no errors.

**A fullscreen hang in Wine.** `tests/probes/fsprobe.c` calls `SetFullscreenState` directly. With a second display connected, the call did not return within 13.7 seconds, although the display mode had changed. The suspect is `apply_display_settings` in win32u, which waits for explorer.exe's restorer window with no timeout, but that is not confirmed. The game does not reach this code with two displays and did not hang with one.

**winemac and other displays.** `macdrv_ChangeDisplaySettings` only handles the main display, so a game can't change the mode of an external one.

**Geometry shaders.** Metal has no geometry stage, and MoltenVK drops it without an error, wiring the vertex shader to the pixel shader. The game's shader cache has five geometry shaders. D9VK 0003 covers the two seen in play. `EnergyGeom`, `ParticleVoxelization` and `PixelBlendCubes` stay broken, and have not been seen in play so far.

**Tessellation.** The hull and domain shaders tried so far compile, but none has been seen in play, so how they look is unknown.

**A silent run is not a pass.** MoltenVK can drop what it does not support without an error. When testing a shader stage, run with `MVK_CONFIG_DEBUG=1` to see which stages were converted, and read the result back.
