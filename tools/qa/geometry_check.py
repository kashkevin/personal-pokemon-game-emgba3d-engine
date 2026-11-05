#!/usr/bin/env python3
"""Every map of the Emerald game looked at in 3D by a tool (step 16, the world geometry overhaul: the app's
docs/3d-world-geometry-20261029.md). TEST BUILDS ONLY (DIORAMA=1 BFX_TEST=1: the geometry probe,
include/diorama/geometry_probe.h).

    tools/qa/geometry_check.py tour --out build/qa/geo/tour.txt [--maps A,B] [--kinds outdoor,cave] [--sample N]
    tools/qa/geometry_check.py run --exe ./pokeemerald-mp-diorama-bfxtest --save S.sav --tour T --out build/qa/geo \\
        [--jobs 2] [--chunk 40] [--xvfb]
    tools/qa/geometry_check.py check --out build/qa/geo [--sheets]

tour: the places to stand, from the geometry inventory (tools/qa/geometry_inventory.py: the 503 maps the game shows)
and each map's layout: as few as cover every cell of the map with the 3D view's 81 x 81 cells (40 either side of the
player, 52 rows ahead - north - and 28 behind), each on a cell a player can stand on. run: the tour in game processes
of --chunk places each (a process that stops is started again after the place it stopped at), each place's cells
(cells/<key>.tsv.gz) and pictures (shots/<key>-<camera>.png) under OUT. check: what the cells and the pictures show,
per place and per map, in OUT/manifest.json and OUT/report.md:

    fin          terrain standing three quarters of a tile or more over both its neighbours on a side, in a run of
                 three cells or more (outdoors, caves): the owner's "tall, narrow, rectangular cliffs"
    spike        the same, one or two cells alone (a rock, or a broken piece of cliff)
    flat-blocked a cell no one walks on, drawn flat on the ground (not water, a model, a tree, a detail object or the
                 void): a rock, a fence, a counter... lying as a picture (outdoors, caves and rooms)
    card-tree    a tree cell whose crown is the 3DS project's flat cut-out (a solid crown and trunk since step 16)
    walk-step    two cells the player walks between, their ground a third of a tile or more apart (no ramp, no
                 stairs, no ledge): a step the player climbs as if it were not there; "seam" when the two belong to
                 two maps
    water-wall   water standing a fifth of a tile or more over the land beside it (the blue box)
    water-drop   walkable land three quarters of a tile or more over the water beside it
    hole         pixels of the probe's magenta sky enclosed by the scene (not pockets that open onto its far edge,
                 drawn in the edge fog's colour), or anywhere in the steep view: the scene has a gap
    not-3d       the place was drawn in 2D, or its scene never completed
A map is covered when every one of its cells was in some place's scene.

The tour leaves out the maps no player reaches on PC (NOT_REACHED: the Union Room and the link rooms - no Wireless
Adapter or link cable -, the e-Reader trainer's room, the original introduction's truck, the contest halls' shared
events map; --with-unreachable, or naming them in --maps, puts them back as warps), and enters the rooms a warp cannot
show as play does through their facilities (FACILITY_ENTRIES, KNOWN_ISSUES MS-98: the Battle Tower's multi partner
room and the Battle Pike's room, each in a game process of its own with FACILITY_PARTY; diorama/geometry_probe.h).
tools/qa/map_coverage.py turns the check's manifest into the coverage of every map of the game's map list.
"""
import argparse
import collections
import concurrent.futures
import gzip
import json
import os
import re
import shutil
import signal
import struct
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "tools", "battle_env"))
import mega_check as mc  # noqa: E402  (the game in a folder of its own, the short introduction)
sys.path.insert(0, HERE)
import importlib.util  # noqa: E402
_spec = importlib.util.spec_from_file_location("qa_geometry_inventory", os.path.join(HERE, "geometry_inventory.py"))
geometry_inventory = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(geometry_inventory)

AHEAD, BEHIND, SIDE = 52, 28, 40         # scene_snapshot.h: the grid round the player
CAMERAS = "a,d,s15,s75"
SECONDS_PER_PLACE = 45                   # a software renderer's worst; a process is given its places' share
STALL_SECONDS = 240                      # none of its places finished for this long: the process stopped
CHECKS = ("fin", "spike", "flat-blocked", "card-tree", "walk-step", "seam", "water-wall", "water-drop", "hole",
          "not-3d")
WATER_BEHAVIOR = re.compile(r"WATER|SEAWEED|CURRENT|NO_SURFACING|WATERFALL|PUDDLE|HOT_SPRINGS")
UNSTANDABLE = re.compile(r"WATERFALL|CURRENT|NO_SURFACING|^MB_JUMP_")     # gameplay_check.py's BLOCKED

# The maps of the game the sweep leaves out: no player reaches them on PC - "pc": the PC lacks what the GBA's way in
# needs; "never": nothing leads there on any machine. Read from the engine's code; each reason names it, and its label
# is the coverage report's short form (tools/qa/map_coverage.py).
_LINK_ROOM = ("a link room: entered only after a link-up with another GBA over the link cable (data/scripts/"
              "cable_club.inc: TryBattleLinkup, TryTradeLinkup or TryRecordMixLinkup has to succeed; src/cable_club.c "
              "Task_LinkupAwaitConnection waits for a second linked player) or from the Union Room (src/union_room.c "
              "WarpForCableClubActivity). The PC has no link cable and no Wireless Adapter; its own trades and battles "
              "between players never use this room")
NOT_REACHED = {
    "UnionRoom": dict(
        kind="pc", label="no Wireless Adapter on PC",
        reason="the Union Room attendant (data/scripts/cable_club.inc CableClub_EventScript_UnionRoomAttendant) lets a "
               "player in only when IsWirelessAdapterConnected, which is FALSE on PC (src/link.c: no GBA Wireless "
               "Adapter): \"The Wireless Adapter isn't connected\". Her warpspinenter is the room's only way in (the "
               "Pokemon Centers' Union Room scan is off on PC too: src/link.c ShouldCheckForUnionRoom). Warped in "
               "directly, the room's wireless partner search faults (KNOWN_ISSUES MS-98)"),
    "TradeCenter": dict(kind="pc", label="no link cable on PC", reason=_LINK_ROOM),
    "RecordCorner": dict(kind="pc", label="no link cable on PC", reason=_LINK_ROOM),
    "BattleColosseum_2P": dict(kind="pc", label="no link cable on PC", reason=_LINK_ROOM),
    "BattleColosseum_4P": dict(kind="pc", label="no link cable on PC", reason=_LINK_ROOM),
    "SootopolisCity_MysteryEventsHouse_B1F": dict(
        kind="pc", label="no e-Reader trainer on PC",
        reason="its stairs open only while an e-Reader trainer is stored (SootopolisCity_MysteryEventsHouse_1F's "
               "OnTransition: frontier_checkvisittrainer, then setmaplayoutindex ..._STAIRS_UNBLOCKED), and that "
               "trainer comes only from an e-Reader card over the link cable (src/mystery_event_script.c) or a Mystery "
               "Gift over wireless (src/mystery_gift_client.c): a PC receives neither (no link cable; the Mystery "
               "Gift's wireless search says the Wireless Adapter isn't connected, src/union_room.c)"),
    "InsideOfTruck": dict(
        kind="pc", label="the PC's short introduction",
        reason="the original introduction's first room: the PC game starts a new character in Littleroot Town through "
               "its short introduction (src/multiplayer_rival.c MultiplayerIntro_IsEnabled: the truck only with "
               "PKMN_ORIGINAL_INTRO=1, a development switch)"),
    "ContestHall": dict(
        kind="never", label="nothing leads there",
        reason="no warp, script or C code leads to MAP_CONTEST_HALL: it holds the events and scripts the five contest "
               "halls of each category share (their map.json shared_events_map), and those are the halls a contest "
               "enters (LilycoveCity_ContestLobby: setwarp MAP_CONTEST_HALL_COOL and the others)"),
}

# The rooms a warp cannot show as play does (KNOWN_ISSUES MS-98): each entered through its facility instead, in a
# game process of its own (the challenge's state and its save stay there). The probe starts at `start` facing the
# attendant; A through every line, question (its first choice: CHALLENGE, LV. 50, YES) and battle; the frontier's
# party screen takes the party's first Pokemon; on the field of a map of `steps`, that map's action (A: talk, U/D/L/R:
# walk, pike: the room of three paths' door by its hint). `at` is where the facility leaves the player; the cells and
# pictures are taken on the first frame the field is free there, as for a warp.
FACILITY_PARTY = "6:50,9:50,3:50"     # Charizard, Blastoise, Venusaur at level 50: Lv. 50 challenges, none banned
FACILITY_SECONDS = 900                # a facility's way in (its talk, menus, save, rooms, a battle) at most
FACILITY_ENTRIES = {
    "BattleFrontier_BattleTowerMultiPartnerRoom": dict(
        at=(10, 2), start=("BattleFrontier_BattleTowerLobby", 14, 6, "U"),
        steps=[("BattleFrontier_BattleTowerLobby", "A")],
        how="a Battle Tower multi battle challenge (Lv. 50, two Pokemon) from the lobby's multi battle attendant: her "
            "questions, the party screen, the save, the elevator, then the partner room's welcome"),
    "BattleFrontier_BattlePikeRoomNormal": dict(
        at=(4, 7), start=("BattleFrontier_BattlePikeLobby", 5, 6, "U"),
        steps=[("BattleFrontier_BattlePikeLobby", "A"), ("BattleFrontier_BattlePikeThreePathRoom", "pike")],
        how="a Battle Pike challenge (Lv. 50, three Pokemon) from its attendant: her questions, the party screen, the "
            "save, the corridor, the room of three paths and the door its hint points to (another one when the hinted "
            "room is the wild Pokemon's, a map of its own); the pictures as the player steps in, before the room's "
            "event (a trainer, a healer, a Pokemon's status move) starts"),
}


def enum_values(path, name):
    """{value: member} of a C enum in a header (consecutive, explicit values honoured)."""
    text = open(path, encoding="utf-8").read()
    body = re.search(r"enum\s+" + name + r"\s*\{(.*?)\};", text, re.S).group(1)
    out, value = {}, 0
    for line in re.sub(r"/\*.*?\*/|//[^\n]*", "", body, flags=re.S).split(","):
        line = line.strip()
        if not line:
            continue
        if "=" in line:
            line, number = (part.strip() for part in line.split("=", 1))
            value = int(number, 0)
        out[value] = line
        value += 1
    return out


RULES_H = os.path.join(ROOT, "include", "diorama", "rules.h")
SHAPES = {v: k.replace("DIORAMA_SHAPE_", "") for v, k in enum_values(RULES_H, "DioramaShape").items()}
ARCHETYPES = {v: k.replace("DIORAMA_ARCHETYPE_", "") for v, k in enum_values(RULES_H, "DioramaArchetype").items()}
MOUNTAIN_ART = 1 << 5                    # rules.h DIORAMA_MEASURED_MOUNTAIN_ART
SOURCE_VALID = 1 << 0                    # scene_snapshot.h DIORAMA_CELL_SOURCE_VALID
# what stands as something other than terrain (its own model, a tree, furniture...): no fin, spike or flat check
OBJECT_ARCHETYPES = {"ROUND_HULL", "GROUPED_HULL", "TREE", "STUMP", "FOREST_WALL", "SHRUB", "HEDGE", "BUILDING",
                     "ROOF", "AWNING", "TOP_SLAB", "CLAIM_ONLY", "BILLBOARD", "CUTOUT", "CONSOLE", "SIGNPOST", "POST",
                     "COUNTER", "TABLE", "DESK", "BED", "BOOKCASE", "RELIEF", "VOID"}
OBJECT_SHAPES = {"BUILDING_PART", "ROOF", "BILLBOARD", "CUTOUT", "HIDDEN"}
WATER_ARCHETYPES = {"WATER", "SHALLOW_WATER", "CURRENT", "HOT_SPRING"}
STEP_SHAPES = {"LEDGE", "STAIRS", "BRIDGE"}


# --- the tour ----------------------------------------------------------------------------------------------------

def map_numbers(root):
    groups = json.load(open(os.path.join(root, "data", "maps", "map_groups.json")))
    return {name: (g, n) for g, group in enumerate(groups["group_order"]) for n, name in enumerate(groups[group])}


def layout_cells(root, layout_id):
    layouts = {l["id"]: l for l in json.load(open(os.path.join(root, "data", "layouts", "layouts.json")))["layouts"]}
    layout = layouts[layout_id]
    width, height = layout["width"], layout["height"]
    data = open(os.path.join(root, layout["blockdata_filepath"]), "rb").read()
    return width, height, struct.unpack(f"<{width * height}H", data[:width * height * 2])


def standable(blocks, width, x, y, water):
    block = blocks[y * width + x]
    return not block & 0x0C00 and 3 <= block >> 12 <= 14 and not water(block)


def places_for(root, row, behaviours):
    """The places that put every cell of a map in some scene: a tiling of the layout by the scene's window, each
    centre moved to the nearest cell a player stands on (land; else any open cell), and more where cells are left."""
    width, height, blocks = layout_cells(root, row["layout"])
    attributes = {}

    def water(block):
        return bool(WATER_BEHAVIOR.search(behaviours(row, block & 0x3FF, attributes)))

    land = [(x, y) for y in range(height) for x in range(width) if standable(blocks, width, x, y, water)]
    # open water too (a warp onto it puts the player on Surf), not a current, a waterfall or a ledge
    sea = [(x, y) for y in range(height) for x in range(width)
           if not blocks[y * width + x] & 0x0C00 and water(blocks[y * width + x])
           and not UNSTANDABLE.search(behaviours(row, blocks[y * width + x] & 0x3FF, attributes))]
    open_cells = land + sea or [(x, y) for y in range(height) for x in range(width)
                                if not blocks[y * width + x] & 0x0C00] or [(width // 2, height // 2)]
    land_set = set(land)

    def window(px, py):
        return {(x, y) for y in range(max(0, py - AHEAD), min(height, py + BEHIND + 1))
                for x in range(max(0, px - SIDE), min(width, px + SIDE + 1))}

    def nearest(tx, ty):
        # land unless the nearest land is more than ten cells further than the nearest water
        return min(open_cells, key=lambda c: (c[0] - tx) ** 2 + (c[1] - ty) ** 2 + (0 if c in land_set else 100))

    want = set((x, y) for y in range(height) for x in range(width))
    xs = [min(width - 1, SIDE + i * (2 * SIDE + 1)) for i in range(max(1, -(-width // (2 * SIDE + 1))))]
    ys = [min(height - 1, AHEAD + i * (AHEAD + BEHIND + 1)) for i in range(max(1, -(-height // (AHEAD + BEHIND + 1))))]
    chosen = []
    for ty in ys:
        for tx in xs:
            # a single place in a small map: its middle, the scene's window reaching every cell
            cx = width // 2 if width <= 2 * SIDE + 1 else tx
            cy = min(max(height - 1 - BEHIND, height // 2), AHEAD) if height <= AHEAD + BEHIND + 1 else ty
            spot = nearest(cx, cy)
            if spot not in chosen:
                chosen.append(spot)
    seen = set().union(*(window(*c) for c in chosen))
    left = want - seen
    while left and len(chosen) < 12:
        tx, ty = min(left, key=lambda c: (c[1], c[0]))
        spot = nearest(min(width - 1, tx + SIDE // 2), min(height - 1, ty + AHEAD // 2))
        if spot in chosen:
            break
        chosen.append(spot)
        seen |= window(*spot)
        left = want - seen
    return chosen, len(left)


def facility_place(root, row, numbers):
    """A room's place entered through its facility (FACILITY_ENTRIES): its tour line, and the cells of the map the
    scene's window round the place does not reach."""
    entry = FACILITY_ENTRIES[row["name"]]
    group, num = numbers[row["name"]]
    x, y = entry["at"]
    start, sx, sy, facing = entry["start"]
    steps = ",".join(f"{numbers[name][0]}.{numbers[name][1]}={action}" for name, action in entry["steps"])
    width, height, _ = layout_cells(root, row["layout"])
    seen = sum(1 for cy in range(max(0, y - AHEAD), min(height, y + BEHIND + 1))
               for cx in range(max(0, x - SIDE), min(width, x + SIDE + 1)))
    line = (f"{row['name']}_{x}_{y} {group} {num} {x} {y} enter {numbers[start][0]}.{numbers[start][1]}:{sx},{sy}:"
            f"{facing} {steps}")
    return line, width * height - seen


def is_facility(place):
    """A tour line (split) entered through its facility."""
    return len(place) > 5 and place[5] == "enter"


def tour(args):
    root = args.root
    maps = [m for m in geometry_inventory.geometry_inventory(root) if m["in_game"]]
    left_out = []
    if args.maps:
        wanted = set(args.maps.split(","))
        maps = [m for m in maps if m["name"] in wanted]
    elif not args.with_unreachable:
        left_out = sorted(m["name"] for m in maps if m["name"] in NOT_REACHED)
        maps = [m for m in maps if m["name"] not in NOT_REACHED]
    if args.kinds:
        maps = [m for m in maps if m["kind"] in args.kinds.split(",")]
    numbers = map_numbers(root)
    names = geometry_inventory.behaviour_names(root)
    tilesets = {symbol: geometry_inventory.read_u16(path)
                for symbol, path in geometry_inventory.tileset_roots(root).items() if os.path.exists(path)}

    def behaviours(row, metatile, cache):
        table = tilesets.get(row["primary_tileset"] if metatile < 512 else row["secondary_tileset"], [])
        index = metatile if metatile < 512 else metatile - 512
        return names.get(table[index] & 0xFF, "?") if index < len(table) else "?"

    lines, uncovered, entered = [], {}, 0
    # the maps a script sets up last: a hang there costs only the places after it (the facilities' rooms the very
    # last: each a game process of its own)
    maps.sort(key=lambda m: (m["name"] in FACILITY_ENTRIES, m["entry"].startswith("scripted"),
                             m["kind"] == "secret_base", numbers[m["name"]]))
    if args.sample > 1:
        # every sample-th map in this order (each kind and part of the world, a slow machine's representative
        # share), the facilities' rooms always
        maps = [m for i, m in enumerate(maps) if i % args.sample == 0 or m["name"] in FACILITY_ENTRIES]
    for m in maps:
        group, num = numbers[m["name"]]
        if m["name"] in FACILITY_ENTRIES:
            line, left = facility_place(root, m, numbers)
            lines.append(line)
            entered += 1
        else:
            spots, left = places_for(root, m, behaviours)
            for x, y in spots:
                lines.append(f"{m['name']}_{x}_{y} {group} {num} {x} {y}")
        if left:
            uncovered[m["name"]] = left
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, "w") as out:
        out.write("# key group num x y [enter <g>.<n>:<x>,<y>:<facing> <steps>] (tools/qa/geometry_check.py tour)\n"
                  + "\n".join(lines) + "\n")
    print(f"{len(lines)} places for {len(maps)} maps ({entered} entered through their facilities); cells no place "
          f"reaches: {sum(uncovered.values())} in {len(uncovered)} maps {sorted(uncovered)[:10]}; left out, as no "
          f"player reaches them on PC: {len(left_out)} {left_out}")
    return 0


# --- the run -----------------------------------------------------------------------------------------------------

def read_tour(path):
    return [line.split() for line in open(path) if line.strip() and not line.startswith("#")]


def finished_keys(out):
    path = os.path.join(out, "results.tsv")
    return {line.split("\t", 1)[0] for line in open(path)} if os.path.exists(path) else set()


def run_chunk(args, places, first, last, index):
    """One game process for tour lines first..last-1: until it ends or stops. Returns its exit code, the place it
    stopped at (when it did not end by itself) and the places it did not finish. A place entered through its facility
    runs alone, with the party its challenge takes (FACILITY_PARTY, as PKMN_TEST_GEO_PARTY) and its way in's time
    (FACILITY_SECONDS)."""
    rundir = os.path.join(args.out, "runs", f"{index:03d}")
    facility = any(is_facility(places[i]) for i in range(first, last))
    env = dict(os.environ, PKMN_TEST_BATTLE="field:100000000", PKMN_TEST_AUTOPILOT="1",
               PKMN_TEST_BATTLE_DELAY="100", PKMN_TEST_PARTY="280:50", PKMN_TEST_FIELD_KEYS="",
               PKMN_TEST_OWE_NO_BATTLE="1", PKMN_TEST_BADGES="8", PKMN_TEST_GEO_TOUR=os.path.abspath(args.tour),
               PKMN_TEST_GEO_OUT=os.path.abspath(args.out), PKMN_TEST_GEO_CAMERAS=args.cameras,
               PKMN_TEST_GEO_FIRST=str(first), PKMN_TEST_GEO_LAST=str(last), SDL_AUDIODRIVER="dummy",
               PKMN_NOSCENE="1", PKMN_TEST_EXIT="1", PKMN_TEST_LOG=os.path.join(rundir, "test.log"))
    if facility:
        # the party the challenge takes, given as the probe starts the way in (a tour's places never reach the battle
        # set-up that would apply PKMN_TEST_PARTY: the save's new character has no Pokemon)
        env["PKMN_TEST_GEO_PARTY"] = FACILITY_PARTY
    # the air's dust, rays and gloom (gl_air.c) are drawn over the scene, not part of it: off unless asked for (the
    # dust reads as stars in a cave's void, the rays lift its black, and neither is geometry)
    env.setdefault("PKMN_DIORAMA_AIR", "0")
    prefix = ["xvfb-run", "-a", "-s", "-screen 0 1280x720x24 -nolisten tcp"] if args.xvfb else []
    shutil.rmtree(rundir, ignore_errors=True)
    os.makedirs(rundir)
    shutil.copy(args.save, os.path.join(rundir, "test.sav"))
    with open(os.path.join(rundir, "pokeemerald.cfg"), "w") as cfg:
        cfg.write(mc.CONFIG)
    with open(os.path.join(rundir, "game.log"), "w") as output:
        # a session of its own: stopped, the process and what it started (xvfb-run's server) go together, and only they
        process = subprocess.Popen(prefix + [os.path.abspath(args.exe), "--save", "test.sav"], cwd=rundir, env=env,
                                   stdout=output, stderr=subprocess.STDOUT, start_new_session=True)
        # stopped when it ends, runs past its places' share, or goes STALL_SECONDS without finishing a place
        deadline = time.monotonic() + 120 + (FACILITY_SECONDS if facility else SECONDS_PER_PLACE * (last - first))
        stall = FACILITY_SECONDS if facility else STALL_SECONDS
        mine = {places[i][0] for i in range(first, last)}
        done_before, progress = len(mine & finished_keys(args.out)), time.monotonic()
        code = None
        while True:
            try:
                code = process.wait(timeout=5)
                break
            except subprocess.TimeoutExpired:
                pass
            done_now = len(mine & finished_keys(args.out))
            if done_now != done_before:
                done_before, progress = done_now, time.monotonic()
            if time.monotonic() > deadline or time.monotonic() - progress > stall:
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                process.wait()
                code = None
                break
    done = finished_keys(args.out)
    log = open(os.path.join(rundir, "game.log"), errors="replace").read()
    started = [int(m.group(1)) for m in re.finditer(r"Geometry probe: place (\d+) ", log)]
    left = [i for i in range(first, last) if places[i][0] not in done]
    stopped_at = started[-1] if started and left and started[-1] in left else None
    return code, stopped_at, left


def compress_cells(out):
    """The finished places' dumps (a place's result line is written after its dump is closed): another game still
    running may be writing its own, which compressed then would be cut short."""
    folder = os.path.join(out, "cells")
    done = finished_keys(out)
    for name in os.listdir(folder) if os.path.isdir(folder) else []:
        if name.endswith(".tsv") and name[:-4] in done:
            path = os.path.join(folder, name)
            with open(path, "rb") as source, gzip.open(path + ".gz", "wb") as target:
                shutil.copyfileobj(source, target)
            os.remove(path)


def run(args):
    places = read_tour(args.tour)
    os.makedirs(args.out, exist_ok=True)
    if args.make_save:
        args.save = mc.make_save(argparse.Namespace(out=args.out, exe=args.exe, display=os.environ.get("DISPLAY")),
                                 os.path.abspath(args.make_save))
    if not args.save:
        sys.exit("--save or --make-save")
    args.save = os.path.abspath(args.save)
    done = finished_keys(args.out)
    # --chunk places a process; a place entered through its facility alone in one
    ranges, first = [], 0
    while first < len(places):
        last = first + 1
        while not is_facility(places[first]) and last < len(places) and last - first < args.chunk \
                and not is_facility(places[last]):
            last += 1
        ranges.append((first, last))
        first = last
    # a run into an --out with results goes on with the places that have none (each run of them), not whole chunks
    # again
    pending = []
    for first, last in ranges:
        i = first
        while i < last:
            if places[i][0] in done:
                i += 1
                continue
            j = i
            while j < last and places[j][0] not in done:
                j += 1
            pending.append((i, j))
            i = j
    ranges = pending
    given_up, index = [], len(os.listdir(os.path.join(args.out, "runs"))) \
        if os.path.isdir(os.path.join(args.out, "runs")) else 0
    # --minutes: no game process started after that (the one running finishes its places); the rest left for a
    # later run into the same --out
    stop_at = time.monotonic() + args.minutes * 60 if args.minutes else None
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        running = {}
        while ranges or running:
            if stop_at is not None and time.monotonic() > stop_at and ranges:
                print(f"--minutes {args.minutes} reached: {sum(last - first for first, last in ranges)} places left "
                      "for a later run", flush=True)
                ranges = []
                if not running:
                    break
            while ranges and len(running) < args.jobs:
                first, last = ranges.pop(0)
                running[pool.submit(run_chunk, args, places, first, last, index)] = (first, last)
                index += 1
            finished, _ = concurrent.futures.wait(running, return_when=concurrent.futures.FIRST_COMPLETED)
            for future in finished:
                first, last = running.pop(future)
                code, stopped_at, left = future.result()
                note = ""
                if stopped_at is not None:
                    # the place it stopped at is not tried again; the rest of its places are
                    given_up.append(places[stopped_at][0])
                    note = f", stopped at {places[stopped_at][0]}"
                    if stopped_at + 1 < last:
                        ranges.append((stopped_at + 1, last))
                elif left and left[0] > first:
                    # it stopped between two places (a cutscene kept the field): on from the next
                    note = f", stopped after {places[left[0] - 1][0]}"
                    ranges.append((left[0], last))
                elif left:
                    note = f", {len(left)} places not finished (not tried again)"
                    given_up.extend(places[i][0] for i in left)
                print(f"places {first}-{last - 1}: exit {code}{note}", flush=True)
            compress_cells(args.out)
    # a facility's way in that did not get there (a challenge's battle lost, its time gone) is tried again in a fresh
    # game process (its draws are the clock's), alone: no other process writes the results meanwhile
    for i, place in enumerate(places):
        tries = 1
        while is_facility(place) and tries < args.facility_tries and result_status(args.out, place[0]) in (
                None, "not-reached"):
            drop_result(args.out, place[0])
            code, stopped_at, left = run_chunk(args, places, i, i + 1, index)
            index += 1
            tries += 1
            print(f"place {i} ({place[0]}), try {tries}: exit {code}, {result_status(args.out, place[0])}", flush=True)
            compress_cells(args.out)
    # (with what earlier runs into this --out left, a place done since taken off)
    done = finished_keys(args.out)
    path = os.path.join(args.out, "stopped.txt")
    before = open(path).read().split() if os.path.exists(path) else []
    given_up = [key for key in dict.fromkeys(before + given_up) if key not in done]
    with open(path, "w") as out:
        out.write("\n".join(given_up) + ("\n" if given_up else ""))
    print(f"done: {len(finished_keys(args.out))} of {len(places)} places; not finished: {len(given_up)}")
    return 0


def result_status(out, key):
    """The status of a place's result line (ok, unsettled, not-3d, other-map, not-reached), or None without one."""
    path = os.path.join(out, "results.tsv")
    status = None
    for line in open(path) if os.path.exists(path) else ():
        fields = line.rstrip("\n").split("\t")
        if fields[0] == key and len(fields) > 9:
            status = fields[9]
    return status


def drop_result(out, key):
    """A place's result line, cells and pictures taken away (it is run again)."""
    path = os.path.join(out, "results.tsv")
    if os.path.exists(path):
        lines = [line for line in open(path) if line.split("\t", 1)[0] != key]
        with open(path, "w") as handle:
            handle.writelines(lines)
    for name in (os.path.join(out, "cells", key + ".tsv"), os.path.join(out, "cells", key + ".tsv.gz")):
        if os.path.exists(name):
            os.remove(name)


# --- the check ---------------------------------------------------------------------------------------------------

def read_cells(path):
    opener = gzip.open if path.endswith(".gz") else open
    with opener(path, "rt") as text:
        header = text.readline()
        columns = text.readline().rstrip("\n").split("\t")
        cells = {}
        for line in text:
            values = line.rstrip("\n").split("\t")
            row = dict(zip(columns, values))
            for key in columns:
                if key in ("ground", "top", "feature", "takeoff", "landing"):
                    row[key] = float(row[key])
                else:
                    row[key] = int(row[key])
            row["shape_name"] = SHAPES.get(row["shape"], "?")
            row["archetype_name"] = ARCHETYPES.get(row["archetype"], "?")
            top = row["top"]
            if row["measured"] & MOUNTAIN_ART:
                top = max(top, row["ground"] + row["pixmax"] / 16.0)
            row["surface"] = top
            cells[(row["x"], row["y"])] = row
    return header, cells


def is_object(c):
    # c["prop"]: a detail object (props.c: a fence, a sign, a lamp, a rock...) stands on the cell's ground
    return (c["model"] or c["archetype_name"] in OBJECT_ARCHETYPES or c["shape_name"] in OBJECT_SHAPES or c["void"]
            or c.get("prop", 0))


def is_water(c):
    return c["shape_name"] == "WATER" or c["archetype_name"] in WATER_ARCHETYPES


def cell_checks(cells, group, num, kind):
    """{check: set of (source x, source y) of the place's own map} for one place's scene."""
    found = collections.defaultdict(set)
    flat_metatiles = found.setdefault("_flat_metatiles", collections.Counter())
    own = {xy: c for xy, c in cells.items() if c["flags"] & SOURCE_VALID and c["group"] == group and c["num"] == num}

    def neighbour(xy, dx, dy):
        return cells.get((xy[0] + dx, xy[1] + dy))

    outdoors = kind in ("outdoor", "cave", "underwater")
    tall = set()
    for xy, c in own.items():
        if is_object(c) or is_water(c) or c["shape_name"] in STEP_SHAPES:
            continue
        for axis in (((1, 0), (-1, 0)), ((0, 1), (0, -1))):
            a, b = neighbour(xy, *axis[0]), neighbour(xy, *axis[1])
            if a is not None and b is not None and c["surface"] >= max(a["surface"], b["surface"]) + 0.75:
                tall.add(xy)
    # fins: runs of three or more such cells (8-connected), spikes the rest
    seen = set()
    for start in tall:
        if start in seen:
            continue
        run, todo = [], [start]
        seen.add(start)
        while todo:
            xy = todo.pop()
            run.append(xy)
            for dx in (-1, 0, 1):
                for dy in (-1, 0, 1):
                    other = (xy[0] + dx, xy[1] + dy)
                    if other in tall and other not in seen:
                        seen.add(other)
                        todo.append(other)
        if outdoors:
            found["fin" if len(run) >= 3 else "spike"].update((own[xy]["sx"], own[xy]["sy"]) for xy in run)
    for xy, c in own.items():
        if c.get("tree"):
            if c.get("crown", 1) != 2:      # the 3DS project's cut-out (the default trees), not a solid crown (C3)
                found["card-tree"].add((c["sx"], c["sy"]))
        elif c["collision"] and not is_object(c) and not is_water(c) and c["shape_name"] == "FLAT" \
                and c["surface"] - c["ground"] < 0.06 and c["metatile"] != 0:
            found["flat-blocked"].add((c["sx"], c["sy"]))
            flat_metatiles[c["metatile"]] += 1
    for xy, c in cells.items():
        if not (c["flags"] & SOURCE_VALID):
            continue
        for dx, dy in ((1, 0), (0, 1)):
            d = neighbour(xy, dx, dy)
            if d is None or not (d["flags"] & SOURCE_VALID):
                continue
            mine = [e for e in (c, d) if e["group"] == group and e["num"] == num]
            if not mine:
                continue
            if c["collision"] == 0 and d["collision"] == 0 and not is_water(c) and not is_water(d) \
                    and c["shape_name"] not in STEP_SHAPES and d["shape_name"] not in STEP_SHAPES \
                    and (c["elevation"] == d["elevation"] or c["elevation"] in (0, 15) or d["elevation"] in (0, 15)) \
                    and abs(c["ground"] - d["ground"]) >= 0.33:
                check = "seam" if (c["group"], c["num"]) != (d["group"], d["num"]) else "walk-step"
                found[check].add((mine[0]["sx"], mine[0]["sy"]))
            for w, land in ((c, d), (d, c)):
                if is_water(w) and not is_water(land) and not is_object(land) \
                        and w["archetype_name"] != "WATERFALL":
                    if w["top"] > land["surface"] + 0.2:
                        found["water-wall"].add((w["sx"], w["sy"]) if w in mine else (land["sx"], land["sy"]))
                    if land["collision"] == 0 and land["ground"] - w["top"] >= 0.75:
                        found["water-drop"].add((land["sx"], land["sy"]) if land in mine else (w["sx"], w["sy"]))
    return found


def picture_holes(path, steep, dark_open=False):
    """Pixels of the probe's magenta sky the scene encloses (not reaching the picture's edge), or, in the steep view
    (the scene fills the picture), any inside a tenth's margin. `dark_open`: a room's or a cave's black backdrop is
    open too (the void it stands in: the game's own clear colour there is near-black, not a hole in play)."""
    import numpy  # noqa: E402
    from PIL import Image, ImageDraw  # noqa: E402

    rgb = numpy.asarray(Image.open(path).convert("RGB"))
    sky = (rgb[:, :, 0] > 235) & (rgb[:, :, 1] < 30) & (rgb[:, :, 2] > 235)
    height, width = sky.shape
    if steep:
        return int(sky[height // 10:height - height // 10, width // 10:width - width // 10].sum())
    # (the probe's sky and the void's black meet in a pixel of both, r = b with no green: open too)
    blend = (rgb[:, :, 1] <= 2) & (numpy.abs(rgb[:, :, 0].astype(int) - rgb[:, :, 2].astype(int)) <= 3)
    passable = sky | (rgb.max(axis=2) == 0) | blend if dark_open else sky
    # the scene's far edge, drawn wholly in the edge fog's colour (the light's: the day's, the evening's, the rain's):
    # the sky in pockets along it is past the loaded scene, not a gap in it (step 16: the solid trees on the far
    # skyline). The fog's colour is the one most of the scene's topmost pixels have.
    scene = ~sky
    top = numpy.where(scene.any(axis=0), scene.argmax(axis=0), height)
    samples = [tuple(int(v) for v in rgb[y, x]) for x in range(0, width, 2) if top[x] < height - 3
               for y in range(top[x], top[x] + 3)]
    if samples:
        fog, count = collections.Counter(samples).most_common(1)[0]
        if count * 3 >= len(samples):
            passable = passable | numpy.all(rgb == numpy.array(fog, dtype=rgb.dtype), axis=2)
    mask = Image.fromarray(passable.astype(numpy.uint8) * 255).copy()   # (fromarray's image is read-only: no fill)
    # the open sky: what reaches the picture's edge, or its top quarter (a low view's horizon: the window's own top
    # rows are the game's, not the sky)
    edge = [(x, y) for x in range(width) for y in list(range(0, height // 4)) + [height - 1]] + \
           [(x, y) for y in range(height) for x in (0, width - 1)]
    for xy in edge:
        if mask.getpixel(xy) == 255:
            ImageDraw.floodfill(mask, xy, 128)      # the open sky: reaches the picture's edge
    return int(((numpy.asarray(mask) == 255) & sky).sum())


def check(args):
    out = args.out
    inventory_rows = {m["name"]: m for m in geometry_inventory.geometry_inventory(args.root)}
    numbers = map_numbers(args.root)
    by_number = {v: k for k, v in numbers.items()}
    tour_places = read_tour(args.tour) if args.tour else []
    results = {}
    if os.path.exists(os.path.join(out, "results.tsv")):
        for line in open(os.path.join(out, "results.tsv")):
            fields = line.rstrip("\n").split("\t")
            results[fields[0]] = dict(group=int(fields[1]), num=int(fields[2]), x=int(fields[3]), y=int(fields[4]),
                                      arrived=(int(fields[5]), int(fields[6])), cells=int(fields[8]),
                                      status=fields[9], shots=int(fields[10]), failure=fields[13],
                                      # (since MS-98's sweep) where the player stood, and how the place was reached
                                      stood=(int(fields[14]), int(fields[15])) if len(fields) > 15 else None,
                                      entry=fields[16] if len(fields) > 16 else "warp")
    stopped = set(open(os.path.join(out, "stopped.txt")).read().split()) if os.path.exists(
        os.path.join(out, "stopped.txt")) else set()
    maps = collections.OrderedDict()
    for name, row in inventory_rows.items():
        if row["in_game"]:
            maps[name] = dict(name=name, kind=row["kind"], cells=row.get("cells", 0), places=[], seen=set(),
                              defects={k: set() for k in CHECKS}, notes=[], flat_metatiles=collections.Counter())
    for key, place in sorted(results.items()):
        name = by_number.get((place["group"], place["num"]))
        if name not in maps:
            continue
        m = maps[name]
        entry = dict(key=key, x=place["x"], y=place["y"], status=place["status"], failure=place["failure"],
                     stood=place["stood"], entry=place["entry"], shots=[], holes={})
        m["places"].append(entry)
        if place["status"] == "not-reached":
            # its facility did not take the player there: nothing drawn to judge (not a defect of the 3D view)
            m["notes"].append(f"{key}: not reached through its facility: {place['failure']}")
            continue
        if place["status"] in ("not-3d", "other-map") or place["failure"] != "-":
            m["defects"]["not-3d"].add((place["x"], place["y"]))
            m["notes"].append(f"{key}: {place['status']} {place['failure']}")
        path = os.path.join(out, "cells", key + ".tsv.gz")
        if not os.path.exists(path):
            path = path[:-3]
        if os.path.exists(path) and place["cells"]:
            _, cells = read_cells(path)
            m["seen"] |= {(c["sx"], c["sy"]) for c in cells.values()
                          if c["flags"] & SOURCE_VALID and (c["group"], c["num"]) == (place["group"], place["num"])}
            for name_, where in cell_checks(cells, place["group"], place["num"], m["kind"]).items():
                if name_ == "_flat_metatiles":
                    m["flat_metatiles"].update(where)
                else:
                    m["defects"][name_] |= where
        for camera in args.cameras.split(","):
            shot = os.path.join(out, "shots", f"{key}-{camera}.png")
            if os.path.exists(shot):
                entry["shots"].append(os.path.relpath(shot, out))
                # (the steep view fills the picture outdoors; a room or a cave, smaller than the view, lies in the
                # void round it, the picture's edges open sky as in the other views)
                holes = picture_holes(shot, steep=camera.startswith("s") and int(camera[1:] or 0) >= 60
                                      and m["kind"] == "outdoor", dark_open=m["kind"] != "outdoor")
                entry["holes"][camera] = holes
                if holes >= args.hole_pixels:
                    m["defects"]["hole"].add((place["x"], place["y"]))
    for key in stopped:
        name = key.rsplit("_", 2)[0]
        if name in maps:
            maps[name]["notes"].append(f"{key}: the game stopped here")
            maps[name]["defects"]["not-3d"].add(tuple(int(v) for v in key.rsplit("_", 2)[1:]))
    manifest = []
    for name, m in maps.items():
        planned = [p for p in tour_places if p[0].rsplit("_", 2)[0] == name]
        reached = [p for p in m["places"] if p["status"] != "not-reached"]
        unreachable = NOT_REACHED.get(name)
        manifest.append(dict(
            map=name, kind=m["kind"], cells=m["cells"], covered=len(m["seen"]),
            places_planned=len(planned), places_done=len(reached),
            status=("not reachable on PC" if unreachable and not reached else
                    "not inspected" if not reached else
                    "inspected" if len(m["seen"]) >= m["cells"] else "partly inspected"),
            # how its places were reached: through its facility (FACILITY_ENTRIES) or a warp
            entry=("facility" if any(p["entry"] == "facility" for p in reached) else "warp" if reached else None),
            unreachable=unreachable,
            facility=FACILITY_ENTRIES[name]["how"] if name in FACILITY_ENTRIES else None,
            defects={k: len(v) for k, v in m["defects"].items() if v},
            examples={k: sorted(v)[:12] for k, v in m["defects"].items() if v},
            flat_blocked_metatiles={f"0x{k:03x}": n for k, n in m["flat_metatiles"].most_common(12)},
            places=m["places"], notes=m["notes"]))
    with open(os.path.join(out, "manifest.json"), "w") as handle:
        json.dump(dict(cameras=args.cameras, maps=manifest), handle, indent=1)
    write_report(out, manifest)
    if args.sheets:
        write_sheets(out, manifest, args.cameras.split(","))
    totals = collections.Counter()
    for row in manifest:
        totals.update(row["defects"])
    print(json.dumps(dict(maps=len(manifest), inspected=sum(r["status"] == "inspected" for r in manifest),
                          through_facility=sum(r["status"] == "inspected" and r["entry"] == "facility"
                                               for r in manifest),
                          partly=sum(r["status"] == "partly inspected" for r in manifest),
                          not_inspected=sum(r["status"] == "not inspected" for r in manifest),
                          not_reachable_on_pc=sum(r["status"] == "not reachable on PC" for r in manifest),
                          defects=dict(totals))))
    return 0


def write_report(out, manifest):
    lines = ["# 3D geometry check", "", "tools/qa/geometry_check.py check: each map's places, what share of its cells "
             "they reached, and the cells (or places, for holes and not-3d) each check found.", ""]
    for kind in ("outdoor", "cave", "underwater", "indoor", "secret_base"):
        rows = [r for r in manifest if r["kind"] == kind]
        if not rows:
            continue
        totals = collections.Counter()
        for r in rows:
            totals.update(r["defects"])
        lines += [f"## {kind}: {len(rows)} maps, {sum(r['status'] == 'inspected' for r in rows)} wholly inspected",
                  "", "Totals: " + (", ".join(f"{k} {totals[k]}" for k in CHECKS if totals[k]) or "none"), "",
                  "| Map | Status | Cells seen | " + " | ".join(CHECKS) + " |",
                  "|---|---|---|" + "---|" * len(CHECKS)]
        for r in rows:
            status = r["status"] + (" (through its facility)" if r["entry"] == "facility" else "")
            seen = "-" if r["status"] == "not reachable on PC" else f"{r['covered']}/{r['cells']}"
            lines.append(f"| {r['map']} | {status} | {seen} | "
                         + " | ".join(str(r["defects"].get(k, "")) for k in CHECKS) + " |")
        lines.append("")
    entered = [r for r in manifest if r["facility"]]
    if entered:
        lines += ["## Entered through their facilities", ""]
        lines += [f"* {r['map']}: {r['status']}, {r['covered']}/{r['cells']} cells - {r['facility']}"
                  + "".join(f"; {note}" for note in r["notes"]) for r in entered] + [""]
    unreachable = [r for r in manifest if r["status"] == "not reachable on PC"]
    if unreachable:
        lines += ["## Not reachable on PC (left out of the tour)", ""]
        lines += [f"* {r['map']}: {r['unreachable']['reason']}" for r in unreachable] + [""]
    with open(os.path.join(out, "report.md"), "w") as handle:
        handle.write("\n".join(lines))


def write_sheets(out, manifest, cameras):
    """A contact sheet per map: each place's pictures, a row each (JPEG, for looking at)."""
    from PIL import Image, ImageDraw  # noqa: E402

    folder = os.path.join(out, "sheets")
    os.makedirs(folder, exist_ok=True)
    for row in manifest:
        shots = [[os.path.join(out, s) for s in p["shots"]] for p in row["places"] if p["shots"]]
        if not shots:
            continue
        cell_w, cell_h = 320, 180
        sheet = Image.new("RGB", (cell_w * len(cameras), (cell_h + 14) * len(shots)), (16, 16, 16))
        draw = ImageDraw.Draw(sheet)
        for r, place_shots in enumerate(shots):
            for c, path in enumerate(place_shots):
                image = Image.open(path).convert("RGB").resize((cell_w, cell_h))
                sheet.paste(image, (c * cell_w, r * (cell_h + 14) + 14))
                draw.text((c * cell_w + 4, r * (cell_h + 14)), os.path.basename(path)[:-4], fill=(255, 255, 255))
        sheet.save(os.path.join(folder, row["map"] + ".jpg"), quality=82)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", default=ROOT)
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("tour")
    p.add_argument("--out", required=True)
    p.add_argument("--maps", help="only these maps (comma-separated names; a map of NOT_REACHED named here is warped "
                                  "to)")
    p.add_argument("--kinds")
    p.add_argument("--with-unreachable", action="store_true",
                   help="the maps no player reaches on PC (NOT_REACHED) too, warped to")
    p.add_argument("--sample", type=int, default=1,
                   help="every N-th map of the tour's order only (and the facilities' rooms): a representative share "
                        "for a slow machine; a later run of the whole tour into the same --out goes on from it")
    p = sub.add_parser("run")
    p.add_argument("--exe", required=True)
    p.add_argument("--save")
    p.add_argument("--make-save")
    p.add_argument("--tour", required=True)
    p.add_argument("--out", required=True)
    p.add_argument("--jobs", type=int, default=2)
    p.add_argument("--chunk", type=int, default=40)
    p.add_argument("--cameras", default=CAMERAS)
    p.add_argument("--xvfb", action="store_true", help="each process on a display of its own (xvfb-run)")
    p.add_argument("--minutes", type=float, default=0,
                   help="start no game process after this many minutes (the running one finishes); 0: no limit")
    p.add_argument("--facility-tries", type=int, default=3,
                   help="game processes a place entered through its facility may take (a lost battle, its time gone)")
    p = sub.add_parser("check")
    p.add_argument("--out", required=True)
    p.add_argument("--tour")
    p.add_argument("--cameras", default=CAMERAS)
    p.add_argument("--hole-pixels", type=int, default=40)
    p.add_argument("--sheets", action="store_true")
    args = parser.parse_args()
    return {"tour": tour, "run": run, "check": check}[args.command](args)


if __name__ == "__main__":
    sys.exit(main())
