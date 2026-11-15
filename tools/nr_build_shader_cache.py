#!/usr/bin/env python3
"""Build the native renderer's shader_cache.bin from the game files (NR-7).

    python -I tools/nr_build_shader_cache.py [--out PATH] [--assets assets] [--used used.json]

XenosRecomp.exe and ufc3_prelink.exe are built with the game (build.bat native, cmake/xenosrecomp.cmake)
and looked for beside ufc3.exe.

1. Extracts every shader container (tools/nr_shader_extract.py) into a temporary folder.
2. Translates them to DXIL into --out (default out/build/win-amd64-relwithdebinfo/shader_cache.bin)
   and lists the shaders that failed in <out>.failures.txt. The expected failures are the 64
   XDK-internal vertex shaders in default.xex (never created at run time) and the 8 memexport pixel
   shaders (NR-10).
3. Runs ufc3_prelink to write <out stem>_linked.bin: every
   spec-constant variant pre-linked, so the game never runs DXC while drawing (NR-7b). The game
   reads both files (cvar ufc3_shader_cache).

--used takes NR-3's used.json (container names the game was seen creating) and reports how many
of them made it into the cache.
"""
import argparse
import json
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
XR_SRC = os.path.join(ROOT, "third_party", "XenosRecomp")
GAME_DIR = os.path.join(ROOT, "out", "build", "win-amd64-relwithdebinfo")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--out", default=os.path.join(GAME_DIR, "shader_cache.bin"))
    ap.add_argument("--prelink", default=os.path.join(GAME_DIR, "ufc3_prelink.exe"),
                    help="ufc3_prelink.exe (needs dxcompiler.dll + dxil.dll beside it)")
    ap.add_argument("--xenosrecomp", default=os.path.join(GAME_DIR, "XenosRecomp.exe"),
                    help="XenosRecomp.exe (needs dxcompiler.dll + dxil.dll beside it)")
    ap.add_argument("--assets", default=os.path.join(ROOT, "assets"))
    ap.add_argument("--used", help="NR-3 used.json: report how many used shaders converted")
    a = ap.parse_args()

    if not os.path.exists(a.xenosrecomp):
        sys.exit(f"{a.xenosrecomp} missing: build the game with build.bat native first")
    with tempfile.TemporaryDirectory(prefix="ufc3_shaders_") as tmp:
        subprocess.run([sys.executable, "-I", os.path.join(ROOT, "tools", "nr_shader_extract.py"), tmp,
                        "--assets", a.assets], check=True)
        r = subprocess.run([a.xenosrecomp, tmp, a.out, os.path.join(XR_SRC, "XenosRecomp", "shader_common.h")],
                           capture_output=True, text=True)
        sources = dict(l.split(" ", 1) for l in open(os.path.join(tmp, "_sources.txt")).read().splitlines())
    if r.returncode != 0:
        sys.exit(f"XenosRecomp failed ({r.returncode}):\n{r.stdout[-2000:]}\n{r.stderr[-2000:]}")
    print(r.stdout.strip().splitlines()[-1])

    failures = re.findall(r"source=(\S+) reason=(.*)", r.stderr)
    with open(a.out + ".failures.txt", "w") as f:
        for name, reason in failures:
            f.write(f"{name} {reason} [{sources.get(name + '.bin', '?')}]\n")
    xex = sum("default.xex" in sources.get(n + ".bin", "") for n, _ in failures)
    print(f"failed {len(failures)}: default.xex {xex}, fxl.pac {len(failures) - xex} "
          f"(list: {a.out}.failures.txt)")
    if a.used:
        used = json.load(open(a.used))
        missed = sorted(n for n, _ in failures if n in used)
        print(f"used set: {len(used) - len(missed)}/{len(used)} converted, missing {missed}")

    stem, ext = os.path.splitext(a.out)
    if not os.path.exists(a.prelink):
        sys.exit(f"{a.prelink} missing: build the game with build.bat native first")
    subprocess.run([a.prelink, a.out, stem + "_linked" + ext], check=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
