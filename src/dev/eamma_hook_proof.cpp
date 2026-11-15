// eamma - EN-0 hook proof (NATIVE-PLAN.md), linked into the game DLL target.
//
// The recompiled guest functions are weak (generated eamma_pch.h DEFINE_REX_FUNC), so a strong
// definition here replaces sub_8831C250 (the D3D Swap path: the DLL's only VdSwap caller) for every
// caller, and __imp__sub_8831C250 is the recompiled original. Observe-only: logs the first calls
// and then every 600th, then forwards unchanged.

#include <atomic>
#include <cstdint>

#include <rex/logging.h>
#include <rex/ppc/func.h>

extern "C" REX_FUNC(__imp__sub_8831C250);

extern "C" REX_FUNC(sub_8831C250) {
  static std::atomic<uint32_t> count{0};
  uint32_t n = count.fetch_add(1);
  if (n < 3 || n % 600 == 0)
    REXLOG_INFO("[hook] sub_8831C250 (swap) #{} r3 {:08X} r4 {:08X} r5 {:08X} lr {:08X}", n, ctx.r3.u32,
                ctx.r4.u32, ctx.r5.u32, static_cast<uint32_t>(ctx.lr));
  __imp__sub_8831C250(ctx, base);
}
