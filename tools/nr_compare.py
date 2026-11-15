"""Native renderer A/B: pair two self-test runs' captures and show where they differ.

Usage: python -I tools/nr_compare.py <runA> <runB> [--out <dir>] [--tol 1.0] [--by-index] [--threshold 6.0]

<runA>/<runB> are ufc_selftest.py run folders (or TestFarm\\results\\<run>), each with shots\\NNN_TTTT.TTs.png.
Usually A = --set ufc3_renderer=emulated, B = --set ufc3_renderer=native, same build, same script.

Pairing: by capture time (seconds after launch, nearest within --tol), or with --by-index by capture number
(use that for --on-log captures, whose times drift between runs).

Writes to --out (default <runB>/compare_<runA name>):
  compare_sheet.jpg  one row per pair: A | B | diff heat map (labelled, mean diff in the label)
  diff_<label>.png   full-res heat map per pair (black = same, red/yellow = differs)
  compare.txt        one line per pair: label, mean abs diff (0-255), % of pixels off by > 24, FLAG if over --threshold
Exit 0 = no pair over --threshold, 1 = some pair over it, 2 = nothing to pair.

A low number alone is not a pass (FINDINGS-native-renderer.md "Proof method"): look at every pair at full res.
"""
import os
import re
import sys

from PIL import Image, ImageDraw
import numpy as np

SHOT = re.compile(r'^(\d{3})_(\d+\.\d+)s\.png$')


def shots(run):
    folder = os.path.join(run, 'shots')
    out = []
    for f in sorted(os.listdir(folder)) if os.path.isdir(folder) else []:
        m = SHOT.match(f)
        if m:
            out.append((int(m.group(1)), float(m.group(2)), os.path.join(folder, f)))
    return out


def pair(a, b, tol, by_index):
    if by_index:
        bi = {n: (n, t, p) for n, t, p in b}
        return [(x, bi[x[0]]) for x in a if x[0] in bi]
    pairs, used = [], set()
    for x in a:
        best = None
        for y in b:
            if y[0] in used:
                continue
            d = abs(y[1] - x[1])
            if d <= tol and (best is None or d < abs(best[1] - x[1])):
                best = y
        if best:
            used.add(best[0])
            pairs.append((x, best))
    return pairs


def heat(diff):
    """Per-pixel max channel diff -> black/red/yellow heat map."""
    d = diff.max(axis=2).astype(np.float32)
    r = np.clip(d * 4, 0, 255)
    g = np.clip((d - 64) * 2, 0, 255)
    return Image.fromarray(np.stack([r, g, np.zeros_like(r)], axis=2).astype(np.uint8))


def main(argv):
    opt = {'--out': '', '--tol': '1.0', '--threshold': '6.0'}
    by_index, pos, i = False, [], 0
    while i < len(argv):
        if argv[i] == '--by-index':
            by_index = True
        elif argv[i] in opt and i + 1 < len(argv):
            opt[argv[i]] = argv[i + 1]
            i += 1
        else:
            pos.append(argv[i])
        i += 1
    if len(pos) != 2:
        print(__doc__)
        return 2
    run_a, run_b = pos
    out = opt['--out'] or os.path.join(run_b, 'compare_' + os.path.basename(os.path.normpath(run_a)))
    os.makedirs(out, exist_ok=True)
    pairs = pair(shots(run_a), shots(run_b), float(opt['--tol']), by_index)
    if not pairs:
        print('nothing to pair (shots folders empty or no times within --tol)')
        return 2

    threshold, flagged, rows, lines = float(opt['--threshold']), 0, [], []
    for (na, ta, pa), (nb, tb, pb) in pairs:
        ia, ib = Image.open(pa).convert('RGB'), Image.open(pb).convert('RGB')
        if ib.size != ia.size:
            ib = ib.resize(ia.size, Image.BILINEAR)
        diff = np.abs(np.asarray(ia, np.int16) - np.asarray(ib, np.int16)).astype(np.uint8)
        mean = float(diff.mean())
        off = float((diff.max(axis=2) > 24).mean() * 100)
        label = '%03d_%.1fs' % (na, ta)
        h = heat(diff)
        h.save(os.path.join(out, 'diff_%s.png' % label))
        flag = mean > threshold
        flagged += flag
        lines.append('%-14s A %s  B %s  mean %6.2f  off>24 %6.2f%%%s' % (
            label, os.path.basename(pa), os.path.basename(pb), mean, off, '  FLAG' if flag else ''))
        rows.append((label, mean, off, flag, ia, ib, h))

    w = 480
    hgt = int(w * rows[0][4].size[1] / rows[0][4].size[0])
    sheet = Image.new('RGB', (w * 3, hgt * len(rows)), (0, 0, 0))
    draw = ImageDraw.Draw(sheet)
    for r, (label, mean, off, flag, ia, ib, h) in enumerate(rows):
        for c, im in enumerate((ia, ib, h)):
            sheet.paste(im.resize((w, hgt)), (c * w, r * hgt))
        colour = (255, 64, 64) if flag else (255, 255, 0)
        draw.text((4, r * hgt + 4), '%s A' % label, fill=colour)
        draw.text((w + 4, r * hgt + 4), '%s B' % label, fill=colour)
        draw.text((2 * w + 4, r * hgt + 4), 'mean %.2f  off %.2f%%' % (mean, off), fill=colour)
    sheet.save(os.path.join(out, 'compare_sheet.jpg'), quality=85)
    with open(os.path.join(out, 'compare.txt'), 'w', encoding='utf-8') as f:
        f.write('A = %s\nB = %s\n' % (run_a, run_b))
        f.write('\n'.join(lines) + '\n')
    print('\n'.join(lines))
    print('%d pairs, %d over threshold %.1f -> %s' % (len(rows), flagged, threshold, out))
    return 1 if flagged else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
