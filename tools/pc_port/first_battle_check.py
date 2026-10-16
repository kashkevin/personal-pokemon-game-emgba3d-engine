#!/usr/bin/env python3
"""Regression check: the story's first battle has the Route 101 world behind it, not a black one.

Update 26, bug 3. Birch's bag on Route 101 runs a script that fades the screen to black (fadescreen), opens the starter choice
and starts the first battle. The 3D battle stands on "the field as last seen with its palettes steady"
(gl_compositor.c DioramaGL_GetBattleWorld); the snapshot only called a palette unsteady while a fade was *running*
(gPaletteFade.active), so the field standing there faded out whole - every palette black - counted as steady, and the first
battle's whole world was black (scene_snapshot.c now counts a fade that has run to its end as not steady, as the native
interface's CaptureScreenFade does).

This runs the 3D game (the Diorama build) headless under xvfb through the same story path (PKMN_DEBUG_BATTLE=bag runs the
script of Birch's bag), takes window screenshots inside the battle and measures the stretch of scenery to the left of the wild
Pokemon: black (before the fix: 0 % lit pixels) or lit scenery. It also reads the battle's own log line
("Diorama battle: 3D on map 0.16 (wild)").

    tools/pc_port/first_battle_check.py [--binary ./pokeemerald-mp-diorama]

Needs Pillow and xvfb-run. A simulation on a software display; the pictures are in docs/update26-bug-fixes.md.
"""
import argparse
import glob
import os
import shutil
import subprocess
import sys
import tempfile

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SHOT_FRAMES = (1350, 1650, 1800)  # inside the battle (it runs from about frame 130 to 2540 on this script)
LIT = 24  # a channel above this is not black
MIN_LIT = 0.6  # of the sampled stretch


def keys():
    first = [f"{f}:A" for f in range(500, 1001, 100)]
    return ",".join(first + [f"{f}:A" for f in range(1100, 5001, 60)])


def lit_fraction(path):
    img = Image.open(path).convert("RGB")
    w, h = img.size
    box = img.crop((int(w * 0.05), int(h * 0.25), int(w * 0.30), int(h * 0.55)))
    raw = box.tobytes()
    px = [raw[i:i + 3] for i in range(0, len(raw), 3)]
    return sum(1 for p in px if max(p) > LIT) / len(px)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--binary", default=os.path.join(ROOT, "pokeemerald-mp-diorama"))
    ap.add_argument("--timeout", type=int, default=1200)
    args = ap.parse_args()
    if not os.path.exists(args.binary):
        print(f"first_battle_check: {args.binary} not found (make -f Makefile_pc NATIVE_LINUX=1 DIORAMA=1)")
        return 2
    work = tempfile.mkdtemp(prefix="first-battle-check-")
    env = dict(os.environ, PKMN_DEBUG_SCENE="field", PKMN_DEBUG_HERE="0,16,7,15", PKMN_DEBUG_BATTLE="bag:0:0", PKMN_DEBUG_BATTLE_AT="100",
               PKMN_DEBUG_KEYS=keys(), PKMN_DEBUG_GLSHOTS=",".join(str(f) for f in SHOT_FRAMES), SDL_AUDIODRIVER="dummy")
    xvfb = shutil.which("xvfb-run")
    cmd = ([xvfb, "-a", "-s", "-screen 0 1280x800x24"] if xvfb and not os.environ.get("DISPLAY") else []) + [
        "timeout", "-k", "3", str(args.timeout), os.path.abspath(args.binary)]
    result = subprocess.run(cmd, cwd=work, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace")
    problems = []
    log = [l for l in result.stdout.splitlines() if "Diorama battle:" in l]
    if not any("3D on map" in l for l in log):
        problems.append("the first battle was not drawn in 3D (log: %s)" % (log or "no 'Diorama battle' line"))
    shots = sorted(glob.glob(os.path.join(work, "screenshots", "*.png")))
    if len(shots) != len(SHOT_FRAMES):
        problems.append(f"expected {len(SHOT_FRAMES)} screenshots, found {len(shots)}\n" + result.stdout[-1200:])
    for path in shots:
        lit = lit_fraction(path)
        if lit < MIN_LIT:
            problems.append(f"{os.path.basename(path)}: the battle's scenery is black ({lit:.0%} lit, at least {MIN_LIT:.0%} wanted)")
    if problems:
        print("first_battle_check: FAILED (pictures kept in %s)" % work)
        for p in problems:
            print("  " + p)
        return 1
    shutil.rmtree(work, ignore_errors=True)
    print("first_battle_check: ok (the first battle stands in the 3D Route 101: " + "; ".join(log) + ")")
    return 0


if __name__ == "__main__":
    sys.exit(main())
