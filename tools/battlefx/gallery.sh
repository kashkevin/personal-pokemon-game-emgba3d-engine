#!/bin/sh
# Battle FX regression run (test build): plays animations at a battle's action menu, or fights a battle,
# and writes per-animation results (src/diorama/battlefx/README.md, Tests).
#
#   tools/battlefx/gallery.sh OUT SAVE BATTLE MOVES [VAR=VALUE ...]
#
#   OUT     a new folder for the run (the binary and its battlefx data folder are copied into it first, so a
#           rebuild during the run cannot swap them; crash.<n>.log addresses symbolise against that copy)
#   SAVE    a save to start from (copied; the file itself is never written); "-": no save, the debug field scene
#           (PKMN_DEBUG_SCENE=field, a fresh game's field with a Level 30 Mudkip: src/platform/debug_scene.c) and the
#           autopilot's keys - what the cloud runs of Update 27 used
#   BATTLE  PKMN_TEST_BATTLE: wild:SPECIES:LEVEL, wild2:A:LA:B:LB, trainer:ID, double:ID, two:A:B, ...
#   MOVES   PKMN_BFX_TEST_MOVES: all, MOVE_A,MOVE_B,... or "" (with PKMN_BFX_TEST_ANIMS=..., or a fought battle:
#           PKMN_TEST_FIGHT=SECONDS PKMN_TEST_STRONG=1 PKMN_BFX_TEST_REAL_PARITY=1)
#
#   environment: EXE (default ./pokeemerald-mp-diorama-bfxtest), SCALE (window scale, default 2),
#   TIMEOUT (seconds per segment, default 7200), DISPLAY (an X display), MAXSEG (default 40)
#
# A run that crashes the game (exit >= 128, or 124: timed out) inside an animation is resumed just after it
# (PKMN_BFX_TEST_FROM=ID+); every crash is listed in OUT/out/crashes.jsonl and its logs kept as
# game.<n>.log / crash.<n>.log. An animation whose game animation does not end (exit 3: this battle lacks
# what it waits for, a Safari Zone's Pokeblock throw at a wild battle's menu) is in report.jsonl with
# "stuck": true, and the run goes on after it in a new game (what it left running would hold every
# animation after it). Results: OUT/out/report.jsonl (galleries), real_report.jsonl (fought battles),
# summary.json; tools/battlefx/summarize.py reads them.
set -u
EXE=${EXE:-./pokeemerald-mp-diorama-bfxtest}
OUT=$1; SAVE=$2; BATTLE=$3; MOVES=$4; shift 4
mkdir -p "$OUT/out" || exit 1
OUT=$(cd "$OUT" && pwd)
DATA=${PKMN_BATTLEFX_DIR:-$(cd "$(dirname "$EXE")" && pwd)/battlefx}
cp "$EXE" "$OUT/game.bin" || exit 1
rm -rf "$OUT/battlefx" && cp -r "$DATA" "$OUT/battlefx" || exit 1
printf 'borderBackground=0\nbackgroundOrder=2\nfullscreen=0\nwindowScale=%s\nintegerScale=0\nvsync=0\nborder=1\nvolume=0\ncryVolume=0\n' \
  "${SCALE:-2}" > "$OUT/pokeemerald.cfg"
cd "$OUT" || exit 1
FROM=""
seg=0
while :; do
  if [ "$SAVE" = "-" ]; then
    SAVEARGS=""
    SCENE="PKMN_DEBUG_SCENE=field PKMN_DEBUG_HERE=0,16,12,14"
  else
    cp "$SAVE" "$OUT/test.sav"; chmod u+w "$OUT/test.sav"
    SAVEARGS="--save test.sav"
    SCENE="PKMN_NOSCENE=1"
  fi
  rm -f "$OUT/crash.log"
  env $SCENE SDL_AUDIODRIVER=dummy PKMN_TEST_BATTLE="$BATTLE" PKMN_TEST_AUTOPILOT=1 PKMN_TEST_BATTLE_DELAY=100 PKMN_TEST_FREEZE_FIELD=1 \
    PKMN_DIORAMA_LOG=1 PKMN_BFX_TEST_MOVES="$MOVES" PKMN_BFX_TEST_OUT="$OUT/out" PKMN_BFX_TEST_EXIT=1 \
    PKMN_BATTLEFX_LOG=1 PKMN_BATTLEFX_DIR="$OUT/battlefx" PKMN_BFX_TEST_FROM="$FROM" \
    "$@" timeout "${TIMEOUT:-7200}" "$OUT/game.bin" $SAVEARGS > "$OUT/game.$seg.log" 2>&1
  rc=$?
  echo "$rc" > "$OUT/exit.$seg"
  # the battle asked for, not another (a wild Pokemon of the field's walking into the player first)
  grep -q "Battle env test: starting" "$OUT/game.$seg.log" || \
    echo "segment $seg: the battle asked for ($BATTLE) did not start; its results are not that battle's" >> "$OUT/out/problems.txt"
  [ -f "$OUT/crash.log" ] && mv "$OUT/crash.log" "$OUT/crash.$seg.log"
  if [ "$rc" -eq 0 ] || [ "$rc" -eq 6 ] || [ "$rc" -eq 4 ]; then break; fi
  LAST=$(grep -o 'Battle FX test: playing [A-Z0-9_]*' "$OUT/game.$seg.log" | tail -1 | awk '{print $5}')
  if [ -z "$LAST" ]; then
    echo "segment $seg ended with $rc outside any animation; stopping" >> "$OUT/runner.log"
    break
  fi
  if [ "$rc" -eq 3 ]; then
    echo "segment $seg: $LAST did not end (stuck); going on after it" >> "$OUT/runner.log"
  else
    printf '{"id":"%s","crashed":true,"exit":%s,"segment":%s}\n' "$LAST" "$rc" "$seg" >> "$OUT/out/crashes.jsonl"
    echo "segment $seg: exit $rc during $LAST; resuming after it" >> "$OUT/runner.log"
  fi
  FROM="$LAST+"
  seg=$((seg + 1))
  if [ "$seg" -ge "${MAXSEG:-80}" ]; then echo "too many segments" >> "$OUT/runner.log"; break; fi
done
ln -sf "game.$seg.log" "$OUT/game.log"
echo "$rc" > "$OUT/exit"
exit "$rc"
