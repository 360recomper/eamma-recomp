# WP4 static research (2026-11-15)

Static only, by a research agent (condensed by the main session): leads until a run checks them. Sources: asm
comments in `generated/mmadllzf_xex`, the unpacked DLL image, the 157 RNAFX effects of `shader_big.ast`, HLSL from
XenosRecomp, the render script `assets/data/xenon/scrape/fnbgame.scrape` (plain text: names every pass, target and
post effect; read it before further renderer work), draw log `shots/n76scale2log/run.log`.

## Correction: the alpha-to-mask draw
- It is **VS E74543F394642BD7 / PS FE27AA635E5ACE9B = HAIR_TINT_KK_CARDS_ALPHA** (Kajiya-Kay hair cards; in the test
  fight, the referee's hair), not VS C0591884 / PS B61F6480 (REFEREE_BODY/HEAD opaque skin, alpha = bloom mask). The
  draw log prints `colorctl` before its `#N` line, so 0x7E000016 was read against the previous draw. Confirmed at run
  time by the coverage ledger (run b1).
- Alpha = `specularShiftTexture.a`. Pass states: AlphaTestEnable 0, AlphaRef 0x80, AlphaFunc GEQUAL, ColorWrite 7,
  AlphaToMaskEnable 1, offsets 0x7E; a second pass with ColorWrite 0 is the hair's depth-only pass.
- All users of alpha-to-mask: HAIR_KK_CARDS_ALPHA, HAIR_RECOLOR_CARDS_ALPHA, HAIR_RECOLOR_KK_CARDS_ALPHA,
  HAIR_TINT_KK_CARDS_ALPHA (PS FE27AA63, depth pass 00A69C7D; others 85A4669C, C9AD633E, 6A9EF29D).
- 1-sample imitation: alpha test GEQUAL at the material's own ref (128/255). Softer option: 2x2 screen-door. With
  real MSAA: host alpha-to-coverage for these draws (`draw_bindings.cpp:186` forces it off).

## 1. Depth bias: nothing sets it (closed)
- Setters `sub_8830A498` (slope scale) / `sub_8830A560` (bias) are reached only through the render-state table at
  0x89067618 (12 bytes per entry: getter, setter, default; copied to device +0x40 + state by `sub_88325688`).
- The three variable-index paths (RNA state flush `sub_88A69D70`, RNA SetRenderState `sub_88A66028` with constant
  indices only, effect pass states `sub_88A7CB10`) never carry states 204/208: no pass state block of the 157 effects
  holds them, and the render script has no render-state command. Draw logs agree (mode only 0x18000 / 0x18002).
- Shadows are biased in the pixel shaders. `pipeline_cache.cpp:105` can stay 0.

## 2. Light shafts (PS 1CCBA38AC6B29550)
- Script flag `postfx LIGHTSHAFTS`, set every frame in `SetPostfxParams` (`fnbgame.scrape:176-216`): on only when not
  in gameplay and either (a) `inGym` and `inFrontEnd` are both set, or (b) the venue lighting has
  `enableIntroLighting` and `enableLightShafts`. Drawn in `BloomPreRender` (`:1053-1087`).
- Scene to test (lead): the career gym (VENUE_GYMA) outside a drill; then intros in other venues (the boot_taps venue
  has it off). Per-venue values are in `render_big.ast` (not unpacked). Run with `SVR_LOG_PS=1CCBA38AC6B29550`.

## 3. PS 60D26E9886365F89 (480x270 16F)
- SSAO geometry pass (linZ.fx `tec0_PosAndNorms`, `fnbgame.scrape:1299-1345`): view-space position in RGB, packed
  view normal in A (`floor(32 nx + 32) - 31.5 + 0.5 ny`). Read by aoShaders `tec3_SSAO` and camera flashes.
- For B11: must not be filtered or averaged; keep it 1x. Its depth should then be 1x too (at 2x the log showed rt
  480x270 with ds 960x540).

## 4. MSAA audit (for WP9)
- Guest 4x surfaces: the 1280x720 scene pair (0x14020500) and a 640x360 pair (0x0A020280; lead: the fake-DOF
  background buffer, where the crowd is drawn). Everything else is 1x. The script has `rendering force_no_msaa`.
- Native never reads the sample bits (`SvrResolveSurface` reads size and format only): every host surface is 1 sample.
- With samples > 1: `resolve.cpp:250-276` MSAA resolve has no rect mode (each of the 3 tile resolves would copy the
  whole surface); `hooks/state.cpp:123` takes the count from colour only (depth-only draws, mixed pairs);
  `resolve.cpp:584` half-pixel fill returns early; `present.cpp:457` abandons the frame when the pick is MSAA;
  `present.cpp:124` safe; `surface_registry.cpp:81` cache key lacks the count; `ufc_resolve_writeback.cpp:1264` fine
  while resolve targets stay 1x.

## Not done
- The code that sets `inGym` / `inFrontEnd`; per-venue `enableLightShafts` values.
