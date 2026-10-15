#!/usr/bin/env python3
"""Visual regression check for the four-player free-for-all battle screen (Classic 2D path).

Runs the headless free-for-all preview (PKMN_DEBUG_SCENE=ffa, src/platform/debug_scene.c) under xvfb, takes GBA-frame
screenshots at fixed frames and compares each with a stored *signature*: the picture reduced to a 24 x 16 grid of mean
colours. No picture is stored in the repository, only numbers, so the check carries no game art. A change that moves,
hides or overlaps a healthbox, a name or a Pokemon changes the grid.

    tools/pc_port/ffa_visual_check.py [--binary ./pokeemerald-mp] [--update]

--update rewrites tests/visual/ffa_signatures.json from the current build (do that on purpose, after looking at the
pictures). Needs Pillow and xvfb-run. This is a simulation on a software display: it says the layout is the one that was
approved, not that it looks right on a real monitor.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SIGNATURES = os.path.join(ROOT, "tests", "visual", "ffa_signatures.json")
GRID = (24, 16)
TOLERANCE = 9.0  # mean absolute difference per channel, 0-255
# the script presses A through the intro (every 70 frames), then FIGHT at the action menu
FRAMES = {"sendout": 760, "action": 1340}


def keys():
    return ",".join(f"{f}:A" for f in range(200, 1300, 70))


def capture(binary, out_dir):
    env = dict(os.environ, PKMN_DEBUG_SCENE="ffa", PKMN_DEBUG_KEYS=keys(), PKMN_DEBUG_SHOTS=",".join(str(f) for f in FRAMES.values()),
               PKMN_DEBUG_STEM="ffa", SDL_AUDIODRIVER="dummy")
    xvfb = shutil.which("xvfb-run")
    cmd = ([xvfb, "-a"] if xvfb and not os.environ.get("DISPLAY") else []) + ["timeout", "-k", "3", "120", os.path.abspath(binary)]
    subprocess.run(cmd, cwd=out_dir, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)


def signature(path):
    img = Image.open(path).convert("RGB").resize(GRID, Image.BOX)
    raw = img.tobytes()
    return [list(raw[i:i + 3]) for i in range(0, len(raw), 3)]


def distance(a, b):
    total = sum(abs(x - y) for pa, pb in zip(a, b) for x, y in zip(pa, pb))
    return total / (len(a) * 3)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--binary", default=os.path.join(ROOT, "pokeemerald-mp"))
    ap.add_argument("--update", action="store_true")
    args = ap.parse_args()
    with tempfile.TemporaryDirectory() as tmp:
        capture(args.binary, tmp)
        found = {}
        for name, frame in FRAMES.items():
            shot = os.path.join(tmp, f"ffa-{frame:04d}.png")
            if not os.path.exists(shot):
                print(f"FAIL {name}: the preview wrote no screenshot at frame {frame}")
                return 1
            found[name] = signature(shot)
        if args.update:
            os.makedirs(os.path.dirname(SIGNATURES), exist_ok=True)
            with open(SIGNATURES, "w") as f:
                json.dump({"grid": GRID, "frames": FRAMES, "signatures": found}, f)
            print("updated", SIGNATURES)
            return 0
        with open(SIGNATURES) as f:
            stored = json.load(f)["signatures"]
        bad = 0
        for name, sig in found.items():
            d = distance(sig, stored[name])
            print(f"{'ok  ' if d <= TOLERANCE else 'FAIL'} {name}: difference {d:.2f} (limit {TOLERANCE})")
            bad += d > TOLERANCE
        return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
