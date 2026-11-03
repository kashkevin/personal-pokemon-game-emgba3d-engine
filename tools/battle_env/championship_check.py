#!/usr/bin/env python3
"""Step 18: a Championship match against a computer trainer, in real game processes (test builds; Linux and Windows).

The host starts such a match with the Battle Tower's solo command in mode 2 (src/tower_legends.c; the app's
docs/championships-eight.md): the floor's solo trainer is one of the Championship's eight places. The test build's
driver starts it the same way without a host (PKMN_TEST_BATTLE=tower:<floor> with PKMN_TEST_CHAMPIONSHIP_ROUND=1..3 and
PKMN_TEST_CHAMPIONSHIP_EXP=0/1: TowerLegends_TestStartChampionship), and the battle is fought by the driver's autopilot
(PKMN_TEST_TOWER_WIN for a won run: tools/battle_env/tower_check.py says what it does; a level-1 Magikarp alone loses).

Checked for each run, from the game's own log (nothing reads its memory): the field message the script shows names the
round and the floor's trainer ("QUARTERFINAL! TUBER|LARRY wants to battle!") and never a floor; the battle is against the
floor's solo trainer; its result names the round ("You won the QUARTERFINAL!" / "You lost the SEMIFINAL.") and nothing
asks to go on to a floor; the result waits in the save's Tower record for its ticket and floor (the host settles it); no
money moved; the party is healed; and experience and effort values: unchanged when the command did not ask for them, the
ordinary award when it did. A plain Tower floor (no round) is the control: it still shows its floor and gives experience.

    tools/battle_env/championship_check.py --exe ./pokeemerald-mp-diorama-bfxtest --save town.sav --out build/champ-check \\
        [--display :99] [--speed 4]
"""
import argparse
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mega_check import driver_lines, finish, numbers, start  # noqa: E402
from tower_check import DATA, TICKET, parse  # noqa: E402

LEVEL = 50
WIN_PARTY = f"150:{LEVEL}"            # one level-50 Mewtwo (Slow growth: 5 n^3 / 4 experience at level n)
LOSE_PARTY = "129:1"                  # a level-1 Magikarp: Splash
ROUNDS = {1: "QUARTERFINAL", 2: "SEMIFINAL", 3: "FINAL"}
CLASS_NAMES = {"PKMN_RANGER": "PKMN RANGER", "BATTLE_GIRL": "BATTLE GIRL", "TUBER_M": "TUBER", "TUBER_F": "TUBER",
               "SWIMMER_M": "SWIMMER", "SWIMMER_F": "SWIMMER", "PKMN_BREEDER": "PKMN BREEDER", "RICH_BOY": "RICH BOY",
               "PARASOL_LADY": "PARASOL LADY", "BUG_MANIAC": "BUG MANIAC", "BLACK_BELT": "BLACK BELT",
               "RUIN_MANIAC": "RUIN MANIAC", "COOLTRAINER": "COOLTRAINER"}
# (name, floor, round, award experience, won run, expected experience: "same" / "gained")
CASES = (
    ("quarterfinal-won-no-exp", 5, 1, False, True, "same"),
    ("semifinal-lost", 11, 2, False, False, "same"),
    ("final-won-exp-asked", 6, 3, True, True, "gained"),
    ("tower-floor-control", 5, 0, True, True, "gained"),
)


def slow_exp(level):
    return 5 * level ** 3 // 4


def class_name(solo):
    raw = solo["class"].removeprefix("TRAINER_CLASS_")
    return CLASS_NAMES.get(raw, raw.replace("_", " "))


def run_case(args, data, case):
    name, floor, round_, exp, won, expect = case
    rundir = os.path.join(args.out, name)
    env = {"PKMN_TEST_BATTLE": f"tower:{floor}", "PKMN_TEST_AUTOPILOT": "1", "PKMN_TEST_FIGHT": "420",
           "PKMN_TEST_PARTY": WIN_PARTY if won else LOSE_PARTY}
    if round_:
        env["PKMN_TEST_CHAMPIONSHIP_ROUND"] = str(round_)
        env["PKMN_TEST_CHAMPIONSHIP_EXP"] = "1" if exp else "0"
    if won:
        env["PKMN_TEST_TOWER_WIN"] = "1"
    else:
        env["PKMN_TEST_LEAD_ONLY"] = "1"
    if args.speed > 1:
        env["PKMN_TEST_SPEED"] = str(args.speed)
    began = time.monotonic()
    process, output = start(args, rundir, env)
    code = finish(process, output, 600)
    lines = driver_lines(os.path.join(rundir, "test.log"))
    run = parse(lines)
    texts = [line.split("tower text: ", 1)[1] for line in lines if line.startswith("tower text: ")]
    started = [numbers(line) for line in lines if line.startswith("tower championship round")]
    evs = [numbers(line)[-1] for line in lines if line.startswith("tower own ")]
    solo = data["floors"][floor - 1]["solo"]
    problems = []
    if round_ and [n[:3] for n in started[:1]] != [[round_, int(exp), floor]]:
        problems.append(f"the match did not start as round {round_}: {started}")
    battle = run["battle"]
    if battle is None:
        problems.append(f"no battle started (exit {code}; {'; '.join(run['notes']) or 'nothing logged'})")
    elif battle["floor"] != floor or battle["cls"] != solo["classId"] or battle["pic"] != solo["picId"]:
        problems.append(f"the battle: floor {battle['floor']} class {battle['cls']} pic {battle['pic']}")
    if run["outcome"] != (1 if won else 2):
        problems.append(f"outcome {run['outcome']} (exit {code})")
    if round_:
        intro = f"{ROUNDS[round_]}! {class_name(solo)}|{solo['name']} wants to battle!"
        result = (f"You won the {ROUNDS[round_]}!" if won else f"You lost the {ROUNDS[round_]}.") \
            + "|The host records it in PLAYER."
        if intro not in texts:
            problems.append(f"no intro {intro!r} (texts: {texts})")
        if result not in texts:
            problems.append(f"no result {result!r} (texts: {texts})")
        if any("FLOOR" in text.upper() or "Go on" in text or "Try floor" in text for text in texts):
            problems.append(f"a floor or a go-on question shown: {texts}")
    elif not any(text.startswith(f"FLOOR {floor}") for text in texts):
        problems.append(f"the control floor's own intro is missing: {texts}")
    end = run["end"]
    if end is None:
        problems.append("no end logged (the field did not come back)")
    else:
        if (end["record"], end["pending"], end["ticket"], end["pendingFloor"]) != (1, 2 if won else 3, TICKET(floor), floor):
            problems.append(f"the save's record: valid {end['record']} pending {end['pending']} ticket {end['ticket']:08x} "
                            f"floor {end['pendingFloor']}")
        if end["money"] != end["was"]:
            problems.append(f"money moved: {end['was']} -> {end['money']}")
    for own in run["own"]:
        if own["hp"] != own["maxhp"]:
            problems.append(f"own Pokemon {own['slot'] + 1} not healed: {own['hp']}/{own['maxhp']}")
    before = slow_exp(LEVEL) if won else 0
    after = run["own"][0]["exp"] if run["own"] else None
    if won and after is None:
        problems.append("no own party logged")
    elif won and expect == "same" and (after != before or evs[:1] != [0]):
        problems.append(f"experience {before} -> {after}, effort values {evs[:1]}: expected no change")
    elif won and expect == "gained" and not after > before:
        problems.append(f"experience {before} -> {after}: expected the ordinary award")
    return dict(case=name, floor=floor, round=round_, exp_asked=exp, won=won, exit=code, texts=texts,
                exp_before=before if won else None, exp_after=after, evs=evs[:1],
                seconds=round(time.monotonic() - began, 1), passed=not problems, problems=problems)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", required=True)
    parser.add_argument("--save", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--data", default=DATA)
    parser.add_argument("--display", default=os.environ.get("DISPLAY"))
    parser.add_argument("--speed", type=int, default=1)
    parser.add_argument("--cases", default=",".join(case[0] for case in CASES))
    args = parser.parse_args()
    args.exe, args.save = os.path.abspath(args.exe), os.path.abspath(args.save)
    os.makedirs(args.out, exist_ok=True)
    with open(args.data) as source:
        data = json.load(source)
    wanted = set(args.cases.split(","))
    results = [run_case(args, data, case) for case in CASES if case[0] in wanted]
    with open(os.path.join(args.out, "championship_check.json"), "w") as out:
        json.dump(results, out, indent=1)
    for r in results:
        print(f"{'passed' if r['passed'] else 'FAILED'} {r['case']} ({r['seconds']} s)"
              + ("" if r["passed"] else ": " + "; ".join(r["problems"])))
    failed = sum(not r["passed"] for r in results)
    print(f"championship_check: {len(results)} runs, {failed} failed")
    sys.exit(1 if failed or not results else 0)


if __name__ == "__main__":
    main()
