// EN-3 (NATIVE-PLAN.md), from UFC's NR-6 census: observe-only D3D call census for EA MMA.
// EA MMA addresses: src/native/ufc_d3d_names.h and FINDINGS-en1-d3d-map.md. EA links no Present,
// SetViewportF or BeginIndexedVertices, and none of UFC's VIZ_QUERY survey functions; it adds
// DrawVerticesUP (which calls BeginVertices, so its draws are counted there).
//
// --eamma_d3d_census=true counts calls to the game's XDK D3D functions (ufc_d3d_names.h) per Swap
// and logs a report every --eamma_d3d_census_every frames. The emulated GPU still draws; every hook
// runs the game's own function unchanged, so with the cvar off (default) the game behaves exactly
// like main (one flag test per call).
//
// What it answers for the native renderer (Stop 1):
//   - draws per frame: direct calls (Begin(Indexed)Vertices, Draw(Indexed)Vertices) outside a
//     command-buffer recording, draws recorded into a command buffer (BeginCommandBuffer stores the
//     buffer at device+0x35A0), and draws a RunCommandBuffer replays. Replays are counted by
//     walking the buffer's PM4 (segment list at cb+116, INDIRECT_BUFFER entries {size, address})
//     for DRAW_INDX / DRAW_INDX_2 packets, so prerecorded buffers the game never records at run
//     time are counted too. Clears are draws on the GPU, so they're listed beside the draws.
//   - draws issued inside BeginZPass..EndZPass and BeginTiling..EndTiling (tile count), which the
//     GPU runs more than once;
//   - whether the device's VS / PS / decl fields (EA +0x3248 / +0x3244 / +0x2ED8) hold the objects the last SetVertexShader /
//     SetPixelShader / SetVertexDeclaration got (NR-1: med confidence), counted at every draw;
//   - D3DQuery Issue/GetData and VIZ_QUERY use; the render-state call table (dumped once).
// Compare "gpu-like" (direct + replayed + clears) with the emulated renderer's per-frame draw
// count: --trace_draw_ps=<prefix> logs "Draw trace c.f (frame F): end, X of Y draws logged".
//
// SvR made its census and the native hooks exclusive (both override the same functions); here the
// census is built only while EAMMA_NATIVE_HOOKS is off (cmake/native_renderer.cmake). NR-8 moves
// these counters into the native hooks.

#include <atomic>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <mutex>
#include <string>
#include <unordered_map>

#include <algorithm>
#include <fstream>
#include <set>
#include <vector>

#include <windows.h>

#include <fmt/format.h>
#include <xxhash.h>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>

#include "../native/ufc_d3d.h"
#include "../native/ufc_d3d_names.h"

REXCVAR_DEFINE_BOOL(eamma_d3d_census, false, "eamma",
                    "NR-6: count the game's D3D calls per frame (draws direct / in command buffers / "
                    "in Z pass and tiling, clears, resolves, queries) and log a [census] report every "
                    "eamma_d3d_census_every frames. Observe only; the emulated GPU still draws");
REXCVAR_DEFINE_STRING(eamma_census_shader_cache, "", "eamma",
                      "EN-4: shader_cache.bin to check created shaders against (empty = beside the exe)");
REXCVAR_DEFINE_INT32(eamma_d3d_census_every, 300, "eamma",
                     "eamma_d3d_census: frames (Swaps) per report")
    .range(1, 100000);

namespace eamma_census {

// One counter per hooked function (calls), reset each report.
enum Fn : int {
  kSwap, kPresent, kResolve, kClear, kClearF, kSetRenderTarget, kSetDepthStencil, kSetViewport,
  kSetScissor, kBeginVertices, kBeginIndexedVertices, kDrawVertices, kDrawIndexedVertices,
  kSetVertexShader, kSetPixelShader, kSetVertexDeclaration, kSetTexture, kSetStreamSource,
  kSetIndices, kSetVsConstF, kSetPsConstF, kVbUnlock, kIbUnlock, kCreateVs, kCreatePs, kCreateDecl,
  kBeginCb, kEndCb, kRunCb, kCbPaired, kBeginZPass, kEndZPass, kBeginTiling, kEndTiling,
  kQueryIssue, kQueryGetData, kDrawUP, kInt3, kInt4, kInt0, kInt1, kInt2, kClearDraw, kSwapDraw, kGprAlloc, kFnCount
};
constexpr const char* kFnName[kFnCount] = {
    "Swap", "Present", "Resolve", "Clear", "ClearF", "SetRenderTarget", "SetDepthStencil",
    "SetViewport", "SetScissor", "BeginVertices", "BeginIndexedVertices", "DrawVertices",
    "DrawIndexedVertices", "SetVS", "SetPS", "SetDecl", "SetTexture", "SetStreamSource",
    "SetIndices", "SetVSConstF", "SetPSConstF", "VBUnlock", "IBUnlock", "CreateVS", "CreatePS",
    "CreateDecl", "BeginCB", "EndCB", "RunCB", "CB_8831D880", "BeginZPass", "EndZPass",
    "BeginTiling", "EndTiling", "QueryIssue", "QueryGetData", "DrawVerticesUP", "Call_88320C20",
    "unused", "Draw_88320378", "Draw_8830D5C0", "Draw_8830D718", "ClearDraw_8830EDE0", "SwapDraw_8832A148",
    "SetShaderGPRAllocation"};

// Per-report draw tallies (reset each report).
enum Tally : int {
  kDirect,         // draw calls outside a command-buffer recording
  kInternal,       // D3D-internal draws that skip the public draw calls (88320378, 8830D5C0, 8830D718, 8832A148)
  kRecorded,       // draw calls while a command buffer records (run later by RunCommandBuffer)
  kReplayed,       // draw packets in the ring range RunCommandBuffer wrote (incl. its INDIRECT_BUFFERs)
  kBufferDraws,    // draw packets in the buffers themselves (segment list), once per run
  kReplayedPred,   // ... of those, predicated (bit 0 of the header)
  kRunsNoDraws,    // runs whose PM4 held no draw (or no readable segment list)
  kRunsInRecord,   // runs issued while recording (nested: counted when the outer buffer runs)
  kZPassDraws,     // direct draws between BeginZPass and EndZPass
  kTilingDraws,    // direct draws between BeginTiling and EndTiling
  kTiles,          // sum of BeginTiling's tile counts
  kResolveDraws,   // draw packets in the ring range each Resolve writes (incl. its copy-mode draw)
  kClearRects,     // clear draws: calls of sub_8830EDE0, the one DRAW_INDX_2 writer under ClearF
  kGprDraws,       // draw packets in SetShaderGPRAllocation's three INDIRECT_BUFFERs (24-point flush)
  kVsMatch, kVsMismatch, kPsMatch, kPsMismatch, kDeclMatch, kDeclMismatch,
  kQueryOcclusion, kQueryOther,
  kTallyCount
};

inline std::atomic<uint64_t> g_calls[kFnCount];
inline std::atomic<uint64_t> g_tally[kTallyCount];
inline std::atomic<uint32_t> g_frame_draws{0};  // gpu-like draws since the last Swap
inline std::atomic<uint32_t> g_last_vs{0}, g_last_ps{0}, g_last_decl{0};
inline std::atomic<int32_t> g_in_zpass{0}, g_in_tiling{0};
inline std::atomic<bool> g_dumped_device{false};

struct Window {
  std::mutex lock;
  uint64_t frames = 0, total = 0;
  uint32_t min = UINT32_MAX, max = 0, last = 0;
  uint64_t frame_index = 0;
};
inline Window g_window;

inline uint32_t Load32(uint8_t* base, uint32_t addr) {
  uint32_t v;
  std::memcpy(&v, base + addr, 4);
  return _byteswap_ulong(v);
}

inline bool Plausible(uint32_t addr) { return addr >= 0x10000; }

inline bool On() { return REXCVAR_GET(eamma_d3d_census); }

inline void Count(Fn f) { g_calls[f].fetch_add(1, std::memory_order_relaxed); }
inline void Add(Tally t, uint64_t n = 1) { g_tally[t].fetch_add(n, std::memory_order_relaxed); }

// INDIRECT_BUFFER addresses are physical; the 0xA0000000 view maps physical memory 1:1.
inline uint32_t PhysToVirtual(uint32_t addr) {
  return addr >= 0x80000000u ? addr : 0xA0000000u + (addr & 0x1FFFFFFFu);
}

struct Pm4Count {
  uint32_t draws = 0, predicated = 0;
  uint32_t* ops = nullptr;  // optional: type-3 opcode histogram (128 entries), top level and nested
};

// Walks one PM4 range for DRAW_INDX (0x22) / DRAW_INDX_2 (0x36), following INDIRECT_BUFFER (0x3F)
// and INDIRECT_BUFFER_PFD (0x37).
inline void ScanPm4Va(uint8_t* base, uint32_t va, uint32_t dwords, int depth, Pm4Count& out);
inline void ScanPm4(uint8_t* base, uint32_t addr, uint32_t dwords, int depth, Pm4Count& out) {
  if (!addr || dwords > 0x100000 || depth > 4) return;
  ScanPm4Va(base, PhysToVirtual(addr), dwords, depth, out);
}

// Guest ranges a PM4 walk may read: virtual heaps, the two images, the physical views. A packet
// walk that has lost sync can pick up any value as an INDIRECT_BUFFER address (EN-3 run en3c read
// 0x8100000A and faulted).
inline bool Readable(uint32_t va, uint32_t dwords) {
  const uint64_t end = uint64_t(va) + 4ull * dwords;
  auto in = [&](uint64_t lo, uint64_t hi) { return va >= lo && end <= hi; };
  return in(0x40000000u, 0x80000000u) || in(0x82000000u, 0x82200000u) || in(0x88000000u, 0x89200000u) ||
         in(0xA0000000u, 0x100000000ull);
}

inline void ScanPm4Va(uint8_t* base, uint32_t va, uint32_t dwords, int depth, Pm4Count& out) {
  if (!va || dwords > 0x100000 || depth > 4 || !Readable(va, dwords)) return;
  for (uint32_t i = 0; i < dwords;) {
    const uint32_t h = Load32(base, va + 4 * i);
    const uint32_t type = h >> 30;
    if (type == 0) {
      i += 2 + ((h >> 16) & 0x3FFF);
    } else if (type == 1) {
      i += 3;
    } else if (type == 2) {
      i += 1;
    } else {
      const uint32_t op = (h >> 8) & 0x7F, count = ((h >> 16) & 0x3FFF) + 1;
      if (out.ops) ++out.ops[op];
      if (op == 0x22 || op == 0x36) {
        ++out.draws;
        if (h & 1) ++out.predicated;
      } else if ((op == 0x3F || op == 0x37) && i + 2 < dwords) {  // INDIRECT_BUFFER(_PFD)
        ScanPm4(base, Load32(base, va + 4 * (i + 1)), Load32(base, va + 4 * (i + 2)) & 0xFFFFF,
                depth + 1, out);
      }
      i += 1 + count;
    }
  }
}

// RunCommandBuffer's segment list (EA sub_8831D9D8, same layout as UFC): cb+116 -> node {next, count,
// entries[count] {size dwords (low 24 bits), address}}.
inline Pm4Count ScanCommandBuffer(uint8_t* base, uint32_t cb, uint32_t* ops = nullptr) {
  Pm4Count out;
  out.ops = ops;
  uint32_t node = Load32(base, cb + ufc3::d3d::kCbSegments);
  for (int nodes = 0; Plausible(node) && nodes < 256; ++nodes) {
    const uint32_t n = Load32(base, node + 4);
    for (uint32_t e = 0; e < n && e < 4096; ++e) {
      const uint32_t entry = node + 8 + 8 * e;
      ScanPm4(base, Load32(base, entry + 4), Load32(base, entry) & 0xFFFFFF, 0, out);
    }
    node = Load32(base, node);
  }
  return out;
}

// NR-6: the emulated GPU runs 72 one-vertex point-list draws a frame (3 x 24, VS B6C9863F) that no
// hooked function writes. Canned PM4 in the image would explain it (Resolve memcpys one from
// 0x82017420), so look once for DRAW_INDX_2 point-list packets (header 0xC0003600 / 0xC0003601,
// then 1 index, auto: 0x00010081) and plain DRAW_INDX point lists in the loaded image.
inline void ScanImageOnce(uint8_t* base) {
  static std::atomic<bool> done{false};
  if (done.exchange(true)) return;
  uint32_t hits = 0;
  for (uint32_t a = 0x88000000u; a < 0x89100000u && hits < 24; a += 4) {  // MMADLLzf.xex.dll
    const uint32_t h = Load32(base, a);
    if ((h & 0xFFFFFFFEu) == 0xC0003600u && Load32(base, a + 4) == 0x00010081u) {
      REXLOG_INFO("[census] image: DRAW_INDX_2 point packet at {:08X}: {:08X} {:08X} {:08X} {:08X}", a,
                  Load32(base, a - 8), Load32(base, a - 4), h, Load32(base, a + 4));
      ++hits;
    }
  }
  REXLOG_INFO("[census] image: {} point-list DRAW_INDX_2 packets in 0x88000000..0x89100000", hits);
}

inline void DumpDeviceOnce(uint8_t* base, uint32_t dev) {
  bool expected = false;
  if (!g_dumped_device.compare_exchange_strong(expected, true)) return;
  // NR-1 open point: the render-state call table (Blue Dragon m_SetRenderStateCall +0x38; UFC's
  // ring is at +0x30, so probably +0x40). Count code pointers in +0x38..+0x400.
  uint32_t code = 0, first = 0, last = 0;
  for (uint32_t off = 0x38; off < 0x400; off += 4) {
    const uint32_t v = Load32(base, dev + off);
    if (v >= 0x88300000u && v < 0x88FCAF6Cu) {  // DLL .text
      if (!code) first = off;
      last = off;
      ++code;
    }
  }
  std::string head;
  for (uint32_t off = 0x28; off < 0x60; off += 4) head += fmt::format(" {:08X}", Load32(base, dev + off));
  REXLOG_INFO("[census] device {:08X}: +0x28..+0x5C{}", dev, head);
  REXLOG_INFO("[census] device {:08X}: {} code pointers in +0x38..+0x3FC, first +0x{:X}, last +0x{:X}",
              dev, code, first, last);
  REXLOG_INFO("[census] device {:08X}: VS +0x3248 {:08X}, PS +0x3244 {:08X}, decl +0x2ED8 {:08X}, "
              "IB +0x3144 {:08X}, RT0 +0x3148 {:08X}, DS +0x3158 {:08X}, recording cb +0x35A0 {:08X}, "
              "front +0x3AC0 {:08X}, back +0x3AC4 {:08X}",
              dev, Load32(base, dev + ufc3::d3d::kVertexShader), Load32(base, dev + ufc3::d3d::kPixelShader),
              Load32(base, dev + ufc3::d3d::kVertexDeclaration), Load32(base, dev + ufc3::d3d::kIndexBuffer),
              Load32(base, dev + ufc3::d3d::kRenderTarget0), Load32(base, dev + ufc3::d3d::kDepthStencil),
              Load32(base, dev + ufc3::d3d::kRecordingCommandBuffer), Load32(base, dev + ufc3::d3d::kFrontBuffer),
              Load32(base, dev + ufc3::d3d::kBackBuffer));
}

// Every draw call: where it goes, and the device-state cross-check.
inline void OnDraw(uint8_t* base, uint32_t dev) {
  if (Load32(base, dev + ufc3::d3d::kRecordingCommandBuffer)) {
    Add(kRecorded);
    return;
  }
  DumpDeviceOnce(base, dev);
  Add(kDirect);
  g_frame_draws.fetch_add(1, std::memory_order_relaxed);
  if (g_in_zpass.load(std::memory_order_relaxed) > 0) Add(kZPassDraws);
  if (g_in_tiling.load(std::memory_order_relaxed) > 0) Add(kTilingDraws);
  Add(Load32(base, dev + ufc3::d3d::kVertexShader) == g_last_vs.load(std::memory_order_relaxed) ? kVsMatch
                                                                                                 : kVsMismatch);
  Add(Load32(base, dev + ufc3::d3d::kPixelShader) == g_last_ps.load(std::memory_order_relaxed) ? kPsMatch
                                                                                                : kPsMismatch);
  Add(Load32(base, dev + ufc3::d3d::kVertexDeclaration) == g_last_decl.load(std::memory_order_relaxed)
          ? kDeclMatch
          : kDeclMismatch);
}

// EN-4: every shader the game creates, hashed as the native renderer keys it (XXH3 of the whole
// container, virtualSize + physicalSize: src/reblue/gpu/shaders/guest_shaders.cpp CreateShader) and
// looked up in shader_cache.bin's index (header 32 bytes, entries {u64 hash, u32 x4} sorted by hash).
struct ShaderCheck {
  std::mutex lock;
  bool loaded = false;
  std::vector<uint64_t> index;
  std::set<uint64_t> seen;
  uint64_t hits = 0, misses = 0, creates = 0;
};
inline ShaderCheck g_shaders;

inline void LoadShaderIndex(ShaderCheck& sc) {
  sc.loaded = true;
  std::string path = REXCVAR_GET(eamma_census_shader_cache);
  if (path.empty()) {
    char exe[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    path = exe;
    path = path.substr(0, path.find_last_of("\\/") + 1) + "shader_cache.bin";
  }
  std::ifstream f(path, std::ios::binary);
  char head[32] = {};
  if (!f.read(head, 32) || std::memcmp(head, "UFCSHC01", 8) != 0) {
    REXLOG_WARN("[census] shader check: no shader cache at {}", path);
    return;
  }
  uint32_t count;
  std::memcpy(&count, head + 12, 4);
  sc.index.resize(count);
  for (uint32_t i = 0; i < count; ++i) {
    char e[24];
    if (!f.read(e, 24)) break;
    std::memcpy(&sc.index[i], e, 8);
  }
  std::sort(sc.index.begin(), sc.index.end());
  REXLOG_INFO("[census] shader check: {} shaders in {}", sc.index.size(), path);
}

inline void OnShaderCreate(uint8_t* base, uint32_t container, bool pixel) {
  if (!Plausible(container)) return;
  const uint32_t len = Load32(base, container + 4) + Load32(base, container + 8);
  if (len > (1u << 20)) return;
  const uint64_t hash = XXH3_64bits(base + container, len);
  ShaderCheck& sc = g_shaders;
  std::lock_guard<std::mutex> guard(sc.lock);
  if (!sc.loaded) LoadShaderIndex(sc);
  ++sc.creates;
  if (!sc.seen.insert(hash).second) return;
  if (std::binary_search(sc.index.begin(), sc.index.end(), hash)) {
    ++sc.hits;
  } else {
    ++sc.misses;
    REXLOG_WARN("[census] shader miss {} {:016X} (len {}, magic {:08X})", pixel ? "PS" : "VS", hash, len,
                Load32(base, container));
  }
}

inline uint64_t Take(std::atomic<uint64_t>& a) { return a.exchange(0, std::memory_order_relaxed); }

inline void OnSwap() {
  const uint32_t draws = g_frame_draws.exchange(0, std::memory_order_relaxed);
  Window& w = g_window;
  std::lock_guard<std::mutex> guard(w.lock);
  ++w.frame_index;
  ++w.frames;
  w.total += draws;
  w.last = draws;
  if (draws < w.min) w.min = draws;
  if (draws > w.max) w.max = draws;
  if (w.frames < uint64_t(REXCVAR_GET(eamma_d3d_census_every))) return;

  const double f = double(w.frames);
  uint64_t calls[kFnCount], t[kTallyCount];
  for (int i = 0; i < kFnCount; ++i) calls[i] = Take(g_calls[i]);
  for (int i = 0; i < kTallyCount; ++i) t[i] = Take(g_tally[i]);
  const uint64_t gpu_like = t[kDirect] + t[kInternal] + t[kReplayed] + t[kClearRects] + t[kGprDraws];
  const double cb_share = gpu_like ? 100.0 * double(t[kReplayed]) / double(gpu_like) : 0.0;
  const double zpass_share = t[kDirect] ? 100.0 * double(t[kZPassDraws]) / double(t[kDirect]) : 0.0;
  // gpu-like = what the GPU runs: direct draws + replayed command-buffer draws + clear quads + the
  // GPR-allocation flush points.
  REXLOG_INFO("[census] frame {} ({} frames): gpu-like {:.1f}/frame (min {} max {} last {}) = direct {:.1f} "
              "+ internal {:.1f} + replayed {:.1f} + clears {:.1f} + gpr flush {:.1f}; recorded {:.1f}; runs {} ({:.1f}/frame, {} predicated draws, "
              "{} empty, {} nested; {:.1f} draws/frame in the buffers themselves); command-buffer share {:.2f}%",
              w.frame_index, w.frames, double(gpu_like) / f, w.min, w.max, w.last, double(t[kDirect]) / f,
              double(t[kInternal]) / f, double(t[kReplayed]) / f, double(t[kClearRects]) / f, double(t[kGprDraws]) / f,
              double(t[kRecorded]) / f, calls[kRunCb],
              double(calls[kRunCb]) / f, t[kReplayedPred], t[kRunsNoDraws], t[kRunsInRecord],
              double(t[kBufferDraws]) / f, cb_share);
  REXLOG_INFO("[census] frame {}: zpass {} calls, {:.1f} draws/frame ({:.2f}% of direct); tiling {} calls, "
              "{} tiles, {:.1f} draws/frame; queries issue {} (occlusion {}, other {}) getdata {}; "
              "DrawVerticesUP {}",
              w.frame_index, calls[kBeginZPass], double(t[kZPassDraws]) / f, zpass_share, calls[kBeginTiling],
              t[kTiles], double(t[kTilingDraws]) / f, calls[kQueryIssue], t[kQueryOcclusion], t[kQueryOther],
              calls[kQueryGetData], calls[kDrawUP]);
  // Each Resolve's own copy is a draw packet too, but the emulated GPU runs it as a copy (not a draw):
  // resolve draws beyond one per resolve are the extra GPU draws.
  REXLOG_INFO("[census] frame {}: resolves {:.1f}/frame, draw packets in them {:.1f}/frame (extra over one "
              "per resolve {:.1f}); gpu-like + extra {:.1f}/frame",
              w.frame_index, double(calls[kResolve]) / f, double(t[kResolveDraws]) / f,
              (double(t[kResolveDraws]) - double(calls[kResolve])) / f,
              (double(gpu_like) + double(t[kResolveDraws]) - double(calls[kResolve])) / f);
  REXLOG_INFO("[census] frame {}: device check VS +0x3248 {}/{} match, PS +0x3244 {}/{}, decl +0x2ED8 {}/{}",
              w.frame_index, t[kVsMatch], t[kVsMatch] + t[kVsMismatch], t[kPsMatch], t[kPsMatch] + t[kPsMismatch],
              t[kDeclMatch], t[kDeclMatch] + t[kDeclMismatch]);
  std::string line;
  for (int i = 0; i < kFnCount; ++i) {
    if (!calls[i]) continue;
    line += fmt::format(" {}={:.1f}", kFnName[i], double(calls[i]) / f);
  }
  REXLOG_INFO("[census] frame {}: calls/frame{}", w.frame_index, line);
  {
    std::lock_guard<std::mutex> sg(g_shaders.lock);
    REXLOG_INFO("[census] frame {}: shaders created {} ({} unique): {} in the cache, {} missing", w.frame_index,
                g_shaders.creates, g_shaders.seen.size(), g_shaders.hits, g_shaders.misses);
  }
  w.frames = 0;
  w.total = 0;
  w.min = UINT32_MAX;
  w.max = 0;
}

}  // namespace eamma_census

namespace uc = eamma_census;

// Count-only hooks.
#define EAMMA_CENSUS_COUNT(fn, idx)                       \
  REX_EXTERN(__imp__##fn);                               \
  extern "C" REX_FUNC(fn) {                              \
    if (uc::On()) uc::Count(uc::idx);                    \
    __imp__##fn(ctx, base);                              \
  }

EAMMA_CENSUS_COUNT(D3DDevice_SetRenderTarget, kSetRenderTarget)
EAMMA_CENSUS_COUNT(D3DDevice_SetDepthStencilSurface, kSetDepthStencil)
EAMMA_CENSUS_COUNT(D3DDevice_SetViewport, kSetViewport)
EAMMA_CENSUS_COUNT(D3DDevice_SetScissorRect, kSetScissor)
EAMMA_CENSUS_COUNT(D3DDevice_SetTexture, kSetTexture)
EAMMA_CENSUS_COUNT(D3DDevice_SetStreamSource, kSetStreamSource)
EAMMA_CENSUS_COUNT(D3DDevice_SetIndices, kSetIndices)
EAMMA_CENSUS_COUNT(D3DDevice_SetVertexShaderConstantF, kSetVsConstF)
EAMMA_CENSUS_COUNT(D3DDevice_SetPixelShaderConstantF, kSetPsConstF)
EAMMA_CENSUS_COUNT(D3DVertexBuffer_Unlock, kVbUnlock)
EAMMA_CENSUS_COUNT(D3DIndexBuffer_Unlock, kIbUnlock)
EAMMA_CENSUS_COUNT(D3DDevice_CreateVertexDeclaration, kCreateDecl)
EAMMA_CENSUS_COUNT(D3DDevice_BeginCommandBuffer, kBeginCb)
EAMMA_CENSUS_COUNT(D3DDevice_EndCommandBuffer, kEndCb)
EAMMA_CENSUS_COUNT(sub_8831D880, kCbPaired)
EAMMA_CENSUS_COUNT(D3DQuery_GetData, kQueryGetData)
// EA only: DrawVerticesUP sub_8830E558 calls BeginVertices, which counts the draw.
EAMMA_CENSUS_COUNT(sub_8830E558, kDrawUP)
// sub_88320C20 writes a DRAW_INDX_2 itself and calls sub_8830DAA0/DBC8 -> sub_8830D5C0/D718:
// counted as calls only (its draws would double with the leaves).
EAMMA_CENSUS_COUNT(sub_88320C20, kInt3)

// Draws: (dev, ...). Counted before the call (the call itself doesn't change +0x35E0).
#define EAMMA_CENSUS_DRAW(fn, idx)                        \
  REX_EXTERN(__imp__##fn);                               \
  extern "C" REX_FUNC(fn) {                              \
    if (uc::On()) {                                      \
      uc::Count(uc::idx);                                \
      uc::OnDraw(base, ctx.r3.u32);                      \
    }                                                    \
    __imp__##fn(ctx, base);                              \
  }

EAMMA_CENSUS_DRAW(D3DDevice_BeginVertices, kBeginVertices)
EAMMA_CENSUS_DRAW(D3DDevice_DrawVertices, kDrawVertices)
EAMMA_CENSUS_DRAW(D3DDevice_DrawIndexedVertices, kDrawIndexedVertices)

// Shader / declaration setters: remember the object for the device cross-check at the next draw.
#define EAMMA_CENSUS_SET(fn, idx, last)                   \
  REX_EXTERN(__imp__##fn);                               \
  extern "C" REX_FUNC(fn) {                              \
    if (uc::On()) {                                      \
      uc::Count(uc::idx);                                \
      uc::last.store(ctx.r4.u32, std::memory_order_relaxed); \
    }                                                    \
    __imp__##fn(ctx, base);                              \
  }

// D3D-internal draw emitters (they write DRAW_INDX(_2) headers themselves; found by scanning EA's
// D3D range 0x88300000-0x8832B000 for `ori` 0x2200/0x3600 headers, EN-3): 8830D5C0 / 8830D718
// (under sub_8830DAA0/DBC8 <- sub_88320C20), 88320378 (under sub_88321178, called indirectly),
// 8832A148 (under Swap via sub_8832AA10). sub_883088F0 is device init only. All are assumed to take
// the device in r3, as UFC's did (unchecked).
#define EAMMA_CENSUS_INTERNAL(fn, idx)                      REX_EXTERN(__imp__##fn);                                 extern "C" REX_FUNC(fn) {                                  if (uc::On() && !uc::Load32(base, ctx.r3.u32 + ufc3::d3d::kRecordingCommandBuffer)) {       uc::Count(uc::idx);                                      uc::Add(uc::kInternal);                                  uc::g_frame_draws.fetch_add(1, std::memory_order_relaxed);     }                                                        __imp__##fn(ctx, base);                                }

EAMMA_CENSUS_INTERNAL(sub_88320378, kInt0)
EAMMA_CENSUS_INTERNAL(sub_8830D5C0, kInt1)
EAMMA_CENSUS_INTERNAL(sub_8830D718, kInt2)
EAMMA_CENSUS_INTERNAL(sub_8832A148, kSwapDraw)  // under Swap (sub_8831C250 -> sub_8832AA10)

// The clear draw writer (predicated DRAW_INDX_2), r3 = device.
REX_EXTERN(__imp__sub_8830EDE0);
extern "C" REX_FUNC(sub_8830EDE0) {
  if (uc::On() && !uc::Load32(base, ctx.r3.u32 + ufc3::d3d::kRecordingCommandBuffer)) {
    uc::Count(uc::kClearDraw);
    uc::Add(uc::kClearRects);
    uc::g_frame_draws.fetch_add(1, std::memory_order_relaxed);
  }
  __imp__sub_8830EDE0(ctx, base);
}

// SetShaderGPRAllocation(dev, flags, vs_gprs, ps_gprs) (NR-6b): writes three INDIRECT_BUFFERs from
// device init's buffers: the base state (EA +0x36AC), the table entry for vs_gprs (+0x36B4 + vs*8, vs and
// ps default to 64 when both are 0) and the pipe flush (EA +0x3AB4: 24 one-vertex point-list draws,
// VS B6C9863F / PS A4A965C1 on the GPU). Those points are the GPU draws no other hook sees. Scan
// the three buffers before the call (their contents don't change).
REX_EXTERN(__imp__D3DDevice_SetShaderGPRAllocation);
extern "C" REX_FUNC(D3DDevice_SetShaderGPRAllocation) {
  const uint32_t dev = ctx.r3.u32;
  if (uc::On() && !uc::Load32(base, dev + ufc3::d3d::kRecordingCommandBuffer)) {
    uc::Count(uc::kGprAlloc);
    uint32_t vs = ctx.r5.u32;
    if (!vs && !ctx.r6.u32) vs = 64;
    uc::Pm4Count c;
    const uint32_t table = vs >= 16 && vs <= 112 ? ufc3::d3d::kGprTableIb + 8 * vs : 0;  // init fills 16..112
    for (uint32_t ib : {ufc3::d3d::kGprBaseIb, table, ufc3::d3d::kGprFlushIb}) {
      if (!ib) continue;
      uc::ScanPm4(base, uc::Load32(base, dev + ib + 4), uc::Load32(base, dev + ib) & 0xFFFFFF, 1, c);
    }
    uc::Add(uc::kGprDraws, c.draws);
    uc::g_frame_draws.fetch_add(c.draws, std::memory_order_relaxed);
  }
  __imp__D3DDevice_SetShaderGPRAllocation(ctx, base);
}

EAMMA_CENSUS_SET(D3DDevice_SetVertexShader, kSetVertexShader, g_last_vs)
EAMMA_CENSUS_SET(D3DDevice_SetPixelShader, kSetPixelShader, g_last_ps)
EAMMA_CENSUS_SET(D3DDevice_SetVertexDeclaration, kSetVertexDeclaration, g_last_decl)

REX_EXTERN(__imp__D3DDevice_Swap);
extern "C" REX_FUNC(D3DDevice_Swap) {
  __imp__D3DDevice_Swap(ctx, base);
  if (uc::On()) {
    uc::Count(uc::kSwap);
    uc::ScanImageOnce(base);
    uc::OnSwap();
  }
}

// Clear(dev, Count, pRects, Flags, Color, Z, Stencil) and ClearF: calls only. Their draws are counted
// at sub_8830EDE0 (UFC: ClearF internal -> ... -> the clear emitter; EA's internal is sub_8830F868),
// so one clear can be several GPU draws.
REX_EXTERN(__imp__D3DDevice_Clear);
extern "C" REX_FUNC(D3DDevice_Clear) {
  if (uc::On() && !uc::Load32(base, ctx.r3.u32 + ufc3::d3d::kRecordingCommandBuffer)) {
    uc::Count(uc::kClear);
  }
  __imp__D3DDevice_Clear(ctx, base);
}

// ClearF is also what Clear calls per rect; count only calls that don't come from Clear.
REX_EXTERN(__imp__D3DDevice_ClearF);
extern "C" REX_FUNC(D3DDevice_ClearF) {
  const uint32_t lr = static_cast<uint32_t>(ctx.lr);
  const bool from_clear = lr >= 0x8830FF18u && lr < 0x8830FFD8u;  // inside Clear sub_8830FF18
  if (uc::On() && !from_clear && !uc::Load32(base, ctx.r3.u32 + ufc3::d3d::kRecordingCommandBuffer)) {
    uc::Count(uc::kClearF);
  }
  __imp__D3DDevice_ClearF(ctx, base);
}

// Resolve(dev, ...): in UFC the internal resolver memcpys canned PM4 into the ring, with
// draws no hook sees (the GPU logs them as 1-vertex point lists). Scan the ring range the call wrote
// (device +0x30 before / after; a KickOff mid-call resets it, then the call is skipped).
REX_EXTERN(__imp__D3DDevice_Resolve);
extern "C" REX_FUNC(D3DDevice_Resolve) {
  if (!uc::On()) {
    __imp__D3DDevice_Resolve(ctx, base);
    return;
  }
  const uint32_t dev = ctx.r3.u32;
  const bool recording = uc::Load32(base, dev + ufc3::d3d::kRecordingCommandBuffer) != 0;
  const uint32_t before = uc::Load32(base, dev + ufc3::d3d::kRing);
  __imp__D3DDevice_Resolve(ctx, base);
  uc::Count(uc::kResolve);
  const uint32_t after = uc::Load32(base, dev + ufc3::d3d::kRing);
  if (!recording && after > before && after - before < 0x40000) {
    uc::Pm4Count c;
    uc::ScanPm4Va(base, before + 4, (after - before) / 4, 0, c);
    uc::Add(uc::kResolveDraws, c.draws);
  }
}

// RunCommandBuffer(dev, cb, ...): count the draws the buffer replays. The ring range the call writes
// is scanned (following its INDIRECT_BUFFERs), because the call can emit a buffer more than once
// (the fight's 12-draw predicated buffers reach the GPU three times: once per tile). The buffer's own
// segment list is scanned too, for the per-buffer draw count.
REX_EXTERN(__imp__D3DDevice_RunCommandBuffer);
extern "C" REX_FUNC(D3DDevice_RunCommandBuffer) {
  if (!uc::On()) {
    __imp__D3DDevice_RunCommandBuffer(ctx, base);
    return;
  }
  uc::Count(uc::kRunCb);
  const uint32_t dev = ctx.r3.u32, cb = ctx.r4.u32;
  const bool recording = uc::Load32(base, dev + ufc3::d3d::kRecordingCommandBuffer) != 0;
  const uint32_t before = uc::Load32(base, dev + ufc3::d3d::kRing);
  if (recording) {
    uc::Add(uc::kRunsInRecord);
  } else if (uc::Plausible(cb)) {
    static std::atomic<bool> logged{false};
    uint32_t ops[128] = {};
    const bool log_ops = !logged.exchange(true);
    const uc::Pm4Count c = uc::ScanCommandBuffer(base, cb, log_ops ? ops : nullptr);
    if (log_ops) {
      std::string line;
      for (int op = 0; op < 128; ++op)
        if (ops[op]) line += fmt::format(" {:02X}x{}", op, ops[op]);
      REXLOG_INFO("[census] first RunCommandBuffer cb {:08X} (flags {:08X}): {} draws ({} predicated), type-3 ops{}",
                  cb, uc::Load32(base, cb + ufc3::d3d::kCbFlags), c.draws, c.predicated, line);
    }
    uc::Add(uc::kBufferDraws, c.draws);
    uc::Add(uc::kReplayedPred, c.predicated);
    if (!c.draws) uc::Add(uc::kRunsNoDraws);
  }
  __imp__D3DDevice_RunCommandBuffer(ctx, base);
  const uint32_t after = uc::Load32(base, dev + ufc3::d3d::kRing);
  if (!recording && after > before && after - before < 0x40000) {
    uc::Pm4Count c;
    uc::ScanPm4Va(base, before + 4, (after - before) / 4, 0, c);
    uc::Add(uc::kReplayed, c.draws);
    uc::g_frame_draws.fetch_add(c.draws, std::memory_order_relaxed);
  }
}

REX_EXTERN(__imp__D3DDevice_BeginZPass);
extern "C" REX_FUNC(D3DDevice_BeginZPass) {
  if (uc::On()) {
    uc::Count(uc::kBeginZPass);
    uc::g_in_zpass.fetch_add(1, std::memory_order_relaxed);
  }
  __imp__D3DDevice_BeginZPass(ctx, base);
}

REX_EXTERN(__imp__D3DDevice_EndZPass);
extern "C" REX_FUNC(D3DDevice_EndZPass) {
  if (uc::On()) {
    uc::Count(uc::kEndZPass);
    if (uc::g_in_zpass.load(std::memory_order_relaxed) > 0) uc::g_in_zpass.fetch_sub(1, std::memory_order_relaxed);
  }
  __imp__D3DDevice_EndZPass(ctx, base);
}

// BeginTiling(dev, flags, count, rects, ...).
REX_EXTERN(__imp__D3DDevice_BeginTiling);
extern "C" REX_FUNC(D3DDevice_BeginTiling) {
  if (uc::On()) {
    uc::Count(uc::kBeginTiling);
    uc::Add(uc::kTiles, ctx.r5.u32 < 64 ? ctx.r5.u32 : 0);
    uc::g_in_tiling.fetch_add(1, std::memory_order_relaxed);
  }
  __imp__D3DDevice_BeginTiling(ctx, base);
}

REX_EXTERN(__imp__D3DDevice_EndTiling);
extern "C" REX_FUNC(D3DDevice_EndTiling) {
  if (uc::On()) {
    uc::Count(uc::kEndTiling);
    if (uc::g_in_tiling.load(std::memory_order_relaxed) > 0) uc::g_in_tiling.fetch_sub(1, std::memory_order_relaxed);
  }
  __imp__D3DDevice_EndTiling(ctx, base);
}

// D3DQuery_Issue(query, flags): are occlusion queries used? (UFC layout, EA sub_88317408) reads the
// device at query+0 and the type at query+4 (9 = occlusion / ZPD, 10 = event).
REX_EXTERN(__imp__D3DQuery_Issue);
extern "C" REX_FUNC(D3DQuery_Issue) {
  if (uc::On()) {
    uc::Count(uc::kQueryIssue);
    const uint32_t q = ctx.r3.u32;
    const uint32_t type = uc::Plausible(q) ? uc::Load32(base, q + ufc3::d3d::kQueryType) : 0;
    uc::Add(type == 9 ? uc::kQueryOcclusion : uc::kQueryOther);
  }
  __imp__D3DQuery_Issue(ctx, base);
}

// CreateVertexShader / CreatePixelShader(pFunction): the container is r3 (as the native hook reads it).
REX_EXTERN(__imp__D3DDevice_CreateVertexShader);
extern "C" REX_FUNC(D3DDevice_CreateVertexShader) {
  if (uc::On()) {
    uc::Count(uc::kCreateVs);
    uc::OnShaderCreate(base, ctx.r3.u32, false);
  }
  __imp__D3DDevice_CreateVertexShader(ctx, base);
}

REX_EXTERN(__imp__D3DDevice_CreatePixelShader);
extern "C" REX_FUNC(D3DDevice_CreatePixelShader) {
  if (uc::On()) {
    uc::Count(uc::kCreatePs);
    uc::OnShaderCreate(base, ctx.r3.u32, true);
  }
  __imp__D3DDevice_CreatePixelShader(ctx, base);
}
