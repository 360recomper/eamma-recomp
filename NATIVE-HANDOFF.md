# EA MMA native renderer: handoff (2026-11-15)

**Remaining work is planned in `NATIVE-FINISH-DESIGN.md` (work packages WPF, WP0-WP12, order, process rules); start
there.** Read this first in a new session, then `NATIVE-ISSUES.md` (open issues + mistakes), then the FINDINGS file for the
step you touch. The user wants work to continue step to step without asking for a go-ahead; commit locally, never push.

## Where things are
- Repo `E:\ClaudeGame\EAMMA Recomp`, branch `native` (main line `master` = emulated only). Local only, no remote.
- Plan: `NATIVE-PLAN.md` (EN-0..EN-5 done, "Progress" at the end). Guide it follows:
  `E:\ClaudeGame\UFC-native\docs\NATIVE-RENDERER-GUIDE.md` (chapter 9 = bug classes, 10 = resolves/write-back).
- Code came from UFC's `claude/native-renderer` branch (0334642): `src/reblue` (generic), `src/native` (UFC-named
  files, EA addresses in `ufc_d3d_names.h` / `ufc_d3d.h`), built INTO THE GAME DLL target `eamma_MMADLLzf_xex`.
- EA-specific native code: `src/native/eamma_bridge.cpp` (exe <-> DLL exports), `src/native/eamma_constant_loads.cpp`
  (GPU-written constants), tiling in `hooks/draw.cpp` (EammaBeginTilingObserved) + `ufc_resources.cpp`
  (SvrNoteTiledSurface), DrawVerticesUP hook (draw.cpp), DEC3N + g_DepthFlip in XenosRecomp
  (`patches/xenosrecomp-eamma.patch`), DXN/DXT5A in `native_texture_mirror.cpp`, `src/dev/eamma_cmdbuf_off.cpp`.

## Build and run
- `build.bat native` (renderer; hooks gated at run time), `build.bat census` (EN-3 call census, emulated),
  `build.bat nonative`. Switching between them rebuilds the whole DLL (~10 min). The scratch build scripts used in the
  last session just ran cmake with the same -D flags.
- Shader cache: `python -I tools/nr_build_shader_cache.py` (~4 s; 417/418). Needed after any XenosRecomp change;
  XenosRecomp itself is rebuilt by the native build (ExternalProject). `third_party/plume` + `XenosRecomp` are not
  committed: copied patched from UFC's tree; `patches/xenosrecomp-eamma.patch` is our delta, regenerate it with
  `git diff --no-index` against `E:\ClaudeGame\UFC Recomp\third_party\XenosRecomp` after editing.
- Test: `tools/run_shots.ps1 -Tag X -Secs 150 -Every 15 -Script tools/selftest_scripts/boot_taps.txt
  -Extra @("--eamma_renderer=native")` -> `shots/X/` (screens + run.log). Boot -> menus -> fight (~45 s native) ->
  highlights. Respects Rumble Roses GPU priority.
- Emulated reference runs: `shots/en3a` (command buffers on), `shots/en3d` (off). Native good run: `shots/n33`.

- Laptop farm (WPF, 2026-11-15): `py -I tools/eamma_selftest.py <run> --script <script> [--renderer native|emulated]
  [--set k=v]` = UFC-style self-test (runs in E:\ClaudeGame\EAMMA-tests\selftest\<run>, sheet.jpg). `py -I tools/eamma_farm_job.py
  <tag> [--queue]` freezes the build to TestFarmuilds\eamma-<tag> and writes a native + emulated job pair (default
  jobs\hold\eamma-staged). Laptop setup: E:\ClaudeGame\TestFarm\EAMMA-JOBS.md. Results land in TestFarmesults\<run>.
  Laptop = 1x only; scaled and perf runs stay on Chi.

## Diagnostics that work
- `--eamma_native_draw_log_at=<s>`: one frame draw by draw (+ first vertex and VS c0..c7, c156..c159 for indexed
  draws). Summary: `python -I tools/nr_drawlog_summary.py shots/X/run.log`.
- `SVR_PRESENT_TARGET=<va hex>`, `=tiled` or `=<W>x<H>` (env): present a resolve target instead of the front buffer
  (`256x256` = the skin SSS map). Colours are as the present path shows them, not raw values.
- `SVR_LOG_PS=<hash>` (env): a line per second while that pixel shader is drawn (find the scene using it).
- `XENOS_RECOMP_HLSL_OVERRIDE=<dir>` (env at cache build): replace one shader's HLSL (`<dir>/<HASH>.hlsl`) to instrument it.
- `--eamma_native_skin_black_slots=<mask>`: null the given texture slots for the skin pixel shaders (bisection).
- Run scripts need an absolute `-Script` path (the game's working directory is the build folder).
- `EAMMA_NO_DEPTH_TEST=1` (env), `SVR_SAMPLE_STACKS=<ms>` (stack samples to native_samples.txt).
- Census build: `--eamma_d3d_census=true` (calls/draws per frame, shader-cache key check, device constant-block
  change counts, ring packet log between draws).
- Crash: minidump in the shot folder; module+offset with scratch `mdmod.py` (simple minidump module-list reader),
  then `llvm-symbolizer --obj=out/build/win-amd64-relwithdebinfo/eamma_MMADLLzf_xex.dll --relative-address`.
- HLSL of every shader: run `out/build/.../XenosRecomp.exe <containers dir> <out.bin> <shader_common.h> <hlsl dir>`;
  containers from `python -I tools/eamma_shader_extract.py <dir>`.

## Key facts learned (don't re-derive)
- EA's constants: c156..c187 (both stages) via LOAD_ALU_CONSTANT from memory (sub_883198E0), c0..c155 via ring
  memcpy (sub_88319858); SetVS/PSConstantF never called; bool setters are used (~100/frame).
- Viewport depth range is inverted (MinZ 1, MaxZ 0) with GEQUAL; native flips z in the VS (g_DepthFlip).
- 3D scene is drawn with predicated tiling (3 tiles of 1280x256) every frame.
- Command buffers carry ~15% of fight draws; natively they are turned off (game's own fallback path).
- Present = Swap (no Present in this XDK); device 0x5F00, SvR-2010 offsets.

## State (2026-11-15 evening; keep these three blocks current at every commit)

### True now
- **WP0 passed** (950e8b2). **WP3 passed**: `--eamma_native_coverage=true` prints `[cover]` lines,
  `py -I tools/nr_coverage.py <run dir>...` reports them (run b1: 70 of 240 pixel shaders drawn in boot -> fight).
  `config/native/shader_inventory.txt` (the list of all 240) is not made yet: `nr_coverage.py --make-inventory <hlsl dir>`.
- **WP4 passed**: FINDINGS-wp4-static.md (depth bias closed, light-shaft scene, SSAO pass, hair cards, MSAA audit).
- **WP2 mostly passed**: EA's keep-1x list is the default (`1024x512,1024x1024,480x270,64x64`), 16F rule behind
  `eamma_native_keep1x_16f`. Runs w2a and w2b (2x, w2b with the final list): zero stretch lines, no "not done", no capture-full, empty
  stderr. Not run: 1.5x, 3x, the mip dump compare. w2b's 60 s shot shows a dark swirl on the mat behind the pause
  menu and 75 s / 105 s fall on menu fades: compare with emulated before calling 2x clean.
- **WP1 open, mostly closed**: fight 48.8 -> 60 presents/s in most seconds (run w2b: 40 of 63 fight seconds at 59+, mean 56.5), emulated 60. Defaults changed: host limiter off
  (`eamma_native_fps_limit=0`), music-status poll every 30 calls (`eamma_xmp_status_every`). FINDINGS-perf.md has the
  facts, what was ruled out and the next leads. `[nperf]` shows the fight only with `--eamma_native_perf_min_draws=100`.
- **WP6 built, not judged**: alpha-to-mask draws (hair cards) get an alpha test at 0.5; off bit `eamma_native_off2` 2.
- **WP7 mostly passed** (exports present in eamma.exe; run df1: forced device failure -> exit 3 + native-error.txt;
  run b2 loaded the PSO seed). Still open: FSR run, exit 4 check, a launcher-built cache loaded by the game. Built: gpu_preference in the exe, device-fail path (`src/eamma_renderer_fail.*`, exit 3/4,
  `native-error.txt`), `config/native/pipelines_seed.bin` (117 records; run b2 logged the seed line), C# shader cache
  builder byte-identical to the Python one. FINDINGS-wp7-wp10.md lists the post-build checks. FSR run not done.
- **WP10 port built**: `launcher/` compiles, its tests pass; not run against the real game or a disc image.
- **A6 fixed** (crowd draws; run c2): XenosRecomp now honours texel-coordinate fetches. The shader cache must be
  rebuilt after this commit (`py -I tools/nr_build_shader_cache.py`), and `patches/xenosrecomp-eamma.patch` was regenerated.
  A8 (pause menu highlight bar) is new.
- **Locked fighters unlocked by a hook** (`src/eamma_unlock.cpp`, `eamma_unlock_fighters`, FINDINGS-unlocks.md): run u1
  shows the hook firing at fighter select; the user will play-test picking them. Title updates #1/#2 are code-only
  patches and are not applied (FINDINGS-wp7-wp10.md "Decisions"). Launcher questions were decided there too.
- Unattended tools pass `--eamma_native_error_box=false` (eamma_selftest.py). Frozen builds older than today do not
  know that cvar.

- **Laptop lap2 jobs (1x, build of commit 363a363 + unlock hook), all exit 0, empty stderr, no stretch / not-done / miss:**
  `eamma-lap2` (boot -> fight -> results: crowd present, fight 60 fps in 16 of 35 fight seconds, mean 54);
  `eamma-lap2-down1` = MMA 101 tutorial in a ring venue (lead: in the submission mini-game emulated is much darker
  with a vignette; shots are not at the same moment, pair them by content before calling it a defect);
  `eamma-lap2-down2` = Career -> create-a-fighter screens, native matches emulated; `eamma-lap2-down3` = Xbox Live
  error dialog, same on both. Scripts: `tools/selftest_scripts/laptop/menu_down{1,2,3}.txt`. Coverage over the four
  native runs: 84 of 240 pixel shaders.
- **Twitching / seam cracks reported by the user (bodies splitting at seams, black flicker, referee head detaching):
  cause found, fix built, user confirmation pending.** Not from today's changes (old pacing and 1x both showed it).
  A burst of 5 shots a second on the main menu (run g1, `tools/selftest_scripts/menu_idle.txt`) caught black cracks
  on Fedor in 2 of 24 frames: the A5 symptom. The game's job threads are still writing the skinned buffers when
  the draw is issued, so the copy taken at the draw can be torn. Fix: `SvrGeometryLateFixup` (ufc_geometry.cpp,
  called from Present before the list is closed) re-compares each dynamic ring copy at frame end and refreshes it
  in place; cvar `eamma_native_geometry_late_fix` (false = old path). Run g2: "312 of 5511 dynamic copies refreshed
  in 600 frames", no cracks seen in 64 burst frames. Holes: a buffer copied twice in one frame keeps its earlier
  copy as taken; command-list chunks already sent by the list split (`eamma_native_list_split_us`) may have been
  read. Stills every 5-15 s cannot show this class of bug: use bursts.
  **Second round (the user still saw it):** the cvar is now an int, default 2 = every copy of a buffer made in the
  frame gets the final content (shadow / SSS / main passes agree), and `eamma_native_list_split_us` defaults to 0 so no
  part of the frame is sent before the refresh. Run g3 (fight burst, 64 frames at 4 a second): 457-665 of 7-15 thousand
  copies refreshed per 600 frames, no torn body seen at thumbnail size. User confirmation still pending. If it
  persists: (a) copies made on the static-buffer path (first two changes, or over 4 MB) are not tracked, (b) resolve
  write-backs still send the list mid-frame, (c) it may be a different bug: get a burst of the exact scene.
  **Third round (2026-11-21; the user still saw parts jumping/detaching, constantly):** the same timing gap exists for the
  shader constants the game loads from memory (GpuLoadShaderConstantF4Pointer: per-object matrices and bone palettes; the
  GPU reads them when it runs the draw). Run g4: in menus 0 changed after the draw, in the fight 31188 of 251967 uploads
  (~152 registers each) changed between the draw and Present. Fix: `ConstantLateFixup` (constant_buffers.cpp, called from
  Present after the geometry fix) re-reads them and refreshes the upload; cvar `eamma_native_constant_late_fix` (true).
  Burst frames look normal; stills can't show a twitch, user play test pending. Unknown: whether the refreshed content is
  frame N's final matrices or frame N+1's written early (if the game single-buffers them).
  **Fourth round (same day, user still saw black seam lines / parts separating / referee head):** hole in the vertex
  path: a late-written buffer's first use in a frame often still matched last frame's hash, so it settled into an
  untracked host buffer, and the next change counted as the first (changes < 2, untracked again). Now any buffer
  rewritten in the last 60 frames always takes a tracked ring copy (`late_tracked`, ufc_geometry.cpp). Run g5: geometry
  late fix 2205-4864 refreshed per 600 frames (g4: 814), fight 48.8 presents/s, burst frames clean. User play test pending.
  User: "way less issues" on that build. Then runs g6/g7 with `tools/selftest_scripts/fight_burst.txt` (boot taps up to
  42 s, then stick + strikes, no START: the old script paused the fight every 4 s): probe
  `eamma_native_geometry_late_probe_us=2000` found 0 of ~80k refreshed buffers changing in the 2 ms after the refresh (the
  jobs finish before Present), and the new per-600-frame line "geometry untracked copies of rewritten buffers" is 0 in
  the fight (one idle case per minute). Known holes left: a mesh idle > 60 frames whose first use in a frame comes before
  the job writes it (one stale frame when it starts moving, e.g. the referee at the introductions); constants were not probed.
  **Fifth round (Gemini 4 Neon review, HANDOFF-gemini-a9.md):** (1) `NoteLateConstants` snapshotted live guest memory
  instead of the uploaded bytes, so a matrix finished between the copy and the snapshot looked fresh: now snapshots
  `StageConstantCache::raw` (run g8: 49-60k late constant refreshes per 600 fight frames, was ~20k). (2) the frame-end
  refresh never renewed a buffer's tracking when only it saw the change: it now sets `rewritten_frame`. (3) tracking
  window 600 frames (`kLateTrackFrames`), first content of a buffer also takes a tracked ring copy (new meshes, referee).
  (4) both refreshes also run in `SubmitOpenListLocked` (mid-frame submits by resolve write-back). Run g9: fight 60 fps
  every second. Gemini's lock-race point was wrong (geometry copies run under state().mutex, draw.cpp:217). Its
  non-timing leads (depth bias, keep-1x seams, CPU-written texture staleness with `eamma_native_g4=log`) are untested.

### In flight
- Nothing queued on the laptop. The build in out/build has all of the above.

### Next command to type
1. WP1 remainder, then WP5. Reference run (2x, all diagnostics):
   `py -I tools/eamma_selftest.py <tag> --script tools/selftest_scripts/boot_taps.txt --renderer native --scale 2
   --secs 150 --every 15 --set eamma_native_draw_log_at=72 --set eamma_native_perf=true
   --set eamma_native_perf_min_draws=100`: (= w2b). Its draw log has the crowd draws (VS 19010A0F...) with their `ib` and `vb fetch` lines.
2. WP1 lead 1 (hook the guest fence waits with the `gwait` zone), FINDINGS-perf.md.
3. WP5 mode scripts (career, create-a-fighter, venue select, replay) or ask the user for a human session; pick a
   long-haired fighter for WP6; the career gym for light shafts (A3).
4. WP8 ladder after WP1; WP10 first-run test with the user's disc image; WP9 only on the user's OK.
