"""NR-6: line up the D3D census (--ufc3_d3d_census) with the emulated GPU's own draw count.

Usage: python -I tools/nr_census_report.py <run or test.log> [...]

Reads the [census] reports (src/dev/ufc_d3d_census.cpp) and the trace_draw_ps capture ends
("Draw trace c.f (frame F): end, X of Y draws logged": Y = every draw the emulated GPU issued that frame).
Run the game with --set ufc3_d3d_census=true --set ufc3_d3d_census_every=150 --set trace_draw_ps=0
--set trace_draw_frames=1 --set trace_draw_every=150 --set trace_draw_captures=400.

Prints one row per census report: log time, census gpu-like draws/frame (+ the extra draws inside
Resolve's canned PM4 when the log has them) (avg, min, max, last), direct,
replayed (command buffers), clears, command-buffer share, and the emulated GPU's draw counts from the
trace captures that ended within that report's window (mean, and the census/GPU ratio). Ends with the
whole-run totals and the largest command-buffer share seen (Stop 1: about 5%).
"""
import os
import re
import sys
from datetime import datetime

TS = re.compile(r'^\[(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d{3})\]')
REPORT = re.compile(r'\[census\] frame (\d+) \((\d+) frames\): gpu-like ([\d.]+)/frame \(min (\d+) max (\d+) '
                    r'last (\d+)\) = direct ([\d.]+) (?:\+ internal ([\d.]+) )?\+ replayed ([\d.]+) \+ clears ([\d.]+)(?: \+ gpr flush ([\d.]+))?; recorded ([\d.]+); '
                    r'runs (\d+) .*command-buffer share ([\d.]+)%')
ZPASS = re.compile(r'\[census\] frame (\d+): zpass (\d+) calls, ([\d.]+) draws/frame .*tiling (\d+) calls, (\d+) tiles, '
                   r'([\d.]+) draws/frame; queries issue (\d+) \(occlusion (\d+), other (\d+)\) getdata (\d+); viz (.*)$')
CHECK = re.compile(r'\[census\] frame (\d+): device check (.*)$')
RESOLVE = re.compile(r'\[census\] frame (\d+): resolves .*gpu-like \+ extra ([\d.]+)/frame')
TRACE = re.compile(r'Draw trace \d+\.\d+ \(frame (\d+)\): end, \d+ of (\d+) draws logged')


def when(line):
    m = TS.match(line)
    return datetime.strptime(m.group(1), '%Y-%m-%d %H:%M:%S.%f') if m else None


def report(path):
    log = os.path.join(path, 'test.log') if os.path.isdir(path) else path
    reports, traces, extra, start, fixed = [], [], {}, None, {}
    with open(log, encoding='utf-8', errors='replace') as f:
        for line in f:
            t = when(line)
            if t and start is None:
                start = t
            if '[census]' in line:
                m = REPORT.search(line)
                if m:
                    reports.append((t, m))
                    continue
                r = RESOLVE.search(line)
                if r:
                    fixed[r.group(1)] = float(r.group(2))
                m = ZPASS.search(line) or CHECK.search(line) or r
                if m:
                    extra.setdefault(m.group(1), []).append(line.split('[census] ', 1)[1].strip())
                elif 'device' in line:
                    print('  ' + line.split('[census] ', 1)[1].strip())
            elif 'Draw trace' in line:
                m = TRACE.search(line)
                if m:
                    traces.append((t, int(m.group(2))))
    print('== %s: %d census reports, %d GPU trace frames' % (log, len(reports), len(traces)))
    print('  %7s %7s %8s %6s %6s %6s %8s %8s %8s %6s %6s %7s | %8s %3s %6s' % (
        'secs', 'frame', 'gpu-like', 'min', 'max', 'last', 'direct', 'internal', 'replayed', 'clears', 'gpr', 'cb%',
        'gpu avg', 'n', 'ratio'))
    prev, max_share, tot_census, tot_gpu = None, 0.0, [], []
    for t, m in reports:
        window = [y for (tt, y) in traces if tt and prev is not None and prev < tt <= t]
        gpu = sum(window) / len(window) if window else 0.0
        like = fixed.get(m.group(1), float(m.group(3)))
        share = float(m.group(14))
        max_share = max(max_share, share)
        if window:
            tot_census.append(like)
            tot_gpu.append(gpu)
        print('  %7.1f %7s %8.1f %6s %6s %6s %8s %8s %8s %6s %6s %7.2f | %8.1f %3d %6s' % (
            (t - start).total_seconds() if t and start else 0, m.group(1), like, m.group(4), m.group(5),
            m.group(6), m.group(7), m.group(8) or '-', m.group(9), m.group(10), m.group(11) or '-', share, gpu, len(window),
            '%.3f' % (like / gpu) if gpu else '-'))
        for e in extra.get(m.group(1), []):
            print('          ' + e)
        prev = t
    if tot_gpu:
        c, g = sum(tot_census) / len(tot_census), sum(tot_gpu) / len(tot_gpu)
        print('  windows with GPU traces: census %.1f vs GPU %.1f draws/frame (ratio %.3f)' % (c, g, c / g))
    print('  largest command-buffer share: %.2f%% (Stop 1 if over ~5%%)' % max_share)


if __name__ == '__main__':
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(2)
    for p in sys.argv[1:]:
        report(p)
