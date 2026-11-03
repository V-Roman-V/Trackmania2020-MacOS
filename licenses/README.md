# Compiler runtime notices

DXVK, built from the D9VK source, links MinGW's C and C++ runtime support statically. `make build` copies these notices into the runtime's `licenses/toolchain` folder, next to that code. The compiler version of each build is in the runtime's `build-info.txt`.

- `mingw-w64.txt`: COPYING shipped with Homebrew's mingw-w64 14.0.0.
- `winpthreads.txt`: [mingw-w64 14.0.0 winpthreads COPYING](https://github.com/mingw-w64/mingw-w64/blob/v14.0.0/mingw-w64-libraries/winpthreads/COPYING).
- `GCC-RUNTIME.txt`: [GCC 16.2 runtime library exception](https://github.com/gcc-mirror/gcc/blob/releases/gcc-16.2.0/COPYING.RUNTIME).
- `GPL-3.0.txt`: [GCC 16.2 COPYING3](https://github.com/gcc-mirror/gcc/blob/releases/gcc-16.2.0/COPYING3).

The build also copies the license files from each pinned source tree. Check these notices again when the toolchain changes. They come from [TrackMania-MacOS](https://github.com/vakesz/TrackMania-MacOS), which collected them first.
