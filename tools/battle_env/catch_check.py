#!/usr/bin/env python3
"""Catching wild Pokemon in real game processes (test builds; Linux and Windows alike): a Master Ball thrown from the
battle's Bag, the catch kept in the party or sent to the PC, each saved and loaded. Everything is read from the game's
own log (PKMN_TEST_BREED_LOG: every party Pokemon's species, level and ball, the PC's Pokemon and the species the
Pokedex counts as caught; PKMN_TEST_BAG_LOG: the Master Balls left); the battle keys are the presses a person makes
(game frames from the action menu at fast text, as the pictures each step takes showed them).

  1. catch           five Pokemon in the party, a wild level-5 Magikarp: BAG, the POKe BALLS pocket, the Master Ball,
                     USE; "Gotcha! Magikarp was caught!", the experience (the lead's, then the Exp. Share's), "Magikarp's
                     data has been added to the Pokedex!", its Pokedex page, "Would you like to give Magikarp a
                     nickname?" NO; back on the field, SAVE. The party six with the Magikarp last (level 5, Master
                     Ball), the PC unchanged, one species more caught, the Master Ball used
  2. catch-load      that save loaded: the same
  3. catch-full      six Pokemon in the party, a wild level-5 Poochyena: the same presses; after the nickname question
                     "Poochyena was transferred to SOMEONE'S PC."; SAVE. The party unchanged, one Pokemon more in the
                     PC, one species more caught
  4. catch-full-load that save loaded: the same

    tools/battle_env/catch_check.py --exe ./pokeemerald-mp-bfxtest --save town.sav --out build/catch-check \\
        [--display :99] [--make-save [PATH]]

Exit 0 when every step gave what it expects; the report is OUT/catch_check.json.
"""
import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mega_check import driver_lines, finish, make_save, numbers, save_keys, start  # noqa: E402

SPECIES_MAGIKARP, SPECIES_POOCHYENA, SPECIES_ZIGZAGOON = 129, 261, 263
ITEM_MASTER_BALL = 4
WILD_LEVEL = 5

# At the action menu (FIGHT, BAG / POKeMON, RUN): BAG; the Bag opens on its ITEMS pocket (~110), RIGHT to POKe BALLS,
# the Master Ball (the only ball: PKMN_TEST_BAG_RESET empties the Bag first, as a new character starts with 5 Poke
# Balls that would come first), USE. The throw, the ball's shakes, "Gotcha! ... was caught!" waits (~1100): A; "<lead> gained ... Exp.
# Points!" (~1400): A; "The rest of your team gained Exp. Points thanks to the Exp. Share!" (~1760): A; "...'s data has
# been added to the Pokedex!" (~2020): A; its Pokedex page (~2180): A; "Would you like to give ... a nickname?" with
# YES / NO (~2540): B, NO. With a full party "... was transferred to SOMEONE'S PC." follows: the fight's A presses
# (PKMN_TEST_FIGHT, from the last key on) take it, and the field's return.
KEYS = ["30:RIGHT", "60:A", "140:RIGHT", "200:A", "260:A", "1200:A", "1450:A", "1800:A", "2100:A", "2300:A", "2650:B"]
# After the battle (PKMN_TEST_FIELD_AFTER: frames from the field's return) the start menu's cursor is on its first
# row: mega_check.py's save_keys.
SAVE_AT = 300


def catch_lines(lines):
    """The party ({slot: (species, level, ball)}) the log ends with, every state it gave (party, boxes, dex caught) in
    order - the save's, the test party's, the end's - and the Master Balls left (the last "bag field" count, or None)."""
    party, states, balls = {}, [], None
    for line in lines:
        if line.startswith("breed party "):
            slot = numbers(line.split(":", 1)[0])[0]
            if line.endswith(": none"):
                party.pop(slot, None)
            else:
                v = numbers(line.split(":", 1)[1])     # species S egg E level L ivs a..f nature T num A ball B ...
                party[slot] = (v[0], v[2], v[11])
        elif line.startswith("breed state: "):
            n = numbers(line)                          # party N daycare D egg waiting W boxes B dex caught C
            if len(n) >= 5:
                states.append(dict(party=n[0], boxes=n[3], caught=n[4]))
        elif line.startswith("bag field: "):
            n = numbers(line)                          # money M item I count C
            balls = n[2]
    return party, states, balls


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

    def step(name, save, frames, extra, shots=""):
        rundir = os.path.join(args.out, name)
        saved, args.save = args.save, save
        env = dict(PKMN_TEST_BATTLE=f"field:{frames}", PKMN_TEST_BREED_LOG="1", PKMN_TEST_BAG_LOG=str(ITEM_MASTER_BALL),
                   PKMN_TEST_FREEZE_FIELD="1", PKMN_TEST_AUTOPILOT="1", PKMN_TEST_BATTLE_DELAY="100",
                   PKMN_TEST_MEGA_SHOT_STEM=os.path.join(rundir, "step"))
        env.update(extra)                         # a battle instead of the field alone
        if shots:
            env["PKMN_TEST_FIELD_SHOTS"] = shots
        try:
            process, output = start(args, rundir, env)
            code = finish(process, output, frames / 60 + 300)
        finally:
            args.save = saved
        lines = driver_lines(os.path.join(rundir, "test.log"))
        party, states, balls = catch_lines(lines)
        steps.append(dict(step=name, exit=code, party={str(k): v for k, v in sorted(party.items())}, states=states,
                          master_balls=balls,
                          battles=[line for line in lines if line.startswith("battle over")],
                          notes=[line for line in lines if line.startswith(("cannot", "assert"))],
                          pictures=sorted(f for f in os.listdir(rundir) if f.endswith(".png"))))
        if code != 0:
            problems.append(f"{name}: the game ended with {code}")
        problems.extend(f"{name}: {line}" for line in steps[-1]["notes"])
        return rundir, party, states, balls

    def expect(name, got, want):
        if got != want:
            problems.append(f"{name}: {got}, expected {want}")

    def catch(name, party_size, species):
        team = ",".join([f"{SPECIES_ZIGZAGOON}:10"] * party_size)
        frames = SAVE_AT + 900
        return step(name, args.save, frames, dict(
            PKMN_TEST_BATTLE=f"wild:{species}:{WILD_LEVEL}", PKMN_TEST_PARTY=team, PKMN_TEST_BAG=f"{ITEM_MASTER_BALL}:1",
            PKMN_TEST_BAG_RESET="1",
            PKMN_TEST_KEYS=",".join(KEYS), PKMN_TEST_FIGHT="120", PKMN_TEST_FIELD_AFTER=str(frames),
            PKMN_TEST_FIELD_KEYS=",".join(save_keys(SAVE_AT))), shots="100,400,800")

    def caught(name, battles):
        if not any("outcome 7 " in line for line in battles):     # B_OUTCOME_CAUGHT
            problems.append(f"{name}: the battle did not end with the catch: {battles}")

    def around(name, states, party_size):
        """The state as the battle began (the test party given) and the last one; (None, None) if the log lacks them."""
        before = next((state for state in states if state["party"] == party_size), None)
        if before is None or not states:
            problems.append(f"{name}: no party of {party_size} in the log: {states}")
            return dict(party=party_size, boxes=0, caught=0), dict(party=0, boxes=-1, caught=-1)
        return before, states[-1]

    # 1-2. Caught into the party (one place left), saved and loaded
    rundir, party, states, balls = catch("catch", 5, SPECIES_MAGIKARP)
    caught("catch", steps[-1]["battles"])
    before, last = around("catch", states, 5)
    expect("catch (the party after)", last["party"], 6)
    expect("catch (the caught Pokemon)", party.get(5), (SPECIES_MAGIKARP, WILD_LEVEL, ITEM_MASTER_BALL))
    expect("catch (the PC)", last["boxes"], before["boxes"])
    expect("catch (the Pokedex's caught species)", last["caught"], before["caught"] + 1)
    expect("catch (Master Balls left)", balls, 0)
    _, party, states, balls = step("catch-load", os.path.join(rundir, "test.sav"), 60, {})
    loaded = states[-1] if states else dict(party=0, boxes=-1, caught=-1)
    expect("catch-load (the caught Pokemon)", party.get(5), (SPECIES_MAGIKARP, WILD_LEVEL, ITEM_MASTER_BALL))
    expect("catch-load (the party)", loaded["party"], 6)
    expect("catch-load (the Pokedex's caught species)", loaded["caught"], before["caught"] + 1)
    expect("catch-load (Master Balls left)", balls, 0)

    # 3-4. Caught with a full party: into the PC
    rundir, party, states, balls = catch("catch-full", 6, SPECIES_POOCHYENA)
    caught("catch-full", steps[-1]["battles"])
    before, last = around("catch-full", states, 6)
    expect("catch-full (the party after)", last["party"], 6)
    expect("catch-full (the party's species)", sorted({entry[0] for entry in party.values()}), [SPECIES_ZIGZAGOON])
    expect("catch-full (the PC)", last["boxes"], before["boxes"] + 1)
    expect("catch-full (the Pokedex's caught species)", last["caught"], before["caught"] + 1)
    expect("catch-full (Master Balls left)", balls, 0)
    _, party, states, _ = step("catch-full-load", os.path.join(rundir, "test.sav"), 60, {})
    loaded = states[-1] if states else dict(party=0, boxes=-1, caught=-1)
    expect("catch-full-load (the PC)", loaded["boxes"], before["boxes"] + 1)
    expect("catch-full-load (the party)", loaded["party"], 6)
    expect("catch-full-load (the Pokedex's caught species)", loaded["caught"], before["caught"] + 1)

    report = dict(steps=steps, problems=problems, passed=not problems)
    with open(os.path.join(args.out, "catch_check.json"), "w") as out:
        json.dump(report, out, indent=1)
    print(json.dumps(dict(passed=not problems, problems=problems)))
    return 0 if not problems else 1


if __name__ == "__main__":
    sys.exit(main())
