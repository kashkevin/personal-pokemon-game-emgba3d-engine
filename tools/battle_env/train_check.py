#!/usr/bin/env python3
"""Battle Tower: Legends' items in real game processes (test builds; Linux and Windows alike): a Bottle Cap, a Gold
Bottle Cap and an Ability Patch used through the game's own Bag and party menus, refused where they would do nothing,
saved and loaded. Everything is read from the game's own log (PKMN_TEST_TRAIN_LOG: the lead's IVs, Hyper Training
bits and Ability slot, the Bag's caps and patches); the field keys are the presses a person makes (frames at fast text,
as the pictures each step takes showed them).

  1. cap         a Bulbasaur (Overgrow) with every IV 10, three Bottle Caps, a Gold Bottle Cap and two Ability Patches:
                 BAG > Bottle Cap > USE > the lead > HP > YES: HP trained, one Bottle Cap used; SAVE
  2. cap-load    that save loaded: HP still trained, two Bottle Caps
  3. gold        from that save: Gold Bottle Cap on the lead: every stat trained, the Gold Bottle Cap used; SAVE
  4. gold-again  from that save: a Bottle Cap on the lead (nothing left to train): refused, nothing used
  5. full-ivs    a Bulbasaur with every IV 31: a Bottle Cap: refused, nothing used
  6. patch       from save 3: Ability Patch on the lead, YES: the Hidden Ability (slot 2, Chlorophyll), a patch used;
                 SAVE
  7. patch-back  from that save: the second patch: back to slot 0 (Overgrow), no patch left; SAVE
  8. patch-load  that save loaded: slot 0, every stat still trained

    tools/battle_env/train_check.py --exe ./pokeemerald-mp-bfxtest --save town.sav --out build/train-check \\
        [--display :99] [--make-save]

Exit 0 when every step gave what it expects; the report is OUT/train_check.json.
"""
import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mega_check import driver_lines, finish, make_save, numbers, start  # noqa: E402

SPECIES_BULBASAUR = 1
ITEM_BOTTLE_CAP, ITEM_GOLD_BOTTLE_CAP, ITEM_ABILITY_PATCH = 133, 134, 80
ABILITY_OVERGROW, ABILITY_CHLOROPHYLL = 65, 34
BAG = f"{ITEM_BOTTLE_CAP}:3,{ITEM_GOLD_BOTTLE_CAP}:1,{ITEM_ABILITY_PATCH}:2"

# The menus at fast text, game frames from the field keys' start (60 a second). The start menu of a player with a
# Pokemon and no Pokedex: POKeMON, BAG, ...; the Bag opens on its ITEMS pocket in the order the items came: Bottle Cap,
# Gold Bottle Cap, Ability Patch; an item's menu: USE, GIVE, TOSS, CANCEL; then "Use on which POKeMON?" with the lead
# first.
TO_BAG = ["30:START", "80:DOWN", "120:A"]                          # START > BAG
USE_FIRST = TO_BAG + ["260:A", "320:A", "420:A"]                   # the pocket's first item > USE > the lead
USE_SECOND = TO_BAG + ["260:DOWN", "300:A", "360:A", "460:A"]      # its second item > USE > the lead
USE_THIRD = TO_BAG + ["260:DOWN", "300:DOWN", "340:A", "400:A", "500:A"]
# Bottle Cap: the stat list (HP first) > A, "Hyper Train ...'s HP with the Bottle Cap?" YES, "... was Hyper Trained!",
# back in the Bag: B, then the start menu: B.
CAP_HP = USE_FIRST + ["540:A", "700:A", "860:A", "1000:B", "1080:B"]
# Gold Bottle Cap: straight to "Hyper Train every stat ...?" YES, the message, out.
GOLD = USE_SECOND + ["640:A", "800:A", "940:B", "1020:B"]
# A Bottle Cap with nothing to train: "It won't have any effect.", out.
CAP_REFUSED = USE_FIRST + ["560:A", "700:B", "780:B"]
# Ability Patch: "... Ability will change to ...? " YES, "... Ability changed ...!", out.
PATCH = USE_THIRD + ["640:A", "800:A", "940:B", "1020:B"]
PATCH_AFTER_GOLD = USE_SECOND + ["640:A", "800:A", "940:B", "1020:B"]   # the Gold Bottle Cap gone: the patch is second


def save_keys(start):
    """START > SAVE > YES (mega_check.py's save_keys), from the start menu's cursor where the Bag left it: BAG, two rows
    above SAVE."""
    return [f"{start + f}:{k}" for f, k in ((0, "START"), (50, "DOWN"), (80, "DOWN"), (160, "A"), (260, "A"), (380, "A"),
                                            (700, "B"))]


def train_lines(lines):
    """The states the "train field" lines logged from the field keys' start: the last one before them (a loaded save's
    state is logged as the field comes up), then each change; each (species, ivs[6], trained, ability, num, caps, gold,
    patches)."""
    begun = next((i for i, line in enumerate(lines) if line.startswith("field keys for ")), len(lines))
    states = []
    for i, line in enumerate(lines):
        if line.startswith("train field: "):
            n = numbers(line)
            state = dict(species=n[0], ivs=n[1:7], trained=n[7], ability=n[8], num=n[9], caps=n[10], gold=n[11],
                         patches=n[12])
            if i < begun:
                # only the last state before the keys, and one with a party (PKMN_TEST_PARTY's comes with the keys)
                states[:] = [state] if state["species"] else []
            else:
                states.append(state)
    return states


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", required=True)
    parser.add_argument("--save")
    parser.add_argument("--out", required=True)
    parser.add_argument("--display", default=os.environ.get("DISPLAY"))
    parser.add_argument("--make-save", action="store_true")
    args = parser.parse_args()
    args.exe = os.path.abspath(args.exe)
    os.makedirs(args.out, exist_ok=True)
    if args.make_save:
        args.save = make_save(args, os.path.join(args.out, "made.sav"))
    if not args.save:
        sys.exit("--save or --make-save")
    args.save = os.path.abspath(args.save)
    steps, problems = [], []

    def step(name, save, frames, keys, party=None, bag=None, shots=""):
        rundir = os.path.join(args.out, name)
        saved = args.save
        args.save = save
        env = dict(PKMN_TEST_BATTLE=f"field:{frames}", PKMN_TEST_TRAIN_LOG="1", PKMN_TEST_FREEZE_FIELD="1",
                   PKMN_TEST_AUTOPILOT="1", PKMN_TEST_BATTLE_DELAY="100", PKMN_TEST_FIELD_KEYS=",".join(keys),
                   PKMN_TEST_MEGA_SHOT_STEM=os.path.join(rundir, "step"))
        if party:
            env["PKMN_TEST_PARTY"] = party
        if bag:
            env["PKMN_TEST_BAG"] = bag
        if shots:
            env["PKMN_TEST_FIELD_SHOTS"] = shots
        try:
            process, output = start(args, rundir, env)
            code = finish(process, output, frames / 60 + 180)
        finally:
            args.save = saved
        seen = train_lines(driver_lines(os.path.join(rundir, "test.log")))
        steps.append(dict(step=name, exit=code, first=seen[0] if seen else None, last=seen[-1] if seen else None,
                          pictures=sorted(f for f in os.listdir(rundir) if f.endswith(".png"))))
        return rundir, seen

    def expect(name, seen, first=None, last=None):
        if not seen:
            problems.append(f"{name}: nothing logged")
            return
        for label, want, got in (("before the keys", first, seen[0]), ("after the keys", last, seen[-1])):
            if want and any(got[key] != value for key, value in want.items()):
                problems.append(f"{name}: {label} {got}, expected {want}")

    weak = f"{SPECIES_BULBASAUR}:30:w10"
    cap_dir, seen = step("cap", args.save, 2000, CAP_HP + save_keys(1200), party=weak, bag=BAG,
                         shots="140,280,380,480,600,760,900,1100")
    expect("cap", seen, dict(species=SPECIES_BULBASAUR, ivs=[10] * 6, trained=0, caps=3, gold=1, patches=2, num=0),
           dict(trained=1, caps=2, gold=1, patches=2))
    cap_save = os.path.join(cap_dir, "test.sav")
    _, seen = step("cap-load", cap_save, 60, [])
    expect("cap-load", seen, last=dict(species=SPECIES_BULBASAUR, trained=1, caps=2))
    gold_dir, seen = step("gold", cap_save, 1900, GOLD + save_keys(1100), shots="300,700,860,1000")
    expect("gold", seen, dict(trained=1, gold=1), dict(trained=0x3F, gold=0, caps=2))
    gold_save = os.path.join(gold_dir, "test.sav")
    _, seen = step("gold-again", gold_save, 1000, CAP_REFUSED, shots="600,700")
    expect("gold-again", seen, dict(trained=0x3F, caps=2), dict(trained=0x3F, caps=2))
    _, seen = step("full-ivs", args.save, 1000, CAP_REFUSED, party=f"{SPECIES_BULBASAUR}:30", bag=BAG, shots="600,700")
    expect("full-ivs", seen, dict(ivs=[31] * 6, trained=0, caps=3), dict(trained=0, caps=3))
    patch_dir, seen = step("patch", gold_save, 1900, PATCH_AFTER_GOLD + save_keys(1100), shots="600,760,900")
    expect("patch", seen, dict(num=0, ability=ABILITY_OVERGROW, patches=2),
           dict(num=2, ability=ABILITY_CHLOROPHYLL, patches=1))
    back_dir, seen = step("patch-back", os.path.join(patch_dir, "test.sav"), 1900, PATCH_AFTER_GOLD + save_keys(1100),
                          shots="600,760,900")
    expect("patch-back", seen, dict(num=2, patches=1), dict(num=0, ability=ABILITY_OVERGROW, patches=0))
    _, seen = step("patch-load", os.path.join(back_dir, "test.sav"), 60, [])
    expect("patch-load", seen, last=dict(num=0, ability=ABILITY_OVERGROW, trained=0x3F, caps=2, gold=0, patches=0))
    report = dict(steps=steps, problems=problems, passed=not problems)
    with open(os.path.join(args.out, "train_check.json"), "w") as out:
        json.dump(report, out, indent=1)
    print(json.dumps(dict(passed=not problems, problems=problems)))
    return 0 if not problems else 1


if __name__ == "__main__":
    sys.exit(main())
