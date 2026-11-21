# Second opinion wanted: torn fighter meshes in a native Xbox 360 renderer (read-only job)

You are reviewing, not editing. Do not change any file. Answer in plain text.

## Background
This repo is a static recompilation of an Xbox 360 game (EA Sports MMA). The "native renderer" intercepts the game's
Direct3D 9 (Xbox) calls on the CPU and records them into a D3D12 command list per frame, instead of emulating the
Xenos GPU's command ring. The list is closed and submitted at the game's Swap (our Present hook).

On the console, the GPU executes draws later than the CPU issues them. The game relies on that: its job threads keep
writing the fighters' CPU-skinned vertex buffers, and the per-object constant data (bone palettes, matrices loaded by
`GpuLoadShaderConstantF4Pointer`, which the GPU reads from memory when it runs the packet) after the draw call was
issued. Our renderer copies guest memory at the draw call, so copies can be torn or stale.

## Symptom (user play test, 1440p)
Black lines along mesh seams (neck, shoulders), body parts separating/twitching for a frame, black texture flashing on
legs, the referee's head detaching during the introductions. A screenshot also shows hard-edged dark blotches on a
fighter's back (skin lighting from a subsurface-scattering pass that draws the same mesh). After the fixes below the
user says "way less issues", but not gone.

## What exists now
1. `src/native/ufc_geometry.cpp`: `ResolveLocked` hashes each vertex/index range at every draw; content that changes
   goes into a per-frame upload ring copy, recorded in `g_late`. `SvrGeometryLateFixup` (called from Present in
   `src/reblue/gpu/present.cpp` before the command list is closed) re-hashes every recorded copy and refreshes changed
   ones in place, so every pass in the frame gets the buffer's final content. `late_tracked`: a buffer rewritten in
   the last 60 frames always takes a tracked ring copy.
2. `src/native/eamma_constant_loads.cpp` + `src/reblue/gpu/constant_buffers.cpp` (`NoteLateConstants`,
   `ConstantLateFixup`): same end-of-frame refresh for pointer-loaded constants.
3. Measurements (fight, 600-frame windows): ~6,000 of 26,000 vertex copies and ~20,000 of 200,000 constant uploads
   change between draw and Present; a probe that waits 2 ms after the refresh finds 0 buffers still changing; a counter
   of untracked copies of rewritten buffers reads 0. Menus: 0 late constant changes.

## Questions
1. Reading the code, which ways remain for a draw to use vertex or constant data different from what the console GPU
   would read? Be concrete (function, line, scenario). Consider: the first frame a resting mesh starts moving again
   (first use in the frame before the job writes it returns the settled host buffer); `BoundUploadStillGood` reuse;
   upload ring epochs; constants written via `D3DDevice_GpuBeginShaderConstantF4` (ring memcpy by the caller);
   index buffers; anything recorded on another thread; the threaded command-list recording.
2. Is "refresh at Present" the right model at all, or is there a better approach (e.g. finding the game's own
   job-completion sync and deferring the copy to it, or deferring all geometry/constant copies to Present)? Trade-offs?
3. Anything else that could produce black seam cracks / detached parts that is NOT data timing?

Keep the answer under ~800 words, most likely causes first.
