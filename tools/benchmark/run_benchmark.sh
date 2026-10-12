#!/bin/bash
# Pokemon Emerald native PC - benchmark suite, development runner (Linux; the
# Windows one is run-benchmark.bat). See tools/benchmark/README.md.
#
#   tools/benchmark/run_benchmark.sh GAME SCENES_DIR OUT_DIR [quick|full] [SCENE...]
#
# GAME: the 3D build (pokeemerald-mp-diorama, or the .exe with GAME_RUNNER=wine).
# SCENES_DIR: the scene saves (private; not in Git). OUT_DIR: results.csv and a .log
# per scene. DISPLAY must name an X display. Optional SCENE names run only those.
set -u
game=$(readlink -f "$1"); scenes=$(readlink -f "$2"); out=$3; mode=${4:-full}; shift 4 || shift $#
only=" $* "
runner=${GAME_RUNNER:-}
secs=10; heights=1080,1440,2160
if [ "$mode" = quick ]; then secs=6; heights=1080,2160; fi
mkdir -p "$out"; out=$(readlink -f "$out")

config() { # FILE DETAILS
    { echo cameraMode=adventure; echo resolution=1920x1080; echo displayMode=windowed
      if [ "$2" = 0 ]; then printf "quality=custom\nshadows=medium\nlighting=1\neffects=1\ndetails=0\nantialiasing=0\n"
      else echo quality=high; fi; } > "$1"
}

scene() { # NAME SAVE DETAILS [ENV=VALUE]
    local name=$1 save=$2 details=$3 env=${4:-}
    if [ "$only" != "  " ] && [[ "$only" != *" $name "* ]]; then return; fi
    local w=$out/work-$name
    rm -rf "$w"; mkdir -p "$w"
    if ! cp "$scenes/$save.sav" "$w/test.sav" 2>/dev/null; then echo "  $name: $save.sav missing - skipped"; return; fi
    chmod 600 "$w/test.sav"
    config "$w/pokeemerald.cfg" "$details"
    echo "  $name ..."
    (cd "$(dirname "$game")" && env $env timeout 900 $runner "$game" --save "$w/test.sav" --config "$w/pokeemerald.cfg" \
        --benchmark $secs --bench-name "$name" --bench-heights $heights --bench-log "$out/$name.log" \
        --bench-csv "$out/results.csv" > "$out/$name-game.txt" 2>&1)
    [ -f "$out/$name.log" ] || echo "  $name: no result (see $name-game.txt)"
}

if [ "$mode" = quick ]; then
    scene route101 route101 1
    scene rustboro rustboro 1
    scene dense-objects route110 1
    scene heavy-rain route104 1 PKMN_DIORAMA_WEATHER=13
else
    scene route101 route101 1
    scene petalburg-woods woods 1
    scene rustboro rustboro 1
    scene slateport-market slateport-market 1
    scene mauville mauville 1
    scene granite-cave granite 1
    scene dewford-coast dewford 1
    scene dense-objects route110 1
    scene jagged-pass jagged 1
    scene night-town mauville 1 PKMN_CLOCK_HOUR=22
    scene heavy-rain route104 1 PKMN_DIORAMA_WEATHER=13
    scene route101-details-off route101 0
    scene rustboro-details-off rustboro 0
    scene dense-objects-details-off route110 0
fi
echo "Done: $out"
