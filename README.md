# Trackmania 2020 for Apple Silicon

Run Trackmania (2020) and Ubisoft Connect on an Apple Silicon Mac with a patched Wine runtime and DXVK, for free and without a paid compatibility layer.

This is an independent community project, not affiliated with Nadeo or Ubisoft. The game and Connect are not redistributed here. Connect downloads from Ubisoft, and you sign in with your own Ubisoft account.

This is early work, and it has only been tested on one Mac, an M5 Pro with macOS 27.0.1. There is no ready-made download yet, so you build the runtime yourself.

| Part | Status |
| --- | --- |
| Ubisoft Connect | Opens, signs in and installs the game |
| Trackmania | Starts and draws levels correctly, including Shadows |
| Fullscreen | Works with one display, but stays windowed while a second display is connected |
| Openplanet | Works, with plugins from its Plugin Manager |
| Online races | Not confirmed yet |

## Sign-in and the reported GPU

Please read this before you sign in. Ubisoft's sign-in is guarded by DataDome, which answers "Access is temporarily restricted" to Connect on Wine. It reads the WebGL renderer, and it rejected both SwiftShader and an Apple GPU on Windows ([MetalSharp issue #718](https://github.com/metalsharp/MetalSharp/issues/718)).

This project uses the same workaround as MetalSharp. WebGL in Connect's GPU process runs on the real GPU through DXVK and MoltenVK, while compositing stays in software, and `config/dxvk.conf` makes it report an AMD Radeon RX 6800 XT. That GPU is not the real one: this gets past a security check by reporting false hardware, so using it is your own decision and at your own risk, including any risk to your Ubisoft account. Only `UplayWebCore.exe` is affected, and the game keeps its own adapter.

## Requirements

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
bin/tm2020 connect
```

`make build` downloads pinned sources and compiles Wine, MoltenVK and DXVK. That took about 20 minutes on an M5 Pro with the Homebrew tools already installed. The steps are in [docs/building.md](docs/building.md).

`bin/tm2020 connect` installs Ubisoft Connect on first use and starts it. Sign in, install Trackmania from Connect, and press Play there.

## Everyday commands

| Command | Action |
| --- | --- |
| `bin/tm2020 connect` | Start Connect, and install it on first use |
| `bin/tm2020 install` | Run Connect's installer again |
| `bin/tm2020 stop` | Stop everything in this prefix, including the game |
| `bin/tm2020 check` | Tell whether this network reaches Ubisoft's download server |

To see the frame rate in the game, start Connect with `DXVK_HUD=fps,frametimes bin/tm2020 connect`.

## Display settings

Disconnect any external display before you start the game. With one connected, the game stays windowed and picking a resolution in its settings does nothing. The reason isn't known yet, and [docs/runtime.md](docs/runtime.md#known-limits) has what was found so far. Without one, real fullscreen worked at 1800×1169 on a 14-inch MacBook Pro.

The game keeps its settings in `~/Documents/Trackmania/Config/Default.json`. It writes that file back when it closes, so only edit it while the game is closed. The `Display` block has these settings:

| Setting | Values |
| --- | --- |
| `DisplayMode` | `windowed`, `windowedfull` (borderless) or `fullscreen` |
| `ScreenSizeFS` | Fullscreen size with a space, like `"1800 1169"` |
| `ScreenSizeWin` | Window size with an x, like `"1600x900"` |

## Openplanet

[Openplanet](https://openplanet.dev) works in this setup. Version 1.29.14 was tested, with its bundled plugins and some from its Plugin Manager. Two plugin installs failed once with `Unable to open zip file`. The one that was tried again then installed.

Download the installer from openplanet.dev, stop Connect, then run it with the folder that holds `Trackmania.exe`:

```sh
bin/tm2020 stop
WINEPREFIX=~/Games/TM2020/prefix \
  "$HOME/Library/Application Support/Trackmania2020/runtime/$(cat VERSION)/bin/wine" \
  OpenplanetNext_1.29.14.exe /S '/D=C:\users\<you>\Games\Trackmania'
```

The installer sets the DLL override it needs by itself. Run it again after a game update. Starter and Standard editions of the game only get signed plugins.

## Where files go

| Files | Location |
| --- | --- |
| Wine prefix, with Connect and the game | `~/Games/TM2020/prefix` |
| Game settings, maps and replays | `~/Documents/Trackmania` |
| Logs | `~/Games/TM2020/logs` |
| Shader cache | `~/Games/TM2020/dxvk-cache` |
| Runtime | `~/Library/Application Support/Trackmania2020/runtime` |
| Connect's installer | `~/Library/Caches/Trackmania2020` |

Set `TM2020_ROOT`, `TM2020_RUNTIME`, `TM2020_SUPPORT` or `TM2020_CACHE` to use other folders. To remove everything, run `bin/tm2020 stop` and delete these folders. Wine links the prefix's Documents folder to your real one, so keep `~/Documents/Trackmania` if you want your maps and replays.

## Metrics

Measured on an Apple M5 Pro with macOS 27.0.1 on 2 November 2026. The offline tests run the game's own shaders outside the game through DXVK, MoltenVK and Metal, so they show that a shader compiles or writes the right layers, not that a level looks right. Those tools are not in this repository, because they work on files extracted from the game.

| Metric | Result | Measured |
| --- | --- | --- |
| Frame rate | About 120–150 at 1600×900 windowed with Shadows High and 4x MSAA, capped at 150 | In play |
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
| Ubisoft Connect CPU use | `upc.exe` 50–75% of one core, GPU process about 22% | Before sign-in |
| Game install size | 6.4 GB | On disk |

## Troubleshooting

**"Unable to start download (Code: 17004)".** This is usually the network, not Wine. Some networks, such as campus and office ones, block Ubisoft's download server as a game application. Run `bin/tm2020 check` to see who answers, and install the game from another network if it is blocked. The `OPEN` result has not been seen on a real open network yet, so treat it as a good sign and not as proof.

**"Access is temporarily restricted" at sign-in.** Read [Sign-in and the reported GPU](#sign-in-and-the-reported-gpu). If you saw this page before, Connect's browser profile may be flagged. Stop Connect and move `~/Games/TM2020/prefix/drive_c/users/<you>/AppData/Local/Ubisoft Game Launcher/cache/http2` aside. Your games are not affected, and the next `bin/tm2020 connect` sets the folder up again.

**The game stays windowed.** Disconnect external displays before you start it. See [Display settings](#display-settings).

**Parts of a level are missing for a moment.** Shaders compile in the background on a first visit, so draws can be skipped until they are ready.

Wine's output is in `~/Games/TM2020/logs/connect.log`. Check it for account details and signed URLs before you share it.

## Development

`make check` runs the script checks and offline tests, and `make probes` builds the test programs. [docs/building.md](docs/building.md) explains the build, [docs/runtime.md](docs/runtime.md) covers each patch and the known limits, and [.github/CONTRIBUTING.md](.github/CONTRIBUTING.md) has the contribution rules.

## Acknowledgements

This project stands on other people's hard work. With gratitude to:

- [TrackMania-MacOS](https://github.com/vakesz/TrackMania-MacOS) for the Wine runtime and build system this one starts from.
- [Wine](https://www.winehq.org) for the Windows compatibility layer everything here is built on.
- [DXVK](https://github.com/doitsujin/dxvk) and the [d9vk fork](https://github.com/Sikarugir-App/d9vk) for turning Direct3D into Vulkan.
- [MoltenVK](https://github.com/KhronosGroup/MoltenVK) for running Vulkan on Apple's Metal.
- Apple's Rosetta 2 for translating the x86 code on Apple Silicon.
- [MetalSharp](https://github.com/metalsharp/MetalSharp) for working out the sign-in workaround.
- Porting Kit, whose Wine build showed the mode switch that works under Rosetta.
- [Openplanet](https://openplanet.dev) for its plugin platform.
- Nadeo and Ubisoft for Trackmania itself.

## License

This project's own scripts, tests and tools use the MIT license. Patches keep the license of the code they change. See [LICENSE](LICENSE).

Trademarks and game content belong to their owners. This is an unofficial community project, not affiliated with Nadeo or Ubisoft.
