// shader microcode patch generation (from UFC 3; EA MMA patches no microcode yet, so it stays 0).
//
// Bumped after every write that changes patched microcode in guest memory. The native renderer
// keys host shaders by a hash of the microcode taken when the game creates the shader, so it
// re-hashes a shader whose generation is stale (src/native/ufc_resources.cpp). Kept apart from
// shader_patch.h so renderer code doesn't pull in <windows.h>.

#pragma once

#include <atomic>
#include <cstdint>

namespace eamma_shader_patch {

inline std::atomic<uint32_t> g_generation{0};

inline uint32_t Generation() { return g_generation.load(std::memory_order_acquire); }
inline void BumpGeneration() { g_generation.fetch_add(1, std::memory_order_acq_rel); }

}  // namespace eamma_shader_patch
