"""Summarise the [drawlog] frames of a native renderer test.log.

usage: python -I tools/nr_drawlog_summary.py <test.log> [--frame N]

For each logged frame (ufc3_native_draw_log_at / F9): draws grouped by (call, primitive, vertex shader,
pixel shader, vertex layout) with a count, the first draw's number, its render target and texture 0,
plus the frame's clears and resolves. Shader hash 0000000000000000 = no host shader for it.
"""
import collections
import re
import sys

DRAW = re.compile(r'#(\d+) (\w+) prim (\d+) (\w*)\s*count (\d+) start (\d+) base (-?\d+) \| rt (.*?) ds (.*?) '
                  r'\| vp (.*?) sc (\S+) \| vs (\w+) ps (\w+) \|(.*)')


def main(argv):
    only = None
    pos = []
    i = 0
    while i < len(argv):
        if argv[i] == '--frame':
            only = argv[i + 1]; i += 2
        else:
            pos.append(argv[i]); i += 1
    if len(pos) != 1:
        print(__doc__)
        return 2
    frames, cur = [], None
    for line in open(pos[0], encoding='utf-8', errors='replace'):
        k = line.find('[drawlog] ')
        if k < 0:
            continue
        text = line[k + 10:].rstrip()
        m = re.match(r'logging frame (\d+)(.*)', text)
        if m:
            cur = {'frame': m.group(1), 'note': m.group(2), 'lines': []}
            frames.append(cur)
        elif cur is not None:
            cur['lines'].append(text)
    for fr in frames:
        if only and fr['frame'] != only:
            continue
        groups, first, decl = collections.Counter(), {}, ''
        other = collections.Counter()
        n = 0
        for text in fr['lines']:
            s = text.strip()
            if s.startswith('decl'):
                decl = s
                continue
            m = DRAW.match(s)
            if m:
                n += 1
                t0 = re.search(r't0=\S+\[[^\]]*\]', m.group(14))
                key = (m.group(2), 'prim ' + m.group(3), m.group(4), 'vs ' + m.group(12), 'ps ' + m.group(13), decl)
                groups[key] += 1
                first.setdefault(key, '#%s count %s rt %s vp %s %s' % (m.group(1), m.group(5), m.group(8), m.group(10),
                                                                    t0.group(0) if t0 else ''))
            elif s.startswith('clear') or s.startswith('resolve'):
                other[re.sub(r'0x[0-9A-Fa-f]{8}', lambda h: h.group(0), s)] += 1
        print('== frame %s%s: %d draws' % (fr['frame'], fr['note'], n))
        for key, count in groups.most_common():
            print('%4d  %s' % (count, ' | '.join(k for k in key if k)))
            print('        first: ' + first[key])
        for s, count in other.most_common():
            print('%4d  %s' % (count, s))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
