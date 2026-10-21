#!/usr/bin/env python3
"""Battle Tower: Legends' floor-by-floor report: tools/battle_env/tower_check.py's results (one run or several: Linux,
the Windows runner's shards) beside the floor table (tools/tower_legends/tower_legends.json): each floor's kind, region,
level, trainer and team (* holds its Mega Stone), and for each platform the won and lost runs (passed or the problems,
seconds) and the Mega forms seen in the won run (species names from include/constants/species.h).

    tools/tower_legends/floor_report.py --run Linux=build/tower-check/tower_check.json \\
        --run Windows=shard1/tower_check.json,shard2/tower_check.json --out report.md
"""
import argparse
import json
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
DATA = os.path.join(ROOT, "tools", "tower_legends", "tower_legends.json")


def species_names():
    """Species id -> a readable name ("SPECIES_CHARIZARD_MEGA_X" -> "Charizard Mega X")."""
    names = {}
    text = open(os.path.join(ROOT, "include", "constants", "species.h")).read()
    for name, value in re.findall(r"^\s*(SPECIES_\w+)\s*=\s*(\d+),", text, re.M):
        names.setdefault(int(value), name[len("SPECIES_"):].replace("_", " ").title())
    return names


def readable(constant, prefix):
    return constant[len(prefix):].replace("_", " ").title() if constant.startswith(prefix) else constant


def load_runs(spec):
    """'Label=a.json,b.json' -> (label, {(floor, run): result})."""
    label, _, paths = spec.partition("=")
    results = {}
    for path in paths.split(","):
        with open(path) as source:
            for result in json.load(source)["results"]:
                results[(result["floor"], result["run"])] = result
    return label, results


def cell(result, names):
    if result is None:
        return "not run"
    text = "passed" if result["passed"] else "**FAILED**: " + "; ".join(result["problems"])[:240]
    if result["run"] == "won" and result.get("megas"):
        text += "; Mega: " + ", ".join(names.get(m, str(m)) for m in result["megas"])
    return f"{text} ({result['seconds']} s)"


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--data", default=DATA)
    parser.add_argument("--run", action="append", required=True, help="Label=tower_check.json[,more.json]")
    parser.add_argument("--out", required=True)
    args = parser.parse_args()
    with open(args.data) as source:
        data = json.load(source)
    names = species_names()
    runs = [load_runs(spec) for spec in args.run]
    lines = ["# Battle Tower: Legends - floor by floor", "",
             f"Floor table hash {data['hash']}. Each floor fought twice in the test build's real solo start "
             "(tools/battle_env/tower_check.py): won (six level-100 Mewtwo with an in-memory test advantage) and lost "
             "(a level-1 Magikarp). * marks a Mega Stone holder.", ""]
    for label, results in runs:
        passed = sum(r["passed"] for r in results.values())
        lines.append(f"* {label}: {passed} of {len(results)} runs passed "
                     f"({sum(1 for r in results.values() if r['run'] == 'won' and r['passed'])} won, "
                     f"{sum(1 for r in results.values() if r['run'] == 'lost' and r['passed'])} lost).")
    lines.append("")
    header = "| Floor | Kind | Region | Trainer | Team (Lv = floor) |"
    rule = "| --- | --- | --- | --- | --- |"
    for label, _ in runs:
        header += f" {label} won | {label} lost |"
        rule += " --- | --- |"
    lines += [header, rule]
    for floor in data["floors"]:
        n = floor["floor"]
        solo = floor["solo"]
        team = ", ".join(readable(m["species"], "SPECIES_") + ("*" if m.get("mega") else "") for m in solo["mons"])
        trainer = f"{readable(solo['class'], 'TRAINER_CLASS_')} {solo['name']}"
        row = f"| {n} | {floor['kind']} | {floor['region']} | {trainer} | {team} |"
        for _, results in runs:
            row += f" {cell(results.get((n, 'won')), names)} | {cell(results.get((n, 'lost')), names)} |"
        lines.append(row)
    with open(args.out, "w") as out:
        out.write("\n".join(lines) + "\n")
    print(f"floor_report: {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
