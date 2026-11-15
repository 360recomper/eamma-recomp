#!/usr/bin/env python3
"""Extract EA Sports MMA's shader containers for the native renderer's shader cache (EN-4).

Every GPU shader the game creates is precompiled in data/xenon-gen/bigs/shader_big.ast
(FINDINGS-en1-shaders.md): 157 zlib-compressed RNAFX effects, each holding XDK shader containers
(0x102A11xx header) that FXLite hands to CreateVertexShader / CreatePixelShader unchanged. The
other bigs that hold containers (bootload, particle_shaders_big, crowd_1_big) only hold copies.

Each unique container is written as <sha1[:16]>.bin; <out_dir>/_sources.txt lists
"<name>.bin <effect>@<offset> ..." for tools/nr_build_shader_cache.py.

    python -I tools/eamma_shader_extract.py <out_dir> [--assets assets]

Prints the counts and a hash of the extracted set (the same disc gives the same hash).
"""
import argparse
import collections
import hashlib
import os
import struct
import sys
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SHADER_BIG = os.path.join("data", "xenon-gen", "bigs", "shader_big.ast")


def effects(d):
    """(name, bytes) of every entry of a BGFA big laid out like shader_big.ast (77-byte entries)."""
    count = int.from_bytes(d[12:16], "little")
    for i in range(count):
        e = d[0x44 + i * 77:0x44 + (i + 1) * 77]
        off = int.from_bytes(e[9:12], "little") * 8
        name = e[17:].split(b"\0")[0].decode("ascii", "replace")
        z = zlib.decompressobj()
        yield name, z.decompress(d[off:off + 0x100000])


def containers(d):
    """(offset, bytes) of every 4-aligned shader container in d (whole container: virtual + physical size)."""
    i = 0
    while True:
        i = d.find(b"\x10\x2a\x11", i)
        if i < 0 or i + 36 > len(d):
            return
        if i % 4 == 0:
            virt, phys = struct.unpack_from(">II", d, i + 4)
            if 36 < virt + phys <= min(len(d) - i, 1 << 20):
                yield i, d[i:i + virt + phys]
                i += virt + phys
                continue
        i += 1


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("out_dir")
    ap.add_argument("--assets", default=os.path.join(ROOT, "assets"))
    a = ap.parse_args()
    os.makedirs(a.out_dir, exist_ok=True)
    with open(os.path.join(a.assets, SHADER_BIG), "rb") as f:
        big = f.read()
    if big[:4] != b"BGFA":
        sys.exit(f"{SHADER_BIG}: not a BGFA big")

    uniq = {}
    n_effects = n_total = 0
    for name, blob in effects(big):
        n_effects += 1
        if blob[:2] != b"\xA3\xD7":
            print(f"warning: {name} is not a compiled RNAFX effect", file=sys.stderr)
        for off, c in containers(blob):
            n_total += 1
            uniq.setdefault(c, []).append(f"{name}@{off:X}")

    kinds = collections.Counter(c[:4].hex().upper() for c in uniq)
    whole = hashlib.sha256()
    with open(os.path.join(a.out_dir, "_sources.txt"), "w") as src:
        for c in sorted(uniq, key=lambda c: hashlib.sha1(c).hexdigest()):
            stem = hashlib.sha1(c).hexdigest()[:16]
            with open(os.path.join(a.out_dir, stem + ".bin"), "wb") as f:
                f.write(c)
            src.write(f"{stem}.bin {' '.join(uniq[c])}\n")
            whole.update(c)
    print(f"{n_effects} effects, {n_total} containers, {len(uniq)} unique "
          f"({', '.join(f'{k} x{v}' for k, v in sorted(kinds.items()))}); set {whole.hexdigest()[:16]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
