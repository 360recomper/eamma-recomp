// NR-10 probe stubs (EA MMA). UFC's probe (ufc_nr10_probe.cpp) hooked UFC's own DXT compressor and
// export functions by address; those addresses mean nothing in EA MMA, so it is not copied (trap 13:
// a hook on an unmapped address replaces an unrelated game function). The generic renderer still
// calls these; they do nothing. An EA MMA probe, if one is needed, gets EA MMA's own addresses.

#pragma once

#include <cstdint>
#include <string>

namespace bd::gpu {
struct NativeMirrorSource;
}

inline bool UfcNr10On() { return false; }
inline std::string UfcNr10DumpDir() { return "."; }
inline void UfcNr10NoteResolve(uint32_t, uint32_t, uint32_t, uint32_t, bool, int32_t, int32_t, int32_t, int32_t,
                               int32_t, int32_t, uint32_t, uint32_t) {}
inline void UfcNr10NoteBind(uint32_t, const bd::gpu::NativeMirrorSource&) {}
inline void UfcNr10NoteShortcut(uint32_t) {}
inline void UfcNr10NoteDraw(uint32_t, uint32_t, uint32_t, bool) {}
