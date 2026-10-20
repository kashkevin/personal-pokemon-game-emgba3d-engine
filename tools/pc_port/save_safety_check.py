#!/usr/bin/env python3
"""Save-safety and start/stop check of the native PC game (Update 31 final integration).

Every scenario works on COPIES of a save in a temporary folder; no player save is ever touched.

  load:      a save written by the streamlined introduction is loaded through the real title screen and main menu
             (CONTINUE) by each given binary (e.g. the Update 30 build, the integration's Classic 2D and 3D builds):
             each must reach the overworld, and loading must not change a byte of the save.
  restarts:  the same save, started and killed (SIGTERM or SIGKILL) 12 times at spread moments: the save stays byte-identical
             and valid after every one.
  config:    a malformed pokeemerald.cfg (random bytes, a truncated file, absurd values) beside the game: it still reaches the
             overworld with the save intact.
  faults:    the introduction's one save, with a fault injected into the save write by an LD_PRELOAD shim:
               kill-mid-write  the process dies after writing half of the temporary file,
               kill-at-rename  the process dies after the temporary file is complete, before it replaces the save,
               short-write     the write reports a short count (a full disk);
             the save file on disk must still be the previous save, byte for byte, and the game must load it afterwards.
             Without a fault the new save replaces it and passes the app's own save validator.

    tools/pc_port/save_safety_check.py --binary ./pokeemerald-mp [--load ./old-build ./pokeemerald-mp-diorama ...]
        [--app /path/to/app/src] [--only load,restarts,config,faults]

Needs xvfb-run, gcc with -m32 (for the shim) and, for the validator, the app's src (emerald_multiplayer.flash).
A simulation on a software display: no real PC, disk or power cut.
"""
import argparse
import hashlib
import os
import random
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import threading
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SHIM = r'''
#define _GNU_SOURCE
#include <dlfcn.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static FILE *tmp;
static int ends(const char *s, const char *t) { size_t a = strlen(s), b = strlen(t); return a >= b && !strcmp(s + a - b, t); }
FILE *fopen(const char *path, const char *mode) {
    FILE *(*real)(const char *, const char *) = dlsym(RTLD_NEXT, "fopen");
    FILE *f = real(path, mode);
    if (path && ends(path, ".sav.tmp")) tmp = f;
    return f;
}
FILE *fopen64(const char *path, const char *mode) { return fopen(path, mode); }
size_t fwrite(const void *p, size_t size, size_t n, FILE *f) {
    size_t (*real)(const void *, size_t, size_t, FILE *) = dlsym(RTLD_NEXT, "fwrite");
    const char *fault = getenv("SAVE_FAULT");
    if (f && f == tmp && fault) {
        if (!strcmp(fault, "kill-mid-write")) { real(p, size, n / 2, f); fflush(f); kill(getpid(), SIGKILL); }
        if (!strcmp(fault, "short-write")) return real(p, size, n / 2, f);
    }
    return real(p, size, n, f);
}
int rename(const char *from, const char *to) {
    int (*real)(const char *, const char *) = dlsym(RTLD_NEXT, "rename");
    const char *fault = getenv("SAVE_FAULT");
    if (fault && !strcmp(fault, "kill-at-rename") && to && ends(to, ".sav")) kill(getpid(), SIGKILL);
    return real(from, to);
}
'''


def sha(path):
    return hashlib.sha256(open(path, "rb").read()).hexdigest() if os.path.exists(path) else None


def overworld_address(binary):
    out = subprocess.run(["nm", binary], capture_output=True, text=True).stdout
    match = re.search(r"^([0-9a-f]+) [Tt] CB2_Overworld$", out, re.M)
    return int(match.group(1), 16) if match else None


def run(binary, save, env, keys, seconds, kill=None, cwd=None):
    """Runs the game; returns its log lines. ``kill`` = (seconds, signal) ends it early."""
    full = dict(os.environ, SDL_AUDIODRIVER="dummy", PKMN_DEBUG_FRAMES="1", PKMN_DEBUG_KEYS=",".join(keys), **env)
    cmd = ["xvfb-run", "-a", "-s", "-screen 0 1280x800x24", os.path.abspath(binary), "--save", save]
    proc = subprocess.Popen(cmd, env=full, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace",
                            start_new_session=True, cwd=cwd or os.path.dirname(save))
    lines = []
    thread = threading.Thread(target=lambda: lines.extend(line.rstrip("\n") for line in proc.stdout), daemon=True)
    thread.start()
    deadline = kill[0] if kill else seconds
    try:
        proc.wait(deadline)
    except subprocess.TimeoutExpired:
        pass
    for sig in ((kill[1],) if kill else ()) + (signal.SIGTERM, signal.SIGKILL):
        if proc.poll() is not None:
            break
        try:
            os.killpg(proc.pid, sig)
        except ProcessLookupError:
            break
        try:
            proc.wait(5)
        except subprocess.TimeoutExpired:
            continue
    thread.join(5)
    return lines


def reached_overworld(lines, address):
    return any(re.search(r"callback2 0x%x\b" % address, line) for line in lines) if address else False


CONTINUE = [f"{f}:A" for f in range(400, 1101, 100)]


class Validator:
    def __init__(self, app_src):
        self.ok = False
        if app_src and os.path.isdir(app_src):
            sys.path.insert(0, app_src)
            try:
                from emerald_multiplayer.flash import LAYOUT_ID, inspect_flash
                self.inspect, self.layout, self.ok = inspect_flash, LAYOUT_ID, True
            except Exception:
                self.ok = False
            try:  # the app's first-time character creation accepts a New Game save only if this succeeds
                from emerald_multiplayer.draft_runtime import extract_source_inventory
                self.inventory = extract_source_inventory
            except Exception:
                self.inventory = None

    def __call__(self, path):
        """None when valid, else the reason; the party count is returned in ``self.party``."""
        if not self.ok:
            return "no validator (pass --app)"
        try:
            snapshot = self.inspect(open(path, "rb").read(), save_profile=self.layout)
            self.party = snapshot.metadata.party_count
            if self.inventory is not None:
                self.inventory(snapshot)
            # Update 32: a new character has no Pokemon until it claims its starter in Birch's lab; the app's
            # character creation and enrollment accept a party of 0 to 6.
            if not 0 <= self.party <= 6:
                return f"party count {self.party}: the app's character creation would refuse it"
            return None
        except Exception as error:  # noqa: BLE001 - any refusal is the finding
            return f"refused by the save validator: {error}"


def make_intro_save(binary, folder):
    save = os.path.join(folder, "intro.sav")
    keys = [f"{f}:A" for f in range(500, 981, 60)] + [f"{f}:A" for f in range(1100, 3575, 45)]
    lines = run(binary, save, dict(PKMN_DEBUG_SCENE="newgame", PKMN_DEBUG_HERE="0,9,5,9,0"), keys, 120)
    if not any("intro: saved (status 1)" in line for line in lines) or sha(save) is None:
        raise SystemExit("could not create the introduction's save (no 'intro: saved' line)")
    time.sleep(0.5)
    return save


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--binary", default=os.path.join(ROOT, "pokeemerald-mp"))
    ap.add_argument("--load", nargs="*", default=[], help="more binaries that must load the save (older build, 3D build)")
    ap.add_argument("--app", default="", help="the app's src folder, for the save validator")
    ap.add_argument("--only", default="")
    args = ap.parse_args()
    only = set(filter(None, args.only.split(",")))
    validate = Validator(args.app)
    failures = 0

    def report(name, problems):
        nonlocal failures
        failures += bool(problems)
        print(f"{'FAIL' if problems else 'ok  '} {name}")
        for problem in problems:
            print(f"     {problem}")
        sys.stdout.flush()

    with tempfile.TemporaryDirectory() as root:
        base = make_intro_save(args.binary, root)
        base_sha = sha(base)
        problem = validate(base)
        report(f"the introduction's save is valid and is what Update 30's character creation accepts (party {getattr(validate, 'party', '?')})",
               [problem] if problem else [])

        if not only or "load" in only:
            for binary in [args.binary] + args.load:
                folder = tempfile.mkdtemp(dir=root)
                save = shutil.copy(base, os.path.join(folder, "copy.sav"))
                lines = run(binary, save, {}, CONTINUE, 30)
                problems = []
                if not reached_overworld(lines, overworld_address(binary)):
                    problems.append("did not reach the overworld through CONTINUE")
                if sha(save) != base_sha:
                    problems.append("loading changed the save")
                report(f"load: {os.path.basename(binary)} continues the save and leaves it unchanged", problems)

        if not only or "restarts" in only:
            rng, problems = random.Random(31), []
            folder = tempfile.mkdtemp(dir=root)
            save = shutil.copy(base, os.path.join(folder, "copy.sav"))
            for i in range(12):
                when = rng.uniform(0.3, 9.0)
                sig = signal.SIGKILL if i % 2 else signal.SIGTERM
                run(args.binary, save, {}, CONTINUE, 30, kill=(when, sig))
                if sha(save) != base_sha:
                    problems.append(f"run {i + 1} ({sig.name} after {when:.1f} s) changed the save")
                elif validate(save) and validate.ok:
                    problems.append(f"run {i + 1}: {validate(save)}")
            lines = run(args.binary, save, {}, CONTINUE, 30)
            if not reached_overworld(lines, overworld_address(args.binary)):
                problems.append("the save does not load after the restarts")
            report("restarts: 12 starts killed at random moments (SIGTERM/SIGKILL) leave the save intact and loadable", problems)

        if not only or "config" in only:
            for label, content in (("random bytes", os.urandom(4096)), ("truncated", b"display.width=12"),
                                   ("absurd values", b"display.width=-99999999\ndisplay.height=999999999\nvolume.music=abc\n"
                                                     b"key.a=\xff\xfe\nrenderer=\x00\x00\ngraphics.mode=99\n" * 50)):
                folder = tempfile.mkdtemp(dir=root)
                save = shutil.copy(base, os.path.join(folder, "copy.sav"))
                with open(os.path.join(folder, "pokeemerald.cfg"), "wb") as cfg:
                    cfg.write(content)
                lines = run(args.binary, save, {}, CONTINUE, 30)
                problems = []
                if not reached_overworld(lines, overworld_address(args.binary)):
                    problems.append("did not reach the overworld")
                if sha(save) != base_sha:
                    problems.append("the save changed")
                report(f"config: a malformed pokeemerald.cfg ({label}) does not stop the game or touch the save", problems)

        if not only or "faults" in only:
            shim = os.path.join(root, "save_fault.so")
            build = subprocess.run(["gcc", "-m32", "-shared", "-fPIC", "-o", shim, "-x", "c", "-", "-ldl"], input=SHIM,
                                   capture_output=True, text=True)
            if build.returncode != 0:
                report("faults: the fault shim builds", [build.stderr[-400:]])
            else:
                # The "previous save" is a different valid save: the introduction of a female player.
                for fault in ("kill-mid-write", "kill-at-rename", "short-write", None):
                    folder = tempfile.mkdtemp(dir=root)
                    save = shutil.copy(base, os.path.join(folder, "copy.sav"))
                    env = dict(PKMN_DEBUG_SCENE="newgame", PKMN_DEBUG_HERE="0,9,5,9,1", LD_PRELOAD=shim)
                    if fault:
                        env["SAVE_FAULT"] = fault
                    keys = [f"{f}:A" for f in range(500, 981, 60)] + [f"{f}:A" for f in range(1100, 3575, 45)]
                    lines = run(args.binary, save, env, keys, 120)
                    saved = any("intro: saved" in line for line in lines)
                    problems = []
                    if not saved:
                        problems.append("the introduction never reached its save")
                    if fault:
                        if sha(save) != base_sha:
                            problems.append("the previous save was damaged or replaced")
                        reload = run(args.binary, save, {}, CONTINUE, 30)
                        if not reached_overworld(reload, overworld_address(args.binary)):
                            problems.append("the previous save does not load afterwards")
                    else:
                        if sha(save) == base_sha:
                            problems.append("without a fault the new save did not replace the old one")
                        elif validate.ok and validate(save):
                            problems.append(validate(save))
                    report(f"faults: {fault or 'no fault'}: " + ("the previous save survives byte for byte and loads"
                           if fault else "the new save replaces the old one and is valid"), problems)

    print(f"save_safety_check: {failures} failed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
