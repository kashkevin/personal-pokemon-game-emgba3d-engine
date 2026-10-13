# EmeraldTogether expansion: feature checklist (phases 0-15)

Branch `claude/emeraldtogether-expansion`, from `emeraldtogether-pre-expansion-20261013`
(= engine `8c8c11ab2`, the unified game). Rollback point: that tag.

Owner decisions (2026-10-13): wild spawns are SHARED between players in one world;
all phases in scope; real multi-PC and GPU tests on the owner's PC once a build is ready.

Status values: NOT STARTED | INSPECTING | IN PROGRESS | IMPLEMENTED (no evidence) |
TESTED (evidence named) | BLOCKED (reason). Nothing is TESTED without a recorded run.

| Phase | Feature | Status | Notes |
| --- | --- | --- | --- |
| 0 | Inspect, preserve, checkpoint, checklist | IN PROGRESS | checkpoint tag and branch made; inspection of each system below pending |
| 1 | Permanent Gen 9 EXP Share, all six slots, no duplicate awards, no awards in invalid battles | IMPLEMENTED (upstream code, audited; no run) | Held EXP Share (ITEM_EXP_SHARE, HOLD_EFFECT_EXP_SHARE) is permanent: I_EXP_SHARE_FLAG=0 so the Gen 6 key-item toggle is off; B_SPLIT_EXP GEN_LATEST gives non-participants 50% of the full yield; PARTY_SIZE loop covers all six; expGettersOrder and givenExpMons prevent duplicate awards; BattleTypeAllowsExp excludes link/PvP, Frontier, Safari, Battle Tower, e-Reader. Existing test/battle/exp.c not run here. |
| 2 | Weather-based wild encounter weights (configurable multipliers, regional logic) | IMPLEMENTED (no evidence yet) | PC builds only (GBA ROM unchanged, f214735b...): src/mp_wild_random.c GetWeatherWeight: rain/downpour Water, thunderstorm Electric+Water, snow Ice, sun/drought Fire+Grass, sandstorm Ground+Rock, fog Ghost+Psychic; heavy rain adds x2 to rare tiers; multipliers and MP_WILD_WEATHER_ENABLED are macros; underground maps excluded. Applies to all MPWild_RandomizeSpecies callers (traditional and visible). Not run in game. Open: weather sync between players depends on the shared weather state (Phase 3-5 shared-spawn design); PokéNav info is Phase 7. |
| 3 | Visible overworld shiny Pokemon: sparkle, sound, shiny state fixed at creation | NOT STARTED | visible shiny exists (MP_GEN6_SHINY_ODDS); sparkle/sound/sync not verified |
| 4 | Mass outbreaks: species and map, notice, PokeNav list, expiry, shared world events | NOT STARTED | no outbreak code in our tree |
| 5 | Expanded following Pokemon (animations, weather/object reactions, items, slopes/stairs) | NOT STARTED | follower exists (OW_FOLLOWERS_ENABLED); animations and items not verified |
| 6 | Multiplayer Secret Bases (decor, visits, NPC teams, permissions, saves) | NOT STARTED | upstream secret bases are single-player |
| 7 | PokeNav upgrade (full map, positions, weather by route, outbreaks, event locations, habitats) | NOT STARTED | upstream PokeNav exists; weather and event info new |
| 8 | Multiplayer Battle Frontier modes (2v2, co-op survival, tournaments, rotating rules, streaks) | NOT STARTED | Championship exists (unverified); other modes new |
| 9 | Breeding and Egg system (Day Care UI, hatch progress, inheritance, groups) | NOT STARTED | upstream Day Care exists; check inheritance rules and UI |
| 10 | Dynamic legendary events (weather, night, cooperative objectives, limited time) | NOT STARTED | first-catch rule exists (unverified) |
| 11 | Living world clock: visual clock vs real-time timers; fix the 2-hour daily reset | IMPLEMENTED (no evidence yet) | Native PC builds: daily reset (VAR_DAYS), berry growth and init use the player's wall clock via RtcCalcRealTime even while connected; gLocalTime (visual, 12x) unchanged. GBA ROM byte-identical (f214735b...). Native 3D build compiles; check-native 4203/244/176 pass. No clock-specific regression test yet; no in-game run yet. Host-side outbreak/event timers (app) still count with nobody online: open. |
| 12 | Integrate everything into one playable game (3D, battles, sprites, native UI, customization, trading, saves, weather, events, Mega) | NOT STARTED | after phases 1-11 |
| 13 | Full troubleshooting and testing: gameplay, multiplayer (2-4 players, real PCs), graphics, performance, UI and audio, saves, automated regression | NOT STARTED | includes the Route 110 crash after Psychic Edward (U13); real GPU and multi-PC runs need the owner's PC |
| 14 | Fix every discovered problem, with a regression test per repaired bug | NOT STARTED | priorities: crashes, save corruption, duplication, desync, broken progression |
| 15 | Final build, documentation, checkpoint, delivery with exact commit and build location | NOT STARTED | completion claimed only where the criteria in the brief are met |

Cross-cutting (every phase): regression tests where practical; no feature disabled to pass a
test; saves, multiplayer and battles must keep working; PC build evidence recorded here.
