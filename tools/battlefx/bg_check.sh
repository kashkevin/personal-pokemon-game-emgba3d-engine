#!/bin/sh
# Regression check (Update 27): the moves whose animation backgrounds left a black block over the player's Pokemon
# (gba_easy_draw.c: the picture's rows 112-160 of a move's background, mirrored) play with none.
#
#   tools/battlefx/bg_check.sh [OUT]       needs the test build (make -f Makefile_pc NATIVE_LINUX=1 DIORAMA=1 BFX_TEST=1),
#                                          xvfb-run and Pillow; about 6 minutes on software OpenGL (timeout 900 s)
#
# Plays six package moves, two battle animations and four moves past the package (the game's own animation in the 3D
# view) from the debug field scene, then tools/battlefx/black_rect_check.py reads the captures.
set -u
OUT=${1:-build/bg-check}
HERE=$(cd "$(dirname "$0")" && pwd)
EXE=${EXE:-./pokeemerald-mp-diorama-bfxtest}
rm -rf "$OUT"; mkdir -p "$OUT"
run() { # name moves [VAR=VALUE]
  n=$1; m=$2; shift 2
  EXE=$EXE SCALE=1 TIMEOUT=900 MAXSEG=1 xvfb-run -a -s "-screen 0 1280x800x24" sh "$HERE/gallery.sh" "$OUT/$n" - wild:263:30 "$m" "$@" > "$OUT/$n.out" 2>&1 &
}
run hd "MOVE_AURA_SPHERE,MOVE_ASSURANCE,MOVE_HURRICANE,MOVE_FOCUS_BLAST,MOVE_GIGA_IMPACT,MOVE_HYPER_FANG"
run anim "" PKMN_BFX_TEST_ANIMS=B_ANIM_TRICK_ROOM,B_ANIM_MAGIC_ROOM
# 816 Armor Cannon, 684 Baddy Bad, 838 Mighty Cleave, 822 Blazing Torque (numbers: tools/battlefx/move_numbers.json)
run gba "" PKMN_BFX_TEST_MOVE_NUMS=816,684,838,822
wait
python3 "$HERE/black_rect_check.py" "$OUT/hd" "$OUT/anim" "$OUT/gba"
