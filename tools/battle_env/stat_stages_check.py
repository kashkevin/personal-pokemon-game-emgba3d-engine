#!/usr/bin/env python3
"""Stat stages as Pokemon Showdown shows them, kept until a Pokemon faints or the battle ends (the owner's request of
2026-10-24: "a way for the game to show you what everyone's debuffs and buffs are like in Pokemon Showdown, with that
being said buff and debuffs should carry out til the pokemon dies or match/battle ends"), in real game processes (test
builds; Linux and Windows alike; engine platform/battle_stat_stages.c).

Everything is read from the game's own log (the battle-environment test driver, include/diorama/battle_env_test.h, TEST
BUILDS ONLY): PKMN_TEST_STAGE_LOG (each battler's stages as its tags show them and as the engine has them, its party
member, each party member's kept stages), the player's choices are PKMN_TEST_TURNS, the pictures PKMN_TEST_STAGE_SHOTS
(one at each action menu where the tags changed: the Classic 2D build's GBA frame, the 3D build's window with its battle
HUD). The test Pokemon are level 100 with only the moves named (PKMN_TEST_PARTY / PKMN_TEST_FOE flag V).

Cases:
  single   a wild battle: Lucario (Swords Dance, Scary Face, Body Slam) and Arcanine (Swords Dance, Tail Whip, Body
           Slam) against a Zigzagoon that knows Tail Whip; Swords Dance, Scary Face, a switch to Arcanine, its Swords
           Dance, back to Lucario (its stages again), Body Slam; then a second battle (PKMN_TEST_NEXT_BATTLE) where
           Arcanine comes in with none
  doubles  a wild double battle: Lucario (Swords Dance, Leer) and Arcanine (Agility, Growl) against a Zigzagoon (Growl)
           and a Poochyena (Tail Whip): all four battlers tagged at once
  baton    Lucario's Swords Dance, its Baton Pass to Arcanine (the stages go with it), Arcanine's Agility, back to Lucario
           (none: it passed them), back to Arcanine (its own kept)
  roar     a trainer battle (Youngster Allen's Zigzagoon and Taillow, their own moves): Scary Face, then the player's
           Whirlwind drags them out and in again: each has its Speed -2 again when it is dragged or sent back in
  faint    a trainer battle: Lucario's Swords Dance, then Memento (it faints), Arcanine's Revival Blessing revives it,
           and Lucario comes back in with none

Checked in every case, from the log: each battle starts with every battler at 0 and nothing kept; what the tags show is
the engine's own stages and reads as "ATK+2 SPE-1" in Showdown's order; a Pokemon that leaves alive keeps exactly what it
showed, one that faints keeps nothing, one that passes with Baton Pass keeps nothing and the one coming in shows the
passer's; a Pokemon coming back in shows exactly what it kept (none if it fainted since, or in a later battle). And each
case's own point (above).

    tools/battle_env/stat_stages_check.py --exe ./pokeemerald-mp-bfxtest --save town.sav --out build/stat-stages-check
    tools/battle_env/stat_stages_check.py --exe ./pokeemerald-mp-diorama-bfxtest --save town.sav --out build/ss-3d \\
        --cases single,doubles

Exit 0 when every check holds; the report is OUT/stat_stages_check.json, the pictures OUT/<case>/ and OUT/<case>.png.
"""
import argparse
import glob
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mega_check import driver_lines, finish, start  # noqa: E402

SPECIES = dict(LUCARIO=448, ARCANINE=59, ZIGZAGOON=263, POOCHYENA=261)
MOVE = dict(SWORDS_DANCE=14, SCARY_FACE=184, GROWL=45, TAIL_WHIP=39, BODY_SLAM=34, BATON_PASS=226, ROAR=46,
            WHIRLWIND=18, LEER=43, AGILITY=97, SPLASH=150, MEMENTO=262, REVIVAL_BLESSING=791)
TRAINER_ALLEN = 333            # Youngster Allen: two Pokemon (include/constants/opponents.h)
STATS = ("ATK", "DEF", "SPE", "SPA", "SPD", "ACC", "EVA")  # the log's order: STAT_ATK .. STAT_EVASION
SHOWDOWN = ("ATK", "DEF", "SPA", "SPD", "SPE", "ACC", "EVA")


def moves(*names):
    return "V" + ".".join(str(MOVE[n]) for n in names)


def mon(species, *names):
    return f"{SPECIES[species]}:100:{moves(*names)}"


CASES = {
    "single": dict(
        PKMN_TEST_BATTLE=f"wild:{SPECIES['ZIGZAGOON']}:30", PKMN_TEST_FOE=moves("TAIL_WHIP"),
        PKMN_TEST_PARTY=",".join((mon("LUCARIO", "SWORDS_DANCE", "SCARY_FACE", "BODY_SLAM"),
                                  mon("ARCANINE", "SWORDS_DANCE", "TAIL_WHIP", "BODY_SLAM"))),
        PKMN_TEST_TURNS="m0,m1,s1,m0,s0,m2|s1,m2", PKMN_TEST_NEXT_BATTLE=f"wild:{SPECIES['POOCHYENA']}:30"),
    "doubles": dict(
        PKMN_TEST_BATTLE=f"wild2:{SPECIES['ZIGZAGOON']}:30:{SPECIES['POOCHYENA']}:30",
        PKMN_TEST_FOE=moves("GROWL") + "," + moves("TAIL_WHIP"),
        PKMN_TEST_PARTY=",".join((mon("LUCARIO", "SWORDS_DANCE", "LEER", "BODY_SLAM"),
                                  mon("ARCANINE", "AGILITY", "GROWL", "BODY_SLAM"))),
        PKMN_TEST_TURNS="m0,m0,m1,m1,m2,m2"),
    "baton": dict(
        PKMN_TEST_BATTLE=f"wild:{SPECIES['ZIGZAGOON']}:30", PKMN_TEST_FOE=moves("SPLASH"),
        PKMN_TEST_PARTY=",".join((mon("LUCARIO", "SWORDS_DANCE", "BATON_PASS", "BODY_SLAM"),
                                  mon("ARCANINE", "AGILITY", "TAIL_WHIP", "BODY_SLAM"))),
        PKMN_TEST_TURNS="m0,m1,p1,m0,s0,s1,m2"),
    "roar": dict(
        PKMN_TEST_BATTLE=f"trainer:{TRAINER_ALLEN}",
        PKMN_TEST_PARTY=",".join((mon("LUCARIO", "SWORDS_DANCE", "WHIRLWIND", "SCARY_FACE", "BODY_SLAM"),
                                  mon("ARCANINE", "AGILITY", "WHIRLWIND", "SCARY_FACE", "BODY_SLAM"))),
        PKMN_TEST_TURNS="m0,m2,m1,m2,m1,m1,m3"),
    "faint": dict(
        PKMN_TEST_BATTLE=f"trainer:{TRAINER_ALLEN}",
        PKMN_TEST_PARTY=",".join((mon("LUCARIO", "SWORDS_DANCE", "MEMENTO", "BODY_SLAM"),
                                  mon("ARCANINE", "REVIVAL_BLESSING", "AGILITY", "BODY_SLAM"))),
        PKMN_TEST_TURNS="m0,m1,p1,m0,p0,s0,m2"),
}

BATTLER = re.compile(r"stages battler (\d+) trainer/slot/species/hp/shown/engine (\d+) (\d+) (\d+) (\d+) ([-\d,]+) "
                     r"([-\d,]+) tags \"([^\"]*)\"")
KEPT = re.compile(r"stages kept trainer (\d+) slot (\d+): ([-\d,]+)")
START = re.compile(r"stages battle (\d+) start, type (0x[0-9a-f]+), (\d+) battlers")
END = re.compile(r"stages battle (\d+) end, outcome (-?\d+)")
TURN = re.compile(r"turns: battler (\d+) chooses (\w)(\d+)")
SHOT = re.compile(r"stage shot (\d+): (.*) \(")


def values(text):
    return tuple(int(v) for v in text.split(","))


def tags_of(shown):
    """The tags the screens must print for these stages (Showdown's order)."""
    by = dict(zip(STATS, shown))
    return " ".join(f"{name}{by[name]:+d}" for name in SHOWDOWN if by[name] != 0)


def battles_of(lines):
    """The log as battles: each a list of events (kind, fields...) in order."""
    battles, current = [], None
    for line in lines:
        if m := START.search(line):
            current = dict(number=int(m.group(1)), type=int(m.group(2), 16), count=int(m.group(3)), events=[],
                           outcome=None)
            battles.append(current)
        elif current is None:
            continue
        elif m := END.search(line):
            current["outcome"] = int(m.group(2))
        elif m := BATTLER.search(line):
            b, t, slot, species, hp = (int(m.group(i)) for i in range(1, 6))
            current["events"].append(("battler", b, t, slot, species, hp, values(m.group(6)), values(m.group(7)),
                                      m.group(8)))
        elif m := KEPT.search(line):
            current["events"].append(("kept", int(m.group(1)), int(m.group(2)), values(m.group(3))))
        elif m := TURN.search(line):
            current["events"].append(("turn", int(m.group(1)), m.group(2), int(m.group(3))))
        elif m := SHOT.search(line):
            current["events"].append(("shot", int(m.group(1)), m.group(2)))
    return battles


def arrivals(battle):
    """Every Pokemon that came onto the field in a battle, in order: (battler, trainer, slot, species, first stages,
    departing (trainer, slot, species, last stages, hp) or None at the start, the kept table before it, the kept value
    the departing one got)."""
    occupant, last, kept, out = {}, {}, {}, []
    pending = {}
    for event in battle["events"]:
        if event[0] == "kept":
            _, t, slot, stages = event
            kept[(t, slot)] = stages
            for item in pending.values():
                if item["departing"] is not None and item["departing"][:2] == (t, slot) and item["departed_kept"] is None:
                    item["departed_kept"] = stages
            continue
        if event[0] != "battler":
            continue
        _, b, t, slot, species, hp, shown, engine, text = event
        if species == 0:
            continue
        if occupant.get(b) is None:
            occupant[b] = (t, slot, species)
            last[b] = (shown, hp)
            out.append(dict(battler=b, who=(t, slot, species), first=shown, departing=None, kept_before=dict(kept),
                            departed_kept=None))
            continue
        if species != occupant[b][2]:
            prev = occupant[b]
            item = dict(battler=b, who=(t, slot, species), first=shown,
                        departing=(prev[0], prev[1], prev[2], last[b][0], last[b][1]), kept_before=dict(kept),
                        departed_kept=kept.get((prev[0], prev[1])))
            out.append(item)
            pending[b] = item
            occupant[b] = (t, slot, species)
        last[b] = (shown, hp)
    return out


def check_battle(battle, number, link=False):
    """The generic checks of one battle: (name, ok, detail) each."""
    checks = []
    firsts = {}
    for event in battle["events"]:
        if event[0] == "battler" and event[4] != 0 and event[1] not in firsts:
            firsts[event[1]] = event[6]
    checks.append((f"battle {number}: every battler starts at stage 0",
                   bool(firsts) and all(v == (0,) * 7 for v in firsts.values()), firsts))
    mismatched = [e for e in battle["events"] if e[0] == "battler" and e[4] != 0 and not link and e[6] != e[7]]
    checks.append((f"battle {number}: the tags show the engine's own stages", not mismatched, mismatched[:3]))
    wrong_text = [e for e in battle["events"] if e[0] == "battler" and e[8] != tags_of(e[6])]
    checks.append((f"battle {number}: the tags read as Showdown's (\"ATK+2 SPE-1\", its order)", not wrong_text,
                   wrong_text[:3]))
    problems = []
    for a in arrivals(battle):
        d = a["departing"]
        if d is None:
            continue
        dt, dslot, dspecies, dlast, dhp = d
        k = a["departed_kept"] if a["departed_kept"] is not None else a["kept_before"].get((dt, dslot), (0,) * 7)
        passed = dhp != 0 and any(dlast) and not any(k) and a["first"] == dlast
        if dhp == 0 and any(k):
            problems.append(f"species {dspecies} fainted but kept {k}")
        elif dhp != 0 and not passed and k != dlast:
            problems.append(f"species {dspecies} left with {dlast} but kept {k}")
        t, slot, species = a["who"]
        before = a["kept_before"].get((t, slot), (0,) * 7)
        if not passed and a["first"] != before:
            problems.append(f"species {species} came in with {a['first']}, kept {before}")
        a["passed"] = passed
    checks.append((f"battle {number}: a Pokemon keeps its stages off the field and has them again (none once it "
                   "fainted; Baton Pass gives them on)", not problems, problems))
    return checks


def first_on_arrival(battle, species, nth=1):
    """The stages a species showed the nth time it came onto the field in a battle (None if it did not)."""
    seen = [a for a in arrivals(battle) if a["who"][2] == SPECIES[species]]
    return seen[nth - 1]["first"] if len(seen) >= nth else None


def case_checks(name, battles):
    """Each case's own point: (name, ok, detail)."""
    checks = []
    def stages(**kw):
        return tuple(kw.get(s, 0) for s in STATS)
    if name == "single":
        b1 = battles[0] if battles else None
        back = first_on_arrival(b1, "LUCARIO", 2) if b1 else None
        checks.append(("single: Lucario switched back in shows ATK+2 DEF-2 again (Swords Dance, two Tail Whips)",
                       back == stages(ATK=2, DEF=-2), back))
        arc = first_on_arrival(b1, "ARCANINE", 1) if b1 else None
        checks.append(("single: Arcanine comes in at 0 (Lucario's stages are its own)", arc == stages(), arc))
        foe = [e for e in (b1 or {}).get("events", []) if e[0] == "battler" and e[1] == 1 and e[6] == stages(SPE=-2)]
        checks.append(("single: the opposing Zigzagoon shows SPE-2 after Scary Face", bool(foe), len(foe)))
        b2 = battles[1] if len(battles) > 1 else None
        arc2 = first_on_arrival(b2, "ARCANINE", 1) if b2 else None
        checks.append(("single: the next battle: Arcanine (it kept ATK+2 DEF-2 at the first battle's end) comes in "
                       "at 0", arc2 == stages(), arc2))
    elif name == "doubles":
        four = [e for b in battles for e in b["events"] if e[0] == "shot" and e[2].count("[") == 4
                and all(part.split(":", 1)[1].strip(" ]") for part in e[2].split("[")[1:])]
        checks.append(("doubles: a picture with all four battlers tagged", bool(four), four[:1]))
    elif name == "baton":
        b1 = battles[0] if battles else None
        arc = first_on_arrival(b1, "ARCANINE", 1) if b1 else None
        checks.append(("baton: Arcanine comes in with Lucario's ATK+2 (Baton Pass)", arc == stages(ATK=2), arc))
        luc = first_on_arrival(b1, "LUCARIO", 2) if b1 else None
        checks.append(("baton: Lucario, which passed them, comes back at 0", luc == stages(), luc))
        arc2 = first_on_arrival(b1, "ARCANINE", 2) if b1 else None
        checks.append(("baton: Arcanine comes back with ATK+2 SPE+2 (the passed stages and its Agility, kept)",
                       arc2 == stages(ATK=2, SPE=2), arc2))
    elif name == "roar":
        dragged = []
        for b in battles:
            turns = [e for e in b["events"] if e[0] == "turn" and e[2] == "s"]
            for a in arrivals(b):
                d = a["departing"]
                if d is not None and d[4] != 0 and any(d[3]):
                    dragged.append((a["who"][2], d[2], d[3]))
        back = [x for x in dragged]
        checks.append(("roar: Pokemon with stages dragged out (no switch was chosen: Whirlwind)",
                       not any(e[0] == "turn" and e[2] == "s" for b in battles for e in b["events"]) and bool(back),
                       back[:6]))
        returns = []
        for b in battles:
            gone = {}
            for a in arrivals(b):
                d = a["departing"]
                if d is not None and d[4] != 0:
                    gone[(d[0], d[1])] = d[3]
                key = a["who"][:2]
                if key in gone and any(gone[key]):
                    returns.append((a["who"][2], gone[key], a["first"]))
        checks.append(("roar: a Pokemon dragged out with stages has them again when it comes back",
                       bool(returns) and all(r[1] == r[2] for r in returns), returns[:6]))
    elif name == "faint":
        b1 = battles[0] if battles else None
        fainted = [e for e in (b1 or {}).get("events", []) if e[0] == "battler" and e[4] == SPECIES["LUCARIO"]
                   and e[5] == 0]
        before = [e[6] for e in (b1 or {}).get("events", []) if e[0] == "battler" and e[4] == SPECIES["LUCARIO"]
                  and e[5] != 0]
        checks.append(("faint: Lucario fainted (Memento) after its Swords Dance",
                       bool(fainted) and stages(ATK=2) in before, dict(fainted=len(fainted))))
        luc = first_on_arrival(b1, "LUCARIO", 2) if b1 else None
        checks.append(("faint: revived (Revival Blessing) and sent in again, Lucario shows nothing (fainting cleared "
                       "its stages)", luc == stages(), luc))
        foe = [e for e in (b1 or {}).get("events", []) if e[0] == "battler" and e[1] == 1
               and e[6] == stages(ATK=-2, SPA=-2)]
        checks.append(("faint: the foe shows ATK-2 SPA-2 after Memento", bool(foe), len(foe)))
    return checks


def sheet(case_dir, out, pictures):
    try:
        from PIL import Image, ImageDraw
    except ImportError:
        return None
    if not pictures:
        return None
    tiles = []
    for path, label in pictures:
        im = Image.open(path).convert("RGB")
        scale = 2 if im.width <= 240 else 640 / im.width
        tiles.append((im.resize((int(im.width * scale), int(im.height * scale)), Image.NEAREST), label))
    w, h = max(t.width for t, _ in tiles), max(t.height for t, _ in tiles) + 16
    cols = 2
    board = Image.new("RGB", (w * cols, h * ((len(tiles) + cols - 1) // cols)), (24, 24, 24))
    draw = ImageDraw.Draw(board)
    for i, (im, label) in enumerate(tiles):
        x, y = (i % cols) * w, (i // cols) * h
        board.paste(im, (x, y + 16))
        draw.text((x + 4, y + 2), label[:110], fill=(255, 255, 255))
    board.save(out)
    return out


def run_case(args, name):
    rundir = os.path.join(args.out, name)
    env = dict(CASES[name], PKMN_TEST_STAGE_LOG="1", PKMN_TEST_STAGE_SHOTS="1", PKMN_TEST_FIGHT=str(args.fight),
               PKMN_TEST_FREEZE_FIELD="1", PKMN_TEST_AUTOPILOT="1", PKMN_TEST_MEGA_SHOT_STEM=os.path.join(rundir, "shot"))
    if args.scene_off or name != "single":
        env["PKMN_TEST_SCENE_OFF"] = "1"
    process, output = start(args, rundir, env)
    code = finish(process, output, args.fight + 240)
    lines = driver_lines(os.path.join(rundir, "test.log"))
    battles = battles_of(lines)
    checks = [("the game ended after its battles (exit 0)", code == 0, code)]
    for i, battle in enumerate(battles):
        checks += check_battle(battle, i + 1)
    checks += case_checks(name, battles)
    shots = [(m.group(1), m.group(2)) for m in (SHOT.search(line) for line in lines) if m]
    pictures = sorted(glob.glob(os.path.join(rundir, "shot-*.png")))
    if not pictures:   # the 3D build: window pictures in the order taken
        pictures = sorted(glob.glob(os.path.join(rundir, "screenshots", "*.png")), key=os.path.getmtime)
    labelled = [(p, f"{i:02d} {shots[i][1] if i < len(shots) else ''}") for i, p in enumerate(pictures)]
    board = sheet(rundir, os.path.join(args.out, f"{name}.png"), labelled)
    print(f"{name}: exit {code}, {len(battles)} battle(s), {len(pictures)} pictures")
    for check, ok, detail in checks:
        print(f"  {'ok  ' if ok else 'FAIL'} {check}" + ("" if ok else f": {detail}"))
    return dict(case=name, exit=code, battles=len(battles), pictures=pictures, sheet=board,
                checks=[dict(check=c, ok=bool(ok), detail=repr(d)) for c, ok, d in checks])


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", required=True)
    parser.add_argument("--save", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--display", default=os.environ.get("DISPLAY"))
    parser.add_argument("--cases", default=",".join(CASES))
    parser.add_argument("--fight", type=int, default=600, help="seconds a case's battles may take")
    parser.add_argument("--scene-off", action="store_true", help="battle animations off in every case")
    args = parser.parse_args()
    args.exe, args.save = os.path.abspath(args.exe), os.path.abspath(args.save)
    os.makedirs(args.out, exist_ok=True)
    results = [run_case(args, name) for name in args.cases.split(",") if name]
    passed = all(c["ok"] for r in results for c in r["checks"])
    with open(os.path.join(args.out, "stat_stages_check.json"), "w") as out:
        json.dump(dict(passed=passed, cases=results), out, indent=1)
    print(json.dumps(dict(passed=passed, failed=[c["check"] for r in results for c in r["checks"] if not c["ok"]])))
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
