# EA Sports MMA - native renderer plan

Status 2026-11-15 (branch `native`): EN-0 to EN-5 done: menus and fights render natively. Next: shading A/B (Week 2). See "Progress" at the end. Source: `E:\ClaudeGame\UFC-native\docs\NATIVE-RENDERER-GUIDE.md`
("the guide"; chapter numbers below are its chapters). Port **UFC's tree** (`E:\ClaudeGame\UFC Recomp`, native code in
`src/reblue/` + `src/native/`), not SvR's, as the guide says (ch. 1). Written 2026-11-14 while this tree first reached a fight on
the emulated renderer (NOTES.md).

## What is different about EA MMA (read first)

These change the guide's method; everything not listed here follows the guide as written.

1. **Two guest modules.** `default.xex` is a 626 KB launcher; the game, its D3D library and its shader compiler are in
   `MMADLLzf.xex.dll` (base 0x88000000), recompiled as a separate host DLL (`eamma_MMADLLzf_xex`, `generated/mmadllzf_xex`).
   UFC's hooks work because a local definition of `sub_XXXXXXXX` / `__imp__X` in the exe wins over the recompiled one at link
   time. Here the D3D functions live in the DLL target, so **the hook sources must be compiled into the DLL target**, not the exe
   (or the SDK's hook mechanism for module targets must be found first). This is the first thing to prove, before any port work:
   one observe-only hook on a DLL function (e.g. the present) that logs a line. If it can't be done cleanly, it is a go/no-go item.
2. **Different engine.** EA Tiburon's RNA engine (Fight Night Round 4 lineage), not Yuke's. None of UFC's game-side knowledge
   carries over: device struct, hook addresses, bound-texture table offset, keep-1x size list, game clock hooks, CAF write-back.
   Treat chapter 2's "UFC-specific" list as "redo all of it".
3. **Shaders.** The DLL links the D3DX9.1 effect compiler, FXLite and the Xbox 360 Shader Assembler 2.0.11164.0
   (strings at 0x8820DE60, 0x8822FC94). `data\xenon-gen\bigs\shader_big.ast` (615 KB, BGFA archive; UFC's `tools/eamma_read.py`
   fails on one entry, so the format differs a little) and "RNAFX Compiled shader Version %d" point to precompiled RNAFX shaders,
   with run-time compile as a fallback (`errorMissing.fx`). NR-1 must answer: does every CreateVertexShader/CreatePixelShader in a
   normal run get its microcode from `shader_big.ast`, or are some compiled at run time? If any are compiled at run time, the cache
   key (XXH3 of the guest container) can only be collected by a dump at CreateShader in a run, not by a disc scan.
4. **Command buffers.** Strings `GPUCOMMANDOP_LOAD_SHADER`, `..._EVENT_WRITE_SHADER` and the D3D hang text about `D3DCommandBuffer`
   suggest the game records or builds GPU commands itself. Stop 1 (ch. 6: draws through command buffers over ~5% of a frame) is a
   real risk here, so the census comes before any device work.
5. **Machine rule.** Rumble Roses has GPU priority on the desktop (global rule). EA MMA has no exception like SvR07's or a second
   PC like UFC's: native runs here must check `rumble_roses_xx.exe` / `RumbleRosesRecomp\queue-logs\selftest.lock` first and stay short,
   or get a TestFarm job tool of their own (UFC's `nr_farm_job.py` holds UFC paths).

## Before day 1 (user decisions)
- [ ] OK for GPL-3 in this project (re:Blue / nefariousjosiah credit, guide ch. 1, 18). This repo stays local: no push, no GitHub repo.
- [ ] Native on its own branch (`native`) in this repo; default stays emulated.
- [ ] Cvar prefix `eamma_`; keep UFC's `ufc3_native_off` bit numbers under `eamma_native_off` / `eamma_native_off2` (ch. 16).
- [ ] Which machine runs game tests (see 5 above).

## Week 1 (one item a day, each with a pass line)

**EN-0 Hook proof (new, half a day).** One observe-only hook compiled into the DLL target on a D3D function found by string
cross-reference (e.g. the function that prints `ERR[D3D]: The GPU is hung!` at 0x882A3314 sits beside the Swap/Present path).
Pass: a log line per frame from the hook, emulated picture unchanged. Fail = no-go until the SDK route is found.

**EN-1 Offline map (guide ch. 4, 18.1).**
- D3D map: every function re:Blue hooks (`src/native/ufc_d3d_names.h` as the list), found by call shape and device stores in
  `generated/mmadllzf_xex`. Device size from `Direct3D_CreateDevice`'s allocation (`li r3,<size>`, cf. UFC 0x5F80, RR 0x5080, SvR07 0x5000).
  XDK D3D version from the DLL's import library records. Write `src/native/eamma_d3d_names.h`, `eamma_d3d.h`, `D3DDevice` branch with a `static_assert`.
- Shader inventory: unpack `shader_big.ast`, count containers and their format (0x102A11 like UFC or old 0x102A0E like RR/SvR07 -> take RR's
  `xenosrecomp-0e.patch`), run XenosRecomp with UFC's whole patch stack, count failures by cause. Answer point 3 above.
- Extras (ch. 4, 10): memexport uses, CPU reads of GPU pictures (face/body morph composites from `facegen`?, save thumbnails),
  data maps to keep at 1x.
- Pass: a go/no-go line for the user with the two stop points.

**EN-2 Scaffolding (ch. 5).** Copy `src/reblue/` unchanged, `src/native/` (rename `ufc_` -> `eamma_` only where game-specific),
`cmake/native_renderer.cmake`, `cmake/xenosrecomp.cmake`, `tools/native/`, `third_party/reblue_thirdparty/`, `patches/`,
`tools/nr_fetch_third_party.py`. CMake options `EAMMA_NATIVE_RENDERER` / `EAMMA_NATIVE_HOOKS` **on the DLL target** (point 1);
`build.bat native|nonative`; `-O2 -DNDEBUG` stays pinned (already in `build.bat`; trap 1). SDK null backend: this tree's SDK copy (`EAMMA-rexglue-sdk`, branch `eamma`) is the UFC nightly plus
FNC Reborn's fixes, so it already carries `0036-null-gpu-backend.patch`. `eamma_renderer` cvar + `LatchRenderer` + exit 3 in `src/eamma_app.h`.
Pass: emulated regression run reaches a fight exactly as before; native says "not implemented"; `-O2` in `build.ninja`.

**EN-3 Census, Stop 1 (ch. 6).** Port `src/dev/ufc_d3d_census.cpp`; A/B draw counts against the emulated `trace_draw_ps`
(UFC SDK patch 0028, already in this SDK) on the title, menus and a fight. Prove the device offsets at run time; look for hidden draws
(command buffers, GPR flushes, canned PM4 - point 4). Stop 1 if command buffers carry over ~5% of a fight frame.

**EN-4 Shader cache (ch. 7).** Cache + loader + misses. If point 3 says run-time compiled shaders exist, add a dump-at-CreateShader
cvar (`eamma_native_shader_dump`) and build the cache from a dump run of every scene; the player-PC build (ch. 7 "Built on the player's
PC") then needs a different source than a disc scan. Pass: `native: shader miss` only for XDK-internal shaders; first-frame draw log.

**EN-5 Device, menus, Stop 2 (ch. 8).** Null guest GPU, plume device, present with the TV output curve (`kPresentDisplayCorrection`),
render scale from `resolution_scale`. Menus A/B at full res against emulated, chapter 9's table as the first suspects.
Stop 2 if the menus can't be made to match.

## Week 2 on
Fights at 1x (chapter 9 table first: fans, integer vertex formats, ambient cube as 2D, fetch swizzle, computed vfetch, co-issue,
constant arrays to c255), then pictures the CPU reads (ch. 10), scaling with EA MMA's own keep-1x list (ch. 11), speed (ch. 12;
copy every UFC *speed* default with its off bit, but not keep-1x, the 16F rule or Swap-poll, which are UFC game data/addresses; measure on the reference GPU only after `-O2` is checked), FSR 1 (ch. 13, generic: copy the
four files), GPU pick (ch. 14: `gpu_preference.cpp` must go in the **exe**, not the DLL), and a launcher only if EA MMA gets one (ch. 15).
Every new path gets an off bit. Mods last.

## Traps that apply directly (guide ch. 17)
1 (-O2), 2 (stale cache options), 3 (stale shader cache), 13 (unmapped hook names: a mapped name replaces the game function),
16-18 (draw log one draw late, shot numbers don't pair, emulated-only bugs aren't ours), **19 (junction deletes: `assets\` here is a
junction to the only copy of the disc extract - never delete or clean recursively through it)**, 22 (agent reports are leads).

## Open questions
1. How hooks reach a module (DLL) target (point 1). Does the SDK have a per-module override hook, or must hook sources join the DLL's sources?
2. `shader_big.ast` format and whether run-time compilation happens in normal play (point 3).
3. How much of a frame goes through command buffers (point 4).
4. Which machine tests EA MMA native (point 5).

## Progress

- **2026-11-15 decisions taken (user: "continue, maybe we don't need anything else")**: GPL-3 accepted (local repo, no push);
  work on branch `native`; cvar prefix `eamma_` with UFC's bit numbers; tests on this desktop through `tools/run_shots.ps1`
  (it refuses to start while Rumble Roses holds the GPU), short runs.
- **EN-0 passed.** Generated DLL functions are `weak` (`generated/mmadllzf_xex/eamma_pch.h` DEFINE_REX_FUNC), so a strong
  `extern "C" REX_FUNC(sub_X)` compiled into the DLL target replaces it for every caller and `__imp__sub_X` is the original.
  `src/dev/eamma_hook_proof.cpp` hooks `sub_8831C250` (D3DDevice_Swap: the DLL's only VdSwap caller): logs once a frame
  (r3 = device 0xBE4B9E80, game caller lr 0x88A64C34), picture unchanged (run en0, attract screen at 30 s).
  Open question 1 answered: hook sources join the DLL's sources (`eamma_add_native_renderer(eamma_MMADLLzf_xex)`).
- **Reference choice:** FNC Reborn (the EA fight-game reference) has no native renderer: it runs the SDK's emulated GPU.
  UFC's base is its `claude/native-renderer` branch (0334642, the state the guide describes), not `main`: main has photo
  mode, eyelid blend and blood code woven into `src/reblue`. Generic fixes made on UFC main later, to cherry-pick when
  needed: c9c75d2 (unaligned DXT decoded to RGBA8), d4dec70 + 783c7f5 (constant arrays to c255, split off bits).
- **EN-2 scaffolding:** `src/reblue`, `src/native`, `cmake/`, `patches/`, `tools/native`, `tools/nr_*.py`, `third_party/`
  copied; plume / XenosRecomp copied already patched from UFC's tree (not committed). Removed because they hook UFC
  addresses (trap 13): `ufc_nr10_probe.cpp` (stubbed header kept), `ufc_memexport_cpu.*`, `ufc_gpu_wait_diag.cpp`.
  `ufc3_` -> `eamma_` in names; `EAMMA_NATIVE_HOOKS` defaults OFF until `ufc_d3d_names.h` holds EA MMA's map;
  UFC's player-PC shader extractor is not built (EN-4 decides EA MMA's source). `eamma_renderer` cvar in `src/eamma_app.h`.
  - Still Blue Dragon / UFC guest addresses in generic code to audit before EN-5 (in EA MMA 0x82xxxxxx is the launcher image):
    `kCurrentRenderPassIdVa` 0x82777474 (gpu/device.cpp), `kRenderStateCacheVa` (gpu/draw.cpp), `kTextQuadBatchEA`
    (hooks/draw.cpp), render/sampler state tables (hooks/device.cpp), census image scan 0x82000000.. (src/dev).
  - App <-> renderer bridge (EN-5): the renderer lives in the DLL, the window/app in the exe; `g_eamma_native` and the
    cvars must be read on the DLL side. Cvars defined in the DLL do take command-line values (EN-3's `--eamma_d3d_census=true` worked).
- **EN-1 done** (`FINDINGS-en1-d3d-map.md`, `FINDINGS-en1-shaders.md`). Go/no-go: **go.**
  - D3D: XDK 2.0.11164 (older than UFC's 20500). Device 0x5F00 with SvR 2010's offsets (IB 0x3144 .. VS 0x3248),
    past 0x3290 UFC - 0x40. 43 names: 27 high, 10 med, 3 low; **not linked: Present, SetViewportF,
    BeginIndexedVertices** (the game's frame end sub_88A64B70 calls Sync, Clear/Resolve, GPR, Swap itself, so the
    native present has to hang off Swap). Linked only here: DrawVerticesUP sub_8830E558 (7 callers; needs a hook).
    Spot-checked in the generated code: memset 24320, texture table slot+3180 (+0x31B0), recording CB +0x35A0.
  - `src/native/ufc_d3d_names.h`, `ufc_d3d.h` and `gpu/d3d.h`'s `D3DDevice` (static_asserts) now hold EA MMA's values.
    Hooks-on build (`-DEAMMA_NATIVE_HOOKS=ON`) compiles and links: 24 game functions overridden, all of them mapped
    names (checked against the linker map); emulated run with them compiled in plays as before (run en1hooks).
    The build folder's CMake cache now has `EAMMA_NATIVE_HOOKS=ON` (hooks compiled in, gated off at run time:
    `g_eamma_native` lives in the DLL and nothing sets it until EN-5's app bridge). The option default stays OFF.
  - Shaders: **all precompiled**: 157 RNAFX effects in `shader_big.ast`, 418 unique containers (178 VS, 240 PS),
    all 0x102A11xx; CreateVS/PS get the container unchanged from FXLite (sub_88C881D0), so the cache comes from
    the disc alone (point 3 answered: no run-time compile in normal play; `uiMovies.fx` the one name without a
    twin). UFC's XenosRecomp: 414/418 after three small translator fixes (0x102A1121 two-shader VS, TEXCOORD7
    location, integer loop constants) -> `patches/xenosrecomp-eamma.patch` in EN-4.
  - Stop 1 risk looks low: one command-buffer record site and one replay site (sub_885A1FE0). EN-3 measures it.
  - EN-3 needs: `src/dev/ufc_d3d_census.cpp` still holds UFC's raw clear-emitter hook `sub_8238D540` (EA:
    sub_8830EDE0) and UFC offsets in its log lines; fix before building with `EAMMA_NATIVE_CENSUS=ON`. Add a
    DrawVerticesUP counter, and count SetVS/PS-ConstantF calls (only 3+3 vtable thunks call them).
- **EN-3 done** (`FINDINGS-en3-census.md`). Command buffers carry ~15% of a fight frame's draws (Stop 1 line is 5%),
  but the game has a direct-draw fallback: `--eamma_command_buffers=false` (`src/dev/eamma_cmdbuf_off.cpp`, makes
  sub_885A1FE0 report "no replay" so its callers draw pass 4 directly). Off: 0 replays, picture complete, no crash,
  emulated draw count within 3.5% of the census. **Native runs with command buffers off; Stop 1 avoided.**
  Device VS/PS/decl offsets proven (85,192/85,192). New for EN-5: **SetVS/PSConstantF are never called**: shader
  constants reach the GPU another way; check where before trusting the device's constant blocks.
  Build folder is in the census configuration (hooks off, census on) after these runs.
- **EN-4 done** (`FINDINGS-en4-shaders.md`). `python -I tools/nr_build_shader_cache.py` builds the cache from
  shader_big.ast in ~4 s: 417/418 shaders, 842 prelinked variants (`patches/xenosrecomp-eamma.patch`: two-shader VS,
  TEXCOORD7). Run-time key check (census at CreateVS/PS, emulated): 287 of 288 shaders the game creates up to a
  fight are in the cache; the miss is the light-shaft PS (loop count = run-time integer constant i16).
  EN-5 list: (1) where the game's shader constants live (no ConstantF calls), (2) integer loop constants for
  light shafts, (3) present on Swap (no Present), (4) app bridge + `eamma_command_buffers=false` with native,
  (5) audit Blue Dragon guest addresses in generic code (Progress, EN-2).
- **EN-5 done** (`FINDINGS-en5-native.md`). Movies, menus, loading, fighter select and fights render on the native
  renderer (`build.bat native`, `--eamma_renderer=native`). Seven fixes: app bridge to the DLL, DrawVerticesUP,
  predicated tiling, DEC3N normals, GPU-written shader constants (GpuLoadShaderConstantF4Pointer /
  GpuBeginShaderConstantF4), reversed-Z depth flip in the vertex shader, DXN/DXT5A normal maps.
  Open: skin shading and a menu halo differ from emulated; light shafts (integer constants); A/B the eager
  write-back; speed unmeasured.
