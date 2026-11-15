# EN-4: shader cache (2026-11-15)

## Build
```
python -I tools/nr_build_shader_cache.py
```
- `tools/eamma_shader_extract.py` unpacks `data/xenon-gen/bigs/shader_big.ast` (157 RNAFX effects) into 418 unique
  containers (240 PS 0x102A1100, 156 VS 0x102A1101, 22 two-shader VS 0x102A1121), set hash a5b10be8599f168d.
- XenosRecomp with UFC's patch stack + `patches/xenosrecomp-eamma.patch` (added to `tools/nr_fetch_third_party.py`):
  1. the scanner accepts flag 0x20 containers (a VS with a second, position-only shader after the main microcode;
     the main shader is translated, the key still hashes the whole container);
  2. TEXCOORD7 gets vertex input location 20 (shared with the host input-layout table).
- Result: `shader_cache.bin` **417 of 418** shaders (558 KB, xxh3 AF83CD9766C8BD14), `shader_cache_linked.bin`
  842 prelinked spec-constant variants. Build time ~4 s. Both land beside `eamma.exe`.

## Keys checked in a real run (emulated, census build)
`--eamma_d3d_census=true` now hashes every container at CreateVertexShader / CreatePixelShader exactly as the native
renderer keys it (XXH3 of virtualSize + physicalSize bytes, container = r3) and looks it up in shader_cache.bin.
Run en4a (boot -> menus -> fight, command buffers off): **288 unique shaders created (421 creates), 287 in the cache,
1 miss**: PS 1CCBA38AC6B29550 = container 71c9fb2befac8551 (POSTFX@3F04), the one translation failure.

## The one failure: light shafts
POSTFX's light-shaft pixel shader (constants lightShaftBrightness / Density / Step, `TNUMSAMPLES`) loops
`for (aL = 0; aL < i16.x; aL++)`: the loop count is integer constant i16, set by the game at run time.
XenosRecomp only declares integer constants the shader defines literally, and the renderer uploads no integer
(loop) constants at all, so dxc fails. Natively, light shafts are skipped until EN-5 gives integer constants a path
(XenosRecomp: declare undefined iN from a shared-constants slot; renderer: fill it from wherever the game writes
loop constants). This joins EN-5's constants question: the game never calls SetVertex/PixelShaderConstantF
(EN-3), so where its constants live must be found first anyway.

## Not done
- Player-PC cache build (UFC's `tools/native/shader_extract`, C++, no Python) is not ported: EA MMA's source is one
  BGFA big + zlib, simpler than UFC's xex decrypt. Needed only for a release.
- Rendering correctness of the 417: only a native run can tell (EN-5).
