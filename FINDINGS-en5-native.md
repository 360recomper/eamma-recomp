# EN-5: native device, present, menus, first fights (2026-11-15)

Build: `build.bat native` (renderer + hooks in the game DLL), shader cache: `python -I tools/nr_build_shader_cache.py`.
Run: `--eamma_renderer=native`. Test runs: `tools/run_shots.ps1` + `tools/selftest_scripts/boot_taps.txt` (n1..n33).

## Result
EA logo and attract movies, menus, loading screen, fighter select / walkout, the fight (both fighters skinned and posed,
cage fence, crowd, mat, HUD, shadows) and the round highlights render on the native plume D3D12 renderer
(`shots/n33`). Not yet equal to emulated: fighter skin is pale/ashen with orange highlights at some angles, and the
menu fighter has a light halo around his silhouette. Stop 2 (menus can't be made to match) is not met: everything
draws; what is left is shading.

## What it took (each was a black or missing picture)
1. **App bridge.** The renderer lives in the game DLL (`src/native/eamma_bridge.cpp` exports); `eamma.exe` loads that
   DLL early (same module the SDK uses later), enables the hooks there, loads the Xenos plugin's null backend, sets
   `eamma_command_buffers=false` and creates the plume device on the app window (`src/eamma_app.h`).
2. **DrawVerticesUP** (sub_8830E558) inlines EndVertices, so the EndVertices hook never drew its quads: the movies and
   2D layers were missing. Hooked: after the game's call, the pending BeginVertices block is drawn.
   (Mis-step on the way, for the record: I first took the white boot screen for a stall on GPU readback; the emulated
   GPU with `readback_resolve=none` showed the same 1-draw frames because the attract movie *is* one quad a frame.)
3. **Predicated tiling.** The 3D scene renders into a tile-sized EDRAM surface (1280x256) between BeginTiling and
   EndTiling (3 tiles); resolves of tiles 2 and 3 were refused. BeginTiling is hooked: the bound colour/depth surfaces
   get host copies as large as all tiles (`SvrNoteTiledSurface`), and the tile clear the game does through its
   internal ClearF (no hook) is done on the whole target.
4. **DEC3N normals** (decl type 0x2A2187, three signed 10-bit lanes): had no host format, so every lit mesh's
   declaration was invalid and the draw dropped. Decoded in the shader (`SPEC_CONSTANT_DEC3N_NORMAL`, bit 6).
5. **Shader constants (the big one).** EA's renderer never calls SetVertex/PixelShaderConstantF and keeps nothing it
   needs in the device's constant block. Two XDK paths, both hooked in `src/native/eamma_constant_loads.cpp`:
   - `GpuLoadShaderConstantF4Pointer` sub_883198E0(dev, VS/PS, start, data, count): a LOAD_ALU_CONSTANT packet makes
     the GPU read c156..c187 (world-view-projection etc.) from a per-draw constant buffer in memory.
   - `GpuBeginShaderConstantF4` sub_88319858(dev, VS/PS, start, count) returns ring space the caller memcpys into
     (bone palettes / crowd instance data, VS c0..c155).
   Each call is recorded and copied into the device block at the next native draw (`DispatchDraw`), where the
   constant upload reads it. Found by logging c156 at a draw (zero) and walking the ring between draws in the
   census (LOAD_ALU_CONSTANT offset 0x270 = dword 624 = c156).
6. **Reversed Z.** EA builds a reversed-Z buffer with an inverted viewport depth range (MinZ 1, MaxZ 0) and GEQUAL.
   D3D12 can't take an inverted range; the renderer swapped it, which reversed depth order, so the arena drawn first
   hid the fighters. The vertex shader now flips z (`g_DepthFlip`, shared constant c20.w, set per draw from the
   viewport) and stored depth matches the console.
7. **Normal maps.** k_DXN (BC5) and k_DXT5A (BC4) textures were rejected by the mirror. Mapped, with Xenia's channel
   fill (DXN R G G G, DXT5A R R R R) composed into the view swizzle.

## Open (next work, roughly in order)
- Skin shading differs from emulated (pale/ashen, orange highlights) and a halo around fighters in the menu (depth of
  field or a depth-resolve format). Method: draw log at the bad second, skip the skin PS by hash, read its HLSL.
- Light shafts: the one shader not in the cache (integer loop constant i16, FINDINGS-en4-shaders.md).
- `eamma_native_wb_eager_frames` (eager resolve write-back, default on) was added while chasing the misread stall; it
  costs a GPU sync per new resolve. A/B it off once the picture is right.
- One crash in a native run (n26): null read +0x134 in rexruntime during the profile save at ~15 s, the same moment as
  the intermittent emulated crash in NOTES.md (fnc2). Not reproduced in the next 7 native runs.
- Speed not measured; menus/movies run at their own pace, fights were smooth by eye in screenshots only.
- Dev aids added: draw log prints stream 0's first vertex and VS c0..c7, c156..c159 for indexed draws;
  `EAMMA_NO_DEPTH_TEST=1`; `SVR_PRESENT_TARGET=tiled`; `SVR_SAMPLE_STACKS` samples now flush per sample.
