// EA Sports MMA addresses for the XDK D3D functions that re:Blue's renderer hooks by name.
//
// Force-included into the native renderer sources (cmake/native_renderer.cmake), so
// REX_HOOK(D3DDevice_Clear, ...) overrides the game DLL's recompiled sub_8830FF18 and
// __imp__D3DDevice_Clear calls its original body. The hooks are compiled into the game DLL target
// (eamma_MMADLLzf_xex), where these functions live (NATIVE-PLAN.md EN-0).
//
// Source: FINDINGS-en1-d3d-map.md (matched against UFC 3's map by code shape, each device offset
// from EA MMA's own stores; "med"/"low" there are checked by the EN-3 census). EA MMA links XDK
// 2.0.11164 (older than UFC's 20500): device offsets follow SvR 2010, see ufc_d3d.h.
//
// NOT linked in EA MMA, so deliberately left undefined (a hook naming them compiles to an unused
// function, never to a game function):
//   D3DDevice_Present            the game's frame end sub_88A64B70 calls Sync, Clear/Resolve,
//                                SetShaderGPRAllocation and Swap itself: present belongs on Swap.
//   D3DDevice_SetViewportF       internal float setter sub_88309718 has only SetViewport,
//                                BeginTiling and BeginZPass as callers.
//   D3DDevice_BeginIndexedVertices
// Linked in EA MMA only: D3DDevice_DrawVerticesUP sub_8830E558 (7 game callers; re:Blue has no hook).

#pragma once

// Device / frame
#define D3DDevice_Swap                        sub_8831C250
#define D3DDevice_SynchronizeToPresentationInterval sub_8831BD10
#define D3DDevice_Resolve                     sub_88307738
#define D3DDevice_Clear                       sub_8830FF18
#define D3DDevice_ClearF                      sub_8830FFD8
#define D3DDevice_SetRenderTarget             sub_8830C0E8  // body; sub_8830C0E0 is a `b` alias the game calls
#define D3DDevice_SetDepthStencilSurface      sub_8830C438
#define D3DDevice_SetScissorRect              sub_8830BC80
#define D3DDevice_SetViewport                 sub_8830BDA0

// Draws
#define D3DDevice_BeginVertices               sub_8830E0B0
#define D3DDevice_EndVertices                 sub_8830E548
#define D3DDevice_DrawVertices                sub_8830E5A0
#define D3DDevice_DrawIndexedVertices         sub_8830E990
// EA only: BeginVertices + memcpy + EndVertices *inlined*, so EndVertices' hook never sees these
// draws (the attract / logo movies are one such quad a frame). Hooked in gpu/hooks/draw.cpp.
#define D3DDevice_DrawVerticesUP              sub_8830E558

// Buffers: matched but NOT mapped (gpu/hooks/resource.cpp's REX_HOOKs would replace them):
// D3DDevice_CreateVertexBuffer sub_8830D2A0, D3DDevice_CreateIndexBuffer sub_8830D3C8,
// D3DVertexBuffer_Lock sub_8830D368, D3DIndexBuffer_Lock sub_8830D478.
#define D3DVertexBuffer_Unlock                sub_8830D3B8
#define D3DIndexBuffer_Unlock                 sub_8830D4C0

// Resource creation (VS/PS names: med, from UFC's 872-byte VS prefix; census open item 1)
#define D3DDevice_CreateVertexDeclaration     sub_883116C0
#define D3DDevice_CreateVertexShader          sub_88311238
#define D3DDevice_CreatePixelShader           sub_88310E58

// State
#define D3DDevice_SetTexture                  sub_88305D10
#define D3DDevice_SetVertexShader             sub_88311328
#define D3DDevice_SetPixelShader              sub_88310F48
#define D3DDevice_SetVertexDeclaration        sub_88311580
#define D3DDevice_SetStreamSource             sub_8830BE70
#define D3DDevice_SetIndices                  sub_8830C018
#define D3DDevice_SetVertexShaderConstantF    sub_88310AB0
#define D3DDevice_SetPixelShaderConstantF     sub_88310B88
// EA's real constant path (EN-5): LOAD_ALU_CONSTANT from a memory pointer, see eamma_constant_loads.cpp.
#define D3DDevice_GpuLoadShaderConstantF4Pointer sub_883198E0

// Recorded work (one record site and one replay site in the game, FINDINGS-en1 §5)
#define D3DDevice_BeginCommandBuffer          sub_8831CEB0
#define D3DDevice_EndCommandBuffer            sub_8831D4A8
#define D3DDevice_RunCommandBuffer            sub_8831D9D8
#define D3DDevice_BeginZPass                  sub_883069C0  // low
#define D3DDevice_EndZPass                    sub_88306C58  // low
#define D3DDevice_BeginTiling                 sub_88306170
#define D3DDevice_EndTiling                   sub_883064E8
#define D3DQuery_Issue                        sub_88317408
#define D3DQuery_GetData                      sub_88317690
#define D3DDevice_SetShaderGPRAllocation      sub_88311740

// Device fields the census and hooks read (ufc_d3d.h has the full layout).
#define EAMMA_D3D_DEVICE_RECORDING_CB         0x35A0  // BeginCommandBuffer stores the buffer here

// Original bodies (__imp__ = the recompiled function, for hooks that call through).
#define __imp__D3DDevice_Swap                 __imp__sub_8831C250
#define __imp__D3DDevice_SynchronizeToPresentationInterval __imp__sub_8831BD10
#define __imp__D3DDevice_Resolve              __imp__sub_88307738
#define __imp__D3DDevice_Clear                __imp__sub_8830FF18
#define __imp__D3DDevice_ClearF               __imp__sub_8830FFD8
#define __imp__D3DDevice_SetTexture           __imp__sub_88305D10
#define __imp__D3DDevice_SetRenderTarget      __imp__sub_8830C0E8
#define __imp__D3DDevice_SetDepthStencilSurface __imp__sub_8830C438
#define __imp__D3DDevice_SetScissorRect       __imp__sub_8830BC80
#define __imp__D3DDevice_SetViewport          __imp__sub_8830BDA0
#define __imp__D3DDevice_BeginVertices        __imp__sub_8830E0B0
#define __imp__D3DDevice_EndVertices          __imp__sub_8830E548
#define __imp__D3DDevice_DrawVertices         __imp__sub_8830E5A0
#define __imp__D3DDevice_DrawIndexedVertices  __imp__sub_8830E990
#define __imp__D3DDevice_DrawVerticesUP       __imp__sub_8830E558
#define __imp__D3DDevice_CreateVertexDeclaration __imp__sub_883116C0
#define __imp__D3DDevice_CreateVertexShader   __imp__sub_88311238
#define __imp__D3DDevice_CreatePixelShader    __imp__sub_88310E58
#define __imp__D3DDevice_SetVertexShader      __imp__sub_88311328
#define __imp__D3DDevice_SetPixelShader       __imp__sub_88310F48
#define __imp__D3DDevice_SetVertexDeclaration __imp__sub_88311580
#define __imp__D3DDevice_SetStreamSource      __imp__sub_8830BE70
#define __imp__D3DDevice_SetIndices           __imp__sub_8830C018
#define __imp__D3DDevice_SetVertexShaderConstantF __imp__sub_88310AB0
#define __imp__D3DDevice_SetPixelShaderConstantF  __imp__sub_88310B88
#define __imp__D3DDevice_GpuLoadShaderConstantF4Pointer __imp__sub_883198E0
#define __imp__D3DVertexBuffer_Unlock         __imp__sub_8830D3B8
#define __imp__D3DIndexBuffer_Unlock          __imp__sub_8830D4C0
#define __imp__D3DDevice_BeginCommandBuffer   __imp__sub_8831CEB0
#define __imp__D3DDevice_EndCommandBuffer     __imp__sub_8831D4A8
#define __imp__D3DDevice_RunCommandBuffer     __imp__sub_8831D9D8
#define __imp__D3DDevice_BeginZPass           __imp__sub_883069C0
#define __imp__D3DDevice_EndZPass             __imp__sub_88306C58
#define __imp__D3DDevice_BeginTiling          __imp__sub_88306170
#define __imp__D3DDevice_EndTiling            __imp__sub_883064E8
#define __imp__D3DQuery_Issue                 __imp__sub_88317408
#define __imp__D3DQuery_GetData               __imp__sub_88317690
#define __imp__D3DDevice_SetShaderGPRAllocation __imp__sub_88311740

// Resource description getters (med/low; the re:Blue hooks only need them to link until EN-5).
#define __imp__D3DSurface_GetDesc             __imp__sub_88305BB0
#define __imp__D3DTexture_GetLevelDesc        __imp__sub_88304750
#define __imp__D3DVolumeTexture_GetLevelDesc  __imp__sub_88304850
