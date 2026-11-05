#!/usr/bin/env python3
"""Adds HD versions of moves past the Battle FX package's 621 to a battlefx/ data folder.

    python3 tools/battlefx/extend_folder.py --out battlefx --moves MOVE_A,MOVE_B [--src battlefx] [--root .]

  bfx_moves.json  the package's data (the game's own conversion, pack_data.py) with the moves asked for added, converted
                  from this tree's scripts by tools/battlefx/convert_more.py (meta.more lists them)
  bfx_core.js     the package's bundle, then tools/battlefx/core_more.js (its callbacks, tasks and HD presentation for
                  those moves) after a marker line
  MANIFEST.txt    each file's SHA-256 again, and what was added

Run again with another list, it starts from the package's part of each file (the moves meta.more names and the part
after the marker are taken out first), so the folder always holds exactly the moves of the last run. --out may be the
folder itself (in place). The game reads the folder beside the executable or PKMN_BATTLEFX_DIR (bfx_game.c): a copy
can be tried without rebuilding the game.
"""
import argparse
import hashlib
import json
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import convert_more  # noqa: E402

MARKER = "/* ==== EmeraldTogether additions: tools/battlefx/core_more.js (tools/battlefx/extend_folder.py) ==== */"
NOTE_HEAD = "Moves past the package (tools/battlefx/extend_folder.py):"


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def package_data(data):
    """The package's part of an (extended) bfx_moves.json."""
    more = data.get("meta", {}).get("more")
    if not more:
        return data
    added = set(more.get("moves", []))
    data["moves"] = [m for m in data["moves"] if m["id"] not in added]
    tpl = more.get("templates", [])
    if tpl:
        keep = [t for t in data["templates"] if t["n"] not in set(tpl)]
        if len(keep) != len(data["templates"]) - len(tpl) or data["templates"][-len(tpl):] != [t for t in data["templates"] if t["n"] in set(tpl)]:
            raise SystemExit("extend_folder.py: the added templates are not the last ones; convert from the package's data")
        data["templates"] = keep
    for key in ("anims", "affines"):
        n = more.get("added_" + key, 0)
        if n:
            del data[key][-n:]
    for key in ("animTables", "affTables", "affArrays"):
        for name in more.get("added_" + key, []):
            data[key].pop(name, None)
    del data["meta"]["more"]
    data["meta"]["moves"] = len(data["moves"])
    return data


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--src", default=os.path.join(ROOT, "battlefx"))
    ap.add_argument("--out", required=True)
    ap.add_argument("--root", default=ROOT, help="the game's tree the scripts are read from (read only)")
    ap.add_argument("--moves", required=True, help="MOVE_A,MOVE_B,... (or @file with one per line)")
    o = ap.parse_args()
    moves = o.moves
    if moves.startswith("@"):
        moves = ",".join(x.strip() for x in open(moves[1:]) if x.strip() and not x.startswith("#"))
    ids = [x for x in moves.split(",") if x]
    src, out = os.path.abspath(o.src), os.path.abspath(o.out)

    data = package_data(json.load(open(os.path.join(src, "bfx_moves.json"))))
    core = open(os.path.join(src, "bfx_core.js"), encoding="utf-8").read()
    core = core.split("\n" + MARKER + "\n")[0]
    manifest = open(os.path.join(src, "MANIFEST.txt"), encoding="utf-8").read()
    manifest = manifest.split("\n" + NOTE_HEAD)[0].rstrip("\n") + "\n"

    before = {k: len(data[k]) for k in ("anims", "affines")}
    before_names = {k: set(data[k]) for k in ("animTables", "affTables", "affArrays")}
    scripts = convert_more.Scripts(o.root)
    added, skipped, tpl = convert_more.extend(scripts, data, set(ids), 0)
    data["meta"]["more"]["added_anims"] = len(data["anims"]) - before["anims"]
    data["meta"]["more"]["added_affines"] = len(data["affines"]) - before["affines"]
    for k in ("animTables", "affTables", "affArrays"):
        data["meta"]["more"]["added_" + k] = sorted(set(data[k]) - before_names[k])
    missing = [x for x in ids if x not in added]

    if src != out:
        if os.path.exists(out):
            shutil.rmtree(out)
        shutil.copytree(src, out)
    with open(os.path.join(out, "bfx_moves.json"), "w") as f:
        json.dump(data, f, separators=(",", ":"))
    more = open(os.path.join(HERE, "core_more.js"), encoding="utf-8").read()
    with open(os.path.join(out, "bfx_core.js"), "w", encoding="utf-8") as f:
        f.write(core + "\n" + MARKER + "\n" + more)
    lines = []
    for line in manifest.split("\n"):
        parts = line.split("  ", 1)
        if len(parts) == 2 and len(parts[0]) == 64 and os.path.exists(os.path.join(out, parts[1])):
            line = sha256(os.path.join(out, parts[1])) + "  " + parts[1]
        lines.append(line)
    manifest = "\n".join(lines).rstrip("\n") + "\n\n" + NOTE_HEAD + "\n"
    manifest += ("%d moves past the package's 621 are converted from this game's own scripts by tools/battlefx/convert_more.py "
                 "(bfx_moves.json meta.more) and bfx_core.js ends with tools/battlefx/core_more.js (their callbacks, tasks "
                 "and HD presentation): %s.\n" % (len(added), ", ".join(added)))
    with open(os.path.join(out, "MANIFEST.txt"), "w", encoding="utf-8") as f:
        f.write(manifest)
    print("%s: %d moves added%s" % (out, len(added), (", templates " + ", ".join(tpl)) if tpl else ""))
    for mid in missing:
        print("  not added: %s (%s)" % (mid, skipped.get(mid, "not a move, or already in the data")))
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main())
