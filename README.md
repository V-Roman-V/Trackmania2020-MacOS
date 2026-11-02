# Trackmania 2020 for Apple Silicon

A free, open way to run Trackmania (2020) on Apple Silicon Macs with a Wine runtime tuned for this game, without a paid compatibility layer.

This is an independent community project, not affiliated with Nadeo or Ubisoft. The game is not redistributed here.

Status: early work. Ubisoft Connect opens, draws its window and signs in on Wine 11.17 with the patches in `patches/wine` and `patches/d9vk` and the workaround below. The game itself is not tried.

## Ubisoft Connect

`bin/tm2020 connect` installs Ubisoft Connect on first use and starts it. `bin/tm2020 stop` stops everything in this prefix.

The prefix lives in `~/Games/TM2020/prefix` and logs in `~/Games/TM2020/logs`. The runtime is for now a copy of the MacTMNF 0.3.0 runtime at `~/Library/Application Support/Trackmania2020/runtime/0.1.0-dev`, with `wow64cpu.dll`, `win32u.so` and the 32-bit `kernelbase.dll` rebuilt from the patches, and 32-bit `d3d11.dll` and `dxgi.dll` in `lib/dxvk/x32`, built from the same d9vk source with `patches/d9vk`.

## Sign-in and the reported GPU

Ubisoft's sign-in is guarded by DataDome, which answers "Access is temporarily restricted" to Connect on Wine. It reads the WebGL renderer, and it rejected both SwiftShader and an Apple GPU on Windows ([MetalSharp issue #718](https://github.com/metalsharp/MetalSharp/issues/718)).

This setup uses the same workaround as MetalSharp. WebGL in Connect's GPU process runs on the real GPU through DXVK and MoltenVK, while compositing stays in software, and `config/dxvk.conf` makes it report an AMD Radeon RX 6800 XT. That GPU is not the real one: this gets past a security check by reporting false hardware, so it is your own decision and at your own risk. Only `UplayWebCore.exe` is affected, and games keep their own adapter.
