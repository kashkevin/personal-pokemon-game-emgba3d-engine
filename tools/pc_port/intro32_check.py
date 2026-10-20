#!/usr/bin/env python3
"""Field check of the Update 32 short introduction and the starter claim in Birch's lab, through the game's own UI.

Runs the native game headless under xvfb with PKMN_DEBUG_KEYS (tools/pc_port/rival_check.py's runner and its
"rival F: ..." state lines). Each scenario states what must be true:

  - the short introduction, from a fresh boot through the real title screen and main menu: NEW GAME asks boy or girl
    at once (no speech), then the name, then the game starts in Littleroot in front of the player's house with no
    Pokemon, the claim pending (VAR_MULTIPLAYER_STARTER_CHOICE 0x62FF), Birch's lab state 3, never the 18-starter
    screen, and one save of 128 KiB;
  - the Route 101 guard: with no Pokemon the path north turns the player back; with a starter it is open;
  - the lab: each ball on the table opens the 18-starter screen on its own type; the chosen starter joins the party
    (level 5, met in Littleroot), the starter flag and choice are set and the balls are gone; B on the screen backs out
    with nothing given and the claim still open; an Update 31 character's lab has no balls.

    tools/pc_port/intro32_check.py [--binary ./pokeemerald-mp] [--only name,name] [--keep-logs DIR]

Needs xvfb-run. A simulation on a software display: no real PC, network or host. The field scenarios start from the
test-only field scene (PKMN_DEBUG_SCENE, never reachable in normal play) with the save state of a short-introduction
character set through PKMN_DEBUG_RIVAL_SET.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rival_check as rc  # noqa: E402  (the runner, the constants and the state lines)

ROOT = rc.ROOT
C, ref, setup, presses, hold = rc.C, rc.ref, rc.setup, rc.presses, rc.hold
PENDING, LITTLEROOT, ROUTE101, LAB = 0x62FF, (0, 9), (0, 16), (1, 4)
TREECKO, TORCHIC, SQUIRTLE, MUDKIP = 252, 255, 7, 258
MAPSEC_LITTLEROOT = 0
WATCH = ("VAR_MULTIPLAYER_STARTER_CHOICE", "FLAG_SYS_POKEMON_GET", "VAR_BIRCH_LAB_STATE", "VAR_LITTLEROOT_TOWN_STATE",
         "VAR_STARTER_MON", "FLAG_SYS_POKEDEX_GET", "FLAG_RESCUED_BIRCH")
JOHTO_FLAGS = dict(FLAG_HIDE_LITTLEROOT_TOWN_BIRCHS_LAB_POKEBALL_CYNDAQUIL=1, FLAG_HIDE_LITTLEROOT_TOWN_BIRCHS_LAB_POKEBALL_TOTODILE=1,
                   FLAG_HIDE_LITTLEROOT_TOWN_BIRCHS_LAB_POKEBALL_CHIKORITA=1)


def symbols(binary):
    out = subprocess.run(["nm", binary], capture_output=True, text=True).stdout
    return {m.group(2): int(m.group(1), 16) for m in re.finditer(r"^([0-9a-f]+) [tT] (\w+)$", out, re.M)}


def callbacks(r):
    return [int(m.group(1), 16) for line in r.lines for m in [re.search(r"game frame \d+: callback2 (0x[0-9a-f]+)", line)] if m]


def mon(row):
    m = re.search(r"\| mon (\d+) lv (\d+) met (\d+)", row["rest"])
    return tuple(int(v) for v in m.groups()) if m else None


def short_intro_keys(gender):
    keys = [f"{f}:A" for f in range(400, 801, 100)]                     # title, main menu, NEW GAME
    keys += ["970:DOWN", "1010:A"] if gender else ["980:A"]              # BOY / GIRL
    keys += ["1100:START", "1140:A"]                                    # naming screen: OK
    keys += ["1300:A", "1360:A"]                                        # "So it's ...?" YES
    keys += presses(1500, 24, 45)                                       # the arrival message and the save
    return keys


def short_intro(gender, sym):
    def check(r):
        problems = []
        last = r.last
        spawn = (5, 9) if gender == 0 else (14, 9)
        if (last["group"], last["num"]) != LITTLEROOT or (last["x"], last["y"]) != spawn:
            problems.append(f"ended at map {last['group']}.{last['num']} {last['x']},{last['y']}, expected Littleroot {spawn}")
        if r.ever(lambda row: row["party"] != 0):
            problems.append("a Pokemon was given by the introduction")
        if r.watched(last, "VAR_MULTIPLAYER_STARTER_CHOICE") != PENDING or r.watched(last, "FLAG_SYS_POKEMON_GET") != 0:
            problems.append(f"starter state {r.watched(last, 'VAR_MULTIPLAYER_STARTER_CHOICE')} / flag "
                            f"{r.watched(last, 'FLAG_SYS_POKEMON_GET')}, expected the pending claim 0x62FF and no starter flag")
        if r.watched(last, "VAR_BIRCH_LAB_STATE") != 3 or r.watched(last, "VAR_LITTLEROOT_TOWN_STATE") != 2 \
                or r.watched(last, "FLAG_RESCUED_BIRCH") != 1:
            problems.append("the story state is not Birch's 'go to Route 103' (lab 3, Littleroot 2, rescued)")
        if r.watched(last, "FLAG_SYS_POKEDEX_GET") or last["balls"]:
            problems.append("a Pokedex or Poke Balls granted by the introduction")
        seen = callbacks(r)
        if sym.get("CB2_StarterChoose") in seen:
            problems.append("the 18-starter screen opened during the introduction")
        if sym.get("CB2_NamingScreen") not in seen:
            problems.append("the naming screen never opened")
        if not r.trace("intro: saved (status 1)"):
            problems.append("the introduction did not save (no 'intro: saved (status 1)')")
        if r.ever(lambda row: (row["group"], row["num"]) in ((25, 40), ROUTE101) or row["battle"]):
            problems.append("the truck, Route 101 or a battle happened")
        return problems
    return check


def guard(blocked):
    def check(r):
        problems = []
        on_route = r.ever(lambda row: (row["group"], row["num"]) == ROUTE101)
        if blocked:
            if on_route or r.ever(lambda row: (row["group"], row["num"]) == LITTLEROOT and row["y"] <= 0):
                problems.append("a player with no Pokemon reached Route 101")
            if not r.ever(lambda row: (row["group"], row["num"]) == LITTLEROOT and row["y"] == 1):
                problems.append("the player never stepped onto the path (the guard was not reached)")
            if (r.last["group"], r.last["num"]) != LITTLEROOT or r.last["y"] != 2:
                problems.append(f"not turned back: ends at {r.last['group']}.{r.last['num']} y {r.last['y']}, expected Littleroot y 2")
        elif not on_route:
            problems.append("a player with a starter was stopped before Route 101")
        return problems
    return check


def claimed(species, choice, sym):
    def check(r):
        problems = []
        last = r.last
        m = mon(last)
        if last["party"] != 1 or not m or m != (species, 5, MAPSEC_LITTLEROOT):
            problems.append(f"party {last['party']} {m}: expected one level 5 species {species} met in Littleroot")
        if r.watched(last, "FLAG_SYS_POKEMON_GET") != 1 or r.watched(last, "VAR_MULTIPLAYER_STARTER_CHOICE") != 0x6100 | choice \
                or r.watched(last, "VAR_STARTER_MON") != choice % 3:
            problems.append(f"flag {r.watched(last, 'FLAG_SYS_POKEMON_GET')}, choice {r.watched(last, 'VAR_MULTIPLAYER_STARTER_CHOICE')}, "
                            f"starter {r.watched(last, 'VAR_STARTER_MON')}: expected 1, {0x6100 | choice}, {choice % 3}")
        if sym.get("CB2_StarterChoose") not in callbacks(r):
            problems.append("the 18-starter screen never opened")
        if r.obj(last) != 0:
            problems.append("the ball on the table is still there after the claim")
        if r.ever(lambda row: row["party"] > 1):
            problems.append("more than one Pokemon")
        return problems
    return check


def backed_out(sym):
    def check(r):
        problems = []
        if sym.get("CB2_StarterChoose") not in callbacks(r):
            problems.append("the 18-starter screen never opened")
        if r.last["party"] != 0 or r.watched(r.last, "VAR_MULTIPLAYER_STARTER_CHOICE") != PENDING:
            problems.append(f"party {r.last['party']} choice {r.watched(r.last, 'VAR_MULTIPLAYER_STARTER_CHOICE')}: B must give nothing")
        if r.obj(r.last) != 1:
            problems.append("the ball went away although nothing was claimed")
        if (r.last["group"], r.last["num"]) != LAB:
            problems.append("not back in the lab")
        return problems
    return check


def old_character(sym):
    def check(r):
        problems = []
        if sym.get("CB2_StarterChoose") in callbacks(r):
            problems.append("the 18-starter screen opened for a character that has its starter")
        if r.last["party"] != 1 or r.obj(r.last) != 0:
            problems.append(f"party {r.last['party']}, ball {r.obj(r.last)}: expected the one Pokemon and no ball")
        return problems
    return check


def scenarios(sym):
    pending = dict(VAR_MULTIPLAYER_STARTER_CHOICE=PENDING, VAR_BIRCH_LAB_STATE=3, VAR_LITTLEROOT_TOWN_STATE=2, FLAG_RESCUED_BIRCH=1,
                   **JOHTO_FLAGS)
    watch = ",".join(ref(n) for n in WATCH)

    def field(here, state, party0=True, obj=None):
        env = dict(PKMN_DEBUG_SCENE="field", PKMN_DEBUG_HERE=here, PKMN_DEBUG_RIVAL_SET=setup(**state), PKMN_DEBUG_RIVAL_WATCH=watch)
        if party0:
            env["PKMN_DEBUG_PARTY"] = "0"
        if obj is not None:
            env["PKMN_DEBUG_RIVAL_OBJ"] = str(obj)
        return env

    lab = lambda x, obj: field(f"1,4,{x},5", pending, obj=obj)  # noqa: E731  (in front of the table, below the ball)
    # face the ball (a tap turns without a step), open the screen, pick, YES; then B only: the received message needs no
    # button, B answers NO to the nickname and pages through the last message (an A there would answer YES)
    claim_keys = lambda browse=(): (["300:UP", "360:A"] + list(browse) + ["700:A", "900:A"]  # noqa: E731
                                    + [f"{f}:B" for f in range(1350, 2100, 50)])
    return [
        ("intro-short-male", "fresh boot: NEW GAME asks boy or girl at once, then the name; Littleroot (5,9), no Pokemon, one save",
         dict(PKMN_DEBUG_RIVAL_WATCH=watch), short_intro_keys(0), short_intro(0, sym)),
        ("intro-short-female", "the same as a girl: Littleroot (14,9)",
         dict(PKMN_DEBUG_RIVAL_WATCH=watch), short_intro_keys(1), short_intro(1, sym)),
        ("guard-no-pokemon", "no Pokemon: the path to Route 101 turns the player back",
         field("0,9,10,4", pending), hold("UP", 300, 40) + presses(420, 8, 40) + hold("UP", 800, 40) + presses(920, 8, 40), guard(True)),
        ("guard-with-starter", "with a starter the path is open",
         field("0,9,10,4", dict(VAR_MULTIPLAYER_STARTER_CHOICE=0x6101, VAR_LITTLEROOT_TOWN_STATE=2, FLAG_RESCUED_BIRCH=1),
               party0=False),
         hold("UP", 300, 120), guard(False)),
        ("lab-fire-ball", "the Fire ball opens the screen on Torchic; YES gives it, the balls are gone",
         lab(9, 4), claim_keys(), claimed(TORCHIC, 1, sym)),
        ("lab-grass-ball", "the Grass ball opens it on Treecko",
         lab(8, 6), claim_keys(), claimed(TREECKO, 0, sym)),
        ("lab-water-ball-kanto", "the Water ball, then Down to the next region: Squirtle",
         lab(10, 5), claim_keys(["520:DOWN"]), claimed(SQUIRTLE, 5, sym)),
        ("lab-back-out", "B on the screen goes back with nothing given; the ball stays",
         lab(9, 4), ["300:UP", "360:A", "700:B", "900:B"], backed_out(sym)),
        ("lab-update31-character", "a character that has its starter: no ball, no screen",
         field("1,4,9,5", dict(VAR_MULTIPLAYER_STARTER_CHOICE=0x6105, FLAG_SYS_POKEMON_GET=1, VAR_BIRCH_LAB_STATE=3, **JOHTO_FLAGS),
               party0=False, obj=4),
         ["300:UP", "360:A", "700:A", "900:A"], old_character(sym)),
    ]


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--binary", default=os.path.join(ROOT, "pokeemerald-mp"))
    parser.add_argument("--only", default="")
    parser.add_argument("--timeout", type=float, default=120)
    parser.add_argument("--keep-logs", default="")
    args = parser.parse_args()
    only = set(filter(None, args.only.split(",")))
    sym = symbols(os.path.abspath(args.binary))
    failures = 0
    for name, what, env, keys, check in scenarios(sym):
        if only and name not in only:
            continue
        with tempfile.TemporaryDirectory() as folder:
            save = os.path.join(folder, "check.sav")
            result = rc.run(args.binary, env, keys, args.timeout, save)
            if args.keep_logs:
                os.makedirs(args.keep_logs, exist_ok=True)
                with open(os.path.join(args.keep_logs, name + ".log"), "w") as log:
                    log.write("\n".join(result.lines))
                if os.path.exists(save):
                    shutil.copyfile(save, os.path.join(args.keep_logs, name + ".sav"))
            problems = ["no state lines (the game did not reach the field)"] if not result.rows else check(result)
            if name.startswith("intro") and not (os.path.exists(save) and os.path.getsize(save) == 128 * 1024):
                problems.append("no 128 KiB save file was written")
        failures += bool(problems)
        print(f"{'FAIL' if problems else 'ok  '} {name}: {what}")
        for problem in problems:
            print(f"     {problem}")
        sys.stdout.flush()
    print(f"intro32_check: {failures} failed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
