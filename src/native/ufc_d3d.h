// EA Sports MMA's XDK D3DDevice layout (namespace kept as ufc3::d3d so the copied UFC code reads it).
//
// EA MMA links XDK 2.0.11164, older than UFC 3's 20500 (FINDINGS-en1-d3d-map.md §3). The constants
// block and register shadows match UFC/SvR (to +0x2AC0); the IB..shaders block equals SvR 2010's
// offsets; past +0x3290 EA = UFC - 0x40. Device = 0x5F00 bytes (Direct3D_CreateDevice sub_88308A68
// allocates 24448, aligns to 128 by hand and memsets 24320). Each offset names the EA function whose
// store proves it. Nothing here is checked at run time yet: the EN-3 census does that.

#pragma once

#include <cstdint>

namespace ufc3::d3d {

constexpr uint32_t kDeviceSize = 0x5F00;

// Dirty masks and the ring (KickOff / MakeSpace sub_8831AD40).
constexpr uint32_t kDirtyMasks = 0x000;         // be_u64[5]
constexpr uint32_t kRing = 0x030;               // write pointer; BeginCommandBuffer resets it to 4
constexpr uint32_t kRingLimit = 0x034;
constexpr uint32_t kRingGuarantee = 0x038;

// Constants block: same as SvR 2010.
constexpr uint32_t kFetchConstants = 0x480;     // SetTexture sub_88305D10: (sampler + 48) * 24
constexpr uint32_t kVsFloatConstants = 0x780;   // SetVertexShaderConstantF sub_88310AB0
constexpr uint32_t kPsFloatConstants = 0x1780;  // SetPixelShaderConstantF sub_88310B88
constexpr uint32_t kVsBoolConstants = 0x2780;   // sub_88310C60
constexpr uint32_t kPsBoolConstants = 0x2790;   // sub_88310CC0
constexpr uint32_t kRbDepthControl = 0x2934;    // register shadow (SvR RB_BLENDCONTROL0 at +0x2938)
constexpr uint32_t kPaScWindowScissor = 0x28C4; // UFC: SetScissorRect writes +0x28C4/+0x28C8 (not checked in EA)

// Past +0x2AC0: EA MMA offsets (SvR 2010 values up to +0x3290, then UFC - 0x40).
constexpr uint32_t kStateFlags = 0x2ABC;        // byte (10940): SetRT, BeginCB, RunCB, BeginZPass
constexpr uint32_t kVertexDeclaration = 0x2ED8; // SetVertexDeclaration sub_88311580 (UFC +0x2F58)
constexpr uint32_t kIndexBuffer = 0x3144;       // SetIndices sub_8830C018 (UFC +0x318C)
constexpr uint32_t kRenderTarget0 = 0x3148;     // SetRenderTarget sub_8830C0E8: (idx + 3154) * 4, 4 slots
constexpr uint32_t kDepthStencil = 0x3158;      // SetDepthStencilSurface sub_8830C438 (UFC +0x31A0)
constexpr uint32_t kStreamSource = 0x315C;      // SetStreamSource sub_8830BE70: (3159 + s) * 4, 16 streams
constexpr uint32_t kStreamStrideDiv4 = 0x31A0;  // u8 per stream; BeginVertices writes stream 0's
constexpr uint32_t kTexture = 0x31B0;           // SetTexture sub_88305D10: (3180 + s) * 4
constexpr uint32_t kViewport = 0x3218;          // SetViewport -> sub_88309718 (6 floats to +0x322C)
constexpr uint32_t kViewportFlags = 0x3230;
constexpr uint32_t kScissorRect = 0x3234;       // SetScissorRect sub_8830BC80
constexpr uint32_t kPixelShader = 0x3244;       // SetPixelShader sub_88310F48
constexpr uint32_t kVertexShader = 0x3248;      // SetVertexShader sub_88311328
constexpr uint32_t kTileCount = 0x327C;         // BeginTiling sub_88306170: stw r5 (count), rects follow
constexpr uint32_t kZPassActive = 0x3260;       // BeginZPass sub_883069C0: stw at 12896 (low confidence)
constexpr uint32_t kSavedRingEnd = 0x3554;      // EndVertices sub_8830E548: ring = saved end
constexpr uint32_t kRecordingCommandBuffer = 0x35A0;  // BeginCommandBuffer sub_8831CEB0: stw cb,13728
constexpr uint32_t kPresentParams = 0x361C;     // Swap sub_8831C250
// SetShaderGPRAllocation sub_88311740 emits INDIRECT_BUFFERs {size dwords, physical address}
// (UFC NR-6b; same code shape in EA MMA):
constexpr uint32_t kGprBaseIb = 0x36AC;         // GPR buffer builder sub_883103F0
constexpr uint32_t kGprTableIb = 0x36B4;        // + vs_gprs * 8 (SetShaderGPRAllocation reads 14004)
constexpr uint32_t kGprFlushIb = 0x3AB4;        // 24 x DRAW_INDX_2 point list (pipe flush), built by sub_883103F0
constexpr uint32_t kFrontBuffer = 0x3AC0;       // late init sub_883258F8, GetFrontBuffer sub_8830C710
constexpr uint32_t kBackBuffer = 0x3AC4;        // late init store only (unproven; EA has no Present)
constexpr uint32_t kCreateFlags = 0x5E80;

// Command buffer object (RunCommandBuffer sub_8831D9D8, same layout as UFC): flags +108, segment list +116
// (node {next, count, entries[count] {size dwords in the low 24 bits, physical address}}).
constexpr uint32_t kCbFlags = 108;
constexpr uint32_t kCbSegments = 116;

// D3DQuery (D3DQuery_Issue sub_88317408): device +0, type +4 (9 = occlusion / ZPD, 10 = event).
constexpr uint32_t kQueryDevice = 0;
constexpr uint32_t kQueryType = 4;

static_assert(kVertexShader == kPixelShader + 4);
static_assert(kStreamSource == kDepthStencil + 4);
static_assert(kRecordingCommandBuffer < kDeviceSize && kCreateFlags < kDeviceSize);

}  // namespace ufc3::d3d
