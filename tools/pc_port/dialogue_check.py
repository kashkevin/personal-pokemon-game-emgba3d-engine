#!/usr/bin/env python3
"""Regression check: dialogue answers every press of A, in 3D and in Classic 2D (Update 28, bug 5).

The bug: the player pressed A at a line of NPC or story dialogue and nothing changed on screen; two or three more presses were
needed. Measured cause: a keyboard tap that begins and ends between two game frames left no trace at the frame boundary (the
window loop only looked at the keys held when the frame was taken), and the 3D view's frames are long enough for that to
happen to a quick tap (a quarter to a half of 15-30 ms taps were lost in the 3D build on the test machine's software display);
the old page also stayed up until the next page's first letter. See docs/update28-dialogue-and-menu.md.

This runs the game headless under xvfb (PKMN_DEBUG_DIALOG starts one of the game's own scripts on Route 101;
PKMN_DEBUG_TAPS presses Z, the key bound to A, as real SDL key events: quick taps, one every 600 ms) and reads the trace
lines the game logs ("dialog F: ..." and "taps: pushed N seen M"). It fails when

  - a tap never reached the game (seen < pushed),
  - the script did not run to its end,
  - the 3D message box kept the old page after the player pressed A at a page break (the trace shows the page it draws).

    tools/pc_port/dialogue_check.py [--diorama ./pokeemerald-mp-diorama] [--classic ./pokeemerald-mp] [--scripts boy,rescue]

Needs xvfb-run. A simulation on a software display, not a real keyboard on a real PC.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import threading
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
LINE = re.compile(r"dialog (\d+): script \S+ mode (\d+) status (\d+) \| printer (\d+) state (-?\d+) delay (-?\d+) waiting (\d+) \| "
                  r"native printed (\d+) active (\d+) \| keys held ([0-9a-f]+) new ([0-9a-f]+) \| page \"(.*)\"")


def run(binary, script, taps, timeout):
    xvfb = shutil.which("xvfb-run")
    env = dict(os.environ, PKMN_DEBUG_SCENE="field", PKMN_DEBUG_HERE="0,16,7,15", PKMN_DEBUG_DIALOG=script, PKMN_DEBUG_TAPS=taps,
               PKMN_DEBUG_FRAMES="1", SDL_AUDIODRIVER="dummy")
    cmd = ([xvfb, "-a", "-s", "-screen 0 1280x800x24"] if xvfb and not os.environ.get("DISPLAY") else []) + [os.path.abspath(binary)]
    proc = subprocess.Popen(cmd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace",
                            cwd=os.path.dirname(os.path.abspath(binary)) if False else None, start_new_session=True)
    lines = []
    done = threading.Event()

    def reader():
        for line in proc.stdout:
            lines.append(line.rstrip("\n"))
            if "taps: pushed" in line:
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
    return lines


def analyse(lines, three_d):
    problems = []
    taps = [l for l in lines if "taps: pushed" in l]
    if not taps:
        return ["the run ended before the taps finished (no 'taps:' line)"], {}
    m = re.search(r"pushed (\d+) seen (\d+)", taps[-1])
    pushed, seen = int(m.group(1)), int(m.group(2))
    if seen < pushed:
        problems.append(f"{pushed - seen} of {pushed} taps never reached the game")
    rows = [LINE.search(l) for l in lines]
    rows = [r for r in rows if r]
    started = any("START script" in l for l in lines)
    if not started:
        problems.append("the script did not start")
    # the script ended: status 2 (shutdown) after it started
    ended = [r for r in rows if r.group(3) == "2"]
    if started and not ended:
        problems.append("the script never ran to its end")
    stale = 0
    breaks = 0
    for prev, cur in zip(rows, rows[1:]):
        # the printer left a wait (the player pressed A at a page break): the 3D box must not keep the page it showed
        if prev.group(7) == "1" and cur.group(7) == "0" and cur.group(4) == "1":
            breaks += 1
            if three_d and cur.group(12) == prev.group(12) and cur.group(12) != "":
                stale += 1
    if stale:
        problems.append(f"{stale} of {breaks} page breaks left the old page up after A")
    return problems, {"pushed": pushed, "seen": seen, "page_breaks": breaks, "script_ended": bool(ended)}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--diorama", default=os.path.join(ROOT, "pokeemerald-mp-diorama"))
    ap.add_argument("--classic", default=os.path.join(ROOT, "pokeemerald-mp"))
    ap.add_argument("--scripts", default="boy,rescue")
    ap.add_argument("--taps", default="12000:600:25:60", help="startMs:periodMs:durationMs:count")
    ap.add_argument("--timeout", type=int, default=240)
    args = ap.parse_args()
    failed = False
    for label, binary, three_d in (("3D", args.diorama, True), ("Classic 2D", args.classic, False)):
        if not os.path.exists(binary):
            print(f"dialogue_check: {binary} not found (make -f Makefile_pc NATIVE_LINUX=1 [DIORAMA=1])")
            return 2
        for script in args.scripts.split(","):
            lines = run(binary, script, args.taps, args.timeout)
            problems, info = analyse(lines, three_d)
            if problems:
                failed = True
                print(f"dialogue_check: FAILED {label} {script}: " + "; ".join(problems))
                print("\n".join(lines[-6:]))
            else:
                print(f"dialogue_check: ok {label} {script}: {info['seen']}/{info['pushed']} taps seen, {info['page_breaks']} page breaks, script ended")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
