#!/usr/bin/env python3
"""The Game Corner's RARE GOODS (the owner's request of 2026-10-24): every item a player cannot get anywhere else in the
game, sold for Game Corner coins at the prize counter, priced by what it does.

"all items that arent obtainable from anywhere that we have in game should be added to the casino store. if its an op item
make it more expensive base it off how we have our other items in the casino"; "items like ability patch and gold bottle
caps, if decided to add to the store make them extremyly expensive from like 200k - 500k".

An item is obtainable when any of these gives it (all read from the engine's own data, so the list follows the game):

  - the scripts a player can run (tools/qa/inventory.py: the maps reachable from Littleroot Town and the scripts that
    can run there): finditem / giveitem / additem / giveitem_msg, an item set into a variable for a later giveitem,
    the berries of setberrytree and giverandomberry, a held item of givemon, every pokemart list they open (the Mega
    Stone dealer's included);
  - the hidden items of the reachable maps;
  - the Battle Frontier's and Trainer Hill's prizes, the Lilycove lottery, the Pickup Ability's tables (C data);
  - what wild Pokemon hold (Thief and catching keep it): the species that appear in the PC game's wild pools, encounter
    tables and legend pool (not their evolutions: a Pokemon evolved or hatched holds nothing);
  - what the in-game trade Pokemon of runnable scripts hold (src/data/trade.h);
  - the PC game's own: the Game Corner's major prizes (Mega Ring, Shiny Charm), a new character's Poke Balls, and the
    world host's Battle Tower: Legends shop and floor prizes (TOWER_ITEMS below: the app's tower_rewards.py).

Left out of the shop (listed by --markdown): key and story items (but the key items that work here, KEY_GOODS: the Oval,
Exp. and Catching Charms and the form-change key items of Pokemon a player can own here, are sold, one each), items no
player can use in this game (a battle gimmick it does not enable - Z-Moves, Dynamax, Terastallization -, an item only for
a species nobody can own here, an evolution item whose evolution needs another region, an item with no effect at all in
this engine), and placeholders. Ability Patch and Gold Bottle Cap are sold although the Tower gives them (the owner named
them). Every list fits the native shop screen (NATIVE_SHOP_MAX_ITEMS; --check fails otherwise).

Prices (coins) follow the casino's own (the Mega Stones 30,000-100,000, the Mega Ring 25,000, the Shiny Charm 100,000; the
Game Corner's TMs 1,500-4,000): the item's kind decides its band (BANDS below); the permanent trait changers sit in the
owner's 200,000-500,000 band (Ability Patch 500,000, Gold Bottle Cap 400,000, the Mints 200,000); the key goods at the
Shiny Charm's 100,000; no price is ever below a twentieth of the item's money price, so coins bought with money and spent
here never buy an item that sells for more.

    tools/mp/gen_rare_goods.py             write data/scripts/mp_rare_goods_shop.inc and src/platform/mp_rare_goods_prices.h
    tools/mp/gen_rare_goods.py --check     fail when they are not what the generator makes, or a list is too long
    tools/mp/gen_rare_goods.py --markdown  the shop and the left-out items as tables, and every source counted
"""
from __future__ import annotations

import argparse
import collections
import json
import os
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "qa"))
import gen_wild_pool as pool  # noqa: E402
import inventory as qa  # noqa: E402

ENGINE = pool.ENGINE
SHOP = ENGINE / "data/scripts/mp_rare_goods_shop.inc"
OUTPUT = ENGINE / "src/platform/mp_rare_goods_prices.h"
LIST_LABEL = "MauvilleCity_GameCorner_Pokemart_RareGoods"
# The native (HD/3D) shop screen copies at most this many goods of a list (include/native_ui/ui_engine.h); the Mega Stone
# dealer's coin shop list is held to it too.
NATIVE_UI_HEADER = ENGINE / "include/native_ui/ui_engine.h"
MEGA_STONE_SHOP = ENGINE / "data/scripts/mp_mega_stone_shop.inc"

# The world host's Battle Tower: Legends shop and floor prizes (the app: src/emerald_multiplayer/tower_rewards.py,
# TOWER_SHOP and the floor prizes): the items it gives, by the engine's names.
TOWER_ITEMS = ("ITEM_HP_UP", "ITEM_PROTEIN", "ITEM_IRON", "ITEM_CALCIUM", "ITEM_ZINC", "ITEM_CARBOS", "ITEM_PP_UP",
               "ITEM_PP_MAX", "ITEM_ABILITY_CAPSULE", "ITEM_ABILITY_PATCH", "ITEM_RARE_CANDY", "ITEM_BOTTLE_CAP",
               "ITEM_GOLD_BOTTLE_CAP", "ITEM_CHOICE_BAND", "ITEM_CHOICE_SPECS", "ITEM_CHOICE_SCARF", "ITEM_LEFTOVERS",
               "ITEM_EXPERT_BELT", "ITEM_LIFE_ORB", "ITEM_FOCUS_SASH", "ITEM_ROCKY_HELMET", "ITEM_WEAKNESS_POLICY",
               "ITEM_ASSAULT_VEST", "ITEM_LUM_BERRY", "ITEM_SITRUS_BERRY")
# The PC game's own gifts and prizes in C (multiplayer_rival.c: a new character's Poke Balls; the Game Corner's major
# prize counter, data/scripts/mp_progression.inc, is a script and found as one).
PC_GIFTS = ("ITEM_POKE_BALL",)
# Sold here although obtainable (the owner's request): price fixed.
NAMED = {"ITEM_ABILITY_PATCH": 500_000, "ITEM_GOLD_BOTTLE_CAP": 400_000}
# Key items that work in this engine and no one can get, sold one each (shop.c: SOLD OUT once owned, a quantity of 1), at the
# Shiny Charm's price (the major prizes: 100,000). (list, coins, needs): needs None - always (a Charm works from the Bag:
# daycare.c, the Oval Charm's egg rate; battle_script_commands.c, the Catching Charm's critical captures and the Exp.
# Charm's 1.5x Exp.); "form" - a Pokemon a player can own here changes form with it (its form change table names it);
# "fusion" - two Pokemon a player can own here fuse with it (the fusion tables). Every other key item stays out (a story
# item, a ticket, a gimmick this game does not enable, the Pokemon it is for owned by no one here).
KEY_GOODS = {
    "ITEM_OVAL_CHARM": ("Other", 100_000, None),
    "ITEM_CATCHING_CHARM": ("Other", 100_000, None),
    "ITEM_EXP_CHARM": ("Training", 100_000, None),
    "ITEM_ROTOM_CATALOG": ("Evolution", 100_000, "form"),
    "ITEM_GRACIDEA": ("Evolution", 100_000, "form"),
    "ITEM_REVEAL_GLASS": ("Evolution", 100_000, "form"),
    "ITEM_ZYGARDE_CUBE": ("Evolution", 100_000, "form"),
    "ITEM_PRISON_BOTTLE": ("Evolution", 100_000, "form"),
    "ITEM_DNA_SPLICERS": ("Evolution", 100_000, "fusion"),
    "ITEM_N_SOLARIZER": ("Evolution", 100_000, "fusion"),
    "ITEM_N_LUNARIZER": ("Evolution", 100_000, "fusion"),
    "ITEM_REINS_OF_UNITY": ("Evolution", 100_000, "fusion"),
}
# The regions GetCurrentRegion() (include/regions.h) can name: an evolution IF_REGION another region never happens here.
REGIONS_HERE = ("REGION_KANTO", "REGION_HOENN")

GIVE_COMMANDS = re.compile(r"^\s*(finditem|giveitem|additem|giveitem_msg|giveitemfanfaremsg|buffergiveitem|checkitemspace_give)\b")


def cpp(source: str, portable: bool) -> str:
    flags = pool.CPPFLAGS + (["-DPORTABLE"] if portable else [])
    text = pool.run(["arm-none-eabi-gcc", "-E", "-P", *flags, source], cwd=ENGINE)
    return "\n".join(line for line in text.split("\n") if not line.lstrip().startswith("#"))


def item_ids() -> dict[str, int]:
    text = (ENGINE / "include/constants/items.h").read_text()
    body = text[text.index("enum __attribute__((packed)) Item"):text.index("ITEMS_COUNT")]
    return {m.group(1): int(m.group(2)) for m in re.finditer(r"^\s*(ITEM_\w+)\s*=\s*(\d+)\s*,", body, re.M)}


def c_value(expr: str):
    """A preprocessed constant expression: numbers, comparisons and ?: (the item data's generation switches)."""
    expr = expr.strip()
    while True:
        m = re.search(r"\(?\s*(\d+)\s*(>=|<=|==|!=|>|<)\s*(\d+)\s*\)?\s*\?\s*(\d+)\s*:\s*(\d+)", expr)
        if not m:
            break
        a, op, b, yes, no = m.groups()
        expr = expr[:m.start()] + (yes if eval(f"{a}{op}{b}") else no) + expr[m.end():]
    expr = expr.strip("() ")
    return int(expr) if re.fullmatch(r"\d+", expr) else None


def item_info(ids: dict[str, int]) -> dict[str, dict]:
    pre = cpp("src/item.c", True)
    start = pre.index("gItemsInfo[]")
    out = {}
    for match in re.finditer(r"\[(ITEM_\w+)\]\s*=\s*\{", pre[start:]):
        name = match.group(1)
        if name not in ids or name in out:
            continue
        brace = start + match.end() - 1
        body = pre[brace + 1:pool.balanced(pre, brace) - 1]
        fields = {}
        for part in pool.split_top(body):
            m = re.match(r"\.(\w+)\s*=\s*(.*)$", part.strip(), re.S)
            if m:
                fields[m.group(1)] = m.group(2).strip()
        title = re.search(r'_\("([^"]*)"\)', fields.get("name", "")) or re.search(r'"([^"]*)"', fields.get("name", ""))
        out[name] = dict(id=ids[name], title=title.group(1) if title else name, price=c_value(fields.get("price", "0")) or 0,
                         pocket=fields.get("pocket", ""), importance=c_value(fields.get("importance", "0")) or 0,
                         holdEffect=fields.get("holdEffect", "HOLD_EFFECT_NONE"), fieldUse=fields.get("fieldUseFunc", ""),
                         battleUsage=fields.get("battleUsage", ""), sortType=fields.get("sortType", ""),
                         secondaryId=fields.get("secondaryId", ""), effect=fields.get("effect", ""))
    return out


# ---------------------------------------------------------------------------------------------------------------------
# Where items come from

def script_sources(ids):
    """Items the runnable scripts of the reachable maps give, by source kind; and the species they give or battle."""
    scripts = qa.Scripts(str(ENGINE))
    map_ids = []
    for name, group, number in qa.map_list(str(ENGINE)):
        data = json.load(open(ENGINE / "data/maps" / name / "map.json"))
        map_ids.append(dict(name=name, id=data["id"]))
    seen_maps, run = qa.reachable(str(ENGINE), map_ids, scripts)
    found = collections.defaultdict(set)
    species = set()
    trades = set()
    lists = {}
    for path in [ENGINE / "data/event_scripts.s", *sorted((ENGINE / "data/scripts").glob("*.inc")),
                 *sorted((ENGINE / "data/maps").glob("*/scripts.inc"))]:
        label = None
        for line in qa.script_lines(str(path)):
            code = re.sub(r'"[^"]*"', "", line.split("@")[0]).strip()
            m = qa.LABEL.match(code)
            if m:
                label = m.group(1)
                continue
            if label is None or label not in run or label.startswith(LIST_LABEL):
                continue                # the RARE GOODS lists themselves are what is decided here
            if code.startswith(".2byte") and "ITEM_" in code:
                lists.setdefault(label, []).extend(re.findall(r"\bITEM_\w+", code))
            elif GIVE_COMMANDS.match(code):
                found["script gift"].update(re.findall(r"\bITEM_\w+", code))
            elif code.startswith(("setvar", "setorcopyvar", "copyvar")) and "ITEM_" in code:
                found["script gift (a variable)"].update(re.findall(r"\bITEM_\w+", code))
            elif code.startswith("setberrytree") or code.startswith("giverandomberry"):
                names = re.findall(r"\bBERRY_ID_(\w+)", code)
                if code.startswith("setberrytree"):
                    found["berry tree"].update(f"ITEM_{n}_BERRY" for n in names)
                else:
                    found["Berry Master"].add(("range",) + tuple(names))
            elif code.startswith("givemon") or code.startswith("givecustommon"):
                found["held by a gift Pokemon"].update(re.findall(r"\bITEM_\w+", code))
                species.update(re.findall(r"\bSPECIES_\w+", code))
            elif code.startswith(("setwildbattle", "seteventmon")):
                species.update(re.findall(r"\bSPECIES_\w+", code))
            if "INGAME_TRADE_" in code:
                trades.update(re.findall(r"\bINGAME_TRADE_\w+", code))
    # a list the runnable scripts open (pokemart <label>): the reach above names it as run when a script names it
    for label, items in lists.items():
        found["shop"].update(items)
    found["held by an in-game trade Pokemon"].update(trade_items(trades))
    return found, species, seen_maps


def trade_items(trades) -> set[str]:
    """The items the in-game trade Pokemon (src/data/trade.h, the Emerald build's) of these trades hold."""
    text = cpp("src/trade.c", True)
    start = text.find("sIngameTrades[]")
    out = set()
    for m in re.finditer(r"\[(INGAME_TRADE_\w+)\]\s*=\s*\{", text[start:] if start >= 0 else ""):
        if m.group(1) not in trades:
            continue
        brace = start + m.end() - 1
        held = re.search(r"\.heldItem\s*=\s*(ITEM_\w+)", text[brace:pool.balanced(text, brace)])
        if held and held.group(1) != "ITEM_NONE":
            out.add(held.group(1))
    return out


def berry_range(first: str, last: str, ids) -> set[str]:
    a, b = ids.get(f"ITEM_{first}_BERRY"), ids.get(f"ITEM_{last}_BERRY")
    if a is None or b is None:
        return set()
    return {n for n, v in ids.items() if a <= v <= b and n.endswith("_BERRY")}


def hidden_items(seen_maps) -> set[str]:
    out = set()
    for path in (ENGINE / "data/maps").glob("*/map.json"):
        data = json.load(open(path))
        if data["id"] not in seen_maps:
            continue
        for event in data.get("bg_events", []):
            if event.get("type") == "hidden_item" and event.get("item", "").startswith("ITEM_"):
                out.add(event["item"])
    return out


def c_tables() -> dict[str, set[str]]:
    """Prize tables in C (the named arrays): the Battle Frontier's and Battle Tents' prizes and its exchange corner, the
    Trainer Hill's prizes, the Lilycove lottery, the Pickup Ability's table."""
    tables = {
        "Battle Frontier prize": {"src/battle_palace.c": r"sBattlePalace\w*Prizes", "src/battle_arena.c": r"s\w*StreakPrizeItems",
                                  "src/battle_pyramid.c": r"s\w*StreakRewardItems", "src/battle_tent.c": r"s\w*TentRewards",
                                  "src/data/battle_frontier/battle_frontier_exchange_corner.h":
                                  r"sFrontierExchangeCorner_(?:Vitamins|HoldItems)"},
        "Trainer Hill prize": {"src/trainer_hill.c": r"sPrizeList\w+"},
        "Lilycove lottery": {"src/lottery_corner.c": r"sLotteryPrizes"},
        "Pickup": {"src/battle_script_commands.c": r"sPickupTable"},
    }
    out = {}
    for kind, files in tables.items():
        found = set()
        for rel, name in files.items():
            text = re.sub(r"//[^\n]*|/\*.*?\*/", "", (ENGINE / rel).read_text(errors="replace"), flags=re.S)
            for array in re.finditer(r"\b(" + name + r")\s*(?:\[[^\]]*\])+\s*=\s*\{", text):
                end = pool.balanced(text, array.end() - 1)
                found.update(re.findall(r"\bITEM_\w+", text[array.end():end]))
        out[kind] = found
    return out


WILD_SOURCES = ("src/data/mp_wild_pool.h", "src/platform/mp_wild_safari.h", "src/platform/mp_legend_data.h",
                "src/data/wild_encounters.json")


def species_named(files) -> set[str]:
    names = set()
    for rel in files:
        path = ENGINE / rel
        if path.exists():
            names.update(re.findall(r"\bSPECIES_[A-Z0-9_]+", path.read_text(errors="replace")))
    return names


def evolutions(pre):
    """[(species, method, target, the entry's text)] for every evolution entry of gSpeciesInfo."""
    out = []
    start = pre.index("gSpeciesInfo[]")
    for m in re.finditer(r"\[(SPECIES_\w+)\]\s*=\s*\{", pre[start:]):
        brace = start + m.end() - 1
        body = pre[brace:pool.balanced(pre, brace)]
        e = re.search(r"\.evolutions\s*=\s*\(const struct Evolution\[\]\)\s*\{", body)
        if not e:
            continue
        end = pool.balanced(body, e.end() - 1)
        for entry in pool.split_top(body[e.end():end - 1]):
            entry = entry.strip()
            method = re.match(r"\{\s*(EVO_\w+)", entry)
            target = re.search(r"\bSPECIES_\w+", entry)
            if method and target:
                out.append((m.group(1), method.group(1), target.group(0), entry))
    return out


def evolution_happens(method: str, entry: str) -> bool:
    """FALSE for an evolution that never happens here: EVO_NONE (a form the Pokedex lists) or one that needs a region
    GetCurrentRegion() never names (Alola, Galar, Hisui: Galarian Weezing, Alolan Raichu, Ursaluna by Peat Block)."""
    if method == "EVO_NONE":
        return False
    return all(region in REGIONS_HERE for region in re.findall(r"\bIF_REGION\s*,\s*(REGION_\w+)", entry))


def owned_species(pre, script_species) -> set[str]:
    """The species a player can own here: the PC game's wild pools and encounter tables, the starters, the scripts'
    gifts and static battles, the in-game trades; with the evolutions that can happen here, and pre-evolutions
    (breeding)."""
    names = set(script_species)
    names |= species_named(WILD_SOURCES + ("src/data/trade.h", "src/starter_choose.c", "src/platform/mp_starters.h",
                                           "src/data/pokemon/starters.h"))
    # evolutions (those that happen here) and pre-evolutions, until nothing new
    evo = collections.defaultdict(set)
    for species, method, target, entry in evolutions(pre):
        if evolution_happens(method, entry):
            evo[species].add(target)
        evo[target].add(species)
    todo = list(names)
    while todo:
        s = todo.pop()
        for t in evo.get(s, ()):
            if t not in names:
                names.add(t)
                todo.append(t)
    return names


def species_items(pre) -> dict[str, set[str]]:
    """species -> the items its wild members hold (itemCommon / itemRare)."""
    out = {}
    start = pre.index("gSpeciesInfo[]")
    for m in re.finditer(r"\[(SPECIES_\w+)\]\s*=\s*\{", pre[start:]):
        brace = start + m.end() - 1
        body = pre[brace:pool.balanced(pre, brace)]
        out.setdefault(m.group(1), set()).update(re.findall(r"\.item(?:Common|Rare)\s*=\s*(ITEM_\w+)", body))
    return out


def item_users(pre):
    """(item -> the species it is for: an evolution by the item (held or used) that can happen here, or a form change by
    it (the species whose form change table names it: a Mega Stone, a Primal orb, a plate, a drive, a memory...);
    item -> the pairs of species it fuses (the fusion tables: DNA Splicers...); the items only an evolution that never
    happens here names (Peat Block: Ursaluna in Hisui alone))."""
    out = collections.defaultdict(set)
    named = set()
    tables = {}
    for m in re.finditer(r"\b(s\w*FormChangeTable)\s*\[\s*\]\s*=\s*\{", pre):
        end = pool.balanced(pre, m.end() - 1)
        tables[m.group(1)] = set(re.findall(r"\{\s*FORM_CHANGE_\w+\s*,\s*SPECIES_\w+\s*,\s*(ITEM_\w+)", pre[m.end():end]))
    for species, method, target, entry in evolutions(pre):
        item = re.match(r"\{\s*EVO_\w+\s*,\s*(ITEM_\w+)", entry)
        if item:
            named.add(item.group(1))
            if evolution_happens(method, entry):
                out[item.group(1)].add(species)
    start = pre.index("gSpeciesInfo[]")
    for m in re.finditer(r"\[(SPECIES_\w+)\]\s*=\s*\{", pre[start:]):
        brace = start + m.end() - 1
        body = pre[brace:pool.balanced(pre, brace)]
        table = re.search(r"\.formChangeTable\s*=\s*(s\w+)", body)
        for item in tables.get(table.group(1), ()) if table else ():
            out[item].add(m.group(1))
    fusions = collections.defaultdict(set)
    for m in re.finditer(r"\bs\w*FusionTable\s*\[\s*\]\s*=\s*\{", pre):
        end = pool.balanced(pre, m.end() - 1)
        for item, a, b in re.findall(r"\{\s*\w+\s*,\s*(ITEM_\w+)\s*,\s*(SPECIES_\w+)\s*,\s*(SPECIES_\w+)", pre[m.end():end]):
            fusions[item].add((a, b))
    return out, fusions, named - set(out)


def sources(ids, info):
    found, script_species, seen_maps = script_sources(ids)
    pre = pool.preprocess()
    out = collections.defaultdict(set)
    for kind, items in found.items():
        for item in items:
            if isinstance(item, tuple):
                out[kind].update(berry_range(item[1], item[2], ids))
            else:
                out[kind].add(item)
    out["hidden item"] = hidden_items(seen_maps)
    for kind, items in c_tables().items():
        out[kind] = items
    owned = owned_species(pre, script_species)
    held = species_items(pre)
    # only a Pokemon met in the wild holds its species' items (CreateWildMon); one evolved, hatched or traded does not
    for s in species_named(WILD_SOURCES):
        out["held by wild Pokemon"].update(held.get(s, ()))
    out["Battle Tower: Legends (world host)"] = set(TOWER_ITEMS)
    out["PC game gift"] = set(PC_GIFTS)
    return {k: {i for i in v if i in ids} for k, v in out.items()}, owned, item_users(pre)


# ---------------------------------------------------------------------------------------------------------------------
# What is sold, in which of the clerk's lists, at what price

# Left out, by name (with why): what does nothing in this game.
UNUSABLE = {
    "a battle gimmick this game does not enable": (r"_Z$", r"TERA_SHARD$", r"^ITEM_TERA_ORB$", r"^ITEM_MAX_(HONEY|MUSHROOMS)$",
                                                   r"^ITEM_DYNAMAX_CANDY$", r"^ITEM_WISHING_PIECE$", r"^ITEM_GLIMMERING_CHARM$"),
    "a crafting or Pokemon-camp material with no use here": (r"MULCH$", r"APRICORN$", r"^ITEM_(ARMORITE|DYNITE)_ORE$",
                                                            r"^ITEM_GALARICA_TWIG$"),
    "a fossil no one here revives (the Devon scientist takes the Root and Claw Fossils)": (r"FOSSIL", r"^ITEM_OLD_AMBER$"),
    "for Pokemon no one can own here (Gen 7-9 forms)": (r"_MEMORY$", r"NECTAR$", r"_SWEET$", r"MASK$", r"^ITEM_BOOSTER_ENERGY$",
                                                        r"^ITEM_GIMMIGHOUL_COIN$"),
    "a placeholder (a TM slot that teaches nothing)": (r"^ITEM_TM(5[1-9]|[6-9]\d|100)$",),
}

# The clerk's lists: (label suffix, its name in the clerk's menu)
LISTS = (("Held", "HELD ITEMS"), ("Berries", "BERRIES"), ("Training", "TRAINING"), ("Evolution", "EVOLUTION"),
         ("Other", "BALLS & OTHER"))

# Prices in coins. The casino's own: the Mega Stones 30,000-100,000, the Mega Ring 25,000, the Shiny Charm 100,000, the
# Game Corner's TMs 1,500-4,000.
BANDS = (
    # (list, coins, names or patterns)
    # the permanent trait changers, in the owner's band ("items like ability patch and gold bottle caps ... 200k - 500k"):
    # a Mint rewrites a nature as a Patch rewrites an ability and a Gold Bottle Cap the IVs (the Bottle Cap and the Ability
    # Capsule come from the Tower and are not sold; priced here should they ever be)
    ("Training", 500_000, ("^ITEM_ABILITY_PATCH$",)),
    ("Training", 400_000, ("^ITEM_GOLD_BOTTLE_CAP$",)),
    ("Training", 250_000, ("^ITEM_BOTTLE_CAP$",)),
    ("Training", 200_000, ("_MINT$", "^ITEM_ABILITY_CAPSULE$")),
    ("Evolution", 100_000, ("^ITEM_(RED|BLUE)_ORB$", "^ITEM_(ADAMANT_CRYSTAL|LUSTROUS_GLOBE|GRISEOUS_CORE)$")),
    ("Held", 90_000, ("^ITEM_(SOUL_DEW|ADAMANT_ORB|LUSTROUS_ORB|GRISEOUS_ORB)$",)),
    ("Training", 30_000, ("^ITEM_LUCKY_EGG$",)),
    ("Held", 30_000, ("^ITEM_EVIOLITE$",)),
    ("Held", 20_000, ("^ITEM_(HEAVY_DUTY_BOOTS|COVERT_CLOAK|ABILITY_SHIELD|CLEAR_AMULET|MIRROR_HERB|LOADED_DICE)$",
                      "^ITEM_(FLAME|TOXIC)_ORB$", "_PLATE$")),
    ("Evolution", 15_000, ("_DRIVE$", "^ITEM_RUSTED_(SWORD|SHIELD)$")),
    ("Held", 15_000, ("^ITEM_LUCKY_PUNCH$",)),
    ("Evolution", 10_000, ("^ITEM_(ICE|SHINY|DUSK|DAWN)_STONE$", "^ITEM_(UPGRADE|PROTECTOR|DUBIOUS_DISC|REAPER_CLOTH|PRISM_SCALE)$",
                           "^ITEM_(WHIPPED_DREAM|SACHET|RAZOR_CLAW|RAZOR_FANG|LINKING_CORD|BLACK_AUGURITE)$",
                           "^ITEM_(SWEET_APPLE|TART_APPLE|SYRUPY_APPLE|CRACKED_POT|CHIPPED_POT|GALARICA_CUFF|GALARICA_WREATH)$",
                           "^ITEM_(UNREMARKABLE|MASTERPIECE)_TEACUP$", "^ITEM_(AUSPICIOUS|MALICIOUS)_ARMOR$",
                           "^ITEM_SCROLL_OF_", "^ITEM_METAL_ALLOY$", "^ITEM_ELECTIRIZER$", "^ITEM_MAGMARIZER$", "^ITEM_OVAL_STONE$")),
    ("Training", 10_000, ("^ITEM_EXP_CANDY_XL$", "^ITEM_FRESH_START_MOCHI$")),
    ("Held", 8_000, ("^ITEM_(MUSCLE_BAND|WISE_GLASSES|ZOOM_LENS|FLOAT_STONE|RING_TARGET|BINDING_BAND|ADRENALINE_ORB)$",
                     "^ITEM_(PROTECTIVE_PADS|UTILITY_UMBRELLA|ROOM_SERVICE|RED_CARD|EJECT_BUTTON|EJECT_PACK|THROAT_SPRAY)$",
                     "^ITEM_(BLUNDER_POLICY|AIR_BALLOON|SAFETY_GOGGLES|PUNCHING_GLOVE|TERRAIN_EXTENDER)$")),
    ("Held", 5_000, ("^ITEM_(DAMP|HEAT|SMOOTH|ICY)_ROCK$", "_SEED$", "_INCENSE$", "^ITEM_BERSERK_GENE$")),
    ("Training", 5_000, ("^ITEM_POWER_", "^ITEM_EXP_CANDY_L$")),
    ("Berries", 5_000, ("^ITEM_(GANLON|SALAC|PETAYA|APICOT|MICLE|CUSTAP|JABOCA|ROWAP|KEE|MARANGA|ENIGMA|LANSAT|STARF)_BERRY$",)),
    ("Held", 3_000, ("_GEM$", "^ITEM_FAIRY_FEATHER$")),
    ("Berries", 2_500, ("_BERRY$",)),
    ("Other", 2_500, ("^ITEM_(DUSK|QUICK|DREAM|BEAST)_BALL$",)),
    ("Training", 2_000, ("_MOCHI$", "^ITEM_EXP_CANDY_M$")),
    ("Other", 1_500, ("^ITEM_MAX_LURE$",)),
    ("Other", 1_000, ("_BALL$", "^ITEM_SUPER_LURE$")),
    ("Training", 500, ("^ITEM_EXP_CANDY_S$", "_FEATHER$")),
    ("Other", 500, ("^ITEM_LURE$", "^ITEM_X_", "^ITEM_POKE_TOY$", "_MAIL$")),
    ("Training", 200, ("^ITEM_EXP_CANDY_XS$",)),
)
# Everything else sold: what heals or feeds (the regional foods, remedies, Jubilife Muffin) 500; a treasure (a thing to
# sell) a tenth of its money price, at least 1,000.
FOOD, TREASURE = 500, 1_000


def unusable(name: str):
    for why, patterns in UNUSABLE.items():
        if any(re.search(p, name) for p in patterns):
            return why
    return None


def band(name: str, item: dict):
    for kind, coins, patterns in BANDS:
        if any(re.search(p, name) for p in patterns):
            return kind, coins
    if item["fieldUse"] in ("ItemUseOutOfBattle_Medicine",) or "MEDICINE" in item["sortType"] or "HEALTH" in item["sortType"]:
        return "Other", FOOD
    return "Other", max(TREASURE, -(-item["price"] // 10 // 100) * 100)


def round_coins(coins: int) -> int:
    step = 1000 if coins >= 10_000 else 100
    return -(-coins // step) * step


def no_effect(item: dict) -> bool:
    """An item this engine gives no effect at all: no use in the field (ItemUseOutOfBattle_CannotUse) or in battle, no
    held effect, no item effect, and not a treasure to sell or a mail (the Aux items and the Legends: Arceus foods, whose
    effects src/data/items.h marks "currently missing")."""
    return (item["fieldUse"] in ("", "ItemUseOutOfBattle_CannotUse") and not item["battleUsage"]
            and item["holdEffect"] in ("", "HOLD_EFFECT_NONE") and not item["effect"]
            and item["sortType"] not in ("ITEM_TYPE_SELLABLE", "ITEM_TYPE_RELIC", "ITEM_TYPE_MAIL"))


def key_good(name: str, users, fusions, owned) -> bool:
    """A key item of KEY_GOODS that works here: a Charm, or a form change or fusion of Pokemon a player can own here."""
    needs = KEY_GOODS[name][2]
    if needs == "form":
        return bool(users.get(name, set()) & owned)
    if needs == "fusion":
        return any(a in owned and b in owned for a, b in fusions.get(name, ()))
    return True


def build():
    """(the goods: [(item, list, coins)], the left out: [(item, why)], the sources) in item order."""
    ids = item_ids()
    info = item_info(ids)
    src, owned, (users, fusions, dead) = sources(ids, info)
    obtainable = set().union(*src.values())
    goods, left_out = [], []
    for name in sorted(info, key=lambda n: ids[n]):
        item = info[name]
        if name in obtainable and name not in NAMED:
            continue
        if name in KEY_GOODS:
            if key_good(name, users, fusions, owned):
                goods.append((name, KEY_GOODS[name][0], KEY_GOODS[name][1]))
            else:
                left_out.append((name, "a key item for Pokemon no one can own here"))
            continue
        if item["pocket"] == "POCKET_KEY_ITEMS" or item["importance"] > 0 and not name.startswith("ITEM_TM"):
            left_out.append((name, "a key or story item"))
            continue
        if name == "ITEM_ENIGMA_BERRY_E_READER" or item["title"] in ("?????", ""):
            left_out.append((name, "a placeholder"))
            continue
        why = unusable(name)
        if why is None and users.get(name) and not users[name] & owned:
            why = "for Pokemon no one can own here (Gen 7-9 forms)"     # an evolution or form item, a Mega Stone
        if why is None and name in dead:
            why = "only for an evolution in another region (Alola, Galar, Hisui): it never happens here"
        if why is None and not users.get(name) and no_effect(item):
            why = "no effect in this engine (its effect is missing)"
        if why:
            left_out.append((name, why))
            continue
        kind, coins = band(name, item)
        coins = max(coins, -(-item["price"] // 20))     # never a cheap way to money: coins cost money too
        goods.append((name, kind, NAMED.get(name, round_coins(coins))))
    return goods, left_out, src, info


def write(goods, info):
    shop = ["@ GENERATED by tools/mp/gen_rare_goods.py: the Game Corner's RARE GOODS (the PC game), every item no player",
            "@ can get anywhere else, in the prize clerk's lists (data/scripts/mp_progression.inc). Do not edit by hand.", ""]
    for suffix, title in LISTS:
        shop += ["\t.align 2", f"{LIST_LABEL}{suffix}::"]
        shop += [f"\t.2byte {name}" for name, kind, _ in goods if kind == suffix]
        shop += ["\tpokemartlistend", ""]
    SHOP.write_text("\n".join(shop), encoding="utf-8")
    prices = sorted(goods, key=lambda g: info[g[0]]["id"])
    lines = ["// GENERATED by tools/mp/gen_rare_goods.py: the Game Corner coin price of every RARE GOODS item (the PC game).",
             "// Do not edit by hand. The price follows what the item does (the generator's BANDS).", "",
             f"#define MP_RARE_GOODS_COUNT {len(prices)}",
             f"#define MP_RARE_GOODS_LIST_COUNT {len(LISTS)}", ""]
    lines += [f"extern const u16 {LIST_LABEL}{suffix}[];" for suffix, _ in LISTS]
    lines += ["", f"static const u16 *const sMPRareGoodsLists[MP_RARE_GOODS_LIST_COUNT] =", "{"]
    lines += [f"    {LIST_LABEL}{suffix}," for suffix, _ in LISTS]
    lines += ["};", "", "struct MPRareGoodsPrice { u16 item; u32 coins; };",
              "static const struct MPRareGoodsPrice sMPRareGoodsPrices[MP_RARE_GOODS_COUNT] =", "{"]
    lines += [f"    {{{name}, {coins}}}, // {info[name]['title']} ({kind})" for name, kind, coins in prices]
    lines += ["};", ""]
    OUTPUT.write_text("\n".join(lines), encoding="utf-8")


def markdown(goods, left_out, src, info) -> str:
    out = ["## Sources counted", "", "| Source | Items |", "|---|---:|"]
    out += [f"| {k} | {len(v)} |" for k, v in sorted(src.items())]
    for suffix, title in LISTS:
        rows = [g for g in goods if g[1] == suffix]
        out += ["", f"## {title} ({len(rows)})", "", "| Item | Coins |", "|---|---:|"]
        out += [f"| {info[n]['title']} | {c:,} |" for n, _, c in sorted(rows, key=lambda g: (-g[2], info[g[0]]['title']))]
    out += ["", f"## Left out ({len(left_out)})", "", "| Item | Why |", "|---|---|"]
    out += [f"| {info[n]['title']} | {why} |" for n, why in left_out]
    return "\n".join(out)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true", help="fail when the committed files are not what the generator makes")
    parser.add_argument("--markdown", action="store_true")
    args = parser.parse_args()
    goods, left_out, src, info = build()
    if args.markdown:
        print(markdown(goods, left_out, src, info))
        return 0
    too_long = lists_too_long(goods)
    if too_long:
        print("longer than the native shop screen shows (NATIVE_SHOP_MAX_ITEMS): " + ", ".join(too_long))
        return 1
    before = (SHOP.read_text() if SHOP.exists() else "", OUTPUT.read_text() if OUTPUT.exists() else "")
    write(goods, info)
    after = (SHOP.read_text(), OUTPUT.read_text())
    if args.check and before != after:
        SHOP.write_text(before[0])
        OUTPUT.write_text(before[1])
        print("mp_rare_goods_shop.inc / mp_rare_goods_prices.h are out of date")
        return 1
    print(f"{len(goods)} goods, {len(left_out)} left out")
    return 0


def lists_too_long(goods) -> list[str]:
    """The coin shops' lists the native shop screen could not show whole: every RARE GOODS list and the Mega Stone
    dealer's are held to NATIVE_SHOP_MAX_ITEMS (src/shop.c NativeUi_ShopStock copies no more; the rest and CANCEL
    would be out of sight)."""
    cap = int(re.search(r"#define\s+NATIVE_SHOP_MAX_ITEMS\s+(\d+)", NATIVE_UI_HEADER.read_text()).group(1))
    sizes = {f"RARE GOODS {title}": sum(1 for g in goods if g[1] == suffix) for suffix, title in LISTS}
    stones = re.search(r"MauvilleCity_Mart_Pokemart_MegaStones::(.*?)pokemartlistend", MEGA_STONE_SHOP.read_text(), re.S)
    if stones:
        sizes["Mega Stones"] = len(re.findall(r"\bITEM_\w+", stones.group(1)))
    return [f"{name} {n} > {cap}" for name, n in sizes.items() if n > cap]


if __name__ == "__main__":
    sys.exit(main())
