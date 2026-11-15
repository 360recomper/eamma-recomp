// eamma - WP1 fight pacing (FINDINGS-perf.md).
//
// The game's render thread (FGGameRender) asks the music player for its state once a frame:
// sub_884274E0 -> sub_8840ECC0 -> sub_88DF7308 -> XMsgInProcessCall(0xFA, 0x70009) = XMPGetStatus. The SDK's
// XMPGetStatus sleeps 1 ms on purpose (games that hammer it), which Windows rounds up to 1-2 ms: about a tenth of
// the fight's frame budget on the thread that also draws. The fight is vblank-locked, so frames that go over
// 16.7 ms take two vblanks (48.8 presents a second instead of 60).
//
// sub_8840ECC0 returns the player state (0 idle / 1 playing / 2 paused, -1 unknown). The hook asks the SDK once
// every eamma_xmp_status_every calls and repeats the last answer in between. 1 = the old path (every call).

#include <atomic>
#include <cstdint>

#include <rex/cvar.h>
#include <rex/ppc/func.h>

REXCVAR_DEFINE_INT32(eamma_xmp_status_every, 30, "eamma",
                     "The game polls the music player state every frame and the SDK's answer sleeps 1-2 ms on "
                     "the render thread: ask once every this many calls and repeat the answer in between "
                     "(WP1 fight pacing). 1 = every call (the old path)");

extern "C" REX_FUNC(__imp__sub_8840ECC0);

extern "C" REX_FUNC(sub_8840ECC0) {
  static std::atomic<uint32_t> s_calls{0};
  static std::atomic<uint64_t> s_answer{0};
  const int32_t every = REXCVAR_GET(eamma_xmp_status_every);
  if (every <= 1 || s_calls.fetch_add(1, std::memory_order_relaxed) % uint32_t(every) == 0) {
    __imp__sub_8840ECC0(ctx, base);
    s_answer.store(ctx.r3.u64, std::memory_order_relaxed);
    return;
  }
  ctx.r3.u64 = s_answer.load(std::memory_order_relaxed);
}
