"""EA MMA laptop jobs: freeze the current build and write the native / emulated job files (TestFarm).

usage: python -I tools/eamma_farm_job.py <tag> [--script tools/selftest_scripts/laptop/boot_taps.txt] [--secs 150]
           [--every 5] [--shots 50,62,...] [--only emu|native] [--set k=v ...] [--native-set k=v ...]
           [--out <dir>] [--no-freeze] [--suffix <text>] [--why "one line"] [--queue]

- Freezes out/build/win-amd64-relwithdebinfo (eamma.exe, *.dll, shader_cache*.bin, pipelines_seed.bin) into
  E:/ClaudeGame/TestFarm/builds/eamma-<tag>/bin unless --no-freeze (the folder must exist then). PDBs go to
  builds/eamma-<tag>/pdb (stay on Chi; for symbolizing crash traces). A job always tests the frozen copy, so this tree
  can be rebuilt while jobs wait.
- Writes eamma-<tag><suffix>-native.json and -emu.json (same script, same build, eamma_renderer differs). The job's
  "build" field names the frozen folder; the laptop runner syncs it to C:\\ClaudeGame\\EAMMA\\bin\\<build> and passes
  --exe-dir there (E:/ClaudeGame/TestFarm/EAMMA-JOBS.md).
- Default --out is TestFarm/jobs/hold/eamma-staged: nothing runs from there. --queue writes straight to TestFarm/jobs
  (only when no higher-priority game's jobs are waiting: UFC, then rrxx, then svr07 go first).
- --script: a path relative to this repo's root (synced to the laptop with tools/selftest_scripts). Laptop-timed copies
  live in tools/selftest_scripts/laptop/.
- Laptop rules the runner enforces anyway: --scale 1, one game at a time. Native jobs pick the RTX 4060 by name
  (eamma_native_adapter); the runner never forces a device.
- json.dump writes the backslashes; never write job files from a shell heredoc.
"""
import glob
import json
import os
import shutil
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, 'out', 'build', 'win-amd64-relwithdebinfo')
FARM = r'E:\ClaudeGame\TestFarm'
LAPTOP_ADAPTER = 'NVIDIA GeForce RTX 4060 Laptop GPU'
NEED = ('eamma.exe', 'eamma_MMADLLzf_xex.dll', 'shader_cache.bin', 'shader_cache_linked.bin')


def main(argv):
    opt = {'--script': 'tools/selftest_scripts/laptop/boot_taps.txt', '--secs': '150', '--every': '5',
           '--shots': '', '--only': '', '--out': os.path.join(FARM, 'jobs', 'hold', 'eamma-staged'),
           '--suffix': '', '--why': ''}
    sets, native_sets, pos, freeze, queue = [], [], [], True, False
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == '--set':
            sets.append(argv[i + 1]); i += 2
        elif a == '--native-set':
            native_sets.append(argv[i + 1]); i += 2
        elif a == '--no-freeze':
            freeze = False; i += 1
        elif a == '--queue':
            queue = True; i += 1
        elif a in opt:
            opt[a] = argv[i + 1]; i += 2
        else:
            pos.append(a); i += 1
    if len(pos) != 1:
        print(__doc__)
        return 2
    if opt['--only'] not in ('', 'emu', 'native'):
        print('--only must be emu or native')
        return 2
    build = 'eamma-' + pos[0]
    if not os.path.isfile(os.path.join(ROOT, opt['--script'])):
        print('no script in this repo:', opt['--script'])
        return 2
    frozen = os.path.join(FARM, 'builds', build, 'bin')
    if freeze:
        os.makedirs(frozen, exist_ok=True)
        files = [os.path.join(BUILD, 'eamma.exe')] + glob.glob(os.path.join(BUILD, '*.dll')) + \
            glob.glob(os.path.join(BUILD, 'shader_cache*.bin')) + glob.glob(os.path.join(BUILD, 'pipelines_seed.bin'))
        files = [f for f in files if not f.endswith('.failures.txt')]
        for f in files:
            shutil.copy2(f, os.path.join(frozen, os.path.basename(f)))
        print('froze %d files -> %s' % (len(files), frozen))
        pdb_dir = os.path.join(FARM, 'builds', build, 'pdb')
        os.makedirs(pdb_dir, exist_ok=True)
        for f in ('eamma.pdb', 'eamma_MMADLLzf_xex.pdb'):
            if os.path.isfile(os.path.join(BUILD, f)):
                shutil.copy2(os.path.join(BUILD, f), os.path.join(pdb_dir, f))
    for need in NEED:
        if not os.path.isfile(os.path.join(frozen, need)):
            print('missing in the frozen build:', need)
            return 1
    out = os.path.join(FARM, 'jobs') if queue else opt['--out']
    os.makedirs(out, exist_ok=True)
    for kind in ('native', 'emu'):
        if opt['--only'] and opt['--only'] != kind:
            continue
        run = '%s%s-%s' % (build, opt['--suffix'], kind)
        args = ['--script', opt['--script'], '--secs', opt['--secs'], '--every', opt['--every'],
                '--renderer', 'native' if kind == 'native' else 'emulated']
        if opt['--shots']:
            args += ['--shots', opt['--shots']]
        for s in sets:
            args += ['--set', s]
        if kind == 'native':
            args += ['--set', 'eamma_native_adapter=' + LAPTOP_ADAPTER]
            for s in native_sets:
                args += ['--set', s]
        job = {'game': 'eamma', 'run': run, 'build': build, 'args': args,
               'why': opt['--why'] or 'EA MMA native renderer A/B (%s), build %s' % (kind, build),
               'from': 'EA MMA native renderer session'}
        path = os.path.join(out, run + '.json')
        with open(path, 'w', encoding='utf-8') as f:
            json.dump(job, f, indent=1)
        print('wrote', path)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
