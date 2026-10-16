#!/usr/bin/env python3
"""Summarises Battle FX test runs (tools/battlefx/gallery.sh): report.jsonl (galleries), real_report.jsonl
(fought battles) and crashes.jsonl, as a Markdown table on stdout and JSON with --json FILE.

  python3 tools/battlefx/summarize.py RUN_OUT_FOLDER [RUN_OUT_FOLDER ...] [--json summary.json]

Per run: animations played, exact (every compared frame's effect sprites equal), different (first
different frame, the game's and the core's sprite lists there), failed (the core refused or failed:
GBA effects shown), crashed (the game crashed during it: listed by the runner), frames after the core's
end with GBA effect sprites still on screen, effect sprites left on screen after the game's animation ended
(the 24 frames the driver waits before the next), length differences (game frames - core frames), and the
cost: the core's begin and step, the renderer's GPU and CPU time a layer (software rendering in the
cloud runs: not a graphics card's numbers).
"""
import json
import statistics
import sys
from pathlib import Path


def load(path):
    rows = []
    if path.exists():
        for line in path.read_text().splitlines():
            line = line.strip()
            if line:
                rows.append(json.loads(line))
    return rows


def pct(values, q):
    if not values:
        return 0.0
    values = sorted(values)
    k = max(0, min(len(values) - 1, int(round(q * (len(values) - 1)))))
    return values[k]


def summarise(folder):
    folder = Path(folder)
    rows = load(folder / "report.jsonl")
    real = load(folder / "real_report.jsonl")
    crashes = load(folder / "crashes.jsonl")
    problems = (folder / "problems.txt").read_text().splitlines() if (folder / "problems.txt").exists() else []
    out = {"folder": str(folder), "played": len(rows), "real": len(real), "crashed": [c["id"] for c in crashes],
           "problems": problems}
    exact, diff, failed, tails, longer, shorter, stuck, leftover = [], [], [], [], [], [], [], []
    begin, step, worst, gpu, cpu = [], [], [], [], []
    gamefps, presfps, presp99 = [], [], []
    for r in rows:
        p = r.get("parity", {})
        key = r["id"] + ("/t%d" % r["turn"] if r.get("turn") else "")
        if r.get("failed"):
            failed.append(key)
        if r.get("stuck"):
            stuck.append(key)
        if p.get("compared", 0) > 0 and p.get("equal") == p.get("compared"):
            exact.append(key)
        elif p.get("compared", 0) > 0:
            diff.append({"id": key, "compared": p["compared"], "equal": p["equal"], "firstBad": p["firstBad"],
                         "game": p.get("game", "")[:160], "core": p.get("core", "")[:160]})
        t = r.get("tail", {})
        if t.get("withSprites", 0) > 0:
            tails.append({"id": key, "frames": t["withSprites"], "first": t.get("first", "")[:120]})
        a = r.get("afterEnd", {})
        if a.get("frames", 0) > 0 and not r.get("stuck"):
            leftover.append({"id": key, "frames": a["frames"], "first": a.get("first", "")[:120]})
        d = r.get("gameFrames", 0) - (r.get("coreFrames", 0) - r.get("hdTail", 0))
        if d > 1:
            longer.append(d)
        elif d < 0:
            shorter.append(key)
        begin.append(r.get("beginMs", 0.0))
        step.append(r.get("stepMs", 0.0))
        worst.append(r.get("stepMsWorst", 0.0))
        if r.get("gameFps"):
            gamefps.append(r["gameFps"])
        pr = r.get("presents") or {}
        if pr.get("count", 0) > 0:
            presfps.append(pr["fps"])
            presp99.append(pr["p99Ms"])
        rs = r.get("render")
        if rs and rs.get("layers"):
            gpu.append(rs.get("gpuMs", 0.0))
            cpu.append(rs.get("cpuMs", 0.0))
    real_exact = [x["id"] for x in real if x["compared"] > 0 and x["equal"] == x["compared"]]
    real_diff = [x for x in real if x["compared"] > 0 and x["equal"] != x["compared"]]
    out.update({
        "exact": len(exact), "different": diff, "failed": failed, "stuck": stuck, "tailSprites": tails,
        "leftoverSprites": leftover,
        "gameLongerThanCore": {"count": len(longer), "medianFrames": statistics.median(longer) if longer else 0},
        "coreLongerThanGame": shorter,
        "beginMs": {"median": statistics.median(begin) if begin else 0, "p95": pct(begin, 0.95), "max": max(begin or [0])},
        "stepMs": {"median": statistics.median(step) if step else 0, "p95": pct(step, 0.95), "worst": max(worst or [0])},
        "renderGpuMsPerLayer": {"median": statistics.median(gpu) if gpu else 0, "p95": pct(gpu, 0.95)},
        "renderCpuMsPerLayer": {"median": statistics.median(cpu) if cpu else 0, "p95": pct(cpu, 0.95)},
        "realExact": len(real_exact), "realDifferent": real_diff, "realIds": [x["id"] for x in real],
        "gameFps": {"median": statistics.median(gamefps) if gamefps else 0, "lowest": min(gamefps or [0])},
        "presentFps": {"median": statistics.median(presfps) if presfps else 0, "lowest": min(presfps or [0])},
        "presentP99Ms": {"median": statistics.median(presp99) if presp99 else 0, "worst": max(presp99 or [0])},
    })
    return out


def markdown(s):
    lines = ["### %s" % s["folder"], ""]
    for p in s.get("problems", []):
        lines.append("**NOT VALID: %s**" % p)
        lines.append("")
    lines.append("| | |")
    lines.append("| --- | --- |")
    lines.append("| animations played (gallery) | %d |" % s["played"])
    lines.append("| exact: every compared frame equal | %d |" % s["exact"])
    lines.append("| different | %d %s |" % (len(s["different"]), " ".join(d["id"] for d in s["different"])))
    lines.append("| failed (GBA shown) | %d %s |" % (len(s["failed"]), " ".join(s["failed"])))
    lines.append("| crashed the game | %d %s |" % (len(s["crashed"]), " ".join(s["crashed"])))
    lines.append("| the game's own animation did not end here (stuck) | %d %s |" % (len(s["stuck"]), " ".join(s["stuck"])))
    lines.append("| GBA effect sprites after the core's end | %d %s |" % (len(s["tailSprites"]), " ".join(t["id"] for t in s["tailSprites"])))
    lines.append("| effect sprites left on screen after the game's animation ended (settle, %d frames) | %d %s |"
                 % (24, len(s["leftoverSprites"]), " ".join("%s (%d)" % (t["id"], t["frames"]) for t in s["leftoverSprites"])))
    lines.append("| core longer than the game | %d %s |" % (len(s["coreLongerThanGame"]), " ".join(s["coreLongerThanGame"])))
    lines.append("| game longer than the core (its end waits) | %d (median %s frames) |" % (s["gameLongerThanCore"]["count"], s["gameLongerThanCore"]["medianFrames"]))
    lines.append("| core begin, ms (median / p95 / max) | %.1f / %.1f / %.1f |" % (s["beginMs"]["median"], s["beginMs"]["p95"], s["beginMs"]["max"]))
    lines.append("| core step, ms (median / p95 of the averages / worst) | %.2f / %.2f / %.1f |" % (s["stepMs"]["median"], s["stepMs"]["p95"], s["stepMs"]["worst"]))
    lines.append("| renderer GPU ms a layer (median / p95) | %.2f / %.2f |" % (s["renderGpuMsPerLayer"]["median"], s["renderGpuMsPerLayer"]["p95"]))
    lines.append("| renderer CPU ms a layer (median / p95) | %.2f / %.2f |" % (s["renderCpuMsPerLayer"]["median"], s["renderCpuMsPerLayer"]["p95"]))
    lines.append("| game frames a second during an animation (median / lowest) | %.1f / %.1f |" % (s["gameFps"]["median"], s["gameFps"]["lowest"]))
    lines.append("| presents a second during an animation (median / lowest) | %.1f / %.1f |" % (s["presentFps"]["median"], s["presentFps"]["lowest"]))
    lines.append("| 99th percentile present interval, ms (median / worst) | %.1f / %.1f |" % (s["presentP99Ms"]["median"], s["presentP99Ms"]["worst"]))
    if s["real"]:
        lines.append("| fought battle: animations | %d, exact %d: %s |" % (s["real"], s["realExact"], " ".join(s["realIds"])))
        if s["realDifferent"]:
            lines.append("| fought battle: different | %s |" % " ".join(x["id"] for x in s["realDifferent"]))
    lines.append("")
    for d in s["different"]:
        lines.append("- %s: %d of %d frames equal, first different frame %d; game `%s`; core `%s`"
                     % (d["id"], d["equal"], d["compared"], d["firstBad"], d["game"], d["core"]))
    return "\n".join(lines)


def main():
    args = sys.argv[1:]
    json_out = None
    if "--json" in args:
        i = args.index("--json")
        json_out = args[i + 1]
        del args[i:i + 2]
    sums = [summarise(a) for a in args]
    print("\n\n".join(markdown(s) for s in sums))
    if json_out:
        Path(json_out).write_text(json.dumps(sums, indent=1))


if __name__ == "__main__":
    main()
