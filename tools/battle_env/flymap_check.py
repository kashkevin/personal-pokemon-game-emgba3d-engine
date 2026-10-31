#!/usr/bin/env python3
"""Fly from the maps (PC only; the owner's request of 2026-10-31, "let the map be used for fly"), in a test build (the 3D
test build or the Classic 2D test build). When Fly could be used there and then - the Feather Badge, a Pokemon in the
party or the PC boxes that knows Fly, a place it can be used from:
  - the PokeNav's HOENN MAP (START > POKeNAV > HOENN MAP; the map every player has): R on a place Fly can reach flies
    there (the expansion's Poke Rider, its help bar's "R FLY"), with that Pokemon; otherwise R does nothing;
  - the Town Map (registered on SELECT here; no script of this game gives it): it is the Fly map - a place chosen there
    is flown to, B goes back to the field; otherwise it is the map to look at, as before.
The game's own log of the player's map and place (PKMN_TEST_HM_LOG) is what is checked; pictures are taken of each map.

usage: flymap_check.py --exe pokeemerald-mp-diorama-bfxtest --save boy.sav --out DIR"""
import argparse
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import mega_check as mc  # noqa: E402  (the game in a folder of its own)

TOWN_MAP = 713
MOVE_FLY = 19
PIDGEOT = 18
OLDALE = "0:10:0:10:10"            # Oldale Town's square (map 0.10); Littleroot Town (0.9) two places south on the map
HM = re.compile(r"hm field: frame (\d+) .* map (\d+)\.(\d+) at (\d+),(\d+)")
SHOTS = "100,170,560"
FLAG_SYS_POKENAV_GET = 0x862       # include/constants/flags.h (SYSTEM_FLAGS + 2)
# START > POKeNAV (the third entry: POKeMON, BAG, POKeNAV), the PokeNav's HOENN MAP (its first entry) on Oldale Town;
# DOWN twice to Littleroot Town; R
NAV = ["30:START", "80:DOWN", "110:DOWN", "150:A", "330:A", "460:DOWN", "500:DOWN", "560:R"]
NAV_SHOTS = "300,420,540,600,760,1050"


def run(args, name, frames, keys, party, warp=OLDALE, badges="8", box=None, pokenav=False, shots=SHOTS):
    rundir = os.path.join(args.out, name)
    shutil.rmtree(rundir, ignore_errors=True)
    os.makedirs(rundir)
    shutil.copy(args.save, os.path.join(rundir, "test.sav"))
    with open(os.path.join(rundir, "pokeemerald.cfg"), "w") as cfg:
        cfg.write(mc.CONFIG)
    env = dict(os.environ, PKMN_TEST_BATTLE=f"field:{frames}", PKMN_TEST_AUTOPILOT="1", PKMN_TEST_BATTLE_DELAY="100",
               PKMN_TEST_FIELD_KEYS=",".join(keys), PKMN_TEST_OWE_NO_BATTLE="1", PKMN_TEST_BADGES=badges,
               PKMN_TEST_WARP=warp, PKMN_TEST_BAG=f"{TOWN_MAP}:1", PKMN_TEST_REGISTER=str(TOWN_MAP),
               PKMN_TEST_PARTY=party, PKMN_TEST_HM_LOG="1", PKMN_TEST_FIELD_SHOTS=shots,
               PKMN_TEST_MEGA_SHOT_STEM=os.path.join(rundir, "shot"), SDL_AUDIODRIVER="dummy", PKMN_NOSCENE="1",
               PKMN_TEST_EXIT="1", PKMN_TEST_LOG=os.path.join(rundir, "test.log"))
    if box:
        env["PKMN_TEST_BOX"] = box
    if pokenav:
        env["PKMN_TEST_SET_FLAGS"] = hex(FLAG_SYS_POKENAV_GET)
    prefix = ["xvfb-run", "-a", "-s", "-screen 0 1280x720x24 -nolisten tcp"] if args.xvfb else []
    with open(os.path.join(rundir, "game.log"), "w") as output:
        subprocess.run(prefix + [os.path.abspath(args.exe), "--save", "test.sav"], cwd=rundir, env=env,
                       stdout=output, stderr=subprocess.STDOUT, timeout=900)
    steps = []
    for line in open(os.path.join(rundir, "test.log"), errors="replace"):
        match = HM.search(line)
        if match:
            steps.append(tuple(int(match.group(i)) for i in range(1, 6)))
    return steps


def at(steps, frame):
    """(map group, map num, x, y) as of that field-key frame."""
    where = None
    for step in steps:
        if step[0] <= frame:
            where = step[1:]
    return where


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--exe", required=True)
    parser.add_argument("--save", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--xvfb", action="store_true", help="the game on a display of its own (xvfb-run)")
    args = parser.parse_args()
    os.makedirs(args.out, exist_ok=True)
    results = []

    def check(label, ok, detail):
        results.append(ok)
        print(f"{'PASS' if ok else 'FAIL'} {label}: {detail}")

    flier = f"{PIDGEOT}:40:v{MOVE_FLY}"
    walker = f"{PIDGEOT}:40"
    # 1. A party Pokemon that knows Fly: SELECT opens the Fly map on Oldale Town; DOWN twice to Littleroot Town, A.
    keys = ["30:SELECT", "120:DOWN", "150:DOWN", "190:A"]
    steps = run(args, "party", 600, keys, flier)
    check("party Fly: in Oldale Town first", at(steps, 25) is not None and at(steps, 25)[:2] == (0, 10), f"{at(steps, 25)}")
    check("party Fly: flown to Littleroot Town", at(steps, 600) is not None and at(steps, 600)[:2] == (0, 9),
          f"at {at(steps, 600)}")
    # 2. The same, B on the Fly map: back in the field (Oldale Town), and walking (LEFT held).
    keys = ["30:SELECT", "150:B"] + [f"{frame}:LEFT" for frame in range(300, 360, 2)]
    steps = run(args, "cancel", 600, keys, flier)
    end = at(steps, 600)
    check("B on the Fly map: back in the field, walking", end is not None and end[:2] == (0, 10) and end[2] < 10,
          f"at {end} (from 10,10)")
    # 3. Fly known only in the PC boxes (HMs from the PC boxes): the same flight.
    keys = ["30:SELECT", "120:DOWN", "150:DOWN", "190:A"]
    steps = run(args, "box", 600, keys, walker, box=f"0:0:{PIDGEOT}:40:v{MOVE_FLY}")
    check("boxed Fly: flown to Littleroot Town", at(steps, 600) is not None and at(steps, 600)[:2] == (0, 9),
          f"at {at(steps, 600)}")
    # 4. No Pokemon knows Fly: the map to look at (no flight; B closes it; still in Oldale Town).
    keys = ["30:SELECT", "120:DOWN", "150:DOWN", "190:A", "300:B", "330:B"]
    steps = run(args, "nofly", 600, keys, walker)
    check("no Fly: the Town Map, no flight", at(steps, 600) is not None and at(steps, 600)[:2] == (0, 10),
          f"at {at(steps, 600)}")
    # 5. No Feather Badge: the same.
    keys = ["30:SELECT", "120:DOWN", "150:DOWN", "190:A", "300:B", "330:B"]
    steps = run(args, "nobadge", 600, keys, flier, badges="0")
    check("no badge: the Town Map, no flight", at(steps, 600) is not None and at(steps, 600)[:2] == (0, 10),
          f"at {at(steps, 600)}")
    # 6. The PokeNav's HOENN MAP, a party Pokemon that knows Fly: R on Littleroot Town flies there; then the PokeNav and
    #    its map open again (the START menu's cursor still on POKeNAV) and close (B: the PokeNav's menu, B: the START
    #    menu, as the PokeNav leaves it, B: the field), and the player walks (LEFT).
    keys = NAV + ["1150:START", "1210:A", "1390:A", "1560:B", "1700:B", "1820:B"]
    keys += [f"{f}:LEFT" for f in range(1900, 1960, 2)]
    steps = run(args, "nav", 2100, keys, flier, pokenav=True, shots=NAV_SHOTS)
    check("PokeNav map: in Oldale Town first", at(steps, 25) is not None and at(steps, 25)[:2] == (0, 10), f"{at(steps, 25)}")
    check("PokeNav map, R: flown to Littleroot Town", at(steps, 1100) is not None and at(steps, 1100)[:2] == (0, 9),
          f"at {at(steps, 1100)}")
    end = at(steps, 2100)
    check("PokeNav map again after the flight: opened and closed, walking", end is not None and end[:2] == (0, 9)
          and end[2] < 5, f"at {end} (from 5,9)")
    # 7. Fly known only in the PC boxes: the same flight.
    steps = run(args, "nav-box", 1150, NAV, walker, box=f"0:0:{PIDGEOT}:40:v{MOVE_FLY}", pokenav=True, shots=NAV_SHOTS)
    check("PokeNav map, boxed Fly: flown to Littleroot Town", at(steps, 1150) is not None and at(steps, 1150)[:2] == (0, 9),
          f"at {at(steps, 1150)}")
    # 8. No Pokemon knows Fly: R does nothing (B, B, B close the map and the PokeNav).
    steps = run(args, "nav-nofly", 1150, NAV + ["700:B", "850:B", "1000:B"], walker, pokenav=True, shots=NAV_SHOTS)
    check("PokeNav map, no Fly: R does nothing", at(steps, 1150) is not None and at(steps, 1150)[:2] == (0, 10),
          f"at {at(steps, 1150)}")
    # 9. No Feather Badge: the same.
    steps = run(args, "nav-nobadge", 1150, NAV + ["700:B", "850:B", "1000:B"], flier, badges="0", pokenav=True, shots=NAV_SHOTS)
    check("PokeNav map, no badge: R does nothing", at(steps, 1150) is not None and at(steps, 1150)[:2] == (0, 10),
          f"at {at(steps, 1150)}")
    print(f"{sum(results)} of {len(results)} checks passed (pictures: {args.out}/*/shot-*.png)")
    return 0 if all(results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
