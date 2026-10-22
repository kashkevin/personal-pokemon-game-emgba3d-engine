#!/usr/bin/env python3
"""Evolution in real game processes (test builds; Linux and Windows alike): a level-up evolution from a Rare Candy, the
same evolution stopped with B, an evolution stone, and a level-up evolution after a won battle, each saved and loaded.
Everything is read from the game's own log (PKMN_TEST_BREED_LOG: every party Pokemon's species, level, IVs, nature,
Ability slot, ball and moves); the field keys are the presses a person makes (game frames at fast text, as the pictures
each step takes showed them). The species learn no move at these levels with the game's level-up sets (Generation 6,
include/config/general.h GEN_LATEST), so no move question comes in between.

  1. candy        a Zigzagoon at level 19 and one Rare Candy: BAG > Rare Candy > USE > the lead: "Zigzagoon was
                  elevated to Lv. 20.", its stats, "What? Zigzagoon is evolving!", the evolution,
                  "Congratulations! Your Zigzagoon evolved into Linoone!"; back on the field; SAVE
  2. candy-load   that save loaded: a level-20 Linoone with the Zigzagoon's IVs, nature, ball and moves
  3. cancel       as 1, B pressed during the evolution: it stops ("Huh? ... stopped evolving!"): a level-20 Zigzagoon;
                  SAVE
  4. cancel-load  that save loaded: still a level-20 Zigzagoon
  5. stone        a Vulpix at level 10 and a Fire Stone: BAG > Fire Stone > USE > the lead: the evolution at once,
                  "Congratulations! Your Vulpix evolved into Ninetales!"; SAVE
  6. stone-load   that save loaded: a level-10 Ninetales
  7. battle       a Zigzagoon at level 19, one experience point short of 20, Tackle first, against a wild level-2
                  Magikarp: A through the battle, won; "... grew to Lv. 20!", the evolution after the battle, the
                  field; SAVE
  8. battle-load  that save loaded: a level-20 Linoone

    tools/battle_env/evolve_check.py --exe ./pokeemerald-mp-bfxtest --save town.sav --out build/evolve-check \\
        [--display :99] [--make-save [PATH]]

Exit 0 when every step gave what it expects; the report is OUT/evolve_check.json.
"""
import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mega_check import driver_lines, finish, make_save, numbers, save_keys, start  # noqa: E402

SPECIES_VULPIX, SPECIES_NINETALES, SPECIES_MAGIKARP = 37, 38, 129
SPECIES_ZIGZAGOON, SPECIES_LINOONE = 263, 264
ITEM_RARE_CANDY, ITEM_FIRE_STONE = 102, 211
MOVE_TACKLE = 33

# The start menu of a player with a Pokemon and no Pokedex: POKeMON, BAG, ...; the Bag opens on its ITEMS pocket, the
# item first; its menu USE, GIVE, TOSS, CANCEL; "Use on which POKeMON?" with the lead first (train_check.py's).
USE_FIRST = ["30:START", "80:DOWN", "120:A", "260:A", "320:A", "420:A"]
# The Rare Candy: "... was elevated to Lv. 20." > the stat gains > the stats; the evolution scene starts by itself
# (frame ~880) and plays until "Congratulations! ..." waits for a button (~2450); then the Bag (~2560): B, the start
# menu: B.
CANDY = USE_FIRST + ["600:A", "720:A", "840:A", "2600:A", "2800:B", "2960:B"]
CANDY_SAVE_AT = 3100
# B during the evolution's animation (~1100 to ~2000) stops it; the later presses take the Bag and the start menu away.
CANCEL = USE_FIRST + ["600:A", "720:A", "840:A"] + [f"{frame}:B" for frame in range(1150, 1950, 40)]
CANCEL_SAVE_AT = 2100
# The stone: the evolution at once (~460) until "Congratulations! ..." waits (~2000); the Bag; the start menu.
STONE = USE_FIRST + ["2200:A", "2400:B", "2560:B"]
STONE_SAVE_AT = 2700
# After the battle (PKMN_TEST_FIELD_AFTER: these frames count from the field's return) the start menu's cursor is on
# its first row: mega_check.py's save_keys.
BATTLE_SAVE_AT = 300


def lead_states(lines):
    """Every state of the party's first Pokemon the log tells, in order (PKMN_TEST_BREED_LOG's "breed party 0" lines:
    breed_check.py's fields). The first is the save's (or the test party as it came), the last the end's."""
    states = []
    for line in lines:
        if line.startswith("breed party 0: ") and not line.endswith(": none"):
            v = numbers(line.split(":", 1)[1])
            states.append(dict(species=v[0], egg=v[1], level=v[2], ivs=v[3:9], nature=v[9], num=v[10], ball=v[11],
                               moves=v[12:16]))
    return states


def bag_save_keys(start):
    """START > SAVE > YES from the start menu's cursor where the Bag left it (BAG, two rows above SAVE: train_check.py's),
    YES again over the saved file; the closing message goes by itself; a B for safety."""
    return [f"{start + f}:{k}" for f, k in ((0, "START"), (50, "DOWN"), (80, "DOWN"), (160, "A"), (260, "A"), (380, "A"),
                                            (700, "B"))]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", required=True)
    parser.add_argument("--save")
    parser.add_argument("--out", required=True)
    parser.add_argument("--display", default=os.environ.get("DISPLAY"))
    parser.add_argument("--make-save", nargs="?", const="", default=None, metavar="PATH",
                        help="first make the save (the short introduction) at PATH, or at OUT/made.sav")
    args = parser.parse_args()
    args.exe = os.path.abspath(args.exe)
    os.makedirs(args.out, exist_ok=True)
    if args.make_save is not None:
        args.save = make_save(args, os.path.abspath(args.make_save) if args.make_save
                              else os.path.join(args.out, "made.sav"))
    if not args.save:
        sys.exit("--save or --make-save")
    args.save = os.path.abspath(args.save)
    steps, problems = [], []

    def step(name, save, frames, keys, extra=None, shots=""):
        rundir = os.path.join(args.out, name)
        saved, args.save = args.save, save
        env = dict(PKMN_TEST_BATTLE=f"field:{frames}", PKMN_TEST_BREED_LOG="1", PKMN_TEST_FREEZE_FIELD="1",
                   PKMN_TEST_AUTOPILOT="1", PKMN_TEST_BATTLE_DELAY="100", PKMN_TEST_FIELD_KEYS=",".join(keys),
                   PKMN_TEST_MEGA_SHOT_STEM=os.path.join(rundir, "step"))
        env.update(extra or {})                   # a battle instead of the field keys alone
        if shots:
            env["PKMN_TEST_FIELD_SHOTS"] = shots
        try:
            process, output = start(args, rundir, env)
            code = finish(process, output, frames / 60 + 240)
        finally:
            args.save = saved
        lines = driver_lines(os.path.join(rundir, "test.log"))
        states = lead_states(lines)
        first, last = (states[0], states[-1]) if states else (None, None)
        steps.append(dict(step=name, exit=code, before=first, after=last, states=len(states),
                          battles=[line for line in lines if line.startswith("battle over")],
                          notes=[line for line in lines if line.startswith(("cannot", "assert"))],
                          pictures=sorted(f for f in os.listdir(rundir) if f.endswith(".png"))))
        if code != 0:
            problems.append(f"{name}: the game ended with {code}")
        problems.extend(f"{name}: {line}" for line in steps[-1]["notes"])
        return rundir, last, first

    def check(name, got, **want):
        if got is None:
            problems.append(f"{name}: no Pokemon logged")
            return
        wrong = {key: got[key] for key, value in want.items() if got[key] != value}
        if wrong:
            problems.append(f"{name}: {wrong}, expected {dict((k, want[k]) for k in wrong)}")

    def kept(name, got, original):
        """What an evolution keeps: the IVs, nature, Ability slot, ball and moves of the Pokemon before it."""
        if got is not None and original is not None:
            check(name, got, **{k: original[k] for k in ("ivs", "nature", "num", "ball", "moves")})

    zigzagoon = dict(PKMN_TEST_PARTY=f"{SPECIES_ZIGZAGOON}:19", PKMN_TEST_BAG=f"{ITEM_RARE_CANDY}:1")

    # 1-2. The Rare Candy's level-up evolution, saved and loaded
    candy_dir, after, before = step("candy", args.save, CANDY_SAVE_AT + 900, CANDY + bag_save_keys(CANDY_SAVE_AT),
                                    zigzagoon, shots="500,880,1500,2450,2650,2900,3200,3700")
    check("candy (before)", before, species=SPECIES_ZIGZAGOON, level=19)
    check("candy", after, species=SPECIES_LINOONE, level=20)
    kept("candy", after, before)
    _, loaded, _ = step("candy-load", os.path.join(candy_dir, "test.sav"), 60, [])
    check("candy-load", loaded, species=SPECIES_LINOONE, level=20)
    kept("candy-load", loaded, after)

    # 3-4. The same evolution stopped with B
    cancel_dir, after, before = step("cancel", args.save, CANCEL_SAVE_AT + 900, CANCEL + bag_save_keys(CANCEL_SAVE_AT),
                                     zigzagoon, shots="1150,1400,1700,2050,2300,2700")
    check("cancel (before)", before, species=SPECIES_ZIGZAGOON, level=19)
    check("cancel", after, species=SPECIES_ZIGZAGOON, level=20)
    kept("cancel", after, before)
    _, loaded, _ = step("cancel-load", os.path.join(cancel_dir, "test.sav"), 60, [])
    check("cancel-load", loaded, species=SPECIES_ZIGZAGOON, level=20)

    # 5-6. The Fire Stone
    stone_dir, after, before = step("stone", args.save, STONE_SAVE_AT + 900, STONE + bag_save_keys(STONE_SAVE_AT),
                                    dict(PKMN_TEST_PARTY=f"{SPECIES_VULPIX}:10", PKMN_TEST_BAG=f"{ITEM_FIRE_STONE}:1"),
                                    shots="480,1200,2000,2300,2500,2900,3300")
    check("stone (before)", before, species=SPECIES_VULPIX, level=10)
    check("stone", after, species=SPECIES_NINETALES, level=10)
    kept("stone", after, before)
    _, loaded, _ = step("stone-load", os.path.join(stone_dir, "test.sav"), 60, [])
    check("stone-load", loaded, species=SPECIES_NINETALES, level=10)

    # 7-8. Experience from a won battle: the level-up evolution after it
    battle_dir, after, before = step(
        "battle", args.save, BATTLE_SAVE_AT + 900, save_keys(BATTLE_SAVE_AT),
        dict(PKMN_TEST_BATTLE=f"wild:{SPECIES_MAGIKARP}:2", PKMN_TEST_PARTY=f"{SPECIES_ZIGZAGOON}:19:ev{MOVE_TACKLE}",
             PKMN_TEST_FIGHT="120", PKMN_TEST_FIELD_AFTER=str(BATTLE_SAVE_AT + 900)),
        shots="100,400,600,800")
    check("battle (before)", before, species=SPECIES_ZIGZAGOON, level=19)
    check("battle", after, species=SPECIES_LINOONE, level=20)
    kept("battle", after, before)
    if not any("outcome 1 (won)" in line for line in steps[-1]["battles"]):
        problems.append(f"battle: the battle was not won: {steps[-1]['battles']}")
    _, loaded, _ = step("battle-load", os.path.join(battle_dir, "test.sav"), 60, [])
    check("battle-load", loaded, species=SPECIES_LINOONE, level=20)

    report = dict(steps=steps, problems=problems, passed=not problems)
    with open(os.path.join(args.out, "evolve_check.json"), "w") as out:
        json.dump(report, out, indent=1)
    print(json.dumps(dict(passed=not problems, problems=problems)))
    return 0 if not problems else 1


if __name__ == "__main__":
    sys.exit(main())
