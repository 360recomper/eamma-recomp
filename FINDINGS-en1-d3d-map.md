# EN-1: XDK D3D function map for EA Sports MMA (2026-11-15, research only)

Nothing built or run. Source: `generated/mmadllzf_xex/eamma_recomp.*.cpp` asm comments (game module `MMADLLzf.xex.dll`, base 0x88000000), indexed with scratch scripts (index, caller/callee queries, mnemonic-sequence match against UFC's NR-1 index, aligned UFC-vs-EA operand diff; temporary, not kept). Reference = UFC 3's `FINDINGS-nr1-d3d-map.md` / `src/native/ufc_d3d_names.h` / `ufc_d3d.h`. Written by a research agent; agent reports are leads (trap 22) until the EN-3 census checks them.

Method: every UFC D3D-range function (606) was matched against EA functions in 0x882E0000-0x8832B000 by mnemonic sequence (difflib ratio): 441 match at >=0.90, 84 at 0.70-0.90. Then each mapped pair was diffed instruction by instruction so only changed immediates show; every device offset below comes from that. "Conf":
- high = same code shape as a UFC function that is high confidence or census-confirmed, plus a device store or call pattern that proves it.
- med = shape match, but the name rests on UFC's own medium reasoning or the ratio is < 0.9.
- low = guess.

## Summary
1. **XDK: D3D9 2.0.11164.0.** The XEX static-library record (0x200FF) lists D3D9, D3D9LTCG, D3DX9, XGRAPHC and XBOXKRNL at 2.0.11164.0. Older than UFC 3 (D3D9 2.0.20500.0).
2. **The device has SvR 2010's layout, not UFC's.** Direct3D_CreateDevice sub_88308A68 allocates `li r3,24448` (0x5F80) but aligns the pointer to 128 itself and memsets `li r5,24320`: **device = 0x5F00 bytes** (SvR 0x5F00, UFC 0x5F80).
   - IB..shaders block equals SvR's values (IB 0x3144, RT 0x3148, DS 0x3158, streams 0x315C, stride 0x31A0, decl 0x2ED8, scissor 0x3234, PS 0x3244, VS 0x3248).
   - Past 0x3290: EA = UFC - 0x40 (saved ring end, recording CB, GPR buffers, front buffer).
   - Below 0x2AC0 (constants, fetch constants, dirty masks, ring): same as UFC.
3. **D3D runtime range:** 0x88303A88 - ~0x8832ABD8 core; 0x88300000-0x88303A88 holds D3D-lib helpers (not classified). Swap sub_8831C250 confirmed.
4. **43 names: 27 high, 10 med, 3 low, 3 not linked** (Present, SetViewportF, BeginIndexedVertices). EA also links DrawVerticesUP (7 callers), which UFC doesn't.
5. **No D3DDevice_Present.** The game's frame end sub_88A64B70 (vtable only; run-time lr 0x88A64C34 is its `bl Swap`) does, in order: SynchronizeToPresentationInterval thunk; Clear (flags 0x3F) or Resolve (flags 0x300) into its own front-buffer texture (game object +0xD08); SetShaderGPRAllocation(0,0); Swap(dev, frontBuffer, NULL); InsertFence-like sub_8831B270. The native present hook must live on Swap (or sub_88A64B70).
6. **Command buffers: one record site, one replay site.** Begin/End/RunCommandBuffer have 1 caller each, all RNA renderer wrappers under sub_885A1FE0. Strings "EnableCommandBuffers" (x3), "BeginCommandBuffer failed.\n" suggest an engine setting. Likely one cached object (UFC: ~2% of a fight frame); draws in it only measurable at run time.

## 1. Map (every name in ufc_d3d_names.h)

| Name | EA MMA | Evidence | Conf |
|---|---|---|---|
| D3DDevice_Present | **not linked** | Swap's 3 callers: sub_8831E0F0, sub_8831F578 (internal), game sub_88A64B70 (does Clear/Resolve + GPR + Swap itself). UFC sub_8237C3E8 best match 0.48 | n/a |
| D3DDevice_Swap | sub_8831C250 | Only `__imp__VdSwap` caller; also VdGetSystemCommandBuffer, VdSetDisplayMode, VdPersistDisplay. 0.83 vs UFC sub_8237BD60; frame counter `16772(r31)` (UFC 16840), present params `addi r10,r31,13852` (UFC 13916). Called as (dev, frontBufferTexture, NULL) | high |
| D3DDevice_SynchronizeToPresentationInterval | sub_8831BD10 | 1.00 vs UFC sub_8237B930. No-arg thunk sub_8831C248 (`li r4,0; b`) used by the frame end. Hook sub_8831BD10 | high |
| D3DDevice_Resolve | sub_88307738 | 1104 instrs, 0.84 vs UFC's internal resolver sub_8238C3A0. Public 11-arg function is the body (no UFC wrapper): dest texture `lwz r28,40(r16)`, flags `clrlwi r11,r4,29` (4 = DS `12632(r3)`, else RT `(idx+3154)*4`). 13 callers | high |
| D3DDevice_Clear | sub_8830FF18 | 1.00 vs UFC sub_8238E7B8, loops into ClearF. Frame end: Count 0, Flags 63 | high |
| D3DDevice_ClearF | sub_8830FFD8 | 0.97 vs UFC sub_8238E680; RT0 `12616(r3)`, DS `12632(r3)`; internal sub_8830F868; clear-draw emitter sub_8830EDE0 (1.00 vs UFC sub_8238D540, census counting point) | high |
| D3DDevice_SetRenderTarget | sub_8830C0E8 | `addi r11,r4,3154` + stwx = **+0x3148**. Alias sub_8830C0E0 (`b`, 5 game callers); body has 5 internal callers. Hook the body | high |
| D3DDevice_SetDepthStencilSurface | sub_8830C438 | `stw r4,12632(r3)` = **+0x3158**. 7 callers | high |
| D3DDevice_SetScissorRect | sub_8830BC80 | Rect `12852..12864(r3)` = **+0x3234**, clamped to viewport +0x3218, reads +0x2F00 | high |
| D3DDevice_SetViewport | sub_8830BDA0 | Integer to float, then internal sub_88309718 (`stfs f31,12824(r31)` = **+0x3218**) | high |
| D3DDevice_SetViewportF | **not linked** | sub_88309718's only callers: SetViewport, BeginTiling, BeginZPass. Hook sub_88309718 if a float path is needed | n/a |
| D3DDevice_BeginVertices | sub_8830E0B0 | stride/4 `stb r10,12704(r3)` = **+0x31A0**. 0.92. 3 callers (DrawVerticesUP + 2 game) | high |
| D3DDevice_BeginIndexedVertices | **not linked** | Draw block 0x8830E0B0-0x8830EDE0 holds Begin/End/DrawUP/Draw/DrawIndexed only | n/a |
| D3DDevice_EndVertices | sub_8830E548 | `lwz r11,13652(r3); stw r11,48(r3)` (ring = saved end **+0x3554**). 2 callers | high |
| D3DDevice_DrawVertices | sub_8830E5A0 | (dev, prim, start, count), DRAW_INDX auto-index. 0.95. 3 game callers | high |
| D3DDevice_DrawIndexedVertices | sub_8830E990 | (dev, prim, baseVertex, startIndex, indexCount), IB `12612(r31)`. 0.95. Callers sub_88A78F28 (main RNA draw), sub_88A790A8, sub_885D0E90 | high |
| D3DVertexBuffer_Unlock | sub_8830D3B8 | Identical to UFC sub_8236B540 (generic unlock sub_8830CE98). 1 caller sub_88A66120 | high |
| D3DIndexBuffer_Unlock | sub_8830D4C0 | Identical to UFC sub_8236B600. 1 caller sub_88A65450 | high |
| D3DDevice_CreateVertexDeclaration | sub_883116C0 | 1.00 vs UFC sub_82371CC0 (helper sub_883115D0). 1 caller sub_88A67288 | high |
| D3DDevice_CreateVertexShader | sub_88311238 | 1.00 vs UFC sub_823721B8: `li r5,872` memset, code at +872. Both creators called once from sub_88C881D0 | med |
| D3DDevice_CreatePixelShader | sub_88310E58 | 1.00 vs UFC sub_82371FD0, no 872 prefix | med |
| D3DDevice_SetTexture | sub_88305D10 | Fetch constant `(s+48)*24` = **+0x480+24s**; texture ptr `(3180+s)*4` = **+0x31B0**. 15 callers | high |
| D3DDevice_SetVertexShader | sub_88311328 | `stw r29,12872(r30)` = **+0x3248**. Get sub_883114F8 | high |
| D3DDevice_SetPixelShader | sub_88310F48 | `stw r29,12868(r30)` = **+0x3244**. Get sub_88311108 | high |
| D3DDevice_SetVertexDeclaration | sub_88311580 | `stw r4,11992(r3)` = **+0x2ED8**; identical to UFC's census-confirmed one. Get sub_88311598 | high |
| D3DDevice_SetStreamSource | sub_8830BE70 | VB `(3159+s)*4` = **+0x315C**, stride/4 `stb r9,12704(r11)` = **+0x31A0+s**. Get sub_8830BF90 | high |
| D3DDevice_SetIndices | sub_8830C018 | `stw r29,12612(r31)` = **+0x3144**. Get sub_8830C0A8 | high |
| D3DDevice_SetVertexShaderConstantF | sub_88310AB0 | `(reg+120)<<4` = **+0x780**, dirty `0(r3)`. Callers: 3 vtable thunks sub_88CF3E50/3EE0/3F68 | high |
| D3DDevice_SetPixelShaderConstantF | sub_88310B88 | `+376` = **+0x1780**, dirty `8(r3)`. Thunks sub_88CF4170/4200/4288 | high |
| D3DDevice_BeginCommandBuffer | sub_8831CEB0 | 0.92: `stw r30,13728(r31)` = recording CB **+0x35A0**. 1 caller sub_88A6CA10 | med |
| D3DDevice_EndCommandBuffer | sub_8831D4A8 | 0.93: clears +0x35A0. 1 caller sub_88A68DB0 (also calls sub_8831D880 = UFC sub_82372F58) | med |
| D3DDevice_RunCommandBuffer | sub_8831D9D8 | 0.94: CB flags +108, segment list +116, +112. 1 caller sub_88A6BB80 | med |
| D3DDevice_BeginZPass | sub_883069C0 | 0.86 (`stw r11,12896(r3)`). 1 caller sub_885D75B8 | low |
| D3DDevice_EndZPass | sub_88306C58 | 0.87. 1 caller sub_885D7678 | low |
| D3DDevice_BeginTiling | sub_88306170 | 0.96: tile count `stw r5,12924(r3)` = **+0x327C**, rects follow. Callers sub_883377D0, sub_884B67C0, sub_88A7C118 | med |
| D3DDevice_EndTiling | sub_883064E8 | 0.94, Resolve per tile. Callers sub_883377D0, sub_884B67C0, sub_88A708F8 | med |
| D3DQuery_Issue | sub_88317408 | 0.97. 1 caller sub_888BB248 | high |
| D3DQuery_GetData | sub_88317690 | 0.85. 1 caller sub_888A93E0 | med |
| D3DDevice_SetShaderGPRAllocation | sub_88311740 | 1.00: GPR flush buffer `15028(r31)`/`15032` (UFC 15092/15096), tables 13996/14000/14004. 7 callers incl. frame end (0,0) | high |
| __imp__D3DSurface_GetDesc | sub_88305BB0 | 1.00 vs UFC sub_8236E8B0 | med |
| __imp__D3DTexture_GetLevelDesc | sub_88304750 (thunk sub_88305760) | 1.00 vs UFC sub_8236D590 | med |
| __imp__D3DVolumeTexture_GetLevelDesc | sub_88304850 (thunk sub_88305858) | 1.00 vs UFC sub_8236D690; D3DVolume_GetDesc sub_88305CA8 | low |

Leave Present, SetViewportF and BeginIndexedVertices undefined so a hook naming one fails to compile.

## 2. Section-2 names and other matches

| Name | EA MMA | Evidence | Conf |
|---|---|---|---|
| Direct3D_CreateDevice | sub_88308A68 | `li r3,24448` to allocator sub_885E1480, align 128, `li r5,24320` memset, init sub_88325D58, create flags `stw r30,24192(r31)`. 1 caller sub_88346108 | high |
| D3DDevice_GetViewport | sub_8830BE20 | 1.00 | high |
| D3DResource_AddRef | sub_8830D0B0 | 1.00. 20 callers | high |
| D3DResource_Release | sub_8830D128 | 1.00. 34 callers | high |
| D3DResource_Destroy (internal) | sub_8830C8E8 | 0.73 | low |
| D3DTexture_GetSurfaceLevel | sub_88305658 | 0.90 | high |
| D3DSurface_LockRect | sub_88305C48 | 1.00 | med |
| D3DTexture_LockRect | sub_88305728 | 1.00 (lock body sub_88304EA0) | med |
| D3DTexture / Surface UnlockRect | sub_88305640 / sub_88305C90 | 1.00 | med |
| D3DDevice_CreateTexture | sub_88305928 | 1.00 | med |
| D3DDevice_CreateSurface | sub_88305A48 | 0.91 | med |
| D3DDevice_CreateVertexBuffer | sub_8830D2A0 | 1.00; not to be mapped (resource.cpp would replace it) | high |
| D3DDevice_CreateIndexBuffer | sub_8830D3C8 | 1.00; same note | high |
| D3DVertexBuffer_Lock | sub_8830D368 | 1.00 | high |
| D3DIndexBuffer_Lock | sub_8830D478 | 1.00 | high |
| **D3DDevice_DrawVerticesUP** (not in UFC) | sub_8830E558 | BeginVertices, memcpy count*stride, EndVertices inlined. 7 game callers sub_88A72750..B10 | high |

Other runtime functions: VS/PS bool constants sub_88310C60 (+0x2780) / sub_88310CC0 (+0x2790), int sub_88310D20 / sub_88310D78; GetTexture sub_88305EA0; GetFrontBuffer sub_8830C710 (`lwz r8,15040(r3)`); ring KickOff sub_8831AD40, reserve sub_88319960, init sub_8831B3A0; late init sub_883258F8 (front/back buffer); UnsetAll-like sub_88308D00; GPR buffer builder sub_883103F0; BlockUntilIdle-like sub_8831B1F0, IsFencePending sub_8831B310, BlockOnFence sub_8831B2A8, InsertFence-like sub_8831B270 (guess). Not found: GetRenderTarget, GetDepthStencilSurface, GetBackBuffer, D3DDevice_AddRef/Release, VIZ_QUERY survey functions.

## 3. Device fields (EA offset, proof, [UFC])

| Field | EA | Proof | [UFC] |
|---|---|---|---|
| Device size | 0x5F00 | CreateDevice memset `li r5,24320` | [0x5F80] |
| Dirty masks | +0x00..+0x28 | BeginVertices `ld 0..40(r31)` | same |
| Ring write ptr / limit | +0x30 / +0x34 | EndVertices, reserve sub_88319960 | same |
| Ring guarantee (?) | +0x38 | Swap only; not proven | same |
| Fetch constants | +0x480, 24 B each | SetTexture | same |
| VS / PS float constants | +0x780 / +0x1780 | ConstantF setters | same |
| VS / PS bool constants | +0x2780 / +0x2790 | bool setters | same |
| State flags byte | +0x2ABC | `10940(r31)` in SetRT, BeginCB, RunCB, BeginZPass | [0x2B3C] |
| Vertex declaration | +0x2ED8 | SetVertexDeclaration | [0x2F58] |
| Index buffer | +0x3144 | SetIndices | [0x318C] |
| Render targets 0-3 | +0x3148 | SetRenderTarget | [0x3190] |
| Depth stencil | +0x3158 | SetDepthStencilSurface | [0x31A0] |
| Stream sources (16) | +0x315C | SetStreamSource | [0x31A4] |
| Stream stride/4 (u8) | +0x31A0 | SetStreamSource, BeginVertices | [0x31E8] |
| Bound textures table | +0x31B0 | SetTexture | [0x31F8] |
| Viewport (6 floats) / flags | +0x3218 / +0x3230 | sub_88309718 | [0x3260 / 0x3278] |
| Scissor rect | +0x3234 | SetScissorRect | [0x327C] |
| Pixel / vertex shader | +0x3244 / +0x3248 | SetPS / SetVS | [0x328C / 0x3290] |
| ZPass active (low) | +0x3260 | BeginZPass | [0x32A8] |
| Tile count, rects follow | +0x327C | BeginTiling | [0x32C4] |
| Saved ring end | +0x3554 | Begin/EndVertices | [0x3594] |
| **Recording command buffer** | **+0x35A0** | Begin/EndCommandBuffer | [0x35E0] |
| Present params | +0x361C | Swap | [0x365C] |
| GPR base / table | +0x36AC / +0x36B0, +0x36B4 | sub_883103F0 | [0x36EC / 0x36F4] |
| **GPR-flush buffer** | **+0x3AB4** (+0x3AB8) | sub_883103F0, SetShaderGPRAllocation | [0x3AF4] |
| Front buffer | +0x3AC0 | late init, GetFrontBuffer | [0x3B00] |
| Back buffer (?) | +0x3AC4 | late init store only | [0x3B04] |
| Swap frame counter | +0x4184 | Swap | [0x41C8] |
| Create flags | +0x5E80 | CreateDevice | [0x5EC8] |

Command-buffer object and D3D resources (Common/RefCount/Fence, +0x18 address, VB tagged |3) same as UFC. Render-state call table not located (probably +0x40 as UFC).

## 4. Game-side caller layer
- RNA renderer device class at 0x88A64000-0x88A7D000, mostly vtable-reached: frame end sub_88A64B70; main draw sub_88A78F28 (+ sub_88A790A8); immediate helpers sub_88A72750..B10 (DrawVerticesUP); IB wrappers sub_88A65330/65430/65450, VB sub_88A66100/66120/66128; decl sub_88A67288/67300; state block sub_88A6BA70; CB wrappers sub_88A6CA10 / sub_88A68DB0 / sub_88A6BB80.
- Other callers: sub_8886A200 (2D/immediate path), sub_885D0E90 (DrawIndexed), sub_885D75B8/7678 (ZPass, GPR), sub_88B0BBB8, sub_883377D0 / sub_884B67C0 (tiling users), shader loader sub_88C881D0, effect-side sub_88C88048 / sub_88C8C0D8.
- Shader constants go only through 3+3 vtable thunks at 0x88CF3xxx (likely the D3DX/FXLite effect state manager). Only 4 non-D3D functions emit SET_CONSTANT PM4 (sub_88460D40, sub_887DC600, sub_88E2D3A8, sub_88E2E1E8). The census should count constant calls against draws.

## 5. Command buffers (Stop 1 input)
- Record: sub_885A1FE0 -> sub_885A1340 -> sub_888AE750 -> sub_88A6CA10 (BeginCommandBuffer, SetRT x4, SetDS, sub_88308C68); later sub_888AE6B8 -> sub_88A6CB58 -> sub_88A68DB0 (End).
- Replay: sub_885A1FE0 -> sub_885A1430 -> sub_888AE640 -> sub_88A6BB80 -> RunCommandBuffer. sub_885A1FE0 has 2 callers (sub_885B4978, sub_885BFD18).
- "EnableCommandBuffers" at 0x880CD390 / 0x88132418 / 0x88134B14 (no code xref; data tables?). `GPUCOMMANDOP_*` strings belong to an RNA PM4 dump routine (sub_88A644F8 via sub_88A6C340 <- sub_88A6D568), likely debug output.

## 6. Differences from UFC
- Not linked: Present, SetViewportF, BeginIndexedVertices, UFC's Resolve wrapper, D3DDevice_AddRef/Release, VIZ_QUERY survey, GetRenderTarget/GetDepthStencilSurface/GetBackBuffer.
- Linked only in EA: DrawVerticesUP.
- Older XDK: SvR 2010 device offsets, 0x5F00. Don't take UFC's `d3d.h` D3DDevice as-is; SvR 2010's re:Blue layout is the closer start; re-check +0x3290 and up against section 3.

## 7. Open items (census)
1. VS vs PS creator naming: check +0x3248 / +0x3244 at the first draw.
2. Draws per frame through the record/replay pair; re-recording during play.
3. Whether all shader constants go through the ConstantF setters.
4. ZPass (low), tiling (med), Query GetData (med) real callers in a fight.
5. Back buffer +0x3AC4, ring guarantee +0x38 unproven.
6. Frame end swaps game object +0xD08's texture; check it is the device's +0x3AC0.
7. Block 0x88300000-0x88303A88 not classified.
