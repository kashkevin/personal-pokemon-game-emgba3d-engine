#!/usr/bin/env python3
"""The fishing rods in real game processes (test builds; Linux and Windows alike): each rod used from the Bag the way a
person does, on Route 109's beach facing the sea, until a Pokemon is on the hook and its battle starts. Everything is read
from the game's own log: PKMN_TEST_REEL=1 answers "Oh! A bite!" in time ("fishing: reel rod R" each round) and logs the
Pokemon hooked ("fishing: rod R hooked species S level L map G.N"); PKMN_TEST_BAG_LOG the rod's count in the Bag.

The lead is a Lileep (Suction Cups): every Good and Super Rod cast bites, an Old Rod cast half of them (the Gen 4+ odds,
--selftest-rods), so the Old Rod's step may end with "Not even a nibble..." and is then cast again, up to 4 times.

  old     START > BAG, RIGHT to KEY ITEMS, the Old Rod > USE; B through the messages: a Pokemon at a level of the Old
          Rod's slots (Route 109: 5-10); the rod still in the Bag
  good    the same with the Good Rod (10-30)
  super   the same with the Super Rod (20-45)

The species is not checked against the map's table: the PC game draws a fished Pokemon from its own fishing pool
(mp_wild_random.c, MPWild_RandomizeSpecies), keeping the slot's level.

    tools/battle_env/rods_check.py --exe ./pokeemerald-mp-bfxtest --save town.sav --out build/rods-check \\
        [--display :99] [--make-save [PATH]]

Exit 0 when every rod gave what it expects; the report is OUT/rods_check.json.
"""
import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mega_check import driver_lines, finish, make_save, numbers, start  # noqa: E402

SPECIES_LILEEP = 345
ROUTE109_WARP = "0:24:0:20:24"   # Route 109's beach, the sea one step south
RODS = (("old", 709, 0), ("good", 710, 1), ("super", 711, 2))   # name, item, the game's rod number

# DOWN faces the sea; START > BAG (under POKeMON: a player with no Pokedex); RIGHT four times from ITEMS to KEY ITEMS;
# the rod (the pocket's only item) > USE. Then B every half second: it takes "Oh! A bite!"'s and "A POKeMON's on the
# hook!"'s waits away (A would cancel the cast while its dots show) and does nothing in the battle's action menu.
KEYS = ["30:DOWN", "90:START", "140:DOWN", "180:A", "320:RIGHT", "380:RIGHT", "440:RIGHT", "500:RIGHT", "620:A",
        "720:A"] + [f"{f}:B" for f in range(820, 2301, 30)]
FRAMES = 2600


def rod_levels(engine, mapname, rod):
    """The lowest and highest level of a rod's slots on a map (src/data/wild_encounters.json; Old 0-1, Good 2-4,
    Super 5-9)."""
    slots = {0: range(0, 2), 1: range(2, 5), 2: range(5, 10)}[rod]
    with open(os.path.join(engine, "src", "data", "wild_encounters.json"), encoding="utf-8") as f:
        data = json.load(f)
    for group in data["wild_encounter_groups"]:
        for encounter in group.get("encounters", []):
            if encounter.get("map") == mapname and "fishing_mons" in encounter:
                mons = encounter["fishing_mons"]["mons"]
                return min(mons[i]["min_level"] for i in slots), max(mons[i]["max_level"] for i in slots)
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", required=True)
    parser.add_argument("--save")
    parser.add_argument("--out", required=True)
    parser.add_argument("--display", default=os.environ.get("DISPLAY"))
    parser.add_argument("--make-save", nargs="?", const="", default=None, metavar="PATH",
                        help="first make the save (the short introduction) at PATH, or at OUT/made.sav")
    parser.add_argument("--rods", default="old,good,super")
    args = parser.parse_args()
    args.exe = os.path.abspath(args.exe)
    os.makedirs(args.out, exist_ok=True)
    if args.make_save is not None:
        args.save = make_save(args, os.path.abspath(args.make_save) if args.make_save
                              else os.path.join(args.out, "made.sav"))
    if not args.save:
        sys.exit("--save or --make-save")
    args.save = os.path.abspath(args.save)
    engine = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    wanted = set(args.rods.split(","))
    steps, problems = [], []

    for name, item, rod in RODS:
        if name not in wanted:
            continue
        low, high = rod_levels(engine, "MAP_ROUTE109", rod)
        for attempt in range(1, 5):
            rundir = os.path.join(args.out, f"{name}-{attempt}")
            env = dict(PKMN_TEST_BATTLE=f"field:{FRAMES}", PKMN_TEST_AUTOPILOT="1", PKMN_TEST_BATTLE_DELAY="100",
                       PKMN_TEST_REEL="1", PKMN_TEST_BAG=f"{item}:1", PKMN_TEST_BAG_RESET="1",
                       PKMN_TEST_BAG_LOG=str(item), PKMN_TEST_PARTY=f"{SPECIES_LILEEP}:20", PKMN_TEST_WARP=ROUTE109_WARP,
                       PKMN_TEST_FIELD_KEYS=",".join(KEYS), PKMN_TEST_MEGA_SHOT_STEM=os.path.join(rundir, "step"),
                       PKMN_TEST_FIELD_SHOTS="700,1300,1900,2500")
            process, output = start(args, rundir, env)
            code = finish(process, output, FRAMES / 60 + 240)
            with open(os.path.join(rundir, "test.log"), encoding="utf-8", errors="replace") as f:
                raw = [line.rstrip("\n") for line in f]
            reels = [line for line in raw if line.startswith("fishing: reel ")]
            hooked = [numbers(line) for line in raw if line.startswith("fishing: rod ")]
            counts = [numbers(line)[2] for line in driver_lines(os.path.join(rundir, "test.log"))
                      if line.startswith("bag field: ")]
            step = dict(rod=name, attempt=attempt, exit=code, reels=len(reels),
                        hooked=dict(rod=hooked[0][0], species=hooked[0][1], level=hooked[0][2]) if hooked else None,
                        rod_counts=counts, slot_levels=[low, high],
                        pictures=sorted(f for f in os.listdir(rundir) if f.endswith(".png")))
            steps.append(step)
            if hooked or name != "old":
                break
        if code != 0:
            problems.append(f"{name}: the game ended with {code}")
        if not hooked:
            problems.append(f"{name}: no Pokemon hooked in {attempt} casts")
        else:
            got = step["hooked"]
            if got["rod"] != rod:
                problems.append(f"{name}: hooked with rod {got['rod']}, not {rod}")
            if not low <= got["level"] <= high:
                problems.append(f"{name}: a level-{got['level']} Pokemon, outside the rod's slots ({low}-{high})")
        if not counts or counts[-1] != 1:
            problems.append(f"{name}: the rod's count in the Bag {counts}, not 1 to the end (a key item is kept)")

    report = dict(steps=steps, problems=problems, passed=not problems)
    with open(os.path.join(args.out, "rods_check.json"), "w") as f:
        json.dump(report, f, indent=2)
    for step in steps:
        print(step["rod"], step["attempt"], "exit", step["exit"], "reels", step["reels"], "hooked", step["hooked"],
              "levels", step["slot_levels"], "rod in the Bag", step["rod_counts"])
    print("PASS" if not problems else "FAIL: " + "; ".join(problems))
    return 0 if not problems else 1


if __name__ == "__main__":
    sys.exit(main())
