#!/bin/sh
# Battle FX regression run (test build): plays animations at a battle's action menu, or fights a battle,
# and writes per-animation results (src/diorama/battlefx/README.md, Tests).
#
#   tools/battlefx/gallery.sh OUT SAVE BATTLE MOVES [VAR=VALUE ...]
#
#   OUT     a new folder for the run (the binary is copied into it first, so a rebuild during the run
#           cannot swap it; crash.<n>.log addresses symbolise against that copy)
#   SAVE    a save to start from (copied; the file itself is never written)
#   BATTLE  PKMN_TEST_BATTLE: wild:SPECIES:LEVEL, wild2:A:LA:B:LB, trainer:ID, double:ID, two:A:B, ...
#   MOVES   PKMN_BFX_TEST_MOVES: all, MOVE_A,MOVE_B,... or "" (with PKMN_BFX_TEST_ANIMS=..., or a fought battle:
#           PKMN_TEST_FIGHT=SECONDS PKMN_TEST_STRONG=1 PKMN_BFX_TEST_REAL_PARITY=1)
#
#   environment: EXE (default ./pokeemerald-mp-diorama-bfxtest), SCALE (window scale, default 2),
#   TIMEOUT (seconds per segment, default 7200), DISPLAY (an X display), MAXSEG (default 40)
#
# A run that crashes the game (exit >= 128, or 124: timed out) inside an animation is resumed just after it
# (PKMN_BFX_TEST_FROM=ID+); every crash is listed in OUT/out/crashes.jsonl and its logs kept as
# game.<n>.log / crash.<n>.log. Results: OUT/out/report.jsonl (galleries), real_report.jsonl (fought
# battles), summary.json; tools/battlefx/summarize.py reads them.
set -u
EXE=${EXE:-./pokeemerald-mp-diorama-bfxtest}
OUT=$1; SAVE=$2; BATTLE=$3; MOVES=$4; shift 4
mkdir -p "$OUT/out" || exit 1
OUT=$(cd "$OUT" && pwd)
DATA=$(cd "$(dirname "$EXE")" && pwd)/battlefx
cp "$EXE" "$OUT/game.bin" || exit 1
printf 'borderBackground=0\nbackgroundOrder=2\nfullscreen=0\nwindowScale=%s\nintegerScale=0\nvsync=0\nborder=1\nvolume=0\ncryVolume=0\n' \
  "${SCALE:-2}" > "$OUT/pokeemerald.cfg"
cd "$OUT" || exit 1
FROM=""
seg=0
while :; do
  cp "$SAVE" "$OUT/test.sav"; chmod u+w "$OUT/test.sav"
  rm -f "$OUT/crash.log"
  env SDL_AUDIODRIVER=dummy PKMN_TEST_BATTLE="$BATTLE" PKMN_TEST_AUTOPILOT=1 PKMN_TEST_BATTLE_DELAY=100 \
    PKMN_DIORAMA_LOG=1 PKMN_BFX_TEST_MOVES="$MOVES" PKMN_BFX_TEST_OUT="$OUT/out" PKMN_BFX_TEST_EXIT=1 \
    PKMN_BATTLEFX_LOG=1 PKMN_BATTLEFX_DIR="${PKMN_BATTLEFX_DIR:-$DATA}" PKMN_BFX_TEST_FROM="$FROM" \
    "$@" timeout "${TIMEOUT:-7200}" "$OUT/game.bin" --save test.sav > "$OUT/game.$seg.log" 2>&1
  rc=$?
  echo "$rc" > "$OUT/exit.$seg"
  [ -f "$OUT/crash.log" ] && mv "$OUT/crash.log" "$OUT/crash.$seg.log"
  if [ "$rc" -eq 0 ] || [ "$rc" -eq 6 ] || [ "$rc" -eq 4 ]; then break; fi
  LAST=$(grep -o 'Battle FX test: playing [A-Z0-9_]*' "$OUT/game.$seg.log" | tail -1 | awk '{print $5}')
  if [ -z "$LAST" ]; then
    echo "segment $seg ended with $rc outside any animation; stopping" >> "$OUT/runner.log"
    break
  fi
  printf '{"id":"%s","crashed":true,"exit":%s,"segment":%s}\n' "$LAST" "$rc" "$seg" >> "$OUT/out/crashes.jsonl"
  echo "segment $seg: exit $rc during $LAST; resuming after it" >> "$OUT/runner.log"
  FROM="$LAST+"
  seg=$((seg + 1))
  if [ "$seg" -ge "${MAXSEG:-40}" ]; then echo "too many segments" >> "$OUT/runner.log"; break; fi
done
ln -sf "game.$seg.log" "$OUT/game.log"
echo "$rc" > "$OUT/exit"
exit "$rc"
