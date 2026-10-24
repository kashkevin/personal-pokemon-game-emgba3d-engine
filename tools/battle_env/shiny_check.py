#!/usr/bin/env python3
"""Shiny wild Pokemon are seen before they are talked to (the owner's requests of 2026-10-24), in real game processes
(test builds; the Classic 2D one pictures each case's field, the 3D one is pictured by its window): a shiny overworld
Pokemon is drawn in its shiny colours and twinkles (src/platform/ow_shiny_sparkle.c); a Pokemon that is not shiny never
twinkles. Everything is read from the game's own log (PKMN_TEST_SPARKLE_LOG, once a second: the twinkles on the field
and made so far, each wild Pokemon drawn with its picture's shiny bit).

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

    tools/battle_env/shiny_check.py --exe ./pokeemerald-mp-bfxtest --save route101.sav --out build/shiny-check

Exit 0 when every case gave what it expects; the report is OUT/shiny_check.json.
"""
import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mega_check import driver_lines, finish, numbers, start  # noqa: E402

ROUTE_101 = "0:16:0:10:11"         # PKMN_TEST_WARP: Route 101 (its grass right of the player)
SPECIES_MUDKIP, SPECIES_ZIGZAGOON, ITEM_SHINY_CHARM = 258, 263, 691
PERSONALITY = 0x12345678
CHARM_SPAWN = 4457                 # its hash with PERSONALITY is under MP_SPAWN_CHARM_THRESHOLD (multiplayer_spawns.c)
SPAWNS = f"{SPECIES_ZIGZAGOON}:9:13:1:101:{PERSONALITY};{SPECIES_ZIGZAGOON}:11:13:0:102:{PERSONALITY};" \
         f"{SPECIES_ZIGZAGOON}:10:14:0:{CHARM_SPAWN}:{PERSONALITY}"


def sparkle_seconds(lines):
    """Each second's (live, made, {(local, spawn): shiny})."""
    seconds = []
    for line in lines:
        if line.startswith("sparkle field: "):
            live, made = numbers(line)
            seconds.append([live, made, {}])
        elif line.startswith("sparkle mon: ") and seconds:
            local, _, shiny, spawn = numbers(line)[:4]
            seconds[-1][2][(local, spawn)] = shiny
    return seconds


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", required=True)
    parser.add_argument("--save", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--display", default=os.environ.get("DISPLAY"))
    args = parser.parse_args()
    args.exe, args.save = os.path.abspath(args.exe), os.path.abspath(args.save)
    os.makedirs(args.out, exist_ok=True)
    cases, problems = [], []

    def case(name, frames, extra, shots):
        rundir = os.path.join(args.out, name)
        env = dict(PKMN_TEST_BATTLE=f"field:{frames}", PKMN_TEST_SPARKLE_LOG="1", PKMN_TEST_FREEZE_FIELD="1",
                   PKMN_TEST_AUTOPILOT="1", PKMN_TEST_BATTLE_DELAY="100", PKMN_TEST_WARP=ROUTE_101,
                   PKMN_TEST_PARTY=f"{SPECIES_MUDKIP}:20", PKMN_TEST_FIELD_KEYS="",
                   PKMN_TEST_FIELD_SHOTS=shots, PKMN_TEST_MEGA_SHOT_STEM=os.path.join(rundir, "step"))
        env.update(extra)
        process, output = start(args, rundir, env)
        code = finish(process, output, frames / 60 + 240)
        lines = driver_lines(os.path.join(rundir, "test.log"))
        seconds = sparkle_seconds(lines)
        cases.append(dict(case=name, exit=code, seconds=[[l, m, sorted(d.items())] for l, m, d in seconds],
                          notes=[line for line in lines if line.startswith(("cannot", "assert"))],
                          pictures=sorted(f for f in os.listdir(rundir) if f.endswith(".png"))))
        if code != 0:
            problems.append(f"{name}: the game ended with {code}")
        problems.extend(f"{name}: {line}" for line in cases[-1]["notes"])
        if not seconds:
            problems.append(f"{name}: nothing logged")
        return seconds

    def never_more_twinkles_than_shinies(name, seconds):
        for live, _, mons in seconds:
            if live > sum(mons.values()):
                problems.append(f"{name}: {live} twinkles on the field with {sum(mons.values())} shiny Pokemon")
                return

    burst = ",".join(str(1500 + 3 * i) for i in range(16))      # a twinkle's whole life, every 3 frames
    # owe: the game's own, every other one shiny
    seconds = case("owe", 1600, dict(PKMN_TEST_OWE_SHINY="1"), burst)
    seen = {shiny for _, _, mons in seconds for shiny in mons.values()}   # the non-shiny ones leave sooner
    if seen != {0, 1}:
        problems.append(f"owe: the wild Pokemon drawn were shiny {sorted(seen)}: not shiny ones and others")
    if not seconds or seconds[-1][1] == 0:
        problems.append("owe: no twinkle made")
    never_more_twinkles_than_shinies("owe", seconds)
    # owe-none: nothing forced
    seconds = case("owe-none", 1600, {}, "1500")
    if any(any(mons.values()) for _, _, mons in seconds):
        problems.append("owe-none: a shiny Pokemon drawn (the game's own 1 in 4096 roll? run it again)")
    elif seconds and seconds[-1][1] != 0:
        problems.append(f"owe-none: {seconds[-1][1]} twinkles made with no shiny Pokemon")
    # spawns with and without the Charm
    burst = ",".join(str(400 + 3 * i) for i in range(16))
    for name, bag, want in (("spawns-charm", f"{ITEM_SHINY_CHARM}:1", {224: 1, 225: 0, 226: 1}),
                            ("spawns-plain", "", {224: 1, 225: 0, 226: 0})):
        seconds = case(name, 500, dict(PKMN_TEST_SPAWNS=SPAWNS, PKMN_TEST_BAG=bag), burst)
        got = {local: shiny for (local, spawn), shiny in (seconds[-1][2].items() if seconds else ()) if spawn}
        if got != want:
            problems.append(f"{name}: the shared spawns drawn shiny {got}, expected {want}")
        if not seconds or seconds[-1][1] == 0:
            problems.append(f"{name}: no twinkle made")
        never_more_twinkles_than_shinies(name, seconds)
    # spawns-gone: twinkling, then gone with its Pokemon
    seconds = case("spawns-gone", 600, dict(PKMN_TEST_SPAWNS=SPAWNS.split(";")[0], PKMN_TEST_SPAWNS_UNTIL="300"),
                   "250,550")
    before = [s for s in seconds if s[2]]
    after = seconds[len(before):]
    if not before or before[-1][1] == 0:
        problems.append("spawns-gone: the shiny spawn made no twinkle while it was there")
    if not after or after[-1][0] != 0 or after[-1][2] or (len(after) > 1 and after[-1][1] != after[0][1]):
        problems.append(f"spawns-gone: after the host dropped it {after[-1:]}: not gone with no twinkle left and none made")

    report = dict(cases=cases, problems=problems, passed=not problems)
    with open(os.path.join(args.out, "shiny_check.json"), "w") as out:
        json.dump(report, out, indent=1)
    print(json.dumps(dict(passed=not problems, problems=problems)))
    return 0 if not problems else 1


if __name__ == "__main__":
    sys.exit(main())
