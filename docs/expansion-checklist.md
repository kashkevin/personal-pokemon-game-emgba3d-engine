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
| 1 | Permanent Gen 9 EXP Share, all six slots, no duplicate awards, no awards in invalid battles | NOT STARTED | upstream expansion has an EXP Share item; check how it is wired |
| 2 | Weather-based wild encounter weights (configurable multipliers, regional logic) | NOT STARTED | `weather` only drives visuals in our code today |
| 3 | Visible overworld shiny Pokemon: sparkle, sound, shiny state fixed at creation | NOT STARTED | visible shiny exists (MP_GEN6_SHINY_ODDS); sparkle/sound/sync not verified |
| 4 | Mass outbreaks: species and map, notice, PokeNav list, expiry, shared world events | NOT STARTED | no outbreak code in our tree |
| 5 | Expanded following Pokemon (animations, weather/object reactions, items, slopes/stairs) | NOT STARTED | follower exists (OW_FOLLOWERS_ENABLED); animations and items not verified |
| 6 | Multiplayer Secret Bases (decor, visits, NPC teams, permissions, saves) | NOT STARTED | upstream secret bases are single-player |
| 7 | PokeNav upgrade (full map, positions, weather by route, outbreaks, event locations, habitats) | NOT STARTED | upstream PokeNav exists; weather and event info new |
| 8 | Multiplayer Battle Frontier modes (2v2, co-op survival, tournaments, rotating rules, streaks) | NOT STARTED | Championship exists (unverified); other modes new |
| 9 | Breeding and Egg system (Day Care UI, hatch progress, inheritance, groups) | NOT STARTED | upstream Day Care exists; check inheritance rules and UI |
| 10 | Dynamic legendary events (weather, night, cooperative objectives, limited time) | NOT STARTED | first-catch rule exists (unverified) |
| 11 | Living world clock: visual clock vs real-time timers; fix the 2-hour daily reset | NOT STARTED | known mismatch: berries and daily flags reset every 2 real hours while online |
| 12 | Integrate everything into one playable game (3D, battles, sprites, native UI, customization, trading, saves, weather, events, Mega) | NOT STARTED | after phases 1-11 |
| 13 | Full troubleshooting and testing: gameplay, multiplayer (2-4 players, real PCs), graphics, performance, UI and audio, saves, automated regression | NOT STARTED | includes the Route 110 crash after Psychic Edward (U13); real GPU and multi-PC runs need the owner's PC |
| 14 | Fix every discovered problem, with a regression test per repaired bug | NOT STARTED | priorities: crashes, save corruption, duplication, desync, broken progression |
| 15 | Final build, documentation, checkpoint, delivery with exact commit and build location | NOT STARTED | completion claimed only where the criteria in the brief are met |

Cross-cutting (every phase): regression tests where practical; no feature disabled to pass a
test; saves, multiplayer and battles must keep working; PC build evidence recorded here.
