#!/usr/bin/env python3
"""A Poke Mart in real game processes (test builds; Linux and Windows alike): a Potion bought with money at the Oldale
Town Mart and sold back, each saved and loaded. Everything is read from the game's own log (PKMN_TEST_BAG_LOG: the
money and the Bag's Potions whenever one changes); the prices are the game's own (PKMN_TEST_FACTS: what a Mart asks for a
Potion and pays for one); the field keys are the presses a person makes (game frames at fast text, as the pictures each
step takes showed them). The player stands in front of the counter (PKMN_TEST_WARP) and faces the clerk.

  1. buy        "Welcome!", BUY, the Potion, one, "That will be ...: OK?" YES, "Here you go!", out of the list, QUIT,
                "Please come again!"; SAVE. The money is a Potion's price less, the Bag holds one Potion more
  2. buy-load   that save loaded: the same money and Potion
  3. sell       from that save: SELL, the Potion in the Bag, one, "I can pay ...: Would that be okay?" YES, "Turned over
                the Potion and received ...", out of the Bag, "Please come again!"; SAVE. The money is the Mart's price
                for a Potion more, the Potion gone
  4. sell-load  that save loaded: the same

    tools/battle_env/shop_check.py --exe ./pokeemerald-mp-bfxtest --save town.sav --out build/shop-check \\
        [--display :99] [--make-save [PATH]]

Exit 0 when every step gave what it expects; the report is OUT/shop_check.json.
"""
import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mega_check import driver_lines, finish, game_facts, make_save, numbers, save_keys, start  # noqa: E402

SPECIES_ZIGZAGOON = 263       # any Pokemon: the start menu then opens on POKeMON (save_keys counts its rows from there)

# Facing the clerk across the counter (LEFT), A: "Welcome!", "How may I serve you?" with BUY / SELL / QUIT (BUY first),
# the Mart's list (the Potion first), "Potion? Certainly. How many would you like?" (1), "That will be ... OK?" (YES
# first), "Here you go! Thank you very much.", the list again: B, "Is there anything else I can help you with?": B
# (QUIT), "Please come again!": A.
BUY = ["30:LEFT", "80:A", "200:A", "320:A", "440:A", "560:A", "680:A", "800:A", "920:B", "1040:B", "1160:A"]
BUY_SAVE_AT = 1400
# SELL (one row down), the Bag's ITEMS pocket with the Potion first, A, how many (1), "I can pay ... Would that be
# okay?" YES, "Turned over the Potion and received ...", the Bag again: B, "Please come again!": B.
SELL = ["30:LEFT", "80:A", "200:A", "330:DOWN", "380:A", "600:A", "720:A", "840:A", "960:A", "1080:B", "1200:B"]
SELL_SAVE_AT = 1400


def bag_lines(lines):
    """Each "bag field" line's (money, Potions), in order: the first is the save's (or the test set-up's), the last the
    end's."""
    states = []
    for line in lines:
        if line.startswith("bag field: "):
            n = numbers(line)                     # money M item I count C
            states.append((n[0], n[2]))
    return states


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", required=True)
    parser.add_argument("--save")
    parser.add_argument("--out", required=True)
    parser.add_argument("--display", default=os.environ.get("DISPLAY"))
    parser.add_argument("--make-save", nargs="?", const="", default=None, metavar="PATH",
                        help="first make the save (the short introduction) at PATH, or at OUT/made.sav")
    args = parser.parse_args()
    args.exe = os.path.abspath(args.exe)
    os.makedirs(args.out, exist_ok=True)
    if args.make_save is not None:
        args.save = make_save(args, os.path.abspath(args.make_save) if args.make_save
                              else os.path.join(args.out, "made.sav"))
    if not args.save:
        sys.exit("--save or --make-save")
    args.save = os.path.abspath(args.save)
    facts, _ = game_facts(args)
    potion, mart = facts["ITEM_POTION"], facts["MAP_OLDALE_TOWN_MART"]
    price, paid = facts["PRICE_ITEM_POTION"], facts["SELL_PRICE_ITEM_POTION"]
    at_counter = f"{mart >> 8}:{mart & 0xFF}:0:3:3"   # PKMN_TEST_WARP: the Mart's door, then in front of the counter
    steps, problems = [], []

    def step(name, save, frames, keys, extra=None, shots=""):
        rundir = os.path.join(args.out, name)
        saved, args.save = args.save, save
        env = dict(PKMN_TEST_BATTLE=f"field:{frames}", PKMN_TEST_BAG_LOG=str(potion), PKMN_TEST_FREEZE_FIELD="1",
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
        states = bag_lines(lines)
        steps.append(dict(step=name, exit=code, states=states,
                          notes=[line for line in lines if line.startswith(("cannot", "assert"))],
                          pictures=sorted(f for f in os.listdir(rundir) if f.endswith(".png"))))
        if code != 0:
            problems.append(f"{name}: the game ended with {code}")
        problems.extend(f"{name}: {line}" for line in steps[-1]["notes"])
        return rundir, states

    def expect(name, states, first=None, last=None):
        if not states:
            problems.append(f"{name}: nothing logged")
            return
        for label, want, got in (("before the keys", first, states[0]), ("after the keys", last, states[-1])):
            if want is not None and got != want:
                problems.append(f"{name}: {label} money {got[0]} and {got[1]} Potions, expected {want[0]} and {want[1]}")

    # 1-2. A Potion bought, saved and loaded
    buy_dir, states = step("buy", args.save, BUY_SAVE_AT + 900, BUY + save_keys(BUY_SAVE_AT),
                           dict(PKMN_TEST_PARTY=f"{SPECIES_ZIGZAGOON}:10", PKMN_TEST_WARP=at_counter),
                           shots="200,320,440,800,1040,1160,1300,1700")
    money = states[0][0] if states else 0
    expect("buy", states, (money, 0), (money - price, 1))
    _, states = step("buy-load", os.path.join(buy_dir, "test.sav"), 60, [])
    expect("buy-load", states, last=(money - price, 1))

    # 3-4. Sold back, saved and loaded
    sell_dir, states = step("sell", os.path.join(buy_dir, "test.sav"), SELL_SAVE_AT + 900, SELL + save_keys(SELL_SAVE_AT),
                            dict(PKMN_TEST_WARP=at_counter), shots="380,600,840,960,1100,1300,1700")
    expect("sell", states, (money - price, 1), (money - price + paid, 0))
    _, states = step("sell-load", os.path.join(sell_dir, "test.sav"), 60, [])
    expect("sell-load", states, last=(money - price + paid, 0))

    report = dict(prices=dict(potion=price, sell=paid), steps=steps, problems=problems, passed=not problems)
    with open(os.path.join(args.out, "shop_check.json"), "w") as out:
        json.dump(report, out, indent=1)
    print(json.dumps(dict(passed=not problems, problems=problems)))
    return 0 if not problems else 1


if __name__ == "__main__":
    sys.exit(main())
