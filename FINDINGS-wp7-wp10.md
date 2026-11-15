# WP7 release set + WP10 launcher (2026-11-15)

Spec: NATIVE-FINISH-DESIGN.md WP7, WP10. Written by an agent; the launcher was built and tested, the game-side code
was only syntax-checked by it (the main session's build and checks are recorded in NATIVE-HANDOFF.md).

## WP7
1. **gpu_preference.cpp** is in `EAMMA_SOURCES` (exe). Check: `dumpbin /exports eamma.exe` lists
   `NvOptimusEnablement` and `AmdPowerXpressRequestHighPerformance`.
2. **Device-fail path**: `src/eamma_renderer_fail.h/.cpp` (from UFC's renderer_fail), in the exe (needs d3d12 + dxgi
   linked). `Fail(code, what)` logs, writes `native-error.txt` beside the exe (code, what, gpu, feature level, log),
   shows a message box unless env `EAMMA_LAUNCHER=1` or `--eamma_native_error_box=false`, then exits. 3 = device,
   4 = shader cache missing. Cvars in `src/eamma_app.h`: `eamma_native_test_device_fail`, `eamma_native_fail_report`
   (false = the old behaviour). A native start without `shader_cache.bin` now exits 4 instead of drawing nothing.
   Unattended tools must pass `--eamma_native_error_box=false` (`tools/eamma_selftest.py` does).
3. **Player-PC shader cache builder**: `launcher/ShaderCache.cs`. The extract is C# (port of
   `tools/eamma_shader_extract.py`); translate and link run the shipped `XenosRecomp.exe` and `eamma_prelink.exe`;
   `shader_common.h` is embedded. Output is byte-identical to `nr_build_shader_cache.py`'s on the real assets
   (both cache files, 3.0 s). Stamp file forces a rebuild after an update; failed rebuild keeps the old cache.
4. **PSO seed**: `config/native/pipelines_seed.bin`, 117 records from `eamma-lap-boot-native` (boot -> one fight ->
   results, 1x). Runs log the `rec=` lines when they start with no `pipelines.bin`. Add modes with
   `py -I tools\nr_pso_seed.py config\native\pipelines_seed.bin config\native\pipelines_seed.bin <run dirs>`.
   `cmake/native_renderer.cmake` copies it post-build; `eamma_farm_job.py` freezes it. 4 of 117 records name a shader
   hash not in the cache table (not looked into).
- Not done: the FSR run.

## WP10 launcher (`launcher/`, EAMMALauncher.csproj, "EA MMA Launcher.exe")
- As-is from UFC: Xgd, Stfs, Toml, Gpu (`eamma_native_adapter`), Cpu, AppVersion, Updater, Tabs/Controls, Theme, Ui,
  Shell, Shots. Rewritten: Config, Setup, ShaderCache, NativeRenderer, RendererError, Program, Play/Setup/Settings
  tabs, tests. Left out: Roster, Music, ModPack (WP11/WP12), photo mode, online.
- Native-only: Play passes `--eamma_renderer=native` and sets `EAMMA_LAUNCHER=1`; exit 3/4 opens the renderer error
  dialog from `native-error.txt`.
- Updater: `Updater.Enabled = false`, no network call.
- Art: none (plain text mark, blue accent). No UFC text or art.
- Settings: `eamma_native_scale` (1/1.5/2/3), `eamma_native_adapter`, `eamma_native_upscale`, fullscreen,
  pause on focus loss. No frame-rate row.
- Disc identity: title 454108FC, media 324DBAD4 (NOTES.md, confirmed from `assets\default.xex`). Region unknown.
- TODO in code: `Setup.DlcUi = false` (whether the game has DLC is not established). Saves go to the SDK's per-user
  folder (the launcher does not set `user_data_root`).
- Tests that pass: `dotnet build -c Release` (0 warnings); `launcher\tests\shader-cache-test.ps1` (41 checks);
  `launcher\tests\screenshots.ps1` (pages at 100% and 150%).
- Not tested: a real disc image through Setup, Play with the real game, the device-fail dialog end to end.

## Open questions for the user
1. Release location (turns on the updater and "Report a problem"). 2. Placeholder look and names ("EA MMA Recomp").
3. Does the game have DLC worth installing? 4. Disc region / other media ids to accept. 5. Saves beside the exe?
6. Offer scale above 1x in Settings while B10 is being closed?

## Decisions and title updates (2026-11-15, after the user's answers)
- The user left the open launcher questions to this session. Decided: keep the placeholder look and the names for
  now (the user was sent the Play and Settings screenshots, `shots/launcher/`); updater stays off; Setup accepts only
  media 324DBAD4 (the recomp is built from that exact executable; the title update packages for "EA Sports MMA
  (World)" sit in a folder named 324DBAD4, so this is the worldwide disc); saves stay in the SDK's per-user folder;
  no DLC page (`Setup.DlcUi` false).
- DLC: the "Unlockable Promos" packages are entitlement keys only; the fighters' data is on the disc (user's note:
  Big John McCarthy, Classic Frank Shamrock, Herschel Walker need the key; Classic Randy Couture needed Xbox Live demo
  invites; Classic Bas Rutten, Ken Shamrock and Andrei Arlovski unlock by play). Plan: a game-side unlock switch
  instead of installing packages (research in FINDINGS-unlocks.md when done).
- Title updates #1 and #2 (user's files, `EA Sports MMA (World) (v1|v2).zip`): each is one LIVE package (type
  000B0000, title 454108FC, media 324DBAD4) holding only executable patches: `default.xexp`, `MMAv1zf.xexp`,
  `MMAv1szf.xexp` (4 KB each) and `MMADLLzf.xex.dllp` (1.2 MB in #1, 1.8 MB in #2). No data files. **Not applied,
  and not recommended**: a patched DLL moves code, so the recompilation and every hooked address of the native
  renderer (D3D map, constants, Swap, pacing hook) would have to be redone and re-proven. The user also reports that
  #2 removed create-a-fighter name audio for UFC fighters. What #1 fixes is not known (the patch was not decoded).
