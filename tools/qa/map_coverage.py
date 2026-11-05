#!/usr/bin/env python3
"""Every map of the game's map list with what the automated 3D map sweep made of it: the sweep's coverage report
(KNOWN_ISSUES MS-98).

    tools/qa/map_coverage.py --sweep build/qa/geo --out build/qa/geo/coverage.md [--json build/qa/geo/coverage.json] \\
        [--intro intro.md] [--title "..."] [--previous an-earlier-report.md --previous-label "..."]

--sweep is a sweep's folder after `tools/qa/geometry_check.py check` (its manifest.json); the maps come from the
engine checkout: every map of data/maps/map_groups.json (tools/qa/geometry_inventory.py: the 503 of the Emerald game,
the FireRed/LeafGreen and unused ones). Each map gets one status:

    inspected                     a warp put the probe there; the share of its cells the scenes held, and what each of
                                  the geometry checks found (tools/qa/geometry_check.py)
    entered through its facility  reached as play reaches it, through its facility's scripts (geometry_check.py
                                  FACILITY_ENTRIES: the Battle Tower's multi partner room, the Battle Pike's room)
    not reachable on PC           no player reaches it on PC, and why: not in this build (FireRed/LeafGreen), unused
                                  (nothing leads there), or what the PC lacks (geometry_check.py NOT_REACHED: the Union
                                  Room and the link rooms, the e-Reader trainer's room, the original introduction's truck)
    not inspected                 a player reaches it, but this sweep did not look at it, and why (the game stopped
                                  there, its facility did not take the probe in, not in this run's tour)

The report: the totals of each status (and by kind of map), the reasons, the commands that make it again, and the
table of every map in the map list's order.
"""
import argparse
import collections
import importlib.util
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))


def load(name, file):
    spec = importlib.util.spec_from_file_location(name, os.path.join(HERE, file))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


geometry_check = load("qa_geometry_check", "geometry_check.py")
geometry_inventory = geometry_check.geometry_inventory

STATUSES = ("inspected", "entered through its facility", "not reachable on PC", "not inspected")
KINDS = ("outdoor", "cave", "underwater", "indoor", "secret_base", "other")
FRLG_LABEL = "FireRed/LeafGreen: not in this build"
FRLG_REASON = ("a FireRed/LeafGreen map in the data: the Emerald build leaves it out (tools/mapjson: a map whose layout "
               "is not Emerald's is not assembled; the Kanto map groups are NULL in gMapGroups, data/maps/groups.inc), "
               "so no warp of the game can reach it (KNOWN_ISSUES M14)")
UNUSED_LABEL = "unused: nothing leads there"
UNUSED_REASON = ("in the data and the build, but no warp, map connection or script of the game leads there "
                 "(tools/qa/inventory.py's reachability from Littleroot Town: the Ruby/Sapphire prototypes, the unused "
                 "contest halls and mart)")


# How play enters a map no one walks into (tools/qa/geometry_inventory.py's entry), for one the sweep warped to
PLAY_ENTRY = (("scripted", "in play its facility's or event's script enters it"),
              ("battle pyramid", "in play a piece of a Battle Pyramid floor (the C code copies its cells there)"),
              ("high tide", "in play the low-tide room's map with this layout"),
              ("secret base", "in play entered from a secret base spot"))


def findings(row):
    """The geometry checks' counts of a map, the non-zero ones."""
    return ", ".join(f"{check} {row['defects'][check]}" for check in geometry_check.CHECKS
                     if row["defects"].get(check))


def previous_sweep(path):
    """{map: (status, seen, cells)} from an earlier sweep's report (geometry_check.py check's report.md, or the app's
    docs/3d-geometry-report-*.md made from it): its tables' "| Map | Status | Cells seen |" rows."""
    out = {}
    for line in open(path, encoding="utf-8"):
        match = re.match(r"^\| (\w+) \| ((?:partly |not )?inspected)[^|]* \| (\d+)/(\d+) \|", line)
        if match:
            out[match.group(1)] = (match.group(2), int(match.group(3)), int(match.group(4)))
    return out


def coverage(root, manifest, previous=None, previous_label=""):
    """One entry per map of the map list: its status, cells, how it was reached, findings, and the reason."""
    by_map = {row["map"]: row for row in manifest["maps"]}
    numbers = geometry_check.map_numbers(root)
    entries = []
    for m in geometry_inventory.geometry_inventory(root):
        group, num = numbers[m["name"]]
        entry = dict(map=m["name"], group=group, num=num, kind=m["kind"], cells=m.get("cells"), seen=None,
                     how=None, findings="", label="", reason="", in_game=m["in_game"])
        row = by_map.get(m["name"])
        unreachable = geometry_check.NOT_REACHED.get(m["name"])
        if not m["in_game"]:
            frlg = m["out_of_scope"] == "FireRed/LeafGreen"
            entry.update(status="not reachable on PC", label=FRLG_LABEL if frlg else UNUSED_LABEL,
                         reason=FRLG_REASON if frlg else UNUSED_REASON)
        elif row is not None and row["status"] in ("inspected", "partly inspected"):
            entry.update(status="entered through its facility" if row["entry"] == "facility" else "inspected",
                         seen=row["covered"], cells=row["cells"], findings=findings(row),
                         how="through its facility" if row["entry"] == "facility" else "a warp",
                         reason=row.get("facility") or "")
            if row["status"] == "partly inspected":
                entry["label"] = f"partly: {row['covered']} of {row['cells']} cells"
            elif row["entry"] != "facility":
                play = next((text for prefix, text in PLAY_ENTRY if m["entry"].startswith(prefix)), "")
                entry["label"] = f"warped to; {play}" if play else ""
            if unreachable:     # put back in the tour (--with-unreachable): looked at, though no player goes there
                entry.update(label=f"no player reaches it on PC ({unreachable['label']}): warped to",
                             reason=unreachable["reason"])
        elif unreachable:
            entry.update(status="not reachable on PC", label=unreachable["label"], reason=unreachable["reason"])
        else:
            entry["status"] = "not inspected"
            notes = (row or {}).get("notes") or []
            if row is None:
                entry["label"] = "not in this sweep's manifest"
            elif any("not reached through its facility" in note for note in notes):
                entry["label"] = "its facility did not take the probe in"
            elif any("the game stopped here" in note for note in notes):
                entry["label"] = "the game stopped there"
            elif not row["places_planned"]:
                entry["label"] = "not in this run's tour"
            else:
                entry["label"] = "this run did not get to it"
            entry["reason"] = "; ".join(notes)
            earlier = (previous or {}).get(m["name"])
            if earlier:
                entry["previous"] = f"{previous_label or 'an earlier sweep'}: {earlier[0]}, {earlier[1]}/{earlier[2]}"
        entries.append(entry)
    return entries


def markdown(entries, args):
    counts = collections.Counter(e["status"] for e in entries)
    lines = [f"# {args.title}", ""]
    if args.intro:
        lines += [open(args.intro, encoding="utf-8").read().rstrip("\n"), ""]
    sweep = args.sweep_label or args.sweep
    lines += ["## How this report is made", "",
              "From the engine checkout, the 3D test build (`make -f Makefile_pc NATIVE_LINUX=1 DIORAMA=1 BFX_TEST=1`) "
              "and a display (Linux: `xvfb-run -a`):", "", "```sh",
              f"tools/qa/geometry_check.py tour --out {sweep}/tour.txt",
              f"xvfb-run -a -s \"-screen 0 1280x720x24 -nolisten tcp\" tools/qa/geometry_check.py run \\",
              f"    --exe ./pokeemerald-mp-diorama-bfxtest --make-save {sweep}/new.sav --tour {sweep}/tour.txt \\",
              f"    --out {sweep} --jobs 1",
              f"tools/qa/geometry_check.py check --out {sweep} --tour {sweep}/tour.txt",
              f"tools/qa/map_coverage.py --sweep {sweep} --out <this report>"
              + (" --intro <its introduction>" if args.intro else "")
              + (f" \\\n    --previous {args.previous_path or args.previous}" if args.previous else "")
              + (f" --previous-label \"{args.previous_label}\"" if args.previous and args.previous_label else "")
              + (" --not-run-note <the note below>" if args.not_run_note else ""),
              "```", "",
              "The pictures and the cell dumps the sweep writes (`shots/`, `cells/`) are the game's art and stay out "
              "of the repositories.", ""]
    lines += ["## Totals", "", "| Status | Maps |", "| --- | --- |"]
    lines += [f"| {status} | {counts[status]} |" for status in STATUSES]
    lines += [f"| all (the map list, `data/maps/map_groups.json`) | {len(entries)} |", ""]
    wholly = sum(1 for e in entries if e["status"] in STATUSES[:2] and e["seen"] is not None
                 and e["seen"] >= (e["cells"] or 0))
    lines += [f"Of the {counts[STATUSES[0]] + counts[STATUSES[1]]} maps looked at, {wholly} were wholly seen (every "
              "cell of the map in some place's scene).", ""]
    game = [e for e in entries if e["in_game"]]
    kinds = [k for k in KINDS if any(e["kind"] == k for e in game)]
    lines += [f"The Emerald game's {len(game)} maps by kind (the other {len(entries) - len(game)} of the list are "
              "FireRed/LeafGreen maps this build leaves out, and unused ones):", "",
              "| Kind | " + " | ".join(STATUSES) + " |", "| --- |" + " --- |" * len(STATUSES)]
    for kind in kinds:
        row = collections.Counter(e["status"] for e in game if e["kind"] == kind)
        lines.append(f"| {kind} | " + " | ".join(str(row[s]) for s in STATUSES) + " |")
    row = collections.Counter(e["status"] for e in game)
    lines += ["| all | " + " | ".join(str(row[s]) for s in STATUSES) + " |", ""]
    for status in STATUSES[2:]:
        reasons = collections.OrderedDict()
        for e in entries:
            if e["status"] == status:
                reasons.setdefault((e["label"], e["reason"]), []).append(e["map"])
        if not reasons:
            continue
        lines += [f"## {status[0].upper() + status[1:]}: why", ""]
        if status == "not inspected" and args.not_run_note:
            lines += [args.not_run_note, ""]
        for (label, reason), names in sorted(reasons.items(), key=lambda item: -len(item[1])):
            # every map named, but for the long lists of maps outside the game (FireRed/LeafGreen: in the table)
            shown = ", ".join(names) if len(names) <= 60 else f"{', '.join(names[:6])} ... ({len(names)} maps)"
            why = f"{reason}{'' if reason.endswith('.') else '.'} " if reason else ""
            lines.append(f"* **{label}** ({len(names)}): {why}Maps: {shown}")
            earlier = [e for e in entries if e["map"] in names and e.get("previous")]
            if status == "not inspected" and earlier:
                wholly = sum(1 for e in earlier if e["previous"].split(": ", 1)[1].startswith("inspected"))
                lines.append(f"  * {args.previous_label or 'An earlier sweep'}: {wholly} of these {len(names)} wholly "
                             f"inspected, {len(earlier) - wholly} not wholly; each map's row below says which")
        lines.append("")
    facility = [e for e in entries if e["status"] == STATUSES[1]]
    if facility:
        lines += ["## Entered through their facilities", ""]
        lines += [f"* **{e['map']}** ({e['seen']}/{e['cells']} cells; {e['findings'] or 'nothing found'}): "
                  f"{e['reason']}" for e in facility] + [""]
    lines += ["## Every map", "",
              "In the map list's order (group.number). Cells: those of the map the sweep's scenes held, of all. Found: "
              "the geometry checks' cells (places for hole and not-3d; `tools/qa/geometry_check.py`, the app's "
              "docs/3d-world-geometry-20261029.md). Note: why a map is not looked at (the reasons above), or how.", "",
              "| Map | # | Kind | Status | Cells | Found | Note |", "| --- | --- | --- | --- | --- | --- | --- |"]
    for e in entries:
        cells = f"{e['seen']}/{e['cells']}" if e["seen"] is not None else "-"
        note = e["label"] or (e["how"] if e["how"] and e["how"] != "a warp" else "")
        if e.get("previous"):
            note += f" ({e['previous']})"
        lines.append(f"| {e['map']} | {e['group']}.{e['num']} | {e['kind']} | {e['status']} | {cells} | "
                     f"{e['findings']} | {note} |")
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", default=ROOT)
    parser.add_argument("--sweep", required=True, help="the sweep's folder (its manifest.json from geometry_check.py "
                                                       "check)")
    parser.add_argument("--out", required=True, help="the report (Markdown)")
    parser.add_argument("--json", help="every map's entry, as JSON")
    parser.add_argument("--intro", help="Markdown put after the title (what the sweep was, what it found)")
    parser.add_argument("--title", default="3D map sweep: coverage of every map")
    parser.add_argument("--sweep-label", help="the sweep's folder as the report's commands name it (default --sweep)")
    parser.add_argument("--not-run-note", help="why this run did not get to some maps (a sample, a time budget)")
    parser.add_argument("--previous", help="an earlier sweep's report (its tables' Map / Status / Cells seen): what it "
                                           "made of the maps this run did not get to")
    parser.add_argument("--previous-label", help="that sweep, as the report names it (its date and engine)")
    parser.add_argument("--previous-path", help="that report's path as the report's commands name it (default "
                                                "--previous)")
    args = parser.parse_args()
    manifest = json.load(open(os.path.join(args.sweep, "manifest.json")))
    previous = previous_sweep(args.previous) if args.previous else None
    entries = coverage(args.root, manifest, previous, args.previous_label)
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, "w", encoding="utf-8") as out:
        out.write(markdown(entries, args))
    if args.json:
        with open(args.json, "w") as out:
            json.dump(dict(totals=collections.Counter(e["status"] for e in entries), maps=entries), out, indent=1)
    print(json.dumps(dict(maps=len(entries), **collections.Counter(e["status"] for e in entries))))
    return 0


if __name__ == "__main__":
    sys.exit(main())
