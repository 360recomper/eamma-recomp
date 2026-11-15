"""WP3 coverage ledger: merge [cover] lines of runs made with --eamma_native_coverage=true.

usage: python -I tools/nr_coverage.py <test.log | run dir> [...] [--inventory config/native/shader_inventory.txt]
       python -I tools/nr_coverage.py --make-inventory <hlsl dir> [--inventory <file>]

- Reports "N of M pixel shaders drawn" (M from the inventory, else 240 = FINDINGS-en1-shaders.md), the pixel
  shaders never drawn, every target size/format, resolve signature, and the alpha-to-mask / polygon-offset pairs.
- --make-inventory: <hlsl dir> is XenosRecomp's HLSL output (<HASH>.hlsl, NATIVE-HANDOFF.md "Diagnostics");
  writes one "PS <hash>" / "VS <hash>" line per shader.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
INVENTORY = os.path.join(ROOT, 'config', 'native', 'shader_inventory.txt')
PS_TOTAL = 240


def make_inventory(hlsl_dir, out):
    rows = []
    for name in sorted(os.listdir(hlsl_dir)):
        stem, ext = os.path.splitext(name)
        if ext.lower() != '.hlsl' or not re.fullmatch(r'[0-9A-Fa-f]{16}', stem):
            continue
        with open(os.path.join(hlsl_dir, name), encoding='utf-8', errors='replace') as f:
            text = f.read()
        rows.append(('PS' if 'SV_Target' in text else 'VS') + ' ' + stem.upper())
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, 'w', newline='\n') as f:
        f.write('\n'.join(rows) + '\n')
    print('wrote %s: %d PS, %d VS' % (out, sum(r[0] == 'P' for r in rows), sum(r[0] == 'V' for r in rows)))
    return 0


def main(argv):
    inventory, logs, hlsl = INVENTORY, [], None
    i = 0
    while i < len(argv):
        if argv[i] == '--inventory':
            inventory = argv[i + 1]; i += 2
        elif argv[i] == '--make-inventory':
            hlsl = argv[i + 1]; i += 2
        else:
            logs.append(argv[i]); i += 1
    if hlsl:
        return make_inventory(hlsl, inventory)
    if not logs:
        print(__doc__)
        return 2
    pairs, other = {}, {}
    for path in logs:
        if os.path.isdir(path):
            path = os.path.join(path, 'test.log')
        run = os.path.basename(os.path.dirname(os.path.abspath(path)))
        with open(path, encoding='utf-8', errors='replace') as f:
            for line in f:
                at = line.find('[cover] ')
                if at < 0:
                    continue
                text = re.sub(r' frame \d+\s*$', '', line[at + 8:].strip())
                m = re.match(r'shaders VS ([0-9A-F]{16}) PS ([0-9A-F]{16})', text)
                if m:
                    pairs.setdefault((m.group(1), m.group(2)), run)
                else:
                    other.setdefault(text, run)
    drawn_ps = {ps for _, ps in pairs}
    all_ps = set()
    if os.path.isfile(inventory):
        with open(inventory) as f:
            all_ps = {l.split()[1] for l in f if l.startswith('PS ')}
    total = len(all_ps) or PS_TOTAL
    print('%d of %d pixel shaders drawn (%d shader pairs, %d runs)' % (len(drawn_ps), total, len(pairs), len(logs)))
    for kind in ('target', 'resolve', 'alpha-to-mask', 'polygon-offset'):
        rows = sorted(t for t in other if t.startswith(kind))
        print('\n%s: %d' % (kind, len(rows)))
        for t in rows:
            print('  %s  (%s)' % (t, other[t]))
    if all_ps:
        missing = sorted(all_ps - drawn_ps)
        print('\nnever drawn: %d' % len(missing))
        for h in missing:
            print('  ' + h)
        unknown = sorted(drawn_ps - all_ps)
        if unknown:
            print('\ndrawn but not in the inventory: ' + ' '.join(unknown))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
