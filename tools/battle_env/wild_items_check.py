#!/usr/bin/env python3
"""The 2026-10-27 wild item audit in real game processes (test builds), on Route 101's grass, where the game's own visible
wild Pokemon appear (offline: no world, so every Pokemon there is the player's own spawner's). Everything is read from the
game's own log.

  sync       a Synchronize lead (Ralts, Timid, ability slot 0) bumps into the first visible wild Pokemon
             (PKMN_TEST_OWE_BATTLE=wild): "owe battle: ... nature N" - the lead's nature about half the time (52 %)
  sync-none  the same lead with Trace (slot 1): the lead's nature 1 time in 25
  rate       the visible spawner over 70 s on the field, standing still (PKMN_TEST_OWE_COUNT; PKMN_TEST_OWE_NO_BATTLE: a
             Pokemon that walks into the player starts no battle, so the minute is not cut short): how many Pokemon it made
             with no item, with the White Flute's flag (x1.5), the Black Flute's (x0.5), a Cleanse Tag held (x2/3) and an
             Illuminate lead (x2): the waits between spawns run at that rate, so the counts are ordered with them

    tools/battle_env/wild_items_check.py --exe ./pokeemerald-mp-bfxtest --save route101.sav --out build/wild-items \\
        --displays :94,:95,:96,:97 --runs 20

Exit 0 when every case gave what it expects; the report is OUT/wild_items_check.json.
"""
import argparse
import json
import math
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mega_check import driver_lines, finish, numbers, start  # noqa: E402

ROUTE_101 = "0:16:0:10:11"          # PKMN_TEST_WARP: Route 101 (its grass right of the player)
SPECIES_RALTS, SPECIES_ZIGZAGOON, SPECIES_STARYU = 280, 263, 120
NATURE_TIMID = 10
ITEM_CLEANSE_TAG = 467               # include/constants/items.h (compiled and printed, 2026-10-27)
FLAG_SYS_ENC_UP_ITEM, FLAG_SYS_ENC_DOWN_ITEM = 0x8AD, 0x8AE   # include/constants/flags.h (compiled and printed)
SYNC_FRAMES = 1500
RATE_FRAMES = 4200                  # 60 s of field after the start


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", required=True)
    parser.add_argument("--save", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--displays", default=os.environ.get("DISPLAY", ":0"), help="comma separated, one game on each")
    parser.add_argument("--runs", type=int, default=20, help="battles for each Synchronize case")
    parser.add_argument("--rate-runs", type=int, default=2, help="60-second runs for each encounter-rate case")
    parser.add_argument("--flags", default="", help="FLAG_SYS_ENC_UP_ITEM,FLAG_SYS_ENC_DOWN_ITEM as the game numbers them")
    parser.add_argument("--cases", default="sync,sync-none,rate")
    args = parser.parse_args()
    args.exe, args.save = os.path.abspath(args.exe), os.path.abspath(args.save)
    os.makedirs(args.out, exist_ok=True)
    up, down = (FLAG_SYS_ENC_UP_ITEM, FLAG_SYS_ENC_DOWN_ITEM)
    if args.flags:
        up, down = (int(v, 0) for v in args.flags.split(","))
    displays = [d for d in args.displays.split(",") if d]
    wanted = set(args.cases.split(","))

    base = dict(PKMN_TEST_FREEZE_FIELD="1", PKMN_TEST_AUTOPILOT="1", PKMN_TEST_BATTLE_DELAY="100",
                PKMN_TEST_WARP=ROUTE_101, PKMN_TEST_FIELD_KEYS="")
    jobs = []   # (case, run, frames, env)
    if "sync" in wanted:
        for run in range(args.runs):
            jobs.append(("sync", run, SYNC_FRAMES, dict(base, PKMN_TEST_BATTLE=f"field:{SYNC_FRAMES}", PKMN_TEST_OWE_BATTLE="wild",
                                                        PKMN_TEST_PARTY=f"{SPECIES_RALTS}:20:n{NATURE_TIMID}a0")))
    if "sync-none" in wanted:
        for run in range(args.runs):
            jobs.append(("sync-none", run, SYNC_FRAMES, dict(base, PKMN_TEST_BATTLE=f"field:{SYNC_FRAMES}", PKMN_TEST_OWE_BATTLE="wild",
                                                             PKMN_TEST_PARTY=f"{SPECIES_RALTS}:20:n{NATURE_TIMID}a1")))
    rate_cases = [
        ("rate-none", 1.0, dict(PKMN_TEST_PARTY=f"{SPECIES_ZIGZAGOON}:20")),
        ("rate-white-flute", 1.5, dict(PKMN_TEST_PARTY=f"{SPECIES_ZIGZAGOON}:20", PKMN_TEST_SET_FLAGS=f"{up}")),
        ("rate-black-flute", 0.5, dict(PKMN_TEST_PARTY=f"{SPECIES_ZIGZAGOON}:20", PKMN_TEST_SET_FLAGS=f"{down}")),
        ("rate-cleanse-tag", 2 / 3, dict(PKMN_TEST_PARTY=f"{SPECIES_ZIGZAGOON}:20:i{ITEM_CLEANSE_TAG}")),
        ("rate-illuminate", 2.0, dict(PKMN_TEST_PARTY=f"{SPECIES_STARYU}:20:a0")),
    ]
    if "rate" in wanted:
        for name, _, extra in rate_cases:
            for run in range(args.rate_runs):
                jobs.append((name, run, RATE_FRAMES, dict(base, PKMN_TEST_BATTLE=f"field:{RATE_FRAMES}", PKMN_TEST_OWE_COUNT="1",
                                                          PKMN_TEST_OWE_NO_BATTLE="1", **extra)))

    results = {}
    pending = list(jobs)
    running = {}   # display -> (job, process, output, started)
    last_start = 0.0
    while pending or running:
        for display in displays:
            # The game seeds its random numbers from the clock's seconds at boot: games started in the same second
            # play the same, so each one starts at least 1.1 s after the one before (independent runs)
            if display not in running and pending and time.time() - last_start >= 1.1:
                last_start = time.time()
                job = pending.pop(0)
                case, run, frames, env = job
                rundir = os.path.join(args.out, f"{case}-{run}")
                sub = argparse.Namespace(save=args.save, display=display, exe=args.exe)
                process, output = start(sub, rundir, env)
                running[display] = (job, process, output, time.time())
        time.sleep(0.2)
        for display in list(running):
            job, process, output, started = running[display]
            if process.poll() is None and time.time() - started < job[2] / 60 + 300:
                continue
            code = finish(process, output, 5)
            case, run, frames, env = job
            lines = driver_lines(os.path.join(args.out, f"{case}-{run}", "test.log"))
            results.setdefault(case, []).append(dict(run=run, exit=code, lines=lines))
            del running[display]

    report, problems = {}, []
    for case in ("sync", "sync-none"):
        if case not in results:
            continue
        natures, leads = [], []
        for r in results[case]:
            for line in r["lines"]:
                if line.startswith("owe battle: "):
                    species, shiny, nature, ability, lead_nature = numbers(line)[:5]
                    natures.append(nature)
                    leads.append((ability, lead_nature))
        same = sum(1 for n, (a, ln) in zip(natures, leads) if n == ln)
        p = 0.52 if case == "sync" else 0.04
        n = len(natures)
        report[case] = dict(battles=n, lead_nature=same, expected=round(n * p, 1), leads=sorted(set(leads)), natures=natures)
        if n < max(5, args.runs // 2):
            problems.append(f"{case}: only {n} battles of {args.runs} runs")
        elif case == "sync":
            # at least a quarter of the battles (52 % expected; 1 in 25 without Synchronize)
            if same < max(2, math.floor(n * 0.25)):
                problems.append(f"sync: the lead's nature in {same} of {n} battles (52 % expected)")
        elif same > max(2, math.ceil(n * 0.2)):
            problems.append(f"sync-none: the lead's nature in {same} of {n} battles (4 % expected)")
    rates = {}
    for name, factor, _ in rate_cases:
        if name not in results:
            continue
        made = []
        for r in results[name]:
            counts = [numbers(line) for line in r["lines"] if line.startswith("owe count: ")]
            if len(counts) >= 2:
                made.append(counts[-1][2] - counts[0][2])
        rates[name] = dict(factor=factor, made_per_run=made, mean=(sum(made) / len(made)) if made else None)
        if not made:
            problems.append(f"{name}: no spawn counts")
    report["rate"] = rates
    order = [name for name, factor, _ in sorted(rate_cases, key=lambda c: c[1]) if rates.get(name, {}).get("mean") is not None]
    means = [rates[name]["mean"] for name in order]
    if len(means) >= 2 and any(b <= a for a, b in zip(means, means[1:])):
        # equal factors would tie; these five differ, so their means should rise with the factor
        problems.append(f"rate: the spawn counts do not rise with the rate: {[(n, rates[n]['mean']) for n in order]}")
    report["problems"] = problems
    report["passed"] = not problems
    with open(os.path.join(args.out, "wild_items_check.json"), "w") as out:
        json.dump(report, out, indent=1)
    print(json.dumps({k: v for k, v in report.items() if k != "sync" and k != "sync-none"}, indent=1))
    for case in ("sync", "sync-none"):
        if case in report:
            print(case, {k: v for k, v in report[case].items() if k != "natures"})
    return 0 if not problems else 1


if __name__ == "__main__":
    sys.exit(main())
