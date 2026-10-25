#!/usr/bin/env python3
"""End-to-end check of the Game Corner economy through the game's own scripts (Update 29).

Runs the real prize counter, the Mega Stone dealer and the coin counter scripts headless under xvfb
(PKMN_DEBUG_DIALOG starts the script, PKMN_DEBUG_SETUP sets the wallet and bag, PKMN_DEBUG_TAPS presses A as real SDL key events,
one every 300 ms: YES to every offer, the first row of the dealer's list every time) and reads the "econ" lines the game logs:
coins, money, Mega Ring, Shiny Charm, Mega Stones in the bag. Each scenario states what must be true at the end:

  - the prize counter sells the Mega Ring for 3,750 coins and the Shiny Charm for 15,000, only what the wallet covers, never
    twice, and takes exactly the price (money untouched);
  - an owned ring or charm is not sold again and costs nothing;
  - without a Coin Case nothing is sold;
  - the dealer sells nothing to a player without the ring, and with it takes the coin price of each stone (and no yen);
  - the coin counter's 5,000-coin pack costs 100,000 yen (and the wallet can pass 65,535 coins).

    tools/pc_port/economy_check.py [--diorama ./pokeemerald-mp-diorama] [--classic ./pokeemerald-mp]

Needs xvfb-run. A simulation on a software display, not a real keyboard on a real PC.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import threading

ECON = re.compile(r"econ (\d+): coins (\d+) money (\d+) ring (\d+) charm (\d+) stones (\d+)")
RING, CHARM = 3750, 15000        # 15% of what they were (the owner, 2026-10-25)
VENUSAURITE = 9000    # first row of the dealer's list


def run(binary, script, setup, taps, timeout):
    xvfb = shutil.which("xvfb-run")
    env = dict(os.environ, PKMN_DEBUG_SCENE="field", PKMN_DEBUG_HERE="0,16,7,15", PKMN_DEBUG_DIALOG=script, PKMN_DEBUG_SETUP=setup,
               PKMN_DEBUG_TAPS=taps, PKMN_DEBUG_FRAMES="1", SDL_AUDIODRIVER="dummy")
    cmd = ([xvfb, "-a", "-s", "-screen 0 1280x800x24"] if xvfb and not os.environ.get("DISPLAY") else []) + [os.path.abspath(binary)]
    proc = subprocess.Popen(cmd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace",
                            start_new_session=True)
    lines, done = [], threading.Event()

    def reader():
        for line in proc.stdout:
            lines.append(line.rstrip("\n"))
            if "taps: pushed" in line:
                done.set()

    thread = threading.Thread(target=reader, daemon=True)
    thread.start()
    done.wait(timeout)
    try:
        os.killpg(proc.pid, 15)
    except ProcessLookupError:
        pass
    try:
        proc.wait(5)
    except subprocess.TimeoutExpired:
        os.killpg(proc.pid, 9)
    thread.join(5)
    rows = [tuple(int(v) for v in m.groups()) for m in map(ECON.search, lines) if m]
    return rows, lines


def scenarios():
    # name, script, setup, taps (start:period:duration:count), check(first, last) -> problem or None
    def bought(first, last, ring, charm, coins_spent, money_spent=0):
        out = []
        if last[1] != first[1] - coins_spent:
            out.append(f"coins {first[1]} -> {last[1]}, expected to spend {coins_spent}")
        if last[2] != first[2] - money_spent:
            out.append(f"money {first[2]} -> {last[2]}, expected to spend {money_spent}")
        if last[3] != ring:
            out.append(f"ring is {last[3]}, expected {ring}")
        if last[4] != charm:
            out.append(f"charm is {last[4]}, expected {charm}")
        return "; ".join(out) or None

    taps = "1500:300:100:110"
    return [
        ("prizes: 30,000 coins buy the ring and the charm", "prizes", "case=1,coins=30000,money=500000", taps,
         lambda a, b: bought(a, b, 1, 1, RING + CHARM)),
        ("prizes: 14,999 coins buy the ring only (the charm needs 15,000)", "prizes", "case=1,coins=14999,money=500000", taps,
         lambda a, b: bought(a, b, 1, 0, RING)),
        ("prizes: 3,749 coins buy nothing", "prizes", "case=1,coins=3749,money=500000", taps,
         lambda a, b: bought(a, b, 0, 0, 0)),
        ("prizes: 3,750 coins buy exactly the ring", "prizes", "case=1,coins=3750", taps,
         lambda a, b: bought(a, b, 1, 0, RING)),
        ("prizes: an owned ring is not sold again; 15,000 coins buy exactly the charm", "prizes", "case=1,ring=1,coins=15000", taps,
         lambda a, b: bought(a, b, 1, 1, CHARM)),
        ("prizes: both owned, nothing is sold or charged", "prizes", "case=1,ring=1,charm=1,coins=200000", taps,
         lambda a, b: bought(a, b, 1, 1, 0)),
        ("prizes: without a Coin Case nothing is sold", "prizes", "coins=200000", taps,
         lambda a, b: bought(a, b, 0, 0, 0)),
        ("dealer: without the Mega Ring nothing is sold", "dealer", "case=1,coins=500000", taps,
         lambda a, b: bought(a, b, 0, 0, 0) or (None if b[5] == 0 else f"{b[5]} stones in the bag")),
        ("dealer: with the ring, each stone costs its coin price (no yen)", "dealer", "case=1,ring=1,coins=130000,money=700000", taps,
         lambda a, b: (None if b[5] >= 1 and a[1] - b[1] == VENUSAURITE * b[5] and b[2] == a[2] and b[0] >= 0
                       else f"stones {b[5]}, coins {a[1]} -> {b[1]}, money {a[2]} -> {b[2]}")),
        ("dealer: 8,999 coins cannot buy a 9,000-coin stone", "dealer", "case=1,ring=1,coins=8999,money=900000", taps,
         lambda a, b: (None if b[5] == 0 and b[1] == 8999 and b[2] == 900000 else f"stones {b[5]}, coins {b[1]}, money {b[2]}")),
        ("coin counter: the 5,000-coin pack costs 100,000 yen (the wallet passes 65,535)", "pack5000", "case=1,coins=62000,money=300000", "1500:300:100:30",
         lambda a, b: (None if b[1] == 67000 and b[2] == 200000 else f"coins {a[1]} -> {b[1]}, money {a[2]} -> {b[2]}")),
        ("coin counter: the 5,000-coin pack is refused without the money", "pack5000", "case=1,coins=1000,money=99999", "1500:300:100:30",
         lambda a, b: (None if b[1] == 1000 and b[2] == 99999 else f"coins {a[1]} -> {b[1]}, money {a[2]} -> {b[2]}")),
        ("coin counter: no room above 999,999 coins (a pack that does not fit is refused whole)", "pack5000", "case=1,coins=996000,money=300000", "1500:300:100:30",
         lambda a, b: (None if b[1] == 996000 and b[2] == 300000 else f"coins {a[1]} -> {b[1]}, money {a[2]} -> {b[2]}")),
        ("coin counter: the 500-coin pack still costs 10,000 yen", "pack500", "case=1,coins=62000,money=300000", "1500:300:100:30",
         lambda a, b: (None if b[1] == 62500 and b[2] == 290000 else f"coins {a[1]} -> {b[1]}, money {a[2]} -> {b[2]}")),
    ]


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--diorama", default="./pokeemerald-mp-diorama")
    parser.add_argument("--classic", default="./pokeemerald-mp")
    parser.add_argument("--only", default="", help="a part of a scenario name")
    parser.add_argument("--timeout", type=int, default=120)
    args = parser.parse_args()
    failures = 0
    for label, binary in (("3D", args.diorama), ("Classic 2D", args.classic)):
        if not os.path.exists(binary):
            print(f"{label}: {binary} not found; not run")
            failures += 1
            continue
        for name, script, setup, taps, check in scenarios():
            if args.only and args.only not in name:
                continue
            rows, lines = run(binary, script, setup, taps, args.timeout)
            if len(rows) < 1 or not any("START script" in l for l in lines):
                print(f"FAIL [{label}] {name}: the script did not start or no state was logged")
                failures += 1
                continue
            first, last = rows[0], rows[-1]
            problem = check(first, last)
            if problem:
                print(f"FAIL [{label}] {name}: {problem}")
                failures += 1
            else:
                print(f"ok   [{label}] {name}: coins {first[1]} -> {last[1]}, money {first[2]} -> {last[2]}, ring {last[3]}, charm {last[4]}, stones {last[5]}")
    print(f"economy_check: {failures} failed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
