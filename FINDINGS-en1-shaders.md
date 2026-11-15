# EN-1 shaders: EA Sports MMA GPU shader inventory

2026-11-15, offline only (no game runs). Written by a research agent; leads until a run-time hash census at
CreateVertexShader/CreatePixelShader checks them (trap 22). Its extracted data and query scripts were in the
session scratchpad (temporary, may be gone); the unpacker is kept as `tools/eamma_unpack_shader_big.py`.

## Summary
1. **Every GPU shader is precompiled.** `data\xenon-gen\bigs\shader_big.ast` holds 157 compiled RNAFX effects
   containing 545 Xenos shader containers, **418 unique (178 VS, 240 PS)**, all the new 0x102A11xx format
   (RR's `xenosrecomp-0e.patch` is not needed).
2. A scan of the whole extract (raw files and every zlib stream; audio/FMV skipped) finds no other shaders:
   bootload, particle_shaders_big and crowd_1_big only hold copies of shader_big effects. The DLL and XEX images
   hold zero containers (UFC had 68 D3D-internal ones). No .fx/HLSL source on the disc.
3. Every CreateVertexShader (sub_88311238) / CreatePixelShader (sub_88310E58) call comes from FXLite's
   sub_88C881D0, which passes the container embedded in the effect unchanged: an XXH3 of the container at
   create time equals the offline key. **The cache can be built from the disc (shader_big.ast) alone.**
4. Run-time compile (D3DX9.1 effect compiler, HLSL, Shader Assembler) is reachable only through the RNA loader
   sub_888A6B98, when effect data is NULL or doesn't start with 0xA3D7, plus a retry that compiles the "error"
   effect from text. All 157 entries start with 0xA3D7: in a normal run it is an error fallback only.
5. One effect name in the data has no compiled twin: `uiMovies.fx` (UI movie widget's default `_MovieShader`).
6. **XenosRecomp (UFC's patched build) converts 395 of the 396 containers it accepts**; it skips the 22
   0x102A1121 VS. With their extra header fields zeroed, 19 of those 22 convert: **414 of 418** with today's
   patch stack, 3 small translator fixes left (section 4).

## 1. shader_big.ast format
- BGFA1.05; entry layout described by header bytes 0x20-0x2F (UFC's eamma_read.py assumed a fixed 79-byte entry).
- Header (LE): 0x0C entry count (157); 0x10 header size 0x40, table at 0x44; 0x18 table size, data at 0x2F80;
  0x21 has-kind-byte; 0x22 hash bytes (8); 0x23 offset bytes (3); 0x24 compressed-size bytes (2); 0x25 "saved"
  bytes (3, 0 = stored raw); 0x26 offset shift (3); 0x2C name length (60; 0 = hash-only big).
- Entry (77 bytes): u8 0x11, u64 name hash (LE, sorted), u24 offset>>3, u16 compressed size, u24 saved,
  char name[60]. Each entry is one zlib stream; all 157 inflate. (This parse does not fit bootload.ast.)
- Each entry is a compiled RNAFX effect (BE): +0 magic 0xA3D70141 (loader checks the top 16 bits), +4 payload
  size (file - 12), +8 size of the FXLite compiled-effect block at +12 (passed to FXLCreateEffect sub_88C8BA68),
  then RNAFX metadata (constants, structs, samplers, techniques, programs, permuted bools).
- Containers (vs_3_0/ps_3_0, assembler 2.0.11164.0) sit 4-aligned inside the FXL block, each after an 8-byte
  {handle, size} record. 93 effects are 1 VS + 1 PS; POSTFX has 43 containers; PLAYER_ZPASS is VS-only.
  particle_shaders_big's 18 entries are byte-identical to shader_big's of the same name.

## 2. Containers
- By header magic: 0x102A1100 PS 240; 0x102A1101 VS 156; **0x102A1121 VS 22**.
- 0x102A1121: flag 0x20 = a second shader is present. Header +0x1C/+0x20 point to a second definition table
  and shader: a position-only VS (1 vertex element), microcode after the main one, probably a Z-pass
  companion. All 22 are character materials (PLAYER_*, NPC_*, REFEREE_*, RINGGIRL_*, HAIR_*, PHYSIQUE,
  GPU_SKINNED_NIS_PROP).
- Semantics: VS inputs POSITION0, TEXCOORD0/1/7, NORMAL0, TANGENT0, BLENDINDICES0, BLENDWEIGHT0, COLOR0;
  interpolators TEXCOORD0-8, COLOR0, NORMAL0 (4 shaders).

## 3. Run-time compile?
- CreateVS/CreatePS's only direct caller is sub_88C881D0 (FXL): walks the VS list (+560/+564) and PS list
  (+572/+576), passes record+8 (the embedded container). It is called by FXLCreateEffect sub_88C8BA68 and by
  sub_88C8C038 (re-creates shaders when the device/pool slot at +700 changes; vtable methods 88A84F88, 88A85198).
- RNA loader sub_888A6B98 (from vtable method sub_888A6E00, "GameFXLiteShaderC", vtable 0x8803FDD4):
  1. sub_88A71DA8 returns 1 if data is NULL, 2 if `*(u16*)data != 0xA3D7`, else 0.
  2. 0: sub_88A71DE0 -> FXLCreateEffect (precompiled).
  3. Otherwise: sub_88A78AE0 -> sub_88C93B40 (FXL compile from source) -> D3DX effect compiler 88C8F2C8,
     HLSL front end 88C99478, Shader Assembler 88CF0048/88CF0470.
  4. On failure: compiles the "error" effect's text via vfunc+32 ("DATAERROR: RNA: Error loading effect '%s'").
- Every compiler/HLSL/assembler function roots at 888A6E00 -> 888A6B98 -> 88A78AE0 (plus two D3DX-internal
  vtable methods). The material binder sub_88A75A68 substitutes errorMissing.fx for unknown names;
  ERRORMISSING is precompiled in shader_big.
- Data: all 157 effects start with 0xA3D7; all 11 `*.fx` names in the DLL are in shader_big; across all
  decompressed data 34 of 41 effect names match; the real miss is `uiMovies.fx` (ui_big/bootload scripts).
  `data\xenon\shaders` exists only in a pointer table (0x88FD53AC); not on the disc (dev loose-file root).
- Recommendation: cache built offline from shader_big only. Log (or hook) sub_88A78AE0 to catch fallback
  compiles; map that case to the precompiled ERRORMISSING effect.

## 4. XenosRecomp
- Used UFC-native's built `XenosRecomp.exe` (UFC patch stack, DXIL); UFC Recomp's build gives identical results.
- All 418: the scanner accepts 396, skips the 22 0x102A1121 (field1C/field20 != 0). **395/396 converted**, no
  crash, no Defender block. 1 failure: 71c9fb2befac8551 (POSTFX PS), dxc error: loop uses run-time integer
  constant i16, which XenosRecomp never declares.
- The 22 with the second-shader fields zeroed and flag set to 0x1101: 19/22 converted; 3 failed "locatedInput"
  (vertex input TEXCOORD7 has no location): a563cfc17ec3537d, d2b31d00a61a2672, 8b7fb1c1ffeb1a64.
- **Fixes left (a new `patches/xenosrecomp-eamma.patch`):**
  1. Accept flag 0x20: translate the main shader (optionally the position-only second shader as its own
     entry). Hash the whole container either way.
  2. A TEXCOORD7 location in REBLUE_VERTEX_INPUT_LOCATIONS.
  3. Integer loop constants read from a constant buffer when no literal definition exists.
- None of UFC's problems seen: no memexport, no relative-constant palette issue, no NORMAL/FOG interpolators.
- Not checked: that the shaders render right; whether the game patches microcode at run time (needs the
  run-time hash census at CreateVS/PS).
