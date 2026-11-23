# EA Sports MMA recomp

Static recompilation of EA Sports MMA (Xbox 360, title 454108FC) with ReXGlue SDK v0.10, bootstrapped
from the UFC Undisputed 3 recomp. Hosted at github.com/360recomper/eamma-recomp (private). Test the native renderer with `play-native.bat`; report bugs as issues.

No game files are included. `assets\` is a junction to the disc extract `E:\ClaudeGame\EAMMA-extract`.
Do not delete or clean through it recursively: it is the only copy.

## Build and run
1. `build.bat` runs codegen for both guest modules (`default.xex` launcher, `MMADLLzf.xex.dll` game)
   into `generated\`, then configures and builds `eamma.exe` + `eamma_MMADLLzf_xex.dll` with the
   EA MMA SDK copy (`E:\ClaudeGame\EAMMA-rexglue-sdk`: UFC3 nightly + FNC Reborn's fixes).
2. `play.bat` starts it (D3D12; log in `play.log`). Keyboard: add `--mnk_mode=true`.

## Files
- `eamma_manifest.toml`: entrypoint + `[[modules]]` for the game DLL.
- `config\functions_default.toml`, `config\functions_dll.toml`: seeded function entry points (all listed in `NOTES.md`).
- `src\eamma_app.h`: assets path, Xenos GPU plugin, the DLL's device-path alias for the recompiled module.
- `src\eamma_swap_trace.cpp`: VdSwap trace (front buffer address, format, brightness every 600 swaps).
- `tools\codegen_seed_loop.sh`: codegen until no "target not in any function" errors are left.
- `tools\seed_loop.ps1`: boot loop that seeds "unregistered function" addresses at run time.
- `tools\find_indirect_targets.py`, `tools\xex_image.py`: static indirect-target scan, XEX image loader.
- `tools\gamectl.ps1`: screenshots of the game window, posted keys.
- `NOTES.md`: run log and every seeded function. `NATIVE-PLAN.md`: plan for the native renderer.
