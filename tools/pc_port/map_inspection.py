#!/usr/bin/env python3
"""Every map of the 3D (diorama) game pictured and checked, headless: the owner's whole-world 3D inspection
(2026-10-26). TEST TOOL.

The maps come from the game's data (data/maps, tools/qa/inventory.py: whether a player can reach each one). For each
map it pictures the middle of the map and the tile in front of each warp (its doors, stairs and exits, up to --warps
of them), each from the views of --views:
  default   the Adventure camera as a new player has it
  wide      the Adventure camera zoomed out and tilted down as far as it goes (the render-distance case)
  diorama   the Diorama camera zoomed out (the whole place from above)
and checks each picture (tools/pc_port/visual_audit.py's statuses, and more):
  3D / 2D-FALLBACK / BLANK / CRASH / NOSHOT   as visual_audit.py
  HOLE      a twentieth or more of the lower half is the frame's sky colour (no ground drawn there)
  DARK      outdoors, a tenth or more of the picture near black (geometry missing over the void)
Each picture also records what its frame cost to draw (the profiler's last report: fps, CPU ms, draw calls, vertices).
The pictures go to OUT/shots/<MAP>/<position>-<view>.png, one contact sheet per map to OUT/sheets/<MAP>.png, and
every result to OUT/results.json (per picture) and OUT/manifest.json (per map: its positions, views, statuses and the
worst one). Software OpenGL under xvfb: a picture says what the renderer draws there, not how it looks on a graphics
card; "3D" means drawn and not flagged, not verified by eye.

    tools/pc_port/map_inspection.py --binary ./pokeemerald-mp-diorama-bfxtest --out build/map-inspection \\
        [--maps REGEX] [--views default,wide,diorama] [--warps 3] [--jobs 2] [--reachable-only]

FireRed/LeafGreen maps in the data are left out (not in the Emerald game), as --reachable-only leaves out the maps a
player never enters (listed in the manifest with the reason).
"""
import argparse
import concurrent.futures
import glob
import importlib.util
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
VIEWS = {
    "default": {"cameraMode": "adventure", "adventureZoom": 0, "adventureTilt": 0},
    "wide": {"cameraMode": "adventure", "adventureZoom": -6, "adventureTilt": -4},
    "diorama": {"cameraMode": "diorama", "dioramaZoom": -6, "dioramaTilt": 0},
}
OUTDOOR = {"MAP_TYPE_TOWN", "MAP_TYPE_CITY", "MAP_TYPE_ROUTE", "MAP_TYPE_OCEAN_ROUTE"}
WORST = ["CRASH", "NOSHOT", "BLANK", "2D-FALLBACK", "HOLE", "DARK", "NO-TRACE", "3D"]


def load_inventory():
    spec = importlib.util.spec_from_file_location("qa_inventory", os.path.join(ROOT, "tools/qa/inventory.py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.inventory(ROOT)


def map_ids():
    """MAP_* -> (group, num), from include/constants/map_groups.h."""
    out = {}
    for line in open(os.path.join(ROOT, "include/constants/map_groups.h")):
        m = re.match(r"\s*(MAP_[A-Z0-9_]+)\s*=\s*\((\d+) \| \((\d+) << 8\)\)", line)
        if m:
            out[m.group(1)] = (int(m.group(3)), int(m.group(2)))
    return out


def map_data():
    """MAP_* -> its map.json."""
    out = {}
    for path in glob.glob(os.path.join(ROOT, "data/maps/*/map.json")):
        data = json.load(open(path))
        out[data["id"]] = data
    return out


def layout_size(layouts, layout_id):
    for layout in layouts:
        if layout.get("id") == layout_id:
            return layout.get("width", 0), layout.get("height", 0)
    return 0, 0


def positions(data, size, warps):
    """The middle (-1,-1: the debug scene's own middle) and the tile in front of each of the first `warps` warps."""
    out = [("middle", -1, -1)]
    width, height = size
    seen = set()
    for i, warp in enumerate(data.get("warp_events", [])):
        if len(out) > warps:
            break
        x, y = warp["x"], warp["y"]
        y2 = y + 1 if height == 0 or y + 1 < height else y - 1
        if (x, y2) in seen:
            continue
        seen.add((x, y2))
        out.append((f"warp{i}", x, y2))
    return out


def analyse(path, outdoor):
    im = Image.open(path).convert("RGB")
    small = im.resize((96, 54))
    colours = small.getcolors(96 * 54) or []
    if len(colours) < 6:
        return "BLANK", f"{len(colours)} colours"
    w, h = small.size
    px = small.load()
    top = {}
    for x in range(w):
        for y in range(2):
            top[px[x, y]] = top.get(px[x, y], 0) + 1
    sky = max(top, key=top.get)
    lower = [px[x, y] for x in range(w) for y in range(h // 2, h)]
    sky_share = sum(1 for c in lower if max(abs(c[i] - sky[i]) for i in range(3)) <= 6) / len(lower)
    if outdoor and sky_share >= 0.05 and top[sky] >= w:  # the top rows are mostly one colour: a sky
        return "HOLE", f"{sky_share:.0%} of the lower half is the sky colour"
    dark = sum(1 for x in range(w) for y in range(h) if sum(px[x, y]) < 30) / (w * h)
    if outdoor and dark >= 0.10:
        return "DARK", f"{dark:.0%} near black"
    return "3D", ""


def run_one(binary, name, group, num, pos, view, out_dir, frames, timeout, outdoor):
    label, x, y = pos
    tmp = tempfile.mkdtemp(prefix="inspect-")
    try:
        cfg = open(os.path.join(ROOT, "tools/pc_port/visual_audit.cfg")).read()
        for key, value in VIEWS[view].items():
            if re.search(rf"^{key}=", cfg, re.M):
                cfg = re.sub(rf"^{key}=.*$", f"{key}={value}", cfg, flags=re.M)
            else:
                cfg += f"{key}={value}\n"
        open(os.path.join(tmp, "pokeemerald.cfg"), "w").write(cfg)
        env = dict(os.environ, PKMN_DEBUG_SCENE="field", PKMN_DEBUG_HERE=f"{group},{num},{x},{y}",
                   PKMN_DEBUG_HOUR="12", PKMN_DEBUG_GLSHOTS=str(frames), PKMN_DEBUG_GLSHOTS_GAME="1",
                   PKMN_DIORAMA_LOG="1", PKMN_DEBUG_TRACE="1", PKMN_DIORAMA_PROFILE="1", SDL_AUDIODRIVER="dummy")
        cmd = ["xvfb-run", "-a", "-s", "-screen 0 1280x800x24", "timeout", "-k", "3", str(timeout),
               os.path.abspath(binary), "--config", "pokeemerald.cfg"]
        p = subprocess.run(cmd, cwd=tmp, env=env, capture_output=True, text=True, errors="replace")
        log = p.stdout + p.stderr
        shots = sorted(glob.glob(os.path.join(tmp, "screenshots", "*.png")))
        result = {"map": name, "group": group, "num": num, "position": label, "x": x, "y": y, "view": view,
                  "status": "3D", "detail": "", "picture": None}
        # what drawing it cost (the profiler's last report: software GL, comparable between maps, not a GPU's)
        perf = re.findall(r"profile: ([\d.]+) fps; cpu ms frame ([\d.]+).*?per frame (\d+) draws, (\d+)k vertices", log)
        if perf:
            fps, cpu, draws, kverts = perf[-1]
            result["perf"] = {"fps": float(fps), "cpu_ms": float(cpu), "draws": int(draws), "kvertices": int(kverts)}
        if os.path.exists(os.path.join(tmp, "crash.log")) or p.returncode != 0:
            result.update(status="CRASH", detail=f"exit {p.returncode}")
        elif not shots:
            result.update(status="NOSHOT", detail="no picture (timeout?)")
        else:
            dest = os.path.join(out_dir, "shots", name, f"{label}-{view}.png")
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            shutil.copy(shots[-1], dest)
            result["picture"] = os.path.relpath(dest, out_dir)
            frames_seen = re.findall(r"Diorama: frames (\d+) of which 3D (\d+) \(last (\d)\)", log)
            refusals = sorted(set(re.findall(r"Diorama: object local \d+ gfx (\d+) refused \(([^)]*)\)", log)))
            if frames_seen and frames_seen[-1][2] != "1":
                result.update(status="2D-FALLBACK",
                              detail="; ".join(f"gfx {g} {r}" for g, r in refusals) or "terrain not ready or unsupported")
            else:
                status, detail = analyse(dest, outdoor)
                if status == "3D" and not frames_seen:
                    status, detail = "NO-TRACE", "no diorama trace"
                result.update(status=status, detail=detail)
        return result
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def sheet(out_dir, name, results):
    pics = [r for r in results if r["picture"]]
    if not pics:
        return
    cols = len({r["view"] for r in pics})
    rows = sorted({r["position"] for r in pics}, key=lambda p: (p != "middle", p))
    views = sorted({r["view"] for r in pics}, key=list(VIEWS).index)
    tw, th = 320, 180
    im = Image.new("RGB", (tw * cols, th * len(rows)), (40, 40, 40))
    for r in pics:
        tile = Image.open(os.path.join(out_dir, r["picture"])).convert("RGB").resize((tw, th))
        im.paste(tile, (views.index(r["view"]) * tw, rows.index(r["position"]) * th))
    os.makedirs(os.path.join(out_dir, "sheets"), exist_ok=True)
    im.save(os.path.join(out_dir, "sheets", f"{name}.png"))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--binary", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--maps", help="only maps whose MAP_ name matches this regular expression")
    ap.add_argument("--views", default="default,wide,diorama")
    ap.add_argument("--cave-views", default="default,wide", help="the views for caves and underwater maps")
    ap.add_argument("--indoor-views", default="default", help="the views for rooms")
    ap.add_argument("--warps", type=int, default=3, help="warps pictured per outdoor map (besides its middle)")
    ap.add_argument("--indoor-warps", type=int, default=0, help="warps pictured per cave or room")
    ap.add_argument("--jobs", type=int, default=2)
    ap.add_argument("--frames", type=int, default=300)
    ap.add_argument("--timeout", type=int, default=150)
    ap.add_argument("--reachable-only", action="store_true", help="leave out maps a player never enters")
    args = ap.parse_args()

    ids, maps = map_ids(), map_data()
    layouts = json.load(open(os.path.join(ROOT, "data/layouts/layouts.json")))["layouts"]
    inv = load_inventory()
    reach = {m["id"]: m for m in inv["maps"]}
    work, skipped = [], []
    for name, (group, num) in sorted(ids.items(), key=lambda kv: kv[1]):
        if args.maps and not re.search(args.maps, name):
            continue
        data = maps.get(name)
        entry = reach.get(name, {})
        if data is None:
            skipped.append({"map": name, "why": "no map.json"})
            continue
        if entry.get("frlg"):
            skipped.append({"map": name, "why": "FireRed/LeafGreen map: not in the Emerald game"})
            continue
        if args.reachable_only and not entry.get("reachable", True):
            skipped.append({"map": name, "why": entry.get("entry", "not reachable")})
            continue
        outdoor = data.get("map_type") in OUTDOOR
        cave = data.get("map_type") in ("MAP_TYPE_UNDERGROUND", "MAP_TYPE_UNDERWATER")
        views = (args.views if outdoor else args.cave_views if cave else args.indoor_views).split(",")
        size = layout_size(layouts, data.get("layout"))
        for pos in positions(data, size, args.warps if outdoor else args.indoor_warps):
            for view in views:
                work.append((name, group, num, pos, view, outdoor))
    os.makedirs(args.out, exist_ok=True)
    print(f"{len(work)} pictures of {len({w[0] for w in work})} maps; {len(skipped)} maps left out", flush=True)
    results = []
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        futures = [pool.submit(run_one, args.binary, n, g, m, p, v, args.out, args.frames, args.timeout, o)
                   for n, g, m, p, v, o in work]
        for f in concurrent.futures.as_completed(futures):
            r = f.result()
            results.append(r)
            if r["status"] != "3D":
                print(f"{r['status']:12} {r['map']:46} {r['position']:8} {r['view']:8} {r['detail']}", flush=True)
    results.sort(key=lambda r: (r["group"], r["num"], r["position"], r["view"]))
    manifest = {}
    for r in results:
        m = manifest.setdefault(r["map"], {"map": r["map"], "group": r["group"], "num": r["num"], "pictures": 0,
                                           "statuses": {}, "worst": "3D"})
        m["pictures"] += 1
        m["statuses"][r["status"]] = m["statuses"].get(r["status"], 0) + 1
        if WORST.index(r["status"]) < WORST.index(m["worst"]):
            m["worst"] = r["status"]
    for name in manifest:
        sheet(args.out, name, [r for r in results if r["map"] == name])
    json.dump(results, open(os.path.join(args.out, "results.json"), "w"), indent=1)
    json.dump({"maps": list(manifest.values()), "left_out": skipped}, open(os.path.join(args.out, "manifest.json"), "w"),
              indent=1)
    worst = {}
    for m in manifest.values():
        worst[m["worst"]] = worst.get(m["worst"], 0) + 1
    print(f"{len(manifest)} maps, {len(results)} pictures; maps by their worst picture: {worst}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
