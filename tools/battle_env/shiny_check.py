#!/usr/bin/env python3
"""Shiny wild Pokemon are seen before they are talked to (the owner's requests of 2026-10-24), in real game processes
(test builds; the Classic 2D one pictures each case's field, the 3D one is pictured by its window): a shiny overworld
Pokemon is drawn in its shiny colours and twinkles (src/platform/ow_shiny_sparkle.c); a Pokemon that is not shiny never
twinkles; a roamer's and a mass outbreak's battle is shiny exactly when the field drew it shiny. Everything is read from
the game's own log (PKMN_TEST_SPARKLE_LOG, once a second: the twinkles on the field, made so far and hidden only past the
GBA's picture, each wild Pokemon drawn with its picture's shiny bit; PKMN_TEST_OWE_BATTLE: the Pokemon bumped into and its
battle's), and in the 3D build its renderer's (PKMN_DIORAMA_LOG: each field effect drawn over a walker).

  owe            the game's own overworld encounters on Route 101, every other one made shiny (PKMN_TEST_OWE_SHINY=1):
                 shiny ones and others drawn, the shiny ones in shiny colours; twinkles made, never more at once than
                 shiny Pokemon on the field
  owe-none       none forced: no shiny Pokemon (the game's own 1 in 4096 aside), no twinkle
  spawns-charm   a world's shared spawns (PKMN_TEST_SPAWNS: the host's list, by a stand-in host), the player holding the
                 Shiny Charm: one the host rolled shiny, one not, one the Charm makes shiny for this player
                 (MultiplayerSpawns_CharmMakesShiny): the first and the third drawn shiny, the second not; twinkles
  spawns-plain   the same without the Charm: only the first shiny
  spawns-gone    the shiny one alone, then the host lists nothing (PKMN_TEST_SPAWNS_UNTIL): drawn and twinkling, then
                 gone with no twinkle left
  offscreen      a shiny shared spawn ten tiles right of the player, past the GBA's 240x160 picture (the 3D view shows
                 it): twinkles made there, hidden in Classic 2D only for being off its picture; the 3D build draws them
                 over it (its renderer logs a field effect over a walker)
  event          a world's shiny World Event Pokemon (PKMN_TEST_WORLD_EVENT, a stand-in host): drawn shiny, twinkling
  event-plain    the same not shiny: no twinkle
  roamer         a shiny roamer on the map (PKMN_TEST_ROAMER): its overworld Pokemon drawn shiny and twinkling, bumped
                 into: its battle's Pokemon shiny
  roamer-plain   a roamer that is not shiny: drawn plain, its battle not shiny
  outbreak       a mass outbreak on the map (PKMN_TEST_OUTBREAK), every overworld Pokemon made shiny
                 (PKMN_TEST_OWE_SHINY=2): one bumped into is shiny in battle too (the battle once rolled again)
  keep           MS-79, a shiny stays until it is caught or knocked out: the game's own overworld Pokemon on Route 101,
                 every one shiny (each one kept: "owe keep: kept"), the player walks north into Oldale Town (a town: the
                 game takes every overworld Pokemon away) and back: a kept one is put back where it was ("owe keep:
                 back", the same species at the same place) and drawn shiny there
  keep-won       one of them bumped into (PKMN_TEST_OWE_BATTLE=wild) and knocked out (A through the battle): "owe keep:
                 won", never put back
  palette-last   a shiny shared spawn on a crowded map, one sprite palette left free (PKMN_TEST_PALETTE_FREE=1): no
                 twinkle (its palette would take the last one, which then pushes out a Pokemon), one palette still free
  palette-two    two free: twinkles, one palette still free
  tiles-full     a shiny shared spawn twinkling, then the field's sprite tiles all taken (PKMN_TEST_TILES_FULL): the
                 game goes on (no crash screen), the twinkles left out for want of room

    tools/battle_env/shiny_check.py --exe ./pokeemerald-mp-bfxtest --save route101.sav --out build/shiny-check
    tools/battle_env/shiny_check.py --exe ./pokeemerald-mp-diorama-bfxtest ... --cases offscreen,event   (some cases)

Exit 0 when every case gave what it expects; the report is OUT/shiny_check.json.
"""
import argparse
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mega_check import driver_lines, finish, numbers, start  # noqa: E402

ROUTE_101 = "0:16:0:10:11"         # PKMN_TEST_WARP: Route 101 (its grass right of the player)
ROUTE_101_LEFT = "0:16:0:8:11"     # two tiles further left
SPECIES_MUDKIP, SPECIES_ZIGZAGOON, ITEM_SHINY_CHARM = 258, 263, 691
SPECIES_RAIKOU, SPECIES_SEEDOT, SPECIES_LATIAS = 243, 273, 380
OWE_KIND_ROAMER = 2                # battle_env_test.h OWE_KIND_*
PERSONALITY = 0x12345678
CHARM_SPAWN = 4457                 # its hash with PERSONALITY is under MP_SPAWN_CHARM_THRESHOLD (multiplayer_spawns.c)
SPAWNS = f"{SPECIES_ZIGZAGOON}:9:13:1:101:{PERSONALITY};{SPECIES_ZIGZAGOON}:11:13:0:102:{PERSONALITY};" \
         f"{SPECIES_ZIGZAGOON}:10:14:0:{CHARM_SPAWN}:{PERSONALITY}"


def sparkle_seconds(lines):
    """Each second's (live, made, {(local, spawn): shiny}, hidden off the GBA's picture, [the mons' numbers])."""
    seconds = []
    for line in lines:
        if line.startswith("sparkle field: "):
            live, made, hidden, free, skipped = (numbers(line) + [0, 99, 0])[:5]
            seconds.append([live, made, {}, hidden, [], free, skipped])
        elif line.startswith("sparkle mon: ") and seconds:
            mon = numbers(line)
            local, _, shiny, spawn = mon[:4]
            seconds[-1][2][(local, spawn)] = shiny
            seconds[-1][4].append(mon)
    return seconds


def mons(seconds, kind=None, event=None):
    """Every logged mon line (local, gfx, shiny, spawn, x, y, kind, event, offscreen) of that kind / the World Event."""
    return [m for second in seconds for m in second[4]
            if len(m) >= 9 and (kind is None or m[6] == kind) and (event is None or m[7] == event)]


ROUTE_101_MAP, OLDALE = (0, 16), (0, 10)
MOVE_X_SCISSOR = 404
MAP_OFFSET = 7                      # "owe keep" places are the game's own (the map's 7-tile margin added)
# From Route 101's grass (10,11): right to x 15, north past the trees to y 2, left to the gap at x 10, north into Oldale
# Town, a few steps in; then back south onto Route 101. One step a tap (18 frames; a first tap may only turn).
KEEP_WALK_AT = 700                  # the shinies made first
KEEP_PATH = ["RIGHT"] * 6 + ["UP"] * 10 + ["LEFT"] * 6 + ["UP"] * 7 + ["DOWN"] * 8


def keep_steps():
    return [f"{KEEP_WALK_AT + 18 * i}:{key}" for i, key in enumerate(KEEP_PATH)]


KEEP_KEYS = keep_steps()
KEEP_FRAMES = KEEP_WALK_AT + 18 * len(KEEP_PATH) + 900


def keep_line(line):
    """'owe keep: back local L species S x X y Y map G.N' as a dict."""
    what = line.split()[2]
    local, species, x, y, group, num = numbers(line.split(what, 1)[1])[:6]
    return dict(what=what, local=local, species=species, x=x, y=y, map=(group, num))


def hm_place(line):
    """'hm field: ... map G.N at X,Y ...': ((G, N), (X, Y))."""
    m = re.search(r"map (\d+)\.(\d+) at (-?\d+),(-?\d+)", line)
    return (int(m.group(1)), int(m.group(2))), (int(m.group(3)), int(m.group(4)))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", required=True)
    parser.add_argument("--save", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--display", default=os.environ.get("DISPLAY"))
    parser.add_argument("--cases", default="", help="only these cases (comma separated)")
    args = parser.parse_args()
    three_d = "diorama" in os.path.basename(args.exe)
    args.exe, args.save = os.path.abspath(args.exe), os.path.abspath(args.save)
    os.makedirs(args.out, exist_ok=True)
    cases, problems = [], []

    def case(name, frames, extra, shots):
        rundir = os.path.join(args.out, name)
        env = dict(PKMN_TEST_BATTLE=f"field:{frames}", PKMN_TEST_SPARKLE_LOG="1", PKMN_DIORAMA_LOG="1",
                   PKMN_TEST_FREEZE_FIELD="1",
                   PKMN_TEST_AUTOPILOT="1", PKMN_TEST_BATTLE_DELAY="100", PKMN_TEST_WARP=ROUTE_101,
                   PKMN_TEST_PARTY=f"{SPECIES_MUDKIP}:20", PKMN_TEST_FIELD_KEYS="",
                   PKMN_TEST_FIELD_SHOTS=shots, PKMN_TEST_MEGA_SHOT_STEM=os.path.join(rundir, "step"))
        env.update(extra)
        process, output = start(args, rundir, env)
        code = finish(process, output, frames / 60 + 240)
        lines = driver_lines(os.path.join(rundir, "test.log"))
        seconds = sparkle_seconds(lines)
        with open(os.path.join(rundir, "game.log"), encoding="utf-8", errors="replace") as game:
            drawn = [line.strip() for line in game if "Diorama: effect" in line and "over a walker" in line]
        battle = [line for line in lines if line.startswith(("owe bumped: ", "owe battle: "))]
        cases.append(dict(case=name, exit=code, seconds=[[l, m, sorted(d.items()), h, f, k] for l, m, d, h, _, f, k in seconds],
                          effects_3d=len(drawn), battle=battle,
                          notes=[line for line in lines if line.startswith(("cannot", "assert"))],
                          pictures=sorted(f for f in os.listdir(rundir) if f.endswith(".png"))))
        if code != 0:
            problems.append(f"{name}: the game ended with {code}")
        problems.extend(f"{name}: {line}" for line in cases[-1]["notes"])
        if not seconds:
            problems.append(f"{name}: nothing logged")
        case.drawn, case.battle, case.lines = drawn, battle, lines
        return seconds

    def never_more_twinkles_than_shinies(name, seconds):
        for live, _, shinies, _, _, _, _ in seconds:
            if live > sum(shinies.values()):
                problems.append(f"{name}: {live} twinkles on the field with {sum(shinies.values())} shiny Pokemon")
                return

    def battle_shiny(name, want):
        """The bumped Pokemon's field look and its battle's: both `want`."""
        bumped = [numbers(line) for line in case.battle if line.startswith("owe bumped: ")]
        fought = [numbers(line) for line in case.battle if line.startswith("owe battle: ")]
        if not bumped or not fought:
            problems.append(f"{name}: no overworld Pokemon of that kind bumped into and fought ({case.battle})")
        elif bumped[0][-1] != want or fought[0][-1] != want:
            problems.append(f"{name}: drawn shiny {bumped[0][-1]}, its battle shiny {fought[0][-1]}: not both {want}")

    def want(name):
        return not args.cases or name in args.cases.split(",")

    # owe: the game's own, every other one shiny
    if want("owe"):
        burst = ",".join(str(1500 + 3 * i) for i in range(16))      # a twinkle's whole life, every 3 frames
        seconds = case("owe", 1600, dict(PKMN_TEST_OWE_SHINY="1"), burst)
        seen = {shiny for _, _, shinies, _, _, _, _ in seconds for shiny in shinies.values()}   # the non-shiny ones leave sooner
        if seen != {0, 1}:
            problems.append(f"owe: the wild Pokemon drawn were shiny {sorted(seen)}: not shiny ones and others")
        if not seconds or seconds[-1][1] == 0:
            problems.append("owe: no twinkle made")
        never_more_twinkles_than_shinies("owe", seconds)
    # owe-none: nothing forced
    if want("owe-none"):
        seconds = case("owe-none", 1600, {}, "1500")
        if any(any(shinies.values()) for _, _, shinies, _, _, _, _ in seconds):
            problems.append("owe-none: a shiny Pokemon drawn (the game's own 1 in 4096 roll? run it again)")
        elif seconds and seconds[-1][1] != 0:
            problems.append(f"owe-none: {seconds[-1][1]} twinkles made with no shiny Pokemon")
    # spawns with and without the Charm
    burst = ",".join(str(400 + 3 * i) for i in range(16))
    for name, bag, expect in (("spawns-charm", f"{ITEM_SHINY_CHARM}:1", {224: 1, 225: 0, 226: 1}),
                              ("spawns-plain", "", {224: 1, 225: 0, 226: 0})):
        if not want(name):
            continue
        seconds = case(name, 500, dict(PKMN_TEST_SPAWNS=SPAWNS, PKMN_TEST_BAG=bag), burst)
        got = {local: shiny for (local, spawn), shiny in (seconds[-1][2].items() if seconds else ()) if spawn}
        if got != expect:
            problems.append(f"{name}: the shared spawns drawn shiny {got}, expected {expect}")
        if not seconds or seconds[-1][1] == 0:
            problems.append(f"{name}: no twinkle made")
        never_more_twinkles_than_shinies(name, seconds)
    # spawns-gone: twinkling, then gone with its Pokemon
    if want("spawns-gone"):
        seconds = case("spawns-gone", 600, dict(PKMN_TEST_SPAWNS=SPAWNS.split(";")[0], PKMN_TEST_SPAWNS_UNTIL="300"),
                       "250,550")
        # The seconds that drew it need not be one run: the stand-in host lists the spawn on the save's map too, so a
        # second during the test's warp to Route 101 can draw none. "After" starts past the last second that drew it.
        drawn_at = [i for i, s in enumerate(seconds) if s[2]]
        before = [seconds[i] for i in drawn_at]
        after = seconds[drawn_at[-1] + 1:] if drawn_at else seconds
        if not before or before[-1][1] == 0:
            problems.append("spawns-gone: the shiny spawn made no twinkle while it was there")
        if not after or after[-1][0] != 0 or after[-1][2] or (len(after) > 1 and after[-1][1] != after[0][1]):
            problems.append(f"spawns-gone: after the host dropped it {after[-1:]}: not gone with no twinkle left and none made")
    # offscreen: a shiny spawn past the GBA's picture (x 18: the player stands at 8; OnScreen keeps it up to 10 tiles right)
    if want("offscreen"):
        seconds = case("offscreen", 500, dict(PKMN_TEST_SPAWNS=f"{SPECIES_ZIGZAGOON}:18:11:1:101:{PERSONALITY}",
                                              PKMN_TEST_WARP=ROUTE_101_LEFT), "300,400")
        far = mons(seconds)
        if not far or not all(m[2] == 1 and m[8] == 1 for m in far):
            problems.append(f"offscreen: the spawn {far[-1:]}: not drawn shiny past the GBA's picture")
        if not seconds or seconds[-1][1] == 0 or not any(second[3] for second in seconds):
            problems.append(f"offscreen: no twinkle made for it past the GBA's picture {[s[:2] + s[3:4] for s in seconds][-3:]}")
        if three_d and not case.drawn:
            problems.append("offscreen: the 3D view drew no twinkle over it (no 'effect over a walker')")
    # World Event Pokemon, shiny and not
    for name, flags, expect in (("event", 1, 1), ("event-plain", 0, 0)):
        if not want(name):
            continue
        seconds = case(name, 500, dict(PKMN_TEST_WORLD_EVENT=f"{SPECIES_RAIKOU}:11:13:{flags}"), "300,400")
        event = mons(seconds, event=1)
        if not event or event[-1][2] != expect:
            problems.append(f"{name}: the World Event Pokemon {event[-1:]}: not drawn with shiny {expect}")
        made = seconds[-1][1] if seconds else 0
        if (made > 0) != bool(expect):
            problems.append(f"{name}: {made} twinkles made")
    # roamers, shiny and not: drawn as they are, and fought as drawn
    for name, shiny in (("roamer", 1), ("roamer-plain", 0)):
        if not want(name):
            continue
        seconds = case(name, 6000, dict(PKMN_TEST_ROAMER=f"{SPECIES_LATIAS}:40:{shiny}", PKMN_TEST_OWE_BATTLE="roamer"),
                       "")
        roamer = mons(seconds, kind=OWE_KIND_ROAMER)
        if roamer and any(m[2] != shiny for m in roamer):
            problems.append(f"{name}: the roamer drawn shiny {sorted({m[2] for m in roamer})}, it is {shiny}")
        battle_shiny(name, shiny)
        if shiny and roamer and seconds and seconds[-1][1] == 0:
            problems.append(f"{name}: the shiny roamer made no twinkle")
    # a crowded map: one palette free - no twinkle; two - twinkles; never the last palette taken
    for name, free, twinkles in (("palette-last", 1, False), ("palette-two", 2, True)):
        if not want(name):
            continue
        seconds = case(name, 600, dict(PKMN_TEST_SPAWNS=SPAWNS.split(";")[0], PKMN_TEST_PALETTE_FREE=str(free)), "400")
        made = seconds[-1][1] if seconds else 0
        if (made > 0) != twinkles:
            problems.append(f"{name}: {made} twinkles made with {free} palette(s) free")
        if any(second[5] < 1 for second in seconds[2:]):
            problems.append(f"{name}: the last free palette taken {[second[5] for second in seconds]}")
    # the field's sprite tiles all taken while a shiny twinkles: no crash, no twinkle without room
    if want("tiles-full"):
        seconds = case("tiles-full", 900, dict(PKMN_TEST_SPAWNS=SPAWNS.split(";")[0], PKMN_TEST_TILES_FULL="300"), "")
        full = next((i for i, line in enumerate(case.lines) if line.startswith("sprite tiles full")), None)
        if full is None:
            problems.append("tiles-full: the sprite tiles were not filled")
        elif len(seconds) < 10 or seconds[-1][6] == 0:
            problems.append(f"tiles-full: the field did not go on, or no twinkle was left out for want of room "
                            f"{[s[:2] + s[6:7] for s in seconds][-6:]}")
    # a mass outbreak, every overworld Pokemon shiny: fought shiny
    if want("outbreak"):
        case("outbreak", 4000, dict(PKMN_TEST_OUTBREAK=f"{SPECIES_SEEDOT}:5", PKMN_TEST_OWE_SHINY="2",
                                    PKMN_TEST_OWE_BATTLE="outbreak"), "")
        battle_shiny("outbreak", 1)

    # MS-79: kept shinies come back after the player leaves the map, until knocked out
    if want("keep"):
        seconds = case("keep", KEEP_FRAMES, dict(PKMN_TEST_OWE_SHINY="2", PKMN_TEST_HM_LOG="1",
                                                 PKMN_TEST_FIELD_KEYS=",".join(KEEP_KEYS)), "")
        kept = [keep_line(line) for line in case.lines if line.startswith("owe keep: kept")]
        places = [hm_place(line) for line in case.lines if line.startswith("hm field: ")]
        oldale = next((i for i, line in enumerate(case.lines) if line.startswith("hm field: ")
                       and hm_place(line)[0] == OLDALE), None)
        back = [keep_line(line) for line in case.lines[oldale or len(case.lines):] if line.startswith("owe keep: back")]
        kept_route = {(k["species"], k["x"], k["y"]) for k in kept if k["map"] == ROUTE_101_MAP}
        if not kept_route:
            problems.append("keep: no shiny overworld Pokemon kept on Route 101")
        if oldale is None:
            problems.append(f"keep: the player never reached Oldale Town (places {sorted(set(p[0] for p in places))})")
        elif not any(b["map"] == ROUTE_101_MAP for b in back):
            problems.append("keep: no kept shiny put back on Route 101 after Oldale Town")
        else:
            first = next(b for b in back if b["map"] == ROUTE_101_MAP)
            if not any(first["species"] == k[0] for k in kept_route):
                problems.append(f"keep: the one put back {first} is none of the kept ones {sorted(kept_route)}")
            place = [first["x"] - MAP_OFFSET, first["y"] - MAP_OFFSET]     # the sparkle log's places are the map's own
            drawn = [m for m in mons(seconds) if m[1] & 0xFFF == first["species"] and m[4:6] == place]
            if not drawn or not all(m[2] == 1 for m in drawn):
                problems.append(f"keep: the one put back {first} not drawn shiny at its place ({drawn[-1:]})")
    if want("keep-won"):
        # Level 100 with X-Scissor first (no type is immune to Bug, and the level gains nothing to ask about)
        case("keep-won", 6000, dict(PKMN_TEST_OWE_SHINY="2", PKMN_TEST_OWE_BATTLE="wild", PKMN_TEST_STRONG="1",
                                    PKMN_TEST_FIRST_MOVE=str(MOVE_X_SCISSOR),
                                    PKMN_TEST_FIELD_KEYS=",".join(f"{f}:A" for f in range(900, 5700, 30))),
             "900,1500,2400,3600,5000")
        battle_shiny("keep-won", 1)
        won = [keep_line(line) for line in case.lines if line.startswith(("owe keep: won", "owe keep: caught"))]
        if not won:
            problems.append(f"keep-won: the shiny knocked out or caught was not let go ({case.battle})")
        else:
            later = case.lines[next(i for i, line in enumerate(case.lines) if line.startswith(("owe keep: won",
                                                                                               "owe keep: caught"))):]
            again = [keep_line(line) for line in later if line.startswith("owe keep: back")]
            if any(b["species"] == won[0]["species"] and (b["x"], b["y"]) == (won[0]["x"], won[0]["y"]) for b in again):
                problems.append(f"keep-won: the one knocked out {won[0]} was put back")

    report = dict(cases=cases, problems=problems, passed=not problems)
    with open(os.path.join(args.out, "shiny_check.json"), "w") as out:
        json.dump(report, out, indent=1)
    print(json.dumps(dict(passed=not problems, problems=problems)))
    return 0 if not problems else 1


if __name__ == "__main__":
    sys.exit(main())
