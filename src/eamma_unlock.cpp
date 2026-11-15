// eamma - fighters that are on the disc but locked (FINDINGS-unlocks.md).
//
// The roster provider sub_883C3A78 skips every fighter for which sub_8839C918(this, fighter id) returns 1.
// A fighter is locked when fighters.asset_flags in data/common/db/mma.db has
//   8 = needs an entitlement: on console a marketplace key package holding only EA_PackageMeta.xml
//       (McCarthy id 68, Classic Frank Shamrock 67, Herschel Walker 65, and the Online Pass, which is what the
//       package-0 rows of Arlovski, Rutten and Ken Shamrock match), or
//   2 = needs a profile reward bit (Classic Randy Couture: the demo-invite reward of a dead online feature).
// The hook answers "not locked" for the ids in eamma_unlock_fighters and asks the game for everyone else.
// It is a pure query with one caller: nothing is written to the database, the profile or the save.

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/func.h>

REXCVAR_DEFINE_STRING(eamma_unlock_fighters, "31,10,101,15,8,9,12", "eamma",
                      "Fighter ids (mma.db fighters.id, comma-separated) shown on the roster although their "
                      "entitlement or reward is missing: 31 Big John McCarthy, 10 Classic Frank Shamrock, 101 "
                      "Herschel Walker, 15 Classic Randy Couture, 8 Classic Andrei Arlovski, 9 Classic Bas "
                      "Rutten, 12 Classic Ken Shamrock. Empty = the game's own locks");

extern "C" REX_FUNC(__imp__sub_8839C918);

extern "C" REX_FUNC(sub_8839C918) {
  static const std::vector<uint32_t> ids = [] {
    std::vector<uint32_t> out;
    const std::string text = REXCVAR_GET(eamma_unlock_fighters);
    const char *p = text.c_str();
    while (*p) {
      char *end = nullptr;
      const unsigned long v = std::strtoul(p, &end, 10);
      if (end == p) {
        ++p;
        continue;
      }
      out.push_back(uint32_t(v));
      p = end;
    }
    return out;
  }();
  const uint32_t id = ctx.r4.u32;
  __imp__sub_8839C918(ctx, base);
  if (ctx.r3.u32 == 0)
    return;
  for (const uint32_t wanted : ids) {
    if (wanted != id)
      continue;
    static std::atomic<uint32_t> s_logs{0};
    if (s_logs.fetch_add(1, std::memory_order_relaxed) < 16)
      REXLOG_INFO("[unlock] fighter {} shown although the game has it locked", id);
    ctx.r3.u64 = 0;
    return;
  }
}
