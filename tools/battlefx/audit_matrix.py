#!/usr/bin/env python3
"""Battle FX audit matrix (Update 27): one row per supported move and per battle animation.

    tools/battlefx/audit_matrix.py OUT_DIR RUN_DIR [RUN_DIR ...]

RUN_DIR: a tools/battlefx/gallery.sh run (its out/report.jsonl and the window captures beside it). Writes
OUT_DIR/matrix.json, OUT_DIR/matrix.md and OUT_DIR/sheets/*.png (one picture per 30 moves: each move's middle capture).

The columns are kept apart on purpose; a cell is only "yes" for what was actually measured:

  gameplay      a test in test/battle names the move (the GBA test ROM's own run decides whether it passes; this
                column says only that the move's rules are under test)
  triggered     the game's animation ran (report row, not failed, not stuck, frames > 0)
  parity        every compared frame of the game's effect sprites equals the Battle FX core's replay
  hd_layers     the HD renderer drew layers for it (the package's moves; a move past the package is drawn by the
                game's own effect sprites in the 3D view, "gba")
  lit           the captures differ from the scene without effect (median of every capture of the run) by more than
                the noise: something was drawn that frame
  inspected     filled in by hand (OUT_DIR/inspected.json: {"MOVE_X": "ok" | "note ..."}), never by this script
  real_gpu      always "no": nothing here ran on a GPU
"""
import collections
import glob
import json
import os
import re
import statistics
import subprocess
import sys

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
REGION = (0.0, 0.10, 1.0, 0.80)  # the stage; the message strip and the lower UI are left out
STEP = 4                           # pixel stride of the comparison
NOISE = 0.0015                     # fraction of compared pixels that must change to count as "something drawn"
THRESH = 28                        # per channel difference that counts as a changed pixel


_NAMES = None


def canonical(move_id):
    """MOVEN_<n> (a move played by number: past the package's data) -> MOVE_NAME."""
    global _NAMES
    m = re.fullmatch(r"MOVEN_(\d+)", move_id)
    if not m:
        return move_id
    if _NAMES is None:
        nums = json.load(open(os.path.join(ROOT, "tools", "battlefx", "move_numbers.json")))
        _NAMES = {}
        for name, n in sorted(nums.items()):
            _NAMES.setdefault(n, name)
    return _NAMES.get(int(m.group(1)), move_id)


def load_runs(run_dirs):
    rows = {}
    for d in run_dirs:
        path = os.path.join(d, "out", "report.jsonl")
        if not os.path.exists(path):
            continue
        for line in open(path):
            r = json.loads(line)
            r["_dir"] = os.path.join(d, "out")
            r["id"] = canonical(r["id"])
            key = (r["id"], r.get("turn", 0))
            rows[key] = r
        crashes = os.path.join(d, "out", "crashes.jsonl")
        if os.path.exists(crashes):
            for line in open(crashes):
                c = json.loads(line)
                c["id"] = canonical(c["id"])
                rows.setdefault((c["id"], 0), {"id": c["id"], "turn": 0, "crashed": True, "_dir": os.path.join(d, "out")})
    return rows


def inventory():
    nums = os.path.join(ROOT, "tools", "battlefx", "move_numbers.json")
    if os.path.exists(nums):
        return sorted(json.load(open(nums)))
    src = open(os.path.join(ROOT, "include/constants/moves.h")).read()
    body = src[src.index("enum __attribute__((packed)) Move"):]
    body = body[: body.index("};")]
    return [i for i in re.findall(r"^\s+(MOVE_[A-Z0-9_]+)\s*(?:=|,)", body, re.M) if i != "MOVE_NONE"]


def tested_moves():
    out = subprocess.run(["grep", "-rhoE", r"MOVE_[A-Z0-9_]+", os.path.join(ROOT, "test", "battle")], capture_output=True, text=True).stdout
    return collections.Counter(out.split())


def pixels(path):
    img = Image.open(path).convert("RGB")
    w, h = img.size
    box = (int(w * REGION[0]), int(h * REGION[1]), int(w * REGION[2]), int(h * REGION[3]))
    return img.crop(box).resize(((box[2] - box[0]) // STEP, (box[3] - box[1]) // STEP))


def median_scene(paths):
    import numpy as np

    sample = paths[:: max(1, len(paths) // 60)][:60]
    stack = np.stack([np.asarray(pixels(p)) for p in sample])
    return np.median(stack, axis=0)


def lit_fraction(path, scene):
    import numpy as np

    a = np.asarray(pixels(path)).astype(int)
    changed = (abs(a - scene).max(axis=2) > THRESH).mean()
    return float(changed)


NUMS = {}
# the Poke Ball animations are the game's originals by the owner's decision (bfx_game.c PokeBallAnimation): no HD frame, so
# the replay comparison does not apply
BALL_DESIGN = {"B_ANIM_BALL_THROW", "B_ANIM_BALL_THROW_WITH_TRAINER", "B_ANIM_CRITICAL_CAPTURE_THROW",
               "B_ANIM_SWITCH_OUT_PLAYER_MON", "B_ANIM_SWITCH_OUT_OPPONENT_MON"}


def main():
    global NUMS
    NUMS = json.load(open(os.path.join(ROOT, "tools", "battlefx", "move_numbers.json")))
    out_dir, run_dirs = sys.argv[1], sys.argv[2:]
    os.makedirs(os.path.join(out_dir, "sheets"), exist_ok=True)
    rows = load_runs(run_dirs)
    inv = inventory()
    pk = {m["id"] for m in json.load(open(os.path.join(ROOT, "battlefx/bfx_moves.json")))["moves"]}
    tests = tested_moves()
    inspected = {}
    p = os.path.join(out_dir, "inspected.json")
    if os.path.exists(p):
        inspected = json.load(open(p))
    shots = [os.path.join(r["_dir"], s["file"]) for r in rows.values() for s in r.get("shots", []) if s.get("file")]
    shots = [p for p in shots if os.path.isfile(p)]
    scene = median_scene(shots) if shots else None
    matrix = []
    for key, r in sorted(rows.items()):
        mid = None
        files = [os.path.join(r["_dir"], s["file"]) for s in r.get("shots", []) if s.get("file")]
        files = [p for p in files if os.path.isfile(p)]
        if files:
            mid = files[len(files) // 2]
        lit = max((lit_fraction(p, scene) for p in files), default=None) if scene is not None else None
        par = r.get("parity", {})
        render = r.get("render", {})
        matrix.append({
            "id": r["id"], "turn": key[1], "kind": "animation" if r["id"].startswith("B_ANIM") else "move",
            "gameplay_tests": tests.get(r["id"], 0),
            "triggered": bool(r.get("gameFrames", 0) > 0 and not r.get("failed") and not r.get("stuck") and not r.get("crashed")),
            "stuck": bool(r.get("stuck")), "crashed": bool(r.get("crashed")), "failed": bool(r.get("failed")),
            "frames": r.get("gameFrames"),
            "parity": (par.get("equal") == par.get("compared") and par.get("compared", 0) > 0) if par and par.get("compared", 0) > 0 else ("n/a (the game's own animation)" if r.get("coreFrames", 1) == 0 and r.get("gameFrames") else None),
            "hd_layers": render.get("layers", 0), "presentation": "hd" if r["id"] in pk or r["id"].startswith("B_ANIM") else "gba",
            "number": NUMS.get(r["id"]),
            "lit": None if lit is None else round(lit, 4), "lit_ok": None if lit is None else lit > NOISE,
            "ball_by_design": r["id"] in BALL_DESIGN,
            "capture": mid, "inspected": inspected.get(r["id"], "not yet"), "real_gpu": "no",
        })
    json.dump(matrix, open(os.path.join(out_dir, "matrix.json"), "w"), indent=1)
    # contact sheets
    movie = [m for m in matrix if m["kind"] == "move" and m["capture"] and m["turn"] == 0]
    for n in range(0, len(movie), 30):
        chunk = movie[n:n + 30]
        W, H = 384, 216
        sheet = Image.new("RGB", (6 * W, 5 * H))
        from PIL import ImageDraw

        dr = ImageDraw.Draw(sheet)
        for i, m in enumerate(chunk):
            im = Image.open(m["capture"]).convert("RGB").resize((W, H))
            sheet.paste(im, ((i % 6) * W, (i // 6) * H))
            dr.rectangle(((i % 6) * W, (i // 6) * H, (i % 6) * W + 8 * len(m["id"]) - 40, (i // 6) * H + 12), fill=(0, 0, 0))
            dr.text(((i % 6) * W + 2, (i // 6) * H + 1), m["id"][5:], fill=(255, 255, 0))
        sheet.save(os.path.join(out_dir, "sheets", f"moves-{n // 30 + 1:02d}.png"))
    # summary
    tot = collections.Counter()
    for m in matrix:
        tot["rows"] += 1
        tot["triggered"] += m["triggered"]
        tot["parity"] += bool(m["parity"])
        tot["lit"] += bool(m["lit_ok"])
        tot["stuck"] += m["stuck"]
        tot["crashed"] += m["crashed"]
        tot["failed"] += m["failed"]
        tot["hd"] += m["presentation"] == "hd" and m["hd_layers"] > 0
        tot["gba_only"] += m["presentation"] == "gba"
    json.dump(tot, open(os.path.join(out_dir, "summary.json"), "w"), indent=1)
    with open(os.path.join(out_dir, "matrix.md"), "w") as f:
        f.write("| id | turn | gameplay tests | triggered | parity | HD layers | presentation | lit | inspected | real GPU |\n|---|---|---|---|---|---|---|---|---|---|\n")
        for m in matrix:
            f.write(f"| {m['id']} | {m['turn']} | {m['gameplay_tests']} | {'yes' if m['triggered'] else 'NO'} | {m['parity']} | {m['hd_layers']} | {m['presentation']} | {m['lit']} | {m['inspected']} | no |\n")
    print(json.dumps(tot))


if __name__ == "__main__":
    main()
