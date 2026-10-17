#!/usr/bin/env python3
"""Field check of the Update 31 rival checkpoints and the streamlined introduction, through the game's own scripts.

Runs the native game headless under xvfb. The field scene (PKMN_DEBUG_SCENE=field) puts the player next to a rival
scene; PKMN_DEBUG_RIVAL gives the game the host's RivalRecord as the companion would write it into the world mailbox
(the game then behaves as connected to a world with rival checkpoints); PKMN_DEBUG_RIVAL_SET sets save variables and
flags; PKMN_DEBUG_KEYS walks and presses A. The game logs "rival F: ..." lines (place, battle, environment, Dowsing
Machine, HM02, watched variables and flags, a map object) and "rival: ..." trace lines; each scenario states what
must be true:

  - Route 110 / Route 119: an armed trigger line stops a northbound player and opens the Players menu; a pass gives
    the original scene's reward exactly once (Dowsing Machine; HM02 Fly, FLAG_RECEIVED_HM_FLY and Scott's scene);
    a checkpoint the world completed lets the player through without the reward; going back is never stopped;
    offline the original NPC rival battle starts (the original scene is untouched);
  - Route 103: the NPC rival stays away in multiplayer; a pass sets the lab state Birch's Pokedex scene needs and
    grants neither the Pokedex nor Poke Balls;
  - Lilycove: the NPC rival stays away; a pass sends the rival home (flags);
  - the battle: a battle whose preparation names a rival checkpoint takes that checkpoint's environment (PLAIN) even
    on tall grass, where an ordinary battle draws GRASS;
  - the streamlined introduction: New Game goes to the 18-starter screen and on to Littleroot in front of the
    player's house with a level 5 starter met on Route 101, Birch's lab state 3, no Pokedex / Poke Balls / Running
    Shoes, and one save.

    tools/pc_port/rival_check.py [--binary ./pokeemerald-mp] [--only name,name]

Needs xvfb-run. A simulation on a software display with a simulated host record: no real host, network or PC.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
import threading

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
LINE = re.compile(r"rival (\d+): map (\d+)\.(\d+) (-?\d+),(-?\d+) dir (\d+) \| battle (\d+) env (\d+) trainer (\d+) \| "
                  r"dowsing (\d+) hmfly (\d+) balls (\d+) party (\d+)(.*)")


def constants():
    """VAR_* and FLAG_* values as the compiler sees them (some flags are expressions of others)."""
    out = subprocess.run(["gcc", "-E", "-dM", "-iquote", os.path.join(ROOT, "include"), "-x", "c", "-"],
                         input='#include "constants/global.h"\n#include "constants/flags.h"\n#include "constants/vars.h"\n',
                         capture_output=True, text=True, cwd=ROOT, check=True).stdout
    macros = dict(re.findall(r"^#define (\w+) (.+)$", out, re.M))
    values = {}

    def value(name, depth=0):
        if name in values:
            return values[name]
        text = re.sub(r"//.*|/\*.*?\*/", "", macros[name]).strip()
        expr = re.sub(r"\b([A-Za-z_]\w*)\b", lambda m: str(value(m.group(1), depth + 1)), text)
        if not re.fullmatch(r"[0-9xXa-fA-F()+\-*/<>| ]+", expr) or depth > 20:
            raise ValueError(f"cannot evaluate {name}: {text}")
        values[name] = int(eval(expr.replace("/", "//")))
        return values[name]

    class Lazy(dict):
        def __missing__(self, name):
            return value(name)

    return Lazy()


C = constants()


def rival_trainers():
    """The May/Brendan trainer IDs of the five rival scenes (include/constants/opponents.h)."""
    text = open(os.path.join(ROOT, "include/constants/opponents.h")).read()
    return {int(v, 0) for v in re.findall(r"#define TRAINER_(?:MAY|BRENDAN)_(?:ROUTE_103|RUSTBORO|ROUTE_110|ROUTE_119|LILYCOVE)_\w+\s+(\d+)", text)}


RIVAL_TRAINERS = rival_trainers()
DOWN, NORTH = 1, 2


def ref(name):
    return ("v%x" if name.startswith("VAR_") else "f%x") % C[name]


def setup(**values):
    return ",".join(f"{ref(name)}={int(value)}" for name, value in values.items())


def hold(key, start, frames=40, step=4):
    return [f"{f}:{key}" for f in range(start, start + frames, step)]


def presses(start, count, period=50):
    return [f"{start + i * period}:A" for i in range(count)]


class Run:
    def __init__(self, rows, lines):
        self.rows, self.lines = rows, lines

    @property
    def first(self):
        return self.rows[0]

    @property
    def last(self):
        return self.rows[-1]

    def watched(self, row, name):
        match = re.search(r"\| %s=(\d+)" % re.escape(ref(name)), row["rest"])
        return int(match.group(1)) if match else None

    def obj(self, row):
        match = re.search(r"\| obj \d+ (\d)", row["rest"])
        return int(match.group(1)) if match else None

    def ever(self, predicate):
        return any(predicate(row) for row in self.rows)

    def trace(self, text):
        return any(text in line for line in self.lines)


def run(binary, env, keys, timeout, save):
    if len(keys) > 64:  # debug_scene.c MAX_KEYS: later entries would be dropped silently
        raise SystemExit(f"{len(keys)} key entries; the game takes at most 64")
    xvfb = shutil.which("xvfb-run")
    full = dict(os.environ, PKMN_DEBUG_FRAMES="1", SDL_AUDIODRIVER="dummy", PKMN_DEBUG_KEYS=",".join(keys), **env)
    cmd = ([xvfb, "-a", "-s", "-screen 0 1280x800x24"] if xvfb and not os.environ.get("DISPLAY") else []) + \
          [os.path.abspath(binary), "--save", save]
    proc = subprocess.Popen(cmd, env=full, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace",
                            start_new_session=True, cwd=os.path.dirname(save))
    lines, done = [], threading.Event()
    last_key = max(int(k.split(":")[0]) for k in keys) if keys else 0

    def reader():
        for line in proc.stdout:
            lines.append(line.rstrip("\n"))
            match = re.search(r"game frame (\d+):|rival (\d+):", line)
            if match and int(match.group(1) or match.group(2)) > last_key + 240:
                done.set()

    thread = threading.Thread(target=reader, daemon=True)
    thread.start()
    done.wait(timeout)
    try:
        os.killpg(proc.pid, 15)
    except ProcessLookupError:
        pass
    try:
        proc.wait(5)
    except subprocess.TimeoutExpired:
        os.killpg(proc.pid, 9)
    thread.join(5)
    rows = []
    for line in lines:
        match = LINE.search(line)
        if match:
            v = match.groups()
            rows.append(dict(frame=int(v[0]), group=int(v[1]), num=int(v[2]), x=int(v[3]), y=int(v[4]), dir=int(v[5]),
                             battle=int(v[6]), env=int(v[7]), trainer=int(v[8]), dowsing=int(v[9]), hmfly=int(v[10]),
                             balls=int(v[11]), party=int(v[12]), rest=v[13]))
    return Run(rows, lines)


def scenarios():
    route110 = dict(PKMN_DEBUG_SCENE="field", PKMN_DEBUG_HERE="0,25,34,57", PKMN_DEBUG_RIVAL_OBJ="28",
                    PKMN_DEBUG_RIVAL_WATCH=",".join(ref(n) for n in ("VAR_ROUTE110_STATE", "FLAG_HIDE_ROUTE_110_RIVAL")))
    route119 = dict(PKMN_DEBUG_SCENE="field", PKMN_DEBUG_HERE="0,34,25,32",
                    PKMN_DEBUG_RIVAL_WATCH=",".join(ref(n) for n in ("VAR_ROUTE119_STATE", "FLAG_RECEIVED_HM_FLY", "VAR_SCOTT_STATE")))
    walk_north = hold("UP", 300) + presses(420, 30, 40)
    walk_north_short = hold("UP", 300) + presses(420, 10, 40)
    plain, grass = 9, 0  # BATTLE_ENVIRONMENT_PLAIN, BATTLE_ENVIRONMENT_GRASS (include/constants/battle.h)

    def blocked(r):
        problems = []
        # stepping ON the line (y 56) is how the trigger fires; crossing it would reach y 55
        if r.ever(lambda row: row["y"] <= 55) or r.last["y"] != 57:
            problems.append(f"the player crossed the trigger line or was not put back (ends at y {r.last['y']})")
        if not r.trace("checkpoint 2 mode 1"):
            problems.append("no BLOCK decision")
        if not r.trace("Players menu requested"):
            problems.append("the Players menu was not requested")
        if r.last["dowsing"] or r.watched(r.last, "VAR_ROUTE110_STATE"):
            problems.append("a reward without a pass")
        if r.ever(lambda row: row["battle"] and row["trainer"] in RIVAL_TRAINERS):
            problems.append("the NPC rival's battle started")
        return problems

    def route110_reward(r):
        problems = []
        if r.last["dowsing"] != 1:
            problems.append(f"Dowsing Machine {r.last['dowsing']}, expected exactly 1")
        if r.watched(r.last, "VAR_ROUTE110_STATE") != 1 or r.watched(r.last, "FLAG_HIDE_ROUTE_110_RIVAL") != 1:
            problems.append("VAR_ROUTE110_STATE / FLAG_HIDE_ROUTE_110_RIVAL not set")
        if r.ever(lambda row: row["battle"] and row["trainer"] in RIVAL_TRAINERS):
            problems.append("the NPC rival's battle started")
        if not r.ever(lambda row: row["y"] <= 55):
            problems.append("the player could not walk on after the reward")
        return problems

    def route110_again(r):
        # the reward came once: walking back over the line (state 1) does nothing more
        problems = route110_reward(r)
        if r.ever(lambda row: row["dowsing"] > 1):
            problems.append("a second Dowsing Machine")
        return problems

    def catch_up(r):
        problems = no_loop("y", 55)(r)
        if not r.ever(lambda row: row["y"] <= 55):
            problems.append("a world-completed checkpoint stopped the player")
        if r.last["dowsing"] or r.watched(r.last, "VAR_ROUTE110_STATE"):
            problems.append("a reward without the player's own pass")
        if not r.trace("checkpoint 2 mode 4"):
            problems.append("no CATCH_UP decision")
        return problems

    def going_back(r):
        problems = no_loop("y", 57)(r)
        if not r.ever(lambda row: row["y"] >= 57):
            problems.append("the player was stopped going back south")
        if r.trace("Players menu requested"):
            problems.append("going back opened the Players menu")
        return problems

    def no_loop(axis, target):
        def check(r):
            problems = []
            decisions = sum("rival: checkpoint" in line for line in r.lines)
            if decisions > 12:
                problems.append(f"the trigger fired {decisions} times (the player is held on the line)")
            if not r.ever(lambda row: row[axis] == target):
                problems.append(f"the player never reached {axis} {target}")
            return problems
        return check

    def gate_then_offline(r):
        problems = []
        if not r.trace("checkpoint 2 mode 1") or not r.trace("host record gone"):
            problems.append("the gate or the disconnect did not happen")
        after = [row for row in r.rows if 600 <= row["frame"] < 1700]
        if not after or after[-1]["y"] != 57:
            problems.append("the gate scene did not finish (the player was not put back) after the disconnect")
        if not r.ever(lambda row: row["frame"] >= 1700 and (row["y"] <= 56 or row["battle"])):
            problems.append("the player was held after the disconnect")
        if r.ever(lambda row: row["dowsing"] > 1):
            problems.append("a reward twice")
        return problems

    def completed_while_waiting(r):
        problems = []
        if not r.ever(lambda row: row["frame"] >= 1700 and row["y"] <= 55):
            problems.append("the world completion did not open the line")
        if r.last["dowsing"] or r.watched(r.last, "VAR_ROUTE110_STATE"):
            problems.append("a reward without the player's own pass")
        return problems

    def original(r):
        problems = []
        if not r.ever(lambda row: row["battle"] and row["trainer"] in RIVAL_TRAINERS):
            problems.append("the original NPC rival battle did not start offline")
        if r.trace("checkpoint 2 mode"):
            problems.append("offline the multiplayer scene ran")
        return problems

    def hidden(r):
        return [] if r.rows and not r.ever(lambda row: r.obj(row) == 1) else ["the NPC rival object is on the field"]

    def visible(r):
        return [] if r.ever(lambda row: r.obj(row) == 1) else ["offline the NPC rival object is missing"]

    def route119_reward(r):
        problems = []
        if r.last["hmfly"] != 1:
            problems.append(f"HM02 {r.last['hmfly']}, expected exactly 1")
        if r.watched(r.last, "FLAG_RECEIVED_HM_FLY") != 1 or r.watched(r.last, "VAR_ROUTE119_STATE") != 1:
            problems.append("FLAG_RECEIVED_HM_FLY / VAR_ROUTE119_STATE not set")
        if r.watched(r.last, "VAR_SCOTT_STATE") != (r.watched(r.first, "VAR_SCOTT_STATE") or 0) + 1:
            problems.append("Scott's scene did not count (VAR_SCOTT_STATE)")
        if r.ever(lambda row: row["battle"] and row["trainer"] in RIVAL_TRAINERS):
            problems.append("the NPC rival's battle started")
        return problems

    def route119_blocked(r):
        problems = []
        if r.ever(lambda row: row["y"] <= 30) or r.last["y"] != 32:
            problems.append(f"the player crossed the Route 119 line or was not put back (ends at y {r.last['y']})")
        if r.last["hmfly"]:
            problems.append("HM02 without a pass")
        if not r.trace("checkpoint 3 mode 1"):
            problems.append("no BLOCK decision")
        return problems

    lab = ("VAR_BIRCH_LAB_STATE", "FLAG_DEFEATED_RIVAL_ROUTE103", "FLAG_SYS_POKEDEX_GET", "FLAG_RECEIVED_POKEDEX_FROM_BIRCH",
           "VAR_OLDALE_RIVAL_STATE", "FLAG_HIDE_OLDALE_TOWN_RIVAL")
    route103 = dict(PKMN_DEBUG_SCENE="field", PKMN_DEBUG_HERE="0,18,10,6", PKMN_DEBUG_RIVAL_OBJ="2",
                    PKMN_DEBUG_RIVAL_WATCH=",".join(ref(n) for n in lab),
                    PKMN_DEBUG_RIVAL_SET=setup(VAR_BIRCH_LAB_STATE=3, FLAG_SYS_POKEDEX_GET=0, FLAG_HIDE_ROUTE_103_RIVAL=0))

    def route103_reward(r):
        problems = hidden(r)
        last = r.last
        if r.watched(last, "VAR_BIRCH_LAB_STATE") != 4 or r.watched(last, "FLAG_DEFEATED_RIVAL_ROUTE103") != 1:
            problems.append("the lab state for Birch's Pokedex scene (4) was not set")
        if r.watched(last, "VAR_OLDALE_RIVAL_STATE") != 1 or r.watched(last, "FLAG_HIDE_OLDALE_TOWN_RIVAL") != 0:
            problems.append("Oldale's rival state was not set")
        if r.watched(last, "FLAG_SYS_POKEDEX_GET") or r.watched(last, "FLAG_RECEIVED_POKEDEX_FROM_BIRCH") or last["balls"]:
            problems.append("the Pokedex or Poke Balls were granted early")
        return problems

    def route103_waiting(r):
        problems = hidden(r)
        if r.watched(r.last, "VAR_BIRCH_LAB_STATE") != 3:
            problems.append("the lab state changed without a pass")
        return problems

    lily = ("FLAG_MET_RIVAL_LILYCOVE", "FLAG_HIDE_LILYCOVE_CITY_RIVAL", "FLAG_HIDE_LITTLEROOT_TOWN_MAYS_HOUSE_RIVAL_BEDROOM")
    lilycove = dict(PKMN_DEBUG_SCENE="field", PKMN_DEBUG_HERE="0,5,27,10", PKMN_DEBUG_RIVAL_OBJ="17",
                    PKMN_DEBUG_RIVAL_WATCH=",".join(ref(n) for n in lily),
                    PKMN_DEBUG_RIVAL_SET=setup(FLAG_HIDE_LILYCOVE_CITY_RIVAL=0, FLAG_MET_RIVAL_LILYCOVE=0,
                                               FLAG_HIDE_LITTLEROOT_TOWN_MAYS_HOUSE_RIVAL_BEDROOM=1))

    def lilycove_reward(r):
        problems = hidden(r)
        if r.watched(r.last, "FLAG_MET_RIVAL_LILYCOVE") != 1 or r.watched(r.last, "FLAG_HIDE_LILYCOVE_CITY_RIVAL") != 1:
            problems.append("FLAG_MET_RIVAL_LILYCOVE / FLAG_HIDE_LILYCOVE_CITY_RIVAL not set")
        if r.watched(r.last, "FLAG_HIDE_LITTLEROOT_TOWN_MAYS_HOUSE_RIVAL_BEDROOM") != 0:
            problems.append("the rival did not go home (bedroom flag)")
        return problems

    # Route 101 (13,3) is tall grass (MB_TALL_GRASS): an ordinary battle there is GRASS.
    battle = dict(PKMN_DEBUG_SCENE="field", PKMN_DEBUG_HERE="0,16,13,3", PKMN_DEBUG_BATTLE="trainer:1",
                  PKMN_DEBUG_RIVAL_WATCH=ref("VAR_ROUTE101_STATE"))

    def environment(expected):
        def check(r):
            envs = {row["env"] for row in r.rows if row["battle"]}
            if not envs:
                return ["no battle started"]
            return [] if envs == {expected} else [f"battle environment {sorted(envs)}, expected {expected}"]
        return check

    intro_watch = ("VAR_BIRCH_LAB_STATE", "VAR_LITTLEROOT_INTRO_STATE", "VAR_LITTLEROOT_TOWN_STATE", "FLAG_SYS_POKEMON_GET",
                   "FLAG_SYS_POKEDEX_GET", "FLAG_RECEIVED_RUNNING_SHOES", "FLAG_SYS_B_DASH", "FLAG_ADVENTURE_STARTED")

    def intro(gender):
        def check(r):
            problems = []
            last = r.last
            spawn = (5, 9) if gender == 0 else (14, 9)
            if (last["group"], last["num"]) != (0, 9) or (last["x"], last["y"]) != spawn:
                problems.append(f"ended at map {last['group']}.{last['num']} {last['x']},{last['y']}, expected Littleroot 0.9 {spawn}")
            mon = re.search(r"\| mon (\d+) lv (\d+) met (\d+)", last["rest"])
            if last["party"] != 1 or not mon or int(mon.group(2)) != 5 or int(mon.group(3)) != 16:
                problems.append(f"party {last['party']} {mon.groups() if mon else None}: expected one level 5 starter met on Route 101 (16)")
            if r.watched(last, "VAR_BIRCH_LAB_STATE") != 3 or r.watched(last, "VAR_LITTLEROOT_INTRO_STATE") != 7 \
                    or r.watched(last, "FLAG_SYS_POKEMON_GET") != 1:
                problems.append("the post-introduction state is not Birch's 'go to Route 103' (lab state 3)")
            for name in ("FLAG_SYS_POKEDEX_GET", "FLAG_RECEIVED_RUNNING_SHOES", "FLAG_SYS_B_DASH", "FLAG_ADVENTURE_STARTED"):
                if r.watched(last, name):
                    problems.append(f"{name} granted by the introduction")
            if last["balls"]:
                problems.append("Poke Balls granted by the introduction")
            # NewGameInitData points the location at the truck before the introduction takes over (party 0, no field):
            # the truck, Route 101 or the first battle would show as a row with the starter on those maps, or a battle.
            if r.ever(lambda row: ((row["group"], row["num"]) in ((25, 40), (0, 16)) and row["party"]) or row["battle"]):
                problems.append("the truck, Route 101 or the first battle happened")
            if not r.trace("intro: saved"):
                problems.append("the introduction did not save")
            return problems
        return check

    def newgame(gender):
        return dict(PKMN_DEBUG_SCENE="newgame", PKMN_DEBUG_HERE=f"0,9,5,9,{gender}",
                    PKMN_DEBUG_RIVAL_WATCH=",".join(ref(n) for n in intro_watch))

    starter_keys = presses(500, 8, 60) + presses(1100, 56, 45)

    return [
        ("route110-gate", "the armed Route 110 line stops a northbound player, no NPC battle",
         dict(route110, PKMN_DEBUG_RIVAL="1,0,0,0,0"), walk_north, blocked),
        ("route110-reward", "a pass gives the Dowsing Machine once on the line",
         dict(route110, PKMN_DEBUG_RIVAL="1,0,4,0,0"), walk_north + hold("UP", 1700, 20), route110_reward),
        ("route110-reward-again", "walking the line again after the reward gives nothing more",
         dict(route110, PKMN_DEBUG_RIVAL="1,4,4,0,0"), walk_north + hold("UP", 1700, 40) + hold("DOWN", 1900, 20) + hold("UP", 2100, 40),
         route110_again),
        ("route110-catch-up", "completed by the world, not by this player: no barrier, no reward",
         dict(route110, PKMN_DEBUG_RIVAL="1,4,0,0,0"), walk_north, catch_up),
        ("route110-going-back", "going back south over the armed line is never stopped",
         dict(route110, PKMN_DEBUG_HERE="0,25,34,55", PKMN_DEBUG_RIVAL="1,0,0,0,0"),
         hold("DOWN", 300, 60) + presses(420, 20, 40), going_back),
        ("route110-along-line", "walking along the armed line (east) stops for a moment and goes on",
         dict(route110, PKMN_DEBUG_HERE="0,25,33,56", PKMN_DEBUG_RIVAL="1,0,0,0,0"),
         hold("RIGHT", 300, 80) + presses(420, 10, 40), no_loop("x", 35)),
        ("rustboro-cross-north", "Rustboro's optional line in multiplayer: crossing north is never held",
         dict(PKMN_DEBUG_SCENE="field", PKMN_DEBUG_HERE="0,3,15,55", PKMN_DEBUG_RIVAL="1,0,0,0,0",
              PKMN_DEBUG_RIVAL_WATCH=ref("VAR_RUSTBORO_CITY_STATE"), PKMN_DEBUG_RIVAL_SET=setup(VAR_RUSTBORO_CITY_STATE=7)),
         hold("UP", 300, 80) + presses(460, 10, 40), no_loop("y", 51)),
        ("rustboro-cross-east", "... and walking east along it is never held",
         dict(PKMN_DEBUG_SCENE="field", PKMN_DEBUG_HERE="0,3,11,53", PKMN_DEBUG_RIVAL="1,0,0,0,0",
              PKMN_DEBUG_RIVAL_WATCH=ref("VAR_RUSTBORO_CITY_STATE"), PKMN_DEBUG_RIVAL_SET=setup(VAR_RUSTBORO_CITY_STATE=7)),
         hold("RIGHT", 300, 200) + presses(560, 10, 40), no_loop("x", 20)),
        ("route110-hidden", "multiplayer: the NPC rival object on Route 110 stays away",
         dict(route110, PKMN_DEBUG_RIVAL="1,0,0,0,0"), presses(300, 2), hidden),
        ("route110-offline", "offline: the original scene and NPC battle are untouched",
         dict(route110), walk_north, original),
        ("route110-offline-visible", "offline: the NPC rival object is there",
         dict(route110), presses(300, 2), visible),
        ("route119-gate", "the armed Route 119 line stops a northbound player",
         dict(route119, PKMN_DEBUG_RIVAL="1,0,0,0,0"), walk_north, route119_blocked),
        ("route119-reward", "a pass gives HM02 Fly once, FLAG_RECEIVED_HM_FLY, and Scott's scene",
         dict(route119, PKMN_DEBUG_RIVAL="1,8,8,0,0"), walk_north + presses(1700, 24, 40), route119_reward),
        ("route103-reward", "a pass on Route 103: Birch's lab state 4, no Pokedex or Poke Balls yet, no NPC rival",
         dict(route103, PKMN_DEBUG_RIVAL="1,1,1,0,0"), presses(300, 20, 40), route103_reward),
        ("route103-waiting", "Route 103 before a pass: no NPC rival, nothing changes",
         dict(route103, PKMN_DEBUG_RIVAL="1,0,0,0,0"), presses(300, 10, 40), route103_waiting),
        ("route103-offline", "offline: the Route 103 NPC rival is there",
         dict(route103), presses(300, 2), visible),
        ("lilycove-reward", "a pass in Lilycove: the rival goes home, no NPC rival on the field",
         dict(lilycove, PKMN_DEBUG_RIVAL="1,16,16,0,0"), presses(300, 20, 40), lilycove_reward),
        ("battle-rival-environment", "a rival preparation makes the battle PLAIN on tall grass",
         dict(battle, PKMN_DEBUG_RIVAL_BATTLE=hex(0x52560000 | (3 << 8) | plain)), presses(600, 10, 60), environment(plain)),
        ("battle-ordinary-environment", "the same battle without it draws GRASS (ordinary battles unchanged)",
         dict(battle), presses(600, 10, 60), environment(grass)),
        ("route110-reward-disconnect", "the host goes away during the reward scene: the reward still comes once, nobody is held",
         dict(route110, PKMN_DEBUG_RIVAL="1,0,4,0,0", PKMN_DEBUG_RIVAL_THEN="430:off"), walk_north + hold("UP", 1700, 20),
         route110_reward),
        ("route110-gate-disconnect", "the host goes away while the gate stops the player: the scene ends, then the game is offline",
         dict(route110, PKMN_DEBUG_RIVAL="1,0,0,0,0", PKMN_DEBUG_RIVAL_THEN="430:off"), walk_north_short + hold("UP", 1700, 20)
         + presses(1760, 30, 40), gate_then_offline),
        ("route110-pass-while-waiting", "a pass recorded while the player waits at the gate: the next step on the line rewards",
         dict(route110, PKMN_DEBUG_RIVAL="1,0,0,0,0", PKMN_DEBUG_RIVAL_THEN="900:1,0,4,0,0"), walk_north_short
         + hold("UP", 1700, 20) + presses(1760, 30, 40) + hold("UP", 3100, 20), route110_reward),
        ("route110-completed-while-waiting", "the world completes it while the player waits: the player may pass, without a reward",
         dict(route110, PKMN_DEBUG_RIVAL="1,0,0,0,0", PKMN_DEBUG_RIVAL_THEN="900:1,4,0,0,0"), walk_north_short
         + hold("UP", 1700, 20) + presses(1760, 10, 40), completed_while_waiting),
        ("intro-male", "New Game: starter screen, Littleroot (5,9), lab state 3, nothing earned early, one save",
         newgame(0), starter_keys, intro(0)),
        ("intro-female", "the same for a female player: Littleroot (14,9)",
         newgame(1), starter_keys, intro(1)),
    ]


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--binary", default=os.path.join(ROOT, "pokeemerald-mp"))
    parser.add_argument("--only", default="")
    parser.add_argument("--timeout", type=float, default=90)
    parser.add_argument("--keep-logs", default="")
    args = parser.parse_args()
    only = set(filter(None, args.only.split(",")))
    failures = 0
    for name, what, env, keys, check in scenarios():
        if only and name not in only:
            continue
        with tempfile.TemporaryDirectory() as folder:
            save = os.path.join(folder, "check.sav")
            result = run(args.binary, env, keys, args.timeout, save)
            if args.keep_logs:
                os.makedirs(args.keep_logs, exist_ok=True)
                with open(os.path.join(args.keep_logs, name + ".log"), "w") as log:
                    log.write("\n".join(result.lines))
            if args.keep_logs and os.path.exists(save):
                shutil.copyfile(save, os.path.join(args.keep_logs, name + ".sav"))
            problems = ["no state lines (the game did not reach the field)"] if not result.rows else check(result)
            if name.startswith("intro") and not (os.path.exists(save) and os.path.getsize(save) == 128 * 1024):
                problems.append("no 128 KiB save file was written")
        failures += bool(problems)
        print(f"{'FAIL' if problems else 'ok  '} {name}: {what}")
        for problem in problems:
            print(f"     {problem}")
    print(f"rival_check: {failures} failed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
