#!/usr/bin/env python3
"""Regression check of the 3D repairs after the first real-PC play (2026-10-17), on the 3D (Diorama) game, headless.

  1. fade:  walking into Oldale's Pokemon Center, a frame the game has faded to black is black - no lit window, no shadow
            (the renderer's own colours used to stay on the black screen through every door);
  2. grass: in a battle whose world is redrawn at every tile change (PKMN_DIORAMA_BATTLE_TILES=1), a patch of tall grass
            beside the foe does not change between captures (it swayed by a running clock while the battle shows its
            world as a picture redrawn now and then, so its blades jumped);
  3. pc:    switching the Pokemon Center's PC on leaves the modelled room standing (the lit PC's metatile used to drop the
            PC, the counter's arm and the back wall: black notches, a flickering room);
  4. bag:   with the native bag on screen a pocket switch reaches the pocket the bag draws within 3 game frames of the
            press (the hidden GBA list's 16-frame repaint delayed it about 0.3 s);
  6. vehicles: the large vehicles stay in the 3D view (KNOWN_ISSUES P7: a sprite drawn as several OAM parts was
            refused and the whole view fell back to 2D): the moving trucks, the S.S. Tidal (Slateport, Lilycove,
            Southern Island), the submarine's shadow (Slateport, the Aqua Hideout), the Cable Car, Rayquaza (Sky
            Pillar, still and awake; Sootopolis); each on the field, no object refused, 3D throughout; and the
            submarine's escape scene and the S.S. Tidal's departure play to their end in 3D;
  5. link:  the pause menu's link status - OFFLINE without a bridge; SETUP (and the clock at 12:00 noon) for the
            launcher's character setup (--setup); CONNECTING, then NOT CONNECTED after 20 s, for a bridge whose
            companion never reports a world session, with the role from --link-role.

    tools/pc_port/gameplay_repair_check.py [--binary ./pokeemerald-mp-diorama]

Needs Pillow, NumPy and an X display (xvfb-run is used when DISPLAY is not set). Pictures of a failure are kept.
"""
import argparse
import glob
import os
import re
import shutil
import subprocess
import sys
import tempfile

import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ROUTE101_DONE = "v4060=3,f2ee=1,f2d0=1,f2bc=1"  # Route 101's rescue finished: no scene on the way


def run(binary, work, env_extra, timeout, args=()):
    os.makedirs(work, exist_ok=True)
    env = dict(os.environ, SDL_AUDIODRIVER="dummy", PKMN_GRAPHICS_NO_DIALOG="1", PKMN_DEBUG_SCENE="field",
               PKMN_DEBUG_GLSHOTS_GAME="1", **dict(dict(PKMN_DEBUG_HOUR="12"), **env_extra))
    env = {k: v for k, v in env.items() if v is not None}
    xvfb = shutil.which("xvfb-run")
    cmd = ([xvfb, "-a", "-s", "-screen 0 1280x800x24"] if xvfb and not os.environ.get("DISPLAY") else []) + [
        "timeout", "-k", "3", str(timeout), os.path.abspath(binary), *args]
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
    # tall grass right of the foe, above the HUD's databox (since 0493b77b79 the world sits higher, with the back
    # pictures on the HUD panel's top edge, and the old box took in the databox and the blinking command panel)
    box = (int(image.width * 0.82), int(image.height * 0.50), image.width - 4, int(image.height * 0.70))
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


def check_bag(binary, work, problems):
    presses = (700, 800, 900)  # RIGHT, RIGHT, LEFT in the bag (START > the third row, Bag > A)
    _, log = run(binary, os.path.join(work, "bag"), dict(
        PKMN_DEBUG_HERE="0,9,5,9,0", PKMN_DEBUG_BAG_LOG="1", PKMN_DEBUG_GLSHOTS="980",
        PKMN_DEBUG_KEYS="200:START,330:DOWN,400:DOWN,470:A,700:RIGHT,800:RIGHT,900:LEFT"), 300)
    changes = [(int(f), int(p)) for f, p in re.findall(r"bag pocket (\d+): (\d+)", log) if int(f) > 600]
    if [p for _, p in changes] != [1, 2, 1]:
        problems.append(f"bag: pockets {changes}, expected 1, 2, 1 after the three presses\n{log[-600:]}")
        return None
    late = max(f - press for (f, _), press in zip(changes, presses))
    if late > 3:
        problems.append(f"bag: a pocket switch reached the bag {late} game frames after the press (at most 3)")
    return late


def check_link(binary, work, problems):
    bridge = os.path.join(work, "bridge.txt")
    os.makedirs(work, exist_ok=True)
    with open(bridge, "w") as f:
        f.write("port=1\ntoken=" + "ab" * 32 + "\n")  # no companion there: never a world session
    cases = (("offline", (), "980", [(0, "-")], None),
             ("setup", ("--setup",), "980", [(1, "-")], "12:0"),
             ("host", ("--bridge", bridge, "--link-role", "host"), "1300", [(2, "h"), (3, "h")], None))
    for name, args, end, want, clock in cases:
        _, log = run(binary, os.path.join(work, "link-" + name), dict(
            PKMN_DEBUG_HERE="0,9,5,9,0", PKMN_DEBUG_LINK_LOG="1", PKMN_DEBUG_GLSHOTS=end, PKMN_DEBUG_HOUR=None), 300, args)
        states = [(int(f), int(s), r) for f, s, r in re.findall(r"link state (\d+): (\d+) role (\S)", log)]
        if [(s, r) for _, s, r in states] != want:
            problems.append(f"link {name}: states {states}, expected {want}\n{log[-600:]}")
        elif name == "host" and not 1150 <= states[1][0] <= 1260:
            problems.append(f"link host: NOT CONNECTED at game frame {states[1][0]}, expected about 1200 (20 s)")
        seen = re.search(r"clock 300: (\d\d:\d\d)", log)
        if clock is not None and (seen is None or not seen.group(1).startswith(clock)):
            problems.append(f"link {name}: the clock read {seen.group(1) if seen else None}, expected {clock}x")
    return len(cases)


# name, PKMN_DEBUG_HERE, flags/vars to set before the map loads, the vehicle's local id
VEHICLES = (
    ("truck-littleroot", "0,9,5,12", "", 5),
    ("ss-tidal-slateport", "9,9,10,12", "f35c=0,f350=0", 5),
    ("submarine-slateport", "9,9,10,12", "f35c=0,f350=0", 8),
    ("ss-tidal-lilycove", "13,10,10,12", "f35d=0", 2),
    ("ss-tidal-southern-island", "26,9,13,22", "", 2),
    ("submarine-aqua-hideout", "24,25,17,21", "f3af=0", 4),
    ("cable-car-route112", "19,0,6,6", "", 2),
    ("rayquaza-still-sky-pillar", "24,85,14,11", "f50=0", 2),
    ("rayquaza-sky-pillar", "24,85,14,11", "f305=0,f50=1", 1),
    ("rayquaza-sootopolis", "0,7,31,45", "f3e4=0", 11),
)


def vehicle_run(binary, work, name, here, flags, local_id, keys="", end="200", watch="v4001"):
    _, log = run(binary, os.path.join(work, "vehicle-" + name), dict(
        PKMN_DEBUG_HERE=here, PKMN_DEBUG_RIVAL_SET=flags or None, PKMN_DEBUG_RIVAL_WATCH=watch,
        PKMN_DEBUG_RIVAL_OBJ=str(local_id), PKMN_DIORAMA_LOG="1", PKMN_DEBUG_FRAMES="1", PKMN_DEBUG_KEYS=keys or None,
        PKMN_DEBUG_GLSHOTS=end), 300)
    return log


def vehicle_problems(name, log, local_id, expect_present=True):
    problems = []
    decisions = [int(d) for d in re.findall(r"present \d+: terrain \d decision (\d)", log)]
    if re.search(r"refused \(", log):
        problems.append(f"vehicle {name}: an object was refused: " + re.search(r".*refused \(.*", log).group(0))
    if 3 not in decisions:
        problems.append(f"vehicle {name}: the 3D view never came up ({decisions})")
    if expect_present and not re.search(rf"\| obj {local_id} 1", log):
        problems.append(f"vehicle {name}: the vehicle (local id {local_id}) was not on the field")
    return problems, decisions


def check_vehicles(binary, work, problems):
    checked = 0
    for name, here, flags, local_id in VEHICLES:
        log = vehicle_run(binary, work, name, here, flags, local_id)
        found, decisions = vehicle_problems(name, log, local_id)
        first3d = decisions.index(3) if 3 in decisions else len(decisions)
        if any(d != 3 for d in decisions[first3d:]):
            found.append(f"vehicle {name}: the view left 3D ({decisions})")
        problems.extend(found)
        checked += 1
    # The submarine's escape (Slateport harbour, the Aqua scene): it sails off in 3D, is removed, the story goes on.
    keys = "150:LEFT,175:LEFT," + ",".join(f"{f}:A" for f in range(400, 1700, 60))
    log = vehicle_run(binary, work, "submarine-escape", "9,9,9,12", "v40a0=1,f350=0,f34d=0,f34e=0", 8, keys, "1700",
                      "v40a0")
    found, decisions = vehicle_problems("submarine-escape", log, 8)
    if not re.search(r"v40a0=2", log):
        found.append("vehicle submarine-escape: the scene did not end (VAR_SLATEPORT_HARBOR_STATE never 2)")
    if any(d != 3 for d in decisions[decisions.index(3):] if 3 in decisions):
        found.append(f"vehicle submarine-escape: the view left 3D ({decisions})")
    problems.extend(found)
    # The S.S. Tidal's departure from Southern Island: the player boards, the ship sails, the game warps to Lilycove.
    keys = "150:DOWN," + ",".join(f"{f}:A" for f in range(220, 800, 60))
    log = vehicle_run(binary, work, "ss-tidal-departure", "26,9,13,22", "", 2, keys, "1000")
    found, _ = vehicle_problems("ss-tidal-departure", log, 2)
    if not re.search(r"rival \d+: map 13\.10 ", log):
        found.append("vehicle ss-tidal-departure: the ferry never reached Lilycove")
    problems.extend(found)
    return checked + 2


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
    bag = check_bag(args.binary, work, problems)
    links = check_link(args.binary, work, problems)
    vehicles = check_vehicles(args.binary, work, problems)
    if problems:
        print("gameplay_repair_check: FAILED (pictures kept in %s)" % work)
        for p in problems:
            print("  " + p)
        return 1
    shutil.rmtree(work, ignore_errors=True)
    print(f"gameplay_repair_check: ok ({dark} faded-out frames black; battle grass still, at most {grass:.1%} changed; "
          f"the room stood as the PC went on, {pc:.1%} changed; pocket switches {bag} game frames after the press; "
          f"{links} link-status cases; {vehicles} vehicle scenes stayed 3D)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
