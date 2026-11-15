#!/usr/bin/env python3
"""Extract the game's shader containers for the native renderer's shader cache (NR-7).

Game shaders can't ship, so the cache is built from the player's own disc files. Every XDK shader
container (0x102A11xx header) is pulled from shader/fxl.pac and default.xex, including the zlib
'YZLI' blobs inside them, four levels deep (NR-3 found nothing elsewhere on the disc or the DLC).
Each unique container is written as <sha1[:16]>.bin, the names NR-3's used.json uses.

The bloom and depth-of-field switches (src/no_bloom.h, src/no_dof.h) patch shader microcode in guest
memory, so each shader they hit also gets a patched copy (<name>.nobloom.bin / .nodof.bin) and both
variants land in the cache.

    python -I tools/nr_shader_extract.py <out_dir> [--assets assets]
    out/xenosrecomp2/XenosRecomp/XenosRecomp.exe <out_dir> shader_cache.bin \
        third_party/XenosRecomp/XenosRecomp/shader_common.h

Prints a hash of the extracted set: the same disc files give the same hash.
"""
import argparse
import hashlib
import os
import re
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import xex_image  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def containers(d):
    """(offset, bytes) of every 4-aligned shader container in d."""
    i = 0
    while True:
        i = d.find(b"\x10\x2a\x11", i)
        if i < 0 or i + 36 > len(d):
            return
        if i % 4 == 0:
            vsize, psize = struct.unpack_from(">II", d, i + 4)
            f1c, f20 = struct.unpack_from(">II", d, i + 28)
            if f1c == 0 and f20 == 0 and 36 < vsize + psize <= len(d) - i:
                yield i, d[i:i + vsize + psize]
                i += vsize + psize
                continue
        i += 1


def walk(d, src, found, depth=0):
    for off, c in containers(d):
        found.setdefault(c, f"{src}@{off:x}")
    if depth >= 4:
        return
    j = 0
    while True:
        j = d.find(b"YZLI", j)
        if j < 0:
            return
        try:
            sub = zlib.decompressobj().decompress(d[j + 16:], 64 << 20)
        except zlib.error:
            sub = b""
        if sub:
            walk(sub, f"{src}@yz{j:x}", found, depth + 1)
        j += 4


def c_bytes(text, name):
    m = re.search(r"\b%s\[\]\s*=\s*\{([^}]*)\}" % re.escape(name), text)
    return bytes(int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]{2})", m.group(1)))


def patch_targets():
    """[(feature, orig, off, patch)] from the microcode patches in src/."""
    bloom = open(os.path.join(ROOT, "src", "bloom_patterns.h")).read()
    m = re.search(r"\bkPatchOffset\s*=\s*(\d+)", bloom)
    out = [("nobloom", c_bytes(bloom, "kOrig"), int(m.group(1)), c_bytes(bloom, "kPatch"))]
    dof = open(os.path.join(ROOT, "src", "dof_patterns.h")).read()
    for ucode, off, new in re.findall(r"\{(kUcode\d+), sizeof\(\1\), (\d+), (kNew\d+)\}", dof):
        out.append(("nodof", c_bytes(dof, ucode), int(off), c_bytes(dof, new)))
    # Blood light fix (src/blood_light.h, on by default): the two fighter skin pixel shaders.
    blood = open(os.path.join(ROOT, "src", "blood_light_patterns.h")).read()
    for part in ("Body", "Face"):
        m = re.search(r"\bk%sOff\s*=\s*(\d+)" % part, blood)
        out.append(("bloodlight", c_bytes(blood, "k%sOrig" % part), int(m.group(1)), c_bytes(blood, "k%sPatch" % part)))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("out_dir")
    ap.add_argument("--assets", default=os.path.join(ROOT, "assets"))
    a = ap.parse_args()

    found = {}
    pac = os.path.join(a.assets, "DAT", "shader", "fxl.pac")
    walk(open(pac, "rb").read(), "fxl.pac", found)
    n_pac = len(found)
    _, img = xex_image.load(os.path.join(a.assets, "default.xex"))
    walk(img, "default.xex", found)
    n_xex = len(found) - n_pac

    variants = {}
    hits = {}
    for feature, orig, off, patch in patch_targets():
        for c, src in found.items():
            p = c.find(orig)
            while p >= 0:
                key = (c, feature)
                base = variants.get(key, c)
                variants[key] = base[:p + off] + patch + base[p + off + len(patch):]
                hits[feature] = hits.get(feature, 0) + 1
                p = c.find(orig, p + 1)

    os.makedirs(a.out_dir, exist_ok=True)
    names = []
    for c in found:
        names.append((hashlib.sha1(c).hexdigest()[:16] + ".bin", c, found[c]))
    for (c, feature), v in variants.items():
        names.append((f"{hashlib.sha1(c).hexdigest()[:16]}.{feature}.bin", v, f"{found[c]} +{feature}"))
    names.sort()
    total = hashlib.sha1()
    with open(os.path.join(a.out_dir, "_sources.txt"), "w") as f:
        for name, data, src in names:
            with open(os.path.join(a.out_dir, name), "wb") as g:
                g.write(data)
            f.write(f"{name} {src}\n")
            total.update(name.encode() + data)
    print(f"{len(found)} containers (fxl.pac {n_pac}, default.xex {n_xex}), "
          f"{len(variants)} patched variants ({', '.join(f'{k} {v}' for k, v in sorted(hits.items()))})")
    print(f"set hash {total.hexdigest()[:16]}")


if __name__ == "__main__":
    main()
