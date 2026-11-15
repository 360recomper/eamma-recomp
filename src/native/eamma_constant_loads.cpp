// EN-5: shader constants EA MMA loads from memory (XDK GpuLoadShaderConstantF4Pointer).
//
// EA's RNA renderer never calls SetVertex/PixelShaderConstantF (EN-3 census). Its per-draw constants
// (world-view-projection and the rest, registers c156..c187 of both stages) reach the GPU as
// LOAD_ALU_CONSTANT packets that point at a constant buffer in memory, written by
// D3DDevice_GpuLoadShaderConstantF4Pointer sub_883198E0(dev, type 0 VS / 1 PS, start register,
// data, register count); its only caller is the renderer, sub_888B58E8. The GPU reads the data when
// it runs the packet, so the device's constant block never holds those registers, and the native
// renderer, which uploads from that block, drew every mesh with a zero matrix.
//
// The other half of that path: D3DDevice_GpuBeginShaderConstantF4 sub_88319858(dev, type, start
// register, register count) reserves a SET_CONSTANT packet in the ring and returns a pointer to its
// data; the caller (sub_888B5838) memcpys the constants there (bone palettes, crowd instance data).
// Those are recorded with the returned pointer and copied the same way.
//
// Each call is recorded here; at the next native draw (the game has filled the buffer by then) the
// registers are copied into the device's constant block, where constant_buffers.cpp reads them.
// The registers are GPU-owned for the game's D3D, so it neither flushes nor reads those slots.

#include <cstring>
#include <atomic>
#include <mutex>
#include <vector>

#include <rex/logging.h>

#include "core/memory_helpers.h"
#include "gpu/d3d.h"
#include "ufc_native.h"

namespace {
struct ConstantLoad {
  u32 device, type, start, data, count;
};
std::mutex g_mutex;
std::vector<ConstantLoad> g_pending;
}  // namespace

// Called by every native draw (hooks/draw.cpp DispatchDraw) before it reads the device's constants.
void EammaApplyConstantLoads(u32 device_guest) {
  std::vector<ConstantLoad> loads;
  {
    std::lock_guard lock(g_mutex);
    if (g_pending.empty())
      return;
    loads.swap(g_pending);
  }
  auto *dev = bd::mem::try_at<bd::gpu::D3DDevice>(device_guest);
  if (!dev)
    return;
  for (const ConstantLoad &l : loads) {
    if (l.device != device_guest || l.type > 1 || l.start >= 256 || !l.count)
      continue;
    const u32 count = l.start + l.count > 256 ? 256 - l.start : l.count;
    const auto *src = bd::mem::try_at<const u8>(l.data);
    if (!src)
      continue;
    // Both are big-endian guest floats: a straight copy.
    auto *dst = l.type == 0 ? &dev->vsFloatConstants[l.start][0] : &dev->psFloatConstants[l.start][0];
    std::memcpy(dst, src, size_t(count) * 16u);
  }
}

#if defined(D3DDevice_GpuLoadShaderConstantF4Pointer)
REX_EXTERN(__imp__D3DDevice_GpuLoadShaderConstantF4Pointer);
extern "C" REX_FUNC(D3DDevice_GpuLoadShaderConstantF4Pointer) {
  if (g_eamma_native) {
    static std::atomic<u32> s_logs{0};
    if (s_logs.fetch_add(1, std::memory_order_relaxed) < 4)
      REXLOG_INFO("native: constant load {} c{} x{} from {:08X}", ctx.r4.u32 ? "PS" : "VS", ctx.r5.u32,
                  ctx.r7.u32, ctx.r6.u32);
    std::lock_guard lock(g_mutex);
    if (g_pending.size() < 4096)
      g_pending.push_back({ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, ctx.r7.u32});
  }
  __imp__D3DDevice_GpuLoadShaderConstantF4Pointer(ctx, base);
}
#endif

#if defined(D3DDevice_GpuBeginShaderConstantF4)
REX_EXTERN(__imp__D3DDevice_GpuBeginShaderConstantF4);
extern "C" REX_FUNC(D3DDevice_GpuBeginShaderConstantF4) {
  const u32 dev = ctx.r3.u32, type = ctx.r4.u32, start = ctx.r5.u32, count = ctx.r6.u32;
  __imp__D3DDevice_GpuBeginShaderConstantF4(ctx, base);
  const u32 data = ctx.r3.u32;  // where the caller writes the constants next
  if (g_eamma_native && data) {
    static std::atomic<u32> s_logs{0};
    if (s_logs.fetch_add(1, std::memory_order_relaxed) < 4)
      REXLOG_INFO("native: constant ring write {} c{} x{} at {:08X}", type ? "PS" : "VS", start, count, data);
    std::lock_guard lock(g_mutex);
    if (g_pending.size() < 4096)
      g_pending.push_back({dev, type, start, data, count});
  }
}
#endif
