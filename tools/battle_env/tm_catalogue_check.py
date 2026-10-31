#!/usr/bin/env python3
"""The TM catalogue (the owner's TM request of 2026-10-31, docs/owner-decisions.md 26-29) in real game processes (test
builds; Linux and Windows alike; the 3D one, whose party and shop screens are the PC's own, and the Classic 2D one): the
Game Corner's TM counter (TM PRIZES or the TM CATALOGUE), a TM past TM100 bought in its kind for coins, SOLD OUT once
owned, saved and loaded (TM51 and up live in SaveBlock3's TM record), then taught from the Bag to a Pokemon that can learn
it and still in the Bag (TMs are never used up on PC). Everything is read from the game's own log (PKMN_TEST_MEGA_LOG: the
coins and money whenever they change; PKMN_TEST_BAG_LOG: the TM in the Bag; PKMN_TEST_BREED_LOG: the party's species and
moves); the field keys are the presses a person makes. src/platform/native_selftest_tm.c (--selftest-tm-catalogue) checks
every TM, list, price, compatibility and the save record beside these.

  1. buy        below the TM counter's clerk (13,2) with a Coin Case and 20,000 coins: "We exchange your coins for
                prizes." TM CATALOGUE, DEFENSE & UTILITY, BUY, TM165 Detect (the list's last row, 7,500 coins), YES,
                "Here you go!"; A on it again: sold out (one each); out of the list, the kinds, the counter; SAVE.
                12,500 coins left, one TM165 in the Bag
  2. buy-load   that save loaded: the same coins and TM
  3. prizes     the same counter, TM PRIZES (the five as before, now behind the counter's question): TM32 Double Team
                (the first row, 225 coins), YES, "Here you go!"; TM32 again, twice: "You already have the TM32 Double
                Team." "A TM is never used up, so you won't need another one." and no coins taken. 19,775 coins left,
                one TM32 in the Bag
  4. teach      a Pikachu knowing Thunder Shock alone, TM165 in the Bag: BAG > TMs & HMs > TM165 > USE > Pikachu:
                "Pikachu learned Detect!"; TM165 still in the Bag; SAVE
  5. teach-load that save loaded: Pikachu knows Thunder Shock and Detect, TM165 in the Bag

    tools/battle_env/tm_catalogue_check.py --exe ./pokeemerald-mp-diorama-bfxtest --save town.sav --out build/tm-check \\
        [--display :99] [--steps buy,prizes,teach]

Exit 0 when every step gave what it expects; the report is OUT/tm_catalogue_check.json.
"""
import argparse
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mega_check import driver_lines, finish, numbers, save_keys, start  # noqa: E402

ENGINE = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SPECIES_PIKACHU = 25
MOVE_THUNDER_SHOCK = 84
TM_COUNTER = "10:3:0:13:4"     # PKMN_TEST_WARP: the Mauville Game Corner, below the TM prize clerk (13,2)
COINS = 20_000
TM = 165                       # TM165 Detect: DEFENSE & UTILITY's last row; Pikachu learnt it by TM in Gen 2
TM_ITEM = 773 + TM             # TM101-TM168 are items 874-941 (include/constants/items.h, PC)

# The start menu (POKeMON, BAG, ...), BAG; the Bag opens on ITEMS: two pockets right is TMs & HMs
OPEN_BAG = ["30:START", "80:DOWN", "120:A", "300:RIGHT", "340:RIGHT"]


def item_id(name):
    text = open(os.path.join(ENGINE, "include/constants/items.h"), encoding="utf-8").read()
    return int(re.search(r"^\s*" + name + r"\s*=\s*(\d+)\s*,", text, re.M).group(1))


def catalogue_row(label, tm):
    """The row of TM `tm` in the catalogue list `label` (data/scripts/mp_tm_catalogue_lists.inc)."""
    text = open(os.path.join(ENGINE, "data/scripts/mp_tm_catalogue_lists.inc"), encoding="utf-8").read()
    body = re.search(label + r"::(.*?)pokemartlistend", text, re.S).group(1)
    tms = [int(n) for n in re.findall(r"@ TM(\d+) ", body)]
    return tms.index(tm), len(tms)


def move_id(name):
    text = open(os.path.join(ENGINE, "include/constants/moves.h"), encoding="utf-8").read()
    return int(re.search(r"^\s*" + name + r"\s*=\s*(\d+)\s*,", text, re.M).group(1))


def downs(start, count, gap=10):
    return [f"{start + gap * i}:DOWN" for i in range(count)]


def bag_save_keys(t):
    """START > SAVE > YES with the start menu's cursor where the Bag left it (BAG, two rows above SAVE: rotom_check.py's
    close_and_save), YES again over the saved file; the closing message goes by itself; a B for safety."""
    return [f"{t + f}:{k}" for f, k in ((0, "START"), (50, "DOWN"), (80, "DOWN"), (160, "A"), (260, "A"), (380, "A"),
                                        (700, "B"))]


def party_states(lines):
    """{slot: [(species, moves), ...]} in the order the log tells them (PKMN_TEST_BREED_LOG's "breed party N" lines)."""
    states = {}
    for line in lines:
        m = re.match(r"breed party (\d): (.*)", line)
        if not m or m.group(2) == "none":
            continue
        v = numbers(m.group(2))
        state = (v[0], tuple(v[12:16]))
        slot = states.setdefault(int(m.group(1)), [])
        if not slot or slot[-1] != state:
            slot.append(state)
    return states


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", required=True)
    parser.add_argument("--save", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--display", default=os.environ.get("DISPLAY"))
    parser.add_argument("--steps", default="buy,prizes,teach", help="which steps (buy and teach are followed by their loads)")
    args = parser.parse_args()
    args.exe, args.save = os.path.abspath(args.exe), os.path.abspath(args.save)
    args.out = os.path.abspath(args.out)
    os.makedirs(args.out, exist_ok=True)
    wanted = set(args.steps.split(","))
    coin_case = item_id("ITEM_COIN_CASE")
    detect = move_id("MOVE_DETECT")
    steps, problems = [], []

    def step(name, save, frames, keys, extra=None, shots=""):
        rundir = os.path.join(args.out, name)
        saved, args.save = args.save, save
        env = dict(PKMN_TEST_BATTLE=f"field:{frames}", PKMN_TEST_BREED_LOG="1", PKMN_TEST_MEGA_LOG="1",
                   PKMN_TEST_BAG_LOG=str(TM_ITEM), PKMN_TEST_FREEZE_FIELD="1", PKMN_TEST_AUTOPILOT="1",
                   PKMN_TEST_BATTLE_DELAY="100", PKMN_TEST_FIELD_KEYS=",".join(keys),
                   PKMN_TEST_MEGA_SHOT_STEM=os.path.join(rundir, "step"))
        env.update(extra or {})
        if shots:
            env["PKMN_TEST_FIELD_SHOTS"] = shots
        try:
            process, output = start(args, rundir, env)
            code = finish(process, output, frames / 60 + 300)
        finally:
            args.save = saved
        lines = driver_lines(os.path.join(rundir, "test.log"))
        begun = next((i for i, line in enumerate(lines) if line.startswith("field keys for ")), 0) if keys else 0
        wallet = [tuple(numbers(line)) for line in lines[begun:] if line.startswith("mega wallet: ")]
        bag = [numbers(line)[2] for line in lines[begun:] if line.startswith("bag field: ")]
        party = party_states(lines[begun:] if keys else lines)
        record = dict(step=name, exit=code, wallet=wallet, tms=bag,
                      party={str(k): [[s, list(m)] for s, m in v] for k, v in sorted(party.items())},
                      notes=[line for line in lines if line.startswith(("cannot", "assert"))],
                      pictures=sorted(f for f in os.listdir(rundir) if f.endswith(".png")))
        steps.append(record)
        if code != 0:
            problems.append(f"{name}: the game ended with {code}")
        problems.extend(f"{name}: {line}" for line in record["notes"])
        return rundir, wallet, bag, party

    def expect(name, got, want):
        if got != want:
            problems.append(f"{name}: {got}, expected {want}")

    # 1-2. Bought in the TM CATALOGUE > DEFENSE & UTILITY (the fourth kind), sold out once owned; saved and loaded
    if "buy" in wanted:
        row, size = catalogue_row("MauvilleCity_GameCorner_Pokemart_TmUtility", TM)
        price = 7500
        base = dict(PKMN_TEST_PARTY=f"{SPECIES_PIKACHU}:20", PKMN_TEST_BAG=f"{coin_case}:1", PKMN_TEST_COINS=str(COINS),
                    PKMN_TEST_BAG_RESET="1", PKMN_TEST_WARP=TM_COUNTER)
        # talk: "Welcome." "You can exchange your COINS for prizes here."; the counter's question: TM CATALOGUE (the
        # second); "Which kind of TM?": DEFENSE & UTILITY (the fourth); the shop's menu: BUY; the list
        keys = ["30:UP", "80:A", "240:A", "400:A", "640:DOWN", "700:A"] + downs(900, 3, 40) + ["1060:A", "1240:A"]
        end_rows = 1400 + 10 * row
        keys += downs(1400, row)
        keys += [f"{end_rows + f}:A" for f in (100, 220, 340)]          # "You wanted TM165? ... 7500 COINS." YES, "Here you go!"
        keys += [f"{end_rows + f}:A" for f in (460, 580)]               # again: sold out
        keys += [f"{end_rows + f}:B" for f in (700, 820, 940)]          # out of the list, the shop's menu, the kinds
        keys += [f"{end_rows + 1060}:B"]                                # "Please come again!" (an A would talk again)
        save_at = end_rows + 1500
        rundir, wallet, bag, _ = step("buy", args.save, save_at + 900, keys + save_keys(save_at), base,
                                      shots=",".join(str(f) for f in range(200, end_rows + 1400, 60)))
        money = wallet[0][1] if wallet else None
        expect("buy (the wallet: coins, money)", [wallet[:1], wallet[-1:]], [[(COINS, money)], [(COINS - price, money)]])
        expect("buy (TM165 in the Bag)", bag[-1:], [1])
        _, wallet, bag, _ = step("buy-load", os.path.join(rundir, "test.sav"), 60, [])
        expect("buy-load (the coins)", [w[0] for w in wallet[-1:]], [COINS - price])
        expect("buy-load (TM165 in the Bag)", bag[-1:], [1])

    # 3. TM PRIZES: the old counter behind the new question, a second TM refused
    if "prizes" in wanted:
        tm32 = item_id("ITEM_TM32")
        base = dict(PKMN_TEST_PARTY=f"{SPECIES_PIKACHU}:20", PKMN_TEST_BAG=f"{coin_case}:1", PKMN_TEST_COINS=str(COINS),
                    PKMN_TEST_BAG_RESET="1", PKMN_TEST_WARP=TM_COUNTER, PKMN_TEST_BAG_LOG=str(tm32))
        # talk, "Welcome.", "You can exchange ..."; TM PRIZES (the first); "Which prize would you like?" and the prizes:
        # TM32 (the first row); "So your choice is the TM32 Double Team?" YES; "Here you go!"
        keys = ["30:UP", "80:A", "240:A", "400:A", "700:A", "900:A", "1060:A", "1220:A"]
        # the prizes again: TM32 refused ("You already have ...", "A TM is never used up ..."), back to the prizes; again
        keys += ["1420:A", "1580:A", "1780:B", "1940:A"]
        end = 2300
        _, wallet, bag, _ = step("prizes", args.save, end, keys, base, shots=",".join(str(f) for f in range(200, end, 60)))
        money = wallet[0][1] if wallet else None
        expect("prizes (the wallet: coins, money)", [wallet[:1], wallet[-1:]], [[(COINS, money)], [(COINS - 225, money)]])
        expect("prizes (TM32 in the Bag, one)", bag[-1:], [1])

    # 4-5. Taught from the Bag; the TM stays; saved and loaded
    if "teach" in wanted:
        party = f"{SPECIES_PIKACHU}:20:V{MOVE_THUNDER_SHOCK}.0.0.0"
        base = dict(PKMN_TEST_PARTY=party, PKMN_TEST_BAG=f"{TM_ITEM}:1", PKMN_TEST_BAG_RESET="1")
        t = 500
        # TM165 (the pocket's only item): A, USE; "Booted up a TM." "It contained Detect." "Teach Detect to a POKeMON?"
        # YES; "Teach which POKeMON?" Pikachu; "Pikachu learned Detect!"; back in the Bag
        keys = list(OPEN_BAG) + [f"{t}:A", f"{t + 100}:A"] + [f"{t + f}:A" for f in (260, 420, 580)]
        keys += [f"{t + f}:A" for f in (760, 920)]
        close = t + 1300
        keys += [f"{close}:B", f"{close + 120}:B"]                       # the Bag (to the start menu), the start menu
        save_at = close + 300
        rundir, _, bag, got = step("teach", args.save, save_at + 900, keys + bag_save_keys(save_at), base,
                                   shots=",".join(str(f) for f in range(t, save_at + 900, 60)))
        last = got.get(0, [None])[-1]
        expect("teach (Pikachu's moves)", last, (SPECIES_PIKACHU, (MOVE_THUNDER_SHOCK, detect, 0, 0)))
        expect("teach (TM165 still in the Bag: never used up)", bag[-1:] if bag else [1], [1])
        _, _, bag, got = step("teach-load", os.path.join(rundir, "test.sav"), 60, [])
        expect("teach-load (Pikachu's moves)", got.get(0, [None])[-1], (SPECIES_PIKACHU, (MOVE_THUNDER_SHOCK, detect, 0, 0)))
        expect("teach-load (TM165 in the Bag)", bag[-1:], [1])

    report = dict(steps=steps, problems=problems, passed=not problems)
    with open(os.path.join(args.out, "tm_catalogue_check.json"), "w") as out:
        json.dump(report, out, indent=1)
    print(json.dumps(dict(passed=not problems, problems=problems)))
    return 0 if not problems else 1


if __name__ == "__main__":
    sys.exit(main())
