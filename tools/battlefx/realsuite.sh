#!/bin/sh
# Battle FX in battles fought to their end (test build): the battle environment test driver's autopilot
# (PKMN_TEST_FIGHT presses A: the first move on the first target; PKMN_TEST_STRONG=1 gives the party level
# 100 and Flamethrower) fights each battle, and the Battle FX driver records the parity of every animation
# the battle itself plays (real_report.jsonl: the game's effect sprites against the core's, every frame)
# and captures of each (a quarter, half and three quarters through). Each battle is a gallery.sh run.
#
#   tools/battlefx/realsuite.sh OUT SAVE [VAR=VALUE ...]
#
#   OUT   a new folder: OUT/<battle>/ per battle, OUT/realsuite.txt (each battle's exit and outcome)
#   SAVE  a save to start from (copied; never written)
#   environment: EXE (default ./pokeemerald-mp-diorama-bfxtest), DISPLAY, SCALE, TIMEOUT (per battle)
#
# The battles (this game's trainer ids): a wild Zigzagoon, a route trainer, Roxanne (gym), Sidney (Elite
# Four), Tate & Liza (a gym double battle), a wild double battle, two trainers at once, Wallace with the
# save's own party (STRONG=0: a battle lost, exit 4), a shiny wild Zigzagoon (PKMN_TEST_SHINY: its stars as it
# appears), Fabian's Manectric holding its Mega Stone (PKMN_TEST_MEGA: the trainer's AI Mega Evolves it), and
# a Safari Zone battle with a wild Zigzagoon (catching: A throws Safari Balls - the trainer's throw, the shakes,
# a break-out or the catch - until it is caught or flees; exit 4, the outcome not a win). The player's own
# Mega Evolution needs the move menu's Mega trigger and a Pokeblock throw the Pokeblock case (the autopilot
# only presses A): the gallery plays those animations (B_ANIM_MEGA_EVOLUTION, B_ANIM_POKEBLOCK_THROW at a
# Safari battle's menu). Then battles fought with another first move (PKMN_TEST_FIRST_MOVE): Roxanne with
# Fury Cutter and Sidney with Rollout (animations that follow a counter over consecutive turns), a wild
# Zigzagoon with Metronome (the move it calls is the game's own choice: its animation follows Metronome's),
# Roxanne with Mirror Move (the copied move's animation), a wild Zigzagoon with Secret Power (the
# environment's version), a route trainer with Fury Attack (a hit's animation for every hit) and with Fly
# (both turns: the user away, then the strike); and the multiplayer twin Gym partners whose Pokemon start
# a sandstorm (Shale's Hippopotas) and rain (Juan's partner's): the weather's animations every turn.
set -u
OUT=$1; SAVE=$2; shift 2
HERE=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$OUT" || exit 1
OUT=$(cd "$OUT" && pwd)
: > "$OUT/realsuite.txt"
run() { # name battle strong
  name=$1; battle=$2; strong=$3; shift 3
  TIMEOUT=${TIMEOUT:-1500} sh "$HERE/gallery.sh" "$OUT/$name" "$SAVE" "$battle" "" PKMN_TEST_FIGHT=1200 \
    PKMN_TEST_STRONG="$strong" PKMN_TEST_EXIT=1 PKMN_BFX_TEST_REAL_PARITY=1 PKMN_BFX_TEST_REAL_SHOTS=1 "$@"
  echo "$name $battle strong=$strong exit $(cat "$OUT/$name/exit") $(grep -o 'battle over, outcome [0-9] ([a-z]*)' "$OUT/$name/game.log" | head -1)" >> "$OUT/realsuite.txt"
}
run wild_single  "wild:263:30" 1 "$@"
run trainer      "trainer:318" 1 "$@"
run gym_roxanne  "trainer:265" 1 "$@"
run e4_sidney    "trainer:261" 1 "$@"
run gym_double   "double:271" 1 "$@"
run wild_double  "wild2:263:30:261:30" 1 "$@"
run two_trainers "two:318:319" 1 "$@"
run defeat       "trainer:335" 0 "$@"
run shiny_wild   "wild:263:30" 1 PKMN_TEST_SHINY=1 "$@"
run mega_trainer "trainer:759" 1 PKMN_TEST_MEGA=1 "$@"
run safari_catch "safari:263:30" 1 "$@"
run fury_cutter  "trainer:265" 1 PKMN_TEST_FIRST_MOVE=210 "$@"
run rollout      "trainer:261" 1 PKMN_TEST_FIRST_MOVE=205 "$@"
run metronome    "wild:263:30" 1 PKMN_TEST_FIRST_MOVE=118 "$@"
run mirror_move  "trainer:265" 1 PKMN_TEST_FIRST_MOVE=119 "$@"
run secret_power "wild:263:30" 1 PKMN_TEST_FIRST_MOVE=290 "$@"
run multi_hit    "trainer:318" 1 PKMN_TEST_FIRST_MOVE=31 "$@"
run two_turn     "trainer:318" 1 PKMN_TEST_FIRST_MOVE=19 "$@"
run sandstorm    "trainer:855" 1 "$@"
run rain         "trainer:862" 1 "$@"
echo done >> "$OUT/realsuite.txt"
