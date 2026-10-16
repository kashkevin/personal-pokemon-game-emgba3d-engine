#!/usr/bin/env python3
"""Regression check: the party menu can be entered, inspected and left again, twice, with no memory error.

Update 26, bug 2 ("crash when leaving the Pokemon menu"). The Windows crash was not reproducible by name on Linux, which
tolerates what Windows does not, so this runs the sequence under AddressSanitizer, which reports what a Linux run silently
survives:

  * the party menu came back from the summary screen with Task_TryCreateSelectionWindow set, before that task built the action
    list: the native interface read numActions / actions from the freshly allocated, never-written PartyMenuInternal and indexed
    gFieldMoveInfo / asked GetMoveName with the garbage (party_menu.c PartyMenu_NativeUiState);
  * the 3D view's far-scenery colours read metatile colours past the end of a smaller tileset's metatile array
    (scene_snapshot.c ComputeFar);
  * the weather-sprite test read the sprite template of a mon icon, a template on a stack frame long gone
    (battle_capture.c IsMapSprite).

The script: a fresh game in the overworld (PKMN_DEBUG_SCENE=field gives the party, the badges and the POKeMON menu entry), then
START > POKeMON > the first mon > SUMMARY, B (to the party), B (to the menu), B (to the overworld), and the whole of it again.
It passes when the game ends by itself at the last frame with no AddressSanitizer report.

    tools/pc_port/party_menu_check.py [--binary ./pokeemerald-mp-asan-diorama]

Build the binary with   make -f Makefile_pc NATIVE_LINUX=1 SANITIZE=address DIORAMA=1
(the plain 2D build, SANITIZE=address without DIORAMA, takes the same script: its game screens are the GBA's own). Needs xvfb-run.
A simulation on a software display: it does not replace a run on the Windows PC.
"""
import argparse
import glob
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
LAST_FRAME = 4400
KEYS = ("200:START,330:DOWN,400:A,600:A,700:A,1300:B,1500:B,1700:B,1900:B,"
        "2200:START,2330:DOWN,2400:A,2600:A,2700:A,3300:B,3500:B,3700:B,3900:B")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--binary", default=os.path.join(ROOT, "pokeemerald-mp-asan-diorama"))
    ap.add_argument("--timeout", type=int, default=1500)
    args = ap.parse_args()
    if not os.path.exists(args.binary):
        print(f"party_menu_check: {args.binary} not found (see --help for the build command)")
        return 2
    work = tempfile.mkdtemp(prefix="party-menu-check-")
    env = dict(os.environ, ASAN_OPTIONS=f"detect_leaks=0:log_path={work}/asan", PKMN_DEBUG_SCENE="field", PKMN_DEBUG_HERE="0,9,9,10",
               PKMN_DEBUG_KEYS=KEYS, SDL_AUDIODRIVER="dummy")
    env["PKMN_DEBUG_GLSHOTS" if "diorama" in os.path.basename(args.binary) else "PKMN_DEBUG_SHOTS"] = str(LAST_FRAME)
    xvfb = shutil.which("xvfb-run")
    cmd = ([xvfb, "-a", "-s", "-screen 0 1280x800x24"] if xvfb and not os.environ.get("DISPLAY") else []) + [
        "timeout", "-k", "3", str(args.timeout), os.path.abspath(args.binary)]
    result = subprocess.run(cmd, cwd=work, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace")
    reports = sorted(glob.glob(os.path.join(work, "asan.*")))
    shots = glob.glob(os.path.join(work, "screenshots", "*.png")) + glob.glob(os.path.join(work, "debug-scene-*.png"))
    problems = []
    for r in reports:
        with open(r, errors="replace") as f:
            problems.append("AddressSanitizer report:\n" + "".join(f.readlines()[:14]))
    if not reports and result.returncode != 0:
        problems.append(f"the game exited with status {result.returncode} before the last frame\n" + result.stdout[-1500:])
    if not reports and not shots:
        problems.append("no screenshot at the last frame: the sequence did not run to its end\n" + result.stdout[-1500:])
    if problems:
        print("party_menu_check: FAILED")
        for p in problems:
            print(p)
        return 1
    shutil.rmtree(work, ignore_errors=True)
    print("party_menu_check: ok (party menu entered, inspected and left twice, no memory error)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
