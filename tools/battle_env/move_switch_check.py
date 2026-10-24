#!/usr/bin/env python3
"""Move switching as in Legends Z-A (the owner's request of 2026-10-24) in real game processes (test builds; Linux and
Windows alike): MOVES in the party menu opens the move relearner for that Pokemon, a move taken has full PP, no TM is used
up, the relearner comes back to the party menu, and the moveset is saved like any party change. Everything is read from
the game's own log (PKMN_TEST_MOVE_LOG: the lead's moves and PP whenever they change; PKMN_TEST_BAG_LOG: a TM's count);
the field keys are the presses a person makes (game frames at fast text, as the pictures each step takes showed them).

The lead is a level-20 Mudkip (it knows Mud-Slap, Foresight, Bide and Mud Sport; Tackle, Growl and Water Gun are
level-up moves it learned before).

  1. level-up   START > POKeMON > Mudkip > MOVES (under SUMMARY), Tackle, "Teach Tackle?" YES, "Should a move be deleted
                and replaced with Tackle?" YES, Mud-Slap forgotten in the summary's move list; Tackle at 35/35. Out of the
                relearner (B, YES): the party menu again on Mudkip; out of the menus, SAVE
  2. load       that save loaded: Tackle at 35/35 in the first slot
  3. tm         the Bag has TM03 (Water Pulse): MOVES, SELECT to the egg moves, SELECT to the TM moves (Water Pulse),
                taught over the first move: Water Pulse at 20/20, and the Bag still has its TM03

    tools/battle_env/move_switch_check.py --exe ./pokeemerald-mp-bfxtest --save town.sav --out build/move-switch-check

Exit 0 when every step gave what it expects; the report is OUT/move_switch_check.json.
"""
import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mega_check import driver_lines, finish, numbers, save_keys, start  # noqa: E402

SPECIES_MUDKIP = 258
ITEM_TM03 = 584              # Water Pulse (include/constants/items.h)
MOVE_TACKLE, MOVE_MUD_SLAP, MOVE_WATER_PULSE = 33, 189, 352
TACKLE_PP, WATER_PULSE_PP = 35, 20

# START, POKeMON, Mudkip, MOVES (one down from SUMMARY), A: the relearner with the level-up moves (Tackle first)
OPEN = ["30:START", "80:A", "200:A", "280:DOWN", "340:A"]
# "Teach Tackle?" YES, "... wants to learn the move Tackle", "However, ... already knows four moves.", "Should a move be
# deleted and replaced with Tackle?" YES, "Which move should be forgotten?" (the summary's list: the first move), "1, 2,
# and... Poof!", "... forgot how to use Mud-Slap.", "And...", "... learned Tackle!"
TEACH = [f"{f}:A" for f in range(460, 1561, 100)]
# B: "Stop trying to learn new moves for Mudkip?" YES: the party menu on Mudkip; B, B, B: out of the menus
LEAVE = ["1700:B", "1800:A", "2050:B", "2150:B", "2250:B"]
SAVE_AT = 2400
# SELECT: the egg moves; SELECT: the TM moves (Water Pulse alone); then as TEACH
TM = ["480:SELECT", "560:SELECT"] + [f"{f}:A" for f in range(640, 1641, 100)]


def move_lines(lines):
    """Each "move field" line's (species, [(move, pp, max pp) x 4])."""
    out = []
    for line in lines:
        if line.startswith("move field: "):
            n = numbers(line)
            out.append((n[0], [tuple(n[1 + i * 3:4 + i * 3]) for i in range(4)]))
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", required=True)
    parser.add_argument("--save", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--display", default=os.environ.get("DISPLAY"))
    args = parser.parse_args()
    args.exe, args.save = os.path.abspath(args.exe), os.path.abspath(args.save)
    os.makedirs(args.out, exist_ok=True)
    steps, problems = [], []

    def step(name, save, frames, keys, extra=None, shots=""):
        rundir = os.path.join(args.out, name)
        saved, args.save = args.save, save
        env = dict(PKMN_TEST_BATTLE=f"field:{frames}", PKMN_TEST_MOVE_LOG="1", PKMN_TEST_BAG_LOG=str(ITEM_TM03),
                   PKMN_TEST_FREEZE_FIELD="1", PKMN_TEST_AUTOPILOT="1", PKMN_TEST_BATTLE_DELAY="100",
                   PKMN_TEST_FIELD_KEYS=",".join(keys), PKMN_TEST_MEGA_SHOT_STEM=os.path.join(rundir, "step"))
        env.update(extra or {})
        if shots:
            env["PKMN_TEST_FIELD_SHOTS"] = shots
        try:
            process, output = start(args, rundir, env)
            code = finish(process, output, frames / 60 + 240)
        finally:
            args.save = saved
        lines = driver_lines(os.path.join(rundir, "test.log"))
        begun = next((i for i, line in enumerate(lines) if line.startswith("field keys for ")), 0)
        moves = move_lines(lines[begun:]) if keys else move_lines(lines)   # a load: what the save holds
        tms = [numbers(line)[2] for line in lines[begun:] if line.startswith("bag field: ")]
        steps.append(dict(step=name, exit=code, moves=moves, tm03=tms,
                          notes=[line for line in lines if line.startswith(("cannot", "assert"))],
                          pictures=sorted(f for f in os.listdir(rundir) if f.endswith(".png"))))
        if code != 0:
            problems.append(f"{name}: the game ended with {code}")
        problems.extend(f"{name}: {line}" for line in steps[-1]["notes"])
        return rundir, moves, tms

    party = dict(PKMN_TEST_PARTY=f"{SPECIES_MUDKIP}:20")
    # 1. a level-up move switched in, saved
    rundir, moves, _ = step("level-up", args.save, SAVE_AT + 900, OPEN + TEACH + LEAVE + save_keys(SAVE_AT), party,
                            shots="300,420,560,960,1100,1460,1560,1900,2000,2600")
    if not moves or moves[0][1][0][0] != MOVE_MUD_SLAP:
        problems.append(f"level-up: the lead's moves before the keys {moves[:1]}, not Mud-Slap first")
    if not moves or moves[-1][1][0] != (MOVE_TACKLE, TACKLE_PP, TACKLE_PP):
        problems.append(f"level-up: after the keys {moves[-1:]}, not Tackle at {TACKLE_PP}/{TACKLE_PP} in Mud-Slap's place")
    # 2. that save loaded
    _, moves, _ = step("load", os.path.join(rundir, "test.sav"), 60, [])
    if not moves or moves[-1][1][0] != (MOVE_TACKLE, TACKLE_PP, TACKLE_PP):
        problems.append(f"load: the saved lead's moves {moves[-1:]}, not Tackle at {TACKLE_PP}/{TACKLE_PP} first")
    # 3. a TM's move from the Bag: taught, the TM kept
    _, moves, tms = step("tm", args.save, 1900, OPEN + TM, dict(party, PKMN_TEST_BAG=f"{ITEM_TM03}:1"),
                         shots="450,530,610,990,1700")
    if not moves or moves[-1][1][0] != (MOVE_WATER_PULSE, WATER_PULSE_PP, WATER_PULSE_PP):
        problems.append(f"tm: after the keys {moves[-1:]}, not Water Pulse at {WATER_PULSE_PP}/{WATER_PULSE_PP} first")
    if not tms or tms[-1] != 1:
        problems.append(f"tm: the Bag's TM03 count {tms[-1:]}, not 1 (a TM is never used up by MOVES)")

    report = dict(steps=steps, problems=problems, passed=not problems)
    with open(os.path.join(args.out, "move_switch_check.json"), "w") as out:
        json.dump(report, out, indent=1)
    print(json.dumps(dict(passed=not problems, problems=problems)))
    return 0 if not problems else 1


if __name__ == "__main__":
    sys.exit(main())
