#!/usr/bin/env python3
"""Regression check: the native battle screen stays in front through the end of a battle (the "GBA UI flash").

Update 26, bug 1: when a battle ended, the game freed its battle data and faded to the overworld while the engine's callback was
still the battle's own. The native battle screen decided "is a battle" by that data, so for the whole fade it let go and the
original GBA healthbox and message panel showed through, before the 3D / native view returned. ui_battle.c now keeps the screen up
(its "teardown" state) until the engine leaves the battle's callback.

This runs the 3D game (the Diorama build with the native interface) headless under xvfb: PKMN_DEBUG_SCENE=field starts a fresh
game in the overworld, PKMN_DEBUG_BATTLE starts a wild or trainer battle, A is pressed through it, PKMN_DEBUG_FRAMES logs every
change of the engine's callback, the fade, the battle flag and the native screen in front (src/platform/debug_scene.c). The check
reads that log:

  * the native "battle" screen appears;
  * while the engine runs the battle's callback (battleCb 1) the screen never changes from "battle" - not for one game frame, the
    end-of-battle fade included (a frame with battleCb 1, fade 1 and screen "battle" must exist, or the fade was not covered);
  * the battle is over, and the screen "battle" is gone afterwards.

    tools/pc_port/battle_end_check.py [--binary ./pokeemerald-mp-diorama] [--kind wild|trainer] [--timeout 900]

Needs xvfb-run. A simulation on a software display: it shows the screen state the compositor receives, not how it looks on a
real monitor (the pictures are in docs/update26-bug-fixes.md).
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
LINE = re.compile(r"game frame (\d+): callback2 \S+ fade (\d+) inBattle (\d+) battleCb (\d+) native screen (\S+)")
OVER = re.compile(r"debug battle: battle (\d+) over")

# trainer 318: TRAINER_CALVIN_1, one Poochyena - a short battle (the slow software-GL run has to see it end)
KINDS = {"wild": "wild:263:5", "trainer": "trainer:318"}


def run(binary, kind, timeout):
    keys = ",".join(f"{f}:A" for f in range(500, 9000, 50))
    env = dict(os.environ, PKMN_DEBUG_SCENE="field", PKMN_DEBUG_HERE="0,16,12,14", PKMN_DEBUG_BATTLE=KINDS[kind],
               PKMN_DEBUG_BATTLE_AT="100", PKMN_DEBUG_KEYS=keys, PKMN_DEBUG_FRAMES="1", SDL_AUDIODRIVER="dummy")
    xvfb = shutil.which("xvfb-run")
    cmd = ([xvfb, "-a", "-s", "-screen 0 1280x800x24"] if xvfb and not os.environ.get("DISPLAY") else []) + [
        "timeout", "-k", "3", str(timeout), os.path.abspath(binary)]
    lines = []
    work = os.path.join(ROOT, "build", "battle-end-check")
    os.makedirs(work, exist_ok=True)
    proc = subprocess.Popen(cmd, cwd=work, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace")
    over_at = None
    try:
        for line in proc.stdout:
            lines.append(line.rstrip("\n"))
            if OVER.search(line) and over_at is None:
                over_at = time.time()
            # let the first frames of the overworld after the battle through, then stop
            if over_at is not None and time.time() - over_at > 25:
                break
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.kill()
    return lines


def check(lines):
    problems = []
    frames = [(int(m[1]), int(m[2]), int(m[3]), int(m[4]), m[5]) for m in (LINE.search(l) for l in lines) if m]
    if not any(OVER.search(l) for l in lines):
        return ["the battle never ended (no 'debug battle: battle 1 over' line): the run is not a valid check"], frames
    first = next((i for i, f in enumerate(frames) if f[4] == "battle"), None)
    if first is None:
        return ["the native battle screen never appeared"], frames
    covered_fade = False
    last_battle_cb = max(i for i, f in enumerate(frames) if f[3] == 1)
    for i in range(first, last_battle_cb + 1):
        frame, fade, in_battle, battle_cb, screen = frames[i]
        if battle_cb == 1 and screen != "battle":
            # mid-battle menus (bag, party) run their own callbacks (battleCb 0); only the battle's own callback is checked
            problems.append(f"game frame {frame}: the battle's callback runs but the native screen is '{screen}' (fade {fade})"
                            " - the GBA battle UI shows")
        if battle_cb == 1 and fade == 1 and screen == "battle" and i > first:
            covered_fade = True
    if not covered_fade:
        problems.append("no game frame with the battle's callback, a fade and the battle screen: the end-of-battle fade was not observed")
    after = frames[last_battle_cb + 1:]
    if any(f[4] == "battle" and f[2] == 0 for f in after):
        problems.append("the native battle screen stayed after the battle was over")
    return problems, frames


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--binary", default=os.path.join(ROOT, "pokeemerald-mp-diorama"))
    ap.add_argument("--kind", choices=sorted(KINDS), default="wild")
    ap.add_argument("--timeout", type=int, default=900)
    args = ap.parse_args()
    if not os.path.exists(args.binary):
        print(f"battle_end_check: {args.binary} not found (make -f Makefile_pc NATIVE_LINUX=1 DIORAMA=1)")
        return 2
    lines = run(args.binary, args.kind, args.timeout)
    problems, frames = check(lines)
    if problems:
        print(f"battle_end_check ({args.kind}): FAILED")
        for p in problems:
            print("  " + p)
        for l in lines[-40:]:
            print("  | " + l)
        return 1
    print(f"battle_end_check ({args.kind}): ok ({len(frames)} state changes; the battle screen stayed through the end-of-battle fade)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
