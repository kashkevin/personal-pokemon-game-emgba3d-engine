#!/usr/bin/env python3
"""What the game contains, from its own data (the final master update, phase 9: a machine-readable inventory).

From the engine checkout: every map of data/maps/map_groups.json with its object events (people, trainers, item balls,
berry trees and other objects), warps, coordinate triggers, signs, hidden items and secret base spots; whether a player
can reach it from Littleroot Town (warps, connections and the warps of every script that can run, #if IS_FRLG lines
left out as the Emerald build leaves them), and for a map nobody walks into, how the game enters it instead or that it
never does; every trainer of src/data/trainers.party and where each is fought (the trainerbattle commands of the map
and common scripts, tied to the object whose script runs them), and the battles only scripts start.

    tools/qa/inventory.py --out build/qa/inventory.json

Every entry has a stable "key" (its kind and its place in the data) that tests and the coverage manifest refer to.
"""
import argparse
import collections
import glob
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
LABEL = re.compile(r"^([A-Za-z0-9_]+)::?\s*$")
# a trainer battle command (trainerbattle_single, _double, _lavaridge (a local id first), ..., multi_2_vs_2)
BATTLE = re.compile(r"^\s*(trainerbattle[a-z_]*|multi_[a-z0-9_]+)\s+(.*)$")
ITEM_BALL = "OBJ_EVENT_GFX_ITEM_BALL"
SCRIPT_WARP = re.compile(r"^\s*(?:warp[a-z]*|setwarp|setdynamicwarp|setescapewarp|setdivewarp|setholewarp)"
                         r"\s+(MAP_[A-Z0-9_]+)")
START_MAP = "MAP_LITTLEROOT_TOWN"
# The maps the static graph cannot reach and how the game enters them instead (or that it never does).
SPECIAL_ENTRY = [
    (re.compile(r"Unused|Prototype"), "unused: in the data, never entered"),
    (re.compile(r"^SecretBase_"), "secret base: entered from a secret base spot by the C code (secret_base.c)"),
    (re.compile(r"^BattlePyramidSquare"), "battle pyramid: a piece the C code puts into a floor (battle_pyramid.c)"),
    (re.compile(r"^ShoalCave_HighTide"), "high tide: its layout replaces the low-tide room's (setmaplayoutindex)"),
    (re.compile(r"^InsideOfTruck$"), "truck: the original introduction's start (new_game.c); the short one skips it"),
]
IDENT = re.compile(r"\b[A-Za-z_][A-Za-z0-9_]*\b")
TERMINAL = re.compile(r"^(end|return|goto|step_end)\b")


def script_lines(path):
    """A script file's lines as the Emerald build sees them: the C preprocessor's #if IS_FRLG blocks (FireRed and
    LeafGreen's own lines) left out, their #else kept; any other condition is kept (both of its branches)."""
    stack = []      # per open #if: whether this branch is skipped, and whether it is an IS_FRLG test
    for line in open(path, encoding="utf-8", errors="replace"):
        text = line.strip()
        if text.startswith("#if"):
            condition = text.split(None, 1)[1] if " " in text else ""
            frlg = condition.replace(" ", "") in ("IS_FRLG", "!IS_FRLG")
            stack.append((frlg and condition.replace(" ", "") == "IS_FRLG", frlg))
            continue
        if text.startswith("#else") and stack:
            skipped, frlg = stack[-1]
            stack[-1] = (not skipped if frlg else skipped, frlg)
            continue
        if text.startswith("#endif") and stack:
            stack.pop()
            continue
        if any(skipped for skipped, _ in stack):
            continue
        yield line


def map_list(root):
    """(map name, group, number) for every map the game has, in its own order."""
    groups = json.load(open(os.path.join(root, "data", "maps", "map_groups.json")))
    return [(name, g, n) for g, group in enumerate(groups["group_order"]) for n, name in enumerate(groups[group])]


class Scripts:
    """Every script file the Emerald build assembles (data/event_scripts.s, the common scripts, each map's), parsed
    once: each label's file, the identifiers its lines name, the maps its warps go to, its trainerbattle commands, and
    the label it runs on into (when its last command is not end, return or goto)."""

    def __init__(self, root):
        self.root = root
        self.file, self.refs = {}, collections.defaultdict(set)
        self.warps, self.battles = collections.defaultdict(set), collections.defaultdict(list)
        self.labels_of = {}
        files = [os.path.join(root, "data", "event_scripts.s")]
        files += sorted(glob.glob(os.path.join(root, "data", "scripts", "*.inc")))
        files += sorted(glob.glob(os.path.join(root, "data", "maps", "*", "scripts.inc")))
        for path in files:
            self._parse(path)

    def _parse(self, path):
        rel = os.path.relpath(path, self.root)
        label, last, order = None, "", []
        for line in script_lines(path):
            code = re.sub(r'"[^"]*"', "", line.split("@")[0]).strip()
            match = LABEL.match(code)
            if match:
                if label is not None and not TERMINAL.match(last):
                    self.refs[label].add(match.group(1))
                label, last = match.group(1), ""
                self.file[label] = rel
                order.append(label)
                continue
            if label is None or not code or code.startswith(("#", ".align", ".section", ".global")):
                continue
            last = code
            self.refs[label].update(IDENT.findall(code))
            match = SCRIPT_WARP.match(code)
            if match:
                self.warps[label].add(match.group(1))
            match = BATTLE.match(code)
            if match:
                named = re.findall(r"\bTRAINER_[A-Z0-9_]+\b", match.group(2))
                if named:
                    self.battles[label].append((match.group(1), named[0], named[1] if len(named) > 1 else None))
        self.labels_of[rel] = order

    def local(self, label):
        """The label and every label of its own file it calls, jumps to or runs on into (the common scripts not)."""
        own, seen, todo = self.file.get(label), set(), [label]
        while todo:
            current = todo.pop()
            if current in seen or self.file.get(current) != own:
                continue
            seen.add(current)
            todo.extend(self.refs[current] - seen)
        return seen

    def frlg(self, label):
        """A FireRed/LeafGreen script: in a *_frlg.inc file or a FireRed/LeafGreen map's folder."""
        path = self.file.get(label, "")
        return path.endswith("_frlg.inc") or "_Frlg/" in path


def reachable(root, maps, scripts):
    """The maps a player can get to from Littleroot Town, by warps, map connections and the warps of the scripts that
    can run: a reached map's own scripts and those its events name, the scripts the game's C code starts, and every
    script one of those calls, jumps to, lists or runs on into (#if IS_FRLG lines left out, as the Emerald build does).
    Static: a map reached only by a script nobody can run is still counted, and a place only the C code sends the player
    to (a secret base, a Battle Pyramid floor) is not. Returns the maps and the scripts that can run."""
    by_id = {m["id"]: m for m in maps}
    edges = {m["id"]: set() for m in maps}
    starts = {m["id"]: set() for m in maps}     # per map: the scripts that can run once a player is there
    for m in maps:
        data = json.load(open(os.path.join(root, "data", "maps", m["name"], "map.json")))
        for warp in data.get("warp_events", []):
            if warp.get("dest_map") in by_id:
                edges[m["id"]].add(warp["dest_map"])
        for link in data.get("connections") or []:
            if link.get("map") in by_id:
                edges[m["id"]].add(link["map"])
        events = json.load(open(os.path.join(root, "data", "maps", data["shared_events_map"], "map.json"))) \
            if data.get("shared_events_map") else data
        for event in events.get("object_events", []) + events.get("coord_events", []) + events.get("bg_events", []):
            if event.get("script"):
                starts[m["id"]].add(event["script"])
        shared = os.path.join("data", "maps", data.get("shared_scripts_map") or m["name"], "scripts.inc")
        starts[m["id"]].update(scripts.labels_of.get(shared, ()))
    # the scripts the C code starts (data/event_scripts.s's script tables, gStdScripts and the like, are scripts too)
    started = set()
    for path in glob.glob(os.path.join(root, "src", "**", "*.c"), recursive=True):
        started.update(IDENT.findall(open(path, encoding="utf-8", errors="replace").read()))
    run, seen = set(), set()
    todo_scripts = sorted(started & set(scripts.file))
    todo_maps = [START_MAP]
    while todo_scripts or todo_maps:
        while todo_scripts:
            label = todo_scripts.pop()
            if label in run or label not in scripts.file:
                continue
            run.add(label)
            todo_scripts.extend(scripts.refs[label] - run)
            todo_maps.extend(w for w in scripts.warps[label] - seen if w in by_id)
        while todo_maps:
            current = todo_maps.pop()
            if current in seen or current not in by_id:
                continue
            seen.add(current)
            todo_maps.extend(edges[current] - seen)
            todo_scripts.extend(starts[current] - run)
    return seen, run


def trainers(root):
    """Every trainer of the party file (src/data/trainers.party, the Showdown export format): name, class, whether it
    fights doubles, and its party (species, level, held item, moves)."""
    out = {}
    text = open(os.path.join(root, "src", "data", "trainers.party"), encoding="utf-8").read()
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    for match in re.finditer(r"^=== (TRAINER_[A-Z0-9_]+) ===\n(.*?)(?=^===|\Z)", text, flags=re.M | re.S):
        blocks = [b.strip("\n").split("\n") for b in re.split(r"\n\s*\n", match.group(2).strip()) if b.strip()]
        header = dict(line.split(":", 1) for line in (blocks[0] if blocks else []) if ":" in line)
        header = {key.strip(): value.strip() for key, value in header.items()}
        party = []
        for block in blocks[1:]:
            first = block[0].split("@")
            names = [n for n in re.findall(r"\(([^()]+)\)", first[0]) if n not in ("M", "F")]
            fields = dict(line.split(":", 1) for line in block[1:] if ":" in line and not line.startswith("- "))
            level = fields.get("Level", "").strip()
            party.append(dict(species=names[-1] if names else first[0].strip(),
                              item=first[1].strip() if len(first) > 1 else None,
                              level=int(level) if level.isdigit() else None,
                              moves=[line[2:].strip() for line in block[1:] if line.startswith("- ")]))
        double = header.get("Double Battle", header.get("Battle Type", "")).lower() in ("yes", "doubles")
        out[match.group(1)] = dict(id=match.group(1), name=header.get("Name", ""),
                                   trainer_class=header.get("Class", ""), double=double, party=party)
    return out


def rematches(root):
    """{rematch trainer: (the first trainer, its map)} from the rematch table (src/battle_setup.c, gRematchTable): the
    trainers the game picks by itself for a rematch with someone already beaten."""
    out = {}
    text = open(os.path.join(root, "src", "battle_setup.c"), encoding="utf-8").read()
    for match in re.finditer(r"=\s*REMATCH\(([^)]*)\)", text):
        names = [n.strip() for n in match.group(1).split(",")]
        if len(names) == 6 and names[0].startswith("TRAINER_"):
            for later in names[1:5]:
                if later.startswith("TRAINER_") and later != names[0]:
                    out.setdefault(later, (names[0], names[5]))
    return out


def coop_twins(root):
    """{twin leader: (the gym leader, the gym's map)} from this game's co-op gym battles (src/multiplayer_coop.c): the
    second leader a co-op gym battle puts beside the first."""
    text = open(os.path.join(root, "src", "multiplayer_coop.c"), encoding="utf-8").read()
    return {twin: (leader, where) for leader, twin, where in
            re.findall(r"\{\s*(TRAINER_[A-Z0-9_]+),\s*(TRAINER_MP_TWIN_[A-Z0-9_]+),\s*(MAP_[A-Z0-9_]+)\s*\}", text)}


def frontier_brains(root):
    """The Frontier Brains (src/frontier_util.c): the Battle Frontier's C code sets up their battles."""
    text = open(os.path.join(root, "src", "frontier_util.c"), encoding="utf-8").read()
    return set(re.findall(r"\.trainerId\s*=\s*(TRAINER_[A-Z0-9_]+)", text))


def inventory(root):
    scripts = Scripts(root)
    trainer_table = trainers(root)
    tied = set()    # the scripts an object's own script reaches in its file
    placed = collections.defaultdict(list)
    maps = []
    for name, group, number in map_list(root):
        path = os.path.join(root, "data", "maps", name, "map.json")
        if not os.path.exists(path):
            continue
        data = json.load(open(path))
        entry = dict(key=f"map:{data['id']}", id=data["id"], name=name, group=group, num=number,
                     map_type=data.get("map_type"), section=data.get("region_map_section"),
                     shares_events_of=data.get("shared_events_map"), shares_scripts_of=data.get("shared_scripts_map"),
                     objects=[], warps=[], triggers=[], signs=[], hidden_items=[], secret_bases=[])
        for index, obj in enumerate(data.get("object_events", [])):
            local = index + 1
            script = obj.get("script", "")
            own = scripts.local(script) if script in scripts.file else set()
            tied |= own
            fights = [fight for label in sorted(own) for fight in scripts.battles.get(label, ())]
            kind = ("trainer" if obj.get("trainer_type") not in (None, "TRAINER_TYPE_NONE") or fights
                    else "item_ball" if obj.get("graphics_id") == ITEM_BALL
                    else "berry_tree" if obj.get("graphics_id") == "OBJ_EVENT_GFX_BERRY_TREE"
                    else "clone" if obj.get("type") == "clone"
                    else "person" if script and script != "0x0" else "object")
            item = dict(key=f"object:{data['id']}:{local}", local=local, local_name=obj.get("local_id"), kind=kind,
                        graphics=obj.get("graphics_id"), x=obj.get("x"), y=obj.get("y"), script=script,
                        hide_flag=obj.get("flag"), trainer_type=obj.get("trainer_type"),
                        sight=obj.get("trainer_sight_or_berry_tree_id"))
            if fights:
                item["battles"] = [dict(kind=k, trainer=t, partner=p) for k, t, p in fights]
                for k, t, p in fights:
                    for trainer in (t, p) if p else (t,):
                        placed[trainer].append(dict(map=data["id"], local=local, x=obj.get("x"), y=obj.get("y"),
                                                    kind=k, script=script))
            entry["objects"].append(item)
        for index, warp in enumerate(data.get("warp_events", [])):
            entry["warps"].append(dict(key=f"warp:{data['id']}:{index}", index=index, x=warp.get("x"), y=warp.get("y"),
                                       dest_map=warp.get("dest_map"), dest_warp=warp.get("dest_warp_id")))
        for index, event in enumerate(data.get("coord_events", [])):
            entry["triggers"].append(dict(key=f"trigger:{data['id']}:{index}", index=index, type=event.get("type"),
                                          x=event.get("x"), y=event.get("y"), var=event.get("var"),
                                          var_value=event.get("var_value"), script=event.get("script"),
                                          weather=event.get("weather")))
        for index, event in enumerate(data.get("bg_events", [])):
            kind = event.get("type")
            base = dict(index=index, x=event.get("x"), y=event.get("y"))
            if kind == "sign":
                entry["signs"].append(dict(base, key=f"sign:{data['id']}:{index}", script=event.get("script"),
                                           facing=event.get("player_facing_dir")))
            elif kind == "hidden_item":
                entry["hidden_items"].append(dict(base, key=f"hidden_item:{data['id']}:{index}",
                                                  item=event.get("item"), flag=event.get("flag")))
            else:
                entry["secret_bases"].append(dict(base, key=f"secret_base:{data['id']}:{index}",
                                                  base=event.get("secret_base_id")))
        maps.append(entry)
    reached, run = reachable(root, maps, scripts)
    users = collections.defaultdict(list)
    for m in maps:
        if m["shares_events_of"]:
            users[m["shares_events_of"]].append(m)
    for m in maps:
        m["reachable"] = m["id"] in reached
        m["frlg"] = m["name"].endswith("_Frlg")
        if m["frlg"]:
            m["entry"] = "FireRed/LeafGreen: compiled in, not part of the Emerald game"
        elif users.get(m["name"]):
            # its events and scripts are those of the maps that share them (the contest halls of each category)
            m["used_by"] = [u["name"] for u in users[m["name"]]]
            m["entry"] = f"shared: its events are those of {len(m['used_by'])} maps"
            m["reachable"] = m["reachable"] or any(u["id"] in reached for u in users[m["name"]])
        elif not m["reachable"]:
            m["entry"] = next((why for pattern, why in SPECIAL_ENTRY if pattern.search(m["name"])), "unknown")
    scripted = [dict(key=f"scripted_battle:{label}:{t}", label=label, kind=k, trainer=t, partner=p,
                     file=scripts.file[label], runs=label in run, frlg=scripts.frlg(label))
                for label in sorted(scripts.battles) if label not in tied for k, t, p in scripts.battles[label]]
    map_of_folder = {m["name"]: m for m in maps}
    for battle in scripted:
        folder = battle["file"].split("/")[2] if battle["file"].startswith("data/maps/") else None
        battle["map"] = map_of_folder[folder]["id"] if folder in map_of_folder else None
        for trainer in (battle["trainer"], battle["partner"]) if battle["partner"] else (battle["trainer"],):
            placed[trainer].append(dict(map=battle["map"], script=battle["label"], kind=battle["kind"], scripted=True))
    emerald = {m["id"] for m in maps if m["reachable"] and not m["frlg"]}
    for tid, places in placed.items():
        for place in places:
            script_ok = not place.get("scripted") or any(b["label"] == place["script"] and b["runs"] and not b["frlg"]
                                                         for b in scripted)
            place["in_game"] = (place["map"] in emerald or place["map"] is None) and script_ok
    for later, (first, where) in rematches(root).items():
        if any(p["in_game"] for p in placed.get(first, ())):
            placed[later].append(dict(map=where, rematch_of=first, kind="rematch", in_game=where in emerald))
    for twin, (leader, where) in coop_twins(root).items():
        placed[twin].append(dict(map=where, twin_of=leader, kind="coop_gym", in_game=where in emerald))
    brains = frontier_brains(root)
    named = set().union(*scripts.refs.values())     # every identifier a script or the C code names
    for path in glob.glob(os.path.join(root, "src", "**", "*.c"), recursive=True):
        named.update(re.findall(r"\bTRAINER_[A-Z0-9_]+\b", open(path, encoding="utf-8", errors="replace").read()))
    trainer_list = []
    for tid, t in sorted(trainer_table.items()):
        entry = dict(key=f"trainer:{tid}", **t, placements=placed.get(tid, []))
        if not any(p["in_game"] for p in entry["placements"]):
            entry["entry"] = ("battle frontier: a Frontier Brain the C code sets up (frontier_util.c)" if tid in brains
                              else "unused: in the data, fought nowhere"
                              if re.search(r"PLACEHOLDER|UNUSED|^TRAINER_NONE$", tid) or tid not in named
                              else "unknown")
        trainer_list.append(entry)
    return dict(maps=maps, trainers=trainer_list, scripted_battles=scripted)


def summary(inv):
    objects = collections.Counter(o["kind"] for m in inv["maps"] for o in m["objects"])
    entries = collections.Counter(m["entry"].split(":")[0] for m in inv["maps"] if not m["reachable"] and "entry" in m)
    return dict(maps=len(inv["maps"]), maps_reachable=sum(m["reachable"] for m in inv["maps"]),
                maps_entered_otherwise=dict(entries),
                maps_frlg=sum(m["frlg"] for m in inv["maps"]),
                maps_frlg_reachable=sum(m["frlg"] and m["reachable"] for m in inv["maps"]), objects=dict(objects),
                warps=sum(len(m["warps"]) for m in inv["maps"]),
                triggers=sum(len(m["triggers"]) for m in inv["maps"]), signs=sum(len(m["signs"]) for m in inv["maps"]),
                hidden_items=sum(len(m["hidden_items"]) for m in inv["maps"]),
                secret_bases=sum(len(m["secret_bases"]) for m in inv["maps"]), trainers=len(inv["trainers"]),
                trainers_placed=sum(1 for t in inv["trainers"] if t["placements"]),
                trainers_in_game=sum(1 for t in inv["trainers"] if any(p["in_game"] for p in t["placements"])),
                scripted_battles=len(inv["scripted_battles"]),
                scripted_battles_in_game=sum(b["runs"] and not b["frlg"] for b in inv["scripted_battles"]))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", default=ROOT)
    parser.add_argument("--out", required=True)
    args = parser.parse_args()
    inv = inventory(args.root)
    inv["summary"] = summary(inv)
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, "w") as out:
        json.dump(inv, out, indent=1)
    print(json.dumps(inv["summary"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
