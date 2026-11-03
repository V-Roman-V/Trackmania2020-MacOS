# Trackmania 2020 for Apple Silicon: development notes

This is the technical side of the project: what works and how it was checked, how to build the runtime yourself, every command, the performance work and the measurements. To install and play, the [README](../README.md) is enough.

The project runs Trackmania (2020) and Ubisoft Connect on an Apple Silicon Mac with a patched Wine runtime and DXVK. The project itself is free, and you don't need a paid compatibility layer.

It doesn't change what you can play. Trackmania has two kinds of access: Starter, which is free and has limited content, and Club, a yearly subscription with everything. Your Ubisoft account decides which one you have, and nothing in this project depends on it. Both have been tested, and they work the same way through Connect.

This is an independent community project, not affiliated with Nadeo or Ubisoft. The game and Connect are not redistributed here. Connect downloads from Ubisoft, and you sign in with your own Ubisoft account.

It has only been tested on one Mac so far, an M5 Pro with macOS 27.0.1. Players install a prebuilt runtime from the GitHub release with `install.sh`. This page builds the runtime yourself instead.

| Part | Status |
| --- | --- |
| Ubisoft Connect | Opens, signs in and installs the game |
| Trackmania | Starts and draws levels correctly, including Shadows |
| Starter and Club access | Both work. Your Ubisoft account decides which one you get |
| Fullscreen | Works with one display, but stays windowed while a second display is connected |
| Openplanet | Works, with plugins from its Plugin Manager |
| Online races | Work |

## Sign-in and the reported GPU

Please read this before you sign in. Ubisoft's sign-in is guarded by DataDome, which answers "Access is temporarily restricted" to Connect on Wine. It reads the WebGL renderer, and it rejected both SwiftShader and an Apple GPU on Windows ([MetalSharp issue #718](https://github.com/metalsharp/MetalSharp/issues/718)).

This project uses the same workaround as MetalSharp. WebGL in Connect's GPU process runs on the real GPU through DXVK and MoltenVK, while compositing stays in software, and `config/dxvk.conf` makes it report an AMD Radeon RX 6800 XT. That GPU is not the real one: this gets past a security check by reporting false hardware, so using it is your own decision and at your own risk, including any risk to your Ubisoft account. Only `UplayWebCore.exe` is affected, and the game keeps its own adapter.

## Requirements for building

- An Apple Silicon Mac with Rosetta 2. To add it: `softwareupdate --install-rosetta --agree-to-license`
- The full Xcode app and [Homebrew](https://brew.sh). The build installs the other tools it needs with `brew install`.
- About 9 GB of free disk space for the build, and 6.4 GB more for the game.
- A folder path without spaces for the checkout, which Wine's makefiles need.
- A network that reaches Ubisoft's download server. See [Troubleshooting](#troubleshooting).

## Build and start

```sh
sudo xcode-select --switch /Applications/Xcode.app/Contents/Developer
git clone https://github.com/V-Roman-V/Trackmania2020-MacOS.git
cd Trackmania2020-MacOS
make build
make install
```

`make build` downloads pinned sources and compiles Wine, MoltenVK and DXVK. That took about 12 minutes on an M5 Pro with the Homebrew tools already installed. The steps are in [building.md](building.md).

`make install` runs `install.sh`, which finds the runtime you just built and doesn't download one. It installs Ubisoft Connect, copies the launcher to `~/Library/Application Support/Trackmania2020/launcher` and makes the Trackmania 2020 app, which runs that copy. To work on the scripts, run `bin/tm2020 connect` from the checkout instead. Sign in, install Trackmania from Connect, and press Play there.

## Everyday commands

| Command | Action |
| --- | --- |
| `bin/tm2020 start` | Start Connect in the background and return, which is what the app does |
| `bin/tm2020 connect` | Start Connect, install it on first use, and wait until it closes |
| `bin/tm2020 stop` | Stop everything in this prefix, including the game |
| `bin/tm2020 openplanet <installer>` | Install Openplanet into the game's folder, while the game is closed |
| `bin/tm2020 check-download` | Tell whether this network lets Connect download from Ubisoft's server |
| `bin/tm2020 diag` | Start Connect the same way, and record frame rate, CPU and GPU use while you play |
| `bin/tm2020 report` | Show what the last `diag` session recorded |
| `bin/tm2020 prepare` | Install Connect if it is missing and set up the prefix, without starting anything |
| `bin/tm2020 install` | Run Connect's installer again |
| `bin/tm2020 app` | Make the Trackmania 2020 app, which runs this copy's `start` |
| `bin/tm2020 uninstall` | Remove Connect, the game, the runtimes, the launcher and the app, after a typed yes |

`diag` and `report` need `python3`, which comes with Apple's command line tools. Nothing else does.

To see the frame rate in the game, start Connect with `DXVK_HUD=fps,frametimes bin/tm2020 connect`.

## Performance

Three changes make the game cheaper to run, and none of them touches a game setting.

**Connect rests while you play.** Connect keeps redrawing its window behind the game, which costs more than a whole core. It stops when its window is minimized, so a small helper minimizes it when the game starts and brings it back when the game closes.

**Fewer switches into Wine.** Every Vulkan call from DXVK has to switch from Windows code to Unix code, and under Rosetta each switch takes about 390 ns, which added up to most of the work of DXVK's render thread. Calls that return nothing now wait in a queue and go across together. To compare with the old way, start Connect with `WINEVULKAN_NO_BATCH=1 bin/tm2020 connect`.

**Reading the clock stays in Windows code.** The game reads the clock about 1,900 times per frame, and each read used to be one of those switches. Under Rosetta the same time can be read with one fast instruction, which takes about 9 ns where the switch took 400 ns. The game also checks its message queue about 27 times per frame, and each check went through Wine's server. It can now read the answer from shared memory, which takes 441 ns where the server took 10 µs. To compare with the old way, start Connect with `WINE_QPC_NO_TSC=1 WINE_MSGWAIT_ASK_SERVER=1 bin/tm2020 connect`. In play they took about 0.4 ms of work per frame off the game's busiest thread, but in the one minute of driving measured so far the frame rate did not go up.

The first two changes are confirmed in play. Connect's median CPU use was 0% over a 36-minute session. On one map with the same settings, the second-busiest game thread went from 5.3 ms to 2.8 ms per frame, and the median went from 119 to 124 FPS. That is one run each, about 12 minutes long. The busiest thread barely changed and looks like the next limit. `bin/tm2020 diag` records what a real session does.

## Display settings

Disconnect any external display before you start the game. With one connected, the game stays windowed and picking a resolution in its settings does nothing. The reason isn't known yet, and [runtime.md](runtime.md#known-limits) has what was found so far. Without one, real fullscreen worked at 1800×1169 on a 14-inch MacBook Pro.

The game keeps its settings in `~/Documents/Trackmania/Config/Default.json`. It writes that file back when it closes, so only edit it while the game is closed. The `Display` block has these settings:

| Setting | Values |
| --- | --- |
| `DisplayMode` | `windowed`, `windowedfull` (borderless) or `fullscreen` |
| `ScreenSizeFS` | Fullscreen size with a space, like `"1800 1169"` |
| `ScreenSizeWin` | Window size with an x, like `"1600x900"` |

## Openplanet

[Openplanet](https://openplanet.dev) works in this setup. Version 1.29.14 was tested, with its bundled plugins and some from its Plugin Manager. Two plugin installs failed once with `Unable to open zip file`. The one that was tried again then installed.

Download the installer from openplanet.dev, quit the game and Connect, then:

```sh
bin/tm2020 openplanet ~/Downloads/OpenplanetNext_1.29.14.exe
```

This runs the installer silently with `/S /D=<folder>`, where the folder is the `InstallDir` that Connect wrote to `HKLM\Software\Wow6432Node\Ubisoft\Launcher\Installs\5595`. The installer refuses a folder without `Trackmania.exe`, and sets the DLL override it needs by itself. Run it again after a game update. With Starter access, Openplanet only loads signed plugins.

## Where files go

| Files | Location |
| --- | --- |
| Wine prefix, with Connect and the game | `~/Games/TM2020/prefix` |
| Game settings, maps and replays | `~/Documents/Trackmania` |
| Logs | `~/Games/TM2020/logs` |
| Shader cache | `~/Games/TM2020/dxvk-cache` |
| Runtime, one folder for each version | `~/Library/Application Support/Trackmania2020/runtime` |
| Launcher that the app runs | `~/Library/Application Support/Trackmania2020/launcher` |
| The app | `/Applications/Trackmania 2020.app`, or `~/Applications` when `/Applications` isn't writable |
| Connect's installer, and downloads while installing | `~/Library/Caches/Trackmania2020` |

Set `TM2020_ROOT`, `TM2020_RUNTIME`, `TM2020_SUPPORT`, `TM2020_CACHE` or `TM2020_APP` to use other folders. `bin/tm2020 app` writes them into the app. `bin/tm2020 uninstall` removes everything in this table except `~/Documents/Trackmania`. Wine links the prefix's Documents folder to your real one, so your maps and replays stay.

## Metrics

Measured on an Apple M5 Pro with macOS 27.0.1 on 2 November 2026. The offline tests run the game's own shaders outside the game through DXVK, MoltenVK and Metal, so they show that a shader compiles or writes the right layers, not that a level looks right. Those tools are not in this repository, because they work on files extracted from the game.

| Metric | Result | Measured |
| --- | --- | --- |
| Frame rate | About 120–150 at 1600×900 windowed with Shadows High and 4x MSAA, capped at 150 | In play |
| Frame rate in fullscreen | Median 123, slowest 10% at 106 or below, at 1800×1169 with Shadows High and 4x MSAA, capped at 150, over 36 minutes; each map's median was between 113 and 149 | In play |
| Shadows on High | Correct with `patches/d9vk/0003`, almost black without it | In play |
| Real fullscreen | Works at 1800×1169 with one display | In play |
| Pipelines the game has used so far | 694 (668 graphics, 26 compute) | In play |
| Pipelines that failed to compile | 33 before `patches/d9vk/0002`, 0 after | In play |
| Direct3D feature level | 11_0 on DXVK, 9_3 on Wine's own D3D11 | Offline test |
| Shaders in the game's cache | 4,860 | Offline test |
| Pixel shaders that fail on Metal | 0 of 3,298 | Offline test |
| Vertex shaders that fail on Metal | 0 of 1,353 | Offline test |
| Compute shaders that fail on Metal | 0 of 196 | Offline test |
| Hull and domain shaders | 8 in the cache, none used in play so far; Ocean and `Block_TAddLighted` compile | Offline test |
| Geometry shaders | 5 in the cache: 2 used in play and stood in for, 3 unsupported | Offline test and play |
| Light probe layers written | 0 of 3 before `patches/d9vk/0003`, 3 of 3 after | Offline test |
| HBAO+ layers written | 1 of 16 before `patches/d9vk/0003`, 16 of 16 after | Offline test |
| Graphics stack time for 3,000 draws, 1800×1169 with 4x MSAA | 7.5 ms before `patches/wine/0004` and `patches/d9vk/0004`, 3.4 ms after, same picture | Offline test |
| Ubisoft Connect CPU use | About 120–140% of one core with its window open, 1% with it minimized | Idle |
| Ubisoft Connect CPU use during play | Median 0% with the helper over 36 minutes | In play |
| Same map before and after `patches/wine/0004` and `patches/d9vk/0004`, one run each | Median 119 → 124 FPS, slowest 10% at 107 → 114, samples under 110 FPS 16% → 3%, game CPU 274% → 227% of a core, GPU about the same | In play |
| Same map before and after `patches/wine/0005` and `0006`, the first minute of one run each | Busiest game thread 5.4 → 5.0 ms of work per frame, wineserver 10.7% → 9.7% of a core, median 129 → 122 FPS; too short to tell the frame rate | In play |
| Game install size | 6.4 GB | On disk |

## Troubleshooting

**"Unable to start download (Code: 17004)".** This is usually the network, not Wine. Some networks, such as campus and office ones, block Ubisoft's download server as a game application. Run `bin/tm2020 check-download` to see who answers, and install the game from another network if it is blocked. The `OPEN` result has not been seen on a real open network yet, so treat it as a good sign and not as proof.

**"Access is temporarily restricted" at sign-in.** Read [Sign-in and the reported GPU](#sign-in-and-the-reported-gpu). If you saw this page before, Connect's browser profile may be flagged. Stop Connect and move `~/Games/TM2020/prefix/drive_c/users/<you>/AppData/Local/Ubisoft Game Launcher/cache/http2` aside. Your games are not affected, and the next `bin/tm2020 connect` sets the folder up again.

**The game stays windowed.** Disconnect external displays before you start it. See [Display settings](#display-settings).

**Parts of a level are missing for a moment.** Shaders compile in the background on a first visit, so draws can be skipped until they are ready.

Wine's output is in `~/Games/TM2020/logs/connect.log`. Check it for account details and signed URLs before you share it.

## Development

`make check` runs the script checks and offline tests, and `make probes` builds the test programs. [building.md](building.md) explains the build and how a release is packed, [runtime.md](runtime.md) covers each patch and the known limits, and [CONTRIBUTING.md](../.github/CONTRIBUTING.md) has the contribution rules.

## Acknowledgements

This project stands on other people's hard work. With gratitude to:

- [TrackMania-MacOS](https://github.com/vakesz/TrackMania-MacOS) for the way to build Wine on Apple Silicon that this build follows, and for five of the Wine patches.
- [Wine](https://www.winehq.org) for the Windows compatibility layer everything here is built on.
- [DXVK](https://github.com/doitsujin/dxvk) and the [d9vk fork](https://github.com/Sikarugir-App/d9vk) for turning Direct3D into Vulkan.
- [MoltenVK](https://github.com/KhronosGroup/MoltenVK) for running Vulkan on Apple's Metal.
- Apple's Rosetta 2 for translating the x86 code on Apple Silicon.
- [MetalSharp](https://github.com/metalsharp/MetalSharp) for working out the sign-in workaround.
- Porting Kit, whose Wine build showed the mode switch that works under Rosetta.
- [Openplanet](https://openplanet.dev) for its plugin platform.
- Nadeo and Ubisoft for Trackmania itself.

## License

This project's own scripts, tests and tools use the MIT license. Patches keep the license of the code they change. See [LICENSE](../LICENSE).

Trademarks and game content belong to their owners. This is an unofficial community project, not affiliated with Nadeo or Ubisoft.
