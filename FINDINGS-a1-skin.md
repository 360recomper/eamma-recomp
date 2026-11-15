# A1 fighter skin washed out: findings (2026-11-15, session 2)

**Fixed (n39loop): XenosRecomp ignored the Xenos loop start/step.** Loop constants are (count, start, step); the
translator always ran `aL = 0..count-1`. The POST_SSS blur PS CE74BC1E (`i31 = (12, 1, 1)`, the only loop in the game
with a non-zero start) takes its centre tap itself and then loops taps 1..12; translated it re-added tap 0 and dropped
tap 12, a per-channel gain of roughly 1.1-1.6 per pass, applied twice, clipping the SSS map. The skin PS adds the map
after `* sssMapMult` (2..2.5), so lit skin went white with orange fringes. Fix: `shader_recompiler.cpp` emits
`for (aL = i.y, aLi = 0; aLi < i.x; aLi++, aL += i.z)` (aLi = iteration counter; same for the non-structured path).
No-op for every other shader (all other loops start 0 step 1, or step 0 and never read aL). Found by the advisor
(Fable) from the shader's loop constant. n39loop/060: skin shaded and tan like emulated en3d/120; n40sssmap/060: the
SSS map is neutral grey with each mesh in its own region, so the "full width" layout lead below was a red herring
(the earlier presented map was the over-bright, blue-shifted one).

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

## A5: menu fighter face patchwork (open, 2026-11-15 later)
Native menu fighter (~10-15 s, `shots/n39loop/015.png` vs emulated `en3d/015.png`): hard-edged patches of different
lighting on face/chest, black seams on the head top, a black line across the neck/collarbone. The A2 halo is gone.
- Patchwork = SSS map content: blacking skin slot 2 removes it (n41menu). The neck line stays with slots 2 and the
  shadow slots blanked (n44neck): separate cause, probably per-vertex/geometry (head/body seam).
- The skin PS's SSS lookup UV is a smooth gradient over the head (head PS override showing `r0.xy`, n50uv): the lookup
  is right; the map content is wrong.
- SSS-pass PS instrumented (override, n48dbg: R = shadow factor `r5.z`, G = `min(ao^2, ssao)`, B = N.L): G and B are
  clean; R shows contour rings (~0.7-0.9) and hard jagged steps on lit surfaces = self-shadow acne in the variance
  shadow test. The shadow map (n51shadow, 512x512 k_8_8_8_8 resolve; R ~0.25 = d, G ~0.03 = d^2) is 8-bit, so the
  variance is near precision limits; the console reads the same 8-bit texture, so the open question is what makes the
  receiver's d differ from the caster's on host.
- Ruled out: AO/SSAO term (G clean, SSAO buffer n47ssao plausible), blur radius (BlurKernel.x/256 < 1 texel by design),
  half-pixel offset (`SVR_NO_HALF_PIXEL=1`, n49nohp: unchanged), sampler filters (device fetch constants: linear
  mag/min on every slot incl. the shadow map, n52fc), loop handling.
- Fixed on the way: **XenosRecomp dropped `setTexLOD`** (register-LOD fetches sampled the computed level). Now
  `SetTextureLod` stores `texLod` and `useRegLod` fetches call `tfetch2D/3D/CubeLevel(..., texLod + lodBias/32)`.
  8 shaders use it (SSS pass: normal/flex/AO at level 3). Did not remove the patchwork by itself.
- Next: compare caster vs receiver d for one texel (log the caster VS 11F5B37D outputs for a vertex and the SSS VS
  E91756D8 for the same vertex), check the shadow-map VSM blur pass (64x64? shader 739d6a448545923c), and the receiver
  bias constants Shadow_Bias_Epsilon c9 = (0.012, 0.001), Shadow_Adjuster c10 = (0.96, 1, 1, 1).

## Dev tools added
- `XENOS_RECOMP_HLSL_OVERRIDE=<dir>` (env, at cache build): `<dir>/<HASH>.hlsl` replaces that shader's translation.
  Scratch generators used: mkdebug.py (SSS PS terms), mkuvdbg.py (head PS lookup UV / SSS sample). Rebuild the normal
  cache afterwards (`python -I tools/nr_build_shader_cache.py` without the env).
- Draw log: SSS PS 8767A8E6 in the full register dump; device-side fetch constants (dword0/dword3) for slots 0-13.
- `SVR_NO_HALF_PIXEL=1` (env): drop the D3D9 half-pixel offset (A/B only).

### A5 update (advisor round 3 + runs n53-n61)
- **Isolated: the variance (Chebyshev) branch of the SSS pass's shadow test.** Override `r5.z = r3.w` (hard test only)
  removes the patchwork; the face then matches emulated (n58hard vs e54menu/012, same pose). Not a fix: the game uses
  `max(hard, p)`.
- Moments: var = m2 - m1^2 (R x200, n59var) is striped on smooth lit areas, above the ~0.003 that 8-bit storage alone
  gives; diff on lit areas ~ -bias as expected. Bypassing the shadow-map blur (#18 A86CC2D0, n60noblur) does not help
  (patchier), so the noise is in the caster output itself.
- Ruled out this round: handedness/TEXCOORD0.w (it is the flex-normal blend weight; zero in guest data too; vfetch
  formats come from the declaration, verified by decoding the containers), DEC3N w (masked), cull, MAX blend mapping,
  shadow RT format (guest D3DFMT 0x18280186 = 8888 for every target, n61rtfmt), emulated resolution scale (1x, ROV path).
- Side finding: the main 1280x720 scene surface has MSAA bits set in RB_SURFACE_INFO (0x14020500): the console renders
  it 4x MSAA; native doesn't.
- Open: why the caster's (d, d^2) pair carries more variance natively. Next ideas: dump caster VS/PS outputs for one
  vertex (log), compare interpolation of d^2 vs d (centroid/sample interpolation flags in the caster PS inputs), and
  check whether the caster PS writes oC0 from interpolators declared `centroid` (MSAA-related) on console.

## A3 light shafts: translated (n57shafts)
XenosRecomp now reads loop (integer) constants it doesn't define from `g_LoopConstants` (shared constants c64..c71,
filled from device +0x27A0: VS i0-15 then PS i0-15, one dword `count | start << 8 | step << 16`; EA setters
sub_88310D20 / sub_88310D78 store at (2536+i)*4 / (2552+i)*4). POSTFX 71c9fb2b (i16 = PS i0) compiles: 418/418, and
the run no longer logs "shader miss 1CCBA38AC6B29550". The shafts were not seen on screen yet (no logged frame drew
that shader); fight/menus unchanged, no hang.

## A5 FIXED: stale geometry (runs n65-n73)
The real cause was not shader math: **the game rewrites the fighters' skinned vertex buffers between draws of one
frame without an Unlock**, and the native geometry mirror compared each buffer once a frame. Shadow caster, SSS and
main passes then drew different vertex sets, so the SSS map's shadow term and the main pass disagreed (lighting
patches along UV islands), and triangles mixing old/new vertices opened cracks (the black neck/chest/head lines).
- `--eamma_native_geometry_every_draw=true` alone fixes the menu face and all black lines (n66); distrusting stable
  buffers alone does not (n67). Check mode (n68) reports mid-frame rewrites of 334 KB / 145 KB / 39 KB vbs.
- A "changed in the last 60 frames -> compare at every use" rule fixed the menu (n69) but a fight still showed black
  blotches once (n70 060), so **every-draw comparison is now the default** (`eamma_native_geometry_every_draw` true;
  ~1 ms/frame of hashing on a 4090 box, 30 fps held, n72perfA/B). The 60-frame rule stays for when it is turned off.
- n73default: 16 frames across intro, fight, pause, KO, highlights clean.
- Everything the A5 shader experiments "found" (variance branch, acne) was downstream of the mismatched geometry.
  Lesson (also for C-list): when two passes of the same mesh disagree, verify the vertex data first.

- A3 follow-up (n74watch, `SVR_LOG_PS=1CCBA38AC6B29550`): the light-shaft PS is never drawn in boot -> menus ->
  fight -> highlights (every draw path reports to the watch). Needs another scene (other venue / walkout).
