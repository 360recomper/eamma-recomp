# EA MMA native renderer: handoff 2026-11-22

Branch `native`, last commit 62ab2d4, working tree clean, nothing pushed (never push without the user's OK).
The build in `out/build/win-amd64-relwithdebinfo` matches HEAD, and its shader cache is the normal one (not a debug override).
Long-form history: `NATIVE-HANDOFF.md`. Issue table: `NATIVE-ISSUES.md`. Plan and work packages: `NATIVE-FINISH-DESIGN.md`.

## Where it stands
Boot, menus, movies, fighter select, intros, fights and highlights all render natively. Intros and fights match the
emulated renderer in the shots compared so far. Fights hold 60 fps on the 4090 box at 1440p.

Fixed in the last two sessions (2026-11-21/22):
| Issue | What it was | Fix (commit) | Verified |
|---|---|---|---|
| A9 twitching: seam cracks, limbs detaching, black flicker, referee head | The game's job threads write skinned vertex buffers and pointer-loaded constants (bone palettes, matrices) after the draw call. The console GPU reads them later. | End-of-frame refresh of vertex copies and constants (rounds 1-5, b4e93f2..2afa7ce) | User play test: "twitching seems gone" |
| A10 white/milky eyes in intros | Round 3's constant refresh re-copied registers from an old pointer-load address, even after the effect code had written them directly into the device block. This corrupted the eye shader's c2/c3 (isLeftEye/eyeUVOffset). | A register keeps its pointer source only while it still holds the bytes that load put there (`EammaConstantSourceStillValid`, 8ffe557) | Self-test i15 vs emulated e1. **User has not played it yet.** |
| A12 intro arena lighting missing (beams, star glare, coloured stage lights) | Same cause as A10: light particle constants overwritten | Same commit 8ffe557 | Run l1 matches e1 shot for shot. **User has not played it yet.** |
| A11 hard-edged neck/chest shadow | Not a bug: emulated shows the same shadow | - | e1 vs i1 |

**First thing next session:** get the user's play test of HEAD (intros and one round). This checks the eyes, the lighting,
and that the twitch is still gone (A10's fix removed ~10k bogus refreshes per 600 fight frames; ~30k genuine ones remain, run g11).
Give it as a Run button:
```powershell
& "E:\ClaudeGame\EAMMA Recomp\play-native.bat"
```

## What's left (most useful first)
1. **Play test of HEAD** (above). If any twitch is back, compare g11 vs g9 counts. `eamma_native_constant_late_fix=false` turns the constant refresh off for A/B.
2. **A4 mode sweep (WP5)**: career, create-a-fighter, other venues, replays have never been compared with emulated.
   Needs new input scripts under `tools/selftest_scripts/` (or a human session).
   Light shafts (A3, PS 1CCBA38A) only show in the career gym or venues with `enableLightShafts`.
3. **A8 pause menu**: the yellow highlight bar behind RESUME was missing at 1x in one run; a dark swirl on the mat at 2x. One emulated/native pair with 1 s shots decides it.
4. **A7 / WP6 hair**: alpha-to-mask hair cards get a 0.5 alpha test. Not yet judged against emulated on a long-haired fighter.
5. **B12 / WP1 pacing**: fight at 60 presents/s in most seconds; game speed not compared with emulated. Next leads are in `FINDINGS-perf.md`.
6. **WP7 leftovers**: FSR run, exit-4 check, a launcher-built shader cache loaded by the game. **WP10**: launcher not yet run against the real game. **WP8** perf ladder/soak, **WP9** MSAA (only on the user's go).
7. **Known holes in the A9 refresh** (no symptom seen): buffers over 4 MB are not tracked. A mid-frame list submit before the jobs finish cannot be fully fixed without hooking the game's job-completion sync. Gemini's suggested redesign: defer copies to submit time or hook the job barrier.
8. **Release risks** (NATIVE-FINISH-DESIGN.md section 5): intermittent profile-save crash (~15 s, both renderers); reversed-Z untested with the viewport transform off; B9 offsets assumed.

## Lessons from this round (don't repeat)
- **Compare against a capture from the current build.** A12 was chased for an hour on a pre-fix native capture; it was already fixed.
- **Late-write fixes can corrupt state.** Effect code (FXL) writes constants and bools straight into the device block,
  and no hook sees those writes. Anything that re-copies "pointer-loaded" registers must check that the register still holds what the load put there.
- Emulated is the reference, but intro timing differs: emulated runs ~45 s behind native with `intro_watch.txt`. Match shots by scene, not by time.
- The game's bool setter (sub_88310C60) stores bit n as `1 << (n & 31)` at device +0x2780 + 4*(n/32), as native reads it. Particle bools are set per draw through it (caller 0x88C942D4, FXL).

## Tools that worked
- `py -I tools/eamma_selftest.py <run> --script tools/selftest_scripts/intro_watch.txt --renderer native|emulated --secs N --shots ...`
  Native timings: intro close-ups at 73-109 s, wide arena at 55/58 s, tale of the tape at 46 s.
  `fight_burst.txt` drives the fight (no START after 42 s). Results are in `E:\ClaudeGame\EAMMA-tests\selftest\<run>`.
- `--set eamma_native_draw_log_at=<s>`: every draw of that frame. It prints the eye constants, the `lynx` particle constants and the post-shader c0-c3.
  Note: the "host blend" values it prints belong to the previous draw (logged before the state is set).
- Env `SVR_LOG_EYE=1`: the eye draws' constants and textures, every 30 frames.
- **Shader A/B:** dump HLSL with `XenosRecomp.exe <dir of .bin> out.bin third_party/XenosRecomp/XenosRecomp/shader_common.h <dumpdir>`,
  edit a copy, then `XENOS_RECOMP_HLSL_OVERRIDE=<dir>` plus `py -I tools/nr_build_shader_cache.py` (5 s).
  **Restore the normal cache afterwards** (rebuild without the env, or copy back a backup of shader_cache.bin + shader_cache_linked.bin).
- Shader hash = XXH3-64 of the container. Effect name to hash: `tools/eamma_shader_extract.py <dir>` gives `_sources.txt`,
  then hash each .bin (third_party/XenosRecomp/thirdparty/xxHash/xxhash.h; Python has no xxhash module installed).
  Eye: VS B145794E / PS B98DE7C0. Glare: VS 1CFB2126 / PS 45AD553C. Light cone: VS DC741F49 / PS 15A17363.
  Bloom: 17218A64 (downsample, rgb x alpha), 89B187F8 (cross-star blur), 371B92EA (composite, bloomWeight 0.2).
- Build: from PowerShell, `cmd /c ".\build.bat native > build-native.log 2>&1"`. If XenosRecomp.exe is locked, retry once.

## Rules
- GPU: Rumble Roses has priority. Don't launch while `rumble_roses_xx.exe` runs or `E:\ClaudeGame\RumbleRosesRecomp\queue-logs\selftest.lock` exists (the self-test tool waits by itself).
- Helpers: Gemini only via `E:\ClaudeGame\tools\gem.ps1`, Codex only via `gpt.ps1`. Read `E:\ClaudeGame\AI-HELPERS.md` first.
- Play tests are offered as a single-line powershell Run button, not as prose.
