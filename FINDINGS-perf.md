# EA MMA native renderer: fight pacing and speed (WP1, 2026-11-15)

Status: **WP1 not passed, most of the gap closed.** With the two default changes below the native fight runs at 60
in most seconds (run w2b, 2x on Chi: 40 of 63 fight seconds at 59+, mean 56.5; the rest 49-53, and the 600-frame
average line still shows 48.8 for the first stretch of the fight). Emulated holds 60. Seven runs used; parked with
the leads below.

## Facts (each with its run)
- Emulated, laptop RTX 4060 (`eamma-p1-emu`, `perf_second_summary`): menus/intros 30 frames/s, fight 60 frames/s,
  highlights 30.
- Native, same script (`eamma-p1-native` laptop, `p1s` / `b1` / `b2` Chi): menus 30.0, fight 48.8 on both machines.
  Renderer zones sum to under 3 ms a frame and GPU time is under 1 ms on the 4090: not GPU cost, not host renderer cost.
- Fight frames are vblank-quantized: `[nperf]` p50 17 ms, p95 34 ms (run b1). About a quarter of the frames take two
  vblanks. The game paces itself on the guest vblank (SynchronizeToPresentationInterval `sub_8831BD10`, called from the
  frame end `sub_88A64B70`; interval at device +13808).
- Threads (stack samples, run p1s, `SVR_SAMPLE_STACKS=40`; the sampler really takes ~170 ms a sample, so 139 samples):
  the render thread is `FGGameRender`, the sim is `FGGameSim`, `Main_XThread` only sleeps (`sub_88335070`).
  FGGameRender in fight seconds: ~47% busy, 29% waiting on a guest object (`sub_88D7A0C8` <- `sub_888A57B0` <-
  `sub_8889D3E0` <- `sub_88D8BCB8`; FGGameSim waits in the same call 84% of the time), 11% asleep inside
  **XMPGetStatus**, 9% asleep in the host limiter `PaceFrame`.
- XMPGetStatus: the game polls the music player every frame on the render thread (`sub_884274E0` -> `sub_8840ECC0`
  -> `sub_88DF7308` -> `XMsgInProcessCall(0xFA, 0x70009)`), and the SDK's `XmpApp::XMPGetStatus` sleeps 1 ms on
  purpose (`EAMMA-rexglue-sdk/src/kernel/xam/apps/xmp_app.cpp:45`).
- The host limiter was hard-coded to 60 fps (`engine.h FPSLimit()`), a second 60 Hz clock beside the guest vblank.

## What was tried
| Run | Change | Fight result |
|---|---|---|
| b1 | `eamma_native_fps_limit=0` (new cvar) | 48.8, p50 17.1 / p95 33.9 |
| b2 | XMP poll once every 30 calls (`src/native/eamma_pacing.cpp`, `eamma_xmp_status_every`) | 48.5-48.8 |
| w2b | both as defaults, 2x | 57-60 in most seconds (p50 16.7, p95 17.7), some seconds 49-53; results screen reached ~45 s earlier than before |
| b3 | both | `[nperf]` 51-53 a second, p50 16.8 / p95 33.4; the 600-frame average line still said 48.8 |

Each sleep alone is enough to push a frame past 16.7 ms, so neither helped alone. Both are now defaults
(`eamma_native_fps_limit` 0, `eamma_xmp_status_every` 30; old path = 60 and 1).

## Ruled out
GPU cost (same number on a 4060 and a 4090, and at 1x and 2x); host draw/state/texture/geometry zones; the host
limiter alone; the XMP sleep alone; command lists or present (`sub`, `pres` under 0.6 ms).

## Leads, in order
1. In b3 the guest D3D zone `gd3d` is 10.5 ms a frame and `game` 4.8: with both sleeps gone, about 15 ms of a 16.7 ms
   budget is used before the vblank wait, so a slow frame still slips. Find what the 10.5 ms is: the `gwait` zone is
   not hooked in EA MMA (UFC hooks its block-on-fence body). Candidates: BlockOnFence `sub_8831B2A8`, BlockUntilIdle
   `sub_8831B1F0`, the ring KickOff `sub_8831AD40` (names from FINDINGS-en1-d3d-map.md, guesses). Hook them with
   `SvrZone kGuestWait` first, then decide on UFC's poll sleep (NR-P6d, off bit 524288).
2. A faster sampler on the render thread only (UFC's `ufc3_perf_probe` / `nr_symbolize.py --samples` were not ported).
3. Whether the "presents/s" line and `[nperf]` count the same thing (48.8 vs 52 in b3).
4. Game speed: not measured. The fight clock was not compared between renderers (the scripted fights diverge after
   the first exchange, and emulated ends by KO about 15 s earlier). Needs 1 s shots of the clock on both.
