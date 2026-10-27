#!/usr/bin/env python3
"""Breeding and egg hatching in real game processes (test builds; Linux and Windows alike): the Day Care's Egg taken
from the Day Care man on Route 117, saved, hatched by walking, saved again, each read from the game's own log
(PKMN_TEST_BREED_LOG: every party Pokemon's species, Egg flag, level, IVs, nature, Ability slot, ball, moves and Egg
cycles, and the Day Care's state). The field keys are the presses a person makes (frames at fast text, as the pictures
each step takes showed them).

The Day Care (PKMN_TEST_DAYCARE, a test set-up as the Day Care lady would store them) holds a female Pikachu (Modest,
in a Quick Ball, every IV 31, holding an Everstone, Fake Out first) and a Ditto (every IV 0, holding a Destiny Knot);
an Egg of theirs waits with the Day Care man (PKMN_TEST_EGG_READY: the step check that makes one, now).

  1. egg          Route 117 below the Day Care man: talk, YES to "You do want it, yes?", the Egg received; SAVE.
                  The Egg (the engine's rules, src/daycare.c): a Pichu (Pikachu's baby), level 1; the Everstone's
                  nature (Modest); the mother's ball (Quick Ball: Ditto passes none); five IVs from the parents (the
                  Destiny Knot: each 31 or 0, the sixth random); Fake Out, an Egg move the mother knows
  2. egg-load     that save loaded: the Egg as it was given
  3. hatch        from that save in Littleroot, its Egg cycles run down (PKMN_TEST_EGG_CYCLES=0: in memory, an
                  acceleration; the steps of one cycle are still walked): left and right until "Huh?", the hatching,
                  "... hatched from the EGG!", NO to a nickname; SAVE. The Pichu is no longer an Egg and keeps the
                  Egg's IVs, nature, ball and moves
  4. hatch-load   that save loaded: the hatched Pichu
  5. light-ball   as 1 with the mother holding a Light Ball instead: the Egg knows Volt Tackle (the Light Ball rule)
                  besides Fake Out
  6. pc-egg       Eggs hatching in the PC (the owner's request of 2026-10-27): 1's save, its Egg put in the PC
                  (PKMN_TEST_EGG_TO_PC) with 3 cycles left, 96 steps walked in Littleroot: cycles spent (fewer than 3,
                  still an Egg); SAVE
  7. pc-egg-load  that save loaded: the Egg in the PC with the cycles it was saved with; the party without it
  8. pc-egg-hatch from 6's save, walked until it hatches in its PC slot: the Pichu with the Egg's IVs, nature, ball
                  and moves, and the banner "PC EGG HATCHED!|Pichu hatched in your PC."; SAVE
  9. pc-egg-hatch-load  that save loaded: the hatched Pichu in its PC slot
 10. pc-eggs      three Eggs in three boxes (PKMN_TEST_BOX) with no cycles left: all three hatch at the same cycle's
                  end, one banner "3 PC EGGS HATCHED!|..."

    tools/battle_env/breed_check.py --exe ./pokeemerald-mp-bfxtest --save town.sav --out build/breed-check \\
        [--display :99] [--make-save [PATH]]

Exit 0 when every step gave what it expects; the report is OUT/breed_check.json.
"""
import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mega_check import driver_lines, finish, make_save, numbers, save_keys, start  # noqa: E402

SPECIES_BULBASAUR, SPECIES_PIKACHU, SPECIES_DITTO, SPECIES_PICHU = 1, 25, 132, 172
NATURE_MODEST, BALL_QUICK = 15, 12
ITEM_EVERSTONE, ITEM_LIGHT_BALL, ITEM_DESTINY_KNOT = 245, 392, 486
MOVE_FAKE_OUT, MOVE_VOLT_TACKLE = 252, 344
DITTO = f"{SPECIES_DITTO}:20:w0i{ITEM_DESTINY_KNOT}"
MOTHER = f"{SPECIES_PIKACHU}:20:fn{NATURE_MODEST}b{BALL_QUICK}w31v{MOVE_FAKE_OUT}"
DAYCARE = f"{MOTHER}i{ITEM_EVERSTONE};{DITTO}"
DAYCARE_LIGHT_BALL = f"{MOTHER}i{ITEM_LIGHT_BALL};{DITTO}"
ROUTE_117_DAYCARE_MAN = "0:32:0:47:5"      # PKMN_TEST_WARP: Route 117, below the Day Care man (47,4)
LITTLEROOT = "0:9:0:10:10"                 # an open row of Littleroot Town (no grass: no wild Pokemon)

# Facing the Day Care man (UP), A: "Ah, it's you! ... Your POKeMON had an EGG! ... You do want it, yes?" (the
# cursor on YES), "... received the EGG from the DAY-CARE MAN." (a fanfare, then a button), "Take good care of it."
EGG_KEYS = ["30:UP", "120:A", "300:A", "450:A", "600:A", "750:A", "900:A", "1050:A", "1200:A"]
EGG_SAVE_AT = 1400


def hatch_keys():
    """Left and right, three taps each way (the first turns), 48 steps; then B every second: "Huh?", the hatching's
    lines, NO to the nickname (the cursor starts on YES; B is NO); then START > SAVE."""
    keys, frame = [], 30
    for _ in range(12):
        for key in ("LEFT", "LEFT", "LEFT", "RIGHT", "RIGHT", "RIGHT"):
            keys.append(f"{frame}:{key}")
            frame += 18
    for _ in range(25):
        keys.append(f"{frame}:B")
        frame += 60
    return keys, frame + 100


def walk_keys(rounds):
    """As hatch_keys' walk alone (a PC Egg hatches without a scene): `rounds` of three taps left, three right."""
    keys, frame = [], 30
    for _ in range(rounds):
        for key in ("LEFT", "LEFT", "LEFT", "RIGHT", "RIGHT", "RIGHT"):
            keys.append(f"{frame}:{key}")
            frame += 18
    return keys, frame + 120


def box_lines(lines):
    """The PC's Eggs as the log tells them: ({(box, slot): fields}, notices, moved) - the last "breed box" fields of
    each slot that held an Egg, every "breed pc notice", and the "breed moved" count (None without the line)."""
    boxes, notices, moved = {}, [], None
    for line in lines:
        if line.startswith("breed box "):
            head, rest = line.split(":", 1)
            box, slot = (int(x) for x in head.split()[2:4])
            if rest.strip() == "none":
                boxes[(box, slot)] = None
            else:
                v = numbers(rest)
                boxes[(box, slot)] = dict(species=v[0], egg=v[1], level=v[2], ivs=v[3:9], nature=v[9], num=v[10],
                                          ball=v[11], moves=v[12:16], cycles=v[16])
        elif line.startswith("breed pc notice: "):
            notices.append(line[len("breed pc notice: "):])
        elif line.startswith("breed moved "):
            moved = numbers(line)[0]
    return boxes, notices, moved


def breed_lines(lines):
    """The party and the Day Care as the log tells them: (slots, states) - slots[n] the last "breed party n" fields
    (a dict, or None for an empty slot), and every "breed state" (party, daycare, egg waiting), from the field keys'
    start (the last line before them stands for the loaded save)."""
    begun = next((i for i, line in enumerate(lines) if line.startswith("field keys for ")), len(lines))
    slots, before, states = {}, {}, []
    for i, line in enumerate(lines):
        if line.startswith("breed party "):
            n = int(line.split()[2].rstrip(":"))
            if line.endswith(": none"):
                value = None
            else:
                v = numbers(line.split(":", 1)[1])
                value = dict(species=v[0], egg=v[1], level=v[2], ivs=v[3:9], nature=v[9], num=v[10], ball=v[11],
                             moves=v[12:16], cycles=v[16])
            (slots if i >= begun else before)[n] = value
        elif line.startswith("breed state: "):
            states.append(dict(zip(("party", "daycare", "waiting", "boxes", "caught"), numbers(line)),
                               after_keys=i >= begun))
    return {**before, **slots}, before, states


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
                   PKMN_TEST_MEGA_SHOT_STEM=os.path.join(rundir, "step"), **(extra or {}))
        if shots:
            env["PKMN_TEST_FIELD_SHOTS"] = shots
        try:
            process, output = start(args, rundir, env)
            code = finish(process, output, frames / 60 + 180)
        finally:
            args.save = saved
        lines = driver_lines(os.path.join(rundir, "test.log"))
        slots, before, states = breed_lines(lines)
        boxes, notices, moved = box_lines(lines)
        steps.append(dict(step=name, exit=code, before=before, after=slots, states=states,
                          boxes={f"{b}:{s}": v for (b, s), v in boxes.items()}, notices=notices, moved=moved,
                          notes=[line for line in lines if line.startswith(("cannot", "assert"))],
                          pictures=sorted(f for f in os.listdir(rundir) if f.endswith(".png"))))
        if code != 0:
            problems.append(f"{name}: the game ended with {code}")
        problems.extend(f"{name}: {line}" for line in steps[-1]["notes"])
        return rundir, slots, before, states

    def check(name, got, **want):
        if got is None:
            problems.append(f"{name}: no such Pokemon")
            return
        wrong = {key: got[key] for key, value in want.items() if got[key] != value}
        if wrong:
            problems.append(f"{name}: {wrong}, expected {dict((k, want[k]) for k in wrong)}")

    def check_egg(name, egg, *moves):
        check(name, egg, species=SPECIES_PICHU, level=1)
        if egg is None:
            return
        inherited = sum(1 for iv in egg["ivs"] if iv in (0, 31))
        if inherited < 5:
            problems.append(f"{name}: IVs {egg['ivs']}: {inherited} from the parents, the Destiny Knot's 5 expected")
        for move in moves:
            if move not in egg["moves"]:
                problems.append(f"{name}: moves {egg['moves']} lack {move}")

    # 1. The Egg from the Day Care man, saved
    setup = dict(PKMN_TEST_PARTY=f"{SPECIES_BULBASAUR}:5", PKMN_TEST_DAYCARE=DAYCARE, PKMN_TEST_EGG_READY="1",
                 PKMN_TEST_WARP=ROUTE_117_DAYCARE_MAN)
    egg_dir, slots, _, states = step("egg", args.save, EGG_SAVE_AT + 900, EGG_KEYS + save_keys(EGG_SAVE_AT), setup,
                                     shots="300,750,1050,1200,1500,1700")
    if not any(s["daycare"] == 2 and s["waiting"] == 1 for s in states):
        problems.append(f"egg: the Day Care never held two Pokemon with an Egg waiting: {states}")
    if not states or (states[-1]["party"], states[-1]["waiting"]) != (2, 0):
        problems.append(f"egg: after the keys {states[-1] if states else None}, expected the Egg in the party and "
                        f"none waiting")
    egg = slots.get(1)
    check("egg", egg, egg=1, nature=NATURE_MODEST, ball=BALL_QUICK)
    check_egg("egg", egg, MOVE_FAKE_OUT)
    egg_save = os.path.join(egg_dir, "test.sav")

    # 2. That save loaded
    _, slots, _, _ = step("egg-load", egg_save, 60, [])
    if egg is not None:
        check("egg-load", slots.get(1), **{k: egg[k] for k in ("species", "egg", "ivs", "nature", "ball", "moves")})

    # 3. Hatched by walking, saved
    keys, save_at = hatch_keys()
    hatch_dir, slots, before, _ = step("hatch", egg_save, save_at + 900, keys + save_keys(save_at),
                                       dict(PKMN_TEST_EGG_CYCLES="0", PKMN_TEST_WARP=LITTLEROOT),
                                       shots="1300,1500,1700,1900,2100,2400,2700")
    hatched = slots.get(1)
    check("hatch", hatched, egg=0)
    if egg is not None:
        check("hatch", hatched, **{k: egg[k] for k in ("species", "level", "ivs", "nature", "ball", "moves")})

    # 4. That save loaded
    _, slots, _, _ = step("hatch-load", os.path.join(hatch_dir, "test.sav"), 60, [])
    if hatched is not None:
        check("hatch-load", slots.get(1), **{k: hatched[k] for k in ("species", "egg", "ivs", "nature", "ball", "moves")})

    # 5. The Light Ball's Volt Tackle
    setup["PKMN_TEST_DAYCARE"] = DAYCARE_LIGHT_BALL
    _, slots, _, _ = step("light-ball", args.save, EGG_SAVE_AT + 100, EGG_KEYS, setup)
    check("light-ball", slots.get(1), egg=1, ball=BALL_QUICK)
    check_egg("light-ball", slots.get(1), MOVE_FAKE_OUT, MOVE_VOLT_TACKLE)

    # 6. The Egg put in the PC: its cycles spent by walking, saved
    def pc_egg():
        """The one Egg of the last step's boxes, as (slot, fields)."""
        boxes = steps[-1]["boxes"]
        found = [(k, v) for k, v in boxes.items()]
        return found[0] if len(found) == 1 else (None, None)

    keys, save_at = walk_keys(24)
    pc_dir, slots, _, states = step("pc-egg", egg_save, save_at + 900, keys + save_keys(save_at),
                                    dict(PKMN_TEST_EGG_TO_PC="1", PKMN_TEST_EGG_CYCLES="3", PKMN_TEST_WARP=LITTLEROOT))
    slot, pc = pc_egg()
    if steps[-1]["moved"] != 1:
        problems.append(f"pc-egg: {steps[-1]['moved']} Eggs put in the PC, expected 1")
    if any(v is not None and v["egg"] for v in slots.values()):
        problems.append("pc-egg: an Egg is still in the party")
    if pc is None:
        problems.append(f"pc-egg: no single Egg in the PC: {steps[-1]['boxes']}")
    else:
        check("pc-egg", pc, egg=1)
        if not pc["cycles"] < 3:
            problems.append(f"pc-egg: {pc['cycles']} cycles left after 96 steps from 3: none spent")
        if egg is not None:
            check("pc-egg", pc, **{k: egg[k] for k in ("species", "ivs", "nature", "ball", "moves")})
    pc_save = os.path.join(pc_dir, "test.sav")

    # 7. That save loaded: the Egg in the PC as it was saved
    step("pc-egg-load", pc_save, 60, [])
    loaded = steps[-1]["boxes"].get(slot) if slot else None
    if pc is not None:
        check("pc-egg-load", loaded, **{k: pc[k] for k in ("species", "egg", "cycles", "ivs", "nature", "ball", "moves")})

    # 8. Walked until it hatches in the PC, saved
    keys, save_at = walk_keys(60)
    hatch_pc_dir, _, _, _ = step("pc-egg-hatch", pc_save, save_at + 900, keys + save_keys(save_at),
                                 dict(PKMN_TEST_WARP=LITTLEROOT))
    hatched_pc = steps[-1]["boxes"].get(slot) if slot else None
    check("pc-egg-hatch", hatched_pc, egg=0)
    if pc is not None:
        check("pc-egg-hatch", hatched_pc, **{k: pc[k] for k in ("species", "ivs", "nature", "ball", "moves")})
    want = "PC EGG HATCHED!|Pichu hatched in your PC."
    if want not in steps[-1]["notices"]:
        problems.append(f"pc-egg-hatch: banners {steps[-1]['notices']}, expected {want!r}")

    # 9. That save loaded: the hatched Pokemon in its PC slot
    step("pc-egg-hatch-load", os.path.join(hatch_pc_dir, "test.sav"), 60, [])
    if hatched_pc is not None:
        # the log tracks the PC's Eggs: none after the load; the PC's Pokemon (Eggs aside) one more than with the Egg,
        # and the Pokedex's caught count one more (the hatch registers the Pichu)
        state, egg_state = steps[-1]["states"][-1:], steps[-3]["states"][-1:]
        if steps[-1]["boxes"]:
            problems.append(f"pc-egg-hatch-load: Eggs in the PC after the load: {steps[-1]['boxes']}")
        if not state or not egg_state:
            problems.append("pc-egg-hatch-load: no breed state to compare")
        elif (state[0]["boxes"], state[0]["caught"]) != (egg_state[0]["boxes"] + 1, egg_state[0]["caught"] + 1):
            problems.append(f"pc-egg-hatch-load: PC and Pokedex {state[0]}, expected one more of each than {egg_state[0]}")

    # 10. Three Eggs in three boxes hatch at once
    eggs = f"0:5:{SPECIES_PICHU}:1:E;7:12:{SPECIES_PIKACHU}:1:E;13:29:{SPECIES_BULBASAUR}:1:E"
    keys, end = walk_keys(12)
    step("pc-eggs", args.save, end + 300, keys, dict(PKMN_TEST_BOX=eggs, PKMN_TEST_EGG_CYCLES="0",
                                                       PKMN_TEST_WARP=LITTLEROOT))
    boxes = steps[-1]["boxes"]
    for key, species in (("0:5", SPECIES_PICHU), ("7:12", SPECIES_PIKACHU), ("13:29", SPECIES_BULBASAUR)):
        check(f"pc-eggs {key}", boxes.get(key), egg=0, species=species)
    if not any(n.startswith("3 PC EGGS HATCHED!|") for n in steps[-1]["notices"]):
        problems.append(f"pc-eggs: banners {steps[-1]['notices']}, expected one for 3 Eggs")

    report = dict(steps=steps, problems=problems, passed=not problems)
    with open(os.path.join(args.out, "breed_check.json"), "w") as out:
        json.dump(report, out, indent=1)
    print(json.dumps(dict(passed=not problems, problems=problems)))
    return 0 if not problems else 1


if __name__ == "__main__":
    sys.exit(main())
