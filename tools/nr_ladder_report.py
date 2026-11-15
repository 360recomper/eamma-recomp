"""NR-12: one row per run and scene from the [nperf] lines (cvar ufc3_native_perf) and their NVML columns.

usage: python -I tools/nr_ladder_report.py <run dir> [<run dir> ...] [--min-draws 1100]

Scenes, from each one-a-second line: `fight` = at least --min-draws draws a frame and 40+ fps, `cut` = walkout / ring
intro / replay (400+ draws, under 40 fps: the game runs those at 30). Per scene: seconds, fps mean, p95 mean, max of
the per-second max, `gpu` (timestamp ms a frame), `work` (frame thread ms a frame minus the 60 fps sleep `pace` and the Swap fence sleep `gsleep`),
NVML busy %, graphics clock MHz and its maximum, busy scaled to the max clock (`load` = busy * clk / maxclk, the
share of the card at full clock), VRAM MB (peak) and watts. Emulated runs have no [nperf] lines.
"""
import os
import re
import sys


def parse(line):
    body = line.split('[nperf]', 1)[1]
    vals = {}
    for part in body.split('|'):
        words = part.split()
        for i in range(0, len(words) - 1, 2):
            try:
                vals[words[i]] = float(words[i + 1])
            except ValueError:
                pass
    return vals


def main(argv):
    runs, min_draws = [], 1100.0
    i = 0
    while i < len(argv):
        if argv[i] == '--min-draws':
            min_draws = float(argv[i + 1]); i += 2
        else:
            runs.append(argv[i]); i += 1
    if not runs:
        print(__doc__)
        return 2
    print('%-34s %-5s %4s %5s %5s %5s %5s %5s %4s %5s %5s %5s %6s %5s' % (
        'run', 'scene', 's', 'fps', 'p95', 'max', 'gpu', 'work', 'busy', 'clk', 'maxc', 'load', 'vramMB', 'W'))
    for run in runs:
        path = os.path.join(run, 'test.log') if os.path.isdir(run) else run
        scenes = {'fight': [], 'cut': []}
        with open(path, 'r', encoding='utf-8', errors='replace') as f:
            for line in f:
                if '[nperf] fps' not in line:
                    continue
                v = parse(line)
                if v.get('draws', 0) >= min_draws and v.get('fps', 0) >= 40:
                    scenes['fight'].append(v)
                elif v.get('fps', 0) < 40:
                    scenes['cut'].append(v)
        name = os.path.basename(os.path.normpath(run))
        for scene, rows in scenes.items():
            if not rows:
                continue
            n = len(rows)
            mean = lambda k: sum(r.get(k, 0.0) for r in rows) / n
            has_nvml = all('busy' in r for r in rows)
            load = sum(r['busy'] * r['clk'] / r['maxclk'] for r in rows if r.get('maxclk')) / n if has_nvml else 0
            print('%-34s %-5s %4d %5.1f %5.1f %5.1f %5.2f %5.2f %4.0f %5.0f %5.0f %5.1f %6.0f %5.1f' % (
                name[:34], scene, n, mean('fps'), mean('p95'), max(r.get('max', 0) for r in rows), mean('gpu'),
                mean('avg') - mean('pace') - mean('gsleep'), mean('busy'), mean('clk'), mean('maxclk'), load,
                max(r.get('vramMB', 0) for r in rows), mean('W')))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
