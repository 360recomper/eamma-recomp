# Locked fighters that are on the disc (2026-11-15)

Static research by an agent, then a hook and one run by the main session. "Proven" = read from code or data.

## How the lock works (proven)
- Not by `XamContentGetLicenseMask` (its one caller is file-system setup). The roster comes from the SQLite database
  `assets/data/common/db/mma.db`: `fighters.asset_flags` plus the `dlc_entitlements` table.
- Flag 1 = normal. Flag 8 = needs an entitlement. Flag 2 = needs a profile reward bit (flag 3 on a few others).
- The fighter-list provider `sub_883C3A78` skips every row for which `sub_8839C918(this, fighter id)` returns 1
  (its only caller). That function = `sub_88547320(0, id) == 2` (reward still locked) or `sub_885473A0(0, id) == 8`
  (entitlement missing; owned list in the EntitlementFacade object `[0x89069E20]` +80).
- Entitlements come from installed marketplace packages that hold only `EA_PackageMeta.xml`
  (`<downloadcontent name id contenttype date>`); `sub_8853B340` matches them: content type 128 with id = the row's
  `package_id`, or content type 256 for rows with `package_id <= 1`.

| Fighter | id | flags | package | the user's package file |
|---|---|---|---|---|
| Big John McCarthy | 31 | 8 | 68 | "MMA Fighter: Big John McCarthy", prmbbf01, id 68, type 128 |
| Classic Frank Shamrock | 10 | 8 | 67 | prmwmf01, id 67, type 128 |
| Herschel Walker | 101 | 8 | 65 | prmgsf01, id 65, type 128 |
| Classic Andrei Arlovski / Bas Rutten / Ken Shamrock | 8 / 9 / 12 | 8 | 0 | **Online Pass** ("tollfree", id 0, type 256): these three are not gameplay unlocks, they came with the pass |
| Classic Randy Couture | 15 | 2 | 16 (reward) | none: demo-invite reward, profile bit 15 at profile +168 (`sub_883982F0`) |

- Flag 2/3 rows that are probably not real roster fighters: Player Fighter 0, "Max <weight>" 43-47, McGrath 48,
  generics 102-106; Classic Yoshida 11, Randleman 13, Miletich 14 have flag 3 (lead: may lack assets on the disc;
  not unlocked by default).

## What was built
- `src/eamma_unlock.cpp` (in the game DLL for every build type): hook on `sub_8839C918`. It asks the game first and
  answers "not locked" only for the ids in `--eamma_unlock_fighters` (default `31,10,101,15,8,9,12`; empty = the
  game's own locks). Pure query: nothing is written to the database, profile or save.
- Run u1 (native, boot -> fighter select): `[unlock]` lines for 101, 15, 31 and 8 (the heavyweight list). **Not
  checked: picking one of them and fighting** (model, portrait, name audio), the other weight classes, career mode.

## Not done / leads
- Promo gear (Dr Pepper / Rockstar swap parts, `dlc_entitlements.content_type` 1) goes through `sub_8839B050` /
  `sub_885473A0` and is still locked.
- Installing the user's four packages instead would also work in principle (the SDK would have to enumerate
  marketplace content); not tried, the hook makes it unnecessary.
- Online fighter select and any re-check at fight load were not traced.
- To try the flag-3 classics: `--eamma_unlock_fighters=31,10,101,15,8,9,12,11,13,14`.

## Other hidden fighters: are their assets on the disc? (Opus agent, static, 2026-11-15)
- Model archive per fighter: `sub_883E2760` reads `attribdb_asset_name` (query at 0x8803FEB0) and loads
  `data/xenon/gameexplorer/<name>_big.ast` (proven); it skips the loader when a virtual check before it fails (lead:
  a file-exists test).
- **Classic Yoshida 11, Classic Randleman 13, Classic Miletich 14 are stubs (proven):** no `*_classic_big.ast` among
  the 385 archives, no mesh or texture named for them in any archive, only display names in the name table of
  `common_big`, and 0 rows in `fighters_fighting_moves` (roster fighters have 8-26). Name text and commentary name
  tags exist. Do not add them to `eamma_unlock_fighters`.
- Max <weight> 43-47: debug clones using Couture's archive. Mike McGrath 48: model present, 0 moves (lead: sparring
  NPC). 102-106 (Fujimoto, Rickson Gracie, Wongkamlao, Guerra, McCulloch): gym trainers with `*_trainer_big`
  archives, no own portrait. Player Fighter 0: create-a-fighter template. None is a roster-grade fighter.
- Untested idea for 11/13/14: point their `character_id` at the modern character (11->214, 13->158, 14->141) so they
  load the modern model under the "Classic" name; the 0-moves question stays.
- Roster oddities worth one look in game: Kazuo Misaki (ids 34, 85) has no `misakikazuo_big.ast` on the disc;
  Gesias Cavalcante (75) asks for `cavalcantegesias` but the file is spelled `calvancantegesias_big.ast`. If either
  fights normally, the missing-archive reading above is wrong.
- Not determined: portraits (hashed image libraries), what the game shows when an archive is missing.
