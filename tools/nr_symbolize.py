"""Name the ufc3.exe+0x... frames of a native renderer trace (native_crash_trace.txt, native_hang_trace.txt).

usage: python -I tools/nr_symbolize.py <trace file> <build tag or folder with ufc3.exe + ufc3.pdb> [--symbolizer PATH]
       python -I tools/nr_symbolize.py <run>/test.log <build tag> --samples [--min-fps 40] [--top 40]

<build tag> = TestFarm\\builds\\<tag>\\pdb (tools/nr_farm_job.py puts the exe and its PDB there when it freezes a build).
Prints the trace with each ufc3.exe frame followed by function and source line (llvm-symbolizer reads the PDB).
Recompiled guest code shows as sub_XXXXXXXX (the guest address of the function); where a hook replaces
a function, __imp__sub_XXXXXXXX is the game's own body and sub_XXXXXXXX is the hook.

--samples: the stack sampler's rows ([perf] leaf / incl, ufc3_perf_probe with ufc3_sample_tid >= 0). Adds
up the 5 s windows that lie wholly in the fight (at least 3 [nperf] lines, all >= --min-fps; needs
ufc3_native_perf in the run), by function, and sorts leaf samples into groups. Percentages are of wall
time on the sampled thread, waits included. incl only sees the top 12 frames of a stack.
"""
import os
import re
import subprocess
import sys
from collections import Counter

FRAME = re.compile(r'ufc3\.exe\+0x([0-9A-Fa-f]+)')
WINDOW = re.compile(r'\[perf\] ---- (\d+) samples')
ROW = re.compile(r'\[perf\] (leaf|incl)\s+([\d.]+)% (\S.*)$')
NPERF = re.compile(r'\[nperf\] fps ([\d.]+)')
GUEST = re.compile(r'(?:__imp__)?sub_([0-9A-Fa-f]{8})')
# The XDK D3D library in this image, from FINDINGS-nr1-d3d-map.md (the first and last functions named there,
# rounded out; approximate).
D3D_FIRST, D3D_LAST = 0x82368000, 0x82398000


def symbolize(sym, exe, offsets):
    """{hex offset: [function, file:line]}. Addresses go on stdin: a long log overflows a command line."""
    out = subprocess.run([sym, '--obj=' + exe, '--relative-address', '--functions=linkage', '--no-inlines',
                          '--output-style=LLVM'], input='\n'.join('0x' + o for o in offsets) + '\n',
                         capture_output=True, text=True).stdout
    names = {}
    for o, b in zip(offsets, out.strip().split('\n\n')):
        names[o] = [p.strip() for p in b.strip().splitlines()[:2]]
    return names


def group(name):
    """Which kind of code one leaf function is."""
    m = GUEST.match(name)
    if m:
        return 'guest D3D library' if D3D_FIRST <= int(m.group(1), 16) < D3D_LAST else 'guest, other'
    m = re.match(r'(\S+?\.(?:dll|exe))\+0x', name, re.I)
    if m and m.group(1).lower() != 'ufc3.exe':
        return m.group(1)
    return 'host code in ufc3.exe'


def report_samples(lines, names, min_fps, top):
    tally = {'leaf': Counter(), 'incl': Counter()}
    fps, samples, used, skipped = [], 0, 0, 0
    active = 0
    for line in lines:
        m = NPERF.search(line)
        if m:
            fps.append(float(m.group(1)))
            continue
        m = WINDOW.search(line)
        if m:
            active = int(m.group(1)) if len(fps) >= 3 and min(fps) >= min_fps else 0
            samples += active
            used += 1 if active else 0
            skipped += 0 if active else 1
            fps = []
            continue
        m = ROW.search(line)
        if m and active:
            frame = FRAME.search(m.group(3))
            name = names.get(frame.group(1), [m.group(3)])[0] if frame else m.group(3)
            tally[m.group(1)][name] += float(m.group(2)) * active / 100.0
    print('%d fight windows of 5 s (%d others left out), %d samples' % (used, skipped, samples))
    if not samples:
        return 1
    groups = Counter()
    for name, n in tally['leaf'].items():
        groups[group(name)] += n
    print('\nleaf by group (only the rows the game printed: each window lists its top 60)')
    for name, n in groups.most_common():
        print('%6.1f%%  %s' % (100.0 * n / samples, name))
    for kind in ('leaf', 'incl'):
        print('\n%s, top %d' % (kind, top))
        for name, n in tally[kind].most_common(top):
            print('%6.1f%%  %s' % (100.0 * n / samples, name))
    return 0


def main(argv):
    sym = r'C:\Program Files\LLVM\bin\llvm-symbolizer.exe'
    pos = []
    samples, min_fps, top = False, 40.0, 40
    i = 0
    while i < len(argv):
        if argv[i] == '--symbolizer':
            sym = argv[i + 1]; i += 2
        elif argv[i] == '--samples':
            samples = True; i += 1
        elif argv[i] == '--min-fps':
            min_fps = float(argv[i + 1]); i += 2
        elif argv[i] == '--top':
            top = int(argv[i + 1]); i += 2
        else:
            pos.append(argv[i]); i += 1
    if len(pos) != 2:
        print(__doc__)
        return 2
    trace, where = pos
    if not os.path.isdir(where):
        where = os.path.join(r'E:\ClaudeGame\TestFarm\builds', where, 'pdb')
    exe = os.path.join(where, 'ufc3.exe')
    if not os.path.isfile(exe) or not os.path.isfile(os.path.join(where, 'ufc3.pdb')):
        print('need ufc3.exe and ufc3.pdb in', where)
        return 1
    lines = open(trace, encoding='utf-8', errors='replace').read().splitlines()
    offsets = sorted({m.group(1) for l in lines for m in FRAME.finditer(l)}, key=lambda h: int(h, 16))
    names = symbolize(sym, exe, offsets) if offsets else {}
    if samples:
        return report_samples(lines, names, min_fps, top)
    for l in lines:
        m = FRAME.search(l)
        print(l + ('    ' + ' '.join(names.get(m.group(1), ['?'])) if m else ''))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
