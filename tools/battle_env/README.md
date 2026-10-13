# Battle environments: inventory, survey, room proofs

Tools for the 3D battles' environments: which maps a battle can start on, a picture of a real
battle on each, and the checks the rooms and caves made for them pass. Nothing here is
shipped; the survey needs a TEST build of the game.

| Tool | What it does |
| --- | --- |
| `inventory.py` | Every map of the Emerald game a battle can start on (236), from the game's own data: map headers, wild encounter tables, trainer and scripted battles (map scripts and the shared scripts they call), double battles, the co-op Gym battles, legendaries. Per map: category, map type, battle scene, tilesets, trainers, wild tables, weather, test positions (a walkable cell with room for the battle camera), what the Diorama rules and models raise there. Writes `build/battle_env/inventory.json`. |
| `survey.py` | Photographs a real battle on each map: copies a save with the continue-warp set to the map's test position, starts the game on an X display, presses Continue, and the TEST driver starts the battle (the map's own trainer, its legendary, a wild Pokemon from its own table). At the action menu the game writes `cap.png` (the window) and `cap.json` (what the battle camera sees, the environment interface's view, draw counts and frame times) and exits. |
| `sheets.py` | Contact sheets of a survey per category, for reviewing the pictures by eye; `--compare` puts another survey's picture beside each (before / after). |
| `cave_pins.py` | A cave tileset's rock stood up as cave wall: picks the metatiles that are impassable, have no behaviour, are not void, water or sky, are not named by the game's scripts and lie in rock masses, and writes the rules compiler's pins (`data/diorama/tilesets/<tileset>.json`). `--sheet` draws the chosen and the passed-over metatiles. |
| `auto_rooms.py` | Reads rooms off their cells (`tools/diorama_buildings/battle_room_auto.py`), builds them with the shared model library's generator and proves them: every model against its drawing, and each room composed with every model standing in it against its drawing, pixel for pixel. `--repair N` leaves out the pieces that claim the cells that differ and proves the room again. |
| `perf.py` | The survey's battles at 1080p, 1440p and 4K (each size on its own X display), the world redrawn every frame or kept; the game's own autopilot presses the keys (`survey.py --autopilot`). Writes `perf.json` and `perf.md`. Software OpenGL here: the times compare sizes and places, not graphics cards. |
| `report.py` | The per-map report from the inventory, a before and an after survey and `review.json` (the classes given by looking at the pictures): `report.md`, `report.json` and `page/index.html` (`report_page.py`: coverage, the classes, every map's pictures, filters). A map is reviewed (classed), tested (photographed, not classed), untested (no picture) or excluded (`"after": "n/a"`: the Battle Pyramid's squares). |
| `review.json` | The review: per map, the class before and after (A complete 3D, B needs polish, C partially 3D, D broken or mostly flat) and what the picture shows. Written by people (or Claude) looking at the survey's pictures, never by a script from the numbers. Per map: `before` and `after` (the first rounds), `props` (with the overworld's detail objects), `now` (round 3, `claude/battle-environments-2`) and `r4` (round 4, `claude/battle-environments-3`), each with its `_notes`; the class now is the latest round's that the map has. |

## The test build

The survey's driver is compiled only with `-DDIORAMA_BATTLE_ENV_TEST`
(`include/diorama/battle_env_test.h`, `src/diorama/scene/battle_env_test.c`, two hooks:
`src/overworld.c` `CB1_Overworld` and `src/battle_controller_player.c`
`HandleInputChooseAction`, both under `#if defined(ENABLE_DIORAMA) && defined(DIORAMA_BATTLE_ENV_TEST)`).
A player build has none of it.

    make -f Makefile_pc NATIVE_LINUX=1 DIORAMA=1 OBJ_DIR=build/pc-linux-diorama-envtest \
        TARGET=pokeemerald-mp-diorama-envtest CC="gcc -DDIORAMA_BATTLE_ENV_TEST"

The driver reads `PKMN_TEST_BATTLE` (`wild:SPECIES:LEVEL`, `wild2:S:L:S:L`,
`legendary:SPECIES:LEVEL`, `trainer:ID`, `double:ID`, `two:ID:ID`), `PKMN_TEST_BATTLE_DELAY`
(field frames before it starts), `PKMN_TEST_CAPTURE` (the stem of the files to write),
`PKMN_TEST_CAPTURE_DELAY` (frames at the action menu before the picture), `PKMN_TEST_EXIT`,
`PKMN_TEST_AUTOPILOT=1` (the game presses its own keys: Start, then A until the field, then B
until the action menu, each press held for three frames so a slow run misses none) and
`PKMN_TEST_TIMEOUT` (seconds: no picture by then, say so and exit with code 3). A trainer
battle locks the field and freezes its objects first, as a trainer's script does (a visible
wild Pokemon would otherwise walk into the player, or empty the party, during the transition).

## A survey, start to end

    python3 tools/battle_env/inventory.py
    Xvfb :99 -screen 0 1920x1080x24 &
    python3 tools/battle_env/survey.py --exe ./pokeemerald-mp-diorama-envtest --save town.sav \
        --out build/battle_env/after [--category gym,elite_four | --maps MAP_ROUTE101,...] \
        [--hour 12] [--skip-done]
    python3 tools/battle_env/sheets.py build/battle_env/after build/battle_env/sheets \
        --compare build/battle_env/before

Any save with a party of two or more will do (`town.sav`; no save is written back). The game
runs on the host's clock: `--hour` sets the time zone so the battles are in daylight (or at
night). One game at a time: two at once on two cores make both too slow for the timeouts.
`--kind trainer|wild_land|wild_water|legendary|wild` picks the battle, `--spec` another battle
(`trainer:ID`, `double:ID`, `partner:...`, `wild2:...`, `legendary:SPECIES:LEVEL`, `none`: the
map's own trainers see the player) with `--label` its run's name, `--at X,Y` another cell to
stand on, `--cfg-extra 'cameraMode=diorama\n'` the Diorama camera. `--warp-in` is for a map the
game does not resume on (a secret base comes back in Littleroot Town): the TEST driver warps
there in game first (`PKMN_TEST_WARP`, as the debug menu's warp does; in memory only, no save
written), to the map's warp 0 or `--at`'s cell. `--autopilot` lets the game press its own keys
(no xdotool), `--fight SECONDS` fights the battle through after the picture (A on the first move;
`--env PKMN_TEST_STRONG=1` a party at level 100 that knows Flamethrower) and records how it ended
(won, lost, crashed). A run whose game resumed on another map than the one asked for is recorded
as `wrong map`, not as a picture of that map. Under water the survey presses no B in the field
before the battle (B is the game's "surface?" there).

`cap.json` holds, besides the camera:

* `band`: the shares of the picture's upper band (where the scenery shows) that are void (no
  world), floor, upright (walls, faces), slope, edge, raised above the floor, and the scenery's
  depth (median, 90th percentile) - read from the depth buffer of a fresh draw;
* `env`: the environment interface's view (battle_environment.h) and its check - each
  battler's ground taken back to the window lands on its standing point (`foot_error_px`, and
  by the camera's matrix `matrix_error_px`);
* `perf`: the world's draw calls (terrain, buildings), triangles and the battle's frame times
  so far (kept world, redrawn world, the camera's flight).

The measured shares only point at a picture to look at: every environment is classified by
looking at its picture (docs/battle-environments.md in the app repository has the report).

## Rooms and caves

The shared model library (`tools/diorama_buildings`) takes the battle rooms at the end of its
specs: the Elite Four's and the Champion's rooms are modelled piece by piece
(`battle_room_specs.py`), the other rooms are read off their cells (`battle_room_auto.py`).
A room goes into `battle_room_specs.AUTO_ROOMS` only when `auto_rooms.py` proves it, with the
list in the order it was proved (a room stands the pieces of the rooms built before it that it
repeats: Mt. Pyre's lower floors stand the graves read on 5F); a piece the proof finds wrong goes
into `AUTO_DROP` and the rest of the room stands. An auto room's pieces stand only in the rooms
of that list (`reuse_in`), never in a room nobody proved. A blocked component that does not
fill its rectangle (a maze, a ring of rock) is read as runs of cells each with one foot, so no
piece lays its drawn top back over the floor between its arms as a slab.

What the proof cannot see: it compares the picture the GBA's camera sees. A piece that gives
back its drawing exactly can still be wrong from the battle camera's angle - the survey's
pictures are what show that (Lavaridge Gym 1F's sand ridges were one slab over the room before
the one-foot rule; the stage search found nowhere to stand and the battle looked into a wall;
the Battle Palace's arena is impassable, so its carpet stood up as a block behind the battlers
until its pieces went into `AUTO_DROP`).

    python3 tools/battle_env/auto_rooms.py build/battle_env/rooms LAYOUT_MT_PYRE_1F --repair 2 --previews

Caves are not modelled: their tilesets' rock is pinned as cliffs for the rules compiler
(`data/diorama/tilesets/cave.json`, `meteor_falls.json`, `rusturf_tunnel.json`,
`navel_rock.json`), so it stands up wherever those tilesets are drawn, in the field and in
battles alike.

    python3 tools/battle_env/cave_pins.py gTileset_MeteorFalls --sheet /tmp/mf.png --write

Under the sea (`gTileset_Underwater`) the water's blue is the floor every metatile is drawn over,
so the water test would pass over all of the rock: `--sea-floor` leaves it out
(`data/diorama/tilesets/underwater.json`).

## The report

    python3 tools/battle_env/report.py --before build/battle_env/before --after build/battle_env/after \
        --out build/battle_env/report --extra night=build/battle_env/night \
        --meta "branch, commit, date" --appendix notes.html

`page/index.html` is a page with no `<html>` of its own (a browser shows it as it is); with
`img/` beside it, it is the whole report.
