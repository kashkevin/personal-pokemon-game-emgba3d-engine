#!/usr/bin/env python3
"""Native 3D move animations, played in a real wild battle and captured from the window (Update 27).

    tools/pc_port/move_3d_check.py OUT_DIR MOVE_ID [MOVE_ID ...] [--binary ./pokeemerald-mp-diorama] [--jobs 2]

For each move: the debug field scene (src/platform/debug_scene.c) gives a Level 30 Pokemon whose first move is that move
(PKMN_DEBUG_MOVES), a Level 90 wild Zigzagoon is the foe, A is pressed through the battle, and the GBA frame is captured every
8 game frames (PKMN_DEBUG_SHOTS). Measured from the captures:

  attack   up to 10 captures every 5th drawn frame while the Pokemon's move animation runs (PKMN_DEBUG_ANIMSHOTS); a capture
           differs from the stage's usual picture (the per-pixel median of every capture of the run) where the effect sprites, the
           background changes and the flashes of the GBA's own animation are; "drawn" is true when more than 0.4 % of the stage's
           pixels differ in at least 3 captures (so run it with several moves: the median needs them)
  The best frames of each move go to OUT_DIR/<MOVE>.png (a strip) and the verdicts to OUT_DIR/move3d.json.

Classic 2D plays the game's original animations through the GBA picture; there is no HD layer here. A simulation: it says
the effect drew into the picture, not that it looks right on a monitor (the strips are for looking at).
"""
import argparse
import concurrent.futures
import glob
import json
import os
import shutil
import subprocess
import sys
import tempfile

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
FRAMES = list(range(480, 1700, 8))


def numbers():
    """Every move of the game by number (tools/battlefx/move_numbers.json: compiled from include/constants/moves.h)."""
    return json.load(open(os.path.join(ROOT, "tools", "battlefx", "move_numbers.json")))


def run_one(binary, move, num, out_dir, timeout):
    work = tempfile.mkdtemp(prefix="move3d-")
    keys = ",".join(f"{f}:A" for f in range(300, 4000, 36))
    env = dict(os.environ, PKMN_DEBUG_SCENE="field", PKMN_DEBUG_HERE="0,16,12,14", PKMN_DEBUG_MOVES=str(num),
               PKMN_DEBUG_BATTLE="wild:%s:%s" % (os.environ.get("PKMN_CHECK_FOE", "263"), os.environ.get("PKMN_CHECK_FOE_LEVEL", "90")), PKMN_DEBUG_BATTLE_AT="60", PKMN_DEBUG_KEYS=keys,
               PKMN_DEBUG_GLANIM="8:10", SDL_AUDIODRIVER="dummy")
    xvfb = shutil.which("xvfb-run")
    cmd = ([xvfb, "-a", "-s", "-screen 0 1280x800x24"] if xvfb and not os.environ.get("DISPLAY") else []) + [
        "timeout", "-k", "3", str(timeout), os.path.abspath(binary)]
    subprocess.run(cmd, cwd=work, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    raw = os.path.join(out_dir, "raw")
    os.makedirs(raw, exist_ok=True)
    paths = []
    for k, p in enumerate(sorted(glob.glob(os.path.join(work, "screenshots", "*.png")))):
        dst = os.path.join(raw, f"{move}-3d{k:02d}.png")
        shutil.move(p, dst)
        paths.append(dst)
    shutil.rmtree(work, ignore_errors=True)
    return {"move": move, "num": num, "captures": paths}


def analyse(results, out_dir):
    import numpy as np

    allimgs = [(r, p) for r in results for p in r["captures"]]
    if not allimgs:
        return results
    crops = {}
    for r, p in allimgs:
        im = Image.open(p).convert("RGB")
        w, h = im.size
        crops[p] = np.asarray(im.crop((0, 0, w, int(h * 0.80)))).astype(int)
    med = np.median(np.stack(list(crops.values())), axis=0)
    for r in results:
        diffs = [float((abs(crops[p] - med).max(axis=2) > 40).mean()) for p in r["captures"]]
        r["diffs"] = [round(d, 4) for d in diffs]
        r["active_frames"] = sum(d > 0.004 for d in diffs)
        r["drawn"] = r["active_frames"] >= 3
        r["peak"] = round(max(diffs), 4) if diffs else 0
        if r["captures"]:
            imgs = [Image.open(p).convert("RGB") for p in r["captures"]]
            w, h = imgs[0].size
            pick = list(range(len(imgs)))[:: max(1, len(imgs) // 5)][:5]
            strip = Image.new("RGB", (len(pick) * w, h))
            for k, i in enumerate(pick):
                strip.paste(imgs[i], (k * w, 0))
            strip.save(os.path.join(out_dir, f"{r['move']}.png"))
        r["captures"] = len(r["captures"])
    return results


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out_dir")
    ap.add_argument("moves", nargs="+")
    ap.add_argument("--binary", default=os.path.join(ROOT, "pokeemerald-mp-diorama"))
    ap.add_argument("--jobs", type=int, default=2)
    ap.add_argument("--timeout", type=int, default=420)
    a = ap.parse_args()
    os.makedirs(a.out_dir, exist_ok=True)
    nums = numbers()
    todo = [m for m in a.moves if m in nums]
    results = []
    with concurrent.futures.ThreadPoolExecutor(a.jobs) as ex:
        futs = [ex.submit(run_one, a.binary, m, nums[m], a.out_dir, a.timeout) for m in todo]
        for f in futs:
            results.append(f.result())
    analyse(results, a.out_dir)
    for r in results:
        print(r, flush=True)
    json.dump(results, open(os.path.join(a.out_dir, "move3d.json"), "w"), indent=1)
    bad = [r for r in results if not r["drawn"]]
    print(f"move_3d_check: {len(results) - len(bad)} of {len(results)} moves drew an effect into the 3D view")
    return 0 if not bad else 1


if __name__ == "__main__":
    sys.exit(main())
