#!/usr/bin/env python3
"""Parting Shot under the Gen 6 rule (the owner's decision of 2026-11-14, #63), in real game processes (test builds;
Linux and Windows alike; engine src/battle_move_resolution.c PartingShotReachedTarget, PC only).

The rule: the user of Parting Shot switches out once the move reaches its target - whether or not the stat drops take
(an Ability that stops one, stats already at their lowest) - and not when the move is blocked or bounced back. With
Magic Coat or Magic Bounce the move comes back at its user (its Attack and Sp. Atk drop) and the Pokemon that bounced
it is the one that switches out. Before this the user switched out whenever it could (upstream's pre-Gen 7 rule; the
GBA build keeps it: docs/gba-test-expected-failures.md rows 27-29).

Everything is read from the game's own log (the battle-environment test driver, include/diorama/battle_env_test.h, TEST
BUILDS ONLY): PKMN_TEST_STAGE_LOG (each battler's party member and stat stages whenever they change), the player's
choices PKMN_TEST_TURNS (p1: the party's second Pokemon when the game opens the party screen itself - "no party screen
came for p1" when it does not), and for the trainer cases PKMN_TEST_FOE_PARTY (the opponent's party: the Magic Coat or
Magic Bounce user and a second Pokemon to switch to). The player's Pangoro (Iron Fist: not Mold Breaker, which goes
through Soundproof and Magic Bounce) knows Parting Shot and Body Slam, its partner Arcanine Body Slam; both level 100.

Cases:
  hit           a wild Zigzagoon (Splash): its Attack and Sp. Atk drop and Pangoro switches out to Arcanine (control)
  hyper_cutter  a wild Krabby with Hyper Cutter: its Attack stays, its Sp. Atk drops, Pangoro still switches out (the
                move reached it; the Gen 6 rule the upstream tests "(Gen6)" hold)
  soundproof    a wild Exploud with Soundproof: the move is blocked, no stat moves, Pangoro stays in
  magic_coat    a trainer's Wobbuffet that knows only Magic Coat, then Wynaut: the move bounces back - Pangoro's Attack and
                Sp. Atk drop, Pangoro stays in, and the Wobbuffet switches out (to Wynaut)
  magic_bounce  a trainer's Espeon with Magic Bounce (its hidden Ability), then Wynaut: as magic_coat

    tools/battle_env/parting_shot_check.py --exe ./pokeemerald-mp-bfxtest --save town.sav --out build/parting-shot

Exit 0 when every check holds; the report is OUT/parting_shot_check.json, each case's log in OUT/<case>/test.log.
"""
import argparse
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mega_check import driver_lines, finish, start  # noqa: E402

SPECIES = dict(PANGORO=675, ARCANINE=59, ZIGZAGOON=263, KRABBY=98, EXPLOUD=295, WOBBUFFET=202, WYNAUT=360, ESPEON=196)
MOVE = dict(PARTING_SHOT=575, BODY_SLAM=34, SPLASH=150, MAGIC_COAT=277)
TRAINER_ALLEN = 333  # include/constants/opponents.h: his party is replaced by PKMN_TEST_FOE_PARTY
PLAYER, OPPONENT = 0, 1  # GetBattlerTrainer's numbers in the stage log (B_TRAINER_PLAYER, B_TRAINER_OPPONENT_A)


def moves(*names):
    return "V" + ".".join(str(MOVE[n]) for n in names)


# Pangoro's Ability is Iron Fist (slot 0): its slot 1, Mold Breaker, goes through Soundproof and Magic Bounce (the first
# run of this check met it, the personality the test makes choosing slot 1)
PARTY = ",".join((f"{SPECIES['PANGORO']}:100:a0{moves('PARTING_SHOT', 'BODY_SLAM')}",
                  f"{SPECIES['ARCANINE']}:100:{moves('BODY_SLAM')}"))

# expect: who switches ("user", "foe" or nobody: None), the foe's (ATK, SPA) stages and the user's after Parting Shot
CASES = {
    "hit": dict(env=dict(PKMN_TEST_BATTLE=f"wild:{SPECIES['ZIGZAGOON']}:30", PKMN_TEST_FOE=moves("SPLASH")),
                turns="m0,p1,m0", switch="user", foe=(-1, -1), user=(0, 0)),
    "hyper_cutter": dict(env=dict(PKMN_TEST_BATTLE=f"wild:{SPECIES['KRABBY']}:30", PKMN_TEST_FOE="a0" + moves("SPLASH")),
                         turns="m0,p1,m0", switch="user", foe=(0, -1), user=(0, 0)),
    "soundproof": dict(env=dict(PKMN_TEST_BATTLE=f"wild:{SPECIES['EXPLOUD']}:30", PKMN_TEST_FOE="a0" + moves("SPLASH")),
                       turns="m0,p1,m1", switch=None, foe=(0, 0), user=(0, 0)),
    "magic_coat": dict(env=dict(PKMN_TEST_BATTLE=f"trainer:{TRAINER_ALLEN}",
                                PKMN_TEST_FOE_PARTY=",".join((f"{SPECIES['WOBBUFFET']}:50:{moves('MAGIC_COAT')}",
                                                              f"{SPECIES['WYNAUT']}:50:{moves('SPLASH')}"))),
                       turns="m0,p1,m1", switch="foe", foe=(0, 0), user=(-1, -1)),
    "magic_bounce": dict(env=dict(PKMN_TEST_BATTLE=f"trainer:{TRAINER_ALLEN}",
                                  PKMN_TEST_FOE_PARTY=",".join((f"{SPECIES['ESPEON']}:50:a2{moves('SPLASH')}",
                                                                f"{SPECIES['WYNAUT']}:50:{moves('SPLASH')}"))),
                         turns="m0,p1,m1", switch="foe", foe=(0, 0), user=(-1, -1)),
}

BATTLER = re.compile(r"stages battler (\d+) trainer/slot/species/hp/shown/engine (\d+) (\d+) (\d+) (\d+) ([-\d,]+) "
                     r"([-\d,]+)")
START = re.compile(r"stages battle (\d+) start")
END = re.compile(r"stages battle (\d+) end, outcome (-?\d+)")
CHOOSE = re.compile(r"turns: battler (\d+) chooses (\w)(\d+)")
NO_PARTY = re.compile(r"turns: no party screen came for p(\d+)")
FOE = re.compile(r"foe (\d+) species (\d+) level (\d+) ability (\d+) moves")


def engine_stages(text):
    """(ATK, SPA) of the log's engine stages (ATK, DEF, SPE, SPA, SPD, ACC, EVA)."""
    values = [int(v) for v in text.split(",")]
    return values[0], values[3]


def read(lines):
    """The first battle's events: each battler's (trainer, party slot, species, (ATK, SPA)) as it changes, the turns
    chosen and the party screens that did not come, in order."""
    events, battles, outcome = [], 0, None
    for line in lines:
        if START.search(line):
            battles += 1
            continue
        if battles != 1:
            continue
        if m := END.search(line):
            outcome = int(m.group(2))
        elif m := BATTLER.search(line):
            b, trainer, slot, species, hp = (int(m.group(i)) for i in range(1, 6))
            if species:
                events.append(("battler", b, trainer, slot, species, engine_stages(m.group(7))))
        elif m := CHOOSE.search(line):
            events.append(("choose", int(m.group(1)), m.group(2), int(m.group(3))))
        elif m := NO_PARTY.search(line):
            events.append(("no_party", int(m.group(1))))
    return events, battles, outcome


def after_parting_shot(events):
    """Each side's battler as it stood once Parting Shot's turn was over (before the next choice): (party slot,
    species, (ATK, SPA)) for the player's and the opponent's; and the party slots each side's battler held."""
    first_choice = next((i for i, e in enumerate(events) if e[0] == "choose" and e[2] == "m" and e[3] == 0), None)
    if first_choice is None:
        return None, None, None, None
    later = next((i for i, e in enumerate(events) if i > first_choice and e[0] in ("choose", "no_party")), len(events))
    state, before = {}, {}
    for i, e in enumerate(events[:later]):
        if e[0] != "battler":
            continue
        _, b, trainer, slot, species, stages = e
        state[trainer] = (slot, species, stages)
        if i < first_choice:
            before[trainer] = (slot, species, stages)
    return before.get(PLAYER), before.get(OPPONENT), state.get(PLAYER), state.get(OPPONENT)


def run_case(args, name):
    case = CASES[name]
    rundir = os.path.join(args.out, name)
    env = dict(case["env"], PKMN_TEST_PARTY=PARTY, PKMN_TEST_TURNS=case["turns"], PKMN_TEST_STAGE_LOG="1",
               PKMN_TEST_FIGHT=str(args.fight), PKMN_TEST_FREEZE_FIELD="1", PKMN_TEST_AUTOPILOT="1",
               PKMN_TEST_SCENE_OFF="1")
    process, output = start(args, rundir, env)
    code = finish(process, output, args.fight + 240)
    lines = driver_lines(os.path.join(rundir, "test.log"))
    events, battles, outcome = read(lines)
    user0, foe0, user1, foe1 = after_parting_shot(events)
    no_party = any(e[0] == "no_party" and e[1] == 1 for e in events)
    foes = [tuple(int(v) for v in m.groups()) for m in (FOE.search(line) for line in lines) if m]
    checks = [("the game ended after its battle (exit 0)", code == 0, code),
              ("one battle ran", battles >= 1, battles),
              ("the player's Pangoro and the foe were on the field before Parting Shot",
               user0 is not None and user0[1] == SPECIES["PANGORO"] and foe0 is not None, (user0, foe0)),
              ("Parting Shot's turn was played", user1 is not None and foe1 is not None, (user1, foe1))]
    if "PKMN_TEST_FOE_PARTY" in case["env"]:
        checks.append(("the opponent's party is the test's (two Pokemon)", len(foes) == 2, foes))
    if user1 is not None and foe1 is not None and user0 is not None and foe0 is not None:
        user_switched = user1[0] != user0[0]
        foe_switched = foe1[0] != foe0[0]
        if case["switch"] == "user":
            checks.append(("the user switched out (the party screen came; Arcanine in)",
                           user_switched and user1[1] == SPECIES["ARCANINE"] and not no_party, (user0, user1, no_party)))
            checks.append(("the foe stayed in", not foe_switched, (foe0, foe1)))
        elif case["switch"] == "foe":
            checks.append(("the user stayed in (no party screen came for it)", not user_switched and no_party,
                           (user0, user1, no_party)))
            checks.append(("the Pokemon that bounced it back switched out (its second Pokemon in)",
                           foe_switched and foe1[1] == SPECIES["WYNAUT"], (foe0, foe1)))
        else:
            checks.append(("the user stayed in (no party screen came)", not user_switched and no_party,
                           (user0, user1, no_party)))
            checks.append(("the foe stayed in", not foe_switched, (foe0, foe1)))
        foe_stages = foe1[2] if not foe_switched else foe0[2]
        if case["switch"] == "foe":
            # the foe that bounced it left; its stages are read from its last line on the field
            foe_lines = [e for e in events if e[0] == "battler" and e[2] == OPPONENT and e[3] == foe0[0]]
            foe_stages = foe_lines[-1][5] if foe_lines else foe0[2]
        checks.append((f"the foe's Attack and Sp. Atk stages {case['foe']}", foe_stages == case["foe"], foe_stages))
        user_lines = [e for e in events if e[0] == "battler" and e[2] == PLAYER and e[3] == user0[0]]
        user_stages = user_lines[-1][5] if user_lines else user0[2]
        checks.append((f"the user's Attack and Sp. Atk stages {case['user']}", user_stages == case["user"], user_stages))
    print(f"{name}: exit {code}, {battles} battle(s), outcome {outcome}")
    for check, ok, detail in checks:
        print(f"  {'ok  ' if ok else 'FAIL'} {check}" + ("" if ok else f": {detail}"))
    return dict(case=name, exit=code, outcome=outcome, checks=[dict(check=c, ok=bool(ok), detail=repr(d))
                                                              for c, ok, d in checks])


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", required=True)
    parser.add_argument("--save", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--display", default=os.environ.get("DISPLAY"))
    parser.add_argument("--cases", default=",".join(CASES))
    parser.add_argument("--fight", type=int, default=300, help="seconds a case's battle may take")
    args = parser.parse_args()
    args.exe, args.save = os.path.abspath(args.exe), os.path.abspath(args.save)
    os.makedirs(args.out, exist_ok=True)
    results = [run_case(args, name) for name in args.cases.split(",") if name]
    passed = all(c["ok"] for r in results for c in r["checks"])
    with open(os.path.join(args.out, "parting_shot_check.json"), "w") as out:
        json.dump(dict(passed=passed, cases=results), out, indent=1)
    print(json.dumps(dict(passed=passed, failed=[f"{r['case']}: {c['check']}" for r in results for c in r["checks"]
                                                 if not c["ok"]])))
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
