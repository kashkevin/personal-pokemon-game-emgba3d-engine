#!/usr/bin/env python3
"""Battle-environment inventory: every map of the Emerald game where a battle can start.

Reads only the game's own data (no ROM, no save):
  data/maps/map_groups.json, data/maps/*/map.json   map headers, people, coord events
  data/layouts/layouts.json, data/layouts/*/map.bin the cells (metatile, collision, elevation)
  data/tilesets/**/metatile_attributes.bin          metatile behaviours
  src/data/wild_encounters.json                     wild encounters per map
  data/maps/*/scripts.inc, data/scripts/*.inc       trainer, scripted and legendary battles
  src/data/trainers.party                           double battles
  src/multiplayer_coop.c                            co-op Gym battles (this game)
  build/diorama/buildings.log                       which layouts have modelled rooms/buildings
  src/data/diorama/diorama_rules.generated.c        what the Diorama rules raise per layout

Writes build/battle_env/inventory.json (one record per battle-capable map, plus the
maps left out and why) and prints a summary. Test tool: nothing here is shipped.

    python3 tools/battle_env/inventory.py [--out build/battle_env/inventory.json]
"""

import argparse
import collections
import json
import os
import re
import struct
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))


def rel(*p):
    return os.path.join(ROOT, *p)


# ── constants read from the headers ─────────────────────────────────────────

def read_enum(path, first):
    """The names of a C enum starting with `first`, in order."""
    text = open(rel(path)).read()
    start = text.index(first)
    body = text[start:text.index('}', start)]
    names = []
    for line in body.splitlines():
        line = line.split('//')[0].strip().rstrip(',')
        if not line:
            continue
        name = line.split('=')[0].strip()
        if re.fullmatch(r'[A-Z0-9_]+', name):
            names.append(name)
    return names


BEHAVIORS = read_enum('include/constants/metatile_behaviors.h', 'MB_NORMAL')
B = {n: i for i, n in enumerate(BEHAVIORS)}

PRIMARY_METATILES = 512


def behaviour_sets():
    def ids(*names):
        return {B[n] for n in names if n in B}
    return {
        'tall_grass': ids('MB_TALL_GRASS'),
        'long_grass': ids('MB_LONG_GRASS'),
        'ash_grass': ids('MB_ASHGRASS'),
        'sand': ids('MB_SAND', 'MB_DEEP_SAND', 'MB_FOOTPRINTS'),
        'water_pond': ids('MB_POND_WATER', 'MB_INTERIOR_DEEP_WATER', 'MB_SOOTOPOLIS_DEEP_WATER',
                          'MB_NO_SURFACING', 'MB_SEAWEED', 'MB_SEAWEED_NO_SURFACING'),
        'water_ocean': ids('MB_OCEAN_WATER', 'MB_DEEP_WATER', 'MB_WATERFALL', 'MB_EASTWARD_CURRENT',
                           'MB_WESTWARD_CURRENT', 'MB_NORTHWARD_CURRENT', 'MB_SOUTHWARD_CURRENT'),
        'shallow_water': ids('MB_SHALLOW_WATER', 'MB_PUDDLE'),
        'ice': ids('MB_ICE', 'MB_THIN_ICE', 'MB_CRACKED_ICE'),
        'mountain': ids('MB_MOUNTAIN_TOP'),
        'cave_encounter': ids('MB_CAVE', 'MB_UNUSED_SOOTOPOLIS_DEEP_WATER', 'MB_INDOOR_ENCOUNTER'),
        'ledge': ids('MB_JUMP_EAST', 'MB_JUMP_WEST', 'MB_JUMP_NORTH', 'MB_JUMP_SOUTH',
                     'MB_JUMP_NORTHEAST', 'MB_JUMP_NORTHWEST', 'MB_JUMP_SOUTHEAST', 'MB_JUMP_SOUTHWEST'),
        'stairs': ids('MB_NON_ANIMATED_DOOR', 'MB_LADDER', 'MB_UP_ESCALATOR', 'MB_DOWN_ESCALATOR',
                      'MB_SOUTH_ARROW_WARP', 'MB_NORTH_ARROW_WARP', 'MB_EAST_ARROW_WARP',
                      'MB_WEST_ARROW_WARP', 'MB_DEEP_SOUTH_WARP', 'MB_AQUA_HIDEOUT_WARP',
                      'MB_LAVARIDGE_GYM_B1F_WARP', 'MB_LAVARIDGE_GYM_1F_WARP',
                      'MB_UP_RIGHT_STAIR_WARP', 'MB_UP_LEFT_STAIR_WARP',
                      'MB_DOWN_RIGHT_STAIR_WARP', 'MB_DOWN_LEFT_STAIR_WARP'),
        'hot_spring': ids('MB_HOT_SPRINGS'),
        'bridge': ids('MB_PACIFIDLOG_VERTICAL_LOG_TOP', 'MB_PACIFIDLOG_VERTICAL_LOG_BOTTOM',
                      'MB_PACIFIDLOG_HORIZONTAL_LOG_LEFT', 'MB_PACIFIDLOG_HORIZONTAL_LOG_RIGHT',
                      'MB_FORTREE_BRIDGE', 'MB_BRIDGE_OVER_OCEAN', 'MB_BRIDGE_OVER_POND_LOW',
                      'MB_BRIDGE_OVER_POND_MED', 'MB_BRIDGE_OVER_POND_HIGH',
                      'MB_BRIDGE_OVER_POND_MED_EDGE_1', 'MB_BRIDGE_OVER_POND_MED_EDGE_2',
                      'MB_BRIDGE_OVER_POND_HIGH_EDGE_1', 'MB_BRIDGE_OVER_POND_HIGH_EDGE_2'),
        'counter': ids('MB_COUNTER', 'MB_POKEBLOCK_FEEDER', 'MB_PC', 'MB_TELEVISION',
                       'MB_BOOKSHELF', 'MB_POKEMON_CENTER_BOOKSHELF', 'MB_SHOP_SHELF',
                       'MB_BLUEPRINT', 'MB_CABINET', 'MB_KITCHEN', 'MB_DRESSER', 'MB_SNACKS',
                       'MB_TRASH_CAN', 'MB_VASE', 'MB_PICTURE_BOOK_SHELF', 'MB_QUESTIONNAIRE',
                       'MB_TRAINER_HILL_TIMER', 'MB_CABLE_BOX_RESULTS_1', 'MB_CABLE_BOX_RESULTS_2'),
    }


BSETS = behaviour_sets()


# ── tilesets ───────────────────────────────────────────────────────────────

def tileset_attribute_paths():
    """gTileset_X -> metatile_attributes.bin path."""
    headers = open(rel('src/data/tilesets/headers.h')).read()
    metatiles = open(rel('src/data/tilesets/metatiles.h')).read()
    attr_path = dict(re.findall(r'(gMetatileAttributes_\w+)\[\]\s*=\s*INCBIN_U\d+\("([^"]+)"\)', metatiles))
    out = {}
    for name, body in re.findall(r'const struct Tileset (gTileset_\w+)\s*=\s*\{(.*?)\};', headers, re.S):
        m = re.search(r'\.metatileAttributes\s*=\s*(gMetatileAttributes_\w+)', body)
        if m and m.group(1) in attr_path:
            out[name] = attr_path[m.group(1)]
    return out


class Tilesets:
    def __init__(self):
        self.paths = tileset_attribute_paths()
        self.cache = {}

    def behaviours(self, name):
        if name not in self.cache:
            path = self.paths.get(name)
            data = []
            if path and os.path.exists(rel(path)):
                raw = open(rel(path), 'rb').read()
                n = len(raw) // 2
                data = [v & 0xFF for v in struct.unpack('<%dH' % n, raw[:n * 2])]
            self.cache[name] = data
        return self.cache[name]

    def behaviour(self, primary, secondary, metatile):
        if metatile < PRIMARY_METATILES:
            b = self.behaviours(primary)
            return b[metatile] if metatile < len(b) else 0
        b = self.behaviours(secondary)
        i = metatile - PRIMARY_METATILES
        return b[i] if i < len(b) else 0


# ── maps, layouts ──────────────────────────────────────────────────────────

def load_maps():
    groups = json.load(open(rel('data/maps/map_groups.json')))
    maps = []
    for gi, gname in enumerate(groups['group_order']):
        for mi, folder in enumerate(groups[gname]):
            path = rel('data/maps', folder, 'map.json')
            if not os.path.exists(path):
                continue
            m = json.load(open(path))
            m['_folder'] = folder
            m['_group'] = gi
            m['_num'] = mi
            m['_group_name'] = gname
            m['_frlg'] = gname.endswith('_Frlg') or folder.endswith('_Frlg')
            maps.append(m)
    return maps


def load_layouts():
    data = json.load(open(rel('data/layouts/layouts.json')))
    out = {}
    for index, lay in enumerate(data['layouts']):
        if not lay:
            continue
        lay = dict(lay)
        lay['_number'] = index + 1
        out[lay['id']] = lay
    return out


def read_cells(layout):
    raw = open(rel(layout['blockdata_filepath']), 'rb').read()
    w, h = layout['width'], layout['height']
    vals = struct.unpack('<%dH' % (w * h), raw[:w * h * 2])
    return [[vals[y * w + x] for x in range(w)] for y in range(h)]


# ── battles from data ──────────────────────────────────────────────────────

def load_wild():
    data = json.load(open(rel('src/data/wild_encounters.json')))
    out = collections.defaultdict(dict)
    for group in data['wild_encounter_groups']:
        label = group.get('label')
        for enc in group['encounters']:
            mp = enc.get('map')
            if not mp:
                continue
            for kind in ('land_mons', 'water_mons', 'rock_smash_mons', 'fishing_mons', 'hidden_mons'):
                if kind in enc:
                    mons = enc[kind].get('mons', [])
                    rate = enc[kind].get('encounter_rate', 0)
                    entry = out[mp].setdefault(kind, {'rate': rate, 'species': set(), 'tables': 0, 'group': label})
                    entry['tables'] += 1
                    for mon in mons:
                        entry['species'].add(mon.get('species'))
    return out


def load_double_trainers():
    path = rel('src/data/trainers.party')
    doubles = set()
    if not os.path.exists(path):
        return doubles
    current = None
    for line in open(path, errors='replace'):
        m = re.match(r'===\s*(TRAINER_\w+)\s*===', line)
        if m:
            current = m.group(1)
            continue
        if current and re.match(r'\s*Double Battle:\s*Yes', line, re.I):
            doubles.add(current)
    return doubles


SCRIPT_PATTERNS = [
    ('trainer', re.compile(r'^\s*(trainerbattle\w*|dotrainerbattle)\s+(TRAINER_\w+)?', re.M)),
    ('multi', re.compile(r'^\s*multi_\w+\s+(TRAINER_\w+)?', re.M)),
    ('facility', re.compile(r'^\s*(dofacilitytrainerbattle|facilitytrainerbattle|frontier_\w*battle\w*|tower_\w*battle\w*|special\s+DoSpecialTrainerBattle)', re.M)),
    ('wild', re.compile(r'^\s*setwildbattle\s+(SPECIES_\w+)', re.M)),
    ('legendary', re.compile(r'^\s*special\s+(BattleSetup_StartLegendaryBattle|StartRegiBattle|BattleSetup_StartLatiBattle|StartGroudonKyogreBattle|StartRayquazaBattle|StartSouthernIslandBattle)', re.M)),
    ('scripted', re.compile(r'^\s*special\s+(StartWallyTutorialBattle|BattleSetup_StartRoamerBattle)', re.M)),
]


LABEL = re.compile(r'^(\w+)::?\s*$', re.M)
JUMP = re.compile(r'^\s*(?:goto|call|goto_if\w*|call_if\w*|goto_if_\w+|call_if_\w+)\b[^\n]*?(\w+_EventScript_\w+|EventScript_\w+|\w+_Script_\w+)', re.M)


def split_labels(text):
    out = {}
    marks = [(m.start(), m.group(1)) for m in LABEL.finditer(text)]
    for i, (pos, name) in enumerate(marks):
        end = marks[i + 1][0] if i + 1 < len(marks) else len(text)
        out[name] = text[pos:end]
    return out


SHARED = {}
for _f in sorted(os.listdir(rel('data/scripts'))):
    if _f.endswith('.inc') and 'frlg' not in _f.lower():
        SHARED.update(split_labels(open(rel('data/scripts', _f), errors='replace').read()))


def reachable_text(labels, own):
    """The script text reachable from the labels a map's events use, through goto/call into the
    map's own scripts and the shared ones (data/scripts), a few jumps deep."""
    seen, todo, parts = set(), list(labels), []
    depth = {l: 0 for l in labels}
    while todo:
        l = todo.pop()
        if l in seen:
            continue
        seen.add(l)
        block = own.get(l) or SHARED.get(l)
        if block is None:
            continue
        if l not in own:
            parts.append(block)
        if depth[l] >= 4:
            continue
        for target in JUMP.findall(block):
            if target not in depth:
                depth[target] = depth[l] + 1
                todo.append(target)
    return '\n'.join(parts)


def scan_scripts(folder, m=None):
    path = rel('data/maps', folder, 'scripts.inc')
    found = collections.defaultdict(list)
    text = open(path, errors='replace').read() if os.path.exists(path) else ''
    own = split_labels(text)
    if m is not None:
        labels = [e.get('script') for key in ('object_events', 'bg_events', 'coord_events')
                  for e in m.get(key, []) if e.get('script') and e.get('script') != '0x0']
        labels += [l for l in own if 'OnFrame' in l or 'OnTransition' in l or 'OnLoad' in l]
        text = text + '\n' + reachable_text(labels + list(own), own)
    for kind, pat in SCRIPT_PATTERNS:
        for m in pat.finditer(text):
            found[kind].append(m.group(m.lastindex) if m.lastindex else m.group(0).strip())
    # legendary species set right before the special
    found['wild_species'] = re.findall(r'setwildbattle\s+(SPECIES_\w+)', text)
    return found


def coop_gym_maps():
    path = rel('src/multiplayer_coop.c')
    if not os.path.exists(path):
        return set()
    return set(re.findall(r'(MAP_[A-Z0-9_]+GYM[A-Z0-9_]*)\s*\}', open(path).read()))


def modelled_layouts():
    """Layout numbers with modelled buildings or rooms (3DS generator pages)."""
    path = rel('build/diorama/buildings.log')
    out = set()
    if os.path.exists(path):
        for m in re.finditer(r'page for layout\s+(\d+):', open(path).read()):
            out.add(int(m.group(1)))
    return out


ARCHETYPES = read_enum('include/diorama/rules.h', 'DIORAMA_ARCHETYPE_NONE')
RAISED = {a for a in ARCHETYPES if a not in ('DIORAMA_ARCHETYPE_NONE', 'DIORAMA_ARCHETYPE_GROUND',
                                             'DIORAMA_ARCHETYPE_VOID', 'DIORAMA_ARCHETYPE_WATER',
                                             'DIORAMA_ARCHETYPE_SHALLOW_WATER', 'DIORAMA_ARCHETYPE_CURRENT',
                                             'DIORAMA_ARCHETYPE_HOT_SPRING', 'DIORAMA_ARCHETYPE_CLAIM_ONLY',
                                             'DIORAMA_ARCHETYPE_GRASS', 'DIORAMA_ARCHETYPE_FLOWER')}


def rules_terrain_by_layout():
    """Per layout id: counts of the static terrain records by archetype name."""
    path = rel('src/data/diorama/diorama_rules.generated.c')
    if not os.path.exists(path):
        return {}
    layouts = []
    terrain_arch = []
    section = None
    for line in open(path):
        if line.startswith('const struct DioramaGeneratedLayoutV2'):
            section = 'layout'
            continue
        if line.startswith('const struct DioramaGeneratedTerrainV2'):
            section = 'terrain'
            continue
        if line.startswith('const '):
            section = None
            continue
        if section == 'layout' and line.strip().startswith('{'):
            f = [v.strip() for v in line.strip().strip('{},').split(',')]
            layouts.append((int(f[0]), int(f[5]), int(f[6]), f[8].strip('" }')))
        elif section == 'terrain' and line.strip().startswith('{'):
            f = line.strip().strip('{},').split(',')
            terrain_arch.append(int(f[19]))
    out = {}
    for lid, off, count, symbol in layouts:
        c = collections.Counter(ARCHETYPES[a] if a < len(ARCHETYPES) else str(a)
                                for a in terrain_arch[off:off + count])
        out[symbol] = c
    return out


# ── environment ────────────────────────────────────────────────────────────

LEAGUE_ROOMS = {
    'MAP_EVER_GRANDE_CITY_SIDNEYS_ROOM': 'elite_four', 'MAP_EVER_GRANDE_CITY_PHOEBES_ROOM': 'elite_four',
    'MAP_EVER_GRANDE_CITY_GLACIAS_ROOM': 'elite_four', 'MAP_EVER_GRANDE_CITY_DRAKES_ROOM': 'elite_four',
    'MAP_EVER_GRANDE_CITY_CHAMPIONS_ROOM': 'champion',
}

NAME_RULES = [
    (r'_GYM', 'gym'),
    (r'^MAP_EVER_GRANDE_CITY_(HALL|POKEMON_LEAGUE|HALL_OF_FAME)', 'league_hall'),
    (r'^MAP_BATTLE_PYRAMID_SQUARE|^MAP_BATTLE_FRONTIER_BATTLE_PYRAMID', 'frontier_pyramid'),
    (r'^MAP_BATTLE_FRONTIER_OUTSIDE', 'frontier_outdoor'),
    (r'^MAP_BATTLE_FRONTIER_', 'frontier'),
    (r'^MAP_BATTLE_COLOSSEUM|^MAP_UNION_ROOM', 'link_room'),
    (r'_BATTLE_TENT', 'battle_tent'),
    (r'^MAP_TRAINER_HILL', 'trainer_hill'),
    (r'^MAP_AQUA_HIDEOUT', 'hideout_aqua'),
    (r'^MAP_MAGMA_HIDEOUT', 'hideout_magma'),
    (r'^MAP_MT_PYRE_(\d|[1-6]F)', 'tower'),
    (r'^MAP_SKY_PILLAR_(?!OUTSIDE|TOP)', 'tower'),
    (r'^MAP_MIRAGE_TOWER', 'tower'),
    (r'^MAP_SS_TIDAL|^MAP_ABANDONED_SHIP', 'ship'),
    (r'_POKEMON_CENTER', 'center'),
    (r'_MART$|_DEPARTMENT_STORE', 'mart'),
    (r'_CONTEST_HALL|_CONTEST_LOBBY', 'contest'),
    (r'^MAP_TRICK_HOUSE|^MAP_ROUTE110_TRICK_HOUSE', 'trick_house'),
    (r'^MAP_SECRET_BASE', 'secret_base'),
    (r'_LAB\b|_LAB_|PROFESSOR_BIRCHS_LAB', 'lab'),
    (r'_HOUSE|_FLAT|_MOTEL|_COTTAGE|_HOUSE\d', 'house'),
    (r'^MAP_(DESERT_RUINS|ISLAND_CAVE|ANCIENT_TOMB|SEALED_CHAMBER)', 'ruins_chamber'),
    (r'^MAP_(SHOAL_CAVE)', 'cave_ice'),
    (r'^MAP_(FIERY_PATH|MAGMA_HIDEOUT|SCORCHED_SLAB)', 'cave_heat'),
    (r'^MAP_(METEOR_FALLS|SEAFLOOR_CAVERN|CAVE_OF_ORIGIN|MARINE_CAVE)', 'cave_water'),
    (r'^MAP_(RUSTURF_TUNNEL|DESERT_UNDERPASS)', 'tunnel'),
    (r'^MAP_(SAFARI_ZONE)', 'safari'),
    (r'^MAP_(PETALBURG_WOODS)', 'forest'),
    (r'^MAP_(JAGGED_PASS|MT_CHIMNEY)', 'mountain'),
    (r'^MAP_(MT_PYRE_EXTERIOR|MT_PYRE_SUMMIT)', 'mountain_fog'),
    (r'^MAP_(SKY_PILLAR_OUTSIDE|SKY_PILLAR_TOP)', 'sky_pillar_top'),
    (r'^MAP_(SOUTHERN_ISLAND|FARAWAY_ISLAND|BIRTH_ISLAND|NAVEL_ROCK)', 'island'),
    (r'^MAP_NEW_MAUVILLE', 'power_plant'),
    (r'^MAP_(UNDERWATER)', 'underwater'),
]

OUTDOOR_TYPES = {'MAP_TYPE_ROUTE', 'MAP_TYPE_TOWN', 'MAP_TYPE_CITY', 'MAP_TYPE_OCEAN_ROUTE'}


def classify(m, stats, weathers):
    mid = m['id']
    mtype = m.get('map_type')
    if mid in LEAGUE_ROOMS:
        return LEAGUE_ROOMS[mid]
    if m.get('battle_scene') == 'MAP_BATTLE_SCENE_GYM' and not mid.startswith('MAP_BATTLE_PYRAMID'):
        return 'gym'
    for pat, cat in NAME_RULES:
        if re.search(pat, mid):
            return cat
    if mtype == 'MAP_TYPE_UNDERWATER':
        return 'underwater'
    if mtype == 'MAP_TYPE_SECRET_BASE':
        return 'secret_base'
    if mtype == 'MAP_TYPE_UNDERGROUND':
        if stats['ice']:
            return 'cave_ice'
        if stats['water'] > 0.15:
            return 'cave_water'
        return 'cave'
    if mtype == 'MAP_TYPE_INDOOR':
        return 'building'
    # outdoors
    if 'WEATHER_SANDSTORM' in weathers:
        return 'desert'
    if 'WEATHER_VOLCANIC_ASH' in weathers or stats['ash_grass'] > 0.02:
        return 'volcanic_ash'
    if mtype == 'MAP_TYPE_OCEAN_ROUTE' or stats['water'] > 0.55:
        return 'ocean'
    rainy = {'WEATHER_RAIN', 'WEATHER_DOWNPOUR', 'WEATHER_RAIN_THUNDERSTORM', 'WEATHER_ROUTE119_CYCLE',
             'WEATHER_ROUTE123_CYCLE'} & weathers
    if rainy:
        return 'rainy_route' if mtype == 'MAP_TYPE_ROUTE' else 'rainy_town'
    if {'WEATHER_FOG_HORIZONTAL', 'WEATHER_FOG_DIAGONAL'} & weathers:
        return 'foggy'
    if stats['sand'] > 0.04 and (stats['water'] > 0.08 or mtype in ('MAP_TYPE_TOWN', 'MAP_TYPE_CITY')):
        return 'beach_town' if mtype in ('MAP_TYPE_TOWN', 'MAP_TYPE_CITY') else 'beach'
    if stats['water'] > 0.2:
        return 'coastal'
    if mtype in ('MAP_TYPE_TOWN', 'MAP_TYPE_CITY'):
        return 'town' if mtype == 'MAP_TYPE_TOWN' else 'city'
    if stats['tall_grass'] + stats['long_grass'] > 0.02:
        return 'route_grass'
    return 'route'


# ── the battle environment the game itself picks (BattleSetup_GetEnvironmentId) ──

SURFABLE = {B[n] for n in ('MB_POND_WATER', 'MB_INTERIOR_DEEP_WATER', 'MB_DEEP_WATER', 'MB_WATERFALL',
                           'MB_SOOTOPOLIS_DEEP_WATER', 'MB_OCEAN_WATER', 'MB_NO_SURFACING', 'MB_SEAWEED',
                           'MB_SEAWEED_NO_SURFACING', 'MB_EASTWARD_CURRENT', 'MB_WESTWARD_CURRENT',
                           'MB_NORTHWARD_CURRENT', 'MB_SOUTHWARD_CURRENT') if n in B}
DEEP_OR_OCEAN = {B[n] for n in ('MB_OCEAN_WATER', 'MB_INTERIOR_DEEP_WATER', 'MB_DEEP_WATER') if n in B}


def game_environment(m, behaviour, weathers, trainer_scene=False):
    """BattleSetup_GetEnvironmentId at a cell (src/battle_setup.c); a trainer battle's backdrop
    then follows the map's battle scene (gym, Elite Four, Frontier...)."""
    mtype = m.get('map_type')
    if trainer_scene and m.get('battle_scene') not in (None, 'MAP_BATTLE_SCENE_NORMAL'):
        return 'SCENE_' + m['battle_scene'].replace('MAP_BATTLE_SCENE_', '')
    if behaviour == B.get('MB_TALL_GRASS'):
        return 'GRASS'
    if behaviour == B.get('MB_LONG_GRASS'):
        return 'LONG_GRASS'
    if behaviour in (B.get('MB_SAND'), B.get('MB_DEEP_SAND')):
        return 'SAND'
    if mtype == 'MAP_TYPE_UNDERGROUND':
        if behaviour == B.get('MB_INDOOR_ENCOUNTER'):
            return 'BUILDING'
        if behaviour in SURFABLE:
            return 'POND'
        return 'CAVE'
    if mtype in ('MAP_TYPE_INDOOR', 'MAP_TYPE_SECRET_BASE'):
        return 'BUILDING'
    if mtype == 'MAP_TYPE_UNDERWATER':
        return 'UNDERWATER'
    if mtype == 'MAP_TYPE_OCEAN_ROUTE':
        return 'WATER' if behaviour in SURFABLE else 'PLAIN'
    if behaviour in DEEP_OR_OCEAN:
        return 'WATER'
    if behaviour in SURFABLE:
        return 'POND'
    if behaviour == B.get('MB_MOUNTAIN_TOP'):
        return 'MOUNTAIN'
    if m['id'] == 'MAP_ROUTE113' or 'WEATHER_SANDSTORM' in weathers:
        return 'SAND'
    return 'PLAIN'


# ── test positions ─────────────────────────────────────────────────────────

def pick_position(cells, layout, tilesets, want):
    """The cell farthest from walls and edges among the passable cells that satisfy `want`
    (a behaviour predicate): where a battle there has the most room around it."""
    w, h = layout['width'], layout['height']
    prim, sec = layout['primary_tileset'], layout['secondary_tileset']
    beh = [[tilesets.behaviour(prim, sec, cells[y][x] & 0x3FF) for x in range(w)] for y in range(h)]
    passable = [[((cells[y][x] >> 10) & 3) == 0 for x in range(w)] for y in range(h)]
    # distance to the nearest impassable cell or edge (chessboard, capped)
    INF = 99
    dist = [[INF] * w for _ in range(h)]
    from collections import deque
    q = deque()
    for y in range(h):
        for x in range(w):
            if not passable[y][x] or x == 0 or y == 0 or x == w - 1 or y == h - 1:
                dist[y][x] = 0
                q.append((x, y))
    while q:
        x, y = q.popleft()
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                nx, ny = x + dx, y + dy
                if 0 <= nx < w and 0 <= ny < h and dist[ny][nx] > dist[y][x] + 1:
                    dist[ny][nx] = dist[y][x] + 1
                    q.append((nx, ny))
    best = None
    for y in range(h):
        for x in range(w):
            if not want(beh[y][x], passable[y][x]):
                continue
            # prefer room in front (north) of the player: count passable cells up to 6 north
            north = 0
            for d in range(1, 7):
                if y - d >= 0 and passable[y - d][x]:
                    north += 1
                else:
                    break
            score = min(dist[y][x], 6) * 10 + north
            if best is None or score > best[0]:
                best = (score, x, y, BEHAVIORS[beh[y][x]] if beh[y][x] < len(BEHAVIORS) else beh[y][x])
    return best


def layout_stats(cells, layout, tilesets):
    w, h = layout['width'], layout['height']
    prim, sec = layout['primary_tileset'], layout['secondary_tileset']
    counts = collections.Counter()
    elev = collections.Counter()
    blocked = 0
    for y in range(h):
        for x in range(w):
            v = cells[y][x]
            b = tilesets.behaviour(prim, sec, v & 0x3FF)
            for k, s in BSETS.items():
                if b in s:
                    counts[k] += 1
            if (v >> 10) & 3:
                blocked += 1
            else:
                elev[(v >> 12) & 0xF] += 1
    n = float(w * h)
    stats = {k: counts[k] / n for k in BSETS}
    stats['water'] = (counts['water_pond'] + counts['water_ocean']) / n
    stats['blocked'] = blocked / n
    stats['elevations'] = sorted(e for e, c in elev.items() if c >= 4)
    return stats


# ── main ───────────────────────────────────────────────────────────────────

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=rel('build/battle_env/inventory.json'))
    args = ap.parse_args()

    maps = load_maps()
    layouts = load_layouts()
    tilesets = Tilesets()
    wild = load_wild()
    doubles = load_double_trainers()
    coop = coop_gym_maps()
    modelled = modelled_layouts()
    rules = rules_terrain_by_layout()

    records, skipped = [], []
    for m in maps:
        mid = m['id']
        if m['_frlg']:
            skipped.append({'map': mid, 'why': 'FireRed/LeafGreen map group (not part of the Emerald game)'})
            continue
        lay = layouts.get(m['layout'])
        if lay is None:
            skipped.append({'map': mid, 'why': 'no layout'})
            continue
        sc = scan_scripts(m['_folder'], m)
        trainers_events = [o for o in m.get('object_events', [])
                           if o.get('trainer_type') not in (None, 'TRAINER_TYPE_NONE', '0')]
        w = wild.get(mid, {})
        sources = []
        if w.get('land_mons'):
            sources.append('wild_land')
        if w.get('water_mons'):
            sources.append('wild_water')
        if w.get('rock_smash_mons'):
            sources.append('wild_rock_smash')
        if w.get('fishing_mons'):
            sources.append('wild_fishing')
        if w.get('hidden_mons'):
            sources.append('wild_hidden')
        trainer_ids = sorted({t for t in sc['trainer'] + sc['multi'] if t and t.startswith('TRAINER_')})
        if trainer_ids or trainers_events:
            sources.append('trainer')
        if sc['multi']:
            sources.append('multi_battle')
        if sc['facility']:
            sources.append('facility')
        if sc['legendary']:
            sources.append('legendary')
        if sc['wild'] and not sc['legendary']:
            sources.append('scripted_wild')
        if sc['scripted']:
            sources.append('scripted')
        if mid in coop:
            sources.append('coop_gym')
        cat_guess = None
        if not sources:
            # battle-capable by design though its scripts live elsewhere
            if re.search(r'^MAP_(BATTLE_FRONTIER_BATTLE_\w+_(BATTLE_ROOM|FLOOR|TOP)|BATTLE_FRONTIER_BATTLE_PIKE_ROOM_\w+'
                         r'|BATTLE_PYRAMID_SQUARE\d+|\w+_BATTLE_TENT_BATTLE_ROOM|TRAINER_HILL_(\dF|ROOF))', mid):
                sources.append('facility')
            elif re.search(r'^MAP_(SECRET_BASE_\w+)', mid):
                sources.append('secret_base_battle')
            elif re.search(r'^MAP_(BATTLE_COLOSSEUM_\w+|UNION_ROOM)', mid):
                sources.append('link_battle')
        if not sources:
            skipped.append({'map': mid, 'why': 'no wild encounters, trainers or scripted battles found'})
            continue

        path = rel('data/maps', m['_folder'], 'scripts.inc')
        cells = read_cells(lay)
        stats = layout_stats(cells, lay, tilesets)
        weathers = {m.get('weather', 'WEATHER_NONE')}
        for ev in m.get('coord_events', []):
            if ev.get('type') == 'weather':
                weathers.add(ev['weather'].replace('COORD_EVENT_WEATHER_', 'WEATHER_'))
        path = rel('data/maps', m['_folder'], 'scripts.inc')
        if os.path.exists(path):
            weathers |= set(re.findall(r'setweather\s+(WEATHER_\w+)', open(path, errors='replace').read()))
        weathers.discard('WEATHER_NONE')
        cat = classify(m, stats, weathers)

        # where to stand for the test battle
        def grass(b, p):
            return p and (b in BSETS['tall_grass'] or b in BSETS['long_grass'] or b in BSETS['ash_grass'])

        def water(b, p):
            return b in BSETS['water_pond'] or b in BSETS['water_ocean']

        def floor(b, p):
            return p and b not in BSETS['stairs'] and b not in BSETS['ledge'] \
                and b not in BSETS['water_pond'] and b not in BSETS['water_ocean']

        positions = {}
        if 'wild_land' in sources:
            p = pick_position(cells, lay, tilesets, grass) if m.get('map_type') in OUTDOOR_TYPES else None
            positions['wild_land'] = p or pick_position(cells, lay, tilesets, floor)
        if 'wild_water' in sources:
            positions['wild_water'] = pick_position(cells, lay, tilesets, water)
        anchors = trainers_events
        if not anchors and 'trainer' in sources:
            # trainers started from their own script (Elite Four, leaders, rivals): the people
            # whose script reaches a trainer battle
            own = split_labels(open(path, errors='replace').read()) if os.path.exists(path) else {}
            anchors = [o for o in m.get('object_events', [])
                       if o.get('script') and re.search(r'trainerbattle|multi_2_vs',
                                                        reachable_text([o['script']], own) + own.get(o['script'], ''))]
        if anchors:
            # in front of a trainer: the floor cell nearest the middle of the trainers, two below
            tx = sum(o['x'] for o in anchors) / len(anchors)
            ty = sum(o['y'] for o in anchors) / len(anchors)

            def near(b, p, tx=tx, ty=ty):
                return floor(b, p)
            best = None
            for y in range(lay['height']):
                for x in range(lay['width']):
                    v = cells[y][x]
                    b = tilesets.behaviour(lay['primary_tileset'], lay['secondary_tileset'], v & 0x3FF)
                    if not near(b, ((v >> 10) & 3) == 0):
                        continue
                    if any(o['x'] == x and o['y'] == y for o in m.get('object_events', [])):
                        continue
                    d = (x - tx) ** 2 + (y - ty - 2) ** 2
                    if best is None or d < best[0]:
                        best = (d, x, y, BEHAVIORS[b] if b < len(BEHAVIORS) else b)
            if best:
                positions['trainer'] = (0, best[1], best[2], best[3])
        if not positions:
            positions['trainer' if 'trainer' in sources else 'any'] = pick_position(cells, lay, tilesets, floor)

        rc = rules.get(lay['id'], collections.Counter())
        raised = sum(c for a, c in rc.items() if a in RAISED)
        records.append({
            'map': mid,
            'name': m.get('name'),
            'folder': m['_folder'],
            'group': m['_group'], 'num': m['_num'],
            'layout': lay['id'], 'layout_number': lay['_number'],
            'size': [lay['width'], lay['height']],
            'tilesets': [lay['primary_tileset'], lay['secondary_tileset']],
            'map_type': m.get('map_type'),
            'battle_scene': m.get('battle_scene'),
            'region': m.get('region_map_section'),
            'requires_flash': bool(m.get('requires_flash')),
            'weathers': sorted(weathers),
            'category': cat,
            'sources': sources,
            'trainers': trainer_ids,
            'trainer_events': len(trainers_events),
            'double_trainers': sorted(t for t in trainer_ids if t in doubles)
                               + sorted({t for t in sc['trainer'] if t} & set()),
            'twin_or_double_scripts': len(re.findall(r'trainerbattle_(double|rematch_double)',
                                                     open(path, errors='replace').read())) if os.path.exists(path) else 0,
            'legendary_species': sc['wild_species'] if sc['legendary'] else [],
            'wild': {k: {'rate': v['rate'], 'species': len(v['species'])} for k, v in w.items()},
            'stats': {k: round(v, 3) if isinstance(v, float) else v for k, v in stats.items()},
            'diorama': {
                'modelled_rooms_or_buildings': lay['_number'] in modelled,
                'rules_raised_cells': raised,
                'rules_archetypes': {a.replace('DIORAMA_ARCHETYPE_', '').lower(): c
                                     for a, c in rc.most_common() if a in RAISED},
            },
            'test_positions': {k: {'x': v[1], 'y': v[2], 'behaviour': v[3],
                                   'environment': game_environment(
                                       m, B.get(v[3], 0) if isinstance(v[3], str) else v[3], weathers,
                                       trainer_scene=(k == 'trainer'))}
                               for k, v in positions.items() if v},
        })

    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    json.dump({'maps': records, 'skipped': skipped,
               'counts': {'battle_capable': len(records), 'skipped': len(skipped)}},
              open(args.out, 'w'), indent=1)

    by_cat = collections.Counter(r['category'] for r in records)
    print('battle-capable maps: %d (skipped %d: %s)' % (
        len(records), len(skipped),
        dict(collections.Counter(s['why'].split(' (')[0] for s in skipped))))
    for cat, n in sorted(by_cat.items(), key=lambda kv: -kv[1]):
        mod = sum(1 for r in records if r['category'] == cat and r['diorama']['modelled_rooms_or_buildings'])
        print('  %-16s %3d   modelled layouts %3d' % (cat, n, mod))
    print('wrote', args.out)


if __name__ == '__main__':
    main()
