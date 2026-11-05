#!/usr/bin/env python3
"""The moves past the Battle FX package's 621 (numbers 622 up to the Z-Moves): can a player meet each one in this game,
and what does an HD version of each need? (VR-5)

    python3 tools/battlefx/moves_past_package.py [--root .] [--core battlefx/bfx_core.js] [--json OUT.json] [--md OUT.md]

Reachable: a source this game has for the move, read from its data (read only):
  egg        an egg move of a species line a player can hatch: on PC a Pokemon hatched from an Egg switches in its line's
             egg moves (src/move_relearner.c, the end of the file; include/config/summary_screen.h), so an egg move is
             learnt without a parent that knows it
  level-up   the level-up learnset (P_LVL_UP_LEARNSETS: this game's src/data/pokemon/level_up_learnsets/gen_6.h, every
             species' first or Gen 6 list) of a species a player can own
  tm / tutor the PC game's TM and tutor lists (tm_learnsets_pc.h, teachable_learnsets_pc.h)
  opponent   a trainer's own moveset (src/data/trainers.party, tower_legends.party, battle_partners.party)
Species a player can own: the wild pool (src/data/mp_wild_pool.h: every Gen 1-6 family), the random Legendary /
Mythical encounter (isRestrictedLegendary, isSubLegendary, isMythical, National Dex 1-721), every species a script,
trade or starter list names, their pre-evolutions (breeding), and what they evolve into under this game's conditions
(Hoenn's region; the items no source gives, listed below from the item audit; a move the line can learn).
Metronome calls moves up to B_METRONOME_MOVES (Gen 6: 1-621), the debug menus are off, Z-Moves and Max Moves are off.

Needs: the moves converted by tools/battlefx/convert_more.py; each sprite template's callback and each task, checked
against the core (node runs bfx_core.js and lists its callbacks, tasks and kits; without node, a text search).
"""
import argparse
import collections
import glob
import json
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import convert_more as cm  # noqa: E402

# items no script, map, mart, shop or wild Pokemon gives (docs/item-audit-20261027-items.md of the app: "not obtainable")
UNOBTAINABLE_ITEMS = {"ITEM_SWEET_APPLE", "ITEM_TART_APPLE", "ITEM_CRACKED_POT", "ITEM_CHIPPED_POT", "ITEM_GALARICA_CUFF",
                      "ITEM_GALARICA_WREATH", "ITEM_AUSPICIOUS_ARMOR", "ITEM_MALICIOUS_ARMOR", "ITEM_PEAT_BLOCK",
                      "ITEM_SYRUPY_APPLE", "ITEM_UNREMARKABLE_TEACUP", "ITEM_MASTERPIECE_TEACUP", "ITEM_METAL_ALLOY",
                      "ITEM_SCROLL_OF_DARKNESS", "ITEM_SCROLL_OF_WATERS", "ITEM_STRAWBERRY_SWEET"}
REGIONAL = {"isAlolanForm", "isGalarianForm", "isHisuianForm", "isPaldeanForm"}
NOT_OWNED = {"isGigantamax", "isTeraForm", "isTotem", "isUltraBurst"}
LEGEND = {"isRestrictedLegendary", "isSubLegendary", "isMythical"}
CORE_JS = r"""
const fs = require('fs'), vm = require('vm');
vm.runInThisContext(fs.readFileSync(process.argv[2], "utf8"), { filename: "bfx_core.js" });
const RT = globalThis.BFX_RT, HD = globalThis.BFX_HD;
process.stdout.write(JSON.stringify({ cb: Object.keys(RT.CB), tk: Object.keys(RT.TK), logic: Object.keys(RT.LOGIC_TASKS),
  tag: Object.keys(HD.TAG_KITS), tpl: Object.keys(HD.TPL_KITS), moves: Object.keys(HD.MOVES) }));
"""


def read(path):
    return open(path, encoding="utf-8").read()


def species_data(root):
    text = read(os.path.join(root, "include/constants/pokedex.h"))
    enum = text[text.find("enum NationalDexOrder"):]
    dex, n = {}, 0
    for m in re.finditer(r"\b(NATIONAL_DEX_[A-Z0-9_]+)\b", enum):
        name = m.group(1)
        if name in dex or name == "NATIONAL_DEX_NONE":
            continue
        if name.startswith("NATIONAL_DEX_COUNT"):
            break
        n += 1
        dex[name] = n
    species = {}
    for path in sorted(glob.glob(os.path.join(root, "src/data/pokemon/species_info/gen_*_families.h"))):
        parts = re.split(r"\n    \[(SPECIES_[A-Z0-9_]+)\]\s*=\s*\n?\s*\{", read(path))
        for i in range(1, len(parts), 2):
            body = parts[i + 1]
            end = body.find("\n    },")
            body = body[:end if end >= 0 else len(body)]
            d = {"evos": []}
            m = re.search(r"\.natDexNum\s*=\s*(NATIONAL_DEX_\w+)", body)
            d["dex"] = dex.get(m.group(1)) if m else None
            for k in ("levelUpLearnset", "eggMoveLearnset"):
                m = re.search(r"\." + k + r"\s*=\s*(s\w+)", body)
                d[k] = m.group(1) if m else None
            d["flags"] = set(re.findall(r"\.(is\w+)\s*=\s*TRUE", body))
            m = re.search(r"\.eggGroups\s*=\s*MON_EGG_GROUPS\(([^)]*)\)", body)
            d["egg"] = m.group(1) if m else ""
            k = body.find(".evolutions")
            if k >= 0:
                seg = body[k:]
                for e in re.finditer(r"\{(EVO_[A-Z_]+),\s*([^,]+),\s*(SPECIES_[A-Z0-9_]+)((?:[^{}]|\{[^{}]*\})*)\}", seg):
                    if e.group(1) != "EVO_NONE":
                        d["evos"].append((e.group(1), e.group(2).strip(), e.group(3), e.group(4)))
            species[parts[i]] = d
    return species


def blocks(path, rx):
    return {m.group(1): re.findall(r"\bMOVE_[A-Z0-9_]+\b", m.group(2)) for m in re.finditer(rx, read(path), re.S)}


def reachability(root, ids, names):
    species = species_data(root)
    pre = {}
    for sp, d in species.items():
        for e in d["evos"]:
            pre.setdefault(e[2], sp)
    lvl = blocks(os.path.join(root, "src/data/pokemon/level_up_learnsets/gen_6.h"),
                 r"static const struct LevelUpMove (s\w+)\[\]\s*=\s*\{(.*?)\};")
    egg = blocks(os.path.join(root, "src/data/pokemon/egg_moves.h"), r"static const u16 (s\w+)\[\]\s*=\s*\{(.*?)\};")
    owned = set(x for x in re.findall(r"\{(SPECIES_[A-Z0-9_]+),", read(os.path.join(root, "src/data/mp_wild_pool.h")))
                if x in species)
    for sp, d in species.items():
        if d["dex"] and d["dex"] <= 721 and d["flags"] & LEGEND and not d["flags"] & (REGIONAL | NOT_OWNED):
            owned.add(sp)
    for path in (glob.glob(os.path.join(root, "data/**/*.inc"), recursive=True)
                 + [os.path.join(root, "src/data/trade.h"), os.path.join(root, "src/starter_choose.c")]):
        try:
            owned.update(x for x in re.findall(r"\b(SPECIES_[A-Z0-9_]+)\b", read(path)) if x in species)
        except (OSError, UnicodeDecodeError):
            pass

    def line(sp):
        out = [sp]
        while sp in pre:
            sp = pre[sp]
            out.append(sp)
        return out

    for sp in list(owned):
        owned.update(line(sp))

    def moves_of(sp):
        mv = collections.defaultdict(set)
        for s in line(sp):
            for x in lvl.get(species[s]["levelUpLearnset"] or "", []):
                mv[x].add("level-up: " + s)
        if "EGG_GROUP_NO_EGGS_DISCOVERED" not in species[line(sp)[-1]]["egg"]:
            for s in line(sp):
                for x in egg.get(species[s]["eggMoveLearnset"] or "", []):
                    mv[x].add("egg: " + s)
        return mv

    changed = True
    while changed:
        changed = False
        for sp in list(owned):
            known = moves_of(sp)
            for (meth, param, tgt, cond) in species[sp]["evos"]:
                if tgt in owned or tgt not in species:
                    continue
                ok = not (param in UNOBTAINABLE_ITEMS) and not any(i in UNOBTAINABLE_ITEMS for i in re.findall(r"ITEM_\w+", cond))
                reg = re.search(r"IF_REGION,\s*(REGION_[A-Z]+)", cond)
                if reg and reg.group(1) != "REGION_HOENN":
                    ok = False
                mm = re.search(r"IF_(?:KNOWS_MOVE|USED_MOVE_X_TIMES),\s*(MOVE_[A-Z0-9_]+)", cond)
                if mm and mm.group(1) not in known:
                    ok = False
                if ok:
                    owned.add(tgt)
                    changed = True
    reach = collections.defaultdict(set)
    want = set(ids)
    for sp in owned:
        for x, src in moves_of(sp).items():
            if x in want:
                reach[x].update(src)
    for f in ("tm_learnsets_pc.h", "teachable_learnsets_pc.h"):
        text = cm.strip_comments(read(os.path.join(root, "src/data/pokemon", f)))   # their headers list moves in comments
        for x in want & set(re.findall(r"\bMOVE_[A-Z0-9_]+\b", text)):
            reach[x].add(("tm" if f.startswith("tm") else "tutor") + " list")
    for f in ("trainers.party", "tower_legends.party", "battle_partners.party"):
        text = read(os.path.join(root, "src/data", f))
        for x in want:
            if names.get(x) and re.search(r"^-\s*" + re.escape(names[x]) + r"\s*$", text, re.M):
                reach[x].add("opponent: " + f)
    return reach, owned


def core_inventory(core_path):
    # a script file, not `node -e`: there `require` is global and the bundle takes itself for a CommonJS module
    with tempfile.NamedTemporaryFile("w", suffix=".js", delete=False) as f:
        f.write(CORE_JS)
    try:
        out = subprocess.run(["node", f.name, core_path], capture_output=True, text=True, check=True).stdout
        d = json.loads(out)
        return {k: set(v) for k, v in d.items()}, "node"
    except (OSError, subprocess.CalledProcessError, ValueError):
        text = read(core_path)
        return {"cb": set(re.findall(r"\bCB\.(\w+)\s*=", text)), "tk": set(re.findall(r"\bTK\.(\w+)\s*=", text)),
                "logic": set(re.findall(r"^\s*(AnimTask_\w+): \(\) =>", text, re.M)), "tag": set(), "tpl": set(),
                "moves": set()}, "text search"
    finally:
        os.unlink(f.name)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--root", default=ROOT)
    ap.add_argument("--core", default=os.path.join(ROOT, "battlefx", "bfx_core.js"))
    ap.add_argument("--base", default=os.path.join(ROOT, "battlefx", "bfx_moves.json"))
    ap.add_argument("--json")
    ap.add_argument("--md")
    o = ap.parse_args()
    base = json.load(open(o.base))
    scripts = cm.Scripts(o.root)
    info = cm.MoveInfo(o.root, scripts)
    nums = cm.move_numbers(scripts)
    zfirst = nums.get("MOVE_BREAKNECK_BLITZ", 10 ** 6)
    ids = sorted((m for m, n in nums.items() if 622 <= n < zfirst and m in info.info), key=lambda m: nums[m])
    ids = [m for i, m in enumerate(ids) if nums[m] not in {nums[x] for x in ids[:i]}]
    names = {m: info.entry(m, nums[m])["name"] for m in ids}
    reach, owned = reachability(o.root, ids, names)
    inv, how = core_inventory(o.core)
    templates = cm.Templates(o.root, scripts, base)
    sim = cm.Sim(scripts, templates)
    base_tpl = {t["n"] for t in json.load(open(o.base))["templates"]}
    rows = []
    for mid in ids:
        e = info.entry(mid, nums[mid])
        r = {"id": mid, "num": nums[mid], "name": e["name"], "type": e["type"],
             "reachable": sorted(reach.get(mid, [])), "needs": collections.OrderedDict()}
        try:
            dflt, var = cm.convert_move(sim, e["script"])
        except SystemExit as ex:
            r["needs"]["script command"] = [str(ex).replace("convert_more.py: ", "").replace(" is not converted", "")]
            rows.append(r)
            continue
        T = templates.base["templates"]
        for row in list(dflt.rows) + [x for v in var for x in v["ev"]]:
            if row[1] == 0:
                t = T[row[2]]
                if t["n"] not in base_tpl:
                    r["needs"].setdefault("template", set()).add(t["n"])
                if t["cb"] not in inv["cb"] and t["cb"] not in ("SpriteCallbackDummy", "SpriteCallbackDummy2"):
                    r["needs"].setdefault("callback", set()).add(t["cb"])
                if inv["tag"] and t["tile"] != "0" and t["tile"] not in inv["tag"] and t["n"] not in inv["tpl"]:
                    r["needs"].setdefault("HD kit (tag)", set()).add(t["tile"])
            elif row[1] == 1 and row[2] not in inv["tk"] and row[2] not in inv["logic"]:
                r["needs"].setdefault("task", set()).add(row[2])
            elif row[1] == 2 and row[2] == "createsoundtask" and row[5]["task"] not in inv["tk"]:
                r["needs"].setdefault("sound task", set()).add(row[5]["task"])
        r["needs"] = {k: sorted(v) for k, v in r["needs"].items()}
        r["frames"] = cm.end_time(dflt.rows)
        r["variants"] = [v["label"] for v in var]
        r["hd_config"] = mid in inv["moves"]
        rows.append(r)
    out = {"moves": rows, "core_inventory_by": how, "owned_species": len(owned)}
    if o.json:
        json.dump(out, open(o.json, "w"), indent=1)
    blocking = ("script command", "callback", "task", "sound task")
    caps = collections.defaultdict(lambda: [set(), set()])
    for r in rows:
        for k in blocking + ("template", "HD kit (tag)"):
            for x in r["needs"].get(k, []):
                caps[(k, x)][0].add(r["id"])
                if r["reachable"]:
                    caps[(k, x)][1].add(r["id"])
    lines = ["| need | what | moves | of them reachable |", "|---|---|---|---|"]
    for (k, x), (allm, rm) in sorted(caps.items(), key=lambda kv: (-len(kv[1][1]), -len(kv[1][0]), kv[0])):
        lines.append("| %s | `%s` | %d | %d%s |" % (k, x, len(allm), len(rm), (" (" + ", ".join(sorted(rm)) + ")") if rm else ""))
    nreach = sum(1 for r in rows if r["reachable"])
    ready = [r for r in rows if not any(k in r["needs"] for k in blocking)]
    summary = ["moves past the package: %d; reachable: %d; converted with nothing else missing: %d (reachable %d); core inventory by %s"
               % (len(rows), nreach, len(ready), sum(1 for r in ready if r["reachable"]), how)]
    md = "\n".join(summary + [""] + lines) + "\n"
    if o.md:
        open(o.md, "w").write(md)
    print(md)
    return 0


if __name__ == "__main__":
    sys.exit(main())
