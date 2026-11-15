# Credits

- **ReXGlue SDK** (rexglue/rexglue-sdk): the recompiler and runtime.
- **FNC Reborn** by whompay (github.com/whompay/FNCReborn, Apache-2.0): its ReXGlue SDK patch for Fight Night
  Champion is merged into this project's SDK copy (`E:\ClaudeGame\EAMMA-rexglue-sdk`, branch `eamma`), and its
  settings recipe is the base of `src/eamma_app.h`'s defaults. Those fixes made EA MMA render.
- **UFC Undisputed 3 recomp** (this machine, `E:\ClaudeGame\UFC Recomp`): the project template, tools and SDK base.

## Native renderer (branch `native`, experimental, `eamma_renderer = native`)

Copied from the UFC Undisputed 3 recomp's `claude/native-renderer` branch (0334642), which ported
the renderer of the [SmackDown vs. Raw 2010 recomp](https://github.com/nefariousjosiah/WWE-SVR2010-RECOMP)
by **nefariousjosiah** (GPL-3.0), itself built on [re:Blue](https://github.com/zolaware/reblue)'s
renderer core (zolaware, Blue Dragon recomp, BSD 3-Clause, `src/reblue/LICENSE.reblue`). With that
code in, this project is under the GNU GPL v3.0 ([LICENSE](LICENSE)). It stays a local repository.

| Component | Where | Licence |
|---|---|---|
| re:Blue renderer core (via SvR 2010 and UFC 3) | `src/reblue` | BSD 3-Clause + GPL-3.0 changes |
| SvR 2010 / UFC 3 native layer | `src/native`, `cmake/native_renderer.cmake`, `patches/` | GPL-3.0 |
| [plume](https://github.com/zolaware/plume) | `third_party/plume` at `e0c8871` + `patches/plume*.patch` (not committed) | MIT |
| [reblue-XenosRecomp](https://github.com/zolaware/reblue-XenosRecomp) | `third_party/XenosRecomp` at `339af41` + `patches/xenosrecomp*.patch` (not committed) | MIT |
| zstd decoder, miniz | `third_party/reblue_thirdparty` | BSD / Unlicense |
| AMD FidelityFX FSR 1 | `third_party/fidelityfx` | MIT |
| DirectX Shader Compiler | from XenosRecomp's `thirdparty/dxc-bin` | NCSA / Microsoft terms |
