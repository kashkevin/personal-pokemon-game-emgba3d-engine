#!/usr/bin/env python3
"""The Game Corner's item hub (the PC game; the owner's request of 2026-10-31, docs/owner-decisions.md 30-31).

The prize counter's ITEMS: every item a player can use here, for coins, in the owner's ten kinds - HELD ITEMS, BATTLE
ITEMS, MEGA STONES, EVOLUTION & FORMS, POKé BALLS, HEALING, BERRIES, TRAINING, TMs and OTHER. Eight of them are this
script's lists:

  - the RARE GOODS (tools/gen_rare_goods.py: every item no player can get anywhere else), each at its price there,
    unchanged, sorted into the owner's kinds ("rare_goods" in tools/mp/item_hub.json);
  - the items found elsewhere in the game at the prices the owner approved on 2026-10-31 ("yes approve": the revised
    table, "new" in item_hub.json: the Master Ball 200,000 coins and repeat purchases, Rare Candy and Ability Capsule
    5,000, Bottle Cap 2,500, the vitamins 300, competitive staples 10,000, everyday consumables at money / 20).

MEGA STONES opens the Mega Stone dealer's list (its prices, with a MEGA RING as at the dealer) and TMs the TM CATALOGUE's
kinds (tools/mp/gen_tm_catalogue.py); data/scripts/mp_item_hub.inc is the clerk's menu. This script writes:

    data/scripts/mp_item_hub_lists.inc   the eight lists (pokemart), each by item number
    src/platform/mp_item_hub.h           the lists and the new items' coin prices (src/platform/coin_shop.c; the RARE
                                         GOODS keep src/platform/mp_rare_goods_prices.h)

    python3 tools/mp/gen_item_hub.py            write them
    python3 tools/mp/gen_item_hub.py --check    fail when they are not what this script writes, or a rule is broken

It stops when a RARE GOODS item has no kind (a new one from gen_rare_goods.py needs a row), a row names an item the game
does not have, an item would be in two lists or priced twice, a new item is already sold at another coin counter, or a
list is longer than the native shop screen holds (NATIVE_SHOP_MAX_ITEMS).
"""
import argparse
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DATA = os.path.join(ROOT, "tools/mp/item_hub.json")
LISTS = os.path.join(ROOT, "data/scripts/mp_item_hub_lists.inc")
HEADER = os.path.join(ROOT, "src/platform/mp_item_hub.h")
LIST_LABEL = "MauvilleCity_GameCorner_Pokemart_Hub"
OWN_LISTS = ("Held", "Battle", "Evolution", "Balls", "Healing", "Berries", "Training", "Other")


def fail(message):
    sys.exit(f"gen_item_hub.py: {message}")


def read(path):
    with open(os.path.join(ROOT, path), encoding="utf-8") as fp:
        return fp.read()


def item_numbers():
    """Every ITEM_ constant's number: numbers as written, and names given as another constant (ITEM_CHERI_BERRY =
    FIRST_BERRY_INDEX, the old spellings) through that constant."""
    known = {}
    for name, value in re.findall(r"^\s*([A-Z_][A-Z0-9_]*)\s*=\s*([A-Z0-9_]+)\s*,", read("include/constants/items.h"), re.M):
        if value.isdigit():
            known[name] = int(value)
        elif value in known:
            known[name] = known[value]
    return {name: number for name, number in known.items() if name.startswith("ITEM_")}


def rare_goods():
    """Every RARE GOODS item and its coins (the generated lists and price table, as the game has them)."""
    lists = re.findall(r"MauvilleCity_GameCorner_Pokemart_RareGoods\w+::(.*?)pokemartlistend",
                       read("data/scripts/mp_rare_goods_shop.inc"), re.S)
    items = [item for body in lists for item in re.findall(r"\.2byte (ITEM_\w+)", body)]
    prices = dict((item, int(coins)) for item, coins in
                  re.findall(r"\{(ITEM_\w+), (\d+)\}", read("src/platform/mp_rare_goods_prices.h")))
    return items, prices


def other_counters():
    """Items the other coin counters sell: the Mega Stone dealer's list and the TM CATALOGUE."""
    mega = set(re.findall(r"\.2byte (ITEM_\w+)", read("data/scripts/mp_mega_stone_shop.inc")))
    tms = set(re.findall(r"\{(ITEM_TM\w+), \d+\}", read("src/platform/mp_tm_catalogue.h")))
    return mega | tms


def build():
    data = json.loads(read("tools/mp/item_hub.json"))
    numbers = item_numbers()
    kinds = [kind["key"] for kind in data["kinds"]]
    if kinds != ["Held", "Battle", "Mega", "Evolution", "Balls", "Healing", "Berries", "Training", "Tms", "Other"]:
        fail(f"the kinds are not the owner's ten in order: {kinds}")
    goods, goods_prices = rare_goods()
    mapping = data["rare_goods"]
    if sorted(goods) != sorted(mapping):
        fail("item_hub.json's rare_goods and the RARE GOODS lists differ: missing "
             f"{sorted(set(goods) - set(mapping))}, extra {sorted(set(mapping) - set(goods))}")
    elsewhere = other_counters()
    rows = []          # (kind, item, coins, source, name)
    for item in goods:
        if mapping[item] not in OWN_LISTS:
            fail(f"{item}: kind {mapping[item]!r} is not one of the hub's own lists")
        if item not in goods_prices:
            fail(f"{item}: no RARE GOODS price")
        rows.append((mapping[item], item, goods_prices[item], "rare goods", item))
    seen = set(goods)
    for entry in data["new"]:
        item, kind, coins = entry["item"], entry["kind"], entry["coins"]
        if item not in numbers:
            fail(f"{item}: not an item of include/constants/items.h")
        if item in seen:
            fail(f"{item}: in the hub twice")
        if item in goods_prices or item in elsewhere:
            fail(f"{item}: already sold at another coin counter")
        if kind not in OWN_LISTS:
            fail(f"{item}: kind {kind!r} is not one of the hub's own lists")
        if not isinstance(coins, int) or coins <= 0:
            fail(f"{item}: bad price {coins!r}")
        seen.add(item)
        rows.append((kind, item, coins, "new", entry["name"]))
    limit = int(re.search(r"#define NATIVE_SHOP_MAX_ITEMS (\d+)", read("include/native_ui/ui_engine.h")).group(1))
    lists = {kind: sorted((row for row in rows if row[0] == kind), key=lambda row: numbers[row[1]]) for kind in OWN_LISTS}
    for kind, items in lists.items():
        if not items or len(items) > limit:
            fail(f"the {kind} list has {len(items)} items (1 to {limit})")
    return data, lists


def render(data, lists):
    labels = {kind["key"]: kind["label"] for kind in data["kinds"]}
    out = ["@ GENERATED by tools/mp/gen_item_hub.py: the Game Corner's item hub (the PC game; the owner's request and",
           "@ approved prices of 2026-10-31), its eight own lists - the RARE GOODS at their prices and the items found",
           "@ elsewhere at the owner's - for the prize counter's ITEMS (data/scripts/mp_item_hub.inc). Do not edit by hand.", ""]
    for kind in OWN_LISTS:
        out += ["\t.align 2", f"{LIST_LABEL}{kind}::  @ {labels[kind]}, {len(lists[kind])} items"]
        for _, item, coins, source, name in lists[kind]:
            out.append(f"\t.2byte {item} @ {coins} coins ({source})")
        out += ["\tpokemartlistend", ""]
    new = [row for kind in OWN_LISTS for row in lists[kind] if row[3] == "new"]
    header = ["// GENERATED by tools/mp/gen_item_hub.py: the Game Corner's item hub (the PC game; the owner's request and",
              "// approved prices of 2026-10-31). The eight lists, and the coin price of every item they add to the RARE GOODS",
              "// (the RARE GOODS keep theirs: mp_rare_goods_prices.h). Do not edit by hand.", "",
              f"#define MP_ITEM_HUB_LIST_COUNT {len(OWN_LISTS)}", f"#define MP_ITEM_HUB_NEW_COUNT {len(new)}", ""]
    header += [f"extern const u16 {LIST_LABEL}{kind}[];" for kind in OWN_LISTS]
    header += ["", "static const u16 *const sMPItemHubLists[MP_ITEM_HUB_LIST_COUNT] =", "{"]
    header += [f"    {LIST_LABEL}{kind}," for kind in OWN_LISTS]
    header += ["};", "", "struct MPItemHubPrice { u16 item; u32 coins; };",
               "static const struct MPItemHubPrice sMPItemHubPrices[MP_ITEM_HUB_NEW_COUNT] =", "{"]
    header += [f"    {{{item}, {coins}}}, // {name} ({labels[kind]})" for kind, item, coins, _, name in
               sorted(new, key=lambda row: row[1])]
    header += ["};", ""]
    return "\n".join(out), "\n".join(header)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    data, lists = build()
    lists_text, header_text = render(data, lists)
    if args.check:
        stale = [path for path, text in ((LISTS, lists_text), (HEADER, header_text))
                 if not os.path.exists(path) or open(path, encoding="utf-8").read() != text]
        if stale:
            fail("not what this script writes: " + ", ".join(os.path.relpath(path, ROOT) for path in stale))
        return 0
    for path, text in ((LISTS, lists_text), (HEADER, header_text)):
        with open(path, "w", encoding="utf-8") as fp:
            fp.write(text)
    counts = ", ".join(f"{kind} {len(lists[kind])}" for kind in OWN_LISTS)
    print(f"item hub: {sum(len(items) for items in lists.values())} items ({counts})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
