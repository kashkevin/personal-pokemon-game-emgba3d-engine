#!/usr/bin/env python3
"""The PC game's Game Corner casino economy (the owner's request of 2026-10-25): what a bet costs, what a win pays and what
a player earns, for the Slot Machines and the Roulette. Everything is computed from the numbers in
include/platform/casino_rates.h and from the games' own rules:

  Slot Machines  src/slot_machine.c itself is run (tools/pc_port/casino_sim/slot_sim.c includes it): the machine's bias
                 draws, where each reel stops once A is pressed, the lines a bet plays, Replay and ReelTime. Millions of
                 spins give how often each result comes; the payouts are applied afterwards (they do not change the odds).
  Roulette       the board of 12 slots, 6 balls a round, a ball never landing on a slot already hit (it falls to a
                 neighbour, or is set down on an open slot), the multipliers of GetMultiplier and RecordHit. This is
                 a model of the game's rules (the odds of each ball are computed exactly from them); the multiplier,
                 hit and win checks were compared with a real game's log of 170 balls (no difference).

Time per spin is a model too: the machine's own frame counts were measured in the Linux test build (slots: 79 frames from
bet to result, 67 after a loss, 100 before a win's payout starts, 9 frames a coin counted - 5 with A held - or START once the
win fanfare is over; roulette: 417 from bet to the ball's hit, 170 after a loss, 40-60 to the next bet, 190 more and 4 frames
a coin after a win), and a player's own reaction times are added (below). The earnings are what that player gets, not what
every player gets.

    tools/pc_port/casino_econ.py                 # the report (markdown) on stdout
    tools/pc_port/casino_econ.py --check         # exit 1 when a game's reference earnings are not about 15,000 an hour-half
    tools/pc_port/casino_econ.py --json out.json
    options: --spins N (slot spins a bet and machine, default 2,000,000) --rounds N (roulette rounds, default 60,000)

Needs the GBA build's generated assets (build/assets), gcc -m32 and tools/preproc/preproc, as the PC build does.
"""
from __future__ import annotations

import argparse
import json
import os
import random
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
RATES = os.path.join(ROOT, "include/platform/casino_rates.h")
BUILD = os.path.join(ROOT, "build/casino-sim")
TARGET_30 = 15000
TOLERANCE = 0.10

# ---- the numbers the GBA game has (what the PC game had before this change) -------------------------------------------------
SLOT_NAMES = ["Cherry (middle row)", "Cherry (top/bottom row)", "Replay", "Lotad", "Azurill", "Pikachu Power", "Mixed 7s",
              "Red 7s", "Blue 7s"]
SLOT_KEYS = ["CHERRY", "CHERRY_EDGE", None, "LOTAD", "AZURILL", "POWER", "MIXED_7", "RED_7", "BLUE_7"]
OLD_SLOT_PAY = [2, 4, 0, 6, 12, 3, 90, 300, 300]
OLD_SLOT_COINS_PER_BET = 1
OLD_ROULETTE_BETS = [1, 3, 1, 6]
OLD_ROULETTE_MULT = [3, 4, 6, 12]
TABLES = ["Left table", "Right table", "Left table, special rate", "Right table, special rate"]

# ---- the player ------------------------------------------------------------------------------------------------------------
HUMAN_BET_FRAMES = 30            # the press that bets (R: all three), half a second
HUMAN_STOP_FRAMES = 27           # each A that stops a reel, after the reel before it has stopped
HUMAN_ROULETTE_FRAMES = 90       # choosing what to bet on and pressing A, once a ball
SLOT_MACHINE_FRAMES = 93         # bet to result 79, and the 14 between a spin's end and the next bet
SLOT_LOSS_TAIL = 67
SLOT_WIN_TAIL = 100              # before the payout starts counting (lines flashing)
SLOT_SKIP_TAIL = 160             # START works once the win fanfare (150 frames) is over
SLOT_JACKPOT_EXTRA = 100         # the Big Bonus fanfare lasts 250
SLOT_REELTIME_FRAMES = 800       # Pikachu's draw
ROULETTE_LOSS_FRAMES = 640 + 50  # bet to next bet after a loss (+ the prompt after the sixth ball)
ROULETTE_WIN_EXTRA = 190
ROULETTE_JACKPOT_EXTRA = 100
ROULETTE_FRAMES_PER_COIN = 4


def read_rates():
    text = open(RATES, encoding="utf-8").read()
    rates = {m.group(1): int(m.group(2)) for m in re.finditer(r"#define CASINO_(\w+)\s+(\d+)", text)}
    pay = [rates.get(f"SLOT_PAY_{k}", 0) if k else 0 for k in SLOT_KEYS]
    return dict(
        slot_coins=rates["SLOT_COINS_PER_BET"], slot_pay=pay,
        roulette_bets=[rates["ROULETTE_BET_LEFT"], rates["ROULETTE_BET_RIGHT"], rates["ROULETTE_BET_LEFT_SPECIAL"],
                       rates["ROULETTE_BET_RIGHT_SPECIAL"]],
        roulette_mult=[rates[f"ROULETTE_MULT_{i}"] for i in (1, 2, 3, 4)])


# ---- Slot Machines: the game's code ---------------------------------------------------------------------------------------
def build_slot_sim():
    os.makedirs(BUILD, exist_ok=True)
    src = open(os.path.join(ROOT, "src/slot_machine.c"), encoding="utf-8").read()
    assert src.count("static const u16 sSlotPayouts[];") == 1
    src = src.replace("static const u16 sSlotPayouts[];", "u16 sSlotPayouts[];")
    src = src.replace("static const u16 sSlotPayouts[] = {", "u16 sSlotPayouts[] = {")
    src = src.replace("sSlotMachine->payout += sSlotPayouts[match];", "{ sSlotMachine->payout += sSlotPayouts[match]; gLines[match]++; }")
    src = src.replace("static EWRAM_DATA struct SlotMachine *sSlotMachine = NULL;",
                      "static EWRAM_DATA struct SlotMachine *sSlotMachine = NULL;\ndouble gLines[16];")
    assert src.count("gLines[match]++") == 5
    with open(os.path.join(BUILD, "slot_machine_sim_src.c"), "w", encoding="utf-8") as out:
        out.write(src)
    flags = ["-iquote", "include", "-iquote", BUILD, "-Wno-trigraphs", "-DMODERN=1", "-DTESTING=0", "-DEMERALD", "-std=gnu17",
             "-DMP_LOCAL_RESET_DEVELOPMENT=0", "-DMP_LOCAL_RESET_STATIC=1", "-DMP_LOCAL_RESET_NEW_MAUVILLE_PILOT=0",
             "-DPORTABLE", "-DNONMATCHING", "-DUBFIX", "-DPLATFORM_SDL2", "-DNATIVE_LINUX"]
    harness = os.path.join(ROOT, "tools/pc_port/casino_sim/slot_sim.c")
    objects = []
    for name, source in (("slot_sim", harness), ("random", os.path.join(ROOT, "src/random.c"))):
        obj = os.path.join(BUILD, name + ".o")
        pre = subprocess.run(["gcc", "-m32", "-E", *flags, source], cwd=ROOT, capture_output=True, check=True)
        conv = subprocess.run([os.path.join(ROOT, "tools/preproc/preproc"), "-i", "-p", "-g", os.path.join(ROOT, "build/assets"),
                               source, "charmap.txt"], cwd=ROOT, input=pre.stdout, capture_output=True, check=True)
        subprocess.run(["gcc", "-m32", "-O2", "-fno-pie", "-fwrapv", "-fno-strict-aliasing", "-w", "-x", "cpp-output", "-c", "-o", obj, "-"],
                       cwd=ROOT, input=conv.stdout, check=True)
        objects.append(obj)
    exe = os.path.join(BUILD, "slot_sim")
    subprocess.run(["gcc", "-m32", "-no-pie", *objects, "-lm", "-Wl,--unresolved-symbols=ignore-all", "-o", exe], check=True)
    return exe


def slot_statistics(exe, spins, seed=2026, lucky=2):
    """{bet: [machine dicts]}: per machine, the lines won per match and the segments of each kind, per paid spin."""
    stats = {}
    for bet in (1, 2, 3):
        out = subprocess.run([exe, str(spins), str(bet), str(seed), str(lucky)], capture_output=True, text=True, check=True).stdout
        machines = []
        for line in out.splitlines():
            m = re.match(r"STAT bet=\d+ machine=(\d+) paid_spins=(\d+) segs_loss=(\d+) segs_win=(\d+) segs_replayonly=(\d+) "
                         r"rt_events=(\d+) rt_explode=(\d+) lines=(.*)", line)
            if m:
                n = int(m.group(2))
                lines = [int(x) / n for x in m.group(8).split(",")]
                machines.append(dict(machine=int(m.group(1)), n=n, loss=int(m.group(3)) / n, win=int(m.group(4)) / n,
                                     replay=int(m.group(5)) / n, rt=int(m.group(6)) / n, lines=lines))
        assert len(machines) == 6, out
        stats[bet] = machines
    return stats


def average(machines):
    keys = ("loss", "win", "replay", "rt")
    out = {k: sum(m[k] for m in machines) / len(machines) for k in keys}
    out["lines"] = [sum(m["lines"][i] for m in machines) / len(machines) for i in range(9)]
    return out


def slot_frames(s, pay, style, human=True):
    """Frames of one paid spin (its free Replay spins and ReelTime draw included), averaged."""
    gross = sum(s["lines"][i] * pay[i] for i in range(9))
    reels = 3 * HUMAN_STOP_FRAMES if human else 9
    bet_press = HUMAN_BET_FRAMES if human else 21
    skip = style == "skip"
    tail = SLOT_SKIP_TAIL if skip else SLOT_WIN_TAIL
    frames = s["loss"] * (SLOT_MACHINE_FRAMES + SLOT_LOSS_TAIL + reels)
    frames += s["win"] * (SLOT_MACHINE_FRAMES + reels + tail)
    frames += (s["lines"][6] + s["lines"][7] + s["lines"][8]) * (SLOT_JACKPOT_EXTRA if skip else 0)
    frames += s["replay"] * (SLOT_MACHINE_FRAMES + reels + SLOT_LOSS_TAIL)
    frames += bet_press + s["rt"] * SLOT_REELTIME_FRAMES
    if not skip:
        frames += gross * (5 if style == "hold" else 9)
    return frames, gross


def slot_economy(stats, rates, bet, style, pay=None, coins=None, machines=None):
    pay = rates["slot_pay"] if pay is None else pay
    coins = rates["slot_coins"] if coins is None else coins
    s = average(stats[bet] if machines is None else [stats[bet][m] for m in machines])
    frames, gross = slot_frames(s, pay, style)
    cost = coins * bet
    seconds = frames / 60
    return dict(bet=bet, cost=cost, gross=gross, net=gross - cost, seconds=seconds, spins=1800 / seconds,
                net30=(gross - cost) / seconds * 1800, rtp=gross / cost)


# ---- Roulette: the game's rules ---------------------------------------------------------------------------------------------
WHEEL = [("O", "W"), ("G", "A"), ("P", "S"), ("O", "M"), ("G", "W"), ("P", "A"), ("O", "S"), ("G", "M"), ("P", "W"), ("O", "A"),
         ("G", "S"), ("P", "M")]
ROWS, COLS = "OGP", "WASM"


class Board:
    def __init__(self):
        self.hit = [False] * 12
        self.rowhits = [0] * 3
        self.colhits = [0] * 4

    def record(self, slot):
        self.hit[slot] = True
        self.rowhits[ROWS.index(WHEEL[slot][0])] += 1
        self.colhits[COLS.index(WHEEL[slot][1])] += 1


def selections(board, ladder):
    out = []
    for r in range(3):
        if board.rowhits[r] < 4:
            out.append((f"row {ROWS[r]}", {i for i, s in enumerate(WHEEL) if s[0] == ROWS[r]}, ladder[board.rowhits[r]]))
    for c in range(4):
        if board.colhits[c] < 3:
            out.append((f"col {COLS[c]}", {i for i, s in enumerate(WHEEL) if s[1] == COLS[c]}, ladder[1:][board.colhits[c]]))
    for i, s in enumerate(WHEEL):
        if not board.hit[i]:
            out.append((f"sq {s[0]}{s[1]}", {i}, ladder[3]))
    return out


def landing(board):
    """P(the ball ends in each slot): a ball over a slot already hit falls to its left or right neighbour; if that one is hit
    too it is stuck and set down on an open slot of the five from two past the stuck one (SpriteCB_RollBall_TryLand, SetBallStuck)."""
    p = [0.0] * 12
    for s in range(12):
        w = 1 / 12
        if not board.hit[s]:
            p[s] += w
            continue
        for right in (True, False):
            nb = (s + 1) % 12 if right else (s - 1) % 12
            stuck = nb if right else s
            if not board.hit[nb]:
                p[nb] += w / 2
            else:
                cands = [(stuck + k) % 12 for k in range(2, 7) if not board.hit[(stuck + k) % 12]] \
                        or [k for k in range(12) if not board.hit[k]]
                for k in cands:
                    p[k] += w / 2 / len(cands)
    return p


def play_round(rng, ladder, strategy):
    board, balls = Board(), []
    for _ in range(6):
        sels = selections(board, ladder)
        p = landing(board)
        if strategy == "best":
            name, slots, mult = max(sels, key=lambda x: sum(p[k] for k in x[1]) * x[2])
        else:
            name, slots, mult = rng.choice(sels)
        k = rng.choices(range(12), weights=p)[0]
        balls.append((mult, k in slots))
        board.record(k)
    return balls


def roulette_statistics(ladder, strategy, rounds, seed=7):
    rng = random.Random(seed)
    balls = gross = wins = jackpots = 0
    by_mult = {m: 0 for m in ladder}
    for _ in range(rounds):
        for mult, won in play_round(rng, ladder, strategy):
            balls += 1
            if won:
                gross += mult
                wins += 1
                by_mult[mult] += 1
                if mult == ladder[3]:
                    jackpots += 1
    return dict(rtp=gross / balls, win=wins / balls, jack=jackpots / balls, by_mult={m: c / balls for m, c in by_mult.items()})


def roulette_economy(stats, bet):
    frames = ROULETTE_LOSS_FRAMES + HUMAN_ROULETTE_FRAMES + stats["win"] * ROULETTE_WIN_EXTRA \
        + stats["jack"] * ROULETTE_JACKPOT_EXTRA + ROULETTE_FRAMES_PER_COIN * stats["rtp"] * bet
    net = (stats["rtp"] - 1) * bet
    seconds = frames / 60
    return dict(bet=bet, rtp=stats["rtp"], net=net, seconds=seconds, balls=1800 / seconds, net30=net / seconds * 1800)


# ---- the report -------------------------------------------------------------------------------------------------------------
def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--spins", type=int, default=2_000_000)
    parser.add_argument("--rounds", type=int, default=60_000)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--json")
    args = parser.parse_args()
    rates = read_rates()
    exe = build_slot_sim()
    stats = slot_statistics(exe, args.spins)
    old_rates = dict(slot_coins=OLD_SLOT_COINS_PER_BET, slot_pay=OLD_SLOT_PAY)
    result = dict(rates=rates, slots={}, roulette={})

    # slots
    for label, r in (("old", old_rates), ("new", rates)):
        result["slots"][label] = {
            style: {bet: slot_economy(stats, rates, bet, style, pay=r["slot_pay"], coins=r["slot_coins"]) for bet in (1, 2, 3)}
            for style in ("skip", "hold", "watch")}
    new3 = average(stats[3])
    result["slots"]["frequency_bet3"] = dict(zip(SLOT_NAMES, new3["lines"]))
    result["slots"]["per_machine_skip_bet3"] = [
        slot_economy(stats, rates, 3, "skip", machines=[m])["net30"] for m in range(6)]

    # roulette
    old_ladder, ladder = OLD_ROULETTE_MULT, rates["roulette_mult"]
    for label, lad, bets in (("old", old_ladder, OLD_ROULETTE_BETS), ("new", ladder, rates["roulette_bets"])):
        result["roulette"][label] = {}
        for strategy in ("random", "best"):
            st = roulette_statistics(lad, strategy, args.rounds)
            result["roulette"][label][strategy] = dict(stats=st, tables=[roulette_economy(st, b) for b in bets])
    if args.json:
        json.dump(result, open(args.json, "w"), indent=1)

    new_slot = result["slots"]["new"]["skip"][3]["net30"]
    new_roul = result["roulette"]["new"]["random"]["tables"][0]["net30"]
    ok_slot = abs(new_slot - TARGET_30) <= TARGET_30 * TOLERANCE
    ok_roul = abs(new_roul - TARGET_30) <= TARGET_30 * TOLERANCE
    if args.check:
        print(f"slot machines, three bets, START skipping the count: {new_slot:,.0f} net coins in 30 minutes "
              f"({'ok' if ok_slot else 'OUT OF RANGE'}); roulette, left table, any bet each ball: {new_roul:,.0f} "
              f"({'ok' if ok_roul else 'OUT OF RANGE'}); the target is {TARGET_30:,} within {int(TOLERANCE * 100)}%")
        mult_ok = subprocess.run([sys.executable, os.path.join(ROOT, "tools/pc_port/gen_roulette_multiplier.py"), "--check"]).returncode == 0
        return 0 if ok_slot and ok_roul and mult_ok else 1
    print_report(result, rates)
    return 0


def money(x):
    return f"{x:,.0f}"


def print_report(result, rates):
    s_old, s_new = result["slots"]["old"], result["slots"]["new"]
    print("## Slot Machines\n")
    print(f"Bet cost per bet tile: {OLD_SLOT_COINS_PER_BET} -> {rates['slot_coins']} coins "
          f"(bets of 1, 2, 3 tiles: {', '.join(str(OLD_SLOT_COINS_PER_BET * b) for b in (1, 2, 3))} -> "
          f"{', '.join(str(rates['slot_coins'] * b) for b in (1, 2, 3))}).\n")
    print("| Result | Old pay | New pay | Net profit with 1/2/3 tiles bet (old) | Net profit with 1/2/3 tiles bet (new) | Per spin at 3 tiles |")
    print("|---|---:|---:|---|---|---:|")
    for i, name in enumerate(SLOT_NAMES):
        old, new = OLD_SLOT_PAY[i], rates["slot_pay"][i]
        if name == "Replay":
            print(f"| {name} | free spin | free spin | the bet is kept for the next spin | the bet is kept for the next spin | "
                  f"{result['slots']['frequency_bet3'][name]:.4f} |")
            continue
        o = " / ".join(f"{old - OLD_SLOT_COINS_PER_BET * b:+d}" for b in (1, 2, 3))
        n = " / ".join(f"{new - rates['slot_coins'] * b:+,d}" for b in (1, 2, 3))
        print(f"| {name} | {old} | {new:,} | {o} | {n} | {result['slots']['frequency_bet3'][name]:.4f} lines |")
    print()
    print("Coins lost by a spin that wins nothing: " + ", ".join(
        f"{b} tile{'s' if b > 1 else ''}: {OLD_SLOT_COINS_PER_BET * b} -> {rates['slot_coins'] * b}" for b in (1, 2, 3)) + ".\n")
    print("| Player | Bet | Old: net / 30 min | New: net / 30 min | New: spins / 30 min | New: expected net a spin |")
    print("|---|---|---:|---:|---:|---:|")
    for style, label in (("skip", "START skips the count"), ("hold", "holds A while it counts"), ("watch", "watches it count")):
        for bet in (1, 2, 3):
            o, n = s_old[style][bet], s_new[style][bet]
            print(f"| {label} | {bet} | {money(o['net30'])} | {money(n['net30'])} | {n['spins']:.0f} | {n['net']:+.1f} |")
    print(f"\nThe six machines, three tiles, START: {', '.join(money(x) for x in result['slots']['per_machine_skip_bet3'])}.\n")

    print("## Roulette\n")
    r_old, r_new = result["roulette"]["old"], result["roulette"]["new"]
    print("| Table | Old minimum bet (cost of every ball) | New minimum bet |")
    print("|---|---:|---:|")
    for i, t in enumerate(TABLES):
        print(f"| {t} | {OLD_ROULETTE_BETS[i]} | {rates['roulette_bets'][i]} |")
    print()
    print("What a winning ball pays (the multiplier on the board when the bet is placed; a bet on a closed row or column pays 0):\n")
    print("| Bet on | Multiplier (old -> new) | " + " | ".join(f"{t}: pays (old -> new), net profit (new)" for t in TABLES) + " |")
    print("|---|---|" + "---|" * 4)
    lad_old, lad_new = OLD_ROULETTE_MULT, rates["roulette_mult"]
    states = [("Colour row (4 squares), none of its squares hit yet", 0), ("Colour row, 1 square hit", 1), ("Colour row, 2 squares hit", 2),
              ("Colour row, 3 squares hit", 3), ("Pokemon column (3 squares), none hit yet", 1), ("Pokemon column, 1 square hit", 2),
              ("Pokemon column, 2 squares hit", 3), ("A single square (not yet hit)", 3)]
    for label, k in states:
        cells = " | ".join(f"{OLD_ROULETTE_BETS[i] * lad_old[k]:,} -> {rates['roulette_bets'][i] * lad_new[k]:,}, "
                           f"{rates['roulette_bets'][i] * (lad_new[k] - 1):+,}" for i in range(4))
        print(f"| {label} | x{lad_old[k]} -> x{lad_new[k]} | {cells} |")
    print("\nThe chance of a hit on the first ball of a round: a colour row 4 in 12, a Pokemon column 3 in 12, a single square 1 in 12; "
          "a slot is never hit twice in a round, so every ball after the first is likelier to hit what is left.\n")
    print("A ball that misses loses its minimum bet: " + ", ".join(
        f"{TABLES[i]}: {OLD_ROULETTE_BETS[i]} -> {rates['roulette_bets'][i]}" for i in range(4)) + ".\n")
    print("| Player | Table | Old: net / 30 min | New: net / 30 min | New: balls / 30 min | New: expected net a ball | New: pay-out ratio |")
    print("|---|---|---:|---:|---:|---:|---:|")
    for strategy, label in (("random", "bets on anything open, each ball"), ("best", "bets on the best odds, each ball")):
        for i, t in enumerate(TABLES):
            o, n = r_old[strategy]["tables"][i], r_new[strategy]["tables"][i]
            print(f"| {label} | {t} | {money(o['net30'])} | {money(n['net30'])} | {n['balls']:.0f} | {n['net']:+,.0f} | {n['rtp']:.2f} |")


if __name__ == "__main__":
    sys.exit(main())
