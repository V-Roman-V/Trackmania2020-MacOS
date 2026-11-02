#!/usr/bin/env python3
"""Records and reports what limits a play session, for bin/tm2020 diag and report.

  session.py monitor <session.tsv>                 sample CPU and GPU use until the game has gone
  session.py report <session.tsv> <connect.log>    frame rate, and where the time went

Every sample takes a few ps calls and one ioreg call, every 2 seconds. That costs about
a percent of one core, and none of it on the game's threads.
"""

import re
import statistics
import subprocess
import sys
import time
from pathlib import Path

GAME = 'Trackmania.exe'
INTERVAL = 2.0
# Below this the game is on a loading screen, which says nothing about play.
LOADING_FPS = 15
FPS_LINE = re.compile(r'^(\d+\.\d+):([0-9a-f]+):[0-9a-f]+:trace:fps:\S+ \S+ @ approx ([\d.]+)fps')
COLUMNS = ('time', 'game_cpu', 'wineserver_cpu', 'other_wine_cpu', 'threads', 'top_threads',
           'gpu_device', 'gpu_renderer', 'gpu_tiler', 'memory_mb')


def run(*command):
    return subprocess.run(command, capture_output=True, text=True, errors='replace').stdout


def seconds(text):
    """Turns ps's [dd-]hh:mm:ss.cc into seconds."""
    days, _, rest = text.strip().rpartition('-')
    total = 0.0
    for part in rest.split(':'):
        total = total * 60 + float(part)
    return total + int(days or 0) * 86400


def wine_processes():
    """Maps pid to (name, cpu seconds, megabytes) for the game, wineserver and everything else in Wine."""
    found = {}
    for line in run('ps', '-axww', '-o', 'pid=,time=,rss=,command=').splitlines():
        fields = line.split(None, 3)
        if len(fields) < 4:
            continue
        pid, cputime, rss, command = fields
        if 'wineserver' in command:
            name = 'wineserver'
        elif not re.match(r'[A-Za-z]:[\\/]', command):
            continue
        else:
            name = 'game' if GAME in command else 'other'
        found[pid] = (name, seconds(cputime), int(rss) // 1024)
    return found


def top_threads(pid):
    """Returns the CPU percentages of the game's busiest threads, largest first."""
    percents = []
    for line in run('ps', '-M', '-p', pid).splitlines()[1:]:
        match = re.search(r'\s(\d+\.\d+)\s+[A-Z]', line)
        if match:
            percents.append(float(match.group(1)))
    return sorted(percents, reverse=True)


def gpu():
    text = run('ioreg', '-r', '-d', '1', '-w', '0', '-c', 'IOAccelerator')
    values = []
    for key in ('Device Utilization %', 'Renderer Utilization %', 'Tiler Utilization %'):
        match = re.search(rf'"{key}"\s*=\s*(\d+)', text)
        values.append(match.group(1) if match else '')
    return values


def monitor(path):
    seen_game = False
    previous, previous_time = {}, time.time()
    with open(path, 'w') as out:
        print('\t'.join(COLUMNS), file=out, flush=True)
        while True:
            time.sleep(INTERVAL)
            now = time.time()
            processes = wine_processes()
            game = [pid for pid, process in processes.items() if process[0] == 'game']
            if not processes or (seen_game and not game):
                return
            seen_game = seen_game or bool(game)
            if game:
                used = {'game': 0.0, 'wineserver': 0.0, 'other': 0.0}
                for pid, (name, cpu, _) in processes.items():
                    # A process that started since the last sample counts from zero.
                    used[name] += max(0.0, cpu - previous.get(pid, (name, cpu, 0))[1])
                elapsed = now - previous_time
                threads = top_threads(game[0])
                row = [f'{now:.1f}'] + [f'{used[name] / elapsed * 100:.1f}' for name in used]
                row += [str(len(threads)), ','.join(f'{value:.0f}' for value in threads[:6])]
                row += gpu() + [str(processes[game[0]][2])]
                print('\t'.join(row), file=out, flush=True)
            previous, previous_time = processes, now


def fps_samples(path):
    """Returns the frame rates of the process that presented the most frames."""
    by_process = {}
    for line in open(path, errors='replace'):
        match = FPS_LINE.match(line)
        if match:
            by_process.setdefault(match.group(2), []).append(float(match.group(3)))
    return max(by_process.values(), key=len) if by_process else []


def percentile(values, share):
    return sorted(values)[int(len(values) * share)]


def describe(values):
    return f'median {statistics.median(values):.0f}, low 10% {percentile(values, 0.1):.0f}, high {max(values):.0f}'


def report(session, log):
    fps = fps_samples(log)
    playing = [value for value in fps if value >= LOADING_FPS]
    if playing:
        print(f'Frame rate over {len(playing)} samples: {describe(playing)}')
        print(f'Below 100 FPS: {sum(value < 100 for value in playing) * 100 // len(playing)}% of samples')
    else:
        print('No frame rate lines. Was the game started with bin/tm2020 diag?')

    lines = [line.rstrip('\n').split('\t') for line in open(session)]
    rows = [dict(zip(lines[0], line)) for line in lines[1:]]
    # The first samples are the game starting up, which is mostly loading.
    rows = rows[15:] or rows
    if not rows:
        print('No CPU samples. The game did not run long enough.')
        return
    print(f'CPU and GPU over {len(rows) * INTERVAL / 60:.1f} minutes, in % of one core or % of the GPU:')
    for key, label in (('game_cpu', 'game, all threads'), ('wineserver_cpu', 'wineserver'),
                       ('other_wine_cpu', 'Connect and other Wine processes'), ('gpu_device', 'GPU, device'),
                       ('gpu_renderer', 'GPU, renderer'), ('gpu_tiler', 'GPU, tiler')):
        values = [float(row[key]) for row in rows if row.get(key)]
        if values:
            print(f'  {label:34} {describe(values)}')
    for rank in range(3):
        values = [float(row['top_threads'].split(',')[rank]) for row in rows
                  if row['top_threads'].count(',') >= rank]
        if values:
            print(f'  {"busiest game thread #" + str(rank + 1):34} {describe(values)}')
    print(f'  Game threads: {rows[-1]["threads"]}, memory: {rows[-1]["memory_mb"]} MB')


if __name__ == '__main__':
    if sys.argv[1:2] == ['monitor'] and len(sys.argv) == 3:
        monitor(Path(sys.argv[2]))
    elif sys.argv[1:2] == ['report'] and len(sys.argv) == 4:
        report(Path(sys.argv[2]), Path(sys.argv[3]))
    else:
        sys.exit(__doc__)
