# EA MMA native renderer: finish design (2026-11-15)

Work plan for agents taking the native renderer from "fights render" to "release". Based on a read-only Fable
advisor review of branch `native` @ 5f85caa; its key claims were spot-checked (fight presents/s in n73default /
n75scale2 logs, `eamma_native_keep1x` default, `EAMMA_SOURCES` without `gpu_preference.cpp`, alpha-to-coverage forced
off). Everything marked *guess* is a lead, not a fact.

Read first: `NATIVE-HANDOFF.md` (how to build/run/diagnose), `NATIVE-ISSUES.md`, then the package below you pick up.
Guide: `E:\ClaudeGame\UFC-native\docs\NATIVE-RENDERER-GUIDE.md` (ch. 10-17). Commit locally after every pass line,
never push. Rumble Roses has GPU priority (`tools/run_shots.ps1` refuses to start while it holds the GPU).

## 1. What changed in the plan, and why

1. **Fight frame rate first.** In the fight the native renderer presents **48.8/s** at both 1x and 2x (n73default,
   n75scale2); menus are 30/s. Emulated runs the fight at a steady 60 (NOTES.md:65). Same rate at 1x and 2x means
   it is not GPU cost. The "30 fps held" proof (n72perfA/B) stopped before the fight. *Guess:* the game spins in
   its own Swap/vblank wait (`[nperf]` shows gd3d 18-22 ms with gwait 0 in intros); UFC's Swap-poll sleep (NR-P6d)
   hooks a UFC address and has no EA equivalent mapped. This may also mean game speed is off, which would skew every
   A/B by timing.
2. **B10 is a real bug, and comes from UFC's keep-1x list.** `ufc_resources.cpp:58` keeps `1024x512,384x64,64x64`
   at 1x — UFC's face liquid map and ambient cube. In EA MMA, 1024x512 is the fighter composite; its mips 1-3 are
   resolved from a scaled 1024x1024 scratch surface (the mixed-scale path, `hooks/draw.cpp:1407-1455`, stretches the
   whole surface), and the CPU reads these composites back. At any scale other than 1x, wrong mips reach guest memory.
   Also: the "16F always 1x" rule keeps EA's 480x270 16F scene buffer at 1x while its depth scales (*guess:* the
   depth/DOF/SSAO input).
3. **Alpha-to-mask is dropped.** One fight draw a frame sets `RB_COLORCONTROL` alpha-to-mask (`0x7E000016`, VS
   C05918844A3CF2C8 / PS B61F648032C00706); `draw_bindings.cpp:186` forces alpha-to-coverage off and every surface is
   1 sample, so it draws opaque. *Guess:* hair/beard cards; the two test fighters are bald.
4. **A2 halo is probably gone** (no halo in n73default/015 vs en3d/015); ISSUES still lists it open. In the same pair
   the **menu crowd behind the fence is missing natively** (new lead A6; may be fade timing).
5. **Release items look present but are not wired:** `src/gpu_preference.cpp` is not in `EAMMA_SOURCES`
   (`CMakeLists.txt:14`), no `config/native/pipelines_seed.bin`, shader cache built only by a Python tool, no
   launcher/fallback test, FSR never run.
6. **Testing reach is the bottleneck.** Only `boot_taps.txt` exists; career, create-a-fighter, venue select and
   replays need new scripts or one human-driven session.

Cut/defer: building the depth-bias feed (watch item only; the coverage ledger flags it), census as a run-time switch,
real MSAA (optional, last; UFC shipped at 1 sample and 2x scale already supersamples).

## 2. Order and parallelism

S = static (no game runs), R = needs game runs. Packages that touch `src/reblue/gpu/hooks/draw.cpp`,
`src/native/ufc_resources.cpp` or `src/reblue/gpu/resolve.cpp` run one at a time.

```
WP0 docs fix (S) ──┐
WP4 static batch (S) ─────────────────────────┐
WP3 coverage ledger (S code) ─┐               │
WP1 fight pacing (R) ──┬──────┤               │
WP2 keep-1x + B10 (R) ─┴──────┴─► WP5 mode sweep (R) ─► WP6 alpha-to-mask (R) ─► WP8 perf ladder + soak (R) ─► WP9 MSAA (optional)
WP7 release set (mostly S; CMake/tools/exe only) — any time, parallel with the rest
```

- Only one package with game runs at a time (one GPU, one game window).
- WP0, WP4, WP7 and the code half of WP3 can go to parallel sessions: they touch no shared files with WP1/WP2.
- Batch all new cvars/diagnostics of a session into one DLL build (each rebuild of the DLL target is ~10 min).

## 3. Work packages

Every package ends with: pass line met, docs updated (ISSUES + HANDOFF in the same commit), local commit.
Every new code path gets an off switch (guide: "every new path gets an off bit"); grep the cvar help text for free
bit numbers before taking one (guide trap 28).

### WP0 Docs agree (S, 15 min)
- NATIVE-HANDOFF.md "2560x1440, 30 fps" and FINDINGS-a1-skin.md:125 "30 fps held": true for menus/intros only; fight
  is 48.8 presents/s, unexplained (WP1).
- NATIVE-ISSUES.md: A2 -> "probably gone, confirm with one pair (WP5)"; B10 severity raised (wrong mips written back);
  add A6 (menu crowd), A7 (alpha-to-mask dropped), B11 (UFC keep-1x list / 16F rule on EA surfaces); mark section E's
  "reversed-Z exact" as unverified for viewport-transform-off draws; note profile-save crash (D) as a release risk.
- NATIVE-PLAN.md Week 2: "copy every UFC default" applies to speed defaults only, not to keep-1x, the 16F rule or
  Swap-poll (game data/addresses).
- Pass: the three docs say the same thing about every issue.

### WP1 Fight pacing (R, 2-3 runs; first)
- Goal: explain 48.8 presents/s; native fight runs at emulated speed.
- Read: `src/native/ufc_frame_diag.cpp`, guide ch. 12 (NR-P6d Swap-poll sleep), Swap `sub_8831C250` and its wait
  loop in `generated/mmadllzf_xex`.
- Run 1: `tools/run_shots.ps1 -Tag p1 -Secs 180 -Every 15 -Script <abs path>\tools\selftest_scripts\boot_taps.txt
  -Extra @("--eamma_renderer=native","--eamma_native_perf=true","--svr_frame_diag_ms=45")`. Read `[nperf]` in fight
  seconds: `work` < 16.7 ms at 48.8/s = pacing; > 16.7 ms = cost, and the zone names it.
- Run 2: emulated, same script, frame count per second in the fight (real reference, not NOTES.md's memory).
- If spinning: find EA's poll loop, hook it with the 1 ms sleep, keep UFC's off bit 524288 in `eamma_native_off`.
- Pass: native fight fps equals emulated in the same scene, and the fight clock advances at the same rate on both.
- Docs: new `FINDINGS-perf.md`.

### WP2 Keep-1x list and B10 (R, 3-4 runs, no rebuild until decided)
- Goal: EA's own 1x list; zero mixed-scale stretches.
- Read: `src/native/ufc_resources.cpp:281-470`, `src/reblue/gpu/hooks/draw.cpp:1385-1460`, guide ch. 11, n75/n76 logs.
- Run A (2x): `--eamma_native_scale=2 --eamma_native_keep1x=1024x512,1024x1024,384x64,64x64` (scratch 1x too: exact
  console copies). Run B (2x): `--eamma_native_keep1x=64x64` (composites scaled). Add `--eamma_native_wb_dump=N` and
  compare a level-1 mip dump against a 1x run.
- Decide; set the default and rewrite the help text with EA's reasons per entry. Put the 16F and stain rules behind
  `eamma_native_keep1x_16f` (default decided from a 2x pair of the 480x270 pass). Only if no list works: write a
  scaled region blit in `resolve.cpp`.
- Pass: no "whole surface stretched" lines at 1.5x, 2x, 3x; mip dumps match 1x.
- Docs: ISSUES B10/B11; "Scaling" section in a FINDINGS file.

### WP3 Coverage ledger (S code, then used in WP5)
- Goal: know which shaders/states were drawn and checked.
- File: `src/native/ufc_draw_log.cpp` (beside the `SVR_LOG_PS` watch ~line 149).
- Cvar `eamma_native_coverage` (default false): one line per new VS/PS pair, new target size/format, new resolve
  signature, and any draw with alpha-to-mask or polygon offset (PA_SU_SC_MODE_CNTL bits 11-13) — the latter replaces
  building the depth-bias feed on spec.
- `tools/nr_coverage.py`: merge run logs, report "N of 240 pixel shaders drawn" and list the rest.
- Pass: a boot_taps run prints the ledger; the tool reports the count.

### WP4 Static batch (S; parallel with everything)
One FINDINGS section each, leads marked as leads:
- Depth-bias setters in `generated/mmadllzf_xex`: does any caller pass non-zero?
- Where the light-shaft technique (PS 1CCBA38A) is enabled: venue/walkout data or a setting.
- HLSL of PS 60D26E98 (480x270 16F pass: what it writes) and PS B61F6480 (alpha-to-mask draw: what mesh).
- MSAA audit: MSAA bits in `gpu/d3d.h` surfaces; what breaks with samples > 1 in `present.cpp:124,457`,
  `ufc_resolve_writeback.cpp:1264`, `resolve.cpp:274,584`, `surface_registry.cpp:81`, `hooks/state.cpp:123`.

### WP5 Mode sweep: A4, A3, A6, A2, ch. 10 (R; after WP1-3)
- Write one script per mode (career start, create-a-fighter, venue select, replay); prove each on emulated first.
  Or ask the user for one ~20-minute native session with `--eamma_native_coverage=true` and shots every 5 s.
- Content-matched pairs (native vs emulated, 1 s shots around anything called a bug), opened and looked at.
- A2: one menu pair; strike if no halo. A6: 1 s shots 8-20 s on both; if still missing, draw log at that second.
- A3: run the scene WP4 found with `SVR_LOG_PS=1CCBA38AC6B29550`.
- Ch. 10: create-a-fighter and career with `eamma_native_g4=log`; created fighter's face/body, portraits and save
  thumbnails correct after save and reload.
- Budget: 3 runs per mode, then write findings.
- Pass: every pixel shader drawn in the sweep is in at least one checked pair; no shader miss, dropped draw or
  "partial resolve ... not done" lines in any mode. One ISSUES row per new defect, each with its shot pair.

### WP6 Alpha-to-mask fallback (R, 2 runs; after WP4 names the mesh)
- Files: `src/reblue/gpu/draw_bindings.cpp:170-190`, `pipeline_state.h`.
- At 1 sample: alpha test or screen-door dither for alpha-to-mask draws; off bit in `eamma_native_off2` restores
  today's behaviour.
- Pass: a fighter with hair matches emulated by eye.

### WP7 Release set (mostly S; parallel; CMake, tools, exe only)
- Add `src/gpu_preference.cpp` to `EAMMA_SOURCES`; fix its ufc3 header comment. Check: `dumpbin /exports eamma.exe`
  shows both Optimus/PowerXpress exports; adapter log line names the dedicated card.
- PSO seed: `tools/nr_pso_seed.py` -> `config/native/pipelines_seed.bin`.
- Player-PC shader cache builder (BGFA unpack of `shader_big.ast`, no Python, no xex decrypt needed).
- Device-fail test cvar (port `native_test_device_fail`) and check exit code 3 falls back to emulated.
- FSR: one run with the window larger than the picture, `--eamma_native_upscale=fsr` vs `bilinear`.
- **User decision:** does EA MMA get a launcher? (plan says "only if").

### WP8 Performance ladder and soak (R, 4-6 runs; after WP1, WP2)
- Guide ch. 12 "For a new game" checklist. Ladder 1x / 1.5x / 2x / 3x with `[nperf]` and `[hitch]`.
- A/B `eamma_native_geometry_every_draw` in the fight (ghash ~1 ms in intros).
- 10-minute soak: VRAM flat (`g_tiled_extent`, `s_steady` are never pruned — prune if they grow).
- One run with `--eamma_native_check_constants=true` through a fight (proves B1/B2: partial uploads never miss a
  captured register).
- Pass: target fps in fight seconds at 3x; every frame > 45 ms explained; VRAM flat.

### WP9 MSAA (optional, R; last, only on the user's go)
- Behind `eamma_native_msaa` (default off), 4 samples on the main tiled surface, using WP4's audit.
- Pass: boot to results clean at 1x with 4 samples, edges match emulated, cost recorded on the ladder.

## 4. Process rules (to stop sessions stalling)

- **One hypothesis per run, written down before the run** (expected log line or shot). If the result can't separate
  two causes, the run was badly designed.
- **3 runs per question.** After the third without a separator: stop, write what was ruled out, switch to static
  work or ask for an advisor read.
- **Geometry and inputs before shader math** (lesson C11): when passes of the same mesh disagree, run the check
  cvars first.
- **Stay on `build.bat native`**; never switch to census mid-session.
- **Handoff at every commit, not at session end.** NATIVE-HANDOFF.md keeps three blocks: *true now*, *in flight*
  (run tag + what it should show), *next command to type*. A session that hits its context limit then loses nothing.
- **Close issues everywhere in one commit** (ISSUES + HANDOFF + FINDINGS).
- **Check every run log for:** exit code, empty `native_stderr.txt`, shader miss count, `presents/s`,
  "whole surface stretched", "partial resolve ... not done", "constant capture full".
- Env-only diagnostics (`SVR_PRESENT_TARGET`, `SVR_LOG_PS`, `SVR_NO_HALF_PIXEL`, `EAMMA_NO_DEPTH_TEST`) are fine on
  this desktop; convert to cvars only when a package needs them on another runner (guide trap 12).

## 5. Open release risks not owned by a package
- Intermittent profile-save crash (~15 s, null read +0x134/+0x148 in rexruntime), seen on both renderers.
- Reversed-Z flip untested on a draw with the viewport transform off.
- B9 front/back buffer offsets assumed; matters only if a mode presents wrongly.
