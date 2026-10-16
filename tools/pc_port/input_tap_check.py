#!/usr/bin/env python3
"""Regression check: quick key taps are not lost between game frames (Update 28, bug 5).

    tools/pc_port/input_tap_check.py [--diorama ./pokeemerald-mp-diorama] [--classic ./pokeemerald-mp] [--durations 15,30]

PKMN_DEBUG_TAPS has a thread push real SDL key events (Z, bound to A) and counts what the game sees at its frame boundary.
Before the fix (key_queue.h) the 3D build lost 20-34 of 60 taps of 15-30 ms on the test machine's software display and the
Classic build lost up to 6 of 60; now none. A simulation: a real PC has other frame times, never fewer taps seen than pushed.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import threading

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def taps(binary, duration, timeout):
    xvfb = shutil.which("xvfb-run")
    env = dict(os.environ, PKMN_DEBUG_SCENE="field", PKMN_DEBUG_HERE="0,16,7,15", PKMN_DEBUG_TAPS=f"12000:250:{duration}:60",
               SDL_AUDIODRIVER="dummy")
    cmd = ([xvfb, "-a", "-s", "-screen 0 1280x800x24"] if xvfb and not os.environ.get("DISPLAY") else []) + [os.path.abspath(binary)]
    proc = subprocess.Popen(cmd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace", start_new_session=True)
    result, done = [], threading.Event()

    def reader():
        for line in proc.stdout:
            m = re.search(r"taps: pushed (\d+) seen (\d+)", line)
            if m:
                result.append((int(m.group(1)), int(m.group(2))))
                done.set()

    threading.Thread(target=reader, daemon=True).start()
    done.wait(timeout)
    try:
        os.killpg(proc.pid, 15)
    except ProcessLookupError:
        pass
    try:
        proc.wait(5)
    except subprocess.TimeoutExpired:
        os.killpg(proc.pid, 9)
    return result[0] if result else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--diorama", default=os.path.join(ROOT, "pokeemerald-mp-diorama"))
    ap.add_argument("--classic", default=os.path.join(ROOT, "pokeemerald-mp"))
    ap.add_argument("--durations", default="15,30")
    ap.add_argument("--timeout", type=int, default=120)
    args = ap.parse_args()
    failed = False
    for label, binary in (("3D", args.diorama), ("Classic 2D", args.classic)):
        if not os.path.exists(binary):
            print(f"input_tap_check: {binary} not found")
            return 2
        for duration in args.durations.split(","):
            got = taps(binary, int(duration), args.timeout)
            if got is None:
                failed = True
                print(f"input_tap_check: FAILED {label} {duration} ms taps: the run ended before the result")
            elif got[1] < got[0]:
                failed = True
                print(f"input_tap_check: FAILED {label} {duration} ms taps: {got[0] - got[1]} of {got[0]} lost")
            else:
                print(f"input_tap_check: ok {label} {duration} ms taps: {got[1]}/{got[0]} seen")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
