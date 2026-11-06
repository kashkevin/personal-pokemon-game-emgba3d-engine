#!/bin/sh
# Regression check (MS-200): in a double battle where one side has a single Pokemon, the game draws that Pokemon at the
# singles place (battle_interface.c GetBattlerCoordsIndex), 40 pixels from the doubles one (24 for the foe's side); the
# core gets each battler's place from the game (bfx_game.c: the request's coords), so every compared frame of the effect
# sprites is the game's.
#
#   tools/battlefx/lone_check.sh [OUT]     needs the test build (make -f Makefile_pc NATIVE_LINUX=1 DIORAMA=1 BFX_TEST=1)
#                                          and xvfb-run; a few minutes on software OpenGL (timeout 900 s a run)
#
# From the debug field scene: its lone Mudkip against two wild Pokemon, as the attacker and as the target; a trainer's
# lone Pokemon (trainer 10's Poochyena) against the player's two (PKMN_DEBUG_PARTY=2), as the target and as the
# attacker. Moves placed from the attacker (Ember, Focus Energy, Calm Mind, Swords Dance, Lunge, Power Trip) and from
# both battlers (Giga Drain, Strength Sap; Lunge and Power Trip were among the moves past the package that failed). The
# HD layer is not drawn (the replay and its parity are the same with it). Exit 0: every animation's every compared frame
# equal; 1: not (the runs' report.jsonl say where; their copies of the program are kept for crash logs).
set -u
OUT=${1:-build/lone-check}
HERE=$(cd "$(dirname "$0")" && pwd)
EXE=${EXE:-./pokeemerald-mp-diorama-bfxtest}
MOVES=MOVE_EMBER,MOVE_FOCUS_ENERGY,MOVE_CALM_MIND,MOVE_SWORDS_DANCE,MOVE_LUNGE,MOVE_POWER_TRIP,MOVE_GIGA_DRAIN,MOVE_STRENGTH_SAP
rm -rf "${OUT:?}"; mkdir -p "$OUT"
run() { # name battle [VAR=VALUE ...]
  n=$1; b=$2; shift 2
  EXE=$EXE SCALE=1 TIMEOUT=900 MAXSEG=1 xvfb-run -a -s "-screen 0 1280x800x24" sh "$HERE/gallery.sh" "$OUT/$n" - "$b" \
    "$MOVES" PKMN_BFX_TEST_VIEW=gba "$@" > "$OUT/$n.out" 2>&1
}
run player wild2:263:30:261:30 &
sleep 5
run player_target wild2:263:30:261:30 PKMN_BFX_TEST_ATTACKER=1 PKMN_BFX_TEST_TARGET=0 &
wait
run foe double:10 PKMN_DEBUG_PARTY=2 &
sleep 5
run foe_attacker double:10 PKMN_DEBUG_PARTY=2 PKMN_BFX_TEST_ATTACKER=1 PKMN_BFX_TEST_TARGET=0 &
wait
python3 - "$OUT" player player_target foe foe_attacker <<'EOF'
import json
import sys
from pathlib import Path

out, runs = Path(sys.argv[1]), sys.argv[2:]
bad = 0
for name in runs:
    rep, problems = out / name / "out" / "report.jsonl", out / name / "out" / "problems.txt"
    rows = [json.loads(l) for l in rep.read_text().splitlines() if l.strip()] if rep.exists() else []
    exact = [r for r in rows if r["parity"]["compared"] > 0 and r["parity"]["equal"] == r["parity"]["compared"]
             and not r.get("failed") and not r.get("stuck")]
    ok = len(rows) == 8 and len(exact) == len(rows) and not problems.exists()
    bad += not ok
    print("%-13s %d of %d animations every compared frame equal%s%s" % (
        name, len(exact), len(rows), "" if len(rows) == 8 else " (8 expected)",
        "; " + problems.read_text().strip() if problems.exists() else ""))
    for r in rows:
        if r not in exact:
            p = r["parity"]
            print("  %s: %d of %d frames equal, first different %d: game [%s] core [%s]" % (
                r["id"], p["equal"], p["compared"], p["firstBad"], p["game"][:80], p["core"][:80]))
print("lone_check: %s" % ("passed" if not bad else "FAILED"))
sys.exit(1 if bad else 0)
EOF
rc=$?
# the runs' copies of the program (150 MB each) are kept only for a failure's crash logs (gallery.sh)
[ "$rc" -eq 0 ] && rm -f "${OUT:?}"/*/game.bin
exit "$rc"
