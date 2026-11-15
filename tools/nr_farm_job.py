"""Native renderer laptop jobs: freeze the current build and write the A/B job files.

usage: python -I tools/nr_farm_job.py <tag> [--script <laptop script path>] [--secs 240] [--every 5]
           [--shots 50,62,...] [--only emu|native] [--set k=v ...] [--native-set k=v ...]
           [--out <dir>] [--no-freeze] [--suffix <text>] [--mods none]

- Freezes out/build/win-amd64-relwithdebinfo (ufc3.exe, *.dll, shader_cache*.bin, pipelines_seed.bin) into
  E:/ClaudeGame/TestFarm/builds/<tag>/bin unless --no-freeze (the folder must exist then). A job
  always tests the frozen copy, so this tree can be rebuilt while jobs wait.
- Writes ufc-nr-<tag><suffix>-emu.json and ufc-nr-<tag><suffix>-native.json (same script, same
  build, ufc3_renderer differs) to --out. Default --out is TestFarm/jobs/hold/nr-staged: nothing
  runs from there. Move a job into TestFarm/jobs only when no ufc-lap-* job is queued or running
  (the coordinator's jobs go first) and the BK-FIRST gate lets ufc-nr-* through.
- json.dump writes the UNC backslashes; never write job files from a shell heredoc.
- --script: a path as the LAPTOP sees it. Scripts in main's tools/selftest_scripts are synced to
  the laptop (tools/selftest_scripts/laptop/x.txt); a script that only exists in this tree is given
  as its UNC path (\\\\192.168.1.253\\ClaudeGame\\UFC-native\\tools\\selftest_scripts\\x.txt).
"""
import glob
import json
import os
import shutil
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, 'out', 'build', 'win-amd64-relwithdebinfo')
FARM = r'E:\ClaudeGame\TestFarm'
FARM_UNC = r'\\192.168.1.253\ClaudeGame\TestFarm'


def main(argv):
    opt = {'--script': 'tools/selftest_scripts/laptop/nr5_ufc_exhibition_cpu.txt', '--secs': '240',
           '--every': '5', '--shots': '', '--only': '', '--out': os.path.join(FARM, 'jobs', 'hold', 'nr-staged'),
           '--suffix': '', '--why': '', '--mods': ''}
    sets, native_sets, pos, freeze = [], [], [], True
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == '--set':
            sets.append(argv[i + 1]); i += 2
        elif a == '--native-set':
            native_sets.append(argv[i + 1]); i += 2
        elif a == '--no-freeze':
            freeze = False; i += 1
        elif a in opt:
            opt[a] = argv[i + 1]; i += 2
        else:
            pos.append(a); i += 1
    if len(pos) != 1:
        print(__doc__)
        return 2
    tag = pos[0]
    # --script tree:<path in this tree> builds the UNC path here (Git Bash strips one of the two
    # leading backslashes from a \\server argument, whatever the quoting).
    if opt['--script'].startswith('tree:'):
        opt['--script'] = '\\\\192.168.1.253\\ClaudeGame\\UFC-native\\' + opt['--script'][5:].replace('/', '\\')
    frozen = os.path.join(FARM, 'builds', tag, 'bin')
    if freeze:
        os.makedirs(frozen, exist_ok=True)
        files = [os.path.join(BUILD, 'ufc3.exe')] + glob.glob(os.path.join(BUILD, '*.dll')) + \
            glob.glob(os.path.join(BUILD, 'shader_cache*.bin')) + \
            glob.glob(os.path.join(BUILD, 'pipelines_seed.bin'))  # NR-9h seed list, beside shader_cache.bin
        for f in files:
            shutil.copy2(f, os.path.join(frozen, os.path.basename(f)))
        print('froze %d files -> %s' % (len(files), frozen))
        # The PDB stays on Chi, beside bin\ (the laptop copies only exe + dlls): tools/nr_symbolize.py
        # names the ufc3.exe+0x... frames of native_crash_trace.txt with it.
        pdb = os.path.join(BUILD, 'ufc3.pdb')
        if os.path.isfile(pdb):
            os.makedirs(os.path.join(FARM, 'builds', tag, 'pdb'), exist_ok=True)
            shutil.copy2(pdb, os.path.join(FARM, 'builds', tag, 'pdb', 'ufc3.pdb'))
            shutil.copy2(os.path.join(BUILD, 'ufc3.exe'), os.path.join(FARM, 'builds', tag, 'pdb', 'ufc3.exe'))
    for need in ('ufc3.exe', 'shader_cache.bin', 'shader_cache_linked.bin'):
        if not os.path.isfile(os.path.join(frozen, need)):
            print('missing in the frozen build:', need)
            return 1
    frozen_unc = FARM_UNC + '\\builds\\' + tag + '\\bin'
    os.makedirs(opt['--out'], exist_ok=True)
    for kind in ('emu', 'native'):
        if opt['--only'] and opt['--only'] != kind:
            continue
        run = 'ufc-nr-%s%s-%s' % (tag, opt['--suffix'], kind)
        args = ['--script', opt['--script'], '--secs', opt['--secs'], '--every', opt['--every']]
        if opt['--shots']:
            args += ['--shots', opt['--shots']]
        if opt['--mods']:
            args += ['--mods', opt['--mods']]
        args += ['--exe-from', frozen_unc, '--set', 'ufc3_renderer=' + ('native' if kind == 'native' else 'emulated')]
        for s in sets:
            args += ['--set', s]
        if kind == 'native':
            args += ['--set', 'ufc3_shader_cache=' + frozen_unc + '\\shader_cache.bin']
            for s in native_sets:
                args += ['--set', s]
        job = {'game': 'ufc', 'run': run, 'args': args,
               'why': opt['--why'] or 'native renderer A/B (%s), build %s' % (kind, tag),
               'from': 'UFC native renderer session'}
        path = os.path.join(opt['--out'], run + '.json')
        with open(path, 'w', encoding='utf-8') as f:
            json.dump(job, f, indent=1)
        print('wrote', path)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
