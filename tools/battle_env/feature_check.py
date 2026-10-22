#!/usr/bin/env python3
"""Update 32's single-player features in real game processes (test builds; Linux and Windows alike).

Everything is read from the game's own log (the battle-environment test driver, include/diorama/battle_env_test.h,
TEST BUILDS ONLY: PKMN_TEST_LOG, PKMN_TEST_FACTS, PKMN_TEST_STARTER_LOG, PKMN_TEST_FIELD_KEYS, PKMN_TEST_SOFT_RESET);
nothing reads the game's memory. The keys are the ones a player presses; the Classic 2D test build pictures the steps.

1. new-character (feature 1): from a fresh boot the game's own short introduction (NEW GAME, a boy, the offered name;
   mega_check.make_save) writes the first save on the arrival in Littleroot.
2. no-pokemon (feature 2): that save loaded: in Littleroot, no Pokemon, the starter claim open, no Pokemon got.
3. lab-save: from it, in Birch's lab below the Fire ball (PKMN_TEST_WARP), START > SAVE with no Pokemon (the menu has no
   POKeMON row then).
4. claim (feature 3): from that save, UP (face the ball), A (the 18-starter screen, on Fire), A (Torchic), A (YES), then
   B through the nickname question and the last lines: one Pokemon, Torchic, the claim made (0x6100 | the starter) and
   FLAG_SYS_POKEMON_GET; then START > SAVE.
5. claim-load: that save loaded: the same Torchic (its personality), the claim made.
6. rerolls (features 4 and 5): from the lab save, in one process, the claim and then the game's own soft reset (START >
   PLAYER > RESET, YES), --rerolls times (PKMN_TEST_SOFT_RESET): after every reset the save as it was (no Pokemon, the
   claim open, in the lab), and every claim a new roll (a personality of its own; each one's shininess logged).

    tools/battle_env/feature_check.py --exe ./pokeemerald-mp-bfxtest --make-save build/new.sav --out build/feature-check

Exit 0 when every check gave what it expects; the report is OUT/feature_check.json.
"""
import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mega_check as mc  # noqa: E402  (the game in a folder of its own, the short introduction, the menu keys)

# The Fire ball's place in the lab (tools/pc_port/intro32_check.py): below it, in front of the table.
BALL_X, BALL_Y = 9, 5
# Face the ball (a tap turns without a step), open the 18-starter screen on its type, pick, YES; then B only: the
# received message needs no button, B answers NO to the nickname and pages through the last line (an A there would
# answer YES and open the naming screen).
CLAIM = ["30:UP", "90:A", "430:A", "630:A"] + [f"{f}:B" for f in range(1080, 1850, 50)]
AFTER_CLAIM = 1950


def save_keys_no_pokemon(start):
    """mega_check.save_keys for a player with no Pokemon: the menu has no POKeMON row, so SAVE is the third row."""
    return [f"{start + f}:{k}" for f, k in ((0, "START"), (50, "DOWN"), (80, "DOWN"), (160, "A"), (260, "A"),
                                            (380, "A"), (700, "B"))]


def starter_lines(lines):
    """Each "starter field:" line: map group, map number, x, y, party, lead species, personality, shiny, claim, got."""
    rows = []
    for line in lines:
        if line.startswith("starter field: "):
            words = line.split()
            group, num = (int(v) for v in words[3].split("."))
            x, y = (int(v) for v in words[5].split(","))
            rows.append(dict(map=(group, num), x=x, y=y, party=int(words[7]), lead=int(words[9]),
                             personality=words[11], shiny=int(words[13]), choice=int(words[15], 16), got=int(words[17])))
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", required=True, help="a test build (Makefile_pc BFX_TEST=1; the Classic 2D one pictures)")
    parser.add_argument("--save", help="a short-introduction save to start from (copied; never written)")
    parser.add_argument("--make-save", metavar="PATH", help="first make the save there (the short introduction)")
    parser.add_argument("--out", required=True)
    parser.add_argument("--display", default=os.environ.get("DISPLAY"), help="an X display (Linux)")
    parser.add_argument("--rerolls", type=int, default=3, help="claims, each followed by a soft reset")
    parser.add_argument("--timeout", type=int, default=300, help="seconds per step (the rerolls: per reroll)")
    args = parser.parse_args()
    args.exe, args.out = os.path.abspath(args.exe), os.path.abspath(args.out)
    os.makedirs(args.out, exist_ok=True)
    steps, problems = [], []
    if args.make_save:
        args.save = mc.make_save(args, os.path.abspath(args.make_save))
        steps.append(dict(step="new-character", save=args.save))
    if not args.save:
        parser.error("--save or --make-save")
    args.save = os.path.abspath(args.save)
    facts, _ = mc.game_facts(args)
    lab = (facts["MAP_LITTLEROOT_TOWN_PROFESSOR_BIRCHS_LAB"] >> 8, facts["MAP_LITTLEROOT_TOWN_PROFESSOR_BIRCHS_LAB"] & 0xFF)
    town = (facts["MAP_LITTLEROOT_TOWN"] >> 8, facts["MAP_LITTLEROOT_TOWN"] & 0xFF)
    pending, torchic = facts["MP_INTRO_STARTER_PENDING"], facts["SPECIES_TORCHIC"]
    claimed = 0x6100 | 1    # the Fire ball's first starter: Torchic (the claim's starter number, starter_choose.c)

    def step(name, save, env, timeout=args.timeout):
        rundir = os.path.join(args.out, name)
        saved = args.save
        args.save = save
        try:
            process, output = mc.start(args, rundir, dict(PKMN_TEST_STARTER_LOG="1", PKMN_TEST_FREEZE_FIELD="1",
                                                          PKMN_TEST_AUTOPILOT="1", PKMN_TEST_BATTLE_DELAY="100",
                                                          PKMN_TEST_MEGA_SHOT_STEM=os.path.join(rundir, "step"), **env))
            code = mc.finish(process, output, timeout)
        finally:
            args.save = saved
        lines = mc.driver_lines(os.path.join(rundir, "test.log"))
        rows = starter_lines(lines)
        steps.append(dict(step=name, exit=code, first=rows[0] if rows else None, last=rows[-1] if rows else None,
                          pictures=sorted(f for f in os.listdir(rundir) if f.endswith(".png"))))
        return rundir, lines, rows

    def open_claim(row):
        return row["party"] == 0 and row["choice"] == pending and row["got"] == 0

    def torchic_claimed(row):
        return row["party"] == 1 and row["lead"] == torchic and row["choice"] == claimed and row["got"] == 1

    # 2. the new character's save: Littleroot, no Pokemon, the claim open
    _, _, rows = step("no-pokemon", args.save, dict(PKMN_TEST_BATTLE="field:60"))
    if not rows or rows[-1]["map"] != town or not open_claim(rows[-1]):
        problems.append(f"no-pokemon: the new character's save shows {rows[-1:]}, not Littleroot with no Pokemon and the "
                        f"claim open ({pending:#x})")
    # 3. in the lab below the Fire ball, saved with no Pokemon (pictures: the menu, the question, the end)
    lab_dir, _, rows = step("lab-save", args.save, dict(PKMN_TEST_BATTLE="field:900",
                                                        PKMN_TEST_WARP=f"{lab[0]}:{lab[1]}:0:{BALL_X}:{BALL_Y}",
                                                        PKMN_TEST_FIELD_KEYS=",".join(save_keys_no_pokemon(30)),
                                                        PKMN_TEST_FIELD_SHOTS="60,200,300,420"))
    lab_save = os.path.join(lab_dir, "test.sav")
    if not rows or rows[-1]["map"] != lab or (rows[-1]["x"], rows[-1]["y"]) != (BALL_X, BALL_Y) or not open_claim(rows[-1]):
        problems.append(f"lab-save: {rows[-1:]}, not in the lab at {BALL_X},{BALL_Y} with no Pokemon and the claim open")
    if open(lab_save, "rb").read() == open(args.save, "rb").read():
        problems.append("lab-save: the game's save is unchanged (SAVE did not write it)")
    # 4. the claim, then SAVE (pictures: the ball faced, the 18-starter screen, YES, the received line, the save)
    claim_dir, _, rows = step("claim", lab_save, dict(PKMN_TEST_BATTLE="field:2800",
                                                      PKMN_TEST_FIELD_KEYS=",".join(CLAIM + mc.save_keys(AFTER_CLAIM)),
                                                      PKMN_TEST_FIELD_SHOTS="60,300,560,760,1100,2200,2400"))
    claim_save = os.path.join(claim_dir, "test.sav")
    if not rows or rows[0]["map"] != lab or not open_claim(rows[0]):
        problems.append(f"claim: before the keys {rows[:1]}, not the lab save (no Pokemon, the claim open)")
    if not rows or not torchic_claimed(rows[-1]):
        problems.append(f"claim: after the keys {rows[-1:]}, not Torchic, the claim made ({claimed:#x}) and the flag")
    if any(row["party"] > 1 for row in rows):
        problems.append("claim: more than one Pokemon")
    personality = rows[-1]["personality"] if rows else None
    # 5. the claim's save loaded: the same Torchic
    _, _, rows = step("claim-load", claim_save, dict(PKMN_TEST_BATTLE="field:60"))
    if not rows or not torchic_claimed(rows[-1]) or rows[-1]["personality"] != personality or rows[-1]["map"] != lab:
        problems.append(f"claim-load: the saved state is {rows[-1:]}, not the claimed Torchic (personality {personality})")
    # 6. rerolls: claim, the soft reset key (MS-61), YES - again and again from the lab save, in one process
    reset_at = AFTER_CLAIM
    _, lines, _ = step("rerolls", lab_save, dict(PKMN_TEST_BATTLE=f"field:{reset_at + 700}",
                                                 PKMN_TEST_SOFT_RESET=str(args.rerolls),
                                                 PKMN_TEST_FIELD_KEYS=",".join(CLAIM + mc.reset_keys(reset_at)),
                                                 PKMN_TEST_FIELD_SHOTS=f"1100,{reset_at + 150},{reset_at + 420}"),
                       timeout=args.timeout * args.rerolls)
    rolls, after = [], []
    segment = []
    for line in lines + ["soft reset: end of the log"]:
        if line.startswith("soft reset: the game restarted") or line == "soft reset: end of the log":
            claims = [row for row in starter_lines(segment) if torchic_claimed(row)]
            if claims:
                rolls.append(dict(personality=claims[-1]["personality"], shiny=claims[-1]["shiny"]))
            segment = []
            continue
        if line.startswith("soft reset: back on the field"):
            rows = starter_lines(segment)
            after.append(rows[-1] if rows else None)
        segment.append(line)
    restarts = sum(line.startswith("soft reset: the game restarted") for line in lines)
    steps[-1].update(rolls=rolls, restarts=restarts)
    if restarts != args.rerolls or len(rolls) != args.rerolls:
        problems.append(f"rerolls: {len(rolls)} claims and {restarts} resets, not {args.rerolls} of each")
    for i, row in enumerate(after):
        if row is None or row["map"] != lab or not open_claim(row):
            problems.append(f"rerolls: back from reset {i + 1}: {row}, not the lab save (no Pokemon, the claim open)")
    if len(after) != restarts:
        problems.append(f"rerolls: back on the field {len(after)} times after {restarts} resets")
    if len({roll["personality"] for roll in rolls}) != len(rolls):
        problems.append(f"rerolls: a roll came back: {[roll['personality'] for roll in rolls]}")
    report = dict(exe=args.exe, steps=steps, problems=problems, passed=not problems)
    with open(os.path.join(args.out, "feature_check.json"), "w") as out:
        json.dump(report, out, indent=2)
    for entry in steps:
        print(json.dumps({k: v for k, v in entry.items() if k != "pictures"}), flush=True)
    print(json.dumps(dict(passed=not problems, problems=problems)), flush=True)
    sys.exit(0 if not problems else 1)


if __name__ == "__main__":
    main()
