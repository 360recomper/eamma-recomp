"""One line per draw of a logged native frame, with the state that decides what ends up on screen.

usage: python -I tools/nr_drawlog_frame.py <test.log> <frame number> [first draw] [last draw]

Columns: draw number, call, primitive, count, render target, depth target, viewport, scissor, shader pair,
blend (host), depth control register, colour mask, texture 0. Clears and resolves are printed in order.
Draw numbers are the log's own (they keep counting across logged frames).
"""
import re
import sys

DRAW = re.compile(r'#(\d+) (\S+) prim (\d+) (\w*)\s*count (\d+) start (\d+) base (-?\d+) \| rt (.*?) ds (.*?) '
                  r'\| vp (.*?) sc (\S+) \| vs (\w+) ps (\w+) \|(.*)')


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    lo = int(argv[2]) if len(argv) > 2 else 0
    hi = int(argv[3]) if len(argv) > 3 else 1 << 30
    on = False
    state1 = state2 = ''
    for line in open(argv[0], encoding='utf-8', errors='replace'):
        k = line.find('[drawlog] ')
        if k < 0:
            continue
        text = line[k + 10:].rstrip()
        m = re.match(r'logging frame (\d+)', text)
        if m:
            on = m.group(1) == argv[1]
            continue
        if not on:
            continue
        s = text.strip()
        if s.startswith('blendctl'):
            state1 = s
        elif s.startswith('vte'):
            state2 = s
        elif s.startswith('clear') or s.startswith('resolve') or s.startswith('^'):
            print('      ' + s)
        else:
            m = DRAW.match(s)
            if not m:
                continue
            n = int(m.group(1))
            if n < lo or n > hi:
                continue
            blend = re.search(r'host blend (\w+ \d+/\d+)', state1)
            ztest = re.search(r'zfunc (\d+) zwrite (\w+)', state1)
            depthctl = re.search(r'depthctl (0x\w+) colormask (0x\w+)', state2)
            t0 = re.search(r't0=\S+\[[^\]]*\]', m.group(14))
            print('#%-4d %-24s p%-2s n%-5s rt %-26s ds %-26s vp %-22s sc %-22s %s/%s blend %-9s z %s %s %s' % (
                n, m.group(2), m.group(3), m.group(5), m.group(8), m.group(9), m.group(10).split(' z ')[0],
                m.group(11), m.group(12)[:8], m.group(13)[:8], blend.group(1) if blend else '?',
                ztest.group(0) if ztest else '?', depthctl.group(0) if depthctl else '?', t0.group(0) if t0 else ''))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
