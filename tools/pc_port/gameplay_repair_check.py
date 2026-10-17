#!/usr/bin/env python3
"""Regression check of the 3D repairs after the first real-PC play (2026-10-17), on the 3D (Diorama) game, headless.

  1. fade:  walking into Oldale's Pokemon Center, a frame the game has faded to black is black - no lit window, no shadow
            (the renderer's own colours used to stay on the black screen through every door);
  2. grass: in a battle whose world is redrawn at every tile change (PKMN_DIORAMA_BATTLE_TILES=1), a patch of tall grass
            beside the foe does not change between captures (it swayed by a running clock while the battle shows its
            world as a picture redrawn now and then, so its blades jumped);
  3. pc:    switching the Pokemon Center's PC on leaves the modelled room standing (the lit PC's metatile used to drop the
            PC, the counter's arm and the back wall: black notches, a flickering room).

    tools/pc_port/gameplay_repair_check.py [--binary ./pokeemerald-mp-diorama]

Needs Pillow, NumPy and an X display (xvfb-run is used when DISPLAY is not set). Pictures of a failure are kept.
"""
import argparse
import glob
import os
import shutil
import subprocess
import sys
import tempfile

import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ROUTE101_DONE = "v4060=3,f2ee=1,f2d0=1,f2bc=1"  # Route 101's rescue finished: no scene on the way


def run(binary, work, env_extra, timeout):
    os.makedirs(work, exist_ok=True)
    env = dict(os.environ, SDL_AUDIODRIVER="dummy", PKMN_GRAPHICS_NO_DIALOG="1", PKMN_DEBUG_SCENE="field",
               PKMN_DEBUG_GLSHOTS_GAME="1", **dict(dict(PKMN_DEBUG_HOUR="12"), **env_extra))
    xvfb = shutil.which("xvfb-run")
    cmd = ([xvfb, "-a", "-s", "-screen 0 1280x800x24"] if xvfb and not os.environ.get("DISPLAY") else []) + [
        "timeout", "-k", "3", str(timeout), os.path.abspath(binary)]
    out = subprocess.run(cmd, cwd=work, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                         errors="replace")
    shots = sorted(glob.glob(os.path.join(work, "screenshots", "*.png")), key=os.path.getmtime)
    return shots, out.stdout


def grey(path, box=None):
    image = Image.open(path).convert("L")
    return np.asarray(image.crop(box) if box else image, dtype=np.int16)


def check_fade(binary, work, problems):
    frames = list(range(150, 278, 4))
    shots, log = run(binary, os.path.join(work, "fade"), dict(
        PKMN_DEBUG_HERE="0,10,6,17", PKMN_DEBUG_KEYS="150:UP,154:UP,158:UP", PKMN_DEBUG_HOUR="21",  # windows lit
        PKMN_DEBUG_GLSHOTS=",".join(map(str, frames))), 300)
    if len(shots) != len(frames):
        problems.append(f"fade: {len(shots)} of {len(frames)} pictures\n{log[-600:]}")
        return
    dark = [s for s in shots if grey(s).mean() < 2.0]
    if not dark:
        problems.append("fade: the door's fade never reached black")
    for s in dark:
        lit = int((grey(s) > 12).sum())
        if lit != 0:
            problems.append(f"fade: {os.path.basename(s)} is faded out yet {lit} pixels are lit")
    return len(dark)


def check_grass(binary, work, problems):
    frames = list(range(600, 1100, 25))
    keys = ",".join(f"{f}:A" for f in range(380, 560, 40))
    shots, log = run(binary, os.path.join(work, "grass"), dict(
        PKMN_DIORAMA_BATTLE_TILES="1", PKMN_DEBUG_HERE="0,16,10,16", PKMN_DEBUG_RIVAL_SET=ROUTE101_DONE,
        PKMN_DEBUG_BATTLE="wild:263:3", PKMN_DEBUG_BATTLE_AT="60", PKMN_DEBUG_KEYS=keys,
        PKMN_DEBUG_GLSHOTS=",".join(map(str, frames))), 600)
    if "Diorama battle: 3D" not in log or len(shots) != len(frames):
        problems.append(f"grass: no 3D battle or {len(shots)} of {len(frames)} pictures\n{log[-600:]}")
        return
    image = Image.open(shots[0])
    box = (int(image.width * 0.68), int(image.height * 0.60), image.width - 4, int(image.height * 0.82))
    changes = [float((np.abs(grey(b, box) - grey(a, box)) > 24).mean()) for a, b in zip(shots, shots[1:])]
    worst = max(changes)
    if worst > 0.03:
        problems.append(f"grass: the battle's grass moved between pictures ({worst:.1%} of the patch changed)")
    return worst


def check_pc(binary, work, problems):
    frames = [170, 195, 470]
    shots, log = run(binary, os.path.join(work, "pc"), dict(
        PKMN_DEBUG_HERE="2,2,10,2", PKMN_DEBUG_KEYS="150:UP,180:A,240:A,300:A,360:A,420:A",
        PKMN_DEBUG_GLSHOTS=",".join(map(str, frames))), 300)
    if len(shots) != len(frames):
        problems.append(f"pc: {len(shots)} of {len(frames)} pictures\n{log[-600:]}")
        return
    before, after = grey(shots[0]), grey(shots[2])
    h, w = before.shape
    # The room's west half and the back wall over it: the counter, the healer, the nurse's corner, the wall.
    # (The PC, the player and the menus are on the east side and at the bottom.)
    box = (slice(0, int(h * 0.62)), slice(0, int(w * 0.46)))
    changed = float((np.abs(after[box] - before[box]) > 24).mean())
    if changed > 0.03:
        problems.append(f"pc: the room changed when the PC went on ({changed:.1%} of its west half)")
    return changed


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", default=os.path.join(ROOT, "pokeemerald-mp-diorama"))
    args = parser.parse_args()
    if not os.path.exists(args.binary):
        print(f"gameplay_repair_check: {args.binary} not found (make -f Makefile_pc NATIVE_LINUX=1 DIORAMA=1)")
        return 2
    work = tempfile.mkdtemp(prefix="gameplay-repair-check-")
    problems = []
    dark = check_fade(args.binary, work, problems)
    grass = check_grass(args.binary, work, problems)
    pc = check_pc(args.binary, work, problems)
    if problems:
        print("gameplay_repair_check: FAILED (pictures kept in %s)" % work)
        for p in problems:
            print("  " + p)
        return 1
    shutil.rmtree(work, ignore_errors=True)
    print(f"gameplay_repair_check: ok ({dark} faded-out frames black; battle grass still, at most {grass:.1%} changed; "
          f"the room stood as the PC went on, {pc:.1%} changed)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
