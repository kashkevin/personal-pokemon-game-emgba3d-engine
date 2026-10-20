#!/usr/bin/env python3
"""Mega Evolution in real game processes, single-player battles (test builds; Update 32, Mega Evolution).

Runs on Linux and on Windows alike: everything is read from the game's own log (the battle-environment test driver,
include/diorama/battle_env_test.h, TEST BUILDS ONLY: PKMN_TEST_LOG, PKMN_TEST_FACTS, PKMN_TEST_MEGA_LOG,
PKMN_TEST_MEGA_SWEEP); nothing reads the game's memory. The 3D test build (Makefile_pc DIORAMA=1 BFX_TEST=1) and the
Classic 2D one (BFX_TEST=1) take the same checks. First the game names its numbers (items, species, its Mega Stone
holders: PKMN_TEST_FACTS).

Cases (the default): one battle each from a save; the test party's lead holds a Mega Stone, the bag has the Mega Ring (or
not). At the action menu the keys a player presses: A (Fight), START (the Mega Evolution trigger), A (the first move);
then the driver fights the battle to its end with A. Checked: the lead's move menu offered Mega Evolution (or did not)
and the trigger turned on; the lead became the Mega form its stone makes by the species data, with that form's types and
Ability, marked as a Mega, and after its animation drawn with that form's palette (the shiny one for a shiny lead); the
Megas the battle marked; the battle's end; after it, the party's lead in its own form, still holding its stone. A picture of each Mega (PKMN_TEST_MEGA_SHOT) and, in 3D, a series of the battle.

--sweep: every Mega Stone holder of the form change tables in a wild battle of its own (PKMN_TEST_MEGA_SWEEP; --jobs games
at once), level 100, Flamethrower first: the same checks and a picture of each Mega. --shiny: the holders shiny (the
legendary ones too), and each lead checked shiny.

--parade: every Mega form drawn in battle in turn, on both sides (PKMN_TEST_PARADE_MEGAS; with --shiny in its shiny
colors), a picture of each: the forms' rendering, the 3D build's or the Classic 2D one's.

--items: a Mega Stone given, kept, used and taken through the game's own menus and saves (PKMN_TEST_BATTLE=field with
PKMN_TEST_FIELD_KEYS; the Classic 2D test build pictures each step): 1. Charizard holds nothing, the bag has
Charizardite X and the Mega Ring: START > POKeMON > Charizard > ITEM > GIVE > Charizardite X, then START > SAVE; 2. from
that save, a wild battle: Charizard still holds it and Mega Evolves, and holds it after the battle; 2b. from that save
again, the battle and the Mega Evolution, then back on the field ITEM > TAKE (not saved) and the game's own soft reset,
START > PLAYER > RESET, YES (on PC the A+B+START+SELECT chord never resets, and RESET is a field menu: no reset in the
middle of a battle): back from the title, the save as it was - Charizard holding its stone, none in the bag; 3. from
the save of 1: ITEM > TAKE, SAVE; 4. from that save: Charizard holds nothing and the bag has the one stone (not two,
not none).

    tools/battle_env/mega_check.py --exe ./pokeemerald-mp-bfxtest --save town.sav --out build/mega-check [--sweep]

--make-save: where no save may be brought (the Windows runner), a character made first from a fresh boot through the
game's own short introduction (NEW GAME, a boy, the offered name; PKMN_DEBUG_KEYS) is the save every battle starts from.

Exit 0 when every check gave what it expects; the report is OUT/mega_check.json.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
import re
import shutil
import subprocess
import sys
import time

KEYS = "30:A,70:START,110:A"
# name, PKMN_TEST_BATTLE, PKMN_TEST_PARTY ({NAME}: a fact), PKMN_TEST_BAG, whether the lead Mega Evolves, the Megas the
# battle marks (at least; a Multi's in-game partner and a PKMN_TEST_MEGA trainer may add theirs), more env. one_mega: the
# player's side marks exactly one (one Mega per trainer).
CASES = (
    ("wild", "wild:{SPECIES_POOCHYENA}:30", "{SPECIES_CHARIZARD}:50:i{ITEM_CHARIZARDITE_X}", "{ITEM_MEGA_RING}:1",
     True, 1, {}),
    ("trainer", "trainer:318", "{SPECIES_CHARIZARD}:50:i{ITEM_CHARIZARDITE_Y}", "{ITEM_MEGA_RING}:1", True, 1, {}),
    ("gym-roxanne", "trainer:265", "{SPECIES_MEWTWO}:70:i{ITEM_MEWTWONITE_X}", "{ITEM_MEGA_RING}:1", True, 1, {}),
    ("rival-may-shiny", "trainer:532", "{SPECIES_MEWTWO}:70:si{ITEM_MEWTWONITE_Y}", "{ITEM_MEGA_RING}:1", True, 1, {}),
    ("double-one-mega", "double:271", "{SPECIES_VENUSAUR}:50:i{ITEM_VENUSAURITE},{SPECIES_BLASTOISE}:50:i{ITEM_BLASTOISINITE}",
     "{ITEM_MEGA_RING}:1", True, 1, {"one_mega": True}),
    ("partner-multi", "partner:602:597:1", "{SPECIES_CHARIZARD}:50:i{ITEM_CHARIZARDITE_X}", "{ITEM_MEGA_RING}:1",
     True, 1, {}),
    ("both-sides", "trainer:759", "{SPECIES_BLAZIKEN}:50:i{ITEM_BLAZIKENITE}", "{ITEM_MEGA_RING}:1", True, 2,
     {"PKMN_TEST_MEGA": "1"}),
    ("no-mega-ring", "wild:{SPECIES_POOCHYENA}:30", "{SPECIES_CHARIZARD}:50:i{ITEM_CHARIZARDITE_X}", "", False, 0, {}),
    ("another-species-stone", "wild:{SPECIES_POOCHYENA}:30", "{SPECIES_CHARIZARD}:50:i{ITEM_VENUSAURITE}",
     "{ITEM_MEGA_RING}:1", False, 0, {}),
    ("no-mega-form", "wild:{SPECIES_POOCHYENA}:30", "{SPECIES_PIKACHU}:50:i{ITEM_CHARIZARDITE_X}", "{ITEM_MEGA_RING}:1",
     False, 0, {}),
    # The World Event's Mega Zygarde is caught as Zygarde-Complete (the form that Mega Evolves into it), a battle form.
    ("zygarde-complete-lead", "wild:{SPECIES_POOCHYENA}:30", "{SPECIES_ZYGARDE_COMPLETE}:50:v{MOVE_EARTHQUAKE}",
     "{ITEM_MEGA_RING}:1", False, 0, {}),
    ("zygarde-complete-mega", "wild:{SPECIES_POOCHYENA}:30", "{SPECIES_ZYGARDE_COMPLETE}:50:i{ITEM_ZYGARDITE}",
     "{ITEM_MEGA_RING}:1", True, 1, {}),
)
CONFIG = ("borderBackground=0\nbackgroundOrder=2\nfullscreen=0\nwindowScale=2\nintegerScale=0\nvsync=0\nborder=1\n"
          "volume=0\ncryVolume=0\n")
NUMBERS = re.compile(r"-?\d+")
SAVE_SIZE, SECTOR, SIGNATURE = 128 * 1024, 4096, b"\x25\x20\x01\x08"     # a sector's footer signature 0x08012025


def short_intro_keys():
    """The presses tools/pc_port/intro32_check.py short_intro_keys(0) makes: NEW GAME, a boy, the naming screen's OK,
    "So it's ...?" YES, then A through the arrival message to the game's own save (game frames)."""
    keys = [f"{f}:A" for f in range(400, 801, 100)] + ["980:A", "1100:START", "1140:A", "1300:A", "1360:A"]
    return keys + [f"{1500 + i * 45}:A" for i in range(24)]


def make_save(args, path):
    """A save from a fresh boot through the short introduction: the game writes it at the arrival in Littleroot."""
    rundir = os.path.join(args.out, "new-game")
    shutil.rmtree(rundir, ignore_errors=True)
    os.makedirs(rundir)
    with open(os.path.join(rundir, "pokeemerald.cfg"), "w") as cfg:
        cfg.write(CONFIG)
    env = dict(os.environ, SDL_AUDIODRIVER="dummy", PKMN_DEBUG_KEYS=",".join(short_intro_keys()))
    if args.display:
        env["DISPLAY"] = args.display
    target = os.path.join(rundir, "new.sav")
    with open(os.path.join(rundir, "game.log"), "w") as output:
        process = subprocess.Popen([args.exe, "--save", "new.sav"], cwd=rundir, env=env, stdout=output,
                                   stderr=subprocess.STDOUT)
        deadline, written = time.monotonic() + 300, None
        while time.monotonic() < deadline and process.poll() is None:
            data = open(target, "rb").read() if os.path.exists(target) else b""
            if len(data) == SAVE_SIZE and any(data[i + 0xFF8:i + 0xFFC] == SIGNATURE for i in range(0, SAVE_SIZE, SECTOR)):
                written = written or time.monotonic()
                if time.monotonic() - written > 5:   # every sector of it is on disk
                    break
            time.sleep(.5)
        process.kill()
        process.wait()
    if written is None:
        sys.exit(f"the short introduction wrote no save; see {rundir}")
    shutil.copy(target, path)
    return path


def numbers(text):
    return [int(n) for n in NUMBERS.findall(text)]


def start(args, rundir, env_extra, log_name="test.log"):
    """The game in a folder of its own: a copy of the save, a quiet configuration, the driver's log in log_name."""
    shutil.rmtree(rundir, ignore_errors=True)
    os.makedirs(rundir)
    shutil.copy(args.save, os.path.join(rundir, "test.sav"))
    with open(os.path.join(rundir, "pokeemerald.cfg"), "w") as cfg:
        cfg.write(CONFIG)
    env = dict(os.environ, SDL_AUDIODRIVER="dummy", PKMN_NOSCENE="1", PKMN_TEST_EXIT="1",
               PKMN_TEST_LOG=os.path.join(rundir, log_name), **env_extra)
    if args.display:
        env["DISPLAY"] = args.display
    output = open(os.path.join(rundir, "game.log"), "w")
    process = subprocess.Popen([args.exe, "--save", "test.sav"], cwd=rundir, env=env, stdout=output,
                               stderr=subprocess.STDOUT)
    return process, output


def finish(process, output, timeout):
    """The game's exit code, or None if it had to be stopped (it did not end in time)."""
    try:
        code = process.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()
        code = None
    output.close()
    return code


def driver_lines(path):
    """What the driver logged ("Battle env test: " taken off)."""
    if not os.path.exists(path):
        return []
    with open(path, encoding="utf-8", errors="replace") as text:
        return [line.split("Battle env test: ", 1)[1].rstrip("\n") for line in text if "Battle env test: " in line]


def game_facts(args):
    rundir = os.path.join(args.out, "facts")
    process, output = start(args, rundir, {"PKMN_TEST_FACTS": "1"})
    code = finish(process, output, 180)
    facts, holders = {}, []
    for line in driver_lines(os.path.join(rundir, "test.log")):
        if line.startswith("fact holder "):
            index, species, item, mega = numbers(line)
            holders.append(dict(index=index, species=species, item=item, mega=mega))
        elif line.startswith("fact "):
            name, value = line.split()[1:3]
            facts[name] = int(value)
    if code != 0 or not facts or not holders:
        sys.exit(f"the game named no facts (exit {code}); see {rundir}")
    return facts, holders


def battles_of(lines):
    """The log's battles, each from its "mega lead" line to its "mega after" line."""
    battles, holder, current = [], None, None
    for line in lines:
        if line.startswith("mega sweep holder "):
            holder = numbers(line)[0]
        elif line.startswith("mega lead: "):
            species, item, shiny, mega, type1, type2, ability = numbers(line)
            current = dict(holder=holder, lead=dict(species=species, item=item, shiny=shiny, mega=mega,
                                                   types=[type1, type2], ability=ability),
                           menus=[], battlers={}, outcome=None, after=None, shots=[], palette=None)
            battles.append(current)
        elif current is None:
            continue
        elif line.startswith("mega menu: "):
            battler, usable, trigger = numbers(line)
            current["menus"].append(dict(battler=battler, usable=usable, trigger=trigger))
        elif line.startswith("mega battler "):
            battler, species, type1, type2, ability, gimmick = numbers(line)
            current["battlers"].setdefault(battler, []).append(
                dict(species=species, types=[type1, type2], ability=ability, gimmick=gimmick))
        elif line.startswith("battle over, outcome "):
            current["outcome"] = line.split("battle over, ", 1)[1]
        elif line.startswith("mega palette: "):
            battler, species, shiny, normal = numbers(line)
            current["palette"] = dict(battler=battler, species=species, shiny=shiny, normal=normal)
        elif line.startswith("mega after: "):
            species, item, party, ring = numbers(line)
            current["after"] = dict(species=species, item=item, party=party, ring=ring)
        elif line.startswith("captured ") and line.endswith("(the Mega)"):
            current["shots"].append(os.path.basename(line.split()[1]) + ".png")
    return battles


def judge(battle, facts, mega_expected, megas, lead_species, lead_item, one_mega=False, shiny=None):
    """What went otherwise than a player would expect, in words (empty: all as expected)."""
    mega_gimmick, none = facts["GIMMICK_MEGA"], facts["GIMMICK_NONE"]
    problems = []
    lead = battle["lead"]
    if shiny is not None and bool(lead["shiny"]) != shiny:
        problems.append(f"the lead is {'' if lead['shiny'] else 'not '}shiny")
    menus = [m for m in battle["menus"] if m["battler"] == 0]
    offered = menus[0]["usable"] if menus else None
    trigger_on = any(m["trigger"] == 1 for m in menus)
    lead_lines = battle["battlers"].get(0, [])
    marked = sorted(b for b, seen in battle["battlers"].items() if any(v["gimmick"] == mega_gimmick for v in seen))
    if not menus:
        problems.append("the lead's move menu was never up")
    if mega_expected:
        want = dict(species=lead["mega"], types=lead["types"], ability=lead["ability"])
        if lead["mega"] == 0:
            problems.append("by the species data the lead's item makes no Mega form")
        if offered != mega_gimmick or not trigger_on:
            problems.append(f"the lead's move menu offered gimmick {offered}, trigger on: {trigger_on}")
        if not any(v["gimmick"] == mega_gimmick and v["species"] == want["species"] and v["types"] == want["types"]
                   and v["ability"] == want["ability"] for v in lead_lines):
            problems.append(f"the lead never showed its Mega form {want} marked as a Mega: {lead_lines[-4:]}")
        if len(marked) < megas:
            problems.append(f"the battle marked Megas on battlers {marked}, fewer than {megas}")
        # drawn in its own colors: after the Mega animation the lead's sprite palette is the Mega form's shiny one for a
        # shiny lead, its normal one otherwise (the species data's; every Mega form has a shiny palette of its own)
        palette, colors = battle["palette"], "shiny" if lead["shiny"] else "normal"
        if palette is None or palette["species"] != want["species"] or not palette[colors] or (
                palette["shiny"] and palette["normal"]):
            problems.append(f"after the Mega animation the lead was not drawn with the Mega form's {colors} palette: "
                            f"{palette}")
        if one_mega and len([b for b in marked if b % 2 == 0]) != 1:
            problems.append(f"the player's side marked Megas on battlers {[b for b in marked if b % 2 == 0]}, not one")
    else:
        if offered not in (None, none) or trigger_on:
            problems.append(f"the lead's move menu offered gimmick {offered}, trigger on: {trigger_on}")
        if marked:
            problems.append(f"the battle marked Megas on battlers {marked}")
    if battle["outcome"] is None:
        problems.append("the battle did not end")
    after = battle["after"]
    if after is None or (after["species"], after["item"]) != (lead_species, lead_item):
        problems.append(f"after the battle the party's lead is {after}, not species {lead_species} holding {lead_item}")
    return problems


def run_case(args, facts, case):
    name, battle, party, bag, mega_expected, megas, extra = case
    battle, party, bag = (text.format(**facts) for text in (battle, party, bag))
    extra = dict(extra)
    one_mega = extra.pop("one_mega", False)
    rundir = os.path.join(args.out, name)
    env = dict(PKMN_TEST_BATTLE=battle, PKMN_TEST_PARTY=party, PKMN_TEST_BAG=bag, PKMN_TEST_KEYS=KEYS,
               PKMN_TEST_FIGHT=str(args.fight), PKMN_TEST_AUTOPILOT="1", PKMN_TEST_BATTLE_DELAY="100",
               PKMN_TEST_FREEZE_FIELD="1", PKMN_TEST_MEGA_LOG="1", PKMN_TEST_MEGA_SHOT="30",
               PKMN_TEST_MEGA_SHOT_STEM=os.path.join(rundir, "mega"), **extra)
    if args.series:
        env.update(PKMN_TEST_CAPTURE=os.path.join(rundir, "cap"), PKMN_TEST_CAPTURE_DELAY="20",
                   PKMN_TEST_CAPTURE_EVERY="40", PKMN_TEST_CAPTURE_COUNT="16")
    started = time.monotonic()
    process, output = start(args, rundir, env)
    code = finish(process, output, args.timeout)
    lines = driver_lines(os.path.join(rundir, "test.log"))
    battles = battles_of(lines)
    first = party.split(",")[0].split(":")
    flags = first[2] if len(first) > 2 else ""
    lead_item = int(re.search(r"i(\d+)", flags).group(1)) if "i" in flags else 0
    problems = [] if len(battles) == 1 else [f"{len(battles)} battles in the log, not one"]
    if battles:
        problems += judge(battles[0], facts, mega_expected, megas, int(first[0]), lead_item, one_mega)
    if code is None:
        problems.append(f"the game did not end within {args.timeout} s")
    result = dict(case=name, battle=battle, party=party, bag=bag, mega_expected=mega_expected, exit=code,
                  seconds=round(time.monotonic() - started, 1), battle_seen=battles[0] if battles else None,
                  pictures=sorted(f for f in os.listdir(rundir) if f.endswith(".png")))
    result["problems"] = problems
    result["passed"] = not problems
    return result


def run_sweep_job(args, facts, holders, job, first, last):
    rundir = os.path.join(args.out, f"sweep{'-shiny' if args.shiny else ''}-{job}")
    sweep = f"{first}:{last}:100:s" if args.shiny else f"{first}:{last}"
    env = dict(PKMN_TEST_BATTLE=f"wild:{facts['SPECIES_POOCHYENA']}:2", PKMN_TEST_MEGA_SWEEP=sweep,
               PKMN_TEST_BAG=f"{facts['ITEM_MEGA_RING']}:1", PKMN_TEST_KEYS=KEYS, PKMN_TEST_FIGHT=str(args.fight),
               PKMN_TEST_AUTOPILOT="1", PKMN_TEST_BATTLE_DELAY="60", PKMN_TEST_FREEZE_FIELD="1",
               PKMN_TEST_MEGA_SHOT="30", PKMN_TEST_MEGA_SHOT_STEM=os.path.join(rundir, "mega"))
    started = time.monotonic()
    process, output = start(args, rundir, env)
    code = finish(process, output, args.timeout + 90 * (last - first + 1))
    battles = battles_of(driver_lines(os.path.join(rundir, "test.log")))
    results = []
    for holder in holders[first:last + 1]:
        mine = [b for b in battles if b["holder"] == holder["index"]]
        problems = [] if len(mine) == 1 else [f"{len(mine)} battles of this holder in the log, not one"]
        if mine:
            battle = mine[0]
            problems += judge(battle, facts, True, 1, holder["species"], holder["item"], shiny=args.shiny)
            if battle["lead"]["mega"] != holder["mega"]:
                problems.append(f"the lead's Mega by the species data is {battle['lead']['mega']}, the holder list's "
                                f"{holder['mega']}")
        results.append(dict(holder=holder, job=job, passed=not problems, problems=problems,
                            battle_seen=mine[0] if mine else None))
    return dict(job=job, first=first, last=last, exit=code, seconds=round(time.monotonic() - started, 1),
                results=results)


# The menus at fast text, game frames from the field keys' start (60 a second), as their pictures showed them. The start
# menu of a player with a Pokemon and no Pokedex: POKeMON, BAG, <the player>, SAVE, OPTION, EXIT (it remembers its
# cursor); a lone Pokemon's menu: SUMMARY, ITEM, CANCEL; ITEM's: GIVE, TAKE, MOVE, CANCEL.
PARTY_ITEM = ["30:START", "80:A", "170:A", "220:DOWN", "260:A"]                     # POKeMON > the lead > ITEM
GIVE = PARTY_ITEM + ["320:A", "440:A", "560:A", "640:B", "720:B"]  # GIVE, the bag's first item, "... was given ...", out
TAKE = PARTY_ITEM + ["320:DOWN", "360:A", "460:A", "560:B", "640:B"]  # TAKE, "Received the ... from ...", out


def save_keys(start):
    """START > SAVE > YES ("Would you like to save the game?"), YES again if it asks to overwrite the saved file; the
    closing message ("... saved the game.") goes by itself after a second, so no A after it (in front of a clerk it
    would start another conversation); a B for safety."""
    return [f"{start + f}:{k}" for f, k in ((0, "START"), (50, "DOWN"), (80, "DOWN"), (110, "DOWN"), (160, "A"),
                                            (260, "A"), (380, "A"), (700, "B"))]


def reset_keys(start):
    """START > PLAYER (the third row) > RESET (its list ends with RESET, BACK: Up twice from the top), then YES (the
    question's cursor starts on NO): tools/pc_port/soft_reset_check.py's reset_keys(start, True, 2)."""
    return [f"{start + f}:{k}" for f, k in ((0, "START"), (60, "DOWN"), (100, "DOWN"), (140, "A"), (200, "UP"),
                                            (240, "UP"), (280, "A"), (400, "UP"), (440, "A"))]


# The purchases (PKMN_TEST_WARP to the shop, the player facing down): a tap turns to face the clerk across the counter,
# then the shop's own lines at fast text, as their pictures showed them. The Game Corner's prize counter: its welcome,
# what the Ring does, "Trade 25,000 COINS for it?" YES, "Obtained the Mega Ring!", the KEY ITEMS POCKET, "yours to
# keep", where the stones are sold; then the Shiny Charm's offer: NO (B), "Oh, is that so?" and its second line.
BUY_RING = ["30:UP", "80:A", "200:A", "320:A", "560:A", "680:A", "800:A", "920:B", "1040:A", "1160:A", "1280:B",
            "1400:A", "1520:A"]
# The Mauville dealer (the Ring in the bag): its line and BUY, the first stone (Venusaurite), "How many?" 1, "That will be
# 60000 COINS." YES, "Here you go!", out of the list, QUIT, "Please come again!".
BUY_STONE = ["30:LEFT", "80:A", "200:A", "320:A", "420:A", "520:A", "620:A", "720:B", "840:B", "940:A"]


def wallet_lines(lines):
    """The "mega wallet" lines after the field keys began: (coins, money) each."""
    begun = next((i for i, line in enumerate(lines) if line.startswith("field keys for ")), None)
    return [] if begun is None else [tuple(numbers(line)) for line in lines[begun:] if line.startswith("mega wallet: ")]


def field_lines(lines):
    """The "mega field" lines after the field keys began: (species, item, party, stones, ring) each."""
    begun = next((i for i, line in enumerate(lines) if line.startswith("field keys for ")), None)
    return [] if begun is None else [tuple(numbers(line)) for line in lines[begun:] if line.startswith("mega field: ")]


def run_items(args, facts):
    stone, ring, charizard = facts["ITEM_CHARIZARDITE_X"], facts["ITEM_MEGA_RING"], facts["SPECIES_CHARIZARD"]
    steps, problems = [], []

    def step(name, save, env, timeout=240):
        rundir = os.path.join(args.out, f"items-{name}")
        saved = dict(args.__dict__)
        args.save = save
        try:
            process, output = start(args, rundir, dict(PKMN_TEST_MEGA_LOG="1", PKMN_TEST_FREEZE_FIELD="1",
                                                       PKMN_TEST_AUTOPILOT="1", PKMN_TEST_BATTLE_DELAY="100",
                                                       PKMN_TEST_MEGA_SHOT_STEM=os.path.join(rundir, "step"), **env))
            code = finish(process, output, timeout)
        finally:
            args.save = saved["save"]
        lines = driver_lines(os.path.join(rundir, "test.log"))
        steps.append(dict(step=name, exit=code, pictures=sorted(f for f in os.listdir(rundir) if f.endswith(".png"))))
        return rundir, lines

    # 1. give, save (pictures: the start menu, ITEM's menu, the bag, the message, the save's question, its end)
    give_dir, lines = step("give", args.save, dict(PKMN_TEST_BATTLE="field:1700", PKMN_TEST_PARTY=f"{charizard}:50",
                                                    PKMN_TEST_BAG=f"{stone}:1,{ring}:1",
                                                    PKMN_TEST_FIELD_KEYS=",".join(GIVE + save_keys(800)),
                                                    PKMN_TEST_FIELD_SHOTS="60,300,400,500,1000,1120,1250"))
    seen = field_lines(lines)
    if not seen or seen[0][:2] != (charizard, 0) or seen[0][3:] != (1, 1):
        problems.append(f"give: before the keys {seen[:1]}, not Charizard holding nothing with one stone and the Ring")
    if not seen or seen[-1][:2] != (charizard, stone) or seen[-1][3] != 0:
        problems.append(f"give: after the keys {seen[-1:]}, not Charizard holding the stone and none in the bag")
    given_save = os.path.join(give_dir, "test.sav")
    if open(given_save, "rb").read() == open(args.save, "rb").read():
        problems.append("give: the game's save is unchanged (SAVE did not write it)")
    # 2. a battle from that save: the stone kept, used, kept
    _, lines = step("battle", given_save, dict(PKMN_TEST_BATTLE=f"wild:{facts['SPECIES_POOCHYENA']}:30", PKMN_TEST_KEYS=KEYS,
                                                 PKMN_TEST_FIGHT=str(args.fight), PKMN_TEST_MEGA_SHOT="30"),
                           args.timeout)
    loaded = [tuple(numbers(line)) for line in lines if line.startswith("mega field: ")]
    if not loaded or loaded[0][:2] != (charizard, stone) or loaded[0][3] != 0:
        problems.append(f"battle: the loaded save has {loaded[:1]}, not Charizard holding the stone")
    battles = battles_of(lines)
    if len(battles) != 1:
        problems.append(f"battle: {len(battles)} battles in the log")
    else:
        problems += ["battle: " + text for text in judge(battles[0], facts, True, 1, charizard, stone)]
    # 2b. from that save: the battle and its Mega Evolution, then on the field TAKE (not saved) and RESET, YES: from the
    # title (the autopilot: CONTINUE), the save as it was (pictures: the field, PLAYER's list, the question, YES)
    reset_dir, lines = step("reset", given_save, dict(PKMN_TEST_BATTLE=f"wild:{facts['SPECIES_POOCHYENA']}:30",
                                                        PKMN_TEST_KEYS=KEYS, PKMN_TEST_FIGHT=str(args.fight),
                                                        PKMN_TEST_FIELD_AFTER="1500", PKMN_TEST_SOFT_RESET="1",
                                                        PKMN_TEST_FIELD_KEYS=",".join(TAKE + reset_keys(760)),
                                                        PKMN_TEST_FIELD_SHOTS="700,940,1100,1180"),
                                  args.timeout)
    battles = battles_of(lines)
    mega_seen = battles and any(v["gimmick"] == facts["GIMMICK_MEGA"] for v in battles[0]["battlers"].get(0, []))
    restarted = next((i for i, line in enumerate(lines) if line.startswith("soft reset: the game restarted")), None)
    keys_at = next((i for i, line in enumerate(lines) if line.startswith("field keys for ")), None)
    before = [] if keys_at is None else [tuple(numbers(line)) for line in lines[keys_at:restarted]
                                         if line.startswith("mega field: ")]
    after = [] if restarted is None else [tuple(numbers(line)) for line in lines[restarted:]
                                          if line.startswith("mega field: ")]
    game_log = open(os.path.join(reset_dir, "game.log"), encoding="utf-8", errors="replace").read()
    steps[-1]["game_says"] = [text for text in ("soft reset: asked (no world)", "soft reset: resetting") if text in game_log]
    if not mega_seen or len(battles) != 1 or battles[0]["outcome"] is None:
        problems.append(f"reset: the battle ({len(battles)}, outcome {battles[0]['outcome'] if battles else None}) or "
                        f"its Mega Evolution ({bool(mega_seen)}) did not happen")
    if keys_at is None or keys_at < next((i for i, line in enumerate(lines) if line.startswith("battle over")), len(lines)):
        problems.append("reset: the field keys did not start after the battle")
    if not before or before[-1][:2] != (charizard, 0) or before[-1][3:] != (1, 1):
        problems.append(f"reset: before RESET {before[-1:]}, not Charizard holding nothing and the stone in the bag")
    if restarted is None or "soft reset: back on the field" not in lines[restarted:]:
        problems.append("reset: the game did not restart (its frame count never went back) and come back to the field")
    elif not after or after[-1][:2] != (charizard, stone) or after[-1][3:] != (0, 1):
        problems.append(f"reset: back on the field {after[-1:]}, not the save's Charizard holding its stone, none in the bag")
    # 3. take, save
    take_dir, lines = step("take", given_save, dict(PKMN_TEST_BATTLE="field:1600",
                                                     PKMN_TEST_FIELD_KEYS=",".join(TAKE + save_keys(700)),
                                                     PKMN_TEST_FIELD_SHOTS="340,400,840,1150"))
    seen = field_lines(lines)
    if not seen or seen[-1][:2] != (charizard, 0) or seen[-1][3] != 1:
        problems.append(f"take: after the keys {seen[-1:]}, not Charizard holding nothing and one stone in the bag")
    # 4. load
    _, lines = step("load", os.path.join(take_dir, "test.sav"), dict(PKMN_TEST_BATTLE="field:60"))
    loaded = [tuple(numbers(line)) for line in lines if line.startswith("mega field: ")]
    if not loaded or loaded[-1][:2] != (charizard, 0) or loaded[-1][3:] != (1, 1):
        problems.append(f"load: the saved state is {loaded[-1:]}, not Charizard holding nothing, one stone, the Ring")
    # 5. the Mega Ring bought at the Game Corner's prize counter for coins, saved; 6. from that save, the Mauville
    # dealer's first Mega Stone (Venusaurite) bought for coins, saved; 7. that save loaded: the Ring, the stone, the
    # coins left; the money never changes. The prices are the game's own (facts).
    corner, mart, venusaurite = facts["MAP_MAUVILLE_CITY_GAME_CORNER"], facts["MAP_MAUVILLE_CITY_MART"], facts["ITEM_VENUSAURITE"]
    ring_price, stone_price = facts["MP_MEGA_RING_COINS"], facts["PRICE_ITEM_VENUSAURITE"]
    coins = ring_price + stone_price + 5000
    ring_dir, lines = step("buy-ring", args.save, dict(PKMN_TEST_BATTLE="field:2500", PKMN_TEST_PARTY=f"{charizard}:50",
                                                        PKMN_TEST_BAG=f"{facts['ITEM_COIN_CASE']}:1", PKMN_TEST_COINS=str(coins),
                                                        PKMN_TEST_WARP=f"{corner >> 8}:{corner & 0xFF}:0:12:4",
                                                        PKMN_TEST_FIELD_KEYS=",".join(BUY_RING + save_keys(1640)),
                                                        PKMN_TEST_FIELD_SHOTS="250,380,620,700,1100,1220,1450,2100"))
    seen, wallet = field_lines(lines), wallet_lines(lines)
    money = wallet[0][1] if wallet else None
    if not seen or seen[0][4] != 0 or seen[-1][4] != 1:
        problems.append(f"buy-ring: the Ring before and after {seen[:1]} {seen[-1:]}, not none and then one")
    if not wallet or wallet[0] != (coins, money) or wallet[-1] != (coins - ring_price, money):
        problems.append(f"buy-ring: the wallet {wallet[:1]} then {wallet[-1:]}, not {coins} coins less {ring_price}, "
                        f"the money unchanged")
    stone_dir, lines = step("buy-stone", os.path.join(ring_dir, "test.sav"),
                            dict(PKMN_TEST_BATTLE="field:1700", PKMN_TEST_WARP=f"{mart >> 8}:{mart & 0xFF}:0:3:2",
                                 PKMN_TEST_FIELD_KEYS=",".join(BUY_STONE + save_keys(1060)),
                                 PKMN_TEST_FIELD_SHOTS="260,380,480,560,640,780,1400"))
    seen, wallet = field_lines(lines), wallet_lines(lines)
    left = coins - ring_price - stone_price
    if not seen or seen[0][3:] != (0, 1) or seen[-1][3:] != (1, 1):
        problems.append(f"buy-stone: the stones and the Ring before and after {seen[:1]} {seen[-1:]}, not 0 then 1")
    if not wallet or wallet[0] != (coins - ring_price, money) or wallet[-1] != (left, money):
        problems.append(f"buy-stone: the wallet {wallet[:1]} then {wallet[-1:]}, not {coins - ring_price} coins less "
                        f"{stone_price} (Venusaurite, item {venusaurite}), the money unchanged")
    _, lines = step("bought-load", os.path.join(stone_dir, "test.sav"), dict(PKMN_TEST_BATTLE="field:60"))
    loaded = [tuple(numbers(line)) for line in lines if line.startswith("mega field: ")]
    wallet = [tuple(numbers(line)) for line in lines if line.startswith("mega wallet: ")]
    if not loaded or loaded[-1][3:] != (1, 1) or not wallet or wallet[-1] != (left, money):
        problems.append(f"bought-load: the saved state is {loaded[-1:]} {wallet[-1:]}, not the stone, the Ring and "
                        f"{left} coins")
    return dict(items="give, battle, battle then take then soft reset, take, load, buy the Ring, buy a stone, load",
                steps=steps, problems=problems, passed=not problems)


def run_parade(args, facts, holders):
    """Every Mega form drawn in turn at a wild battle's action menu, a picture of each."""
    rundir = os.path.join(args.out, f"parade{'-shiny' if args.shiny else ''}")
    env = dict(PKMN_TEST_BATTLE=f"wild:{facts['SPECIES_POOCHYENA']}:30", PKMN_TEST_AUTOPILOT="1",
               PKMN_TEST_BATTLE_DELAY="100", PKMN_TEST_FREEZE_FIELD="1", PKMN_TEST_PARADE_MEGAS="1",
               PKMN_TEST_PARADE_FRAMES="40", PKMN_TEST_PARADE_FLAGS="s" if args.shiny else "",
               PKMN_TEST_MEGA_SHOT_STEM=os.path.join(rundir, "form"))
    started = time.monotonic()
    process, output = start(args, rundir, env)
    code = finish(process, output, args.timeout + 2 * len(holders))
    lines = driver_lines(os.path.join(rundir, "test.log"))
    shown = [numbers(line) for line in lines if line.startswith("parade species ")]
    pictures = {f for f in os.listdir(rundir) if f.startswith("form-") and f.endswith(".png")}
    problems = []
    if [index for _, index in shown] != list(range(len(holders))):
        problems.append(f"the parade showed {len(shown)} forms, not the {len(holders)} in order")
    if [species for species, _ in shown] != [h["mega"] for h in holders][:len(shown)]:
        problems.append("the parade's species are not the holders' Mega forms")
    missing = [i for i in range(len(holders)) if f"form-{i:02d}.png" not in pictures]
    if missing:
        problems.append(f"no picture of Mega forms {missing}")
    if "parade done" not in lines:
        problems.append("the parade did not finish")
    return dict(parade="shiny" if args.shiny else "normal", exit=code, seconds=round(time.monotonic() - started, 1),
                forms=len(shown), pictures=len(pictures), problems=problems, passed=not problems)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", required=True, help="a test build (Makefile_pc BFX_TEST=1, with or without DIORAMA=1)")
    parser.add_argument("--save", help="a save to start from (copied; never written)")
    parser.add_argument("--make-save", metavar="PATH", help="first make the save there (the short introduction)")
    parser.add_argument("--out", required=True)
    parser.add_argument("--display", default=os.environ.get("DISPLAY"), help="an X display (Linux)")
    parser.add_argument("--fight", type=int, default=600, help="seconds the driver may fight each battle")
    parser.add_argument("--timeout", type=int, default=900, help="seconds per case (a sweep: plus 90 per holder)")
    parser.add_argument("--only", action="append", help="run just these cases")
    parser.add_argument("--no-cases", action="store_true", help="only the sweep")
    parser.add_argument("--sweep", action="store_true", help="every Mega Stone holder in a battle of its own")
    parser.add_argument("--sweep-range", help="first:last holders of the sweep (default all)")
    parser.add_argument("--jobs", type=int, default=2, help="games at once (the sweep)")
    parser.add_argument("--shiny", action="store_true", help="the sweep's holders and the parade's forms shiny")
    parser.add_argument("--parade", action="store_true", help="every Mega form drawn, a picture of each")
    parser.add_argument("--items", action="store_true", help="a Mega Stone given, kept, used and taken (menus, saves)")
    parser.add_argument("--series", action="store_true", help="a series of 16 pictures of each case's battle (3D)")
    args = parser.parse_args()
    args.exe, args.out = os.path.abspath(args.exe), os.path.abspath(args.out)
    os.makedirs(args.out, exist_ok=True)
    if args.make_save:
        args.save = make_save(args, os.path.abspath(args.make_save))
    if not args.save:
        parser.error("--save or --make-save")
    args.save = os.path.abspath(args.save)
    facts, holders = game_facts(args)
    print(json.dumps(dict(facts=len(facts), mega_stone_holders=len(holders))), flush=True)
    report = dict(exe=args.exe, shiny=args.shiny, facts=facts, holders=holders, cases=[], sweep=[], parade=[], items=[])
    if not args.no_cases:
        for case in CASES:
            if args.only and case[0] not in args.only:
                continue
            result = run_case(args, facts, case)
            report["cases"].append(result)
            print(json.dumps(dict(case=result["case"], passed=result["passed"], seconds=result["seconds"],
                                  problems=result["problems"])), flush=True)
    if args.sweep:
        first, last = (0, len(holders) - 1) if not args.sweep_range else map(int, args.sweep_range.split(":"))
        span = list(range(first, min(last, len(holders) - 1) + 1))
        size = -(-len(span) // max(1, args.jobs))
        ranges = [(job, chunk[0], chunk[-1]) for job, chunk in
                  enumerate(span[i:i + size] for i in range(0, len(span), size))]
        with ThreadPoolExecutor(max_workers=len(ranges)) as pool:
            jobs = list(pool.map(lambda r: run_sweep_job(args, facts, holders, *r), ranges))
        for job in jobs:
            report["sweep"].append(job)
            for result in job["results"]:
                if not result["passed"]:
                    print(json.dumps(dict(holder=result["holder"], problems=result["problems"])), flush=True)
            print(json.dumps(dict(sweep_job=job["job"], holders=f"{job['first']}..{job['last']}", exit=job["exit"],
                                  seconds=job["seconds"], passed=sum(r["passed"] for r in job["results"]),
                                  of=len(job["results"]))), flush=True)
    if args.items:
        result = run_items(args, facts)
        report["items"].append(result)
        print(json.dumps(dict(items=result["items"], passed=result["passed"], problems=result["problems"])), flush=True)
    if args.parade:
        result = run_parade(args, facts, holders)
        report["parade"].append(result)
        print(json.dumps({k: v for k, v in result.items()}), flush=True)
    with open(os.path.join(args.out, f"mega_check{'-shiny' if args.shiny else ''}.json"), "w") as out:
        json.dump(report, out, indent=2)
    everything = (report["cases"] + [r for job in report["sweep"] for r in job["results"]] + report["parade"]
                  + report["items"])
    print(json.dumps(dict(passed=sum(r["passed"] for r in everything), of=len(everything))), flush=True)
    sys.exit(0 if everything and all(r["passed"] for r in everything) else 1)


if __name__ == "__main__":
    main()
