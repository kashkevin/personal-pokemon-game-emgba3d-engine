#!/usr/bin/env python3
"""The coverage manifest (the final master update, phase 15): every entry of the game's content and mechanics with
the status the tests give it - passed, failed, blocked (with the reason), unverified (nothing tests it yet) - generated
from the game's own data, so new content appears unverified by itself.

Sources:
    the inventory (tools/qa/inventory.py): maps, object events, warps, signs, hidden items, trainers, scripted battles
    the gameplay check's plan and results (tools/qa/gameplay_check.py): --gameplay OUT/gameplay_check.json, or --plan
        plan.json with --failed (the "FAILED key: reason" lines a CI report job prints; every other planned target of
        that run passed)
    the test ROM (make check): --test-results, its output with the colors taken off ("[n] name: STATUS"); each test is
        tied to its file (test/battle/ability/<ability>.c, move_effect/<effect>.c, hold_effect/<effect>.c, ...)

    tools/qa/coverage.py --plan plan.json --failed failed.txt --test-results check.txt --out build/qa/coverage

Writes OUT/coverage.json (every entry) and OUT/coverage.md (the summary and every entry that did not pass).
"""
import argparse
import collections
import glob
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import importlib.util  # noqa: E402
_spec = importlib.util.spec_from_file_location("qa_inventory", os.path.join(HERE, "inventory.py"))
inventory_tool = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(inventory_tool)

TEST = re.compile(r'^\s*(?:[A-Z][A-Z0-9_]*_TEST|TEST)\("((?:[^"\\]|\\.)*)"')
RESULT = re.compile(r"^\[\d+\] (.*): ([A-Z_]+)$")
GOOD = {"PASS", "EXPECTED_FAIL", "KNOWN_FAILING_PASS"}
NEUTRAL = {"KNOWN_FAILING", "TO_DO", "ASSUMPTION_FAIL", "SKIPPED"}


def enum_names(path, prefix):
    names = []
    for line in open(path, encoding="utf-8"):
        match = re.match(rf"^\s*({prefix}[A-Z0-9_]+)\b", line)
        if match and not match.group(1).startswith(("NUM_", prefix + "COUNT")):
            names.append(match.group(1))
    return list(dict.fromkeys(names))


def test_files(root):
    """{test name: [file, ...]} of every test of the test ROM."""
    out = collections.defaultdict(list)
    for path in glob.glob(os.path.join(root, "test", "**", "*.c"), recursive=True):
        for line in open(path, encoding="utf-8", errors="replace"):
            match = TEST.match(line)
            if match:
                out[match.group(1).replace('\\"', '"')].append(os.path.relpath(path, root))
    return out


def file_results(root, results_path, known=frozenset()):
    """{test file: Counter of its tests' results} from a test ROM run; a failure of a test in known (the expected
    differences of KNOWN_ISSUES G10: upstream tests of Gen 9 rules under this game's Gen 6 profile) counts as KNOWN."""
    by_name = test_files(root)
    out = collections.defaultdict(collections.Counter)
    for line in open(results_path, encoding="utf-8", errors="replace"):
        match = RESULT.match(line.strip())
        if not match:
            continue
        name, status = match.groups()
        if status == "FAIL" and name in known:
            status = "KNOWN_DIFFERENCE"
        for path in by_name.get(name, ["(unknown file)"]):
            out[path][status] += 1
    return out


def status_of_files(paths, results):
    """passed / failed / unverified for an entry the given test files cover."""
    counts = collections.Counter()
    for path in paths:
        counts.update(results.get(path, {}))
    if not paths:
        return "unverified", "no test of its own"
    if not counts:
        return "unverified", f"tests not run: {', '.join(paths)}"
    bad = {k: v for k, v in counts.items() if k not in GOOD | NEUTRAL | {"KNOWN_DIFFERENCE"}}
    good = sum(v for k, v in counts.items() if k in GOOD)
    if bad:
        return "failed", f"{dict(bad)} of {sum(counts.values())} in {', '.join(paths)}"
    if counts.get("KNOWN_DIFFERENCE"):
        return "expected_difference", (f"{good} passed, {counts['KNOWN_DIFFERENCE']} an upstream Gen 9 rule under the "
                                       f"game's Gen 6 profile (KNOWN_ISSUES G10) in {', '.join(paths)}")
    if not good:
        return "unverified", f"only {dict(counts)} in {', '.join(paths)}"
    return "passed", f"{good} tests in {', '.join(paths)}"


def mechanics(root, results):
    """Abilities, move effects, hold effects: one entry each, from the test ROM's files named after them."""
    entries = []
    groups = [("ability", "include/constants/abilities.h", "ABILITY_", "test/battle/ability"),
              ("move_effect", "include/constants/battle_move_effects.h", "EFFECT_", "test/battle/move_effect"),
              ("hold_effect", "include/constants/hold_effects.h", "HOLD_EFFECT_", "test/battle/hold_effect")]
    for kind, header, prefix, folder in groups:
        if not os.path.exists(os.path.join(root, header)):
            continue
        for name in enum_names(os.path.join(root, header), prefix):
            if name in (prefix + "NONE",):
                continue
            stem = name[len(prefix):].lower()
            paths = [os.path.relpath(p, root) for p in glob.glob(os.path.join(root, folder, stem + ".c"))]
            status, why = (status_of_files(paths, results) if results is not None
                           else ("unverified", "no test run given"))
            entries.append(dict(key=f"{kind}:{name}", category=kind, status=status, evidence=why))
    return entries


def content(inventory, plan, outcomes):
    """Every object, sign, hidden item, warp, map and trainer of the inventory, with its gameplay check status."""
    planned = {t["key"]: t for t in plan.get("targets", [])}
    blocked = {b["key"]: b for b in plan.get("blocked", [])}
    entries = []

    def status(key):
        if key in outcomes:
            passed, why = outcomes[key]
            return ("passed" if passed else "failed"), why
        if key in blocked:
            return "blocked", blocked[key]["reason"]
        if key in planned:
            return "unverified", "planned, not run"
        return "unverified", "not in the gameplay plan"

    passed_maps = collections.Counter()
    for m in inventory["maps"]:
        if not m["reachable"]:
            entries.append(dict(key=m["key"], category="map", status="blocked" if m.get("entry", "").startswith(
                ("unused", "FireRed")) else "unverified", evidence=m.get("entry", "not reachable")))
            continue
        for kind, items in (("object", m["objects"]), ("sign", m["signs"]), ("hidden_item", m["hidden_items"]),
                            ("warp", m["warps"])):
            for item in items:
                if kind == "object" and item["kind"] in ("object", "clone", "berry_tree"):
                    category = "object_without_script" if item["kind"] == "object" else item["kind"]
                    entries.append(dict(key=item["key"], category=category, status="unverified",
                                        evidence="not a gameplay target (no script, a clone or a berry tree)"))
                    continue
                st, why = status(item["key"])
                category = kind if kind != "object" else "trainer_object" if item["kind"] == "trainer" else item["kind"]
                entries.append(dict(key=item["key"], category=category, status=st, evidence=why, map=m["id"]))
                if st == "passed":
                    passed_maps[m["id"]] += 1
        entries.append(dict(key=m["key"], category="map", status="passed" if passed_maps[m["id"]] else "unverified",
                            evidence=f"{passed_maps[m['id']]} of its targets passed" if passed_maps[m["id"]]
                            else "no target of it passed yet"))
    object_status = {e["key"]: e["status"] for e in entries}
    for t in inventory["trainers"]:
        places = [p for p in t["placements"] if p.get("in_game")]
        if not places:
            entries.append(dict(key=t["key"], category="trainer", status="blocked" if t.get("entry", "").startswith(
                "unused") else "unverified", evidence=t.get("entry", "fought nowhere")))
            continue
        objects = [f"object:{p['map']}:{p['local']}" for p in places if "local" in p]
        results = [object_status.get(o) for o in objects]
        if "passed" in results:
            entries.append(dict(key=t["key"], category="trainer", status="passed",
                                evidence=f"fought and won on its map ({objects[results.index('passed')]})"))
        elif "failed" in results:
            entries.append(dict(key=t["key"], category="trainer", status="failed",
                                evidence=f"its object {objects[results.index('failed')]} failed"))
        else:
            kinds = sorted({p.get("kind") for p in places})
            entries.append(dict(key=t["key"], category="trainer", status="unverified",
                                evidence=f"fought by {', '.join(kinds)} (no object battle checked)"))
    for b in inventory["scripted_battles"]:
        if b["runs"] and not b["frlg"]:
            entries.append(dict(key=b["key"], category="scripted_battle", status="unverified",
                                evidence=f"started by {b['label']} ({b['file']}): not checked on its own"))
    return entries


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--gameplay", help="a gameplay_check.json")
    parser.add_argument("--plan", help="a plan.json (with --failed)")
    parser.add_argument("--failed", help="the FAILED lines of a gameplay run whose every other planned target passed")
    parser.add_argument("--test-results", help="the test ROM's output, colors taken off")
    parser.add_argument("--known", help="the expected differences: lines 'file | test name' (KNOWN_ISSUES G10)")
    parser.add_argument("--out", required=True)
    args = parser.parse_args()
    inventory = inventory_tool.inventory(ROOT)
    plan, outcomes = {}, {}
    if args.gameplay:
        report = json.load(open(args.gameplay))
        plan = dict(targets=[dict(key=r["key"], kind=r["kind"]) for r in report["results"]], blocked=report["blocked"])
        outcomes = {r["key"]: (r["passed"], r["reason"]) for r in report["results"]}
    elif args.plan:
        plan = json.load(open(args.plan))
        failed = {}
        if args.failed:
            for line in open(args.failed, encoding="utf-8"):
                match = re.match(r"^FAILED (\S+): (.*)$", line.strip())
                if match:
                    failed[match.group(1)] = match.group(2)
            outcomes = {t["key"]: ((t["key"] not in failed), failed.get(t["key"], "passed in the gameplay check"))
                        for t in plan["targets"]}
    known = frozenset(line.split(" | ", 1)[1].strip() for line in open(args.known, encoding="utf-8")
                      if " | " in line) if args.known else frozenset()
    results = file_results(ROOT, args.test_results, known) if args.test_results else None
    entries = content(inventory, plan, outcomes) + mechanics(ROOT, results)
    summary = collections.defaultdict(collections.Counter)
    for entry in entries:
        summary[entry["category"]][entry["status"]] += 1
    os.makedirs(args.out, exist_ok=True)
    with open(os.path.join(args.out, "coverage.json"), "w") as out:
        json.dump(dict(summary=summary, entries=entries), out, indent=1)
    lines = ["# Coverage manifest", "",
             "| Category | Passed | Expected difference | Failed | Blocked | Unverified |",
             "| --- | --- | --- | --- | --- | --- |"]
    for category in sorted(summary):
        counts = summary[category]
        lines.append(f"| {category} | {counts['passed']} | {counts['expected_difference']} | {counts['failed']} | "
                     f"{counts['blocked']} | {counts['unverified']} |")
    lines += ["", "## Failed", ""] + [f"- `{e['key']}`: {e['evidence']}" for e in entries if e["status"] == "failed"]
    with open(os.path.join(args.out, "coverage.md"), "w") as out:
        out.write("\n".join(lines) + "\n")
    print(json.dumps({k: dict(v) for k, v in summary.items()}))
    return 0


if __name__ == "__main__":
    sys.exit(main())
