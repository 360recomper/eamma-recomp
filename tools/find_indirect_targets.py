#!/usr/bin/env python3
"""Static scan for indirect-call targets missing from the generated function table.

Method follows E:/ClaudeGame/RumbleRosesRecomp/reports/indirect-targets.md:
candidates are code addresses referenced from data words or lis/addi(ori) pairs
that sit right after an unconditional terminator, decode as PPC, and are not
labels inside the function that currently owns them.

    python -I tools/find_indirect_targets.py assets/default.xex generated/default/ufc3_init.cpp [--hide ADDR ...]
"""
import argparse
import bisect
import os
import re
import struct
import sys

import capstone

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from xex_image import load, sections  # noqa: E402

BLR, BCTR = 0x4E800020, 0x4E800420


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("xex")
    ap.add_argument("init_cpp")
    ap.add_argument("--hide", nargs="*", default=[], help="pretend these are unregistered (sanity check)")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    base, img = load(args.xex)
    secs = sections(img, base)
    code = [(s, e) for n, s, e, f in secs if f & 0x20000000]
    data = [(n, s, e) for n, s, e, f in secs if not f & 0x20000000 and n not in (".pdata", ".reloc", ".idata")]

    def in_code(a):
        return any(s <= a < e for s, e in code) and a % 4 == 0

    def word(a):
        return struct.unpack_from(">I", img, a - base)[0]

    hidden = {int(h, 16) for h in args.hide}
    starts = sorted({int(m, 16) for m in re.findall(r"\{\s*0x([0-9A-Fa-f]{8}),\s*\w+\s*\}",
                                                    open(args.init_cpp).read())} - hidden)
    start_set = set(starts)
    print(f"{len(starts)} registered functions", file=sys.stderr)

    def owner(a):
        i = bisect.bisect_right(starts, a) - 1
        return starts[i] if i >= 0 else None

    def next_start(a):
        i = bisect.bisect_right(starts, a)
        return starts[i] if i < len(starts) else max(e for _, e in code)

    def is_terminator(w):
        op = w >> 26
        return w in (BLR, BCTR, 0) or (op == 18 and (w & 3) == 0)

    def branch_target(a, w):
        op = w >> 26
        if op == 18:
            li = w & 0x03FFFFFC
            li -= (li & 0x02000000) << 1
            return li if w & 2 else a + li, bool(w & 1)
        if op == 16:
            bd = w & 0xFFFC
            bd -= (bd & 0x8000) << 1
            return bd if w & 2 else a + bd, bool(w & 1)
        return None, False

    cs = capstone.Cs(capstone.CS_ARCH_PPC, capstone.CS_MODE_32 | capstone.CS_MODE_BIG_ENDIAN)

    def decodes(a):
        for i in range(4):
            w = word(a + 4 * i)
            if w == 0:
                return i > 0  # leaf followed by padding
            if (w >> 26) not in (4, 5, 6) and not list(cs.disasm(struct.pack(">I", w), a + 4 * i)):
                return False
            if is_terminator(w):
                return True
        return True

    # --- collect references ------------------------------------------------
    refs = {}  # target -> list of reference descriptions

    for name, s, e in data:
        for a in range(s - s % 4, e - 3, 4):
            t = word(a)
            if in_code(t) and t not in start_set:
                refs.setdefault(t, []).append(("data", a))

    for s, e in code:
        for a in range(s, e - 3, 4):
            w = word(a)
            if (w >> 26) == 15 and ((w >> 16) & 31) == 0:  # lis rX,hi
                rx, hi = (w >> 21) & 31, (w & 0xFFFF) << 16
                for j in range(1, 25):
                    b = a + 4 * j
                    if b >= e:
                        break
                    w2 = word(b)
                    op = w2 >> 26
                    if op == 14 and ((w2 >> 16) & 31) == rx:  # addi rY,rX,lo
                        lo = w2 & 0xFFFF
                        t = (hi + lo - ((lo & 0x8000) << 1)) & 0xFFFFFFFF
                    elif op == 24 and ((w2 >> 21) & 31) == rx:  # ori rY,rX,lo
                        t = hi | (w2 & 0xFFFF)
                    else:
                        continue
                    if in_code(t) and t not in start_set:
                        # jump tables: base address followed by an indexed load
                        tail = [word(b + 4 * k) for k in range(1, 6) if b + 4 * k < e]
                        if any((x >> 26) == 31 and ((x >> 1) & 0x3FF) in (23, 87, 279) for x in tail):
                            break
                        refs.setdefault(t, []).append(("code", a))
                    break

    # --- filter ---------------------------------------------------------------
    accepted, rejected = [], {}
    branch_cache = {}

    def branches(lo, hi):
        key = (lo, hi)
        if key not in branch_cache:
            out = []
            for a in range(lo, hi, 4):
                t, link = branch_target(a, word(a))
                if t is not None and not link:
                    out.append((a, t))
            branch_cache[key] = out
        return branch_cache[key]

    def reject(t, why):
        rejected.setdefault(why, []).append(t)

    for t in sorted(refs):
        o = owner(t)
        if o is None:
            reject(t, "no owner")
            continue
        if not is_terminator(word(t - 4)):
            # A call to a noreturn helper (assert/abort) can also end a function.
            # Accept that only for short thunks sitting in a real vtable: a data
            # ref whose neighbouring slots are mostly registered function starts.
            prev = word(t - 4)
            short = any(is_terminator(word(t + 4 * k)) and word(t + 4 * k) != 0 for k in range(4))
            in_vtable = any(
                kind == "data" and sum(word(a + 4 * k) in start_set for k in (-2, -1, 1, 2)) >= 2
                for kind, a in refs[t])
            if not ((prev >> 26) == 18 and (prev & 3) == 1 and short and in_vtable):
                reject(t, "prev not terminator")
                continue
        if word(t) == 0 or not decodes(t):
            reject(t, "does not decode")
            continue
        if in_code(word(t)):
            reject(t, "points at code pointer (table)")
            continue
        end = next_start(t)
        if any(t <= dst < end for _, dst in branches(o, t)):
            reject(t, "label inside owner")
            continue
        # Branching to the owner's entry point is a tail call, not a label jump.
        if any(o < dst < t for _, dst in branches(t, end)):
            reject(t, "branches back into owner")
            continue
        # SEH scope tables / label tables: a neighbouring data word pointing
        # mid-function in the same owner that is not itself after a terminator.
        bad = False
        for kind, a in refs[t]:
            if kind != "data":
                continue
            for k in (-3, -2, -1, 1, 2, 3):
                n = word(a + 4 * k)
                if in_code(n) and n not in start_set and owner(n) == o and not is_terminator(word(n - 4)):
                    bad = True
        if bad:
            reject(t, "SEH/label table neighbour")
            continue
        accepted.append(t)

    for why, ts in rejected.items():
        print(f"rejected {len(ts):5}  {why}", file=sys.stderr)
    print(f"accepted {len(accepted):5}", file=sys.stderr)
    if hidden:
        print(f"hidden found: {sorted(hex(h) for h in hidden & set(accepted))}", file=sys.stderr)
        print(f"hidden missed: {sorted(hex(h) for h in hidden - set(accepted))}", file=sys.stderr)
    for t in accepted:
        kinds = sorted({k for k, _ in refs[t]})
        first = refs[t][0][1]
        print(f"0x{t:08X} = {{ name = \"sub_{t:08X}\" }}  # {'+'.join(kinds)} ref @{first:08X}, owner {owner(t):08X}")


if __name__ == "__main__":
    main()
