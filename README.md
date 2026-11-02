# Trackmania 2020 for Apple Silicon

A free, open way to run Trackmania (2020) on Apple Silicon Macs with a Wine runtime tuned for this game, without a paid compatibility layer.

This is an independent community project, not affiliated with Nadeo or Ubisoft. The game is not redistributed here.

Status: early work. `bin/tm2020` installs Ubisoft Connect on Wine 11.17, but it does not run yet.

## Ubisoft Connect

`bin/tm2020 connect` installs Ubisoft Connect on first use and starts it. `bin/tm2020 stop` stops everything in this prefix.

The prefix lives in `~/Games/TM2020/prefix` and logs in `~/Games/TM2020/logs`. The runtime is for now a copy of the MacTMNF 0.3.0 runtime at `~/Library/Application Support/Trackmania2020/runtime/0.1.0-dev`.
