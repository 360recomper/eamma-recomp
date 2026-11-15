// EN-3: draw the game's command-buffer objects directly (NATIVE-PLAN.md "Stop 1").
//
// EA's RNA renderer records some render objects into D3D command buffers once and replays them
// each frame. sub_885A1FE0(obj, arg) does that: byte obj+24 = "recorded"; not recorded -> record
// (sub_885A1340), then replay (sub_885A1430); it returns 1 when the replay ran. Its two callers
// (sub_885B4978, sub_885BFD18, render pass 4) fall back to the direct draw sub_884C3368(this, 4) when
// it returns 0, which is also what they do when the object has no command buffer at all (+192 /
// +168 null). eamma_command_buffers=false returns 0 here without recording or replaying, so the
// game draws that content through the ordinary (hooked) draw calls instead.

#include <rex/cvar.h>
#include <rex/ppc/func.h>

REXCVAR_DEFINE_BOOL(eamma_command_buffers, true, "eamma",
                    "Let the game record and replay D3D command buffers (render pass 4 objects). false = "
                    "draw them directly each frame (the game's own fallback path; for the native renderer)");

extern "C" REX_FUNC(__imp__sub_885A1FE0);

extern "C" REX_FUNC(sub_885A1FE0) {
  if (!REXCVAR_GET(eamma_command_buffers)) {
    ctx.r3.u64 = 0;
    return;
  }
  __imp__sub_885A1FE0(ctx, base);
}
