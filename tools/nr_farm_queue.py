"""Queue one staged native-renderer laptop job when the farm is free, then wait for its result.

usage: python -I tools/nr_farm_queue.py <staged job.json> [--after <run>] [--timeout-min 240]

Rule (user 2026-11-05, native is the focus): a native job joins the line right away and takes its turn
by file time; it no longer waits for ufc-lap-* jobs to clear, which could starve it.
--after <run>: first wait until TestFarm/results/<run>/result.json exists (the A/B partner).
The staged file is copied into jobs/ with the current time (the runner takes the oldest file first)
and removed from the staging folder. Prints one line per event; exit 0 = result.json is there,
1 = timed out, 2 = bad arguments, 3 = the gate took the job back.
"""
import glob
import json
import os
import shutil
import sys
import time

FARM = r'E:\ClaudeGame\TestFarm'
JOBS = os.path.join(FARM, 'jobs')


def say(text):
    print(time.strftime('%H:%M:%S'), text)
    sys.stdout.flush()


def busy():
    return glob.glob(os.path.join(JOBS, '*.json')) + glob.glob(os.path.join(JOBS, 'running', '*.json'))


def main(argv):
    pos, after, timeout = [], '', 240.0
    i = 0
    while i < len(argv):
        if argv[i] == '--after':
            after = argv[i + 1]; i += 2
        elif argv[i] == '--timeout-min':
            timeout = float(argv[i + 1]); i += 2
        else:
            pos.append(argv[i]); i += 1
    if len(pos) != 1 or not os.path.isfile(pos[0]):
        print(__doc__)
        return 2
    staged = pos[0]
    with open(staged, encoding='utf-8') as f:
        run = json.load(f)['run']
    result = os.path.join(FARM, 'results', run, 'result.json')
    end = time.time() + timeout * 60
    if after:
        partner = os.path.join(FARM, 'results', after, 'result.json')
        while not os.path.exists(partner):
            if time.time() > end:
                say('timed out waiting for ' + after); return 1
            time.sleep(15)
        say('partner result is there: ' + after)
    name = os.path.basename(staged)
    target = os.path.join(JOBS, name)
    shutil.copyfile(staged, target)  # copyfile: new file time, so it never jumps ahead of an older job
    os.remove(staged)
    say('queued ' + name)
    seen_running = False
    while not os.path.exists(result):
        if time.time() > end:
            say('timed out waiting for ' + result); return 1
        if os.path.exists(os.path.join(JOBS, 'running', name)):
            if not seen_running:
                say('running on the laptop'); seen_running = True
        elif os.path.exists(os.path.join(JOBS, 'hold', 'bk-first', name)):
            say('the gate moved the job to hold\\bk-first'); return 3
        time.sleep(10)
    with open(result, encoding='utf-8') as f:
        r = json.load(f)
    say('done: exit %s, %s, %s s' % (r.get('exit_code'), r.get('result'), r.get('seconds')))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
