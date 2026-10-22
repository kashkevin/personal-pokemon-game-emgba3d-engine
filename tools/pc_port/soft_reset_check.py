#!/usr/bin/env python3
"""Field check of the soft reset without a world (Update 32; MS-61: the reset key, no menu row), through the game's own UI.

Runs the native game headless under xvfb with PKMN_DEBUG_KEYS (tools/pc_port/rival_check.py's runner and its
"rival F: ..." state lines). The key is A+B+START+SELECT pressed together, as Ctrl+R sends it (src/platform/sdl2.c).
Each scenario states what must be true:

  - the starter reroll: in Birch's lab, before the claim, the player saves (START > SAVE); claims the Fire ball's
    starter; presses the reset key and YES. The game goes back to its title screen (the real soft reset), CONTINUE
    loads the save: no Pokemon, the claim open and the balls back on the table; the second claim is a new roll
    (another personality), and the game keeps exactly one Pokemon;
  - NO keeps playing: the key and NO leave the player on the field with the starter, without a reset, and the player
    walks on afterwards (nothing left locked);
  - with the START menu open the key closes it and asks;
  - pressed in the middle of a step the key asks once the step ends (where START is read);
  - pressed while another screen has the game (the starter's screen) it does nothing at all: the press is dropped and
    the claim goes on as if it had not happened;
  - the PLAYER list has no RESET row any more (the reset is only the key).

    tools/pc_port/soft_reset_check.py [--binary ./pokeemerald-mp] [--only name,name] [--keep-logs DIR]

Needs xvfb-run. A simulation on a software display: no real PC. The field scene (PKMN_DEBUG_SCENE, never reachable in
normal play) starts the first boot in the lab; after the reset the game boots as it would, to its title screen.
The reset in a world (the launcher restarts the game from the host's save) is the app's
scripts/multiplayer/native_launcher_soft_reset_proof.py.
"""
import argparse
import os
import re
import shutil
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rival_check as rc  # noqa: E402  (the runner, the constants and the state lines)
import intro32_check as ic  # noqa: E402  (the lab scene and its watch list)

ROOT = rc.ROOT
TORCHIC, LAB, PENDING = ic.TORCHIC, ic.LAB, ic.PENDING
CLAIM = re.compile(r"intro: lab starter (\d+) \(species (\d+), shiny (\d), personality ([0-9a-f]{8}), ivs ([\d/]+)\)")


def claims(r):
    return [dict(choice=int(m.group(1)), species=int(m.group(2)), shiny=int(m.group(3)), personality=m.group(4),
                 ivs=m.group(5)) for line in r.lines for m in [CLAIM.search(line)] if m]


def claim_keys(at):
    # face the ball (a tap turns), open the screen, take its own starter, YES; then B only: B answers NO to the
    # nickname and pages through the last message (an A there would answer YES)
    return [f"{at}:UP", f"{at + 60}:A", f"{at + 400}:A", f"{at + 600}:A"] + [f"{f}:B" for f in range(at + 1050, at + 1800, 50)]


# The START menu without a Pokedex: BAG, PLAYER, SAVE, OPTION, EXIT.
SAVE_KEYS = ["300:START", "360:DOWN", "400:DOWN", "440:A", "520:A"]
CHORD = "A+B+START+SELECT"      # the reset key: A+B+START+SELECT together, which Ctrl+R sends for one frame


def reset_keys(at, yes):
    # the key, then the question ("Reset the game to your last save?"): its cursor starts on NO
    return [f"{at}:{CHORD}"] + ([f"{at + 120}:UP", f"{at + 160}:A"] if yes else [f"{at + 160}:A"])


def reroll(sym):
    def check(r):
        problems = []
        rolls = claims(r)
        if len(rolls) != 2:
            return [f"{len(rolls)} starter claims logged, expected two (one before and one after the reset)"]
        if any(roll["species"] != TORCHIC for roll in rolls):
            problems.append(f"claimed {[roll['species'] for roll in rolls]}, expected Torchic both times")
        if rolls[0]["personality"] == rolls[1]["personality"]:
            problems.append(f"the same personality {rolls[0]['personality']} after the reset: not a new roll")
        if not r.trace("soft reset: asked (no world)") or not r.trace("soft reset: resetting"):
            problems.append("RESET did not ask, or YES did not reset")
        seen = ic.callbacks(r)
        if sym.get("CB2_InitTitleScreen") not in seen:
            problems.append("the game never went back to its title screen")
        if sym.get("CB2_ContinueSavedGame") not in seen:
            problems.append("CONTINUE never loaded the save")
        # Between the two claims: back in the lab from the save, no Pokemon, the claim open, the Fire ball on the table.
        loaded = [row for row in r.rows if (row["group"], row["num"]) == LAB and row["party"] == 0
                  and r.watched(row, "VAR_MULTIPLAYER_STARTER_CHOICE") == PENDING and r.obj(row) == 1]
        first_claim = next((row["frame"] for row in r.rows if row["party"] == 1), None)
        if first_claim is None or not any(row["frame"] > first_claim for row in loaded):
            problems.append("after the reset the lab was not as saved (no Pokemon, claim open, ball on the table)")
        last = r.last
        if (last["group"], last["num"]) != LAB or last["party"] != 1 or ic.mon(last) != (TORCHIC, 5, ic.MAPSEC_LITTLEROOT):
            problems.append(f"ends at {last['group']}.{last['num']} with party {last['party']} {ic.mon(last)}: "
                            "expected the lab and one Torchic")
        if r.ever(lambda row: row["party"] > 1):
            problems.append("more than one Pokemon")
        return problems
    return check


def kept(sym, asked=True, walked_after=None):
    """No reset: the claim stands. `asked`: the key asked (and NO answered); else the press was dropped and nothing asked.
    `walked_after`: a frame after which the player must have moved (the field was left free)."""
    def check(r):
        problems = []
        if len(claims(r)) != 1:
            problems.append(f"{len(claims(r))} claims logged, expected one")
        if not r.trace("soft reset: key"):
            problems.append("the game never saw the reset key")
        if asked and not (r.trace("soft reset: key taken") and r.trace("soft reset: asked (no world)")):
            problems.append("the key did not ask")
        if not asked and (r.trace("soft reset: key taken") or r.trace("soft reset: asked")):
            problems.append("the key asked where the game was not in the field")
        if not asked and not r.trace("soft reset: key dropped"):
            problems.append("the key was not dropped")
        if r.trace("soft reset: resetting") or sym.get("CB2_InitTitleScreen") in ic.callbacks(r):
            problems.append("the game was reset")
        if r.last["party"] != 1 or (r.last["group"], r.last["num"]) != LAB:
            problems.append(f"party {r.last['party']} at {r.last['group']}.{r.last['num']}: expected the starter, in the lab")
        if walked_after is not None:
            before = [row for row in r.rows if row["frame"] <= walked_after]
            after = [row for row in r.rows if row["frame"] > walked_after]
            if not before or not after or all((row["x"], row["y"]) == (before[-1]["x"], before[-1]["y"]) for row in after):
                problems.append("the player could not walk after the question (the field stayed locked)")
        return problems
    return check


def walked(sym):
    """The key pressed in the middle of a step: taken once the step ends, then NO; the player is a tile further on."""
    inner = kept(sym, walked_after=None)

    def check(r):
        problems = inner(r)
        start = [row for row in r.rows if row["frame"] <= 2200]
        if not start or not any((row["x"], row["y"]) != (start[-1]["x"], start[-1]["y"]) for row in r.rows
                                if row["frame"] > 2200):
            problems.append("the player did not walk the step the key was pressed in")
        return problems
    return check


def scenarios(sym):
    # lab-fire-ball's field scene ((9,5) below the Fire ball, the claim open), without the Pokedex and the PokeNav the
    # field scene gives: a new character has neither, and their START menu rows are the ones the keys count.
    lab = dict(ic.scenarios(sym)[4][2])
    lab["PKMN_DEBUG_RIVAL_SET"] += "," + rc.setup(FLAG_SYS_POKEDEX_GET=0, FLAG_SYS_POKENAV_GET=0)
    title = [f"{f}:A" for f in range(3300, 4001, 140)]     # title screen, PRESS START, CONTINUE (the first row)
    return [
        ("reset-reroll", "save, claim, the reset key and YES: the title screen, CONTINUE, the claim open again, a new roll",
         lab, SAVE_KEYS + claim_keys(800) + reset_keys(2700, True) + title + claim_keys(4300), reroll(sym)),
        ("reset-no", "the reset key and NO: the player keeps playing with the starter, no reset, and walks on",
         lab, claim_keys(300) + reset_keys(2200, False) + rc.hold("DOWN", 2600, 24), kept(sym, walked_after=2560)),
        ("reset-start-menu", "the reset key with the START menu open: the menu closes and the key asks; NO",
         lab, claim_keys(300) + ["2200:START"] + reset_keys(2320, False) + rc.hold("DOWN", 2700, 24),
         kept(sym, walked_after=2660)),
        ("reset-mid-step", "the reset key in the middle of a step: asked once the step ends; NO",
         lab, claim_keys(300) + rc.hold("DOWN", 2200, 16) + reset_keys(2206, False), walked(sym)),
        ("reset-other-screen", "the reset key while the starter's screen has the game: dropped, the claim goes on",
         lab, claim_keys(300)[:2] + [f"500:{CHORD}"] + claim_keys(300)[2:], kept(sym, asked=False)),
    ]


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--binary", default=os.path.join(ROOT, "pokeemerald-mp"))
    parser.add_argument("--only", default="")
    parser.add_argument("--timeout", type=float, default=180)
    parser.add_argument("--keep-logs", default="")
    parser.add_argument("--facts", default="", help="the engine's numbers (rival_check.py --write-facts); else its headers")
    args = parser.parse_args()
    rc.load_facts(args.facts)
    only = set(filter(None, args.only.split(",")))
    sym = ic.symbols(os.path.abspath(args.binary))
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
            problems = ["no state lines (the game did not reach the field)"] if not result.rows else check(result)
            if name == "reset-reroll" and not (os.path.exists(save) and os.path.getsize(save) == 128 * 1024):
                problems.append("no 128 KiB save file was written")
        failures += bool(problems)
        print(f"{'FAIL' if problems else 'ok  '} {name}: {what}")
        for problem in problems:
            print(f"     {problem}")
        for roll in claims(result):
            print(f"     claim: species {roll['species']} shiny {roll['shiny']} personality {roll['personality']} ivs {roll['ivs']}")
        sys.stdout.flush()
    print(f"soft_reset_check: {failures} failed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
