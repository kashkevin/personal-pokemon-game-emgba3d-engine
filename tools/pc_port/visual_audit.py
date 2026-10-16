#!/usr/bin/env python3
"""Visual audit sweep of the overworld in the 3D (diorama) executable, headless (docs/visual-audio-audit.md in the app repo).

For each map it starts the debug field scene (PKMN_DEBUG_SCENE=field, the player in the middle of the map), lets the 3D view
settle, takes the window screenshot and reads the diorama's own log, then classifies the map:
  3D        the frame was drawn by the 3D renderer
  2D-FALLBACK  the 3D view refused the frame (the reason is the log's "refused" line: an object it cannot draw, ...)
  BLANK     the picture is (almost) one colour
  CRASH     the game wrote crash.log / exited with a signal
Software OpenGL under xvfb: it says what the renderer *decides*, not how it looks on a real monitor.

    tools/pc_port/visual_audit.py --binary ./pokeemerald-mp-diorama --out DIR [--maps 0:9,0:16 | --group 0] [--jobs 4]
"""
import argparse
import concurrent.futures
import glob
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def all_maps():
    """(group, num, name) in the order of include/constants/map_groups.h."""
    out = []
    for line in open(os.path.join(ROOT, "include/constants/map_groups.h")):
        m = re.match(r"\s*(MAP_[A-Z0-9_]+)\s*=\s*\((\d+) \| \((\d+) << 8\)\)", line)
        if m:
            out.append((int(m.group(3)), int(m.group(2)), m.group(1)))  # (num | group << 8)
    return out


def run_one(binary, group, num, name, out_dir, frames, timeout):
    tmp = tempfile.mkdtemp(prefix="audit-")
    try:
        shutil.copy(os.path.join(ROOT, "tools/pc_port/visual_audit.cfg"), os.path.join(tmp, "pokeemerald.cfg"))
        env = dict(os.environ, PKMN_DEBUG_SCENE="field", PKMN_DEBUG_HERE=f"{group},{num},-1,-1", PKMN_DEBUG_GLSHOTS=str(frames),
                   PKMN_DIORAMA_LOG="1", PKMN_DEBUG_TRACE="1", SDL_AUDIODRIVER="dummy")
        cmd = ["xvfb-run", "-a", "-s", "-screen 0 1280x800x24", "timeout", "-k", "3", str(timeout), os.path.abspath(binary)]
        p = subprocess.run(cmd, cwd=tmp, env=env, capture_output=True, text=True)
        log = p.stdout + p.stderr
        shots = sorted(glob.glob(os.path.join(tmp, "screenshots", "emerald-*.png")))
        status, detail = "3D", ""
        if os.path.exists(os.path.join(tmp, "crash.log")) or p.returncode not in (0,):
            status, detail = "CRASH", f"exit {p.returncode}"
        elif not shots:
            status, detail = "NOSHOT", "no screenshot (timeout?)"
        else:
            im = Image.open(shots[-1]).convert("RGB")
            colours = im.resize((64, 36)).getcolors(5000) or []
            if len(colours) < 6:
                status, detail = "BLANK", f"{len(colours)} colours"
            shutil.copy(shots[-1], os.path.join(out_dir, f"{name}.png"))
        ready = re.findall(r"Diorama: mode (\d+) supported (\d) flat (\d) ready (\d)", log)
        frames = re.findall(r"Diorama: frames (\d+) of which 3D (\d+) \(last (\d)\)", log)
        refusals = sorted(set(re.findall(r"Diorama: object local \d+ gfx (\d+) refused \(([^)]*)\)", log)))
        if status == "3D":
            if not frames:
                status, detail = "NO-TRACE", "no diorama trace"
            elif frames[-1][2] != "1":
                status = "2D-FALLBACK"
                detail = "; ".join(f"gfx {g} {r}" for g, r in refusals) or ("map unsupported" if ready and ready[-1][1] == "0" else "terrain not ready")
        return {"map": name, "group": group, "num": num, "status": status, "detail": detail}
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--binary", default=os.path.join(ROOT, "pokeemerald-mp-diorama"))
    ap.add_argument("--out", required=True)
    ap.add_argument("--maps", help="group:num,group:num")
    ap.add_argument("--group", type=int, help="every map of this group")
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--frames", type=int, default=450)
    ap.add_argument("--timeout", type=int, default=120)
    args = ap.parse_args()
    maps = all_maps()
    if args.maps:
        want = {tuple(int(x) for x in m.split(":")) for m in args.maps.split(",")}
        maps = [m for m in maps if (m[0], m[1]) in want]
    if args.group is not None:
        maps = [m for m in maps if m[0] == args.group]
    os.makedirs(args.out, exist_ok=True)
    results = []
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        futures = [pool.submit(run_one, args.binary, g, n, name, args.out, args.frames, args.timeout) for g, n, name in maps]
        for f in concurrent.futures.as_completed(futures):
            r = f.result()
            results.append(r)
            print(f"{r['status']:12} {r['map']:48} {r['detail']}", flush=True)
    results.sort(key=lambda r: (r["group"], r["num"]))
    with open(os.path.join(args.out, "results.json"), "w") as fh:
        json.dump(results, fh, indent=1)
    bad = [r for r in results if r["status"] not in ("3D",)]
    print(f"{len(results)} maps: {len(results) - len(bad)} drawn in 3D, {len(bad)} not")
    return 0


if __name__ == "__main__":
    sys.exit(main())
