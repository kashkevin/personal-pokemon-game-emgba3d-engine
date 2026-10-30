#!/usr/bin/env python3
"""The Acro Bike on rails, in the 3D test build (DIORAMA=1 BFX_TEST=1): the Safari Zone South's rails crossed as the
original game asks - hold B until the bike hops, hold B and the way along the rail; on a rail's end, B let go, the way
across and B in one frame for the sideways jump onto the next rail - and the two inputs the original refuses: B a
frame before the way across (a wheelie, no jump), the way across while hopping (nothing). The owner's report of
2026-10-30 ("not able to hop rails with the Acro Bike"). The game's own log of each change of the player's place,
bike state and keys (PKMN_TEST_BIKE_LOG) is what is checked.

usage: acro_check.py --exe pokeemerald-mp-diorama-bfxtest --save boy.sav --out DIR"""
import argparse
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import mega_check as mc  # noqa: E402  (the game in a folder of its own)

ACRO_BIKE = 708
SAFARI_SOUTH = "26:3:0:16:7"   # the west ledge's east edge, the lowest rail's west end beside it (17-20,7)
BIKE = re.compile(r"bike: frame (\d+) at (\d+),(\d+) flags 0x([0-9a-f]+) acro state (\d+)")


def held(key, first, last):
    """A key held every frame from first to last (each press is held three frames)."""
    return [f"{frame}:{key}" for frame in range(first, last + 1, 2)]


def run(args, name, keys, frames):
    rundir = os.path.join(args.out, name)
    shutil.rmtree(rundir, ignore_errors=True)
    os.makedirs(rundir)
    shutil.copy(args.save, os.path.join(rundir, "test.sav"))
    with open(os.path.join(rundir, "pokeemerald.cfg"), "w") as cfg:
        cfg.write(mc.CONFIG)
    env = dict(os.environ, PKMN_TEST_BATTLE=f"field:{frames}", PKMN_TEST_AUTOPILOT="1", PKMN_TEST_BATTLE_DELAY="100",
               PKMN_TEST_FIELD_KEYS=",".join(keys), PKMN_TEST_OWE_NO_BATTLE="1", PKMN_TEST_BADGES="8",
               PKMN_TEST_WARP=SAFARI_SOUTH, PKMN_TEST_SAFARI="1", PKMN_TEST_BAG=f"{ACRO_BIKE}:1",
               PKMN_TEST_REGISTER=str(ACRO_BIKE), PKMN_TEST_BIKE_LOG="1", SDL_AUDIODRIVER="dummy", PKMN_NOSCENE="1",
               PKMN_TEST_EXIT="1", PKMN_TEST_LOG=os.path.join(rundir, "test.log"))
    prefix = ["xvfb-run", "-a", "-s", "-screen 0 1280x720x24 -nolisten tcp"] if args.xvfb else []
    with open(os.path.join(rundir, "game.log"), "w") as output:
        subprocess.run(prefix + [os.path.abspath(args.exe), "--save", "test.sav"], cwd=rundir, env=env,
                       stdout=output, stderr=subprocess.STDOUT, timeout=900)
    steps = []
    for line in open(os.path.join(rundir, "test.log"), errors="replace"):
        match = BIKE.search(line)
        if match:
            steps.append(tuple(int(match.group(i), 16 if i == 4 else 10) for i in range(1, 6)))
    return steps


def place_at(steps, frame):
    """The player's place as of that field-key frame."""
    where = None
    for step in steps:
        if step[0] <= frame:
            where = (step[1], step[2])
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

    # 1. The crossing: SELECT gets on the bike; B held 100-330 (hopping from frame 140), RIGHT with it 200-300 along
    #    the lowest rail to its east end (20,7); B let go; UP and B in one frame twice (the sideways jumps onto the
    #    lone rail (20,6), then the middle rail (20,5)); hop along it to (22,5); the same up to the top rail (22,3);
    #    hop along it onto the east ledge.
    keys = ["30:SELECT"] + held("B", 100, 330) + held("RIGHT", 200, 300)
    keys += ["400:UP", "400:B", "470:UP", "470:B"] + held("B", 560, 800) + held("RIGHT", 660, 740)
    keys += ["860:UP", "860:B", "930:UP", "930:B"] + held("B", 1020, 1300) + held("RIGHT", 1120, 1250)
    steps = run(args, "crossing", keys, 1400)
    check("on the bike", any(step[3] & 0x04 for step in steps), "Acro Bike flag seen")
    check("hop along the lowest rail", place_at(steps, 330) == (20, 7), f"at {place_at(steps, 330)} by frame 330")
    check("sideways jump onto the lone rail", place_at(steps, 450) == (20, 6), f"at {place_at(steps, 450)}")
    check("sideways jump onto the middle rail", place_at(steps, 520) == (20, 5), f"at {place_at(steps, 520)}")
    check("hop along the middle rail", place_at(steps, 800) == (22, 5), f"at {place_at(steps, 800)}")
    check("two jumps onto the top rail", place_at(steps, 1000) == (22, 3), f"at {place_at(steps, 1000)}")
    end = place_at(steps, 1400)
    check("along the top rail onto the east ledge", end is not None and end[0] >= 26 and end[1] == 3, f"at {end}")

    # 2. What the original refuses, on the lowest rail's end: B two frames before UP (a wheelie, no jump), UP while
    #    hopping (nothing); then UP a frame before B (the jump).
    keys = ["30:SELECT"] + held("B", 100, 330) + held("RIGHT", 200, 300)
    keys += ["400:B", "402:UP"] + held("B", 500, 700) + held("UP", 600, 640) + ["800:UP", "801:B"]
    steps = run(args, "refused", keys, 900)
    check("B before UP: no jump (a wheelie, as the original)", place_at(steps, 480) == (20, 7),
          f"at {place_at(steps, 480)}")
    check("UP while hopping: no jump (as the original)", place_at(steps, 760) == (20, 7), f"at {place_at(steps, 760)}")
    check("UP a frame before B: the jump", place_at(steps, 880) == (20, 6), f"at {place_at(steps, 880)}")
    print(f"{sum(results)} of {len(results)} checks passed")
    return 0 if all(results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
