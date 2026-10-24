#!/usr/bin/env python3
"""The Game Corner's RARE GOODS (the owner's request of 2026-10-24) in real game processes (test builds; Linux and Windows
alike): every item no player can get anywhere else, sold for coins at the prize counter (data/scripts/mp_progression.inc,
tools/mp/gen_rare_goods.py). Everything is read from the game's own log (PKMN_TEST_MEGA_LOG: the coins and the money
whenever they change; PKMN_TEST_BAG_LOG: the Ability Patch and Gold Bottle Cap counts); the prices are the game's own
(the generated table); the field keys are the presses a person makes (game frames at fast text, as the pictures each step
takes showed them). The player stands below the prize counter's clerk (PKMN_TEST_WARP) with a Coin Case and 600,000 coins.

  1. buy    UP to the clerk, A: "Welcome to the PRIZE COUNTER! What would you like to see?" RARE GOODS; "RARE GOODS:
            things you won't find anywhere else! Which kind?" TRAINING; BUY; the Ability Patch (the seventh row): "How
            many?" 1, "That will be 500000 COINS." YES, "Here you go!"; A on it again: "You don't have enough COINS." (100,000
            left); out of the list, QUIT, the kinds again: B, "Please come again!"; SAVE. The coins are 500,000 less, the
            money the same, the Bag holds one Ability Patch
  2. load   that save loaded: the same coins and Ability Patch

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
SPECIES_MUDKIP, ITEM_COIN_CASE, ITEM_ABILITY_PATCH, ITEM_GOLD_BOTTLE_CAP = 258, 718, 80, 134
PRIZE_COUNTER = "10:3:0:12:4"     # PKMN_TEST_WARP: the Mauville Game Corner, below the major prize clerk (12,2)
COINS = 600_000

BUY = ["30:UP", "80:A", "200:DOWN", "260:A",                      # talk; RARE GOODS
       "380:DOWN", "420:DOWN", "480:A",                            # TRAINING
       "600:A",                                                    # BUY
       "760:DOWN", "770:DOWN", "780:DOWN", "790:DOWN", "800:DOWN", "810:DOWN",   # the Ability Patch
       "860:A", "980:A", "1100:A", "1220:A",                       # how many 1, YES, "Here you go!"
       "1340:A", "1460:A",                                         # again: not enough coins
       "1560:B", "1680:B", "1800:B", "1920:A"]                     # out of the list, QUIT, the kinds: B, "Please come again!"
SAVE_AT = 2100


def price_of(name):
    """The generated table's coins for an item (src/platform/mp_rare_goods_prices.h)."""
    text = open(os.path.join(ENGINE, "src/platform/mp_rare_goods_prices.h"), encoding="utf-8").read()
    return int(re.search(r"\{" + name + r", (\d+)\}", text).group(1))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", required=True)
    parser.add_argument("--save", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--display", default=os.environ.get("DISPLAY"))
    args = parser.parse_args()
    args.exe, args.save = os.path.abspath(args.exe), os.path.abspath(args.save)
    os.makedirs(args.out, exist_ok=True)
    patch_price = price_of("ITEM_ABILITY_PATCH")
    steps, problems = [], []

    def step(name, save, frames, keys, extra=None, shots=""):
        rundir = os.path.join(args.out, name)
        saved, args.save = args.save, save
        env = dict(PKMN_TEST_BATTLE=f"field:{frames}", PKMN_TEST_MEGA_LOG="1",
                   PKMN_TEST_BAG_LOG=f"{ITEM_ABILITY_PATCH},{ITEM_GOLD_BOTTLE_CAP}", PKMN_TEST_FREEZE_FIELD="1",
                   PKMN_TEST_AUTOPILOT="1", PKMN_TEST_BATTLE_DELAY="100", PKMN_TEST_FIELD_KEYS=",".join(keys),
                   PKMN_TEST_MEGA_SHOT_STEM=os.path.join(rundir, "step"))
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

    rundir, wallet, bag = step("buy", args.save, SAVE_AT + 900, BUY + save_keys(SAVE_AT),
                               dict(PKMN_TEST_PARTY=f"{SPECIES_MUDKIP}:20", PKMN_TEST_BAG=f"{ITEM_COIN_CASE}:1",
                                    PKMN_TEST_COINS=str(COINS), PKMN_TEST_WARP=PRIZE_COUNTER),
                               shots="150,320,460,700,850,1040,1160,1400,1980,2300")
    money = wallet[0][1] if wallet else None
    if not wallet or wallet[0] != (COINS, money) or wallet[-1] != (COINS - patch_price, money):
        problems.append(f"buy: the wallet {wallet[:1]} then {wallet[-1:]}, not {COINS} coins less {patch_price}, the money unchanged")
    if not bag or bag[-1] != (1, 0):
        problems.append(f"buy: the Bag's Ability Patches and Gold Bottle Caps {bag[-1:]}, not one Patch (and the second refused)")
    _, wallet, bag = step("load", os.path.join(rundir, "test.sav"), 60, [])
    if not wallet or wallet[-1] != (COINS - patch_price, money) or not bag or bag[-1] != (1, 0):
        problems.append(f"load: the saved wallet {wallet[-1:]} and Bag {bag[-1:]}, not {COINS - patch_price} coins and one Patch")

    report = dict(price=dict(ability_patch=patch_price), steps=steps, problems=problems, passed=not problems)
    with open(os.path.join(args.out, "casino_check.json"), "w") as out:
        json.dump(report, out, indent=1)
    print(json.dumps(dict(passed=not problems, problems=problems)))
    return 0 if not problems else 1


if __name__ == "__main__":
    sys.exit(main())
