#!/usr/bin/env python3
"""Move switching as in Legends Z-A (the owner's request of 2026-10-24) in real game processes (test builds; Linux and
Windows alike): MOVES in the party menu opens the move relearner for that Pokemon, a move taken has full PP, no TM is used
up (not from the Bag either), the relearner comes back to the party menu, and the moveset is saved like any party change.
The lists follow Legends Z-A: level-up moves to the Pokemon's level (each earlier form's only to the level it evolved
at), egg moves only for one that hatched, tutor moves only from the one-time tutors the player has used, the TMs and HMs
the player has; SELECT or R goes to the next list with moves, L to the one before. A move that can't be switched back in
later, or one with PP Ups, goes only after a YES (the cursor starts on NO); an HM's move can be switched out when the HM
is in the Bag. Everything is read from the game's own log (PKMN_TEST_MOVE_LOG: the lead's moves and PP whenever they
change, and the relearner's list as it shows it - its kind, its moves in order, the cursor; PKMN_TEST_BAG_LOG: a TM's
count; in the 3D build PKMN_NATIVE_UI_LOG: the native screens as they come up); the field keys are the presses a person
makes (game frames at fast text, as the pictures each step takes showed them).

The 3D build (Makefile_pc DIORAMA=1, "Native UI: on" in its log) draws the relearner as its own native screen ("moves",
src/native_ui/screens/ui_move_switch.c): it must come up for MOVES, draw the move to forget itself (the summary's move list
underneath is never shown as the summary screen) and hand back to the party screen. The Classic 2D build keeps the GBA
screens (its rows tagged "Lv12" / "TM03"). PKMN_TEST_FIELD_SHOTS pictures: the GBA frame in 2D, the window in 3D.

The lead is a level-20 Mudkip (it knows Mud-Slap, Foresight, Bide and Mud Sport; Tackle, Growl and Water Gun are
level-up moves it learned before), caught (not hatched) unless a step says otherwise.

  1. level-up   START > POKeMON > Mudkip > MOVES (under SUMMARY): the level-up list, Tackle first; Tackle, "Teach Tackle?"
                YES, "Should a move be deleted and replaced with Tackle?" YES, Mud-Slap forgotten in the move list (3D: the
                relearner's own replace step); Tackle at 35/35. Out of the relearner (B, YES): the party menu again on
                Mudkip; out of the menus, SAVE
  2. load       that save loaded: Tackle at 35/35 in the first slot
  3. tm         the Bag has TM03 (Water Pulse): MOVES, SELECT straight to the TM moves (a caught Mudkip has no egg moves,
                no tutor was used), Water Pulse taught over the first move: Water Pulse at 20/20, the Bag still has TM03,
                and the TM list is then empty (its "No moves to learn here")
  4. tm-bag     the same TM taught the usual way, Bag > TMs & HMs > TM03 > USE: Water Pulse learned, TM03 still there
  5. egg        a Mudkip that hatched from an Egg: SELECT goes to its egg moves first, Refresh (the first) taught
  6. tutor      the Sootopolis tutor used (its flag): SELECT goes to the tutor moves, Double-Edge (that tutor's) taught
  7. hm         Mudkip knows Surf (first slot), HM03 is in the Bag: Tackle taught over Surf (no "HM moves can't be
                forgotten"); Tackle at 35/35
  8. warn-no    Mudkip knows Snore (a Battle Frontier tutor's move, not on any list) with PP Max (24/24): Tackle, Snore
                chosen to forget: "Snore can't be switched back in later." / "Its PP Ups will be lost. Forget it anyway?"
                with the cursor on NO; A (NO), "Stop trying to teach Tackle?" YES: Snore still there at 24/24
  9. warn-yes   the same, UP to YES: Snore forgotten, Tackle at 35/35 (the PP Max went with Snore)
 9b. dont-learn Tackle, YES, YES, then B in the move list to forget (don't learn it), "Stop trying to teach Tackle?" YES:
                Mudkip's moves as they were (Mud-Slap at 10/10 first)
 10. lists-lr   TM03 in the Bag and the Sootopolis tutor used: R, R, L, L go level-up > TM > tutor > TM > level-up
 11. charmeleon a caught level-20 Charmeleon: its level-up list has Scratch but no Scary Face (Charmander learns it at
                Lv 19, after it evolved at Lv 16; Charmeleon's own is Lv 21) - the pre-evolution cap
 12. rotom-wash a level-50 Rotom-Wash knowing Thunder Shock, Thunder Wave, Confuse Ray and Shock Wave, no Hydro Pump (the
                owner's report of 2026-10-31): its level-up list has Hydro Pump first (its form's move) and none of the
                other forms' moves; Hydro Pump taught over Thunder Shock: Hydro Pump at 5/5
 13. mewtwo     a caught level-70 Mewtwo knowing only Confusion (the owner's case; this game's learnsets are Gen 6's): its
                level-up list has Recover (Lv 50), Barrier (Lv 64) and Aura Sphere (Lv 70), not Mist (Lv 86) or
                Psystrike (Lv 100); no Calm Mind (TM04 only, not in the Bag)
 14. mewtwo-tm  the same Mewtwo with TM04 (Calm Mind) in the Bag: SELECT goes to the TM moves, Calm Mind among them

    tools/battle_env/move_switch_check.py --exe ./pokeemerald-mp-bfxtest --save town.sav --out build/move-switch-check

Exit 0 when every step gave what it expects; the report is OUT/move_switch_check.json.
"""
import argparse
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mega_check import driver_lines, finish, numbers, save_keys, start  # noqa: E402

SPECIES_MUDKIP, SPECIES_CHARMELEON = 258, 5
ITEM_TM03, ITEM_HM03 = 584, 684             # Water Pulse, Surf (include/constants/items.h)
MOVE_TACKLE, MOVE_MUD_SLAP, MOVE_WATER_PULSE = 33, 189, 352
MOVE_DOUBLE_EDGE, MOVE_SURF, MOVE_SNORE, MOVE_REFRESH = 38, 57, 173, 287
MOVE_SCRATCH, MOVE_SCARY_FACE = 10, 184
SPECIES_ROTOM_WASH, SPECIES_MEWTWO = 1065, 150
MOVE_THUNDER_SHOCK, MOVE_THUNDER_WAVE, MOVE_CONFUSE_RAY, MOVE_SHOCK_WAVE, MOVE_HYDRO_PUMP = 84, 86, 109, 351, 56
FORM_MOVES = {315: "Overheat", 59: "Blizzard", 403: "Air Slash", 437: "Leaf Storm"}   # the other Rotom forms' moves
MOVE_CONFUSION, MOVE_RECOVER, MOVE_BARRIER, MOVE_AURA_SPHERE, MOVE_MIST, MOVE_PSYSTRIKE = 93, 105, 112, 396, 54, 540
MOVE_CALM_MIND, ITEM_TM04 = 347, 585
HYDRO_PUMP_PP = 5
LIST_LEVEL_UP, LIST_EGG, LIST_TM, LIST_TUTOR = 0, 1, 2, 3   # enum MoveRelearnerStates
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
# then its first move taught as in TEACH (the TM step adds the A that closes "... learned Water Pulse!": the list again)
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
# As TEACH up to the move list to forget (A at 960 closes "Which move should be forgotten?"); there B: don't learn it;
# "Stop trying to teach Tackle?" YES; "Mudkip did not learn the move Tackle." (A)
DONT_LEARN = [f"{f}:A" for f in range(460, 961, 100)] + ["1060:B", "1160:A", "1260:A"]
# R, R, L, L through the lists with moves; B, YES: out of the relearner
LISTS_LR = ["480:R", "560:R", "640:L", "720:L", "800:B", "900:A"]
# B, YES: out of the relearner straight away
OUT = ["520:B", "620:A"]


def move_lines(lines):
    """Each "move field" line's (species, [(move, pp, max pp) x 4])."""
    out = []
    for line in lines:
        if line.startswith("move field: "):
            n = numbers(line)
            out.append((n[0], [tuple(n[1 + i * 3:4 + i * 3]) for i in range(4)]))
    return out


def list_lines(lines):
    """Each "move list" line's (list, cursor, [moves]): the relearner's list as it shows it."""
    out = []
    for line in lines:
        match = re.match(r"move list: list (\d+) cursor (\d+) moves (.*)$", line)
        if match:
            moves = [] if match.group(3) == "-" else [int(m) for m in match.group(3).split(",")]
            out.append((int(match.group(1)), int(match.group(2)), moves))
    return out


def screen_changes(path):
    """The 3D build's native screens as they came up ("Native UI: screen A -> B"); None for a build without them."""
    if not os.path.exists(path):
        return None
    text = open(path, encoding="utf-8", errors="replace").read()
    if "Native UI: on" not in text:
        return None
    return re.findall(r"Native UI: screen (\S+) -> (\S+)", text)


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

    def step(name, save, frames, keys, extra=None, shots="", opens=True, forgets=True, leaves=False):
        rundir = os.path.join(args.out, name)
        saved, args.save = args.save, save
        env = dict(PKMN_TEST_BATTLE=f"field:{frames}", PKMN_TEST_MOVE_LOG="1", PKMN_TEST_BAG_LOG=str(ITEM_TM03),
                   PKMN_NATIVE_UI_LOG="1",
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
        lists = list_lines(lines[begun:])
        screens = screen_changes(os.path.join(rundir, "game.log"))
        steps.append(dict(step=name, exit=code, moves=moves, tm03=tms, lists=lists, screens=screens,
                          notes=[line for line in lines if line.startswith(("cannot", "assert"))],
                          pictures=sorted(f for f in os.listdir(rundir) if f.endswith(".png"))))
        if code != 0:
            problems.append(f"{name}: the game ended with {code}")
        problems.extend(f"{name}: {line}" for line in steps[-1]["notes"])
        if keys and opens and not lists:
            problems.append(f"{name}: the relearner showed no list (no \"move list\" line)")
        if screens is not None and opens:
            check_screens(name, screens, forgets, leaves)
        return rundir, moves, tms

    def check_screens(name, screens, forgets, leaves):
        """The 3D build: MOVES brings up the native relearner screen once and it stays up (the move to forget is its own
        replace step: the summary screen never shows); out of it (leaves), the next screen is the party's (a frame of the
        game's own black picture between them aside)."""
        entries = [i for i, (_, b) in enumerate(screens) if b == "moves"]
        if not entries:
            problems.append(f"{name}: the native relearner screen (moves) never came up: {screens}")
            return
        after = screens[entries[0] + 1:]
        if len(entries) > 1 or any(b == "summary" for a, b in after if a == "moves"):
            problems.append(f"{name}: the relearner screen was left and came back, or the summary screen showed "
                            f"(the move to forget is the relearner screen's own step): {screens[entries[0]:]}")
        if not leaves and after:
            problems.append(f"{name}: the relearner screen did not stay up to the end: {screens[entries[0]:]}")
        if leaves:
            nxt = next((b for _, b in after if b != "(game)"), None)
            if nxt != "party":
                problems.append(f"{name}: out of the relearner the next screen is {nxt}, not the party's: "
                                f"{screens[entries[0]:]}")

    party = dict(PKMN_TEST_PARTY=f"{SPECIES_MUDKIP}:20")
    # 1. a level-up move switched in, saved
    rundir, moves, _ = step("level-up", args.save, SAVE_AT + 900, OPEN + TEACH + LEAVE + save_keys(SAVE_AT), party,
                            shots="300,420,560,960,1100,1460,1560,1900,2000,2600", leaves=True)
    if not moves or moves[0][1][0][0] != MOVE_MUD_SLAP:
        problems.append(f"level-up: the lead's moves before the keys {moves[:1]}, not Mud-Slap first")
    if not moves or moves[-1][1][0] != (MOVE_TACKLE, TACKLE_PP, TACKLE_PP):
        problems.append(f"level-up: after the keys {moves[-1:]}, not Tackle at {TACKLE_PP}/{TACKLE_PP} in Mud-Slap's place")
    lists = steps[-1]["lists"]
    if not lists or lists[0][0] != LIST_LEVEL_UP or not lists[0][2] or lists[0][2][0] != MOVE_TACKLE:
        problems.append(f"level-up: the relearner's first list {lists[:1]}, not the level-up list with Tackle first")
    # 2. that save loaded
    _, moves, _ = step("load", os.path.join(rundir, "test.sav"), 60, [], opens=False)
    if not moves or moves[-1][1][0] != (MOVE_TACKLE, TACKLE_PP, TACKLE_PP):
        problems.append(f"load: the saved lead's moves {moves[-1:]}, not Tackle at {TACKLE_PP}/{TACKLE_PP} first")
    def first_slot(name, moves, want):
        if not moves or moves[-1][1][0] != want:
            problems.append(f"{name}: after the keys {moves[-1:]}, not {want} (move, PP, most PP) in the first slot")

    # 3. a TM's move from the Bag through MOVES: SELECT goes straight to the TM moves; taught, the TM kept
    _, moves, tms = step("tm", args.save, 1900, OPEN + NEXT_LIST + ["1740:A"], dict(party, PKMN_TEST_BAG=f"{ITEM_TM03}:1"),
                         shots="450,530,610,990,1700,1820")
    first_slot("tm", moves, (MOVE_WATER_PULSE, WATER_PULSE_PP, WATER_PULSE_PP))
    if not tms or tms[-1] != 1:
        problems.append(f"tm: the Bag's TM03 count {tms[-1:]}, not 1 (a TM is never used up by MOVES)")
    lists = steps[-1]["lists"]
    if not any(kind == LIST_TM and MOVE_WATER_PULSE in shown for kind, _, shown in lists):
        problems.append(f"tm: no TM list with Water Pulse shown: {lists}")
    if not lists or lists[-1][0] != LIST_TM or lists[-1][2]:
        problems.append(f"tm: once Water Pulse is taught the TM list is not empty: {lists[-1:]}")
    # 4. the same TM taught from the Bag: never used up either (I_REUSABLE_TMS on PC)
    _, moves, tms = step("tm-bag", args.save, 2300, BAG_TEACH, dict(party, PKMN_TEST_BAG=f"{ITEM_TM03}:1"),
                         shots="450,550,850,1050,1450,2100", opens=False)
    first_slot("tm-bag", moves, (MOVE_WATER_PULSE, WATER_PULSE_PP, WATER_PULSE_PP))
    if not tms or tms[-1] != 1:
        problems.append(f"tm-bag: the Bag's TM03 count {tms[-1:]}, not 1 (a TM taught from the Bag is kept on PC)")
    # 5. a hatched Mudkip: its egg moves are the next list (Refresh first)
    _, moves, _ = step("egg", args.save, 1900, OPEN + NEXT_LIST, dict(PKMN_TEST_PARTY=f"{SPECIES_MUDKIP}:20:h"),
                       shots="450,530,1700")
    first_slot("egg", moves, (MOVE_REFRESH, REFRESH_PP, REFRESH_PP))
    if not any(kind == LIST_EGG and shown[:1] == [MOVE_REFRESH] for kind, _, shown in steps[-1]["lists"]):
        problems.append(f"egg: no egg move list with Refresh first: {steps[-1]['lists']}")
    # 6. the Double-Edge tutor used: its move is the next list
    _, moves, _ = step("tutor", args.save, 1900, OPEN + NEXT_LIST,
                       dict(party, PKMN_TEST_SET_FLAGS=hex(FLAG_MOVE_TUTOR_TAUGHT_DOUBLE_EDGE)), shots="450,530,1700")
    first_slot("tutor", moves, (MOVE_DOUBLE_EDGE, DOUBLE_EDGE_PP, DOUBLE_EDGE_PP))
    if not any(kind == LIST_TUTOR and shown == [MOVE_DOUBLE_EDGE] for kind, _, shown in steps[-1]["lists"]):
        problems.append(f"tutor: no tutor list of Double-Edge alone: {steps[-1]['lists']}")
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

    # 9b. the move to forget not chosen (B): nothing is forgotten, nothing learned
    _, moves, _ = step("dont-learn", args.save, 1500, OPEN + DONT_LEARN, party, shots="1010,1110,1210,1310")
    first_slot("dont-learn", moves, (MOVE_MUD_SLAP, 10, 10))
    if len(moves) > 1 and moves[-1] != moves[0]:
        problems.append(f"dont-learn: the moves changed: {moves[0]} -> {moves[-1]}")
    # 10. L and R through the lists (TM03 in the Bag, the Sootopolis tutor used: level-up, TM and tutor lists)
    step("lists-lr", args.save, 1100, OPEN + LISTS_LR,
         dict(party, PKMN_TEST_BAG=f"{ITEM_TM03}:1", PKMN_TEST_SET_FLAGS=hex(FLAG_MOVE_TUTOR_TAUGHT_DOUBLE_EDGE)),
         shots="450,530,610,690,770", forgets=False, leaves=True)
    kinds = []
    for kind, _, _ in steps[-1]["lists"]:
        if not kinds or kinds[-1] != kind:
            kinds.append(kind)
    want = [LIST_LEVEL_UP, LIST_TM, LIST_TUTOR, LIST_TM, LIST_LEVEL_UP]
    if kinds != want:
        problems.append(f"lists-lr: R, R, L, L showed the lists {kinds}, not {want}")
    # 11. a level-20 Charmeleon: Charmander's moves only up to Lv 16 (no Scary Face, Charmander's Lv 19)
    step("charmeleon", args.save, 900, OPEN + OUT, dict(PKMN_TEST_PARTY=f"{SPECIES_CHARMELEON}:20"), shots="450",
         forgets=False, leaves=True)
    lists = steps[-1]["lists"]
    if not lists or lists[0][0] != LIST_LEVEL_UP or MOVE_SCRATCH not in lists[0][2] or MOVE_SCARY_FACE in lists[0][2]:
        problems.append(f"charmeleon: its level-up list {lists[:1]}: not Scratch without Scary Face")
    # 12. a Rotom-Wash without Hydro Pump: its form's move first on the level-up list, taught over Thunder Shock
    rotom = ".".join(str(m) for m in (MOVE_THUNDER_SHOCK, MOVE_THUNDER_WAVE, MOVE_CONFUSE_RAY, MOVE_SHOCK_WAVE))
    _, moves, _ = step("rotom-wash", args.save, 1900, OPEN + TEACH,
                       dict(PKMN_TEST_PARTY=f"{SPECIES_ROTOM_WASH}:50:V{rotom}"), shots="450,1010,1100,1700")
    lists = steps[-1]["lists"]
    if not lists or lists[0][0] != LIST_LEVEL_UP or lists[0][2][:1] != [MOVE_HYDRO_PUMP]:
        problems.append(f"rotom-wash: its level-up list {lists[:1]}: not Hydro Pump first")
    others = sorted({FORM_MOVES[m] for _, _, shown in lists for m in shown if m in FORM_MOVES})
    if others:
        problems.append(f"rotom-wash: other forms' moves on its lists: {others}")
    if not moves or moves[0][1][0][0] != MOVE_THUNDER_SHOCK:
        problems.append(f"rotom-wash: the lead's moves before the keys {moves[:1]}, not Thunder Shock first")
    first_slot("rotom-wash", moves, (MOVE_HYDRO_PUMP, HYDRO_PUMP_PP, HYDRO_PUMP_PP))
    # 13. a level-70 Mewtwo knowing Confusion alone: the moves of its learnset to Lv 70, none later
    mewtwo = dict(PKMN_TEST_PARTY=f"{SPECIES_MEWTWO}:70:V{MOVE_CONFUSION}.0.0.0")
    step("mewtwo", args.save, 900, OPEN + OUT, mewtwo, shots="450", forgets=False, leaves=True)
    lists = steps[-1]["lists"]
    shown = lists[0][2] if lists and lists[0][0] == LIST_LEVEL_UP else []
    missing = [m for m in (MOVE_RECOVER, MOVE_BARRIER, MOVE_AURA_SPHERE) if m not in shown]
    early = [m for m in (MOVE_MIST, MOVE_PSYSTRIKE, MOVE_CALM_MIND) if m in shown]
    if not shown or missing or early:
        problems.append(f"mewtwo: its level-up list {lists[:1]}: missing {missing}, too early {early}")
    if any(MOVE_CALM_MIND in moves_shown for _, _, moves_shown in lists):
        problems.append(f"mewtwo: Calm Mind offered without TM04: {lists}")
    # 14. TM04 in the Bag: SELECT to the TM moves, Calm Mind there
    step("mewtwo-tm", args.save, 1000, OPEN + ["480:SELECT", "600:B", "700:A"],
         dict(mewtwo, PKMN_TEST_BAG=f"{ITEM_TM04}:1"), shots="450,560", forgets=False, leaves=True)
    lists = steps[-1]["lists"]
    if not any(kind == LIST_TM and MOVE_CALM_MIND in shown for kind, _, shown in lists):
        problems.append(f"mewtwo-tm: no TM list with Calm Mind: {lists}")

    report = dict(steps=steps, problems=problems, passed=not problems)
    with open(os.path.join(args.out, "move_switch_check.json"), "w") as out:
        json.dump(report, out, indent=1)
    print(json.dumps(dict(passed=not problems, problems=problems)))
    return 0 if not problems else 1


if __name__ == "__main__":
    sys.exit(main())
