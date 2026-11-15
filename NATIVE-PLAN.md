# EA Sports MMA - native renderer plan

Plan only: nothing native is built in this tree yet. Source: `E:\ClaudeGame\UFC-native\docs\NATIVE-RENDERER-GUIDE.md`
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
`build.bat native|nonative`; `-O2 -DNDEBUG` stays pinned (already in `build.bat`; trap 1). SDK null backend: this tree uses UFC's SDK build
(`UFC-rexglue-sdk-nightly`), which already carries `0036-null-gpu-backend.patch`, so no SDK copy is needed unless EA MMA needs an SDK fix
UFC must not get. `eamma_renderer` cvar + `LatchRenderer` + exit 3 in `src/eamma_app.h`.
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
copy every UFC default with its off bit; measure on the reference GPU only after `-O2` is checked), FSR 1 (ch. 13, generic: copy the
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
