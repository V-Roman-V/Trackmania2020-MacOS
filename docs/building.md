# Building

## Source layout

- `bin/tm2020` installs and starts Ubisoft Connect, and gives DXVK to the programs that need it.
- `tools/build-runtime.sh` builds the runtime.
- `tools/connect-idle.c` minimizes Connect during play, and is built into the runtime.
- `tools/session.py` records and reports a play session for `bin/tm2020 diag` and `report`.
- `config/sources.json` pins every source the runtime is built from.
- `config/dxvk.conf` holds the DXVK settings for each program.
- `patches/wine` and `patches/d9vk` are the changes to Wine and DXVK. `patches/wine/base` holds general macOS fixes from TrackMania-MacOS, which apply first. [Runtime](runtime.md) describes each one.
- `licenses` holds the compiler runtime notices that go into the runtime.
- `tests/test-cli.sh` tests `bin/tm2020` with stand-ins for curl and Wine.
- `tests/probes` holds small Windows programs that check displays, fullscreen, shader compilation and speed.
- `tools/standin-shaders.c` is the source of the bytecode in `patches/d9vk/0003`.

Everything generated goes under `.build`, which is never committed.

## What make build does

`make build` runs `tools/build-runtime.sh`, which:

1. Checks for an Apple Silicon Mac with Rosetta, the full Xcode and Homebrew, and installs the Homebrew tools that are missing: `mingw-w64`, `meson`, `ninja`, `glslang`, `bison`, `flex` and `pkgconf`.
2. Downloads the sources pinned in `config/sources.json` into `.build/runtime/sources`. Wine and D9VK come from git at fixed revisions. MoltenVK and the six libraries come as release archives, which have to match their SHA-256. Then it applies `patches/wine/base` and `patches/wine` to Wine.
3. Builds freetype, gmp, nettle, libtasn1, libunistring, gnutls and MoltenVK for x86_64, then Wine for 32-bit and 64-bit Windows programs.
4. Puts the runtime together. Every macOS binary is relinked to the libraries next to it, stripped and signed, and the license files of every source are copied into `licenses`.
5. Builds `d3d11.dll` and `dxgi.dll` from the D9VK source for 32-bit and 64-bit programs, with the patches for each, and `connect-idle.exe`.
6. Checks that no file contains the checkout path, so nobody's home folder ends up in a runtime, and moves the result to `~/Library/Application Support/Trackmania2020/runtime/<VERSION>`.

The output of every command goes to `.build/runtime/build.log`.

An installed runtime is never replaced. To rebuild, remove it first, or set `TM2020_RUNTIME` to another folder. `bin/tm2020` reads the same variable, so you can try a new runtime next to the old one:

```sh
TM2020_RUNTIME="$PWD/.build/runtime-new" make build
TM2020_RUNTIME="$PWD/.build/runtime-new" bin/tm2020 connect
```

Use a checkout path without spaces, since Wine's makefiles need that. Builds use every CPU core by default. Set `TM2020_JOBS=8` to leave some free.

Running `make build` again reuses what is still valid, and takes about a minute. After a change to a Wine patch, only the files that Wine's patches touch are compiled again. DXVK is always rebuilt from a clean copy.

## Changing a patch

Patches are the edit surface, because the trees under `.build` are thrown away. A Wine patch has to apply with `git apply` after the ones in `patches/wine/base`, against the Wine revision in `config/sources.json`. A D9VK patch goes on the D9VK revision there. The prepared trees are in `.build/runtime/sources`.

Keep a short header in each patch that says what it does and why.

## Checks

`make check` needs ShellCheck, and runs without a runtime or the network:

```sh
brew install shellcheck
make check
```

`make probes` builds the test programs into `.build/probes`. It needs `mingw-w64`, which `make build` installs. Run them in a throwaway prefix, so they can't touch your real one. Point `WINEDLLOVERRIDES` at the DLLs you want to test, and copy `dxgi.dll` and `d3d11.dll` next to the program:

```sh
RUNTIME="$HOME/Library/Application Support/Trackmania2020/runtime/$(cat VERSION)"
cp "$RUNTIME"/lib/dxvk/x64/*.dll .build/probes/
cd .build/probes
export WINEPREFIX="$PWD/prefix" WINEDLLOVERRIDES='dxgi,d3d11,d3dcompiler_47=n;mscoree,mshtml=d' DXVK_CONFIG_FILE=
"$RUNTIME/bin/wine" modes.exe
```

| Probe | What it shows |
| --- | --- |
| `modes.exe` | The displays Wine reports and the modes each one offers |
| `dxgiprobe.exe` | Adapters, outputs and display modes as a game sees them through DXGI. Changes nothing on screen |
| `fsprobe.exe <window x>` | Enters real fullscreen for a few seconds and logs what happens. It changes your display mode, and the watchdog restores it |
| `msprobe.exe d3dcompiler_47.dll` | Whether the two shader forms from D9VK 0002 draw on Metal. Copy `d3dcompiler_47.dll` from the game folder next to it first, as Wine's own rejects `SV_Coverage` |
| `callbench.exe` | What the Windows calls a game makes often cost, in ns |
| `drawbench.exe <draws> <frames>` | What a frame costs through DXVK, MoltenVK and Metal, with a checksum of the picture |
| `drawcheck.exe` | A checksum of a scene that uses every call Wine 0004 queues. It has to be the same with `WINEVULKAN_NO_BATCH=1` |
| `qpccheck.exe` | Whether `QueryPerformanceCounter` agrees with the system call behind it, never goes back, and matches another process. Prints what one read costs. Every line has to say `ok`, with and without `WINE_QPC_NO_TSC=1` |
| `msgcheck.exe` | What `MsgWaitForMultipleObjectsEx` answers when it isn't asked to wait. Standard output has to be the same with `WINE_MSGWAIT_ASK_SERVER=1` |

To see what the game itself asks for, `bin/tm2020 connect` writes Wine's output to `~/Games/TM2020/logs/connect.log`. After a run, count `Failed to compile pipeline` and `Shader library compile failed` there. Both should be zero.

The shader sweep behind the README's offline numbers is not in this repository, because it works on shaders extracted from the game. Please do not commit those.
