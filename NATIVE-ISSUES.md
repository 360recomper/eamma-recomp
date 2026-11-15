# EA MMA native renderer: issues by category, and what went wrong (2026-11-15)

Status: branch `native`, EN-0..EN-5 done. Movies, menus, loading, fighter select and fights render natively; shading
still differs from emulated. Details per step: `FINDINGS-en1..en5-*.md`, plan: `NATIVE-PLAN.md`, handoff: `NATIVE-HANDOFF.md`.

## A. Open visual issues (native vs emulated)
| # | Issue | Seen in | Lead |
|---|---|---|---|
| A1 | ~~Fighter skin pale/ashen with orange or white blotches~~ **Fixed**: XenosRecomp ignored loop start/step; the SSS blur doubled its centre tap and over-brightened the SSS map (FINDINGS-a1-skin.md, n39loop/060) | shots/n33/120.png -> n39loop/060.png | Check more scenes/fighters shot for shot vs emulated (A4) |
| A2 | Light halo around the menu fighter's silhouette: **probably gone** with A1/A5 (no halo in n73default/015 vs en3d/015) | n33/015.png | Confirm with one content-matched menu pair (WP5), then strike |
| A3 | Light shafts: shader now translated (run-time loop constants, 418/418); not yet seen on screen | - | Find a scene that draws PS 1CCBA38A and compare with emulated |
| A5 | ~~Menu/fight fighter lighting patches, black cracks~~ **Fixed**: the game rewrites skinned vertex buffers mid-frame without an Unlock; geometry is now compared at every draw (FINDINGS-a1-skin.md, n73default) | n46lod/010 -> n69geomfix/010 | - |
| A6 | Menu crowd behind the fence missing natively (may be fade timing) | n73default/015 vs en3d/015 | 1 s shots 8-20 s on both renderers; if still missing, draw log at that second (WP5) |
| A7 | Alpha-to-mask dropped: one fight draw a frame sets RB_COLORCONTROL alpha-to-mask (0x7E000016, VS C05918844A3CF2C8 / PS B61F648032C00706); alpha-to-coverage is forced off and surfaces are 1 sample, so it draws opaque. Lead: hair/beard cards (test fighters are bald) | draw_bindings.cpp:186 | WP4 names the mesh, WP6 adds a 1-sample fallback |
| A4 | Unknown: everything not yet A/B'd shot for shot (career, create-a-fighter, other venues, replays) | - | Only boot -> menus -> one fight -> highlights tested |

## B. Correctness risks in what was built
| # | Risk | Where |
|---|---|---|
| B1 | Constant capture applies "at the next draw" only for the same device and keeps at most 4096 records (now drops the oldest quarter when full); GPU-owned constants written for a draw recorded into a command buffer would be applied to a direct draw instead. Command buffers are off natively, so not hit today | src/native/eamma_constant_loads.cpp |
| B2 | Writing captured constants into the device's constant block assumes the game's D3D never reads/flushes those registers (GPU-owned). Holds in tests, not proven from the D3D code | same |
| B3 | Tiled-surface extent is remembered per surface VA forever; a surface reused for something non-tiled keeps the bigger host copy (harmless so far) | ufc_resources.cpp SvrNoteTiledSurface |
| B4 | BeginTiling clear always clears depth+stencil and clears colour when a colour pointer is given; the real Flags semantics were not decoded | hooks/draw.cpp EammaBeginTilingObserved |
| B5 | DEC3N decode returns w = 0 like the other packed-normal paths; a shader using .w of a normal would differ. Checked: none of the 418 shaders reads it | shader_common.h |
| B6 | DXN/DXT5A channel fill copied from Xenia's table, not checked against the game | native_texture_mirror.cpp |
| B7 | ~~Eager resolve write-back on by default~~ **Off by default now** (`eamma_native_wb_eager_frames=-1`): run n43nowb shows boot, menus, loading, fight and highlights the same without it; the switch stays for A/B | ufc_resolve_writeback.cpp |
| B8 | The guest-memory reads in the draw log use TranslatePhysical; earlier `bd::mem::try_load` on the 0xA... view returned 0 silently and misled a diagnosis (C3) | ufc_draw_log.cpp |
| B9 | `src/native/ufc_d3d.h` / `gpu/d3d.h` offsets: only VS/PS/decl and the recording CB were proven at run time; front/back buffer, ring guarantee, register-shadow blocks (+0x2880..) are assumed from UFC/SvR. +0x294C (VTE) and +0x29C0 (pixel centre) now checked against EA's setters | FINDINGS-en1-d3d-map.md section 7 |
| B10 | **Real bug at any scale other than 1x** (wrong mips reach guest memory: the 1024x512 fighter composite's mips 1-3 are resolved from a scaled 1024x1024 scratch surface and the CPU reads them back). Mixed-scale resolve (scaled source -> 1x destination) stretches the whole source surface; the console copies the source rect 1:1, clipped to the destination (here a 2x 1024x1024 surface into a 1024x512 texture should take its top half). Seen 2-3 times per address at 2x (n75/n76), no visible effect in boot/menus/fight | hooks/draw.cpp mixed_scale (needs a scaled region blit) |
| B11 | Keep-1x list `1024x512,384x64,64x64` and the "16F always 1x" rule are UFC's (face liquid map, ambient cube), not EA's: 1024x512 is EA's fighter composite, and EA's 480x270 16F scene buffer stays 1x while its depth scales (lead: depth/DOF/SSAO input) | ufc_resources.cpp:58 (WP2 sets EA's own list) |

## C. What went wrong so far (mistakes and dead ends)
| # | What happened | Cost | Lesson |
|---|---|---|---|
| C1 | Took the white boot screen for a stall on GPU resolve readback. "Proved" it with emulated `readback_resolve=none` showing the same 1 draw/frame, then built eager write-back. The game was not stalled: the attract movie is one DrawVerticesUP quad a frame, and natively that quad was never drawn | ~8 runs, one unneeded feature (B7) | Look at a screenshot of the comparison run before concluding; same draw counts can mean "working" |
| C2 | First SVR_PRESENT_TARGET by address: addresses change between runs, so presenting a fixed VA showed nothing useful | 2 runs | Make dev aids address-independent (did: `=tiled`) |
| C3 | Draw-log vertex dump read zeros through `try_load` on the 0xA0000000 view (validation fallback), which looked like "vertex buffers are empty"; the first version also faulted the game reading a physical address without the view base | 2 runs + a crash | Read guest memory the way the consumer does (TranslatePhysical); never trust a silent fallback value |
| C4 | Census PM4 walker followed a garbage INDIRECT_BUFFER address and crashed the game twice; first blamed the command-buffer switch | 2 runs | Bound every guest read in diagnostics (Readable()) |
| C5 | Assumed the game's constants live in the device block because the census saw the block change at 12-22% of draws; they mostly don't (C156+ via LOAD_ALU_CONSTANT, c0..c155 via ring memcpy) | several runs on depth/vertex theories | Check the exact registers a failing shader reads (its HLSL), not block-level statistics |
| C6 | Chased depth (EAMMA_NO_DEPTH_TEST) and vertex data before the constants; the order was reasonable but each step was a full 60-150 s run | time | Draw log with per-draw constants first |
| C7 | Copied UFC's `main` first, found photo/eyelid/blood code woven into src/reblue, recopied from `claude/native-renderer` | small | Check branch hygiene before copying |
| C8 | Python here-doc edits broke on backslashes and CRLF several times (build.bat, shader_common.h joined line, `\/` path) | small, but one silent bug (path separator) | Use the Edit tool for lines with backslashes; check `file` for CRLF after edits |
| C9 | Ring histogram in the census loses PM4 sync (garbage opcodes); kept as a lead only | - | Don't read counts from it |
| C10 | Ran tests at 1280x720 window only; resolution scale, 4K, FSR, speed never exercised | - | Week 2 work |
| C11 | A5: three advisor rounds and ~20 runs on shader math (VSM, LOD, handedness, half-pixel) before testing the geometry mirror; the shader symptoms were all downstream of stale vertex data | a long afternoon | When passes of the same mesh disagree, run with `eamma_native_geometry_every_draw` / `check_geometry` first |

## D. Process/environment notes
- Every native A/B run is ~2-4 min (build ~1-10 min when the DLL target's defines change: switching native <-> census
  rebuilds the whole 17 MB DLL). Keep one configuration per session where possible.
- Intermittent crash at the profile save (~15 s): null read +0x134/+0x148 in rexruntime, seen once natively (n26) and
  once emulated earlier (NOTES.md fnc2). Not caused by the renderer as far as known. **Release risk** (no package owns it).
- Rumble Roses GPU priority: `tools/run_shots.ps1` refuses to start while it holds the GPU.
- Agent research (EN-1 D3D map, shader inventory) was accurate where spot-checked; both agents could not write files
  (harness rule), so their reports were saved by hand.

## E. Advisor review (Fable, 2026-11-15, read-only) — leads, not verified
- **New top A1 suspect: DEC3N w.** 0x2A2187's "2A" swizzle is the one FLOAT3 (0x2A23B9) uses, where w reads as 1; our
  decode returns w = 0. If the fighter VS uses tangent.w for handedness, the binormal collapses. Test: grep the PLAYER VS
  HLSL for the tangent's `.w`, return 1.0 in the DEC3N branch, rebuild the cache (4 s), rerun. Also: the DEC3N decode is
  only wired for Normal/Tangent/Binormal usages.
- **B9 ranks higher:** `constant_buffers.cpp` ~831/834 reads raw device +0x294C (VTE) and +0x29C0 (pixel centre) every
  draw, comments still cite SvR functions. Verify statically from EA's setters (no runs needed).
- **Missing: guest depth bias is never fed** (`pipeline_cache.cpp` zeroes depthBias). Missing (speculative): MSAA on
  the tiled surfaces (1280x256 tiles, depth resolves with flags 0x14).
- **B1/B2:** design confirmed against sub_883198E0 / sub_88319858. Real defect: at 4096 records the newest are dropped;
  drop the oldest or clear at Swap. B2 speculative: inlined XDK SetConstantF writes (block + dirty bits, invisible to
  the call census) could collide with a capture on the same register before one draw.
- **Reversed-Z flip judged exact (unverified for draws with the viewport transform off)** (stored depth = console's; resolves, shadow compares, depth reads unaffected). Holes:
  gate `depthFlip` on VTE (a viewport-transform-off draw with an inverted device viewport gets flipped wrongly); depth
  bias above.
- B3 leave; B4 nearly right (only a "skip first tile clear" flag could differ; colour clear packed to 8-bit); B6 fine;
  B7 cheap after 8 frames, A/B once; `s_steady` map never pruned. B8 is a fixed mistake (belongs in C).
- **A1 other hypotheses:** PS inputs (log PS c0..c7, c156..c187, bools, per-slot texture format/resolve flag, diff with
  `--trace_draw_ps`); a per-fighter resolved texture (ambient-cube class) or co-issue. Separator: one draw log with both
  fighters — same PS hash for the good and bad fighter means inputs, not the translator.
- **A2:** (1) same cause as A1 feeding bloom — skip the skin PS; (2) DOF / depth resolve — skip DOF PS, present depth;
  (3) half-pixel offset from the raw +0x29C0 read. A 1-pixel halo = offset, wide = blur mask.
- **Process:** make the census a run-time switch in the native build (kills the 10-minute rebuilds); read the suspect
  shader's HLSL before any run; compare the same scene and second on both renderers.
