"""Summarize the [nperf] lines of a native run (cvar ufc3_native_perf, NR-P1).

usage: python -I tools/nr_perf_report.py <run dir or test.log> [--min-draws 1100] [--min-fps 0]

Averages the one-a-second [nperf] lines whose scene has at least --min-draws draws a frame (the
fight; --min-fps 40 drops the 30 fps walkout and ring intro) and prints the zones ranked, their sum against the frame time, and the counters.
"""
import os
import re
import sys


def parse(line):
    body = line.split('[nperf]', 1)[1]
    parts = [p.split() for p in body.split('|')]
    out = []
    for words in parts:
        out.append([(words[i], float(words[i + 1])) for i in range(0, len(words) - 1, 2)])
    return out


def main(argv):
    pos, min_draws, min_fps = [], 1100.0, 0.0
    i = 0
    while i < len(argv):
        if argv[i] == '--min-draws':
            min_draws = float(argv[i + 1]); i += 2
        elif argv[i] == '--min-fps':
            min_fps = float(argv[i + 1]); i += 2
        else:
            pos.append(argv[i]); i += 1
    if len(pos) != 1:
        print(__doc__)
        return 2
    path = pos[0]
    if os.path.isdir(path):
        path = os.path.join(path, 'test.log')
    rows = []
    with open(path, 'r', encoding='utf-8', errors='replace') as f:
        for line in f:
            if '[nperf]' in line:
                head, zones, counts = parse(line)
                if dict(head)['draws'] >= min_draws and dict(head)['fps'] >= min_fps:
                    rows.append((head, zones, counts))
    if not rows:
        print('no [nperf] line with draws >=', min_draws)
        return 1

    def mean(section, name):
        return sum(dict(r[section])[name] for r in rows) / len(rows)

    print('%d windows (1 s each) with draws >= %.0f' % (len(rows), min_draws))
    for name, _ in rows[0][0]:
        values = [dict(r[0])[name] for r in rows]
        print('  %-9s mean %8.2f  min %8.2f  max %8.2f' % (name, sum(values) / len(values), min(values), max(values)))
    zones = sorted(((mean(1, name), name) for name, _ in rows[0][1]), reverse=True)
    total = sum(v for v, _ in zones)
    print('zones, ms per frame (sum %.2f against avg %.2f):' % (total, mean(0, 'avg')))
    for value, name in zones:
        print('  %-7s %6.2f  %5.1f%%' % (name, value, 100.0 * value / total if total else 0.0))
    print('counters per frame:')
    for name, _ in rows[0][2]:
        print('  %-9s %10.1f' % (name, mean(2, name)))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
