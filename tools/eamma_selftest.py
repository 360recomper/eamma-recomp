"""Unattended in-game run of the dev eamma.exe for queue workers and the laptop farm: scripted pads, no user, PNG captures.
Ported from UFC's tools/ufc_selftest.py (same options, output layout and exit codes) so the TestFarm runner treats it alike.

usage: python -I tools/eamma_selftest.py <run name> --script <input script> | --script none
           [--renderer native|emulated] [--exe-dir out/build/win-amd64-relwithdebinfo] [--exe-from <dir>]
           [--save <userdata dir>] [--scale 1] [--backend d3d12|vulkan] [--mods none | <dir>]
           [--set key=value ...] [--secs 240] [--shots 30,45,60] [--every 0]
           [--on-log "text"] [--after 0.2,0.8,1.6,2.6] [--max-hits 3] [--keep-save] [--end-after <s>]
           [--skip-intros <until s>] [--keep-game] [--shot-format png|jpg] [--max-shots 1500] [--min-free-gb 15]

- Run dir <RUNS>\\<run>\\: game\\ = hard links to one copy per build (RUNS\\_bin\\) + private copies of the shader caches,
  userdata\\ = a COPY of the save (55 MB), test.log, shots\\*.png, sheet.jpg. At the end userdata\\ goes unless --keep-save,
  game\\ unless --keep-game. The original save is never touched.
- --renderer: eamma_renderer (default native: this tool exists for the native renderer work). Native turns command buffers
  off by itself (src/eamma_app.h).
- --scale: native = eamma_native_scale, emulated = resolution_scale. The laptop forces --scale 1 (TestFarm rule).
- --backend: gpu_backend of the emulated renderer (default d3d12, as eamma_defaults); the native renderer is D3D12 always.
- Every --set k=v adds/overrides a --k=v cvar (e.g. --set eamma_native_adapter=NVIDIA GeForce RTX 4060 Laptop GPU).
- --script: SDK cvar input_script. One event per line: `pad <n>` connects pad n idle;
  `<t|+dt> <pad> <hold s> [A B X Y START BACK LB RB LS RS UP DOWN LEFT RIGHT] [lx= ly= rx= ry= lt= rt=]`,
  t = seconds after the first pad poll. `--script none` = plain timed boot check (no pads). A relative path is taken from
  the current directory, then from this repo's root.
- Launch: no activation, window at the bottom of the z-order, 1280x720 window, audio muted. Captures (PrintWindow, also
  while covered) at --shots seconds after launch, every --every s, and at --after offsets after each log line holding
  --on-log (up to --max-hits). At --secs (or when the game exits) WM_CLOSE (log flush), then kill ONLY its own process.
- --skip-intros <s>: tap A every 3 s after the script's last press up to <s>.
- --end-after <s>: end <s> seconds after the last planned capture instead of at --secs.
- --exe-from <dir>: test that build instead of --exe-dir, whatever order they come in (the farm runner forces --exe-dir last).
- Rumble Roses has GPU priority on Chi (user 2026-10-29): waits while RRXX's selftest.lock exists or a rumble_roses_xx.exe
  runs (up to 90 min; then exit 2). The laptop patches RRXX_LOCK to a path that never exists.
  Never runs next to another eamma.exe (exit 2), nor while queue-logs\\NO-LAUNCH exists (exit 2).
  One run at a time (queue-logs\\selftest.lock). Exit 0 = ran to --secs, 1 = game exited early (crash?), 2 = not launched.
- Laptop runner: the constant lines RUNS / SAVE / RRXX_LOCK / LOCAL_AI below are patched by exact text
  (E:\\ClaudeGame\\TestFarm\\EAMMA-JOBS.md); keep each on one line and change them only together with that file.
- Token rule: look at sheet.jpg first; open single PNGs only to zoom; grep test.log, never read it whole.
"""
import ctypes, ctypes.wintypes as wt, os, re, shutil, struct, subprocess, sys, time, zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RUNS = r'E:\ClaudeGame\EAMMA-tests\selftest'
LOCK = os.path.join(ROOT, 'queue-logs', 'selftest.lock')
HOLD = os.path.join(ROOT, 'queue-logs', 'NO-LAUNCH')
RRXX_LOCK = r'E:\ClaudeGame\RumbleRosesRecomp\queue-logs\selftest.lock'
EXE = 'eamma.exe'
SAVE = r'E:\ClaudeGame\EAMMA Recomp\userdata'
LOCAL_AI = r'E:\ClaudeGame\local-ai\local_ai.py'
LINK_SAVE_DIRS = set()  # EA MMA's save is small: every folder is copied
MIN_FREE_GB = 15  # disk guard: no launch below this much free space on the run drive (--min-free-gb, EAMMA_MIN_FREE_GB)
DEFAULTS = {'log_level': 'info', 'fullscreen': 'false', 'audio_mute': 'true', 'window_width': '1280',
            'window_height': '720'}

user32, gdi32, kernel32 = ctypes.windll.user32, ctypes.windll.gdi32, ctypes.windll.kernel32
# Per-monitor DPI aware, or a scaled display (the laptop: 125%) reports a smaller client rect and shots lose the right and bottom edges.
user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))


def pids(image):
    out = subprocess.run(['tasklist', '/fo', 'csv', '/nh', '/fi', 'IMAGENAME eq ' + image],
                         capture_output=True, text=True).stdout
    return {int(l.split('","')[1]) for l in out.splitlines() if l.lower().startswith('"' + image.lower())}


def rrxx_busy():
    return os.path.exists(RRXX_LOCK) or bool(pids('rumble_roses_xx.exe'))


def wait_for_rrxx():
    """RRXX chains self-tests with 1-2 s gaps: launch only after 3 min with no RRXX game and no RRXX lock."""
    if not rrxx_busy():  # user 2026-11-06: PC free = launch now (the 3 min clear wait applies only after a Rumble Roses run was seen)
        return True
    start, clear_since = time.time(), None
    while time.time() - start < 90 * 60:
        if rrxx_busy():
            if clear_since is not None or time.time() - start < 1:
                print('Rumble Roses test running, waiting (it has GPU priority)'); sys.stdout.flush()
            clear_since = None
        elif clear_since is None:
            clear_since = time.time()
        elif time.time() - clear_since >= 180:
            return True
        time.sleep(30)
    return False


def pid_alive(pid):
    h = kernel32.OpenProcess(0x1000, False, pid)  # PROCESS_QUERY_LIMITED_INFORMATION
    if not h:
        return False
    code = wt.DWORD()
    kernel32.GetExitCodeProcess(h, ctypes.byref(code))
    kernel32.CloseHandle(h)
    return code.value == 259  # STILL_ACTIVE


def user_playing():
    """EA MMA has no separate play copy: any eamma.exe not started by us counts (only used with --parallel)."""
    return bool(pids(EXE))


def take_lock():
    os.makedirs(os.path.dirname(LOCK), exist_ok=True)
    if os.path.exists(LOCK):
        try:
            pid = int(open(LOCK).read().split()[0])
        except (ValueError, IndexError):
            pid = 0
        # a killed run leaves its lock and Windows reuses PIDs: no run lasts 3 h (90 min RRXX wait + the run)
        if pid and pid_alive(pid) and time.time() - os.path.getmtime(LOCK) < 3 * 3600:
            return False
        os.replace(LOCK, LOCK + '.stale')
    try:
        fd = os.open(LOCK, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
    except FileExistsError:
        return False
    os.write(fd, ('%d %s\n' % (os.getpid(), time.strftime('%Y-%m-%d %H:%M:%S'))).encode())
    os.close(fd)
    return True


def link_tree(src, dst):
    for d, _, files in os.walk(src):
        out = os.path.join(dst, os.path.relpath(d, src))
        os.makedirs(out, exist_ok=True)
        for f in files:
            t = os.path.join(out, f)
            if not os.path.exists(t):
                os.link(os.path.join(d, f), t)


def free_gb(path):
    try:
        return shutil.disk_usage(path).free / 1e9
    except OSError:
        return 1e9


def bin_snapshot(exe_dir, files):
    """Disk fix (2026-11-11, E: hit 0 GB): one private copy of the build's exe + dlls per distinct build in RUNS\\_bin\\<key>\\
    (key = names, sizes and mtimes), shared by every run of that build through hard links. Still a COPY of the build dir (a
    rebuild may rewrite the build's files in place, and a hard link into the build dir would lock the exe for the linker),
    but made once per build instead of once per run (~145 MB each). disk_prune.py removes snapshots no run links to.
    Returns the snapshot dir, or None (then the caller copies as before)."""
    try:
        sig = ';'.join('%s:%d:%d' % (f.lower(), os.stat(os.path.join(exe_dir, f)).st_size,
                                     int(os.stat(os.path.join(exe_dir, f)).st_mtime)) for f in sorted(files))
        snap = os.path.join(RUNS, '_bin', '%08x' % (zlib.crc32(sig.encode()) & 0xFFFFFFFF))
        if os.path.isfile(os.path.join(snap, 'files.txt')):
            return snap
        tmp = snap + '.tmp%d' % os.getpid()
        os.makedirs(tmp, exist_ok=True)
        for f in files:
            shutil.copy2(os.path.join(exe_dir, f), os.path.join(tmp, f))
        with open(os.path.join(tmp, 'files.txt'), 'w') as fh:
            fh.write(exe_dir + '\n' + sig.replace(';', '\n') + '\n')
        try:
            os.rename(tmp, snap)
        except OSError:  # a parallel run made it first
            shutil.rmtree(tmp, ignore_errors=True)
        return snap if os.path.isfile(os.path.join(snap, 'files.txt')) else None
    except OSError as e:
        print('bin snapshot failed (%s), copying' % e)
        return None


def make_copy(exe_dir, save, run):
    rg, rs = os.path.join(run, 'game'), os.path.join(run, 'userdata')
    os.makedirs(rg, exist_ok=True)
    bins, caches = [], []
    for f in os.listdir(exe_dir):
        # merge7: the native files beside the exe too, so a run without renderer/cache flags tests the plain-double-click defaults.
        if f == EXE or f.lower().endswith('.dll'):
            bins.append(f)
        elif f.lower() in ('shader_cache.bin', 'shader_cache_linked.bin', 'pipelines_seed.bin', 'pipelines.bin'):
            caches.append(f)  # the game may write these: always a private copy per run
    snap = bin_snapshot(exe_dir, bins)
    for f in bins:
        t = os.path.join(rg, f)
        if os.path.exists(t):
            os.remove(t)  # never write through an old hard link
        try:
            if not snap:
                raise OSError('no snapshot')
            os.link(os.path.join(snap, f), t)
        except OSError:
            shutil.copy2(os.path.join(exe_dir, f), t)  # a copy: a rebuild may rewrite the build's files in place
    for f in caches:
        shutil.copy2(os.path.join(exe_dir, f), os.path.join(rg, f))
    with open(os.path.join(rg, 'placed.txt'), 'w') as fh:  # what drop_game() may delete at the end
        fh.write('\n'.join(bins + caches + ['placed.txt']) + '\n')
    for d in os.listdir(save):
        src, t = os.path.join(save, d), os.path.join(rs, d)
        if not os.path.isdir(src):
            continue
        if d in LINK_SAVE_DIRS:
            link_tree(src, t)
        else:
            shutil.rmtree(t, ignore_errors=True)
            shutil.copytree(src, t)
    return rg, rs


def drop_game(rg):
    """End of run: delete the files make_copy placed in game\\ (links / copies of the build, ~145 MB a run when copied);
    anything the game itself wrote there stays, and game\\ goes only if it is then empty. --keep-game skips this."""
    lst = os.path.join(rg, 'placed.txt')
    if not os.path.isfile(lst):
        return
    for f in open(lst).read().split():
        p = os.path.join(rg, f)
        try:
            if os.path.isfile(p) and not os.path.islink(p):
                os.remove(p)
        except OSError as e:
            print('could not delete %s: %s' % (p, e))
    try:
        os.rmdir(rg)
    except OSError:
        pass


def save_shot(path, w, h, bgra, fmt):
    """PIL when it is there: same RGBA pixels as png() (alpha forced opaque) but with PNG row filters, ~half the bytes
    (2026-11-11 disk fix: 4.5 -> 2.3 MB for a 1920x1200 shot) and faster; fmt 'jpg' = JPEG q90 (~10x smaller, opt-in).
    Returns the path written."""
    try:
        from PIL import Image
    except ImportError:
        png(path, w, h, bgra)
        return path
    im = Image.frombuffer('RGBA', (w, h), bytes(bgra), 'raw', 'BGRA', 0, 1)
    if fmt == 'jpg':
        path = path[:-4] + '.jpg'
        im.convert('RGB').save(path, quality=90)
    else:
        im.putalpha(255)
        im.save(path, compress_level=6)
    return path


GWL_EXSTYLE, WS_EX_NOACTIVATE, WS_EX_TOPMOST = -20, 0x08000000, 0x8


def no_activate(pid):
    """Mark every visible top-level window of the game WS_EX_NOACTIVATE (focus fix 2026-11-11): Windows' activation
    fallback (when the active window closes) and clicks then never make the test window the foreground window.
    Returns how many windows were changed."""
    changed = []
    proc = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)

    def cb(hwnd, _):
        p = wt.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(p))
        if p.value == pid and user32.IsWindowVisible(hwnd):
            ex = user32.GetWindowLongW(hwnd, GWL_EXSTYLE)
            if not ex & WS_EX_NOACTIVATE:
                user32.SetWindowLongW(hwnd, GWL_EXSTYLE, ex | WS_EX_NOACTIVATE)
                changed.append(hwnd)
        return True
    user32.EnumWindows(proc(cb), 0)
    return len(changed)


def find_window(pid):
    found = []
    proc = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)

    def cb(hwnd, _):
        p = wt.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(p))
        if p.value == pid and user32.IsWindowVisible(hwnd):
            r = wt.RECT()
            user32.GetClientRect(hwnd, ctypes.byref(r))
            if r.right * r.bottom > 320 * 200:
                found.append(hwnd)
        return True
    user32.EnumWindows(proc(cb), 0)
    return found[0] if found else None


def png(path, w, h, bgra):
    stride = w * 4 + 1
    raw = bytearray(b''.join(b'\0' + bytes(bgra[y * w * 4:(y + 1) * w * 4]) for y in range(h)))
    for y in range(h):  # BGRA -> RGBA, alpha forced opaque
        row = raw[y * stride + 1:(y + 1) * stride]
        row[0::4], row[2::4] = row[2::4], row[0::4]
        row[3::4] = b'\xff' * w
        raw[y * stride + 1:(y + 1) * stride] = row

    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xFFFFFFFF)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0))
                + chunk(b'IDAT', zlib.compress(bytes(raw), 6)) + chunk(b'IEND', b''))


def capture(hwnd, path, fmt='png'):
    r = wt.RECT()
    user32.GetClientRect(hwnd, ctypes.byref(r))
    w, h = r.right, r.bottom
    if w <= 0 or h <= 0:
        return False
    hdc = user32.GetDC(hwnd)
    mdc = gdi32.CreateCompatibleDC(hdc)
    bmp = gdi32.CreateCompatibleBitmap(hdc, w, h)
    gdi32.SelectObject(mdc, bmp)
    ok = user32.PrintWindow(hwnd, mdc, 3)  # PW_CLIENTONLY | PW_RENDERFULLCONTENT
    hdr = struct.pack('<IiiHHIIiiII', 40, w, -h, 1, 32, 0, 0, 0, 0, 0, 0)
    buf = ctypes.create_string_buffer(w * h * 4)
    gdi32.GetDIBits(mdc, bmp, 0, h, buf, ctypes.create_string_buffer(hdr), 0)
    gdi32.DeleteObject(bmp)
    gdi32.DeleteDC(mdc)
    user32.ReleaseDC(hwnd, hdc)
    if ok:
        save_shot(path, w, h, buf.raw, fmt)
    return bool(ok)


def sheet(shots, out, cols=4, w=480):
    """Token saver: one small labelled contact sheet of the last 16 captures."""
    try:
        from PIL import Image, ImageDraw
    except ImportError:
        return
    fs = sorted(f for f in os.listdir(shots) if f.endswith(('.png', '.jpg')))[-16:]
    if not fs:
        return
    h = w * 9 // 16
    rows = (len(fs) + cols - 1) // cols
    im = Image.new('RGB', (cols * w, rows * h))
    for i, f in enumerate(fs):
        x, y = i % cols * w, i // cols * h
        im.paste(Image.open(os.path.join(shots, f)).convert('RGB').resize((w, h)), (x, y))
        ImageDraw.Draw(im).text((x + 4, y + 4), f[:-4], fill=(255, 255, 0))
    im.save(out, quality=80)
    print('sheet', out)


def with_intro_skips(script, until, run):
    """--skip-intros <until s>: copy of the script with an A tap every 3 s after its last press up to <until>, so walkouts,
    ring intros and the announcer get skipped (user 2026-11-04: tests kept playing full entrances). Taps closer than 1 s to a
    scripted press are left out. Written to <run>/script_skip.txt; the original file is not touched."""
    lines = open(script, encoding='utf-8').read().splitlines()
    times = []
    for l in lines:
        f = l.split()
        if len(f) >= 4 and not l.lstrip().startswith('#'):
            try:
                times.append(float(f[0]))
            except ValueError:
                pass
    t = (max(times) if times else 0) + 3
    extra = []
    while t <= until:
        if all(abs(t - x) >= 1 for x in times):
            extra.append('%g 0 0.2 A' % t)
        t += 3
    out = os.path.join(run, 'script_skip.txt')
    with open(out, 'w', encoding='utf-8', newline='\n') as fh:
        fh.write('\n'.join(lines + ['# --skip-intros %g: A every 3 s' % until] + extra) + '\n')
    print('skip-intros: %d A taps up to %g s -> %s' % (len(extra), until, out))
    return out


def main():
    a = sys.argv[1:]
    opt = {'--secs': '240', '--shots': '', '--every': '0', '--on-log': '', '--after': '0.2,0.8,1.6,2.6',
           '--max-hits': '3', '--end-after': '', '--skip-intros': '', '--script': '', '--exe-dir': os.path.join(ROOT, 'out', 'build', 'win-amd64-relwithdebinfo'), '--exe-from': '',
           '--mods': 'none', '--save': SAVE, '--scale': '1', '--backend': 'd3d12', '--renderer': 'native',
           '--shot-format': os.environ.get('EAMMA_SHOT_FORMAT', 'png'), '--max-shots': os.environ.get('EAMMA_MAX_SHOTS', '1500'),
           '--min-free-gb': os.environ.get('EAMMA_MIN_FREE_GB', str(MIN_FREE_GB))}
    sets, pos, keep_save, keep_game = {}, [], False, False
    # --parallel: the CALLER holds a game slot lock; the global selftest.lock no longer blocks (kept from UFC's tool).
    parallel = '--parallel' in a
    if parallel:
        a = [x for x in a if x != '--parallel']
    i = 0
    while i < len(a):
        if a[i] == '--set':
            k, v = a[i + 1].split('=', 1); sets[k.strip().lstrip('-')] = v.strip(); i += 2
        elif a[i] == '--keep-save':
            keep_save = True; i += 1
        elif a[i] == '--keep-game':
            keep_game = True; i += 1
        elif a[i] in opt:
            opt[a[i]] = a[i + 1]; i += 2
        else:
            pos.append(a[i]); i += 1
    if len(pos) != 1 or not opt['--script']:
        print(__doc__); return 2
    name = pos[0]
    end_after = float(opt['--end-after']) if opt['--end-after'] else None
    script = '' if opt['--script'].lower() == 'none' else os.path.abspath(opt['--script'])
    if script and not os.path.isfile(script) and not os.path.isabs(opt['--script']):
        script = os.path.join(ROOT, opt['--script'])
    if script and not os.path.isfile(script):
        print('no script file:', script); return 2
    if os.path.exists(HOLD):
        print('NO-LAUNCH hold set, not launched'); return 2
    fmt = opt['--shot-format'].lower().lstrip('.')
    if fmt not in ('png', 'jpg'):
        print('--shot-format must be png or jpg'); return 2
    max_shots, min_free = int(opt['--max-shots']), float(opt['--min-free-gb'])
    os.makedirs(RUNS, exist_ok=True)
    if free_gb(RUNS) < min_free:   # disk guard (2026-11-11: E: at 0 GB broke tests)
        print('LOW DISK: %.1f GB free on %s, need %g GB (--min-free-gb / EAMMA_MIN_FREE_GB). Not launched.' % (free_gb(RUNS), RUNS[:3], min_free))
        return 2
    if parallel and user_playing():
        print('an eamma.exe is running, not launched'); return 2
    if not parallel and pids(EXE):
        print('an eamma.exe is already running (user or another test), not launched'); return 2
    if not parallel and not take_lock():
        print('another EA MMA self-test holds', LOCK); return 2
    run = os.path.join(RUNS, name)
    rs = rg = p = None
    try:
        if not wait_for_rrxx():
            print('Rumble Roses still busy after 90 min, not launched'); return 2
        if (pids(EXE) and not parallel) or (parallel and user_playing()):
            print('an eamma.exe started while waiting, not launched'); return 2
        rg, rs = make_copy(os.path.abspath(opt['--exe-from'] or opt['--exe-dir']), opt['--save'], run)
        shots = os.path.join(run, 'shots')
        os.makedirs(shots, exist_ok=True)
        for f in os.listdir(shots):
            if os.path.isfile(os.path.join(shots, f)):
                os.remove(os.path.join(shots, f))
        log = os.path.join(run, 'test.log')
        if os.path.exists(log):
            os.replace(log, log + '.prev')
        cv = dict(DEFAULTS)
        renderer = opt['--renderer'].lower()
        if renderer in ('emu', 'emulated'):
            renderer = 'emulated'
        elif renderer != 'native':
            print('--renderer must be native or emulated'); return 2
        cv.update({'game_data_root': os.path.join(ROOT, 'assets'), 'user_data_root': rs, 'log_file': log,
                   'gpu_backend': opt['--backend'], 'eamma_renderer': renderer})
        if renderer == 'native':
            cv['eamma_native_scale'] = opt['--scale']
        else:
            cv['resolution_scale'] = opt['--scale']
        if opt['--mods'].lower() != 'none':
            cv['mods_root'] = os.path.abspath(opt['--mods'])
        if script and opt['--skip-intros']:
            script = with_intro_skips(script, float(opt['--skip-intros']), run)
        if script:
            cv['input_script'] = script
        cv.update(sets)
        args = [os.path.join(rg, EXE)] + ['--%s=%s' % (k, v.replace('\\', '/')) for k, v in cv.items()]
        si = subprocess.STARTUPINFO()
        si.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        si.wShowWindow = 4  # SW_SHOWNOACTIVATE
        start = time.time()
        # Never steal the user's focus (user 2026-11-08: test windows popped up in front of their game). SDL3 reads hints from
        # the environment: no activation on show/raise, no topmost for the borderless fullscreen window.
        env = dict(os.environ, SDL_WINDOW_ACTIVATE_WHEN_SHOWN='0', SDL_WINDOW_ACTIVATE_WHEN_RAISED='0',
                   SDL_WINDOW_ALLOW_TOPMOST='0', SDL_FORCE_RAISEWINDOW='0')
        p = subprocess.Popen(args, cwd=rg, startupinfo=si, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        print('launched pid', p.pid, 'run', run); sys.stdout.flush()
        due = sorted(float(t) for t in opt['--shots'].split(',') if t.strip())
        last_due = due[-1] if due else None
        every = float(opt['--every']); nxt = every
        after = [float(t) for t in opt['--after'].split(',') if t.strip()]
        hits, max_hits, log_pos = 0, int(opt['--max-hits']), 0
        hwnd, n_shots, alive, last_watchdog, capped = None, 0, True, -1e9, False
        while time.time() - start < float(opt['--secs']):
            if p.poll() is not None:
                alive = False; break
            now = time.time() - start
            # Focus fix (2026-11-11): no SetForegroundWindow from here (a background process can't take the foreground
            # back, so the old give-back failed and the watchdog re-ran every 50 ms, ~16 SetWindowPos pairs a second).
            # Instead every game window is WS_EX_NOACTIVATE as soon as it is visible (re-checked each pass: the game may
            # recreate its window on a mode switch), and the watchdog acts at most every 3 s.
            n_na = no_activate(p.pid)
            if n_na and now < 120:
                print('%.1fs window(s) made non-activating: %d' % (now, n_na)); sys.stdout.flush()
            if not hwnd:
                hwnd = find_window(p.pid)
                if hwnd:
                    user32.SetWindowPos(hwnd, 1, 0, 0, 0, 0, 0x13)  # HWND_BOTTOM, NOSIZE|NOMOVE|NOACTIVATE
            elif now - last_watchdog >= 3 and (user32.GetForegroundWindow() == hwnd or
                                               user32.GetWindowLongW(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST):
                # Watchdog: the game window got in front later (fullscreen switch, device reset): drop topmost, bottom.
                last_watchdog = now
                user32.SetWindowPos(hwnd, -2, 0, 0, 0, 0, 0x13)  # HWND_NOTOPMOST
                user32.SetWindowPos(hwnd, 1, 0, 0, 0, 0, 0x13)   # HWND_BOTTOM
                print('%.1fs watchdog: game window was in front or topmost, sent to the bottom' % now); sys.stdout.flush()
            if opt['--on-log'] and hits < max_hits and os.path.exists(log):
                with open(log, encoding='utf-8', errors='replace') as f:
                    f.seek(log_pos); new = f.read(); log_pos = f.tell()
                for l in new.splitlines():
                    if opt['--on-log'] in l and hits < max_hits:
                        hits += 1
                        due = sorted(due + [now + t for t in after])
                        last_due = max([now] + due)
                        print('%.1fs log hit %d: %s' % (now, hits, l.strip()[-160:]))
            take = []
            while due and due[0] <= now:
                take.append(due.pop(0))
            if every and now >= nxt:
                take.append(now); nxt += every
            if take and (n_shots >= max_shots or free_gb(shots) < 3):   # runaway / disk guard: keep running, stop capturing
                if not capped:
                    print('%.1fs captures stopped: %s' % (now, '--max-shots %d reached' % max_shots if n_shots >= max_shots
                                                          else 'under 3 GB free')); capped = True
                take = []
            for t in take:
                try:
                    if hwnd and capture(hwnd, os.path.join(shots, '%03d_%07.2fs.png' % (n_shots, now)), fmt):
                        n_shots += 1
                except OSError as e:  # disk full (2026-10-30): skip the shot, keep running and closing the game
                    print('%.1fs capture failed: %s' % (now, e))
            if (end_after is not None and last_due is not None and not due and (hits or not opt['--on-log'])
                    and now >= last_due + end_after):
                print('%.1fs last planned capture done: ending early (--end-after, %.0f s of --secs saved)'
                      % (now, float(opt['--secs']) - now))
                break
            time.sleep(0.05)
        if alive:
            if hwnd:
                user32.PostMessageW(hwnd, 0x0010, 0, 0)  # WM_CLOSE: lets the log flush
            try:
                p.wait(timeout=15)
            except subprocess.TimeoutExpired:
                subprocess.run(['taskkill', '/PID', str(p.pid), '/T', '/F'], capture_output=True)
        print('%s after %.0fs (code %s), %d shots in %s' % ('ran' if alive else 'EXITED EARLY', time.time() - start,
                                                           p.poll(), n_shots, shots))
        sheet(shots, os.path.join(run, 'sheet.jpg'))
        if os.path.exists(log):
            bad = re.compile(r'\b(fatal|crash|exception|assert|unhandled)\b', re.I)
            lines = [l.rstrip() for l in open(log, encoding='utf-8', errors='replace') if bad.search(l)]
            print('log problem lines: %d' % len(lines))
            for l in lines[-10:]:
                print('  ' + l[:200])
        return 0 if alive else 1
    finally:
        if p and p.poll() is None:  # an error above left our own game running: close it (never another process)
            hw = find_window(p.pid)
            if hw:
                user32.PostMessageW(hw, 0x0010, 0, 0)
            try:
                p.wait(timeout=15)
            except subprocess.TimeoutExpired:
                subprocess.run(['taskkill', '/PID', str(p.pid), '/T', '/F'], capture_output=True)
        if rs and not keep_save:
            time.sleep(2)
            for d in os.listdir(rs):
                if d not in LINK_SAVE_DIRS:
                    shutil.rmtree(os.path.join(rs, d), ignore_errors=True)
        if rg and not keep_game and (p is None or p.poll() is not None):   # never pull files from under a live game
            drop_game(rg)
        if not parallel:   # no local Qwen beside another running game
            summarize(run)   # before the lock goes: Qwen must not load beside the next test's game
        if not parallel:   # --parallel never took the global lock (the caller holds a game slot)
            os.replace(LOCK, LOCK + '.done')


def summarize(run):
    """Token saver: local Qwen (local_ai.py sum) writes <run>/summary.txt; read it before grepping test.log. Triage only
    (it invents causes). Runs after our game closed and before the lock is freed (--free unloads Qwen so the next game
    gets the GPU; local_ai exits 3 while a game runs). Never fails the run (no Ollama, timeout, error -> no summary)."""
    log = os.path.join(run, 'test.log')
    # Local AI is paused (user, 2026-11-03: GPU bottleneck): EAMMA_LOCAL_SUMMARY=1 turns this back on.
    if not os.path.exists(log) or os.environ.get('EAMMA_LOCAL_SUMMARY') != '1':
        return
    try:
        r = subprocess.run([sys.executable, '-I', LOCAL_AI, 'sum', log, '--focus', 'errors, crashes, why the run ended',
                            '--free'],
                           capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=150)
        if r.returncode == 0 and r.stdout.strip():
            with open(os.path.join(run, 'summary.txt'), 'w', encoding='utf-8') as f:
                f.write(r.stdout)
            print('summary', os.path.join(run, 'summary.txt'))
    except Exception:
        pass


if __name__ == '__main__':
    sys.exit(main())
