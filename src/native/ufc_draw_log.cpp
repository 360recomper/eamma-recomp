// Native renderer dev aid: everything one frame draws, in order.
//
//   SVR_DRAW_LOG=7000   (frame index, counted in presents; logs/game.log gets a [drawlog] line per
//                        draw, clear and resolve of that frame)
//   F9 in game          logs the next frame the same way (svr_drawlog_key)
//
// Per draw: the game's render target / depth surface and the host target they map to, viewport,
// scissor, shaders, and every bound texture (the game's D3D texture and its host counterpart).

#include "ufc_draw_log.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <fmt/format.h>

#include <rex/cvar.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

#include "core/logging.h"
#include "core/memory_helpers.h"
#include "gpu/d3d.h"
#include "gpu/device.h"
#include "gpu/resources.h"
#include "gpu/shaders/shader_constants.h"
#include "gpu/vertex_declaration.h"
#include "ufc_geometry.h"
#include "ufc_nr10_probe.h"
#include "ufc_resources.h"

REXCVAR_DEFINE_STRING(eamma_native_draw_log_at, "", "eamma",
                      "Native renderer dev aid: seconds after the first present (comma separated) "
                      "at which one whole frame is logged draw by draw ([drawlog] lines)");

REXCVAR_DEFINE_STRING(eamma_native_draw_log_rt, "", "eamma",
                      "Native renderer dev aid: WxH. Every draw, clear and resolve while a render target "
                      "of this host size is bound is logged ([drawlog] lines), up to a few thousand lines: "
                      "for passes that last one frame at no fixed time (2048x1024 = the CAF composites)");

namespace bd::gpu {
namespace {

// eamma_native_draw_log_rt: the bound render target has the asked size, and there is budget left.
bool TargetSizeWanted() {
  static const std::pair<u32, u32> size = [] {
    const std::string text = REXCVAR_GET(eamma_native_draw_log_rt);
    const size_t x = text.find('x');
    if (x == std::string::npos)
      return std::pair<u32, u32>{0u, 0u};
    return std::pair<u32, u32>{u32(std::strtoul(text.c_str(), nullptr, 10)),
                               u32(std::strtoul(text.c_str() + x + 1, nullptr, 10))};
  }();
  if (!size.first)
    return false;
  const GuestTexture *rt = state().render_target;
  if (!rt || rt->width != size.first || rt->height != size.second)
    return false;
  static std::atomic<u32> s_calls{0};
  return s_calls.fetch_add(1, std::memory_order_relaxed) < 6000;
}

u64 TargetFrame() {
  static const u64 frame = [] {
    const char *env = std::getenv("SVR_DRAW_LOG");
    return env ? std::strtoull(env, nullptr, 0) : 0ull;
  }();
  return frame;
}

// SetTexture (sub_82255270) keeps the bound D3D texture per slot at device + (slot + 3178) * 4.
constexpr u32 kBoundTextureTable = 3180u * 4u;  // EA MMA +0x31B0 (ufc_d3d.h kTexture); UFC 3198, SvR 2010: 3178

std::string Host(const GuestTexture *t) {
  if (!t)
    return "-";
  // View dimension: the shader picks the 2D, 3D or cube heap by the fetch, so a cube fetch on a
  // 2D-view texture reads the null cube.
  const char *view = t->viewDimension == plume::RenderTextureViewDimension::TEXTURE_CUBE ? " cube"
                     : t->viewDimension == plume::RenderTextureViewDimension::TEXTURE_3D ? " 3d"
                                                                                          : "";
  return fmt::format("{}x{} f{}{}{}", t->width, t->height, u32(t->format), view,
                     t->descriptorIndex == kInvalidDescriptorIndex ? " nodesc" : "");
}

u64 ShaderHash(const GuestShader *s) {
  return s && s->shaderCacheEntry ? s->shaderCacheEntry->hash : 0ull;
}

}  // namespace

std::atomic<u64> g_key_frame{0};  // the frame F9 (or eamma_native_draw_log_at) asked for

// Laptop farm jobs can only pass cvars (--set), and frame numbers there depend on boot timing:
// eamma_native_draw_log_at=70,95.5 logs the first frame presented after each of those seconds since
// the first present. Called once per presented frame.
void SvrDrawLogTick(u64 frame) {
  static const std::vector<double> times = [] {
    std::vector<double> out;
    const std::string list = REXCVAR_GET(eamma_native_draw_log_at);
    for (size_t pos = 0; pos < list.size();) {
      const size_t end = std::min(list.find(',', pos), list.size());
      const double t = std::atof(list.substr(pos, end - pos).c_str());
      if (t > 0)
        out.push_back(t);
      pos = end + 1;
    }
    std::sort(out.begin(), out.end());
    return out;
  }();
  static size_t next = 0;
  if (next >= times.size())
    return;
  static const auto start = std::chrono::steady_clock::now();
  const double now = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  if (now < times[next])
    return;
  while (next < times.size() && now >= times[next])
    ++next;
  g_key_frame.store(frame, std::memory_order_relaxed);
  BD_INFO("[drawlog] logging frame {} ({:.1f} s after the first present)", frame, now);
}

bool SvrDrawLogActive() {
  const u64 frame = SvrFrameIndex();
  const u64 target = TargetFrame();
  return (target && frame == target) || frame == g_key_frame.load(std::memory_order_relaxed) ||
         TargetSizeWanted();
}

void SvrDrawLogNextFrame() {
  const u64 next = SvrFrameIndex() + 1;
  g_key_frame.store(next, std::memory_order_relaxed);
  BD_INFO("[drawlog] logging frame {} (F9)", next);
}

void SvrDrawLogDraw(u32 device_guest, const char *name, u32 primitive, bool indexed, u32 count,
                    u32 start, i32 base) {
  // SVR_LOG_PS=<hash hex> (env): one line per second while draws use that pixel shader (frame index
  // and how many draws), to find the scenes that draw it (A3 light shafts: 1CCBA38AC6B29550).
  {
    static const u64 watch = [] {
      const char *env = std::getenv("SVR_LOG_PS");
      return env ? std::strtoull(env, nullptr, 16) : 0ull;
    }();
    if (watch && ShaderHash(state().pixel_shader) == watch) {
      static u64 s_last_frame = 0, s_count = 0;
      ++s_count;
      const u64 frame = SvrFrameIndex();
      if (frame >= s_last_frame + 30) {
        BD_INFO("[watch-ps] {:016X} drawn at frame {} ({} draws so far)", watch, frame, s_count);
        s_last_frame = frame;
      }
    }
  }
  if (!SvrDrawLogActive())
    return;
  static u32 s_index = 0;
  const auto *dev = bd::mem::at<const D3DDevice>(device_guest);
  if (!dev)
    return;
  auto &s = state();
  std::string textures;
  for (u32 slot = 0; slot < kTextureSlots; ++slot) {
    const u32 va = bd::mem::load<u32>(device_guest + kBoundTextureTable + slot * 4u);
    if (!va && !s.textures[slot])
      continue;
    textures += fmt::format(" t{}=0x{:08X}[{}]", slot, va, Host(s.textures[slot]));
    // The fetch constant (D3DTexture+0x1C, 6 dwords): mip filter, min/max level, LOD bias.
    // Only slots with a host texture: the table's top slots (28-31 seen) hold floats, not pointers,
    // and reading through one (24.0 = 0x41C00000) is a crash (nr8b10).
    if (va && s.textures[slot]) {
      if (const auto *t = bd::mem::try_at<const D3DTexture>(va))
        textures += fmt::format("{{fc {:08X} {:08X} {:08X} {:08X} {:08X} {:08X}}}",
                                u32(t->Format.dword[0]), u32(t->Format.dword[1]),
                                u32(t->Format.dword[2]), u32(t->Format.dword[3]),
                                u32(t->Format.dword[4]), u32(t->Format.dword[5]));
    }
  }
  // Register shadows: the 0x2100 block at +0x28CC, the 0x2200 block at +0x2934 (one dword each).
  const auto reg = [&](u32 r) -> u32 {
    const u32 off = r >= 0x2200 ? 0x2934u + (r - 0x2200u) * 4u : 0x28CCu + (r - 0x2100u) * 4u;
    return bd::mem::load<u32>(device_guest + off);
  };
  const auto regf = [&](u32 r) {
    const u32 v = reg(r);
    float f;
    std::memcpy(&f, &v, 4);
    return f;
  };
  if (const auto *decl = s.vertex_declaration) {
    std::string elements;
    for (u32 i = 0; i < decl->inputElementCount; ++i) {
      const auto &e = decl->inputElements[i];
      if (e.slotIndex == 15)
        continue;
      elements += fmt::format(" {}{}:s{}+{}:f{}", e.semanticName ? e.semanticName : "?",
                              e.semanticIndex, e.slotIndex, e.alignedByteOffset, u32(e.format));
    }
    BD_INFO("[drawlog]   decl{}", elements);
  }
  BD_INFO("[drawlog]   blendctl 0x{:08X} colorctl 0x{:08X} alpharef {:.4g} | host blend {} {}/{} "
          "alphatest {} threshold {:.4g} zfunc {} zwrite {}",
          reg(0x2201), reg(0x2202), regf(0x210E), s.pipelineState.alphaBlendEnable ? "on" : "off",
          u32(s.pipelineState.srcBlend), u32(s.pipelineState.destBlend),
          (s.pipelineState.specConstants & kSpecConstantAlphaTest) ? "on" : "off",
          Video::AlphaThreshold(), u32(s.pipelineState.zFunc),
          s.pipelineState.zWriteEnable ? "on" : "off");
  // Scissor registers as the draw sends them: PA_SC_SCREEN_SCISSOR TL/BR (0x200E/0x200F, device
  // +0x28B8/+0x28BC) and PA_SC_WINDOW_SCISSOR TL/BR (0x2081/0x2082, +0x28C4/+0x28C8).
  BD_INFO("[drawlog]   scissor regs screen {:08X} {:08X} window {:08X} {:08X}",
          bd::mem::load<u32>(device_guest + 0x28B8u), bd::mem::load<u32>(device_guest + 0x28BCu),
          bd::mem::load<u32>(device_guest + 0x28C4u), bd::mem::load<u32>(device_guest + 0x28C8u));
  // Stencil: UFC's 2D screens clip with stencil masks (mask shapes drawn with colour writes off).
  BD_INFO("[drawlog]   stencil {} two-sided {} func {} ref {} mask 0x{:02X} wmask 0x{:02X} "
          "fail/zfail/pass {}/{}/{} ccw func {} {}/{}/{} | refmask reg 0x{:08X} bf 0x{:08X} | cull {} "
          "front {}",
          s.pipelineState.stencilEnable ? "on" : "off", s.pipelineState.stencilTwoSided ? "yes" : "no",
          u32(s.pipelineState.stencilFunc), u32(s.pipelineState.stencilRef),
          u32(s.pipelineState.stencilMask), u32(s.pipelineState.stencilWriteMask),
          u32(s.pipelineState.stencilFail), u32(s.pipelineState.stencilZFail),
          u32(s.pipelineState.stencilPass), u32(s.pipelineState.stencilFuncCCW),
          u32(s.pipelineState.stencilFailCCW), u32(s.pipelineState.stencilZFailCCW),
          u32(s.pipelineState.stencilPassCCW), reg(0x210D), reg(0x210C),
          u32(s.pipelineState.cullMode), u32(s.pipelineState.frontFace));
  BD_INFO("[drawlog]   vte 0x{:X} clip 0x{:X} mode 0x{:X} depthctl 0x{:08X} colormask 0x{:X} "
          "ps-c13 {:.4g},{:.4g},{:.4g},{:.4g} ps-c14 {:.4g},{:.4g},{:.4g},{:.4g}",
          reg(0x2206), reg(0x2204), reg(0x2205), reg(0x2200), reg(0x2104),
          float(dev->psFloatConstants[13][0]), float(dev->psFloatConstants[13][1]),
          float(dev->psFloatConstants[13][2]), float(dev->psFloatConstants[13][3]),
          float(dev->psFloatConstants[14][0]), float(dev->psFloatConstants[14][1]),
          float(dev->psFloatConstants[14][2]), float(dev->psFloatConstants[14][3]));
  // NR-8b-2j: the mat shadow chain's constants (mask, blur, mat).
  const u32 vs_id = u32(ShaderHash(s.vertex_shader) >> 32);
  const auto vs_regs = [&](u32 first, u32 n) {
    std::string out;
    for (u32 c = first; c < first + n; ++c)
      out += fmt::format(" c{}={:.5g},{:.5g},{:.5g},{:.5g}", c, float(dev->vsFloatConstants[c][0]),
                         float(dev->vsFloatConstants[c][1]), float(dev->vsFloatConstants[c][2]),
                         float(dev->vsFloatConstants[c][3]));
    return out;
  };
  if (vs_id == 0x0E753564u)
    BD_INFO("[drawlog]   shadow-mask{}{}", vs_regs(0, 17), vs_regs(70, 1));
  else if (vs_id == 0x38BEEC78u)
    BD_INFO("[drawlog]   shadow-blur{} ps-c34 {:.5g}", vs_regs(33, 8),
            float(dev->psFloatConstants[34][0]));
  else if (vs_id == 0xDE0484A4u)
    BD_INFO("[drawlog]   shadow-mat{}", vs_regs(137, 8));
  // NR-8b-2m: flare sprites, one draw per channel; the vertices carry the channel, c0 the level.
  if (vs_id == 0xD78630E3u) {
    SvrDrawLogStream0(device_guest);
    BD_INFO("[drawlog]   flare-sprite ps-c0 {:.5g} rt format 0x{:08X}",
            float(dev->psFloatConstants[0][0]),
            u32(bd::mem::at<const D3DSurface>(u32(dev->renderTargetShadow[0]))->Format));
  }
  // A1 (EA MMA): fighter skin pixel shaders PLAYER_HEAD 7854AAED / PLAYER_BODY D0D74FF5. Every float
  // register they read (c0..c40, c188..c197: shadow matrices, light directions) and the bool
  // constants (BOOL_BIT 128 enableIntroLighting, 129 enable_outdoor_shadow_and_lighting), to tell
  // the washed-out fighter's inputs from the good one's.
  if (const u64 ps_hash = ShaderHash(s.pixel_shader);
      ps_hash == 0x7854AAEDF51D3BE1ull || ps_hash == 0xD0D74FF5191A4EE8ull ||
      ps_hash == 0x8767A8E6920BCF0Aull /* SSS pass PS (A5) */) {
    std::string list;
    const auto add = [&](u32 c) {
      list += fmt::format(" c{}={:.4g},{:.4g},{:.4g},{:.4g}", c, float(dev->psFloatConstants[c][0]),
                          float(dev->psFloatConstants[c][1]), float(dev->psFloatConstants[c][2]),
                          float(dev->psFloatConstants[c][3]));
    };
    for (u32 c = 0; c <= 40; ++c)
      add(c);
    for (u32 c = 188; c <= 199; ++c)
      add(c);
    // Device-side fetch constants (device+0x400, what the native sampler is decoded from):
    // dword3 bits 19-20 mag, 21-22 min, 23-24 mip filter (0 point, 1 linear).
    list += " | devfc";
    for (u32 t = 0; t < 14; ++t)
      list += fmt::format(" t{}={:08X}/{:08X}", t, u32(dev->fetchConstants[t].dword[0]),
                          u32(dev->fetchConstants[t].dword[3]));
    BD_INFO("[drawlog]   skin ps {:08X} bools vs {:08X} {:08X} {:08X} {:08X} ps {:08X} {:08X} {:08X} "
            "{:08X} |{}",
            u32(ps_hash >> 32), u32(dev->vsBoolConstants[0]), u32(dev->vsBoolConstants[1]),
            u32(dev->vsBoolConstants[2]), u32(dev->vsBoolConstants[3]), u32(dev->psBoolConstants[0]),
            u32(dev->psBoolConstants[1]), u32(dev->psBoolConstants[2]), u32(dev->psBoolConstants[3]),
            list);
  }
  // NR-10g: the CAF composite's pixel shaders read c0 (g_ETCParam) and c1 (g_HSBColor, the skin
  // tone as hue/saturation/brightness); the head pass C0E01A79 outputs HSB-to-RGB(c1) + its t1
  // detail texture and never reads t0. Log c0-c3, and dump t1's level-0 bytes for the head pass
  // (tools/nr10g_dxt5_view.py decodes the tiled DXT5 dump).
  {
    const u64 ps_hash = ShaderHash(s.pixel_shader);
    std::string list;
    for (const u32 c : {0u, 1u, 2u, 3u})
      list += fmt::format(" c{}={:.5g},{:.5g},{:.5g},{:.5g}", c, float(dev->psFloatConstants[c][0]),
                          float(dev->psFloatConstants[c][1]), float(dev->psFloatConstants[c][2]),
                          float(dev->psFloatConstants[c][3]));
    BD_INFO("[drawlog]   ps {:08X} consts{}", u32(ps_hash >> 32), list);
    // The CAF face pass D7E35565 (dst = t0 the torso part, src = t1 face colour DXT5, mask = t2
    // DXT1): dump t1 and t2 level 0 for the first draws; the shorts pass C0E01A79: t1.
    const bool face = ps_hash == 0xD7E355650C8940D8ull;
    if (face || ps_hash == 0xC0E01A795CA3767Eull) {
      static std::atomic<u32> s_dumped{0};
      for (const u32 slot : {1u, 2u}) {
        if (slot == 2u && !face)
          continue;
        const u32 va = bd::mem::load<u32>(device_guest + kBoundTextureTable + slot * 4u);
        const auto *t = va ? bd::mem::try_at<const D3DTexture>(va) : nullptr;
        if (!t || s_dumped.load(std::memory_order_relaxed) >= 12)
          continue;
        const u32 d1 = u32(t->Format.dword[1]), d2 = u32(t->Format.dword[2]);
        const u32 format = d1 & 0x3Fu, base = d1 & ~0xFFFu;
        const u32 width = (d2 & 0x1FFFu) + 1, height = ((d2 >> 13) & 0x1FFFu) + 1;
        // DXT1 = 18 (8 bytes a 4x4 block), DXT2/3 = 19 and DXT4/5 = 20 (16 bytes): tiled level bytes.
        const u32 bytes = format == 20u || format == 19u ? width * height
                          : format == 18u                ? width * height / 2
                                                         : 0u;
        const u8 *data = bytes ? bd::mem::try_at<const u8>(base) : nullptr;
        if (!data)
          continue;
        const u32 n = s_dumped.fetch_add(1, std::memory_order_relaxed);
        const std::string stem = fmt::format("{}\\t{}_{:04}_{:08X}", UfcNr10DumpDir(), slot, n, va);
        if (FILE *f = std::fopen((stem + ".bin").c_str(), "wb")) {
          std::fwrite(data, 1, bytes, f);
          std::fclose(f);
        }
        const std::string about =
            fmt::format("texture {:08X} base {:08X} format {} width {} height {} fc {:08X} {:08X} "
                        "{:08X} {:08X} {:08X} {:08X}\n",
                        va, base, format, width, height, u32(t->Format.dword[0]), d1, d2,
                        u32(t->Format.dword[3]), u32(t->Format.dword[4]), u32(t->Format.dword[5]));
        if (FILE *f = std::fopen((stem + ".txt").c_str(), "wb")) {
          std::fwrite(about.data(), 1, about.size(), f);
          std::fclose(f);
        }
        BD_INFO("[drawlog]   t{} dump #{} -> {} ({}x{} fmt {} base {:08X}, {} bytes)", slot, n, stem,
                width, height, format, base, bytes);
      }
    }
  }
  // A5: the guest colour surface format (EDRAM format in SurfaceInfo, D3DFORMAT at +0x28).
  if (const auto *rts = bd::mem::try_at<const D3DSurface>(u32(dev->renderTargetShadow[0])))
    BD_INFO("[drawlog]   rt-guest surfaceinfo 0x{:08X} format 0x{:08X}", u32(rts->SurfaceInfo),
            u32(rts->Format));
  BD_INFO("[drawlog] #{} {} prim {} {} count {} start {} base {} | rt 0x{:08X}[{}] ds 0x{:08X}[{}] "
          "| vp {:.0f},{:.0f} {:.0f}x{:.0f} z {:.2f}-{:.2f} sc {},{}-{},{} | vs {:016X} ps {:016X} "
          "|{}",
          s_index++, name, primitive, indexed ? "indexed" : "", count, start, base,
          u32(dev->renderTargetShadow[0]), Host(s.render_target), u32(dev->depthStencilShadow),
          Host(s.depth_stencil), float(dev->viewport.X), float(dev->viewport.Y),
          float(dev->viewport.Width), float(dev->viewport.Height), float(dev->viewport.MinZ),
          float(dev->viewport.MaxZ), i32(dev->scissorRect.left), i32(dev->scissorRect.top),
          i32(dev->scissorRect.right), i32(dev->scissorRect.bottom), ShaderHash(s.vertex_shader),
          ShaderHash(s.pixel_shader), textures);
  // EA MMA (EN-5): for indexed draws, stream 0's first indexed vertex as guest memory holds it
  // (read like the geometry mirror: physical address through TranslatePhysical, big-endian words),
  // and VS float constants c0..c7, to check a transform by hand.
  if (indexed) {
    auto *memory = REX_KERNEL_MEMORY();
    const auto phys = [&](u32 physical) -> const u8 * {
      return memory ? memory->TranslatePhysical<const u8 *>(physical & 0x1FFFFFFFu) : nullptr;
    };
    const auto word = [](const u8 *p) { return u32(p[0]) << 24 | u32(p[1]) << 16 | u32(p[2]) << 8 | p[3]; };
    const u32 constant = 95u;  // stream 0's vertex fetch constant
    const DeviceFetchConstant &slot = dev->fetchConstants[constant / 3u];
    const u32 f0 = u32(slot.dword[(constant % 3u) * 2u]), f1 = u32(slot.dword[(constant % 3u) * 2u + 1u]);
    const u32 stride = u32(dev->streamStrideDiv4[0]) * 4u;
    u32 first = 0;
    if (const auto *ib = bd::mem::try_at<const D3DIndexBuffer>(u32(dev->indexBuffer)))
      if (const u8 *ip = phys(SvrGpuAddress(u32(ib->FetchLo)))) {
        const bool i32x = (u32(ib->resource.Common) & 0x80000000u) != 0;
        first = i32x ? word(ip + start * 4u) : (u32(ip[start * 2u]) << 8 | ip[start * 2u + 1u]);
        first = u32(i32(first) + base);
      }
    std::string v;
    if (const u8 *vp = phys(SvrGpuAddress(f0 & 0x1FFFFFFCu)); vp && stride)
      for (u32 k = 0; k < 8; ++k) {
        const u32 w = word(vp + size_t(first) * stride + k * 4u);
        float f;
        std::memcpy(&f, &w, 4);
        v += fmt::format(" {:08X}({:.4g})", w, f);
      }
    std::string c;
    for (u32 r : {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 156u, 157u, 158u, 159u})
      c += fmt::format(" c{}={:.4g},{:.4g},{:.4g},{:.4g}", r, float(dev->vsFloatConstants[r][0]),
                       float(dev->vsFloatConstants[r][1]), float(dev->vsFloatConstants[r][2]),
                       float(dev->vsFloatConstants[r][3]));
    BD_INFO("[drawlog]   vb fetch {:08X} {:08X} stride {} vertex {}:{}", f0, f1, stride, first, v);
    BD_INFO("[drawlog]   vs{}", c);
  }
}

void SvrDrawLogResolve(u32 device_guest, u32 flags, u32 source_rect_va, u32 dest_va,
                       u32 dest_point_va) {
  if (!SvrDrawLogActive())
    return;
  const auto *dev = bd::mem::at<const D3DDevice>(device_guest);
  std::string rect = "full";
  if (const auto *r = bd::mem::try_at<const D3DRect>(source_rect_va))
    rect = fmt::format("{},{}-{},{}", i32(r->left), i32(r->top), i32(r->right), i32(r->bottom));
  std::string point = "0,0";
  if (const auto *p = bd::mem::try_at<const be_i32>(dest_point_va))
    point = fmt::format("{},{}", i32(p[0]), i32(p[1]));
  BD_INFO("[drawlog] resolve flags 0x{:X} from rt 0x{:08X} rect {} -> 0x{:08X} at {}", flags,
          dev ? u32(dev->renderTargetShadow[0]) : 0u, rect, dest_va, point);
}

void SvrDrawLogVertices(u32 data_va, u32 count, u32 stride) {
  if (!SvrDrawLogActive() || !data_va || !stride)
    return;
  for (u32 v = 0; v < count && v < 4; ++v) {
    std::string line;
    for (u32 k = 0; k < stride / 4 && k < 10; ++k)
      line += fmt::format(" {:08X}", bd::mem::load<u32>(data_va + v * stride + k * 4));
    BD_INFO("[drawlog]   v{}:{}", v, line);
  }
}

void SvrDrawLogClear(u32 device_guest, u32 count, u32 rects_va, u32 flags, u32 color) {
  if (!SvrDrawLogActive())
    return;
  const auto *dev = bd::mem::at<const D3DDevice>(device_guest);
  std::string rects;
  for (u32 i = 0; i < count && i < 8; ++i) {
    if (const auto *r = bd::mem::try_at<const D3DRect>(rects_va + i * 16u))
      rects += fmt::format(" {},{}-{},{}", i32(r->left), i32(r->top), i32(r->right),
                           i32(r->bottom));
  }
  BD_INFO("[drawlog] clear flags 0x{:X} color 0x{:08X} rt 0x{:08X} rects {}{}", flags, color,
          dev ? u32(dev->renderTargetShadow[0]) : 0u, count, rects);
}

}  // namespace bd::gpu
