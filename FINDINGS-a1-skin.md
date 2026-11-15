# A1 fighter skin washed out: findings (2026-11-15, session 2)

Runs: `shots/n34` (draw log at 80 s, extended skin dump), `n35` (+ texture view dims), `n36cube` (skin slot 1 black),
`n37sss` (skin slot 2 black), `n38sssmap` (`SVR_PRESENT_TARGET=256x256`). All native, boot_taps, 1280x720.

## Result: the SSS map is the bad input
- Skin shaders: PLAYER_HEAD VS 632EAA11 / PS 7854AAED, PLAYER_BODY VS 29BDFC70 / PS D0D74FF5. Both fighters use the
  same VS/PS pair (n30 and n34), so the translator is not per-fighter; the difference is inputs.
- Blacking out skin texture slot 2 (`--eamma_native_skin_black_slots=4`, sssTextureSampler, the 256x256 resolve) makes
  both fighters evenly tan; every other run shows white patches with orange fringes on surfaces facing the arena lights
  (n37sss/090 vs n34/075, crops compared). Slot 1 (skinSpecCube) black: unchanged (n36cube/120).
- Skin PS: `r1.xyz = r1.xyz * sssMapMult.x + sss.xyz` with sssMapMult c13 = 2..2.5, so bright SSS texels go white.
- "One fighter fine, the other washed out" fits: the SSS map is one 256x256 atlas, a quadrant per mesh
  (sssUvTransform: head A (0.5,0.5,0,0) top-left, body A (..,0,0.5) bottom-left, head B (..,0.5,0) top-right,
  body B (..,0.5,0.5) bottom-right). The SSS pass VS c0 and the skin PS c14 agree per draw (n35).

## The SSS pass (per frame, before the main pass)
clear 256x256 target to 0x004C4C4C -> 4 draws (VS E91756D8 = PLAYER_BODY/HEAD@... 4103422cda8a69e3, PS 8767A8E6 =
94cb8e4df5b35638) rasterising each mesh in UV space (no depth, zfunc always) -> resolve -> 2 blur passes
(BeginVertices tri, PS CE74BC1E reading the resolve, writing the same target) -> resolve -> skin PS slot 2.
Resolve aliasing is safe here: BindDrawFramebuffer materialises outbound copies before each draw.

## Open lead (not proven): layout of the map
n38sssmap/090 (the map presented, colours not raw): the top half shows ONE mirror-symmetric face across the full
width, the bottom two mirror-image body pieces, i.e. it looks like head and body each drawn at full width (u scale 1,
offset ignored) instead of a quadrant each, so the two fighters overwrite each other and the skin PS reads the wrong
texels. Needs ground truth before acting: compare with the head's diffuse layout (face centred or not), or dump the
SSS-pass vertices (TEXCOORD0 is f10 = R16G16B16A16_FLOAT at +20, swapFloats/sintTexcoord path) and compute oPos.
If confirmed: the SSS VS's u path (`r6.z = u * sss.x + sss.z`, `oPos.x = 2u' - 1`) or the texcoord decode is wrong
for this declaration.

## Ruled out this session
- DEC3N w (advisor's top suspect): no translated shader reads .w of a decoded normal/tangent/binormal (grep of all
  418 HLSL). B5 stays a theoretical risk only.
- Normal maps: n30's "4x4 f20" normal slots were the pre-EN-5 fallback; n34+ bind real 512/1024 BC5.
  Xenia (SDK texture_cache.cpp) maps k_DXN to BC5 with RGGG and no R/G swap, as EN-5 does.
- Skin PS constants (c0..c40, c188..c199) and bools: per-fighter material values, no zeros/NaN/huge (n34 diff).
  PS bools all 0 in fights (enableIntroLighting / enable_outdoor_shadow_and_lighting off, plausible indoors).
- skinSpecCube: bound as a real cube (`[128x128 f61 cube]`); `cube()` emulation is fine for this use.
- B9 static check: EA's ViewportEnable setter sub_8830ABE0 stores 0x43F/0x400 at device+0x294C; HalfPixelOffset
  sub_8830AF38 writes bit 0 of +0x29C0. Both raw reads in constant_buffers.cpp are right for EA.

## Changes
- Draw log: skin PS dump keyed to EA's hashes (c0..c40, c188..c199, VS/PS bools); host textures show " cube"/" 3d" and
  " nodesc".
- `eamma_native_skin_black_slots` also matches PLAYER_HEAD/BODY PS.
- `SVR_PRESENT_TARGET=<W>x<H>`: present the last resolve out of a colour target of that host size. The address form
  now needs a whole hex string (`256x256` parsed as 0x256 crashed n38's first try).
- depthFlip gated on PA_CL_VTE_CNTL.VPORT_Z_SCALE_ENA (bit 4): with the viewport transform off the console applies no
  depth range. Not exercised by a test that would show it (no such draw seen).
- Constant capture: a full list (4096) drops its oldest quarter instead of the newest records (warns 4 times).
