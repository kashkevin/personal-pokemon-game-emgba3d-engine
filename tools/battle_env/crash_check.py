#!/usr/bin/env python3
"""The crash report on this machine (MS-71): the battle test build crashes on purpose and must leave a full crash.log.

PKMN_TEST_CRASH="<frames>:<kind>" (src/diorama/scene/battle_env_test.c) makes the game crash that many frames in:
- segv: a write through NULL (an access violation on Windows): crash.log with the fault, the stack and breadcrumbs.
- abort: abort() (SIGABRT): crash.log ("abort()").
- fatal: fatalf(): crash.log with the message, and the process ends with exit code 70 instead of hanging.

Each case checks crash.log names the engine commit, the game's callbacks, a stack and the last events (and on Windows
the module's base address and link time stamp, which symbolising a release game needs). Run on the GitHub Windows runner
by windows-package.yml's crash-check job, and on Linux by hand.

    crash_check.py --exe pokeemerald-mp-bfxtest(.exe) --out DIR
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import time

CONFIG = ("borderBackground=0\nbackgroundOrder=2\nfullscreen=0\nwindowScale=2\nintegerScale=0\nvsync=0\nborder=1\n"
          "volume=0\ncryVolume=0\n")
CASES = (("segv", 600), ("abort", 600), ("fatal", 600))


def run_case(exe, out, kind, frames, timeout):
    rundir = os.path.join(out, kind)
    shutil.rmtree(rundir, ignore_errors=True)
    os.makedirs(rundir)
    with open(os.path.join(rundir, "pokeemerald.cfg"), "w") as cfg:
        cfg.write(CONFIG)
    env = dict(os.environ, SDL_AUDIODRIVER="dummy", PKMN_TEST_CRASH=f"{frames}:{kind}")
    started = time.monotonic()
    with open(os.path.join(rundir, "game.log"), "w") as output:
        process = subprocess.Popen([exe, "--save", "new.sav"], cwd=rundir, env=env, stdout=output,
                                   stderr=subprocess.STDOUT)
        try:
            code = process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
            code = None
    seconds = round(time.monotonic() - started, 1)
    path = os.path.join(rundir, "crash.log")
    text = open(path, errors="replace").read() if os.path.exists(path) else ""
    log = open(os.path.join(rundir, "game.log"), errors="replace").read()
    problems = []
    if code is None:
        problems.append(f"the game was still running after {timeout} s (a hang instead of a report)")
    elif code == 0:
        problems.append("the game exited normally")
    if f"PKMN_TEST_CRASH {kind} now" not in log:
        problems.append("the game never reached the planned crash")
    if not text:
        problems.append("no crash.log")
    else:
        wanted = ["pokeemerald-mp crash report", "engine ", "gMain.callback2 = ", "frames:",
                  "last events (oldest first"]
        if os.name == "nt":
            wanted.append("module base ")
        if kind == "fatal":
            wanted.append("PKMN_TEST_CRASH fatal")
        if kind == "abort":
            wanted.append("abort")
        problems += [f"crash.log lacks {w!r}" for w in wanted if w.lower() not in text.lower()]
        if "engine unknown" in text:
            problems.append("crash.log does not name the engine commit")
    if kind == "fatal" and code is not None and code != 70:
        problems.append(f"exit code {code}, not 70")
    return dict(kind=kind, frames=frames, exit_code=code, seconds=seconds, crash_log_bytes=len(text),
                passed=not problems, problems=problems, crash_log_head=text.splitlines()[:6])


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--timeout", type=float, default=120)
    args = parser.parse_args()
    exe, out = os.path.abspath(args.exe), os.path.abspath(args.out)
    os.makedirs(out, exist_ok=True)
    results = [run_case(exe, out, kind, frames, args.timeout) for kind, frames in CASES]
    report = dict(exe=exe, platform=sys.platform, cases=results, passed=all(r["passed"] for r in results))
    with open(os.path.join(out, "crash-check.json"), "w") as f:
        json.dump(report, f, indent=2)
    for r in results:
        print(f"{r['kind']:6} {'PASSED' if r['passed'] else 'FAILED'}  exit {r['exit_code']}  {r['seconds']} s  "
              f"crash.log {r['crash_log_bytes']} bytes  {'; '.join(r['problems'])}")
    print("crash check", "PASSED" if report["passed"] else "FAILED")
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
