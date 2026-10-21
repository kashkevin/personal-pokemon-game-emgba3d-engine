#!/usr/bin/env python3
"""The Emerald game's trainers, people, item balls, signs, hidden items and warps, each visited in a real game process
(test builds; the final master update, phase 10).

The targets come from the inventory (tools/qa/inventory.py: the maps a player can reach); the battle-environment test
driver's plan runner (PKMN_TEST_PLAN, include/diorama/battle_env_test.h, TEST BUILDS ONLY) warps the player beside each
one - to a tile picked here from the map's layout, one a player can stand on (no wall, water or ledge, no other object,
across a counter for a clerk) - faces it and acts as a player would: A to talk, read or pick up (a warp: a step onto it
or, on an arrow mat, its arrow), B through every line and question, A through a battle with a strong test party (in
memory only; nothing is saved). Everything is read from the game's own log.

    tools/qa/gameplay_check.py --exe ./pokeemerald-mp-bfxtest --make-save build/new.sav --kinds trainer \\
        --jobs 2 --out build/qa/gameplay

Kinds and what passes:
    trainer   each trainer object: a battle with that trainer, won, its beaten flag set, the field back
    item      each visible item ball: an item into the bag and the ball gone (its flag set)
    person    everyone else with a script: the script ran and gave the field back
    sign      each sign: its script ran and gave the field back
    hidden    each hidden item: an item into the bag and its flag set
    warp      each warp: the destination map reached
    trigger   each coordinate trigger stepped onto: its script runs exactly when its variable holds the value it runs
              on (the save's state decides which story events are due), and gives the field back
A target that keeps the field for longer than the plan's budget is a hang: pictured, and the game started again at the
next target. A target with no tile to stand on is listed as blocked, with the reason. The report:
OUT/gameplay_check.json; exit 0 when every visited target passed.
"""
import argparse
import collections
import concurrent.futures
import json
import os
import re
import shutil
import struct
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "tools", "battle_env"))
import mega_check as mc  # noqa: E402  (the game in a folder of its own, the short introduction, the driver's log)
sys.path.insert(0, HERE)    # before tools/battle_env, which has an inventory.py of its own (the moves')
import importlib.util  # noqa: E402
_spec = importlib.util.spec_from_file_location("qa_inventory", os.path.join(HERE, "inventory.py"))
inventory_tool = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(inventory_tool)

KINDS = ("trainer", "item", "person", "sign", "hidden", "warp", "trigger")
# Tiles a player does not stand on: waterfalls, currents (they carry the player off), ledges. A warp onto surfable
# water puts the player on Surf (overworld.c GetAdjustedInitialTransitionFlags): a swimmer is talked to from the water.
BLOCKED = re.compile(r"WATERFALL|^MB_JUMP_|CURRENT|NO_SURFACING")
# How a warp tile sends the player (src/field_control_avatar.c): an arrow mat or a directional stair when the player
# stands on it and presses its direction (TryArrowWarp); a door when the player walks north into it (TryDoorWarp);
# the others when the player steps onto them (TryStartWarpEventScript). A warp on any other tile is only where a
# warp from elsewhere arrives.
ARROWS = {"MB_EAST_ARROW_WARP": "R", "MB_WEST_ARROW_WARP": "L", "MB_NORTH_ARROW_WARP": "U",
          "MB_SOUTH_ARROW_WARP": "D", "MB_WATER_SOUTH_ARROW_WARP": "D", "MB_SHOAL_CAVE_ENTRANCE": "D",
          "MB_UP_RIGHT_STAIR_WARP": "R", "MB_DOWN_RIGHT_STAIR_WARP": "R",
          "MB_UP_LEFT_STAIR_WARP": "L", "MB_DOWN_LEFT_STAIR_WARP": "L"}
DOORS = {"MB_ANIMATED_DOOR", "MB_PETALBURG_GYM_DOOR"}
STEP_ON = {"MB_LADDER", "MB_UP_ESCALATOR", "MB_DOWN_ESCALATOR", "MB_NON_ANIMATED_DOOR", "MB_LAVARIDGE_GYM_B1F_WARP",
           "MB_LAVARIDGE_GYM_1F_WARP", "MB_AQUA_HIDEOUT_WARP", "MB_MT_PYRE_HOLE", "MB_MOSSDEEP_GYM_WARP",
           "MB_UNION_ROOM_WARP", "MB_DEEP_SOUTH_WARP", "MB_WATER_DOOR"}
STEP = {"U": (0, -1), "D": (0, 1), "L": (-1, 0), "R": (1, 0)}
FACING = {"MOVEMENT_TYPE_FACE_DOWN": "U", "MOVEMENT_TYPE_FACE_UP": "D", "MOVEMENT_TYPE_FACE_LEFT": "R",
          "MOVEMENT_TYPE_FACE_RIGHT": "L"}
SIGN_FACING = {"BG_EVENT_PLAYER_FACING_NORTH": "U", "BG_EVENT_PLAYER_FACING_SOUTH": "D",
               "BG_EVENT_PLAYER_FACING_EAST": "R", "BG_EVENT_PLAYER_FACING_WEST": "L"}
B_OUTCOME_WON = 1


def enum_values(path, prefix):
    """{name: value} of a C enum's entries starting with prefix (sequential, or as assigned)."""
    values, current = {}, -1
    for line in open(path, encoding="utf-8"):
        line = line.split("//")[0].strip()
        match = re.match(rf"^({prefix}[A-Z0-9_]*)\s*(?:=\s*([0-9xA-Fa-f]+))?\s*,?$", line)
        if match:
            current = int(match.group(2), 0) if match.group(2) else current + 1
            values[match.group(1)] = current
    return values


def defines(path, prefix):
    """{name: value} of the plain #define NAME number lines starting with prefix."""
    out = {}
    for line in open(path, encoding="utf-8"):
        match = re.match(rf"^#define\s+({prefix}[A-Z0-9_]*)\s+(0x[0-9A-Fa-f]+|\d+)\b", line)
        if match:
            out[match.group(1)] = int(match.group(2), 0)
    return out


class Tiles:
    """The maps' layouts and their metatiles' behaviors: what a player can stand on."""

    def __init__(self, root):
        self.root = root
        layouts = json.load(open(os.path.join(root, "data", "layouts", "layouts.json")))["layouts"]
        self.layouts = {lay["id"]: lay for lay in layouts if "id" in lay}
        names = enum_values(os.path.join(root, "include", "constants", "metatile_behaviors.h"), "MB_")
        self.behavior_name = {value: name for name, value in names.items()}
        paths = {}
        for line in open(os.path.join(root, "src", "data", "tilesets", "metatiles.h"), encoding="utf-8"):
            match = re.match(r'const u16 (gMetatileAttributes_\w+)\[\] = INCBIN_U16\("([^"]+)"\);', line)
            if match:
                paths[match.group(1)] = match.group(2)
        headers = open(os.path.join(root, "src", "data", "tilesets", "headers.h"), encoding="utf-8").read()
        self.attributes = {}
        for match in re.finditer(r"const struct Tileset (gTileset_\w+) =\s*\{(.*?)\};", headers, flags=re.S):
            attr = re.search(r"\.metatileAttributes = (gMetatileAttributes_\w+)", match.group(2))
            if attr and attr.group(1) in paths:
                self.attributes[match.group(1)] = paths[attr.group(1)]
        self._cache = {}

    def _attrs(self, tileset):
        path = self.attributes.get(tileset)
        if path is None or not os.path.exists(os.path.join(self.root, path)):
            return ()
        data = open(os.path.join(self.root, path), "rb").read()
        return struct.unpack(f"<{len(data) // 2}H", data[:len(data) // 2 * 2])

    def grid(self, layout_id):
        if layout_id not in self._cache:
            lay = self.layouts[layout_id]
            w, h = lay["width"], lay["height"]
            data = open(os.path.join(self.root, lay["blockdata_filepath"]), "rb").read()
            blocks = struct.unpack(f"<{w * h}H", data[:w * h * 2])
            self._cache[layout_id] = (w, h, blocks, self._attrs(lay["primary_tileset"]),
                                      self._attrs(lay["secondary_tileset"]))
        return self._cache[layout_id]

    def at(self, grid, x, y):
        """(collision, elevation, behavior name) of a tile, or None off the map."""
        w, h, blocks, primary, secondary = grid
        if not (0 <= x < w and 0 <= y < h):
            return None
        block = blocks[y * w + x]
        metatile, collision, elevation = block & 0x3FF, (block >> 10) & 3, block >> 12
        attrs, index = (primary, metatile) if metatile < 512 else (secondary, metatile - 512)
        behavior = attrs[index] & 0xFF if index < len(attrs) else 0
        return collision, elevation, self.behavior_name.get(behavior, f"MB_{behavior}")


def standable(tile, elevation=None, strict=False):
    """A tile (collision, elevation, behavior) a player can stand on; with an elevation, one from which the player
    can also step onto or talk to something at that elevation (0 and 15 go with any: AreElevationsCompatible,
    IsElevationMismatchAt). A Pokemon Center's escalator is elevation 4: it is stepped onto from the elevation-0
    tile beside it, never from the floor (3) in front of it. Never a warp's own tile."""
    if tile is None:
        return False
    collision, tile_elevation, behavior = tile
    if collision != 0 or BLOCKED.search(behavior):
        return False
    if behavior in DOORS or behavior in STEP_ON or behavior in ARROWS:
        return False    # a warp's own tile: arriving on a door walks the player out of it, a step from it takes it
    if strict:
        # a background event (a sign, a hidden item) is searched at the player's tile's elevation, 0 on a tile of
        # elevation 0 (GetInFrontOfPlayerPosition): only its own elevation finds it, or any when it is 0
        return elevation in (None, 0) or tile_elevation == elevation
    return elevation is None or elevation in (0, 15) or tile_elevation in (0, 15) or tile_elevation == elevation


def beside(tiles, grid, x, y, occupied, elevation=None, prefer=None, required=None, strict=False):
    """A tile next to (x, y) to stand on, facing it: (x, y, direction), or None. A clerk behind a counter is talked
    to across it."""
    options = [("U", (x, y + 1)), ("D", (x, y - 1)), ("R", (x - 1, y)), ("L", (x + 1, y))]
    if prefer:
        options.sort(key=lambda option: option[0] != prefer)
    for direction, (sx, sy) in options:
        if required and direction != required:
            continue
        if (sx, sy) not in occupied and standable(tiles.at(grid, sx, sy), elevation, strict):
            return sx, sy, direction
    for direction, (sx, sy) in options:
        if required and direction != required:
            continue
        dx, dy = sx - x, sy - y
        middle = tiles.at(grid, sx, sy)
        far = (x + 2 * dx, y + 2 * dy)
        if middle is not None and middle[2] == "MB_COUNTER" and far not in occupied and standable(tiles.at(grid, *far)):
            return far[0], far[1], direction
    return None


def plan_targets(root, inventory, kinds, maps_filter=None):
    """(targets, blocked): each target a dict with its plan line; blocked ones with the reason."""
    tiles = Tiles(root)
    trainer_ids = defines(os.path.join(root, "include", "constants", "opponents.h"), "TRAINER_")
    weathers = enum_values(os.path.join(root, "include", "constants", "weather.h"), "WEATHER_")
    by_id = {m["id"]: m for m in inventory["maps"]}
    targets, blocked = [], []
    for m in inventory["maps"]:
        if not m["reachable"] or m["frlg"] or m.get("used_by") or (maps_filter and not re.search(maps_filter, m["name"])):
            continue
        if m.get("scripted_entry"):
            for kind, items in (("object", m["objects"]), ("sign", m["signs"]), ("hidden", m["hidden_items"]),
                                ("warp", m["warps"]), ("trigger", m["triggers"])):
                for item in items:
                    if kind == "object" and item["kind"] in ("object", "clone", "berry_tree"):
                        continue
                    blocked.append(dict(key=item["key"], map=m["id"], reason=m["scripted_entry"], kind=(
                        kind if kind != "object" else "trainer" if item["kind"] == "trainer"
                        else "item" if item["kind"] == "item_ball" else "person")))
            continue
        data = json.load(open(os.path.join(root, "data", "maps", m["name"], "map.json")))
        if data.get("layout") not in tiles.layouts:
            continue
        grid = tiles.grid(data["layout"])
        objects = data.get("object_events", [])
        occupied = {(o.get("x"), o.get("y")) for o in objects}
        # where an object always stands (no flag hides it): a warp under it is never stepped on (Lavaridge Gym's
        # trainers buried in the sand stand on four of its holes on each floor)
        standing = {(o.get("x"), o.get("y")): o for o in objects if o.get("flag") in (None, "0", 0, "")}

        def free_first(find):
            # a tile no object stands on; else one where only an object the story can hide stands (the Aqua grunts
            # in front of the Slateport museum's doors, gone in a later save): find(busy) for each
            return find(occupied) or find(set(standing))

        def step_from(x, y, elevation, busy):
            # a step onto (x, y) from a tile beside it, at an elevation that may step there
            for direction, (dx, dy) in STEP.items():
                sx, sy = x - dx, y - dy
                if (sx, sy) not in busy and standable(tiles.at(grid, sx, sy), elevation):
                    return sx, sy, direction
            return None

        def add(key, kind, place, action, arg, expect):
            if place is None:
                blocked.append(dict(key=key, kind=kind, map=m["id"], reason="no tile beside it to stand on"))
                return
            x, y, direction = place
            targets.append(dict(key=key, kind=kind, map=m["id"], line=f"{key} {action} {m['group']} {m['num']} "
                                f"{x} {y} {direction} {arg}", expect=expect))

        for item in m["objects"]:
            obj = objects[item["local"] - 1]
            kind = {"trainer": "trainer", "item_ball": "item", "person": "person"}.get(item["kind"])
            if kind not in kinds:
                continue
            if item.get("needs_server"):
                blocked.append(dict(key=item["key"], kind=kind, map=m["id"], reason=(
                    "its script asks the multiplayer server first (a reset-guarded static encounter, "
                    "MultiplayerStaticReset): a game with no server refuses it, as designed (every game the app "
                    "starts has one, Solo too); the test ROM's admission tests cover the script")))
                continue
            own = {(obj.get("x"), obj.get("y"))}
            # its elevation is its tile's once in the game (ObjectEventUpdateElevation), the template's only on an
            # any-elevation tile: Route 134's Marley (template 1) stands on shallow water (3), walked to, not surfed
            under = tiles.at(grid, obj.get("x"), obj.get("y"))
            elevation = under[1] if under is not None and under[1] not in (0, 15) else obj.get("elevation")
            place = free_first(lambda busy: beside(tiles, grid, obj.get("x"), obj.get("y"), busy - own, elevation,
                                                   prefer=FACING.get(obj.get("movement_type"))))
            expect = {}
            if kind == "trainer":
                expect["trainers"] = sorted({trainer_ids[b[w]] for b in item.get("battles", ())
                                             for w in ("trainer", "partner") if b.get(w) in trainer_ids})
            add(item["key"], kind, place, "fight" if kind == "trainer" else "talk", item["local"], expect)
        for sign in m["signs"] if "sign" in kinds else ():
            event = data["bg_events"][sign["index"]]
            place = free_first(lambda busy: beside(tiles, grid, sign["x"], sign["y"], busy, event.get("elevation"),
                                                   required=SIGN_FACING.get(event.get("player_facing_dir")),
                                                   strict=True))
            add(sign["key"], "sign", place, "sign", sign["index"], {})
        for hidden in m["hidden_items"] if "hidden" in kinds else ():
            elevation = data["bg_events"][hidden["index"]].get("elevation")   # found from its own elevation only
            place = free_first(lambda busy: beside(tiles, grid, hidden["x"], hidden["y"], busy, elevation,
                                                   strict=True))
            add(hidden["key"], "hidden", place, "hidden", hidden["index"], dict(item=hidden["item"]))
        for warp in m["warps"] if "warp" in kinds else ():
            if (by_id.get(warp["dest_map"]) or {}).get("scripted_entry"):
                # a door the game opens only by that script (a cable club door behind its counter, a challenge's door)
                blocked.append(dict(key=warp["key"], kind="warp", map=m["id"],
                                    reason="into " + by_id[warp["dest_map"]]["scripted_entry"]))
                continue
            tile = tiles.at(grid, warp["x"], warp["y"])
            behavior = tile[2] if tile else ""
            if behavior not in ARROWS and (warp["x"], warp["y"]) in standing:
                blocked.append(dict(key=warp["key"], kind="warp", map=m["id"], reason=(
                    f"an object always stands on it ({standing[(warp['x'], warp['y'])].get('graphics_id')})")))
                continue
            if behavior in ARROWS:
                place = (warp["x"], warp["y"], ARROWS[behavior])    # on the mat, its arrow pressed
            elif behavior in DOORS:
                below = (warp["x"], warp["y"] + 1)
                place = free_first(lambda busy: (*below, "U") if standable(tiles.at(grid, *below))
                                   and below not in busy else None)
            elif behavior in STEP_ON:
                place = free_first(lambda busy: step_from(warp["x"], warp["y"], tile[1], busy))
            else:
                blocked.append(dict(key=warp["key"], kind="warp", map=m["id"], arrival_only=True,
                                    reason=f"{behavior or 'no tile'}: only an arrival point (nothing warps from it)"))
                continue
            dest = by_id.get(warp["dest_map"])
            add(warp["key"], "warp", place, "warp", warp["index"],
                dict(dest=None if dest is None else [dest["group"], dest["num"]], dest_map=warp["dest_map"],
                     behavior=behavior))
        for trigger in m["triggers"] if "trigger" in kinds else ():
            if trigger["type"] != "trigger":
                blocked.append(dict(key=trigger["key"], kind="trigger", map=m["id"],
                                    reason=f"a {trigger['type']} event (no script to run)"))
                continue
            weather = None
            if trigger["var"] == "TRIGGER_RUN_IMMEDIATELY":
                # its script runs inside the step itself: a weather it sets is what the check sees (Route 111's
                # sandstorm, Mt. Pyre's fog); with none, nothing to see
                weather = weathers.get(trigger.get("sets_weather"))
                if weather is None:
                    blocked.append(dict(key=trigger["key"], kind="trigger", map=m["id"], reason=(
                        "runs inside the step itself (TRIGGER_RUN_IMMEDIATELY) and sets no weather: nothing to see")))
                    continue
            under = tiles.at(grid, trigger["x"], trigger["y"])
            place = None
            if standable(under):
                place = free_first(lambda busy: step_from(trigger["x"], trigger["y"], under[1], busy))
            covered = [o.get("graphics_id") for o in objects if (o.get("x"), o.get("y")) == (trigger["x"], trigger["y"])]
            add(trigger["key"], "trigger", place, "trigger", trigger["index"],
                dict(var=trigger["var"], value=trigger["var_value"], script=trigger["script"], weather=weather,
                     weather_name=trigger.get("sets_weather"), covered=covered))
    return targets, blocked


EVENT = re.compile(r"^plan target (\d+) (\S+) (begin|at|absent|missing|closed|battle|map now|ending done|ending|end|hang|"
                   r"trigger)\b ?(.*)$")


def parse(lines):
    """{target key: {begin, at, battles: [...], outcomes: [...], end: {...}, hang: text}} from the driver's log."""
    out = collections.defaultdict(lambda: dict(battles=[], outcomes=[], maps=[]))
    for line in lines:
        match = EVENT.match(line)
        if not match:
            continue
        index, key, what, rest = int(match.group(1)), match.group(2), match.group(3), match.group(4)
        entry = out[key]
        entry["index"] = index
        if what == "begin":
            entry["begin"] = True
        elif what == "at":
            entry["at"] = rest
        elif what == "absent":
            entry["absent"] = True
        elif what == "closed":
            entry["closed"] = True
        elif what == "missing":
            entry["missing"] = rest
        elif what == "ending":
            entry["ending"] = True
        elif what == "ending done":
            entry["ending_done"] = True
        elif what == "battle":
            if " over outcome " in rest:
                entry["outcomes"].append(int(rest.rsplit(" ", 1)[1]))
            else:
                numbers = re.findall(r"-?\d+", rest)
                entry["battles"].append(dict(type=rest.split("type ")[1].split()[0], trainers=[int(numbers[-2]),
                                                                                               int(numbers[-1])]))
        elif what == "map now":
            entry["maps"].append(rest)
        elif what == "end":
            words = rest.split()

            def value(name, count=1):
                at = words.index(name) + 1
                return words[at:at + count] if count > 1 else words[at]
            fields = {name: int(value(name)) for name in ("frames", "script", "battles", "outcome", "beaten", "own",
                                                          "by_itself")}
            fields["trainers"] = [int(v) for v in value("trainers", 2)]
            fields["money"] = [int(v) for v in value("money", 2)]
            fields["map"], fields["pos"] = value("map", 2)
            bag = value("bag")
            fields["bag"] = {} if bag == "-" else {int(i): int(n) for i, n in (part.split(":") for part in bag.split(","))}
            fields["weather"] = int(value("weather")) if "weather" in words else None
            fields["var"] = int(value("var")) if "var" in words else None
            entry["end"] = fields
        elif what == "hang":
            entry["hang"] = rest
        elif what == "trigger":     # "var V is X wants Y"
            numbers = [int(n) for n in re.findall(r"-?\d+", rest)]
            entry["trigger"] = dict(var=numbers[0], value=numbers[1], wants=numbers[2])
    return out


def judge(target, seen):
    """(passed, reason) for one target from what the game logged."""
    if seen is None or "begin" not in seen:
        return False, "not reached (an earlier target ended the run)"
    if "hang" in seen:
        return False, f"hang: {seen['hang']}"
    if "missing" in seen:
        return False, f"not on its map though nothing hides it ({seen['missing']})"
    kind, expect = target["kind"], target["expect"]
    if seen.get("ending"):
        # the game's ending ran from it (the Champion beaten: the Hall of Fame, the credits, the reset to the title):
        # judged by what came before; the ending itself must run through to the reset
        if not seen.get("ending_done"):
            return False, "the game's ending began (Hall of Fame) but never reached the title"
        reached = [[int(v) for v in m.split(".")] for m in seen["maps"]]
        if kind == "warp" and expect.get("dest") is not None and expect["dest"] not in reached:
            return False, f"the game's ending ran, but {expect['dest_map']} was never reached"
        if kind == "trainer" and B_OUTCOME_WON not in seen["outcomes"]:
            return False, f"the game's ending ran, but battle outcomes {seen['outcomes']}"
        return True, "the game's ending followed (Hall of Fame, credits, back to the title)"
    end = seen.get("end")
    if end is None:
        return False, "the game stopped during it"
    gained = {i: n for i, n in end["bag"].items() if n > 0}
    if kind == "trainer":
        fought = {t for battle in seen["battles"] for t in battle["trainers"]}
        if not seen["battles"]:
            return False, "no battle started"
        if B_OUTCOME_WON not in seen["outcomes"]:
            return False, f"battle outcomes {seen['outcomes']}, not won"
        if expect.get("trainers") and not fought & set(expect["trainers"]):
            # its script picks the battle by the story's state (Gabby and Ty's six objects fight their first battle
            # until the TV crew's state moves on): won, against the trainer the state picked
            return True, f"won against {sorted(fought)} (its script names {expect['trainers']}; the state picked)"
        if end["beaten"] == 0:
            return False, "won, but its beaten flag is not set"
        return True, "won; beaten flag set" if end["beaten"] == 1 else "won"
    if kind == "item":
        if not end["script"]:
            return False, "nothing happened"
        if not gained:
            return False, "no item into the bag"
        if end["own"] == 0:
            return False, f"item {gained} received, but the ball's flag is not set"
        return True, f"item {gained}"
    if kind in ("person", "sign"):
        return (True, "script ran and ended") if end["script"] or end["battles"] else (False, "nothing happened")
    if kind == "hidden":
        if not gained:
            return False, "no item found" if end["script"] else "nothing happened"
        if end["own"] == 0:
            return False, f"item {gained} found, but its flag is not set"
        return True, f"item {gained}"
    if kind == "trigger":
        trigger = seen.get("trigger")
        if trigger is None:
            return False, "its condition was not logged"
        if expect.get("weather") is not None:
            # TRIGGER_RUN_IMMEDIATELY: its script ran inside the step; the weather it sets is what there is to see
            if end.get("weather") == expect["weather"]:
                return True, f"ran at once inside the step: the weather is {expect['weather_name']}"
            return False, f"stepped on, but the weather is {end.get('weather')}, not {expect['weather_name']}"
        due = trigger["var"] == 0 or trigger["value"] == trigger["wants"]
        # a script that only sets flags and variables runs to its end inside the step: its own variable moved
        moved = trigger["var"] != 0 and end.get("var") is not None and end["var"] != trigger["value"]
        ran = bool(end["script"] or end["battles"] or seen["maps"] or moved)
        if due and not ran:
            return False, f"due (var {trigger['var']} = {trigger['value']}) but nothing ran"
        if not due and ran:
            return False, f"not due (var {trigger['var']} = {trigger['value']}, runs on {trigger['wants']}) but ran"
        return True, ("ran and ended" if due else f"not due (var {trigger['var']} = {trigger['value']}, "
                                                    f"runs on {trigger['wants']}): nothing ran")
    if kind == "warp":
        group, num = (int(v) for v in end["map"].split("."))
        if expect["dest"] is not None and [group, num] == expect["dest"]:
            return True, f"reached {expect['dest_map']}"
        if expect["dest"] is None and seen["maps"]:
            return True, f"reached map {end['map']} ({expect['dest_map']})"
        return False, f"on map {end['map']} at {end['pos']}, not {expect['dest_map']}"
    return False, f"unknown kind {kind}"


def run_shard(args, shard, lines, party):
    """One game process after another through a shard's plan, starting again after each hang or crash."""
    shard_dir = os.path.join(args.out, f"shard-{shard:02d}")
    os.makedirs(shard_dir, exist_ok=True)
    plan = os.path.join(shard_dir, "plan.txt")
    with open(plan, "w") as out:
        out.write("\n".join(lines) + "\n")
    first, runs, everything = 0, [], []
    while first < len(lines):
        rundir = os.path.join(shard_dir, f"run-{len(runs):03d}")
        process, output = mc.start(args, rundir, dict(
            PKMN_TEST_PLAN=plan, PKMN_TEST_PLAN_FIRST=str(first), PKMN_TEST_AUTOPILOT="1", PKMN_TEST_STRONG="1",
            PKMN_TEST_PARTY=party, PKMN_TEST_FIRST_MOVE=str(args.move), PKMN_TEST_MORE_MOVES=args.more_moves,
            PKMN_TEST_PLAN_FRAMES=str(args.frames), PKMN_TEST_PLAN_SHOTS="1" if args.shots else "0",
            PKMN_TEST_SPEED=str(args.speed),
            PKMN_TEST_MEGA_SHOT_STEM=os.path.join(rundir, "hang")))
        code = mc.finish(process, output, args.process_timeout)
        log = mc.driver_lines(os.path.join(rundir, "test.log"))
        everything += log
        begun = [int(m.group(1)) for m in map(EVENT.match, log) if m and m.group(3) == "begin"]
        runs.append(dict(run=len(runs), first=first, exit=code, last=max(begun) if begun else None))
        if code == 0:
            break
        if not begun:
            runs[-1]["problem"] = "no target began (the game did not reach the field)"
            # what the game itself said last (its output and the driver's log): a CI job keeps only this report
            tail = []
            for name in ("game.log", "test.log"):
                path = os.path.join(rundir, name)
                if os.path.exists(path):
                    with open(path, encoding="utf-8", errors="replace") as text:
                        tail += [f"{name}: {line.rstrip()}" for line in text.readlines()[-15:]]
            runs[-1]["tail"] = tail
            print(f"shard {shard} run {len(runs) - 1}: no target began (exit {code}); the game's last lines:",
                  *tail, sep="\n  ", flush=True)
            break
        stopped = max(begun)
        if code != 7:   # a crash or a timeout: the target it stopped in is recorded as such
            everything.append(f"plan target {stopped} {lines[stopped].split()[0]} hang the game stopped "
                              f"(exit {code})")
        if os.path.exists(os.path.join(rundir, "hang-99.png")):
            shutil.copy(os.path.join(rundir, "hang-99.png"),
                        os.path.join(shard_dir, f"hang-{lines[stopped].split()[0].replace(':', '_')}.png"))
        first = stopped + 1
        if not args.keep_runs:
            for name in ("test.sav",):
                path = os.path.join(rundir, name)
                if os.path.exists(path):
                    os.remove(path)
    return runs, everything


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", required=True, help="a test build (Makefile_pc BFX_TEST=1; Classic 2D pictures hangs)")
    parser.add_argument("--save", help="a save to start from (copied; never written)")
    parser.add_argument("--make-save", metavar="PATH", help="first make the save there (the short introduction)")
    parser.add_argument("--out", required=True)
    parser.add_argument("--kinds", default=",".join(KINDS), help=f"comma-separated, of {', '.join(KINDS)}")
    parser.add_argument("--maps", help="only maps whose name matches this regular expression (with --plan: the id)")
    parser.add_argument("--plan", help="the plan.json of an earlier --plan-only run, instead of the engine checkout")
    parser.add_argument("--limit", type=int, default=0, help="at most this many targets (0: all)")
    parser.add_argument("--shard", type=int, default=0, help="with --shards: this job's part (0-based)")
    parser.add_argument("--shards", type=int, default=1, help="the targets split this many ways (CI jobs)")
    parser.add_argument("--jobs", type=int, default=1, help="game processes at once")
    parser.add_argument("--frames", type=int, default=2400, help="frames a target may keep the field (a battle: more)")
    parser.add_argument("--process-timeout", type=int, default=3600, help="seconds a game process may run")
    parser.add_argument("--display", default=os.environ.get("DISPLAY"), help="an X display (Linux)")
    parser.add_argument("--keep-runs", action="store_true")
    parser.add_argument("--shots", action="store_true", help="a picture of each target as the player faces it")
    parser.add_argument("--speed", type=int, default=4, help="the game's clock this many times as fast (test builds)")
    parser.add_argument("--plan-only", action="store_true", help="write the plan and the blocked list, run nothing")
    args = parser.parse_args()
    args.exe, args.out = os.path.abspath(args.exe), os.path.abspath(args.out)
    os.makedirs(args.out, exist_ok=True)
    kinds = set(args.kinds.split(","))
    if args.plan:   # a plan written before (--plan-only, from an engine checkout): CI's jobs need no checkout
        plan = json.load(open(args.plan))
        targets = [t for t in plan["targets"] if t["kind"] in kinds and (not args.maps or re.search(
            args.maps, t["map"]))]
        blocked = [b for b in plan["blocked"] if b["kind"] in kinds]
    else:
        targets, blocked = plan_targets(ROOT, inventory_tool.inventory(ROOT), kinds, args.maps)
    if args.shards > 1:     # one CI job's part: every shards-th target from the shard-th
        targets = targets[args.shard::args.shards]
    if args.limit:
        targets = targets[:args.limit]
    with open(os.path.join(args.out, "plan.json"), "w") as out:
        json.dump(dict(targets=targets, blocked=blocked), out, indent=1)
    print(json.dumps(dict(targets=len(targets), blocked=len(blocked),
                          by_kind=collections.Counter(t["kind"] for t in targets))), flush=True)
    if args.plan_only:
        return 0
    if args.make_save:
        args.save = mc.make_save(args, os.path.abspath(args.make_save))
    if not args.save:
        parser.error("--save or --make-save")
    args.save = os.path.abspath(args.save)
    facts, _ = mc.game_facts(args)
    args.move = facts.get("MOVE_FLASH_CANNON", facts["MOVE_FLAMETHROWER"])   # no type is immune to Steel
    # and three more, the most effective of the four chosen each turn against the foe (a Water team out-heals a resisted
    # move: Champion Wallace's Milotic): super effective against Water, Ground, Flying and Dragon between them
    args.more_moves = ",".join(str(facts[name]) for name in ("MOVE_THUNDERBOLT", "MOVE_ICE_BEAM", "MOVE_ENERGY_BALL")
                               if name in facts)
    # six at level 100, so a long fight is still won when the lead faints
    party = ",".join(f"{facts[name]}:100" for name in ("SPECIES_MEWTWO", "SPECIES_CHARIZARD", "SPECIES_BLASTOISE",
                                                        "SPECIES_VENUSAUR", "SPECIES_BLAZIKEN", "SPECIES_PIKACHU"))
    shards = [[t["line"] for t in targets[i::args.jobs]] for i in range(args.jobs)]
    started = time.monotonic()
    seen, runs = {}, {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = {pool.submit(run_shard, args, i, lines, party): i for i, lines in enumerate(shards) if lines}
        for future in concurrent.futures.as_completed(futures):
            shard_runs, lines = future.result()
            runs[futures[future]] = shard_runs
            seen.update(parse(lines))
    results = []
    for target in targets:
        state = seen.get(target["key"]) or {}
        end, at = state.get("end"), state.get("at") or ""
        if (target["kind"] == "trigger" and target["expect"].get("covered") and end is not None
                and not state.get("maps") and at.startswith(f"map {end['map']} {end['pos']} ")):
            # the step never happened: an object its flag shows in this save stands on it (New Mauville's blue
            # button under its Voltorb until that is beaten)
            blocked.append(dict(key=target["key"], kind="trigger", map=target["map"], runtime=True, reason=(
                f"an object stands on it in this state ({', '.join(target['expect']['covered'])}): the step is "
                "blocked until the story removes it")))
            continue
        if "(it began by itself)" in at and state.get("hang") and not state.get("maps"):
            # its map's own scene began as the player was put down there, not through the map's entrance, and hung
            # (the Devon president's walk led off the map); the warps into the map play the scene from its entrance
            blocked.append(dict(key=target["key"], kind=target["kind"], map=target["map"], runtime=True, reason=(
                "its map's scene began as the check put the player down there (not through the entrance) and stopped "
                f"({state['hang'][:80]}); the warps into the map play it from the entrance")))
            continue
        if state.get("absent") or (state.get("closed") and not state.get("maps")):
            # Not there in this game's state: an object a story flag or its map's own script hides, a door the map's
            # script keeps shut (its tile solid) until the story opens it. Blocked, not judged.
            blocked.append(dict(key=target["key"], kind=target["kind"], map=target["map"], runtime=True, reason=(
                "absent: the game hides it in this state (a story flag or its map's script)" if state.get("absent")
                else "closed: its map keeps this door shut in this state (the story opens it)")))
            continue
        passed, reason = judge(target, seen.get(target["key"]))
        results.append(dict(key=target["key"], kind=target["kind"], map=target["map"], passed=passed, reason=reason,
                            seen=seen.get(target["key"])))
    summary = collections.defaultdict(collections.Counter)
    for result in results:
        summary[result["kind"]]["passed" if result["passed"] else "failed"] += 1
    for item in blocked:
        summary[item["kind"]]["blocked"] += 1
    report = dict(exe=args.exe, seconds=round(time.monotonic() - started), summary=summary, runs=runs,
                  results=results, blocked=blocked)
    with open(os.path.join(args.out, "gameplay_check.json"), "w") as out:
        json.dump(report, out, indent=1)
    print(json.dumps(dict(seconds=report["seconds"], summary=summary)), flush=True)
    for result in results:
        if not result["passed"]:
            print(f"FAILED {result['key']}: {result['reason']}", flush=True)
    for item in blocked:    # found in the game, not in the plan: what a coverage manifest needs besides the FAILED lines
        if item.get("runtime"):
            print(f"BLOCKED {item['key']}: {item['reason']}", flush=True)
    return 0 if all(r["passed"] for r in results) else 1


if __name__ == "__main__":
    sys.exit(main())
