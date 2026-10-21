#!/usr/bin/env python3
"""Battle Tower: Legends, every floor in real game processes (test builds; Linux and Windows alike).

Each floor is fought twice through the game's own start of a solo floor (PKMN_TEST_BATTLE=tower:<floor>,
src/tower_legends.c: the floor's script and intro, the battle against the floor's trainer, its end), from a save, with
nobody at the keyboard (the battle-environment driver's autopilot, include/diorama/battle_env_test.h):

  won   six level-100 Mewtwo; PKMN_TEST_TOWER_WIN, a test advantage in memory only: at each of the player's turns the
        active Pokemon's HP is full again, its battle stats and accuracy are +6, it knows Ice Beam, Flamethrower, Shadow
        Ball and Earthquake and the cursor is on the one that does most against the opposing Pokemon. The opposing side
        is the game's own: its AI, its moves, its items, its Mega Evolution.
  lost  a level-1 Magikarp alone (Splash): the floor's trainer wins.

Everything is read from the game's own log (nothing reads its memory). Checked for each run, against the floor table
(tools/tower_legends/tower_legends.json): the battle started on that floor against its solo trainer (class, picture);
every opposing Pokemon as the battle built it (species, level = the floor, held item, Ability when the table names one,
the four moves); the battle ended (no freeze, no crash) with the expected outcome; the result waits in the save's Tower
record (won 2 / lost 3) for the floor and its ticket; no money moved; the party is healed and holds its items again; a
Mega Stone holder of the floor that came into battle Mega Evolved (PKMN_TEST_MEGA_LOG) in the won run. Exit 0 when every
check of every run passed; the report is OUT/tower_check.json and a floor-by-floor OUT/tower_check.md. --caps runs the
experience cap's cases instead (CAP_CASES; OUT/tower_caps.json).

    tools/battle_env/tower_check.py --exe ./pokeemerald-mp-bfxtest --save town.sav --out build/tower-check \\
        [--floors 1-100] [--runs won,lost] [--jobs 3] [--speed 4] [--display :99] [--make-save]
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
END = re.compile(r"tower end floor (\d+) pending (\d+) ticket ([0-9a-f]+) floor (\d+) record (\d+) money (\d+) "
                 r"\(was (\d+)\) cap (\d+)")


def floors_arg(text):
    out = []
    for part in text.split(","):
        first, _, last = part.partition("-")
        out.extend(range(int(first), int(last or first) + 1))
    return [f for f in out if 1 <= f <= 100]


def parse(lines):
    """The driver's Tower lines: the battle, the foes as built, the end, the own party, the Megas, the outcome."""
    run = {"battle": None, "foes": [], "end": None, "own": [], "outcome": None, "megas": [], "notes": [], "cap": None}
    for line in lines:
        if line.startswith("tower battle floor"):
            floor, mode, opponent, cls, pic, party = numbers(line)
            run["battle"] = dict(floor=floor, mode=mode, opponent=opponent, cls=cls, pic=pic, party=party)
        elif line.startswith("tower foe "):
            n = numbers(line)
            run["foes"].append(dict(slot=n[0], species=n[1], level=n[2], item=n[3], ability=n[4], moves=n[5:9],
                                    hp=n[9], maxhp=n[10]))
        elif line.startswith("tower end floor"):
            m = END.match(line)
            if m is None:
                run["notes"].append("unreadable: " + line)
                continue
            g = m.groups()
            run["end"] = dict(floor=int(g[0]), pending=int(g[1]), ticket=int(g[2], 16), pendingFloor=int(g[3]),
                              record=int(g[4]), money=int(g[5]), was=int(g[6]), cap=int(g[7]))
        elif line.startswith("tower own "):
            n = numbers(line)
            run["own"].append(dict(slot=n[0], species=n[1], level=n[2], item=n[3], hp=n[9], maxhp=n[10], exp=n[11]))
        elif line.startswith("tower floor ") and "cap " in line:
            run["cap"] = numbers(line)[-1]
        elif line.startswith("battle over, outcome"):
            run["outcome"] = numbers(line)[0]
        elif line.startswith("mega battler "):
            # PKMN_TEST_MEGA_LOG: "mega battler B: species S types T/T ability A gimmick G" whenever a battler changes;
            # an opposing battler (odd B) with gimmick 1 (GIMMICK_MEGA) has Mega Evolved into species S
            n = numbers(line)
            if n[0] % 2 == 1 and n[-1] == 1 and n[1] not in [m["species"] for m in run["megas"]]:
                run["megas"].append(dict(battler=n[0], species=n[1]))
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
        if run["megas"] and not holders:
            problems.append(f"a Mega Evolution without a Mega Stone holder in the table: {run['megas']}")
    return problems


def run_one(args, data, floor, kind):
    rundir = os.path.join(args.out, f"floor-{floor:03d}-{kind}")
    env = {"PKMN_TEST_BATTLE": f"tower:{floor}", "PKMN_TEST_AUTOPILOT": "1", "PKMN_TEST_FIGHT": str(FIGHT_SECONDS[kind]),
           "PKMN_TEST_MEGA_LOG": "1", "PKMN_TEST_PARTY": WIN_PARTY if kind == "won" else LOSE_PARTY}
    if args.speed > 1:
        env["PKMN_TEST_SPEED"] = str(args.speed)      # the game's clock that many times as fast (test builds)
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
                problems=problems, foes=len(run["foes"]), megas=[m["species"] for m in run["megas"]])


# --caps: the Tower experience cap in real battles (design section 3), each a won run of floor 5 with one Mewtwo (Slow
# growth: 5 n^3 / 4 experience at level n), the badges and the Hall of Fame flag set in memory (PKMN_TEST_BADGES,
# PKMN_TEST_CHAMPION), the Mewtwo one experience point short of its next level (party flag e) unless above the cap:
# (name, badges, champion, the Mewtwo's level, the cap, what its experience must do: capped - exactly the cap level's;
# gained - more, below the cap; unchanged - none at all)
CAP_FLOOR = 5
CAP_CASES = (
    ("cap-15-no-badges", 0, 0, 14, 15, "capped"),
    ("cap-20-one-badge", 1, 0, 14, 20, "gained"),
    ("cap-60-eight-badges", 8, 0, 59, 60, "capped"),
    ("cap-100-champion", 8, 1, 59, 100, "gained"),
    ("above-the-cap", 0, 0, 30, 15, "unchanged"),
)


def slow_exp(level):
    return 5 * level ** 3 // 4


def run_cap(args, case):
    name, badges, champion, level, cap, expect = case
    rundir = os.path.join(args.out, name)
    env = {"PKMN_TEST_BATTLE": f"tower:{CAP_FLOOR}", "PKMN_TEST_AUTOPILOT": "1", "PKMN_TEST_FIGHT": "420",
           "PKMN_TEST_TOWER_WIN": "1", "PKMN_TEST_BADGES": str(badges), "PKMN_TEST_CHAMPION": str(champion),
           "PKMN_TEST_PARTY": f"150:{level}" + ("" if expect == "unchanged" else ":e")}
    if args.speed > 1:
        env["PKMN_TEST_SPEED"] = str(args.speed)
    began = time.monotonic()
    process, output = start(args, rundir, env)
    code = finish(process, output, 600)
    run = parse(driver_lines(os.path.join(rundir, "test.log")))
    before = slow_exp(level) if expect == "unchanged" else slow_exp(level + 1) - 1
    after = run["own"][0]["exp"] if run["own"] else None
    problems = []
    if run["outcome"] != 1:
        problems.append(f"outcome {run['outcome']} (exit {code}), expected a win")
    if run["cap"] != cap:
        problems.append(f"the game's cap {run['cap']}, expected {cap}")
    if after is None:
        problems.append("no end logged")
    elif expect == "capped" and after != slow_exp(cap):
        problems.append(f"experience {before} -> {after}, expected exactly {slow_exp(cap)} (level {cap})")
    elif expect == "gained" and not before < after <= slow_exp(cap):
        problems.append(f"experience {before} -> {after}, expected a gain below {slow_exp(cap)}")
    elif expect == "unchanged" and after != before:
        problems.append(f"experience {before} -> {after}, expected none above the cap")
    return dict(case=name, badges=badges, champion=champion, level=level, cap=cap, expect=expect, exp_before=before,
                exp_after=after, level_after=run["own"][0]["level"] if run["own"] else None, exit=code,
                seconds=round(time.monotonic() - began, 1), passed=not problems, problems=problems)


def report(args, data, results):
    by_floor = {}
    for r in results:
        by_floor.setdefault(r["floor"], {})[r["run"]] = r
    lines = ["# Battle Tower: Legends - floor-by-floor check", "",
             f"Executable: `{os.path.basename(args.exe)}`; floor table hash {data['hash']}; runs: {args.runs}.", "",
             "| Floor | Kind | Trainer | Team | Won run | Lost run | Megas seen (species) |", "| --- | --- | --- | --- | --- | --- | --- |"]
    for floor in sorted(by_floor):
        f = data["floors"][floor - 1]
        cells = []
        for kind in ("won", "lost"):
            r = by_floor[floor].get(kind)
            cells.append("-" if r is None else ("passed" if r["passed"] else "FAILED: " + "; ".join(r["problems"])[:200])
                         + ("" if r is None else f" ({r['seconds']} s)"))
        megas = ", ".join(str(m) for m in by_floor[floor].get("won", {}).get("megas", [])) or "-"
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
    parser.add_argument("--speed", type=int, default=1, help="PKMN_TEST_SPEED: the game's clock this many times as fast")
    parser.add_argument("--caps", action="store_true", help="the experience cap's runs instead of the floors")
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
    if args.caps:
        results = [run_cap(args, case) for case in CAP_CASES]
        for result in results:
            print(json.dumps(result), flush=True)
        with open(os.path.join(args.out, "tower_caps.json"), "w") as out:
            json.dump(results, out, indent=1)
        return 0 if all(r["passed"] for r in results) else 1
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
