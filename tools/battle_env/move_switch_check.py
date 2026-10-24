#!/usr/bin/env python3
"""Move switching as in Legends Z-A (the owner's request of 2026-10-24) in real game processes (test builds; Linux and
Windows alike): MOVES in the party menu opens the move relearner for that Pokemon, a move taken has full PP, no TM is used
up (not from the Bag either), the relearner comes back to the party menu, and the moveset is saved like any party change.
The lists follow Legends Z-A: level-up moves to the Pokemon's level, egg moves only for one that hatched, tutor moves only
from the one-time tutors the player has used, the TMs and HMs the player has. A move that can't be switched back in later,
or one with PP Ups, goes only after a YES (the cursor starts on NO); an HM's move can be switched out when the HM is in the
Bag. Everything is read from the game's own log (PKMN_TEST_MOVE_LOG: the lead's moves and PP whenever they change;
PKMN_TEST_BAG_LOG: a TM's count); the field keys are the presses a person makes (game frames at fast text, as the pictures
each step takes showed them).

The lead is a level-20 Mudkip (it knows Mud-Slap, Foresight, Bide and Mud Sport; Tackle, Growl and Water Gun are
level-up moves it learned before), caught (not hatched) unless a step says otherwise.

  1. level-up   START > POKeMON > Mudkip > MOVES (under SUMMARY), Tackle, "Teach Tackle?" YES, "Should a move be deleted
                and replaced with Tackle?" YES, Mud-Slap forgotten in the summary's move list; Tackle at 35/35. Out of the
                relearner (B, YES): the party menu again on Mudkip; out of the menus, SAVE
  2. load       that save loaded: Tackle at 35/35 in the first slot
  3. tm         the Bag has TM03 (Water Pulse): MOVES, SELECT straight to the TM moves (a caught Mudkip has no egg moves,
                no tutor was used), Water Pulse taught over the first move: Water Pulse at 20/20, the Bag still has TM03
  4. tm-bag     the same TM taught the usual way, Bag > TMs & HMs > TM03 > USE: Water Pulse learned, TM03 still there
  5. egg        a Mudkip that hatched from an Egg: SELECT goes to its egg moves first, Refresh (the first) taught
  6. tutor      the Sootopolis tutor used (its flag): SELECT goes to the tutor moves, Double-Edge (that tutor's) taught
  7. hm         Mudkip knows Surf (first slot), HM03 is in the Bag: Tackle taught over Surf (no "HM moves can't be
                forgotten"); Tackle at 35/35
  8. warn-no    Mudkip knows Snore (a Battle Frontier tutor's move, not on any list) with PP Max (24/24): Tackle, Snore
                chosen to forget: "Snore can't be switched back in later." / "Its PP Ups will be lost. Forget it anyway?"
                with the cursor on NO; A (NO), "Stop trying to teach Tackle?" YES: Snore still there at 24/24
  9. warn-yes   the same, UP to YES: Snore forgotten, Tackle at 35/35 (the PP Max went with Snore)

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
ITEM_TM03, ITEM_HM03 = 584, 684             # Water Pulse, Surf (include/constants/items.h)
MOVE_TACKLE, MOVE_MUD_SLAP, MOVE_WATER_PULSE = 33, 189, 352
MOVE_DOUBLE_EDGE, MOVE_SURF, MOVE_SNORE, MOVE_REFRESH = 38, 57, 173, 287
TACKLE_PP, WATER_PULSE_PP, DOUBLE_EDGE_PP, REFRESH_PP = 35, 20, 15, 20
SNORE_PP_MAX = 24                           # 15 with a PP Max (p3: the first slot's two bits)
FLAG_MOVE_TUTOR_TAUGHT_DOUBLE_EDGE = 0x1B9  # include/constants/flags.h

# START, POKeMON, Mudkip, MOVES (one down from SUMMARY), A: the relearner with the level-up moves (Tackle first)
OPEN = ["30:START", "80:A", "200:A", "280:DOWN", "340:A"]
# "Teach Tackle?" YES, "... wants to learn the move Tackle", "However, ... already knows four moves.", "Should a move be
# deleted and replaced with Tackle?" YES, "Which move should be forgotten?" (the summary's list: the first move), "1, 2,
# and... Poof!", "... forgot how to use Mud-Slap.", "And...", "... learned Tackle!"
TEACH = [f"{f}:A" for f in range(460, 1561, 100)]
# B: "Stop trying to learn new moves for Mudkip?" YES: the party menu on Mudkip; B, B, B: out of the menus
LEAVE = ["1700:B", "1800:A", "2050:B", "2150:B", "2250:B"]
SAVE_AT = 2400
# SELECT: the next list with moves (the TM moves, the egg moves of a hatched Mudkip, the tutor moves of a tutor used);
# then its first move taught as in TEACH
NEXT_LIST = ["480:SELECT"] + [f"{f}:A" for f in range(640, 1641, 100)]
# The Bag: START > BAG (under POKeMON), RIGHT twice to TMs & HMs (TM03 alone), TM03 > USE, "Booted up a TM.", "It
# contained Water Pulse.", "Teach Water Pulse to a POKeMON?" YES, Mudkip, then as TEACH: Mud-Slap forgotten
BAG_TEACH = ["30:START", "80:DOWN", "120:A", "260:RIGHT", "380:RIGHT"] + [f"{f}:A" for f in range(500, 2001, 100)]
# As TEACH up to "Which move should be forgotten?" and the summary's first move (A at 1060); then "Snore can't be switched
# back in later." (A: the next page), "Its PP Ups will be lost. Forget it anyway?" YES / NO with the cursor on NO
WARN = [f"{f}:A" for f in range(460, 1161, 100)]
# A: NO; "Stop trying to teach Tackle?" YES; "Mudkip did not learn the move Tackle." (A)
WARN_NO = ["1260:A", "1360:A", "1460:A"]
# UP: YES, A; "1, 2, and... Poof!", "... forgot how to use Snore.", "And...", "... learned Tackle!"
WARN_YES = ["1240:UP", "1270:A"] + [f"{f}:A" for f in range(1370, 1871, 100)]


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
    def first_slot(name, moves, want):
        if not moves or moves[-1][1][0] != want:
            problems.append(f"{name}: after the keys {moves[-1:]}, not {want} (move, PP, most PP) in the first slot")

    # 3. a TM's move from the Bag through MOVES: SELECT goes straight to the TM moves; taught, the TM kept
    _, moves, tms = step("tm", args.save, 1900, OPEN + NEXT_LIST, dict(party, PKMN_TEST_BAG=f"{ITEM_TM03}:1"),
                         shots="450,530,610,990,1700")
    first_slot("tm", moves, (MOVE_WATER_PULSE, WATER_PULSE_PP, WATER_PULSE_PP))
    if not tms or tms[-1] != 1:
        problems.append(f"tm: the Bag's TM03 count {tms[-1:]}, not 1 (a TM is never used up by MOVES)")
    # 4. the same TM taught from the Bag: never used up either (I_REUSABLE_TMS on PC)
    _, moves, tms = step("tm-bag", args.save, 2300, BAG_TEACH, dict(party, PKMN_TEST_BAG=f"{ITEM_TM03}:1"),
                         shots="450,550,850,1050,1450,2100")
    first_slot("tm-bag", moves, (MOVE_WATER_PULSE, WATER_PULSE_PP, WATER_PULSE_PP))
    if not tms or tms[-1] != 1:
        problems.append(f"tm-bag: the Bag's TM03 count {tms[-1:]}, not 1 (a TM taught from the Bag is kept on PC)")
    # 5. a hatched Mudkip: its egg moves are the next list (Refresh first)
    _, moves, _ = step("egg", args.save, 1900, OPEN + NEXT_LIST, dict(PKMN_TEST_PARTY=f"{SPECIES_MUDKIP}:20:h"),
                       shots="450,530,1700")
    first_slot("egg", moves, (MOVE_REFRESH, REFRESH_PP, REFRESH_PP))
    # 6. the Double-Edge tutor used: its move is the next list
    _, moves, _ = step("tutor", args.save, 1900, OPEN + NEXT_LIST,
                       dict(party, PKMN_TEST_SET_FLAGS=hex(FLAG_MOVE_TUTOR_TAUGHT_DOUBLE_EDGE)), shots="450,530,1700")
    first_slot("tutor", moves, (MOVE_DOUBLE_EDGE, DOUBLE_EDGE_PP, DOUBLE_EDGE_PP))
    # 7. an HM's move switched out (HM03 in the Bag switches it back in)
    _, moves, _ = step("hm", args.save, 1900, OPEN + TEACH,
                       dict(PKMN_TEST_PARTY=f"{SPECIES_MUDKIP}:20:v{MOVE_SURF}", PKMN_TEST_BAG=f"{ITEM_HM03}:1"),
                       shots="1010,1100,1700")
    if not moves or moves[0][1][0][0] != MOVE_SURF:
        problems.append(f"hm: the lead's moves before the keys {moves[:1]}, not Surf first")
    first_slot("hm", moves, (MOVE_TACKLE, TACKLE_PP, TACKLE_PP))
    # 8, 9. a move no list has (Snore, a Battle Frontier tutor's) with PP Max: asked first, NO keeps it, YES lets it go
    snore = dict(PKMN_TEST_PARTY=f"{SPECIES_MUDKIP}:20:v{MOVE_SNORE}p3")
    _, moves, _ = step("warn-no", args.save, 1800, OPEN + WARN + WARN_NO, snore, shots="1010,1140,1210,1310,1410,1600")
    if not moves or moves[0][1][0] != (MOVE_SNORE, SNORE_PP_MAX, SNORE_PP_MAX):
        problems.append(f"warn-no: the lead's moves before the keys {moves[:1]}, not Snore at {SNORE_PP_MAX}/{SNORE_PP_MAX}")
    first_slot("warn-no", moves, (MOVE_SNORE, SNORE_PP_MAX, SNORE_PP_MAX))
    _, moves, _ = step("warn-yes", args.save, 2100, OPEN + WARN + WARN_YES, snore, shots="1140,1210,1310,1900")
    first_slot("warn-yes", moves, (MOVE_TACKLE, TACKLE_PP, TACKLE_PP))

    report = dict(steps=steps, problems=problems, passed=not problems)
    with open(os.path.join(args.out, "move_switch_check.json"), "w") as out:
        json.dump(report, out, indent=1)
    print(json.dumps(dict(passed=not problems, problems=problems)))
    return 0 if not problems else 1


if __name__ == "__main__":
    sys.exit(main())
