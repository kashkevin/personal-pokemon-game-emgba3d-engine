#!/usr/bin/env python3
"""The Game Corner's item hub (the owner's request and approved prices of 2026-10-31, docs/owner-decisions.md 30-31) in
real game processes (test builds; Linux and Windows alike; the Classic 2D one and the 3D one): the prize counter's ITEMS
and its ten kinds (data/scripts/mp_item_hub.inc, tools/mp/gen_item_hub.py). Everything is read from the game's own log
(PKMN_TEST_MEGA_LOG: the coins and the money whenever they change; PKMN_TEST_BAG_LOG: the counts of the items bought); the
rows and prices are the generated lists' own; the field keys are the presses a person makes. The player stands below the
prize counter's clerk (PKMN_TEST_WARP) with a Coin Case. --selftest-item-hub checks every list and price beside these.

  1. master  450,000 coins: ITEMS, POKé BALLS (the fifth kind), BUY, the Master Ball: "How many?" 1, the price
             (200,000), YES, "Here you go!"; again (repeat purchases: the owner's "buy as many as you like"); out of the
             list, the kinds: B, "Please come again!"; SAVE. 50,000 coins left, two Master Balls
  2. load    that save loaded: the same coins and Master Balls
  3. map     20,000 coins: OTHER (the tenth kind), the Town Map (a key item, 1,000): "You wanted Town Map?" YES,
             "Here you go!"; A on it again: sold out. 19,000 coins, one Town Map
  4. mega    20,000 coins and no MEGA RING: MEGA STONES (the third kind): "MEGA STONES are no use without a MEGA RING."
             and back to the kinds; nothing bought
  5. tm      20,000 coins: TMs (the ninth kind), DEFENSE & UTILITY, BUY, TM165 Detect (7,500), YES, "Here you go!"; out
             to the TM kinds, EXIT back to the hub's kinds. 12,500 coins, one TM165

    tools/battle_env/item_hub_check.py --exe ./pokeemerald-mp-bfxtest --save town.sav --out build/item-hub-check

Exit 0 when every step gave what it expects; the report is OUT/item_hub_check.json.
"""
import argparse
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mega_check import driver_lines, finish, numbers, save_keys, start  # noqa: E402

ENGINE = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SPECIES_MUDKIP = 258
PRIZE_COUNTER = "10:3:0:12:4"     # PKMN_TEST_WARP: the Mauville Game Corner, below the major prize clerk (12,2)
KINDS = ["Held", "Battle", "Mega", "Evolution", "Balls", "Healing", "Berries", "Training", "Tms", "Other"]
TM165 = 773 + 165                 # TM101-TM168 are items 874-941 (PC)


def item_id(name):
    text = open(os.path.join(ENGINE, "include/constants/items.h"), encoding="utf-8").read()
    return int(re.search(r"^\s*" + name + r"\s*=\s*(\d+)\s*,", text, re.M).group(1))


def hub_list(kind):
    text = open(os.path.join(ENGINE, "data/scripts/mp_item_hub_lists.inc"), encoding="utf-8").read()
    body = re.search(r"MauvilleCity_GameCorner_Pokemart_Hub" + kind + r"::[^\n]*\n(.*?)pokemartlistend", text, re.S).group(1)
    return re.findall(r"\.2byte (ITEM_\w+) @ (\d+) coins", body)


def utility_row(tm):
    text = open(os.path.join(ENGINE, "data/scripts/mp_tm_catalogue_lists.inc"), encoding="utf-8").read()
    body = re.search(r"MauvilleCity_GameCorner_Pokemart_TmUtility::(.*?)pokemartlistend", text, re.S).group(1)
    return [int(n) for n in re.findall(r"@ TM(\d+) ", body)].index(tm)


def downs(start, count, gap=10):
    return [f"{start + gap * i}:DOWN" for i in range(count)]


def to_kind(kind):
    """Talk to the clerk, ITEMS, the kind (its row of the ten). Returns the keys and the frame the kind was chosen."""
    chosen = 380 + 40 * KINDS.index(kind) + 60
    return ["30:UP", "80:A", "200:DOWN", "260:A"] + downs(380, KINDS.index(kind), 40) + [f"{chosen}:A"], chosen


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", required=True)
    parser.add_argument("--save", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--display", default=os.environ.get("DISPLAY"))
    parser.add_argument("--steps", default="master,map,mega,tm", help="which steps (master is followed by its load)")
    args = parser.parse_args()
    args.exe, args.save = os.path.abspath(args.exe), os.path.abspath(args.save)
    args.out = os.path.abspath(args.out)
    os.makedirs(args.out, exist_ok=True)
    wanted = set(args.steps.split(","))
    names = ("ITEM_MASTER_BALL", "ITEM_TOWN_MAP", "ITEM_VENUSAURITE")
    ids = {name: item_id(name) for name in names + ("ITEM_COIN_CASE",)}
    bag_log = ",".join(str(ids[name]) for name in names) + f",{TM165}"
    balls, other = hub_list("Balls"), hub_list("Other")
    master_row = [item for item, _ in balls].index("ITEM_MASTER_BALL")
    map_row = [item for item, _ in other].index("ITEM_TOWN_MAP")
    master_price = int(dict(balls)["ITEM_MASTER_BALL"])
    map_price = int(dict(other)["ITEM_TOWN_MAP"])
    steps, problems = [], []

    def step(name, save, frames, keys, coins, extra=None, shots=""):
        rundir = os.path.join(args.out, name)
        saved, args.save = args.save, save
        env = dict(PKMN_TEST_BATTLE=f"field:{frames}", PKMN_TEST_MEGA_LOG="1", PKMN_TEST_BAG_LOG=bag_log,
                   PKMN_TEST_FREEZE_FIELD="1", PKMN_TEST_AUTOPILOT="1", PKMN_TEST_BATTLE_DELAY="100",
                   PKMN_TEST_FIELD_KEYS=",".join(keys), PKMN_TEST_MEGA_SHOT_STEM=os.path.join(rundir, "step"))
        if keys:
            env.update(PKMN_TEST_PARTY=f"{SPECIES_MUDKIP}:20", PKMN_TEST_BAG=f"{ids['ITEM_COIN_CASE']}:1",
                       PKMN_TEST_BAG_RESET="1", PKMN_TEST_COINS=str(coins), PKMN_TEST_WARP=PRIZE_COUNTER)
        env.update(extra or {})
        if shots:
            env["PKMN_TEST_FIELD_SHOTS"] = shots
        try:
            process, output = start(args, rundir, env)
            code = finish(process, output, frames / 60 + 240)
        finally:
            args.save = saved
        lines = driver_lines(os.path.join(rundir, "test.log"))
        begun = next((i for i, line in enumerate(lines) if line.startswith("field keys for ")), 0) if keys else 0
        wallet = [tuple(numbers(line)) for line in lines[begun:] if line.startswith("mega wallet: ")]
        bag = [tuple(numbers(line)[2::2]) for line in lines[begun:] if line.startswith("bag field: ")]
        steps.append(dict(step=name, exit=code, wallet=wallet, bag=bag,
                          notes=[line for line in lines if line.startswith(("cannot", "assert"))],
                          pictures=sorted(f for f in os.listdir(rundir) if f.endswith(".png"))))
        if code != 0:
            problems.append(f"{name}: the game ended with {code}")
        problems.extend(f"{name}: {line}" for line in steps[-1]["notes"])
        return rundir, wallet, bag

    def expect(name, got, want):
        if got != want:
            problems.append(f"{name}: {got}, expected {want}")

    # 1-2. two Master Balls (not a key item: repeat purchases), saved and loaded
    if "master" in wanted:
        coins = 450_000
        keys, chosen = to_kind("Balls")
        up = chosen + 300
        keys += [f"{chosen + 140}:A"] + downs(up, master_row)                   # BUY; the Master Ball's row
        t = up + 10 * master_row + 100
        for buy in range(2):                                                   # how many 1, the price, YES, "Here you go!"
            keys += [f"{t + f}:A" for f in (0, 120, 240, 360)]
            t += 480
        keys += [f"{t + f}:B" for f in (0, 120, 240, 360)]                     # the list, the menu, the kinds, "come again"
        save_at = t + 600
        rundir, wallet, bag = step("master", args.save, save_at + 900, keys + save_keys(save_at), coins,
                                   shots=",".join(str(f) for f in range(chosen, save_at + 60, 60)))
        expect("master (the coins)", [w[0] for w in wallet[:1] + wallet[-1:]], [coins, coins - 2 * master_price])
        expect("master (Master Balls in the Bag)", [b[0] for b in bag[-1:]], [2])
        _, wallet, bag = step("load", os.path.join(rundir, "test.sav"), 60, [], 0)
        expect("load (the coins)", [w[0] for w in wallet[-1:]], [coins - 2 * master_price])
        expect("load (Master Balls)", [b[0] for b in bag[-1:]], [2])

    # 3. the Town Map: one per save, SOLD OUT once owned
    if "map" in wanted:
        coins = 20_000
        keys, chosen = to_kind("Other")
        up = chosen + 300
        keys += [f"{chosen + 140}:A"] + downs(up, map_row)
        t = up + 10 * map_row + 100
        keys += [f"{t + f}:A" for f in (0, 120, 240)]                          # "You wanted Town Map?" YES, "Here you go!"
        keys += [f"{t + f}:A" for f in (360, 480)]                             # again: sold out
        keys += [f"{t + f}:B" for f in (600, 720, 840, 960)]
        _, wallet, bag = step("map", args.save, t + 1300, keys, coins,
                              shots=",".join(str(f) for f in range(chosen, t + 1200, 60)))
        expect("map (the coins)", [w[0] for w in wallet[:1] + wallet[-1:]], [coins, coins - map_price])
        expect("map (Town Maps in the Bag)", [b[1] for b in bag[-1:]], [1])

    # 4. MEGA STONES without a MEGA RING: the message, back to the kinds, nothing bought
    if "mega" in wanted:
        coins = 20_000
        keys, chosen = to_kind("Mega")
        keys += [f"{chosen + f}:B" for f in (200, 340, 480, 620)]              # the message, the kinds: B, "come again"
        _, wallet, bag = step("mega", args.save, chosen + 900, keys, coins,
                              shots=",".join(str(f) for f in range(chosen, chosen + 800, 40)))
        expect("mega (the coins)", sorted({w[0] for w in wallet}), [coins])
        expect("mega (stones in the Bag)", sorted({b[2] for b in bag}) or [0], [0])

    # 5. a TM through the hub: TMs > DEFENSE & UTILITY > TM165 Detect
    if "tm" in wanted:
        coins = 20_000
        row = utility_row(165)
        keys, chosen = to_kind("Tms")
        keys += downs(chosen + 200, 3, 40) + [f"{chosen + 360}:A", f"{chosen + 500}:A"]   # DEFENSE & UTILITY; BUY
        up = chosen + 660
        keys += downs(up, row)
        t = up + 10 * row + 100
        keys += [f"{t + f}:A" for f in (0, 120, 240)]                          # "You wanted TM165 Detect?" YES, "Here you go!"
        keys += [f"{t + f}:B" for f in (360, 480, 600, 720, 840)]              # the list, the menu, the TM kinds, the kinds
        _, wallet, bag = step("tm", args.save, t + 1200, keys, coins,
                              shots=",".join(str(f) for f in range(chosen, t + 1100, 60)))
        expect("tm (the coins)", [w[0] for w in wallet[:1] + wallet[-1:]], [coins, coins - 7500])
        expect("tm (TM165 in the Bag)", [b[3] for b in bag[-1:]], [1])

    report = dict(rows=dict(master=master_row, town_map=map_row), steps=steps, problems=problems, passed=not problems)
    with open(os.path.join(args.out, "item_hub_check.json"), "w") as out:
        json.dump(report, out, indent=1)
    print(json.dumps(dict(passed=not problems, problems=problems)))
    return 0 if not problems else 1


if __name__ == "__main__":
    sys.exit(main())
