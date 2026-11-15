// eamma - VdSwap trace (diagnostic), linked into the game DLL.
//
// Wraps the DLL's VdSwap import: logs the front buffer fetch constant, address, format, colour
// space and size for the first swaps and then every 600th (~10 s), and forwards to the runtime.
// Written to chase the black picture at 60 fps (NOTES.md, boot 5).

#include <atomic>
#include <cstdint>
#include <cstdio>

#include <windows.h>

#include <rex/logging.h>
#include <rex/ppc/func.h>

namespace {
using ImportFn = void (*)(PPCContext&, uint8_t*);

uint32_t Be32(const uint8_t* base, uint32_t addr) {
  uint32_t v;
  memcpy(&v, base + addr, 4);
  return _byteswap_ulong(v);
}

ImportFn Runtime(const char* name) {
  HMODULE m = GetModuleHandleA("rexruntimerd.dll");
  if (!m) m = GetModuleHandleA("rexruntime.dll");
  return m ? reinterpret_cast<ImportFn>(GetProcAddress(m, name)) : nullptr;
}
}  // namespace

extern "C" REX_FUNC(__imp__VdSwap) {
  static ImportFn fn = Runtime("__imp__VdSwap");
  static std::atomic<uint32_t> count{0};
  uint32_t n = count.fetch_add(1);
  if (n < 3 || n % 600 == 0) {
    uint32_t f = ctx.r4.u32;
    uint32_t sp = ctx.r1.u32;
    uint32_t wp = Be32(base, sp + 0x54), hp = Be32(base, sp + 0x5C);
    REXLOG_INFO(
        "[swap] #{} fetch {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} front {:08X} format {:X} "
        "colorspace {:X} size {}x{} lr {:08X}",
        n, Be32(base, f), Be32(base, f + 4), Be32(base, f + 8), Be32(base, f + 12), Be32(base, f + 16),
        Be32(base, f + 20), ctx.r8.u32 ? Be32(base, ctx.r8.u32) : 0, ctx.r9.u32 ? Be32(base, ctx.r9.u32) : 0,
        ctx.r10.u32 ? Be32(base, ctx.r10.u32) : 0, wp ? Be32(base, wp) : 0, hp ? Be32(base, hp) : 0,
        static_cast<uint32_t>(ctx.lr));
    // Front buffer contents in guest memory (filled only with readback_resolve=full): how many
    // pixels are not black, and the brightest byte. Tells a black render from a black present.
    uint32_t front = ctx.r8.u32 ? Be32(base, ctx.r8.u32) : 0;
    if (front) {
      const uint8_t* px = base + front;
      uint32_t lit = 0, peak = 0;
      for (uint32_t i = 0; i < 1280u * 720u; ++i) {
        uint32_t v = px[i * 4] | px[i * 4 + 1] | px[i * 4 + 2] | px[i * 4 + 3];
        uint32_t rgb = px[i * 4 + 1] | px[i * 4 + 2] | px[i * 4 + 3];
        (void)v;
        if (rgb) ++lit;
        for (int c = 0; c < 4; ++c) peak = px[i * 4 + c] > peak ? px[i * 4 + c] : peak;
      }
      // Byte sums per position and three sample pixels (raw guest bytes, tiled order).
      uint64_t sum[4] = {};
      for (uint32_t i = 0; i < 1280u * 720u; ++i)
        for (int c = 0; c < 4; ++c) sum[c] += px[i * 4 + c];
      REXLOG_INFO("[swap] #{} front buffer: {} of {} pixels not black, peak byte {}, byte means "
                  "{:.1f} {:.1f} {:.1f} {:.1f}, samples {:08X} {:08X} {:08X}",
                  n, lit, 1280u * 720u, peak, sum[0] / 921600.0, sum[1] / 921600.0,
                  sum[2] / 921600.0, sum[3] / 921600.0, Be32(base, front + 4 * 1000),
                  Be32(base, front + 4 * 460000), Be32(base, front + 4 * 900000));
    }
  }
  if (fn) fn(ctx, base);
}
