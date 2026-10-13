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
# save's own lead alone (STRONG=0, PKMN_TEST_LEAD_ONLY: a battle lost, exit 4; with the whole party the
# autopilot would stop at the party menu once the lead faints), a shiny wild Zigzagoon (PKMN_TEST_SHINY: its stars as it
# appears), Fabian's Manectric holding its Mega Stone (PKMN_TEST_MEGA: the trainer's AI Mega Evolves it), and
# a Safari Zone battle with a wild Zigzagoon (catching: A throws Safari Balls - the trainer's throw, the shakes,
# a break-out or the catch - until it is caught or flees; exit 4, the outcome not a win), Wally's catch tutorial
# (PKMN_TEST_BATTLE=tutorial: his own controller plays it - his Zigzagoon's Tackles, then he comes out and throws
# the ball that catches the Ralts; exit 4, caught - unless his Tackles knock the Ralts out first: exit 0, won). The player's own
# Mega Evolution needs the move menu's Mega trigger and a Pokeblock throw the Pokeblock case (the autopilot
# only presses A): the gallery plays those animations (B_ANIM_MEGA_EVOLUTION, B_ANIM_POKEBLOCK_THROW at a
# Safari battle's menu). Then battles fought with another first move (PKMN_TEST_FIRST_MOVE) and the lead alone
# (should it faint, the battle is lost rather than stopped at the party menu): Roxanne with Fury Cutter and with
# Rollout (animations that follow a counter over consecutive turns), a wild
# Zigzagoon with Metronome (the move it calls is the game's own choice: its animation follows Metronome's),
# Roxanne with Mirror Move (the copied move's animation), a wild Zigzagoon with Secret Power (the
# environment's version), Sidney with Fury Attack (a hit's animation for every hit: his Pokemon take several),
# a route trainer with Fly (both turns: the user away, then the strike); and the multiplayer twin Gym partners whose Pokemon start
# a sandstorm (Shale's Hippopotas) and rain (Juan's partner's): the weather's animations every turn (rain's with the lead
# alone and Thunderbolt: its Water Pokemon outlast Flamethrower in the rain, and once the lead faints the autopilot,
# which only presses A, stays at the party menu). Last, wild
# battles in four other 3D environments (PKMN_TEST_WARP, as the debug menu's warp): Granite Cave, Rustboro's Gym
# (indoors), Route 119 by the Weather Institute (where the warp arrives) and Mt. Chimney; Secret Power in two of
# them (the game picks its animation by the battle environment: the cave's, and plain ground's on Route 119); and
# a move past the package's 621 (Fire Lash): the HD layer refuses it (logged) and the game's own animation shows.
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
run defeat       "trainer:335" 0 PKMN_TEST_LEAD_ONLY=1 "$@"
run shiny_wild   "wild:263:30" 1 PKMN_TEST_SHINY=1 "$@"
run mega_trainer "trainer:759" 1 PKMN_TEST_MEGA=1 "$@"
run safari_catch "safari:263:30" 1 "$@"
run catch_tutorial "tutorial:0" 0 "$@"
run fury_cutter  "trainer:265" 1 PKMN_TEST_FIRST_MOVE=210 PKMN_TEST_LEAD_ONLY=1 "$@"
run rollout      "trainer:265" 1 PKMN_TEST_FIRST_MOVE=205 PKMN_TEST_LEAD_ONLY=1 "$@"
run metronome    "wild:263:30" 1 PKMN_TEST_FIRST_MOVE=118 PKMN_TEST_LEAD_ONLY=1 "$@"
run mirror_move  "trainer:265" 1 PKMN_TEST_FIRST_MOVE=119 PKMN_TEST_LEAD_ONLY=1 "$@"
run secret_power "wild:263:30" 1 PKMN_TEST_FIRST_MOVE=290 PKMN_TEST_LEAD_ONLY=1 "$@"
run multi_hit    "trainer:261" 1 PKMN_TEST_FIRST_MOVE=31 PKMN_TEST_LEAD_ONLY=1 "$@"
run two_turn     "trainer:318" 1 PKMN_TEST_FIRST_MOVE=19 PKMN_TEST_LEAD_ONLY=1 "$@"
run sandstorm    "trainer:855" 1 "$@"
run rain         "trainer:862" 1 PKMN_TEST_FIRST_MOVE=85 PKMN_TEST_LEAD_ONLY=1 "$@"
run cave         "wild:263:30" 1 PKMN_TEST_WARP=24:7:0 "$@"
run indoor_gym   "wild:263:30" 1 PKMN_TEST_WARP=11:3:0 "$@"
run route119     "wild:263:30" 1 PKMN_TEST_WARP=0:34:0 "$@"
run volcano      "wild:263:30" 1 PKMN_TEST_WARP=24:12:0 "$@"
run secret_power_cave  "wild:263:30" 1 PKMN_TEST_FIRST_MOVE=290 PKMN_TEST_LEAD_ONLY=1 PKMN_TEST_WARP=24:7:0 "$@"
run secret_power_route119 "wild:263:30" 1 PKMN_TEST_FIRST_MOVE=290 PKMN_TEST_LEAD_ONLY=1 PKMN_TEST_WARP=0:34:0 "$@"
run past_621     "wild:263:30" 1 PKMN_TEST_FIRST_MOVE=643 PKMN_TEST_LEAD_ONLY=1 "$@"
echo done >> "$OUT/realsuite.txt"
