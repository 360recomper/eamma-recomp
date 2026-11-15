# EA MMA native renderer: issues by category, and what went wrong (2026-11-15)

Status: branch `native`, EN-0..EN-5 done. Movies, menus, loading, fighter select and fights render natively; shading
still differs from emulated. Details per step: `FINDINGS-en1..en5-*.md`, plan: `NATIVE-PLAN.md`, handoff: `NATIVE-HANDOFF.md`.

## A. Open visual issues (native vs emulated)
| # | Issue | Seen in | Lead |
|---|---|---|---|
| A1 | Fighter skin pale/ashen with orange or white blotches at some angles (one fighter fine, the other washed out) | shots/n33/120.png, n33/015.png | Skin PS reads something wrong: a lookup/resolve texture, bool or PS constants not covered by the two constant hooks, or a co-issue/translator case. Draw log at the bad second, skip the skin PS by hash (`eamma_native_skip_vs`-style), read its HLSL |
| A2 | Light halo around the menu fighter's silhouette | n33/015.png | Depth of field / depth-aware blur reading the resolved depth: depth resolve format (k_24_8 vs host D24/D32) or the z flip at resolve time |
| A3 | Light shafts missing | not checked visually | The one untranslated shader (POSTFX 71c9fb2b, loop count = integer constant i16). Needs integer constants in XenosRecomp + an upload source (find how the game sets them; SetVS/PSConstI were never called) |
| A4 | Unknown: everything not yet A/B'd shot for shot (career, create-a-fighter, other venues, replays) | - | Only boot -> menus -> one fight -> highlights tested |

## B. Correctness risks in what was built
| # | Risk | Where |
|---|---|---|
| B1 | Constant capture applies "at the next draw" only for the same device and keeps at most 4096 records; GPU-owned constants written for a draw recorded into a command buffer would be applied to a direct draw instead. Command buffers are off natively, so not hit today | src/native/eamma_constant_loads.cpp |
| B2 | Writing captured constants into the device's constant block assumes the game's D3D never reads/flushes those registers (GPU-owned). Holds in tests, not proven from the D3D code | same |
| B3 | Tiled-surface extent is remembered per surface VA forever; a surface reused for something non-tiled keeps the bigger host copy (harmless so far) | ufc_resources.cpp SvrNoteTiledSurface |
| B4 | BeginTiling clear always clears depth+stencil and clears colour when a colour pointer is given; the real Flags semantics were not decoded | hooks/draw.cpp EammaBeginTilingObserved |
| B5 | DEC3N decode returns w = 0 like the other packed-normal paths; a shader using .w of a normal would differ | shader_common.h |
| B6 | DXN/DXT5A channel fill copied from Xenia's table, not checked against the game | native_texture_mirror.cpp |
| B7 | Eager resolve write-back (`eamma_native_wb_eager_frames`, default 8) is on but was added for a wrong diagnosis (C1); costs a GPU sync per new resolve; may hide or cause timing differences | ufc_resolve_writeback.cpp |
| B8 | The guest-memory reads in the draw log use TranslatePhysical; earlier `bd::mem::try_load` on the 0xA... view returned 0 silently and misled a diagnosis (C3) | ufc_draw_log.cpp |
| B9 | `src/native/ufc_d3d.h` / `gpu/d3d.h` offsets: only VS/PS/decl and the recording CB were proven at run time; front/back buffer, ring guarantee, register-shadow blocks (+0x2880..) are assumed from UFC/SvR | FINDINGS-en1-d3d-map.md section 7 |

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

## D. Process/environment notes
- Every native A/B run is ~2-4 min (build ~1-10 min when the DLL target's defines change: switching native <-> census
  rebuilds the whole 17 MB DLL). Keep one configuration per session where possible.
- Intermittent crash at the profile save (~15 s): null read +0x134/+0x148 in rexruntime, seen once natively (n26) and
  once emulated earlier (NOTES.md fnc2). Not caused by the renderer as far as known.
- Rumble Roses GPU priority: `tools/run_shots.ps1` refuses to start while it holds the GPU.
- Agent research (EN-1 D3D map, shader inventory) was accurate where spot-checked; both agents could not write files
  (harness rule), so their reports were saved by hand.
