# Contributing

Thanks for helping with this project. It runs Ubisoft Connect and Trackmania (2020) on Apple Silicon, so a change is judged by whether Connect still signs in and the game still plays correctly.

Reports from other Macs and other macOS versions are very welcome, even if everything works. So far this has only been tested on one M5 Pro with macOS 27.0.1.

## Before you start

- You need an Apple Silicon Mac, Rosetta 2, the full Xcode app and Homebrew.
- Read [docs/building.md](../docs/building.md) for how the runtime is built, and [docs/runtime.md](../docs/runtime.md) for what each patch does.
- Open an issue before a large change, so we can agree on the approach first.

## Before you open a pull request

```sh
make check
```

- Keep shell scripts ShellCheck-clean. CI runs `make check` on every push.
- A runtime patch goes under `patches/wine` or `patches/d9vk`, with a short header that says what it does and why.
- Say how you checked a runtime change. Compiling is not enough for a shader change, because MoltenVK can drop a stage without an error, so read the result back or show it in play.
- Do not commit game files, shaders extracted from the game, installers, your Wine prefix, or anything under `.build`.

## Commits and pull requests

- Write commit messages in the imperative mood, and explain the why as well as the what.
- Fill in the pull request template. Say how you tested, on which macOS version and which Mac.
