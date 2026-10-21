#!/usr/bin/env python3
"""Data-integrity check (Update 29): no Pokemon species data, move data or fixed encounter level was changed.

    tools/pc_port/species_data_check.py [--binary ./pokeemerald-mp] [--base COMMIT] [--update-baseline]

Two independent checks:

  1. The built game dumps a 64-bit fingerprint of every enabled species (base stats, types, catch rate, experience, EVs,
     abilities, gender ratio, egg data, friendship, growth rate, legendary/mythical flags, level-up, teachable and egg move
     lists, evolutions with their conditions): `pokeemerald-mp --dump-species-data FILE`. It must equal the committed
     baseline tools/pc_port/baselines/species-data.txt, taken from the engine before Update 29 (5aaf2ba2).
  2. With --base COMMIT (default: the baseline commit named in the baseline's sibling file), `git diff --name-only` between
     that commit and the working tree must not list a protected data file: species and learnset data, move data, ability
     data, trainer and party data, the maps' wild encounter tables (levels and slots), the species/move/item constants.

Exit 0: unchanged. Exit 1: the differences are listed.
"""
import argparse
import fnmatch
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BASELINE = os.path.join(ROOT, "tools", "pc_port", "baselines", "species-data.txt")
BASE_COMMIT = "5aaf2ba2e082995022bf0db3d3b43e1c2ec5ef1b"

# Files whose content is Pokemon data, move mechanics, trainer levels or the maps' own wild level tables.
PROTECTED = (
    "src/data/pokemon/*", "src/data/pokemon_graphics/*", "src/data/moves_info.h", "src/data/abilities.h",
    "src/data/battle_move_effects.h", "src/data/trainers.h", "src/data/trainers.party", "src/data/trainer_parties.h",
    "src/data/wild_encounters.h", "src/data/wild_encounters.json", "src/data/battle_frontier/*",
    "include/constants/species.h", "include/constants/moves.h", "include/constants/abilities.h",
    "include/constants/pokemon.h", "src/data/starter*", "data/maps/*/scripts.inc",
)
# Reviewed exceptions: files that match a protected pattern but were changed on purpose, with the reason.
ALLOWED = {
    # Update 29: the coin counter's 5,000-coin pack and the wallet's room check (PC branches under .ifdef PORTABLE). No encounter,
    # level, species or trainer data.
    "data/maps/MauvilleCity_GameCorner/scripts.inc": "Game Corner coin counter (coins economy)",
    # Update 31: the five May/Brendan battle scenes ask the multiplayer layer first (two lines under .ifdef PORTABLE at the top
    # of each scene's entry points). The scenes' trainers, parties, levels and rewards are unchanged; the GBA ROM is unchanged.
    "data/maps/Route103/scripts.inc": "Update 31 rival checkpoint hook (Route 103 rival)",
    "data/maps/RustboroCity/scripts.inc": "Update 31 rival checkpoint hooks (Rustboro rival and its trigger line)",
    "data/maps/Route104/scripts.inc": "Update 31 rival checkpoint hooks (Route 104 rival and trigger)",
    "data/maps/Route110/scripts.inc": "Update 31 rival checkpoint hook (Route 110 trigger line)",
    "data/maps/Route119/scripts.inc": "Update 31 rival checkpoint hook (Route 119 trigger line)",
    "data/maps/LilycoveCity/scripts.inc": "Update 31 rival checkpoint hook (Lilycove rival)",
    # The Mega Evolution check's fix (8906fb9b): Mega Chimecho's base HP follows P_UPDATED_STATS like Chimecho's (a Mega
    # keeps its base form's HP), in the PC build and the test ROM; the release GBA ROM keeps 75.
    "src/data/pokemon/species_info/gen_3_families.h": "Mega Chimecho's base HP follows Chimecho's (PC build, test ROM)",
}
# Reviewed fingerprint changes: species whose data changed on purpose (the fingerprint the change gives, the reason).
ALLOWED_SPECIES = {
    "1555": ("907cd87b71f0b328", "SPECIES_CHIMECHO_MEGA: base HP 65 with P_UPDATED_STATS, as Chimecho (8906fb9b)"),
}


def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, **kw)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--binary", default=os.path.join(ROOT, "pokeemerald-mp"))
    ap.add_argument("--base", default=BASE_COMMIT)
    ap.add_argument("--update-baseline", action="store_true")
    ap.add_argument("--skip-git", action="store_true")
    args = ap.parse_args()
    problems = []
    if not os.path.exists(args.binary):
        print(f"species_data_check: {args.binary} not found (make -f Makefile_pc NATIVE_LINUX=1)")
        return 2
    with tempfile.TemporaryDirectory() as tmp:
        out = os.path.join(tmp, "species.txt")
        result = run(["xvfb-run", "-a", os.path.abspath(args.binary), "--dump-species-data", out]) if not os.environ.get("DISPLAY") \
            else run([os.path.abspath(args.binary), "--dump-species-data", out])
        if not os.path.exists(out):
            print("species_data_check: the game wrote no dump\n" + result.stdout[-400:] + result.stderr[-400:])
            return 2
        now = open(out).read().splitlines()
    if args.update_baseline:
        open(BASELINE, "w").write("\n".join(now) + "\n")
        print(f"species_data_check: baseline rewritten ({len(now)} species)")
        return 0
    base = open(BASELINE).read().splitlines()
    if len(now) != len(base):
        problems.append(f"species count {len(base)} -> {len(now)}")
    base_map = dict(line.split(" ", 1) for line in base)
    now_map = dict(line.split(" ", 1) for line in now)
    for species in sorted(set(base_map) | set(now_map), key=int):
        if base_map.get(species) != now_map.get(species) and ALLOWED_SPECIES.get(species, (None,))[0] != now_map.get(species):
            problems.append(f"species {species}: fingerprint {base_map.get(species)} -> {now_map.get(species)}")
    if not args.skip_git:
        diff = run(["git", "diff", "--name-only", args.base, "--"], cwd=ROOT)
        if diff.returncode != 0:
            problems.append("git diff failed: " + diff.stderr.strip()[:200])
        for name in diff.stdout.split():
            if name in ALLOWED:
                continue
            if any(fnmatch.fnmatch(name, pattern) for pattern in PROTECTED):
                problems.append(f"protected data file changed since {args.base[:8]}: {name}")
    if problems:
        print("species_data_check: FAILED")
        for p in problems[:40]:
            print("  " + p)
        return 1
    print(f"species_data_check: ok ({len(base)} species fingerprints equal the pre-update baseline, {len(ALLOWED_SPECIES)} reviewed "
          f"change; no protected data file changed but the {len(ALLOWED)} reviewed ones)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
