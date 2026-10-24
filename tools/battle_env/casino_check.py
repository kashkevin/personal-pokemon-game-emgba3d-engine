#!/usr/bin/env python3
"""The Game Corner's RARE GOODS (the owner's request of 2026-10-24) in real game processes (test builds; Linux and Windows
alike; the Classic 2D one and the 3D one, whose native shop screen draws the lists): every item no player can get anywhere
else, sold for coins at the prize counter (data/scripts/mp_progression.inc, tools/mp/gen_rare_goods.py). Everything is
read from the game's own log (PKMN_TEST_MEGA_LOG: the coins and the money whenever they change; PKMN_TEST_BAG_LOG: the
counts of the goods bought); the prices and the rows are the generated lists' own (data/scripts/mp_rare_goods_shop.inc,
src/platform/mp_rare_goods_prices.h); the field keys are the presses a person makes (game frames at fast text, as the
pictures each step takes showed them). The player stands below the prize counter's clerk (PKMN_TEST_WARP) with a Coin Case
and 600,000 coins.

  1. buy    UP to the clerk, A: "Welcome to the PRIZE COUNTER! What would you like to see?" RARE GOODS; "RARE GOODS:
            things you won't find anywhere else! Which kind?" TRAINING; BUY; the Ability Patch (its row in the list):
            "How many?" 1, "That will be 500000 COINS." YES, "Here you go!"; A on it again: "You don't have enough COINS."
            (100,000 left); out of the list, QUIT, the kinds again: B, "Please come again!"; SAVE. The coins are 500,000
            less, the money the same, the Bag holds one Ability Patch
  2. load   that save loaded: the same coins and Ability Patch
  3. charm  TRAINING; the Exp. Charm (a key item: one each, at the Shiny Charm's 100,000): "You wanted Exp. Charm?
            That'll be 100000 COINS." YES, "Here you go!"; A on it again: "That item is sold out." The coins are 100,000
            less, the Bag holds one Exp. Charm
  4. long   HELD ITEMS, the longest list (more goods than the native shop screen once showed: 64): down to its last good,
            bought (1, YES); one row further is CANCEL, and A there leaves the list - nothing else bought. The coins are
            that good's price less, the Bag holds one

    tools/battle_env/casino_check.py --exe ./pokeemerald-mp-bfxtest --save town.sav --out build/casino-check

Exit 0 when every step gave what it expects; the report is OUT/casino_check.json.
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
COINS = 600_000
LIST_LABEL = "MauvilleCity_GameCorner_Pokemart_RareGoods"


def item_id(name):
    text = open(os.path.join(ENGINE, "include/constants/items.h"), encoding="utf-8").read()
    return int(re.search(r"^\s*" + name + r"\s*=\s*(\d+)\s*,", text, re.M).group(1))


def price_of(name):
    """The generated table's coins for an item (src/platform/mp_rare_goods_prices.h)."""
    text = open(os.path.join(ENGINE, "src/platform/mp_rare_goods_prices.h"), encoding="utf-8").read()
    return int(re.search(r"\{" + name + r", (\d+)\}", text).group(1))


def goods(suffix):
    """The items of one of the clerk's lists, in the order the shop shows them (data/scripts/mp_rare_goods_shop.inc)."""
    text = open(os.path.join(ENGINE, "data/scripts/mp_rare_goods_shop.inc"), encoding="utf-8").read()
    body = re.search(LIST_LABEL + suffix + r"::(.*?)pokemartlistend", text, re.S).group(1)
    return re.findall(r"\.2byte (ITEM_\w+)", body)


def downs(start, count, gap=10):
    return [f"{start + gap * i}:DOWN" for i in range(count)]


def to_kind(kind_downs):
    """Talk to the clerk, RARE GOODS, the kind (that many rows down), BUY: the list is up at frame 700."""
    return (["30:UP", "80:A", "200:DOWN", "260:A"]                       # talk; RARE GOODS
            + downs(380, kind_downs, 40) + [f"{380 + 40 * kind_downs + 60}:A"]   # the kind
            + ["600:A"])                                                  # BUY


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", required=True)
    parser.add_argument("--save", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--display", default=os.environ.get("DISPLAY"))
    parser.add_argument("--steps", default="buy,load,charm,long", help="which steps (load needs buy)")
    args = parser.parse_args()
    args.exe, args.save = os.path.abspath(args.exe), os.path.abspath(args.save)
    os.makedirs(args.out, exist_ok=True)
    wanted = set(args.steps.split(","))
    training, held = goods("Training"), goods("Held")
    patch, charm, last = "ITEM_ABILITY_PATCH", "ITEM_EXP_CHARM", held[-1]
    prices = {name: price_of(name) for name in (patch, charm, last)}
    ids = {name: item_id(name) for name in (patch, "ITEM_GOLD_BOTTLE_CAP", charm, last, "ITEM_COIN_CASE")}
    bag_log = ",".join(str(ids[n]) for n in (patch, "ITEM_GOLD_BOTTLE_CAP", charm, last))
    steps, problems = [], []
    base = dict(PKMN_TEST_PARTY=f"{SPECIES_MUDKIP}:20", PKMN_TEST_BAG=f"{ids['ITEM_COIN_CASE']}:1",
                PKMN_TEST_COINS=str(COINS), PKMN_TEST_WARP=PRIZE_COUNTER)

    def step(name, save, frames, keys, extra=None, shots=""):
        rundir = os.path.join(args.out, name)
        saved, args.save = args.save, save
        env = dict(PKMN_TEST_BATTLE=f"field:{frames}", PKMN_TEST_MEGA_LOG="1", PKMN_TEST_BAG_LOG=bag_log,
                   PKMN_TEST_FREEZE_FIELD="1", PKMN_TEST_AUTOPILOT="1", PKMN_TEST_BATTLE_DELAY="100",
                   PKMN_TEST_FIELD_KEYS=",".join(keys), PKMN_TEST_MEGA_SHOT_STEM=os.path.join(rundir, "step"))
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

    def spent(name, wallet, cost):
        money = wallet[0][1] if wallet else None
        if not wallet or wallet[0] != (COINS, money) or wallet[-1] != (COINS - cost, money):
            problems.append(f"{name}: the wallet {wallet[:1]} then {wallet[-1:]}, not {COINS} coins less {cost}, the money unchanged")
        return money

    # 1. the Ability Patch, its row in TRAINING (the third kind)
    row = training.index(patch)
    if "buy" in wanted:
        keys = (to_kind(2) + downs(760, row)
                + [f"{f + 10 * row}:A" for f in (860, 980, 1100, 1220)]          # how many 1, YES, "Here you go!"
                + [f"{f + 10 * row}:A" for f in (1340, 1460)]                    # again: not enough coins
                + [f"{f + 10 * row}:B" for f in (1560, 1680, 1800)] + [f"{1920 + 10 * row}:A"])
        save_at = 2100 + 10 * row
        rundir, wallet, bag = step("buy", args.save, save_at + 900, keys + save_keys(save_at), dict(base),
                                   shots=",".join(str(f + 10 * row) for f in (150, 320, 460, 700, 850, 1040, 1160, 1400, 1980, 2300)))
        money = spent("buy", wallet, prices[patch])
        if not bag or bag[-1][:2] != (1, 0):
            problems.append(f"buy: the Bag's Ability Patches and Gold Bottle Caps {bag[-1:]}, not one Patch (and the second refused)")
        if "load" in wanted:
            _, wallet, bag = step("load", os.path.join(rundir, "test.sav"), 60, [])
            if not wallet or wallet[-1] != (COINS - prices[patch], money) or not bag or bag[-1][0] != 1:
                problems.append(f"load: the saved wallet {wallet[-1:]} and Bag {bag[-1:]}, not {COINS - prices[patch]} coins and one Patch")

    # 3. the Exp. Charm: a key item, one each (shop.c: "You wanted ...?" with no quantity; SOLD OUT once owned)
    if "charm" in wanted:
        row = training.index(charm)
        keys = (to_kind(2) + downs(760, row)
                + [f"{f + 10 * row}:A" for f in (860, 980, 1100)]                # "You wanted ...?" YES, "Here you go!"
                + [f"{f + 10 * row}:A" for f in (1220, 1340)]                    # again: "That item is sold out."
                + [f"{f + 10 * row}:B" for f in (1440, 1560, 1680)] + [f"{1800 + 10 * row}:A"])
        _, wallet, bag = step("charm", args.save, 2100 + 10 * row, keys, dict(base),
                              shots=",".join(str(f + 10 * row) for f in (750, 850, 1000, 1160, 1280)))
        spent("charm", wallet, prices[charm])
        if not bag or bag[-1][2] != 1:
            problems.append(f"charm: the Bag's Exp. Charms {bag[-1:]}, not one (and the second refused)")

    # 4. the last good of HELD ITEMS (the first kind), then CANCEL one row further
    if "long" in wanted:
        row = len(held) - 1
        gap = 8
        end = 760 + gap * row
        keys = (to_kind(0) + downs(760, row, gap)
                + [f"{end + f}:A" for f in (100, 220, 340, 460)]                 # how many 1, YES, "Here you go!"
                + [f"{end + 560}:DOWN", f"{end + 620}:A"]                        # CANCEL: out of the list
                + [f"{end + f}:B" for f in (740, 860)] + [f"{end + 980}:A"])     # QUIT, the kinds: B, "Please come again!"
        _, wallet, bag = step("long", args.save, end + 1300, keys, dict(base),
                              shots=",".join(str(end + f) for f in (40, 160, 400, 590, 700)))
        spent("long", wallet, prices[last])
        if not bag or bag[-1][3] != 1:
            problems.append(f"long: the Bag's {last} {bag[-1:]}, not one")

    report = dict(price={k.lower(): v for k, v in prices.items()}, rows=dict(training=len(training), held=len(held)),
                  steps=steps, problems=problems, passed=not problems)
    with open(os.path.join(args.out, "casino_check.json"), "w") as out:
        json.dump(report, out, indent=1)
    print(json.dumps(dict(passed=not problems, problems=problems)))
    return 0 if not problems else 1


if __name__ == "__main__":
    sys.exit(main())
