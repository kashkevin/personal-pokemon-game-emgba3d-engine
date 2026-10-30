#!/usr/bin/env python3
"""The Rotom Catalog (the owner's request of 2026-10-30) in real game processes (test builds; Linux and Windows alike;
the 3D one, whose party and shop screens are the PC's own): bought at the Game Corner's RARE GOODS for 15,000 coins, used
from the Bag on the party's Rotom, the forms' list, the question naming the form and its type, YES / NO, the signature
moves, saved and loaded, and each form in a battle. Everything is read from the game's own log (PKMN_TEST_BREED_LOG: every
party Pokemon's species and moves; PKMN_TEST_MEGA_LOG: the coins and the money whenever they change; PKMN_TEST_BAG_LOG:
the catalogs in the Bag); the field keys are the presses a person makes (game frames at fast text, as the pictures each
step takes showed them). src/platform/native_selftest_rotom.c (--selftest-rotom-catalog) checks every form against every
other, the forms' data, the PC's storage and the price beside these.

  1. buy         below the prize counter's clerk with a Coin Case and 20,000 coins: RARE GOODS, EVOLUTION, BUY, the
                 Rotom Catalog's row: "You wanted Rotom Catalog? That'll be 15000 COINS." YES, "Here you go!"; A on it
                 again: "That item is sold out." (a Key Item, one each); out of the list; SAVE. 5,000 coins left, one
                 Rotom Catalog in the Bag
  2. buy-load    that save loaded: the same coins and catalog
  3. cycle       a party of Pikachu, Rotom and Rotom (Thunder Shock, Thunder Wave), the catalog in the Bag: BAG > KEY
                 ITEMS > Rotom Catalog > USE > the first Rotom > Heat Rotom > "Turn Rotom into Heat Rotom? Its type will
                 be Electric/Fire." YES > "Rotom transformed!" > "Rotom learned Overheat!"; then Wash, Frost, Fan, Mow and
                 Normal the same way, each form's signature move in place of the one before (Overheat, Hydro Pump,
                 Blizzard, Air Slash, Leaf Storm; back to Rotom, Thunder Shock, already known); SAVE. The second Rotom
                 never changes, the catalog stays in the Bag
  4. cycle-load  that save loaded: the first Rotom a Rotom with its two moves, the second unchanged
  5. second      the second Rotom (two rows down) > Frost Rotom > YES: it becomes a Frost Rotom knowing Blizzard; the
                 first stays a Rotom
  6. paths       the first Rotom > Normal Rotom: "Rotom is already Normal Rotom!", back to the list; Wash Rotom > NO:
                 back to the list; CANCEL; Pikachu: "It won't have any effect."; nothing changes
  7. four-moves  a Rotom knowing four moves > Heat Rotom > YES: it becomes a Heat Rotom and asks to make room for
                 Overheat (the party menu's move-replacement rules); NO, "Stop trying to teach Overheat?" YES: a Heat
                 Rotom with its four moves
  8. battle-*    each form leading against a wild one of its own form: the 3D battle's picture at the action menu (its
                 back, the foe's front), A through the battle (its first move, the signature move); after the battle the
                 form is still that form

    tools/battle_env/rotom_check.py --exe ./pokeemerald-mp-diorama-bfxtest --save town.sav --out build/rotom-check \\
        [--display :99] [--steps buy,cycle,...]

Exit 0 when every step gave what it expects; the report is OUT/rotom_check.json.
"""
import argparse
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mega_check import driver_lines, finish, numbers, save_keys, start  # noqa: E402

ENGINE = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SPECIES_PIKACHU, SPECIES_ROTOM = 25, 479
FORMS = [("normal", 479, 84), ("heat", 1064, 315), ("wash", 1065, 56), ("frost", 1066, 59), ("fan", 1067, 403),
         ("mow", 1068, 437)]                     # name, species, signature move (Thunder Shock for Rotom itself)
MOVE_THUNDER_SHOCK, MOVE_THUNDER_WAVE, MOVE_CONFUSE_RAY, MOVE_SHOCK_WAVE = 84, 86, 109, 351
PRIZE_COUNTER = "10:3:0:12:4"     # PKMN_TEST_WARP: the Mauville Game Corner, below the major prize clerk (12,2)
COINS = 20_000
LIST_LABEL = "MauvilleCity_GameCorner_Pokemart_RareGoods"
ROTOM = f"{SPECIES_ROTOM}:30:V{MOVE_THUNDER_SHOCK}.{MOVE_THUNDER_WAVE}.0.0"
PARTY = f"{SPECIES_PIKACHU}:20,{ROTOM},{ROTOM}"

# The start menu (POKeMON, BAG, ...), BAG; the Bag opens on ITEMS: four pockets right is KEY ITEMS, the catalog first
OPEN_BAG = ["30:START", "80:DOWN", "120:A", "300:RIGHT", "340:RIGHT", "380:RIGHT", "420:RIGHT"]
FIRST_USE = 500


def item_id(name):
    text = open(os.path.join(ENGINE, "include/constants/items.h"), encoding="utf-8").read()
    return int(re.search(r"^\s*" + name + r"\s*=\s*(\d+)\s*,", text, re.M).group(1))


def goods(suffix):
    text = open(os.path.join(ENGINE, "data/scripts/mp_rare_goods_shop.inc"), encoding="utf-8").read()
    body = re.search(LIST_LABEL + suffix + r"::(.*?)pokemartlistend", text, re.S).group(1)
    return re.findall(r"\.2byte (ITEM_\w+)", body)


def downs(start, count, gap=10):
    return [f"{start + gap * i}:DOWN" for i in range(count)]


def use(t, slot_moves, row, answer="A", learn_presses=2):
    """One use from the Bag (the cursor on the catalog) at frame t: its menu, USE, the party's cursor moved by
    slot_moves (+ down, - up: it starts where the last use left it), the slot, `row` rows down the forms' list, the
    form; then the answer to the question (A: YES, B: NO) and, after YES, the presses for "... transformed!" and the
    move's lines. Returns the keys and the frame the next use may start at."""
    keys = [f"{t}:A", f"{t + 100}:A"]
    nav = abs(slot_moves)
    keys += [f"{t + 200 + 40 * i}:{'DOWN' if slot_moves > 0 else 'UP'}" for i in range(nav)]
    at = t + 280 + 40 * nav
    keys += [f"{at}:A"] + downs(at + 100, row, 40)
    at += 180 + 40 * row
    keys += [f"{at}:A", f"{at + 140}:{answer}"]
    if answer == "A":
        keys += [f"{at + 440 + 200 * i}:A" for i in range(learn_presses)]
        return keys, at + 440 + 200 * learn_presses + 140
    return keys, at + 300


def close_and_save(t):
    """The Bag closed (B: the start menu), the start menu closed (B), then START > SAVE > YES with the start menu's
    cursor where the Bag left it (BAG, two rows above SAVE: evolve_check.py's bag_save_keys), YES again over the saved
    file; the closing message goes by itself; a B for safety."""
    keys = [f"{t}:B", f"{t + 120}:B"]
    keys += [f"{t + 240 + f}:{k}" for f, k in ((0, "START"), (50, "DOWN"), (80, "DOWN"), (160, "A"), (260, "A"),
                                               (380, "A"), (700, "B"))]
    return keys, t + 240 + 900


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
    parser.add_argument("--steps", default="buy,cycle,second,paths,four-moves,battle",
                        help="which steps (buy and cycle are followed by their loads)")
    args = parser.parse_args()
    args.exe, args.save = os.path.abspath(args.exe), os.path.abspath(args.save)
    args.out = os.path.abspath(args.out)
    os.makedirs(args.out, exist_ok=True)
    wanted = set(args.steps.split(","))
    catalog, coin_case = item_id("ITEM_ROTOM_CATALOG"), item_id("ITEM_COIN_CASE")
    steps, problems = [], []

    def step(name, save, frames, keys, extra=None, shots=""):
        rundir = os.path.join(args.out, name)
        saved, args.save = args.save, save
        env = dict(PKMN_TEST_BATTLE=f"field:{frames}", PKMN_TEST_BREED_LOG="1", PKMN_TEST_MEGA_LOG="1",
                   PKMN_TEST_BAG_LOG=str(catalog), PKMN_TEST_FREEZE_FIELD="1", PKMN_TEST_AUTOPILOT="1",
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
        record = dict(step=name, exit=code, wallet=wallet, catalogs=bag,
                      party={str(k): [[s, list(m)] for s, m in v] for k, v in sorted(party.items())},
                      battles=[line for line in lines if line.startswith("battle over")],
                      notes=[line for line in lines if line.startswith(("cannot", "assert"))],
                      pictures=sorted(f for f in os.listdir(rundir) if f.endswith(".png")))
        steps.append(record)
        if code != 0:
            problems.append(f"{name}: the game ended with {code}")
        problems.extend(f"{name}: {line}" for line in record["notes"])
        return rundir, wallet, bag, party, record

    def expect(name, got, want):
        if got != want:
            problems.append(f"{name}: {got}, expected {want}")

    def last(party, slot):
        return party.get(slot, [None])[-1]

    forms_bag = dict(PKMN_TEST_PARTY=PARTY, PKMN_TEST_BAG=f"{catalog}:1", PKMN_TEST_BAG_RESET="1")

    # 1-2. Bought at RARE GOODS > EVOLUTION (the fourth kind), sold out once owned; saved and loaded
    if "buy" in wanted:
        row = goods("Evolution").index("ITEM_ROTOM_CATALOG")
        base = dict(PKMN_TEST_PARTY=f"{SPECIES_PIKACHU}:20", PKMN_TEST_BAG=f"{coin_case}:1", PKMN_TEST_COINS=str(COINS),
                    PKMN_TEST_WARP=PRIZE_COUNTER)
        kinds = ["30:UP", "80:A", "200:DOWN", "260:A"] + downs(380, 3, 40) + [f"{380 + 40 * 3 + 60}:A", "600:A"]
        keys = (kinds + downs(760, row)
                + [f"{f + 10 * row}:A" for f in (860, 980, 1100)]                # "You wanted ...?" YES, "Here you go!"
                + [f"{f + 10 * row}:A" for f in (1220, 1340)]                    # again: "That item is sold out."
                + [f"{f + 10 * row}:B" for f in (1440, 1560, 1680)] + [f"{1800 + 10 * row}:A"])
        save_at = 2100 + 10 * row
        rundir, wallet, bag, _, _ = step("buy", args.save, save_at + 900, keys + save_keys(save_at), base,
                                         shots=",".join(str(f + 10 * row) for f in (700, 760, 850, 1000, 1160, 1280, 1400)))
        money = wallet[0][1] if wallet else None
        expect("buy (the wallet: coins, money)", [wallet[:1], wallet[-1:]], [[(COINS, money)], [(COINS - 15000, money)]])
        expect("buy (catalogs in the Bag)", bag[-1:], [1])
        _, wallet, bag, _, _ = step("buy-load", os.path.join(rundir, "test.sav"), 60, [])
        expect("buy-load (the coins)", [w[0] for w in wallet[-1:]], [COINS - 15000])
        expect("buy-load (catalogs in the Bag)", bag[-1:], [1])

    # 3-4. The first Rotom through every form and back, each with its move; saved and loaded
    if "cycle" in wanted:
        keys, t = list(OPEN_BAG), FIRST_USE
        for i, row in enumerate((1, 2, 3, 4, 5, 0)):
            # back to Rotom: "Rotom transformed!" alone (Thunder Shock is known already: no line about it)
            more, t = use(t, 1 if i == 0 else 0, row, learn_presses=1 if row == 0 else 2)
            keys += more
        more, save_end = close_and_save(t)
        rundir, _, bag, party, _ = step("cycle", args.save, save_end, keys + more, forms_bag,
                                        shots=",".join(str(f) for f in range(FIRST_USE, t, 120)))
        moves = [MOVE_THUNDER_SHOCK, MOVE_THUNDER_WAVE]
        want, before = [(SPECIES_ROTOM, tuple(moves + [0, 0]))], 0
        for name, species, move in FORMS[1:]:
            want.append((species, tuple(moves + [before, 0])))      # the form first, then its move in the last one's place
            want.append((species, tuple(moves + [move, 0])))
            before = move
        want.append((SPECIES_ROTOM, tuple(moves + [before, 0])))
        want.append((SPECIES_ROTOM, tuple(moves + [0, 0])))         # Leaf Storm gone; Thunder Shock already known
        expect("cycle (the first Rotom's forms and moves, in order)", party.get(1), want)
        expect("cycle (the second Rotom)", party.get(2), [(SPECIES_ROTOM, tuple(moves + [0, 0]))])
        expect("cycle (catalogs in the Bag: never used up)", bag[-1:] if bag else [1], [1])
        _, _, bag, party, _ = step("cycle-load", os.path.join(rundir, "test.sav"), 60, [])
        expect("cycle-load (the first Rotom)", last(party, 1), (SPECIES_ROTOM, tuple(moves + [0, 0])))
        expect("cycle-load (the second Rotom)", last(party, 2), (SPECIES_ROTOM, tuple(moves + [0, 0])))
        expect("cycle-load (catalogs in the Bag)", bag[-1:], [1])

    # 5. The second Rotom, two rows down from Pikachu
    if "second" in wanted:
        more, t = use(FIRST_USE, 2, 3)
        _, _, _, party, _ = step("second", args.save, t + 200, OPEN_BAG + more, forms_bag,
                                 shots=",".join(str(f) for f in range(FIRST_USE, t, 100)))
        expect("second (the second Rotom)", last(party, 2), (FORMS[3][1], (MOVE_THUNDER_SHOCK, MOVE_THUNDER_WAVE, FORMS[3][2], 0)))
        expect("second (the first Rotom)", last(party, 1), (SPECIES_ROTOM, (MOVE_THUNDER_SHOCK, MOVE_THUNDER_WAVE, 0, 0)))

    # 6. Already that form, NO, CANCEL, then Pikachu
    if "paths" in wanted:
        t = FIRST_USE
        keys = list(OPEN_BAG) + [f"{t}:A", f"{t + 100}:A", f"{t + 200}:DOWN", f"{t + 280}:A"]   # the first Rotom
        keys += [f"{t + 460}:A", f"{t + 640}:B"]                     # Normal Rotom: "already ...", back to the list
        keys += downs(t + 760, 2, 40) + [f"{t + 900}:A", f"{t + 1040}:B"]   # Wash Rotom: the question, NO
        keys += [f"{t + 1200}:B"]                                    # CANCEL: "Choose a POKeMON."
        keys += [f"{t + 1320}:UP", f"{t + 1400}:A", f"{t + 1600}:A"]   # Pikachu: "It won't have any effect."
        end = t + 1900
        _, _, bag, party, _ = step("paths", args.save, end, keys, forms_bag,
                                   shots=",".join(str(f) for f in range(t, end, 60)))
        expect("paths (the first Rotom)", party.get(1), [(SPECIES_ROTOM, (MOVE_THUNDER_SHOCK, MOVE_THUNDER_WAVE, 0, 0))])
        expect("paths (Pikachu)", [s for s, _ in party.get(0, [])], [SPECIES_PIKACHU])
        expect("paths (the second Rotom)", party.get(2), [(SPECIES_ROTOM, (MOVE_THUNDER_SHOCK, MOVE_THUNDER_WAVE, 0, 0))])

    # 7. Four moves: the form changes; Overheat is not learnt when the player keeps the four
    if "four-moves" in wanted:
        four = (MOVE_THUNDER_SHOCK, MOVE_THUNDER_WAVE, MOVE_CONFUSE_RAY, MOVE_SHOCK_WAVE)
        party_four = f"{SPECIES_PIKACHU}:20,{SPECIES_ROTOM}:30:V{'.'.join(str(m) for m in four)}"
        t = FIRST_USE
        keys = list(OPEN_BAG) + [f"{t}:A", f"{t + 100}:A", f"{t + 200}:DOWN", f"{t + 280}:A"] + downs(t + 380, 1, 40)
        keys += [f"{t + 460}:A", f"{t + 600}:A"]                    # Heat Rotom, YES
        keys += [f"{t + 900}:A"]                                    # "Rotom transformed!"
        keys += [f"{t + f}:A" for f in (1100, 1250)]                # "... wants to learn Overheat ..." lines
        keys += [f"{t + 1400}:B", f"{t + 1550}:A", f"{t + 1700}:A", f"{t + 1850}:A"]   # NO; stop? YES; "did not learn"
        end = t + 2200
        _, _, _, party, _ = step("four-moves", args.save, end, keys,
                                 dict(forms_bag, PKMN_TEST_PARTY=party_four),
                                 shots=",".join(str(f) for f in range(t + 500, end, 60)))
        expect("four-moves (the Rotom)", last(party, 1), (FORMS[1][1], four))

    # 8. Each form in a battle against its own form: the picture, the fight, the form afterwards
    if "battle" in wanted:
        for name, species, move in FORMS:
            party = f"{species}:50:V{move}.{MOVE_THUNDER_WAVE}.0.0" if move != MOVE_THUNDER_SHOCK else \
                    f"{species}:50:V{MOVE_THUNDER_SHOCK}.{MOVE_THUNDER_WAVE}.0.0"
            rundir = os.path.join(args.out, f"battle-{name}")
            frames = 600
            # a level-2 foe: the first move (A through the battle) wins it even resisted and missing (Blizzard's 70%)
            _, _, _, got, record = step(f"battle-{name}", args.save, frames, save_keys(200), dict(
                PKMN_TEST_BATTLE=f"wild:{species}:2", PKMN_TEST_PARTY=party, PKMN_TEST_LEAD_ONLY="1",
                PKMN_TEST_CAPTURE=os.path.join(rundir, "battle"), PKMN_TEST_CAPTURE_DELAY="90",
                PKMN_TEST_FIGHT="150", PKMN_TEST_FIELD_AFTER=str(frames)))
            if not any("outcome 1 " in line for line in record["battles"]):
                problems.append(f"battle-{name}: the battle did not end in a win: {record['battles']}")
            # the lead's last state in the whole log (it changes no more after the battle: no line then)
            lead = party_states(driver_lines(os.path.join(rundir, "test.log"))).get(0, [])
            expect(f"battle-{name} (the lead after the battle)", [s for s, _ in lead][-1:], [species])
            if not any(p.startswith("battle") for p in record["pictures"]):
                problems.append(f"battle-{name}: no picture of the battle")

    report = dict(steps=steps, problems=problems, passed=not problems)
    with open(os.path.join(args.out, "rotom_check.json"), "w") as out:
        json.dump(report, out, indent=1)
    print(json.dumps(dict(passed=not problems, problems=problems)))
    return 0 if not problems else 1


if __name__ == "__main__":
    sys.exit(main())
