#!/usr/bin/env python3
"""The Acro Bike on rails, in the 3D test build (DIORAMA=1 BFX_TEST=1): the Safari Zone South's rails crossed as the
original game asks - hold B until the bike hops, hold B and the way along the rail; on a rail's end, B let go, the way
across and B in one frame for the sideways jump onto the next rail - and the PC's easier rails (KNOWN_ISSUES MS-171,
the owner's choice of 2026-10-31): hopping on a rail, the way across it toward a rail beside it jumps there and the
hop goes on (B never let go); an arrow tapped during a hop counts at the hop's end; the standing jump takes the way and
B up to 6 frames apart in either order (the original: the way, then B within 3 frames; B first made a wheelie). The
owner's reports of 2026-10-30 and 31 ("not able to hop rails with the Acro Bike"; "held the hop button and clicked the
arrows, nothing works"; "it doesn't move rails, it just keeps jumping"). The game's own log of each change of the
player's place, bike state and keys (PKMN_TEST_BIKE_LOG) is what is checked.

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

    # 1. The original's crossing (still the same on PC): SELECT gets on the bike; B held 100-330 (hopping from frame
    #    140), RIGHT with it 200-300 along the lowest rail to its east end (20,7); B let go; UP and B in one frame twice
    #    (the sideways jumps onto the lone rail (20,6), then the middle rail (20,5)); hop along it to (22,5); the same
    #    up to the top rail (22,3); hop along it onto the east ledge.
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

    # 2. PC: the whole crossing with B never let go - hop along the lowest rail; UP held at its end: the jump onto the
    #    lone rail, the hop going on, the jump onto the middle rail; UP still held there, no rail beyond: hopping in
    #    place (as the original); RIGHT along it; UP: the two jumps onto the top rail; RIGHT onto the east ledge.
    keys = ["30:SELECT"] + held("B", 100, 1000) + held("RIGHT", 200, 300) + held("UP", 380, 470)
    keys += held("RIGHT", 520, 600) + held("UP", 640, 730) + held("RIGHT", 780, 900)
    steps = run(args, "hopping", keys, 1000)
    check("hopping: along the lowest rail", place_at(steps, 370) == (20, 7), f"at {place_at(steps, 370)}")
    places = [(step[1], step[2]) for step in steps if 370 <= step[0] <= 470]
    check("hopping, UP held: jumps onto the lone rail, then the middle rail", places.count((20, 6)) > 0
          and place_at(steps, 470) == (20, 5), f"places {sorted(set(places))}, at {place_at(steps, 470)} by 470")
    check("hopping, UP held, no rail beyond: hops in place (as the original)", place_at(steps, 515) == (20, 5),
          f"at {place_at(steps, 515)}")
    check("hopping: along the middle rail", place_at(steps, 635) == (22, 5), f"at {place_at(steps, 635)}")
    check("hopping, UP held: two jumps onto the top rail", place_at(steps, 775) == (22, 3),
          f"at {place_at(steps, 775)}")
    end = place_at(steps, 1000)
    check("hopping: along the top rail onto the east ledge", end is not None and end[0] >= 26 and end[1] == 3,
          f"at {end}")
    normal = [step[0] for step in steps if 160 <= step[0] <= 1000 and step[4] == 0]
    check("hopping: B never let go - never back to riding normally", not normal, f"normal at frames {normal[:5]}")

    # 3. PC: arrows tapped (3 frames each) while hopping, B held - each counts at the hop's end: the first RIGHT turns
    #    the bike, each next one hops a step along the lowest rail; a tapped UP at its end: the jump onto the lone
    #    rail, another the jump onto the middle rail; two tapped RIGHTs along it.
    keys = ["30:SELECT"] + held("B", 100, 700) + ["200:RIGHT", "240:RIGHT", "280:RIGHT", "320:RIGHT", "360:RIGHT"]
    keys += ["420:UP", "480:UP", "560:RIGHT", "600:RIGHT"]
    steps = run(args, "taps", keys, 700)
    check("taps: four tapped RIGHTs hop four steps", place_at(steps, 415) == (20, 7), f"at {place_at(steps, 415)}")
    check("taps: a tapped UP jumps onto the lone rail", place_at(steps, 475) == (20, 6), f"at {place_at(steps, 475)}")
    check("taps: another onto the middle rail", place_at(steps, 555) == (20, 5), f"at {place_at(steps, 555)}")
    check("taps: two tapped RIGHTs along it", place_at(steps, 690) == (22, 5), f"at {place_at(steps, 690)}")

    # 4. PC: the standing jump's window, on the lowest rail's end (B let go): UP 8 frames before B and B 9 frames
    #    before UP - no jump; UP 6 frames before B - the jump onto the lone rail; B 6 frames before UP - onto the
    #    middle rail.
    keys = ["30:SELECT"] + held("B", 100, 330) + held("RIGHT", 200, 300)
    keys += held("UP", 400, 410) + ["408:B"] + held("B", 500, 528) + ["509:UP"]
    keys += held("UP", 600, 608) + ["606:B"] + held("B", 700, 718) + ["706:UP"]
    steps = run(args, "window", keys, 800)
    check("window: UP 8 frames before B - no jump", place_at(steps, 470) == (20, 7), f"at {place_at(steps, 470)}")
    check("window: B 9 frames before UP - no jump (a wheelie)", place_at(steps, 580) == (20, 7),
          f"at {place_at(steps, 580)}")
    check("window: UP 6 frames before B - the jump", place_at(steps, 650) == (20, 6), f"at {place_at(steps, 650)}")
    check("window: B 6 frames before UP - the jump", place_at(steps, 760) == (20, 5), f"at {place_at(steps, 760)}")

    # 5. PC: B tapped two frames before a tapped UP (the original: a wheelie, no jump) - the jump, though both are let
    #    go before the wheelie is up; then UP a frame before B - the jump.
    keys = ["30:SELECT"] + held("B", 100, 330) + held("RIGHT", 200, 300) + ["400:B", "402:UP", "500:UP", "501:B"]
    steps = run(args, "quick", keys, 600)
    check("quick: B two frames before UP - the jump", place_at(steps, 480) == (20, 6), f"at {place_at(steps, 480)}")
    check("quick: UP a frame before B - the jump", place_at(steps, 580) == (20, 5), f"at {place_at(steps, 580)}")
    print(f"{sum(results)} of {len(results)} checks passed")
    return 0 if all(results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
