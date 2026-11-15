# EN-3: D3D call census and Stop 1 (2026-11-15)

Build: `-DEAMMA_NATIVE_HOOKS=OFF -DEAMMA_NATIVE_CENSUS=ON` (`src/dev/ufc_d3d_census.cpp`, adapted from UFC's NR-6
census to EA MMA's addresses). Runs: `tools/run_shots.ps1` with `tools/selftest_scripts/boot_taps.txt` (EA logo ->
menus -> Emelianenko vs Couture fight), 150 s, `--eamma_d3d_census=true --eamma_d3d_census_every=300`, emulated
draw counts from `--trace_draw_ps=34B29856453FE506 --trace_draw_every=300 --trace_draw_frames=1` ("X of Y draws").
Logs: `shots/en3a` (command buffers on), `shots/en3d` (off). Not committed (shots/ is ignored).

## Result: Stop 1 is met by the numbers, and avoided by the game's own fallback

- **Fight frame, command buffers on (en3a, frames 3000-3900):** direct draws ~296, of them ~146 inside predicated
  tiling (1 BeginTiling a frame, 3 tiles), clears ~15, RunCommandBuffer 3.2 a frame holding **~58 predicated draws a
  frame** (first buffer: 14 draws, all predicated; type-3 ops include IM_LOAD_IMMEDIATE, so the buffers carry their
  own shaders). Command-buffer draws are ~15% of the real draws of a fight frame: over the guide's 5% Stop 1 line.
  Re-recording happens during play (0.2-0.8 recorded draw calls a frame).
- **The game can draw them directly.** sub_885A1FE0 is "record once, replay": object byte +24 = recorded; not
  recorded -> record (sub_885A1340) and replay (sub_885A1430); returns 1 when the replay ran. Its two callers
  (sub_885B4978, sub_885BFD18; render pass 4) call the direct draw sub_884C3368(this, 4) when it returns 0, the same
  path they take when the object has no command buffer (+192 / +168 null). `src/dev/eamma_cmdbuf_off.cpp`:
  `--eamma_command_buffers=false` returns 0 there.
- **Command buffers off (en3d):** 0 RunCommandBuffer, fight direct draws ~358 (+60), tiling draws ~195; emulated
  draws ~1060-1210 a frame as before; no crash in 150 s; fight picture complete (cage, crowd, mat, HUD, fighters,
  referee; `shots/en3d/105.png` vs `shots/en3a/135.png`). Which objects the buffers held is not identified (the
  crowd is the likely one); the picture shows nothing missing.
- Native plan: run with `eamma_command_buffers=false` (EN-5 sets it with the renderer), so no command-buffer replay
  (UFC's `ufc_cb_probe.cpp`) is needed. Keep the replay code as the fallback if a difference shows up later.

## Other answers
- **Device offsets proven at run time:** VS +0x3248, PS +0x3244, decl +0x2ED8 held the last setter's object at every
  draw (85,192 of 85,192 in en3a). Recording command buffer +0x35A0 behaves (recorded draws seen only while set).
  Front / back buffer +0x3AC0 / +0x3AC4 read 0 at the first draw (open item 5/6 stays open: the frame end swaps its
  own texture).
- **Census vs emulated (A/B):** with command buffers off, fight frame: direct 358 + 2 x 195 extra tile passes + 15
  clears + 258 GPR flush points = ~1021 against the emulated ~1058 (3.5%). With them on the gap is ~12% (replayed
  draws inside the tile passes are not all seen by the ring scan). Menus: ~140-410.
- **GPR flush points:** SetShaderGPRAllocation ~11 calls a frame, 24 point-list draws each (~260 "draws"): the pipe
  drain UFC's NR-6b found; the native hook returns without emitting them (as UFC).
- **Hidden draws:** D3D-internal draw writers (sub_88320378, sub_8830D5C0, sub_8830D718, sub_8832A148) were never
  called in these runs; no canned point-list PM4 in the DLL image. Resolves ~21 a frame in the fight.
- **Calls a frame, fight:** SetTexture ~1500, SetStreamSource ~235, DrawIndexedVertices ~168, BeginVertices ~92 (13
  of them from DrawVerticesUP), DrawVertices ~20, SetVS/SetPS ~122, Resolve ~21, SetRenderTarget ~9, Clear ~8.
  **SetVertexShaderConstantF / SetPixelShaderConstantF: 0 calls.** Shader constants never go through the public
  setters: the RNA/FXLite path writes them another way (direct device writes or its own SET_CONSTANT packets). The
  native constant upload (UFC reads the device's +0x780 / +0x1780 blocks at draw time) must be checked against this in
  EN-5: if the game writes constants straight into the ring, the device blocks may be stale.
- No Z pass, no queries.
- Census bug fixed on the way: its PM4 walk followed a garbage INDIRECT_BUFFER address and faulted (runs en3b, en3c,
  minidump symbolized to `ScanPm4Va`); it now only reads mapped guest ranges.
