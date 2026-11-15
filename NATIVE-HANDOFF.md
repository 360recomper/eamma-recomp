# EA MMA native renderer: handoff (2026-11-15)

Read this first in a new session, then `NATIVE-ISSUES.md` (open issues + mistakes), then the FINDINGS file for the
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

## Next steps (suggested order; see NATIVE-ISSUES.md section E for the advisor's reasoning)
1. Done today: A1 (loop start/step), A2 (gone with A1), A5 (stale geometry: every-draw compare is the default),
   A3 translated (run-time loop constants), setTexLOD, B7 off. Next: see light shafts on screen (find a scene that
   draws PS 1CCBA38A), A4 broader A/B (career, create-a-fighter, other venues), MSAA (the main scene is 4x on console).
2. Depth bias: never fed (pipeline/pipeline_cache.cpp:105), but no logged draw enables polygon offset (PA_SU_SC_MODE_CNTL
   is only 0x18000 / 0x18002 in every draw log so far). Feed it when a scene shows bits 11/12.
3. (done: capture drops oldest; depthFlip gated on VTE z-scale.)
4. A2 halo: skip skin PS (bloom?) / skip DOF PS / check half-pixel offset; halo width tells which.
5. B7: done, eager write-back off by default (n43nowb).
6. Census as a run-time switch: deferred. The native hooks already own 14 of its 26 counted functions plus
   Swap/Clear/Resolve/draws, so its counters would have to move into those hooks (hours, low value for now).
7. A3 light shafts (integer constants), then speed, scaling, the guide's Week 2.
