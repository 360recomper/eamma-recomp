# EA Sports MMA recomp - notes

Title 454108FC, media 324DBAD4, version 0.0.0.2 (disc build 2010-09-15). Disc extract:
`E:\ClaudeGame\EAMMA-extract` (6.3 GB), mounted here as the `assets\` junction.
Never run anything recursive (delete, clean) on `assets\`: it is the only copy of the extract.

## Layout of the game
- `default.xex` (626 KB, base 0x82000000) is a launcher. It loads `MMADLLzf.xex.dll` with
  XexLoadImage and calls into it. `MMAv1szf.xex` is byte-identical to `default.xex`;
  `MMAv1zf.xex` differs (another launcher variant, not used).
- `MMADLLzf.xex.dll` (17 MB, base 0x88000000, .text 0x88300000-0x88FCAF6C) is the game: EA's RNA
  engine (Fight Night Round 4 lineage), D3DX effect compiler and the Xbox 360 shader assembler
  linked in (shaders are `.fx` compiled at run time, `data\xenon\shaders`, `shader_big.ast`).
- The manifest has both: `[entrypoint]` = default.xex -> `generated\default`,
  `[[modules]]` guest_path `mmadllzf.xex.dll` -> `generated\mmadllzf_xex`, built as a host DLL
  beside `eamma.exe` by the SDK's generated `dll_targets.cmake`.

## Setup
Bootstrapped 2026-11-14 from the UFC3 recomp's first commit (`E:\ClaudeGame\UFC Recomp` 80c4b3e):
first UFC's SDK build, since 2026-11-15 its own SDK copy (run log), `env.bat`, presets,
`build.bat` with -O2 pinned (UFC trap 1), `play.bat`, `tools\gamectl.ps1`, `tools\xex_image.py`,
`tools\find_indirect_targets.py`, `tools\seed_loop.ps1`. App: `src\eamma_app.h` (assets path, Xenos
GPU plugin load). UFC's `gpu_allow_invalid_fetch_constants` (a Yuke's engine fix) is NOT set.

## Run log
- 2026-11-14 codegen: 3 analysis seeds (1 launcher, 2 DLL); DLL function 0x88709D38 is a 1.5 MB C++ file (over max_file_size_bytes, compiles fine).
- Link: DLL imports XUsbcam* (Live Vision camera), not in the SDK -> `src/eamma_usbcam.cpp` "no camera" stubs in the DLL target.
- Boot 1: launcher's LoadLibrary("MMADLLzf.xex.dll") loads `\Device\Harddisk0\Partition1\MMADLLzf.xex.dll`; the generated
  registry keys the host DLL only as `mmadllzf.xex.dll`, so it was never wired and DllMain (0x8832E2B8) died "not in function
  table". Fix: `OnPostLoadXexImage` registers the device-path alias (`src/eamma_app.h`). Don't seed 0x88336F48: it was this, not a missing seed.
- Boot 2: first real unregistered call 0x888A01E8 (lr 0x884D4658); seeded with all 310 static candidates from find_indirect_targets.py.
- Boot 3: no more unregistered calls in 2 min. 467k "invalid fetch constant" warnings in 100 s -> `gpu_allow_invalid_fetch_constants` on (as UFC).
- Boot 4 (120 s, scripted START/A taps): game runs, creates profile saves (MMA_PROFILE_00 / MMA_FIGHTERS_00), but the window stays black.
  Every few frames the CPU reads back a resolve (2 MB + 3 mips, 0x079A3000 / 0x0D0E1000 / 0x0DA49000 ...) and each 2 MB readback
  takes 2.2-3.5 s (`readback_resolve=ondemand`), so the game crawls far under 1 fps. Next: A/B `readback_resolve=fast` / `none`,
  find what the 2 MB readback is (likely a CPU-side luminance/exposure or a UI capture), and test runs that don't take over the desktop.
- Test runs since 2026-11-14 22:00: `tools/run_shots.ps1` = 1280x720 window (`--fullscreen=false`; the SDK default is
  fullscreen), `--audio_mute=true`, pushed behind other windows with focus handed back (`gamectl.ps1 tuck`).
- Black screen, what is known (runs boot3-boot15):
  - `readback_resolve=fast` removes the 2-3 s readback stalls; the game then runs at a steady 60 fps (`perf_second_summary`),
    ~21 resolves and ~900 draws a frame. D3D12 is black too, so it is not a Vulkan issue; `eamma_invalid_fetch` on/off and
    `integer_texture_fetch=off` change nothing.
  - **The EA SPORTS boot movie (VP6) renders correctly** (shots/boot14repro/015.png); everything after it is black.
  - VdSwap (`src/eamma_swap_trace.cpp`): one 1280x720 k_8_8_8_8 front buffer at E3CB2000 (physical 03CB3000). With
    `readback_resolve=full` its guest bytes are a grey fade in the first frames, then opaque black (000000FF): the game
    really renders black; the present is fine.
  - Last draws before the front-buffer resolve are menu UI quads: VS D8A200AA39273B1B / PS 34B29856453FE506 (`oC0 = tex * vertex
    colour`, colour = FMT_8_8_8_8 at dword 10 of a 48-byte vertex), ortho-like constants c4-c11 look right.
  - The UI textures have real content (`texture_dump_ranges`: grey gradients, white panels), so the vertex colours (or the
    vertices themselves) must be zero. vpkd3d128 (D3DCOLOR pack, 7 sites) reads correct against Xenia's lane order.
  - Next: dump the vertex buffer of the traced draw in the same frame (the dump at swap 1500 was a different frame and the
    region held other data), find which game code writes those vertices, and check the VMX128 ops it uses.
- **2026-11-15: FIXED, fights render.** Cause: the SDK. EA's engine needs the fixes FNC Reborn (Fight Night Champion
  recomp, same EA engine family; github.com/whompay/FNCReborn, Apache-2.0) made in `patches/rexglue-sdk.patch`. The
  likely key one is fused `vmaddfp`/`vnmsubfp` (single rounding, as on the console). Also in: vexptefp range, nested
  command buffers, EDRAM range clamp, `smart` readback, 64-bit kernel args, sign-in notifications to all listeners,
  sysheap pool, waits without spinning, XUsbcam exports (so `src/eamma_usbcam.cpp` was removed).
  - EA MMA builds against its own SDK copy `E:\ClaudeGame\EAMMA-rexglue-sdk` (branch `eamma` = the UFC3 nightly at
    41a568a + FNC's patch, 3-way merged: kept upstream's vpkuhus/vpkuwus fix and this tree's input system; readback
    keeps `ondemand` and adds `smart`). Build it with its `build-eamma-sdk.bat`. UFC's SDK folder is untouched.
  - App defaults follow FNCReborn.toml (`src/eamma_app.h` eamma_defaults; the command line wins): d3d12,
    render_target_path_d3d12=rtv, use_fuzzy_alpha_epsilon, gamma_render_target_as_unorm16, readback_resolve=smart
    + smart_skip_steady, primitive_processor_cache_min_indices=1000000, gpu_allow_invalid_fetch_constants.
  - Runs fnc1 / fnc3 (scripted START/A taps): EA logo -> menus -> a fight (Emelianenko vs Couture: HUD, crowd, cage)
    -> round highlights, steady 60 fps (`perf_second_summary`). Shots: shots/fnc1/090.png, shots/fnc3/105.png.
  - Open: one access violation (null read +0x148 in a host DLL, ~15 s, during a profile save) in run fnc2; not
    reproduced in 5 later runs. Minidump kept in shots/fnc2. Menus not driven deliberately yet (taps only).

## Seeded functions
Every entry in `config\functions_default.toml` (launcher) and `config\functions_dll.toml` (game).
"codegen" = analysis error "target not in any function" (`tools\codegen_seed_loop.sh`).
"runtime" = "Call to invalid or unregistered function" at run time (`tools\seed_loop.ps1`).

| Module | Address | Found by | Context |
|---|---|---|---|
| default | 0x8201442C | codegen | `b` from 0x8201447C (tail branch into an unsplit function) |
| dll | 0x88990630 | codegen | branch target outside any function |
| dll | 0x88DAA860 | codegen | branch target outside any function |
| dll | 0x888A01E8 | runtime | indirect call from 0x884D4658 (first DLL boot) |
| dll | 0x88333B20 | static | data ref @8800351C, owner 88333A78 |
| dll | 0x883424F8 | static | data ref @88008A78, owner 883424E0 |
| dll | 0x88342510 | static | data ref @88008A84, owner 883424E0 |
| dll | 0x88342528 | static | data ref @88008AA8, owner 883424E0 |
| dll | 0x883425F0 | static | data ref @88008B14, owner 883425D8 |
| dll | 0x88345E70 | static | data ref @8800AFC0, owner 88345E58 |
| dll | 0x88345E88 | static | data ref @8800AFC8, owner 88345E58 |
| dll | 0x88345EA0 | static | data ref @8800AFD0, owner 88345E58 |
| dll | 0x8834F838 | static | data ref @88020204, owner 8834F818 |
| dll | 0x88369930 | static | data ref @88020CEC, owner 88369918 |
| dll | 0x8838EE70 | static | code ref @8838EE58, owner 8838EE38 |
| dll | 0x88390170 | static | code ref @88390158, owner 88390138 |
| dll | 0x883903A0 | static | code ref @88390384, owner 88390368 |
| dll | 0x88390440 | static | code ref @88390428, owner 88390408 |
| dll | 0x8839F018 | static | code ref @8839EFFC, owner 8839EFC8 |
| dll | 0x883A3858 | static | data ref @88039FDC, owner 883A3838 |
| dll | 0x883A38A8 | static | data ref @88039FD8, owner 883A3888 |
| dll | 0x883A38C8 | static | data ref @88039FF0, owner 883A3888 |
| dll | 0x883A38E8 | static | data ref @88039FF4, owner 883A3888 |
| dll | 0x883A3908 | static | data ref @88039FF8, owner 883A3888 |
| dll | 0x883A3928 | static | data ref @88039FFC, owner 883A3888 |
| dll | 0x883A3948 | static | data ref @88039FB4, owner 883A3888 |
| dll | 0x883E6CA0 | static | data ref @88044468, owner 883E6C80 |
| dll | 0x883F0CD0 | static | code ref @883F0CB4, owner 883F0CA0 |
| dll | 0x884117F8 | static | code ref @884117DC, owner 884117A0 |
| dll | 0x884D2F00 | static | data ref @8806C47C, owner 884D2EF0 |
| dll | 0x884E74C8 | static | data ref @88089E2C, owner 884E73D0 |
| dll | 0x884F1E9C | static | code ref @884F1E80, owner 884F1E68 |
| dll | 0x884F1F20 | static | code ref @884F1F04, owner 884F1EE8 |
| dll | 0x884F2200 | static | code ref @884F21E4, owner 884F21C8 |
| dll | 0x884F26C8 | static | code ref @884F26AC, owner 884F2690 |
| dll | 0x88500008 | static | data ref @88FEC2C8, owner 884FFFD8 |
| dll | 0x8853C6D8 | static | data ref @880C19C4, owner 8853C6C8 |
| dll | 0x8853C6E8 | static | data ref @880C19AC, owner 8853C6C8 |
| dll | 0x885ABD18 | static | data ref @880D06C4, owner 885ABCB0 |
| dll | 0x885E6F88 | static | code ref @885E7188, owner 885E6F68 |
| dll | 0x885EF608 | static | data ref @880E88EC, owner 885EF5C0 |
| dll | 0x885F11D4 | static | code ref @885F11B8, owner 885F11A0 |
| dll | 0x885FBDD8 | static | data ref @880E9744, owner 885FBD70 |
| dll | 0x885FC358 | static | code ref @8861CF24, owner 885FC348 |
| dll | 0x8860F718 | static | data ref @880E9738, owner 8860F6E0 |
| dll | 0x8862D480 | static | code ref @8862E114, owner 8862D468 |
| dll | 0x88632740 | static | data ref @880EC0F4, owner 88632520 |
| dll | 0x88633CB8 | static | code ref @8863409C, owner 88633C78 |
| dll | 0x88634B38 | static | code ref @88634B1C, owner 88634AD8 |
| dll | 0x8863A450 | static | data ref @880EFD30, owner 8863A418 |
| dll | 0x8863A830 | static | data ref @880EFD58, owner 8863A7F8 |
| dll | 0x8863AC28 | static | data ref @880EFD80, owner 8863ABF0 |
| dll | 0x8863F9C8 | static | data ref @880EA17C, owner 8863F9B0 |
| dll | 0x8863F9E0 | static | data ref @880EA180, owner 8863F9B0 |
| dll | 0x8863F9F8 | static | data ref @880F0D9C, owner 8863F9B0 |
| dll | 0x88647AC8 | static | data ref @880F03B4, owner 88647A90 |
| dll | 0x88651428 | static | data ref @880F09C4, owner 88651420 |
| dll | 0x8869514C | static | code ref @88695130, owner 88695118 |
| dll | 0x886A5B48 | static | data ref @880FC910, owner 886A5B30 |
| dll | 0x887210E0 | static | code ref @887517D4, owner 88721000 |
| dll | 0x88727980 | static | data ref @88FFB7F4, owner 88727948 |
| dll | 0x887799E0 | static | data ref @881057D4, owner 88779980 |
| dll | 0x88784C88 | static | data ref @88107268, owner 88784C58 |
| dll | 0x88784CB8 | static | data ref @88107264, owner 88784C58 |
| dll | 0x88784CE8 | static | data ref @88107260, owner 88784C58 |
| dll | 0x88784D18 | static | data ref @8810725C, owner 88784C58 |
| dll | 0x88784D48 | static | data ref @88107258, owner 88784C58 |
| dll | 0x88784D78 | static | data ref @88107254, owner 88784C58 |
| dll | 0x88784DA8 | static | data ref @8810724C, owner 88784C58 |
| dll | 0x88786C78 | static | data ref @8810728C, owner 88786C68 |
| dll | 0x8878F5C8 | static | code ref @88799E00, owner 8878F5B8 |
| dll | 0x8878F5D8 | static | code ref @8879A288, owner 8878F5B8 |
| dll | 0x8878F5E8 | static | code ref @8879BB38, owner 8878F5B8 |
| dll | 0x8878F5F8 | static | code ref @8879CD7C, owner 8878F5B8 |
| dll | 0x8878F608 | static | code ref @8879C5A4, owner 8878F5B8 |
| dll | 0x8878F618 | static | code ref @887A0318, owner 8878F5B8 |
| dll | 0x8878F628 | static | code ref @8879E6C0, owner 8878F5B8 |
| dll | 0x887A62B0 | static | data ref @8810E35C, owner 887A6278 |
| dll | 0x887A6D00 | static | data ref @8810DF54, owner 887A6CC8 |
| dll | 0x887A6D70 | static | data ref @8810DF74, owner 887A6D38 |
| dll | 0x887A6DE0 | static | data ref @8810DF94, owner 887A6DA8 |
| dll | 0x887A7838 | static | data ref @8810E1CC, owner 887A7800 |
| dll | 0x887BB260 | static | code ref @886F3908, owner 887BB250 |
| dll | 0x887BB270 | static | code ref @886F3A14, owner 887BB250 |
| dll | 0x887C6078 | static | data ref @881118BC, owner 887C6040 |
| dll | 0x887C6DC8 | static | data ref @881119C4, owner 887C6D98 |
| dll | 0x887CC828 | static | data ref @881120F0, owner 887CC7F8 |
| dll | 0x887D8470 | static | data ref @8811F850, owner 887D8440 |
| dll | 0x887E3870 | static | data ref @88039180, owner 887E3860 |
| dll | 0x887E3880 | static | data ref @88121634, owner 887E3860 |
| dll | 0x887E4540 | static | code ref @887E6698, owner 887E4528 |
| dll | 0x887E4550 | static | code ref @887E6964, owner 887E4528 |
| dll | 0x887E4560 | static | code ref @8879A60C, owner 887E4528 |
| dll | 0x887E4570 | static | code ref @887E60A8, owner 887E4528 |
| dll | 0x887E4580 | static | code ref @8879AA5C, owner 887E4528 |
| dll | 0x887E4590 | static | code ref @887E6504, owner 887E4528 |
| dll | 0x887E45A0 | static | code ref @887E6F04, owner 887E4528 |
| dll | 0x887E45B0 | static | code ref @88783310, owner 887E4528 |
| dll | 0x887E45C0 | static | code ref @8879A538, owner 887E4528 |
| dll | 0x887E45D0 | static | code ref @8879A140, owner 887E4528 |
| dll | 0x887E45E0 | static | code ref @88799C78, owner 887E4528 |
| dll | 0x887E45F0 | static | code ref @887E627C, owner 887E4528 |
| dll | 0x887E4600 | static | code ref @887E6860, owner 887E4528 |
| dll | 0x887E4610 | static | code ref @887E7054, owner 887E4528 |
| dll | 0x887E4620 | static | code ref @8879A39C, owner 887E4528 |
| dll | 0x887E4630 | static | code ref @8879CC00, owner 887E4528 |
| dll | 0x887E4640 | static | code ref @887E65E0, owner 887E4528 |
| dll | 0x887E4650 | static | code ref @887E7108, owner 887E4528 |
| dll | 0x887E4660 | static | code ref @8879CA90, owner 887E4528 |
| dll | 0x887E4670 | static | code ref @887E6168, owner 887E4528 |
| dll | 0x887E4680 | static | code ref @887E640C, owner 887E4528 |
| dll | 0x887E8498 | static | data ref @88121B30, owner 887E8480 |
| dll | 0x887ED4D0 | static | data ref @88120354, owner 887ED498 |
| dll | 0x887F4958 | static | code ref @887F7A34, owner 887F4948 |
| dll | 0x887F8E68 | static | data ref @881241DC, owner 887F8E38 |
| dll | 0x88805D30 | static | code ref @88805E24, owner 88805D28 |
| dll | 0x8880E3A8 | static | data ref @88126E54, owner 8880E348 |
| dll | 0x888179B0 | static | data ref @88126E2C, owner 88817948 |
| dll | 0x8885C530 | static | data ref @88128E88, owner 8885C518 |
| dll | 0x8886F6C0 | static | data ref @88129940, owner 8886F628 |
| dll | 0x888710F0 | static | data ref @881299BC, owner 888710D8 |
| dll | 0x888A01F0 | static | data ref @88130EB0, owner 888A01E0 |
| dll | 0x888DF9D8 | static | code ref @888DF9BC, owner 888DF9A0 |
| dll | 0x8890F368 | static | data ref @88020040, owner 8890F358 |
| dll | 0x8890F390 | static | data ref @8802003C, owner 8890F358 |
| dll | 0x88910840 | static | data ref @8815DDBC, owner 88910820 |
| dll | 0x88910860 | static | data ref @8815DDD8, owner 88910820 |
| dll | 0x88910AD8 | static | data ref @8815DE58, owner 88910AB8 |
| dll | 0x88910BE8 | static | data ref @8815DEE8, owner 88910BC8 |
| dll | 0x88910C08 | static | data ref @8815F960, owner 88910BC8 |
| dll | 0x88910D08 | static | data ref @8815F8E0, owner 88910CE8 |
| dll | 0x88910D28 | static | data ref @8815F8E4, owner 88910CE8 |
| dll | 0x88910D48 | static | data ref @8815F8FC, owner 88910CE8 |
| dll | 0x88910D68 | static | data ref @8815F90C, owner 88910CE8 |
| dll | 0x88910E68 | static | data ref @8815F9B0, owner 88910E48 |
| dll | 0x88910E88 | static | data ref @8815F9B4, owner 88910E48 |
| dll | 0x88910FD4 | static | code ref @88910FB8, owner 88910F98 |
| dll | 0x889110D8 | static | data ref @8815DF30, owner 889110B8 |
| dll | 0x88911320 | static | data ref @8815DFCC, owner 88911300 |
| dll | 0x88911340 | static | data ref @8815DFD8, owner 88911300 |
| dll | 0x88911458 | static | data ref @88161144, owner 88911438 |
| dll | 0x8891156C | static | code ref @88911550, owner 88911530 |
| dll | 0x88911AB8 | static | data ref @881611EC, owner 88911A98 |
| dll | 0x88911AD8 | static | data ref @881611F0, owner 88911A98 |
| dll | 0x88911B18 | static | data ref @8816120C, owner 88911AF8 |
| dll | 0x88911B38 | static | data ref @88161220, owner 88911AF8 |
| dll | 0x88912000 | static | data ref @8815E2D0, owner 88911FE8 |
| dll | 0x889120A0 | static | data ref @8815E2E4, owner 88912040 |
| dll | 0x889132C8 | static | data ref @8815E5B4, owner 889132B8 |
| dll | 0x889132D8 | static | data ref @8815E5B0, owner 889132B8 |
| dll | 0x889132E8 | static | data ref @8815E5B8, owner 889132B8 |
| dll | 0x889132F8 | static | data ref @8815E5BC, owner 889132B8 |
| dll | 0x88913308 | static | data ref @8815E5C0, owner 889132B8 |
| dll | 0x88913318 | static | data ref @8815E5C4, owner 889132B8 |
| dll | 0x88913328 | static | data ref @8815E5C8, owner 889132B8 |
| dll | 0x88913338 | static | data ref @8815E5E4, owner 889132B8 |
| dll | 0x889137C0 | static | data ref @8815E614, owner 889137B0 |
| dll | 0x88918BF8 | static | data ref @88161848, owner 88918BD8 |
| dll | 0x889195A8 | static | data ref @88161834, owner 88919588 |
| dll | 0x889195C8 | static | data ref @88161758, owner 88919588 |
| dll | 0x889195E8 | static | data ref @8816175C, owner 88919588 |
| dll | 0x88919608 | static | data ref @88161740, owner 88919588 |
| dll | 0x88919628 | static | data ref @88161744, owner 88919588 |
| dll | 0x88919670 | static | data ref @88161738, owner 88919650 |
| dll | 0x8891BF88 | static | data ref @88160278, owner 8891BF70 |
| dll | 0x88920400 | static | data ref @8815EDEC, owner 889203E0 |
| dll | 0x88925C58 | static | data ref @8815D890, owner 88925C48 |
| dll | 0x889286A0 | static | data ref @8815F384, owner 88928690 |
| dll | 0x88931090 | static | data ref @88161C64, owner 88931088 |
| dll | 0x889310C0 | static | data ref @88161C6C, owner 889310B8 |
| dll | 0x889310F0 | static | data ref @88161C74, owner 889310E8 |
| dll | 0x889368F8 | static | data ref @88161458, owner 889368E0 |
| dll | 0x88936948 | static | data ref @88161450, owner 88936930 |
| dll | 0x88936968 | static | data ref @88161444, owner 88936930 |
| dll | 0x88936988 | static | data ref @88161440, owner 88936970 |
| dll | 0x889369A0 | static | data ref @88161468, owner 88936970 |
| dll | 0x889369B8 | static | data ref @8816146C, owner 88936970 |
| dll | 0x889369D0 | static | data ref @88161488, owner 88936970 |
| dll | 0x889369E8 | static | data ref @88161470, owner 88936970 |
| dll | 0x88936A00 | static | data ref @88161474, owner 88936970 |
| dll | 0x88936A18 | static | data ref @88161478, owner 88936970 |
| dll | 0x88936A30 | static | data ref @88161460, owner 88936970 |
| dll | 0x88936A48 | static | data ref @88161464, owner 88936970 |
| dll | 0x88936A60 | static | data ref @8816148C, owner 88936970 |
| dll | 0x88936A78 | static | data ref @88161494, owner 88936970 |
| dll | 0x88936A90 | static | data ref @88161498, owner 88936970 |
| dll | 0x88936AA8 | static | data ref @881614B4, owner 88936970 |
| dll | 0x88936AC0 | static | data ref @881614B8, owner 88936970 |
| dll | 0x88936AD8 | static | data ref @881614BC, owner 88936970 |
| dll | 0x8898D9E8 | static | data ref @8815DB3C, owner 8898D9E0 |
| dll | 0x8898D9F8 | static | data ref @8815DB24, owner 8898D9F0 |
| dll | 0x8898E528 | static | data ref @8815E260, owner 8898E520 |
| dll | 0x8898E540 | static | data ref @8815E23C, owner 8898E538 |
| dll | 0x8898E548 | static | data ref @8815E22C, owner 8898E538 |
| dll | 0x8898FA98 | static | data ref @8815F2D0, owner 8898FA88 |
| dll | 0x88990340 | static | data ref @8815DB20, owner 88990338 |
| dll | 0x889914A0 | static | data ref @8815FABC, owner 88991480 |
| dll | 0x889914C0 | static | data ref @8815FAE4, owner 88991480 |
| dll | 0x88991D30 | static | data ref @8815E248, owner 88991D28 |
| dll | 0x88991E78 | static | data ref @8815FCD4, owner 88991E58 |
| dll | 0x88993808 | static | data ref @8815E238, owner 88993800 |
| dll | 0x88993810 | static | data ref @8815E224, owner 88993800 |
| dll | 0x88993818 | static | data ref @8815E244, owner 88993800 |
| dll | 0x88993820 | static | data ref @8815E234, owner 88993800 |
| dll | 0x88993A78 | static | data ref @881602B8, owner 88993A70 |
| dll | 0x88993A80 | static | data ref @8815E220, owner 88993A70 |
| dll | 0x88995450 | static | data ref @88160850, owner 88995400 |
| dll | 0x889966D8 | static | data ref @88160A70, owner 889966C0 |
| dll | 0x88996D38 | static | data ref @88160D9C, owner 88996D20 |
| dll | 0x88997778 | static | data ref @88161214, owner 88997770 |
| dll | 0x88997798 | static | data ref @88161218, owner 88997770 |
| dll | 0x889977B8 | static | data ref @88161224, owner 88997770 |
| dll | 0x889977D8 | static | data ref @88161228, owner 88997770 |
| dll | 0x889977F8 | static | data ref @88161254, owner 88997770 |
| dll | 0x88997818 | static | data ref @88161258, owner 88997770 |
| dll | 0x88997838 | static | data ref @8816125C, owner 88997770 |
| dll | 0x88997858 | static | data ref @88161230, owner 88997770 |
| dll | 0x88997878 | static | data ref @8816126C, owner 88997770 |
| dll | 0x88997AA0 | static | data ref @8816141C, owner 88997A88 |
| dll | 0x88997AB8 | static | data ref @88161420, owner 88997A88 |
| dll | 0x88997AD0 | static | data ref @88161414, owner 88997A88 |
| dll | 0x88997AE8 | static | data ref @8816140C, owner 88997A88 |
| dll | 0x88997B00 | static | data ref @88161418, owner 88997A88 |
| dll | 0x88997B18 | static | data ref @88161424, owner 88997A88 |
| dll | 0x88997B30 | static | data ref @88161410, owner 88997A88 |
| dll | 0x88997B48 | static | data ref @88161428, owner 88997A88 |
| dll | 0x88997B60 | static | data ref @8816142C, owner 88997A88 |
| dll | 0x88997B78 | static | data ref @881614D8, owner 88997A88 |
| dll | 0x88997B90 | static | data ref @881614DC, owner 88997A88 |
| dll | 0x88997BA8 | static | data ref @881614E0, owner 88997A88 |
| dll | 0x88997BC0 | static | data ref @881614E4, owner 88997A88 |
| dll | 0x88997BD8 | static | data ref @88161400, owner 88997A88 |
| dll | 0x88997DB0 | static | data ref @88161670, owner 88997D80 |
| dll | 0x88998008 | static | data ref @881617C8, owner 88997FF0 |
| dll | 0x88998090 | static | data ref @88161850, owner 88998078 |
| dll | 0x88998140 | static | data ref @88161870, owner 88998128 |
| dll | 0x8899CA50 | static | data ref @881630DC, owner 8899CA08 |
| dll | 0x8899CA90 | static | data ref @881630C4, owner 8899CA70 |
| dll | 0x889B14D8 | static | data ref @88168EEC, owner 889B12D8 |
| dll | 0x889B9358 | static | data ref @8816DB0C, owner 889B9338 |
| dll | 0x889B9378 | static | data ref @8816DB10, owner 889B9338 |
| dll | 0x889BA838 | static | data ref @8816E094, owner 889BA828 |
| dll | 0x889E24F8 | static | data ref @88181E58, owner 889E24E0 |
| dll | 0x889E6940 | static | data ref @88182218, owner 889E6928 |
| dll | 0x889EBEB0 | static | data ref @88182908, owner 889EBEA0 |
| dll | 0x88A08E00 | static | data ref @8817FD18, owner 88A08DB0 |
| dll | 0x88A84AC0 | static | code ref @88A858A0, owner 88A84A38 |
| dll | 0x88AED3D8 | static | data ref @89037F64, owner 88AED3C8 |
| dll | 0x88AF9348 | static | code ref @88AF93D4, owner 88AF9320 |
| dll | 0x88AFAC18 | static | data ref @89038238, owner 88AFABE8 |
| dll | 0x88AFAC48 | static | data ref @8903823C, owner 88AFABE8 |
| dll | 0x88B02F28 | static | data ref @890382A8, owner 88B02DC0 |
| dll | 0x88B66A18 | static | code ref @88B66F3C, owner 88B669E8 |
| dll | 0x88B77BE0 | static | data ref @881EA800, owner 88B77BD8 |
| dll | 0x88B7CA00 | static | code ref @88B7E514, owner 88B7C9E8 |
| dll | 0x88B7CA18 | static | code ref @88B7E434, owner 88B7C9E8 |
| dll | 0x88B9B818 | static | data ref @881F389C, owner 88B9B800 |
| dll | 0x88BB3340 | static | data ref @881F3D1C, owner 88BB3338 |
| dll | 0x88BBD9E0 | static | data ref @881F42D8, owner 88BBD938 |
| dll | 0x88D0BF48 | static | data ref @8822BD4C, owner 88D0BF38 |
| dll | 0x88D0C350 | static | data ref @8822BF1C, owner 88D0C328 |
| dll | 0x88D0C378 | static | data ref @8822BF20, owner 88D0C328 |
| dll | 0x88D0C3A0 | static | data ref @8822BF24, owner 88D0C328 |
| dll | 0x88D0C3C8 | static | data ref @8822BF28, owner 88D0C328 |
| dll | 0x88D0C7E0 | static | data ref @8822BD70, owner 88D0C7C0 |
| dll | 0x88D0C808 | static | code ref @88D15744, owner 88D0C7E8 |
| dll | 0x88D0C818 | static | code ref @88D1586C, owner 88D0C7E8 |
| dll | 0x88D0C828 | static | code ref @88D16104, owner 88D0C7E8 |
| dll | 0x88D0C838 | static | code ref @88D15B1C, owner 88D0C7E8 |
| dll | 0x88D0C848 | static | code ref @88D1594C, owner 88D0C7E8 |
| dll | 0x88D0C858 | static | code ref @88D156A4, owner 88D0C7E8 |
| dll | 0x88D0C868 | static | code ref @88D15B6C, owner 88D0C7E8 |
| dll | 0x88D0C878 | static | code ref @88D15F34, owner 88D0C7E8 |
| dll | 0x88D0C888 | static | code ref @88D156F4, owner 88D0C7E8 |
| dll | 0x88D0C898 | static | code ref @88D15BBC, owner 88D0C7E8 |
| dll | 0x88D9DE10 | static | data ref @88243C58, owner 88D9DE08 |
| dll | 0x88DA01B0 | static | data ref @88243D14, owner 88DA01A8 |
| dll | 0x88DA01B8 | static | data ref @88243D54, owner 88DA01A8 |
| dll | 0x88DA01C8 | static | data ref @88243D34, owner 88DA01C0 |
| dll | 0x88DA01D0 | static | data ref @88243D10, owner 88DA01C0 |
| dll | 0x88DA01D8 | static | data ref @88243D28, owner 88DA01C0 |
| dll | 0x88DA01E0 | static | data ref @88243D08, owner 88DA01C0 |
| dll | 0x88DA6288 | static | data ref @88244158, owner 88DA6280 |
| dll | 0x88DA6290 | static | data ref @88244128, owner 88DA6280 |
| dll | 0x88DA6298 | static | data ref @88243D20, owner 88DA6280 |
| dll | 0x88DA62A0 | static | data ref @88243D1C, owner 88DA6280 |
| dll | 0x88DA6558 | static | data ref @88244234, owner 88DA6550 |
| dll | 0x88DA6568 | static | data ref @88244160, owner 88DA6560 |
| dll | 0x88DA6570 | static | data ref @88243D24, owner 88DA6560 |
| dll | 0x88DA6578 | static | data ref @88243D48, owner 88DA6560 |
| dll | 0x88DA6580 | static | data ref @88243D44, owner 88DA6560 |
| dll | 0x88DA6588 | static | data ref @88243D4C, owner 88DA6560 |
| dll | 0x88DA6590 | static | data ref @88243D18, owner 88DA6560 |
| dll | 0x88DA6B68 | static | data ref @8824430C, owner 88DA6B50 |
| dll | 0x88DA7820 | static | data ref @882442A0, owner 88DA7818 |
| dll | 0x88DA8570 | static | data ref @88244364, owner 88DA8568 |
| dll | 0x88DAB2E8 | static | data ref @88244538, owner 88DAB230 |
| dll | 0x88E24258 | static | code ref @88E244B4, owner 88E24250 |
| dll | 0x88E25360 | static | data ref @8827B380, owner 88E25348 |
| dll | 0x88E26580 | static | data ref @8827B85C, owner 88E26548 |
| dll | 0x88E289A8 | static | data ref @8827C5F4, owner 88E28950 |
| dll | 0x88E2E100 | static | data ref @8827D4A8, owner 88E2E0D0 |
| dll | 0x88E2E298 | static | data ref @8827D214, owner 88E2E268 |
| dll | 0x88E33FA8 | static | data ref @8827D92C, owner 88E33F78 |
| dll | 0x88E4F038 | static | data ref @88280EEC, owner 88E4F008 |
| dll | 0x88E7C090 | static | code ref @88E7C488, owner 88E7C080 |
| dll | 0x88E81FB0 | static | data ref @89053980, owner 88E81EF8 |
| dll | 0x88EAB264 | static | code ref @88EAB248, owner 88EAB228 |
| dll | 0x88EACBA0 | static | code ref @88E9A448, owner 88EAC9C0 |
| dll | 0x88EE2610 | static | code ref @88EFB8FC, owner 88EE24A8 |
| dll | 0x88F634E8 | static | data ref @8828F620, owner 88F63498 |
| dll | 0x88F924A0 | static | data ref @880C19B4, owner 88F92490 |
| dll | 0x88F9D628 | static | data ref @8829485C, owner 88F9D560 |
