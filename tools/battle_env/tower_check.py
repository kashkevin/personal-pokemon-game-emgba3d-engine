#!/usr/bin/env python3
"""Battle Tower: Legends, every floor in real game processes (test builds; Linux and Windows alike).

Each floor is fought twice through the game's own start of a solo floor (PKMN_TEST_BATTLE=tower:<floor>,
src/tower_legends.c: the floor's script and intro, the battle against the floor's trainer, its end), from a save, with
nobody at the keyboard (the battle-environment driver's autopilot, include/diorama/battle_env_test.h):

  won   six level-100 Mewtwo; PKMN_TEST_TOWER_WIN, a test advantage in memory only: at each of the player's turns the
        active Pokemon's HP is full again, it knows Ice Beam, Flamethrower, Shadow Ball and Earthquake and the cursor is
        on the one that does most against the opposing Pokemon. The opposing side is the game's own: its AI, its moves,
        its items, its Mega Evolution.
  lost  a level-1 Magikarp alone (Splash): the floor's trainer wins.

Everything is read from the game's own log (nothing reads its memory). Checked for each run, against the floor table
(tools/tower_legends/tower_legends.json): the battle started on that floor against its solo trainer (class, picture);
every opposing Pokemon as the battle built it (species, level = the floor, held item, Ability when the table names one,
the four moves); the battle ended (no freeze, no crash) with the expected outcome; the result waits in the save's Tower
record (won 2 / lost 3) for the floor and its ticket; no money moved; the party is healed and holds its items again; a
Mega Stone holder of the floor that came into battle Mega Evolved (PKMN_TEST_MEGA_LOG) in the won run. Exit 0 when every
check of every run passed; the report is OUT/tower_check.json and a floor-by-floor OUT/tower_check.md.

    tools/battle_env/tower_check.py --exe ./pokeemerald-mp-bfxtest --save town.sav --out build/tower-check \\
        [--floors 1-100] [--runs won,lost] [--jobs 3] [--display :99] [--make-save]
"""
import argparse
import concurrent.futures
import json
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mega_check import driver_lines, finish, make_save, numbers, start  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(HERE, "..", "tower_legends", "tower_legends.json")
WIN_PARTY = ",".join(["150:100"] * 6)     # SPECIES_MEWTWO
LOSE_PARTY = "129:1"                      # SPECIES_MAGIKARP: Splash
TICKET = lambda floor: 0x01000000 | (floor << 8) | 0x7E   # TowerLegends_TestStartSolo's test ticket
FIGHT_SECONDS = {"won": 420, "lost": 300}


def floors_arg(text):
    out = []
    for part in text.split(","):
        first, _, last = part.partition("-")
        out.extend(range(int(first), int(last or first) + 1))
    return [f for f in out if 1 <= f <= 100]


def parse(lines):
    """The driver's Tower lines: the battle, the foes as built, the end, the own party, the Megas, the outcome."""
    run = {"battle": None, "foes": [], "end": None, "own": [], "outcome": None, "megas": [], "notes": []}
    for line in lines:
        if line.startswith("tower battle floor"):
            floor, mode, opponent, cls, pic, party = numbers(line)
            run["battle"] = dict(floor=floor, mode=mode, opponent=opponent, cls=cls, pic=pic, party=party)
        elif line.startswith("tower foe "):
            n = numbers(line)
            run["foes"].append(dict(slot=n[0], species=n[1], level=n[2], item=n[3], ability=n[4], moves=n[5:9],
                                    hp=n[9], maxhp=n[10]))
        elif line.startswith("tower end floor"):
            n = numbers(line)
            run["end"] = dict(floor=n[0], pending=n[1], ticket=int(re.search(r"ticket ([0-9a-f]+)", line).group(1), 16),
                              pendingFloor=n[3], record=n[4], money=n[5], was=n[6], cap=n[7])
        elif line.startswith("tower own "):
            n = numbers(line)
            run["own"].append(dict(slot=n[0], species=n[1], level=n[2], item=n[3], hp=n[9], maxhp=n[10]))
        elif line.startswith("battle over, outcome"):
            run["outcome"] = numbers(line)[0]
        elif "mega" in line.lower() and ("evolv" in line.lower() or "became" in line.lower()):
            run["megas"].append(line)
        elif line.startswith(("the battle did not end", "assert", "cannot read")):
            run["notes"].append(line)
    return run


def judge(floor, kind, data, run, code):
    """The problems of one run (an empty list: every check passed)."""
    solo = data["floors"][floor - 1]["solo"]
    problems = []
    battle = run["battle"]
    if battle is None:
        return [f"no Tower battle started (exit {code}; {'; '.join(run['notes']) or 'nothing logged'})"]
    if battle["floor"] != floor or battle["mode"] != 1:
        problems.append(f"the battle was floor {battle['floor']} mode {battle['mode']}")
    if battle["cls"] != solo["classId"] or battle["pic"] != solo["picId"]:
        problems.append(f"trainer class/picture {battle['cls']}/{battle['pic']}, the table's {solo['classId']}/{solo['picId']}")
    if len(run["foes"]) != len(solo["mons"]):
        problems.append(f"{len(run['foes'])} opposing Pokemon, the table's {len(solo['mons'])}")
    for foe, mon in zip(run["foes"], solo["mons"]):
        want_moves = list(mon["moveIds"]) + [0] * (4 - len(mon["moveIds"]))
        if (foe["species"], foe["level"], foe["item"]) != (mon["speciesId"], floor, mon.get("itemId", 0)):
            problems.append(f"foe {foe['slot'] + 1}: species/level/item {foe['species']}/{foe['level']}/{foe['item']}, "
                            f"the table's {mon['speciesId']}/{floor}/{mon.get('itemId', 0)}")
        if mon.get("abilityId") and foe["ability"] != mon["abilityId"]:
            problems.append(f"foe {foe['slot'] + 1}: Ability {foe['ability']}, the table's {mon['abilityId']}")
        if foe["moves"] != want_moves:
            problems.append(f"foe {foe['slot'] + 1}: moves {foe['moves']}, the table's {want_moves}")
        if foe["hp"] != foe["maxhp"] or foe["hp"] == 0:
            problems.append(f"foe {foe['slot'] + 1}: started with {foe['hp']}/{foe['maxhp']} HP")
    expected = 1 if kind == "won" else 2
    if run["outcome"] != expected:
        problems.append(f"outcome {run['outcome']} (exit {code}), expected {expected}; {'; '.join(run['notes'])}")
    end = run["end"]
    if end is None:
        problems.append("no end logged (the field did not come back)")
    else:
        if (end["record"], end["pending"], end["ticket"], end["pendingFloor"]) != (1, 2 if kind == "won" else 3, TICKET(floor), floor):
            problems.append(f"the save's record: valid {end['record']} pending {end['pending']} ticket {end['ticket']:08x} "
                            f"floor {end['pendingFloor']}")
        if end["money"] != end["was"]:
            problems.append(f"money moved: {end['was']} -> {end['money']}")
    for own in run["own"]:
        if own["hp"] != own["maxhp"]:
            problems.append(f"own Pokemon {own['slot'] + 1} not healed: {own['hp']}/{own['maxhp']}")
    if kind == "won":
        holders = [m for m in solo["mons"] if m.get("mega")]
        if holders and not run["megas"]:
            problems.append(f"{len(holders)} Mega Stone holder(s) and no Mega Evolution logged")
    return problems


def run_one(args, data, floor, kind):
    rundir = os.path.join(args.out, f"floor-{floor:03d}-{kind}")
    env = {"PKMN_TEST_BATTLE": f"tower:{floor}", "PKMN_TEST_AUTOPILOT": "1", "PKMN_TEST_FIGHT": str(FIGHT_SECONDS[kind]),
           "PKMN_TEST_MEGA_LOG": "1", "PKMN_TEST_PARTY": WIN_PARTY if kind == "won" else LOSE_PARTY}
    if kind == "won":
        env["PKMN_TEST_TOWER_WIN"] = "1"
    else:
        env["PKMN_TEST_LEAD_ONLY"] = "1"
    began = time.monotonic()
    process, output = start(args, rundir, env)
    code = finish(process, output, FIGHT_SECONDS[kind] + 180)
    run = parse(driver_lines(os.path.join(rundir, "test.log")))
    problems = judge(floor, kind, data, run, code)
    return dict(floor=floor, run=kind, exit=code, seconds=round(time.monotonic() - began, 1), passed=not problems,
                problems=problems, foes=len(run["foes"]), megas=len(run["megas"]))


def report(args, data, results):
    by_floor = {}
    for r in results:
        by_floor.setdefault(r["floor"], {})[r["run"]] = r
    lines = ["# Battle Tower: Legends - floor-by-floor check", "",
             f"Executable: `{os.path.basename(args.exe)}`; floor table hash {data['hash']}; runs: {args.runs}.", "",
             "| Floor | Kind | Trainer | Team | Won run | Lost run | Megas seen |", "| --- | --- | --- | --- | --- | --- | --- |"]
    for floor in sorted(by_floor):
        f = data["floors"][floor - 1]
        cells = []
        for kind in ("won", "lost"):
            r = by_floor[floor].get(kind)
            cells.append("-" if r is None else ("passed" if r["passed"] else "FAILED: " + "; ".join(r["problems"])[:200])
                         + ("" if r is None else f" ({r['seconds']} s)"))
        megas = by_floor[floor].get("won", {}).get("megas", "-")
        lines.append(f"| {floor} | {f['kind']} | {f['solo']['name']} | {len(f['solo']['mons'])} | {cells[0]} | {cells[1]} | {megas} |")
    with open(os.path.join(args.out, "tower_check.md"), "w") as out:
        out.write("\n".join(lines) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", required=True)
    parser.add_argument("--save")
    parser.add_argument("--out", required=True)
    parser.add_argument("--data", default=DATA)
    parser.add_argument("--floors", default="1-100")
    parser.add_argument("--runs", default="won,lost")
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--display", default=os.environ.get("DISPLAY"))
    parser.add_argument("--make-save", action="store_true")
    args = parser.parse_args()
    args.exe = os.path.abspath(args.exe)
    os.makedirs(args.out, exist_ok=True)
    if args.make_save:
        args.save = make_save(args, os.path.join(args.out, "made.sav"))
    if not args.save:
        sys.exit("--save or --make-save")
    args.save = os.path.abspath(args.save)
    with open(args.data) as source:
        data = json.load(source)
    work = [(floor, kind) for floor in floors_arg(args.floors) for kind in args.runs.split(",")]
    results = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        futures = {pool.submit(run_one, args, data, floor, kind): (floor, kind) for floor, kind in work}
        for future in concurrent.futures.as_completed(futures):
            result = future.result()
            results.append(result)
            print(json.dumps(result), flush=True)
    results.sort(key=lambda r: (r["floor"], r["run"]))
    passed = sum(r["passed"] for r in results)
    summary = {"hash": data["hash"], "runs": len(results), "passed": passed, "failed": len(results) - passed,
               "results": results}
    with open(os.path.join(args.out, "tower_check.json"), "w") as out:
        json.dump(summary, out, indent=1)
    report(args, data, results)
    print(json.dumps({"passed": passed, "of": len(results)}))
    return 0 if passed == len(results) else 1


if __name__ == "__main__":
    sys.exit(main())
