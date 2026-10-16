#!/usr/bin/env python3
"""Check the `#line N // gba-line` rule for PC-only changes in GBA-compiled files.

PC-only code goes under `#ifdef PORTABLE` (or PLATFORM_SDL2) with `#line` after
the block, so the GBA build sees every original line at its original number
(`__LINE__`, used by asserts and the allocator, must not move). This preprocesses
each given file the way the GBA build does (arm-none-eabi-cpp, the Makefile's
defines, no PORTABLE) at a base revision and in the working tree, and compares
the source file's own lines with their line numbers (whitespace ignored). Code
the GBA build compiles must be the same at the same numbers; a difference is
reported with both versions.

    python3 tools/pc_port/check_gba_lines.py --base <rev> src/clock.c src/load_save.c ...
    python3 tools/pc_port/check_gba_lines.py --base <rev> --changed     # every changed src/*.c

Exit status 0 when every file matches. The GBA ROM's sha256 is the final check;
this one also covers `__LINE__` values that do not reach the ROM today.
"""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

DEFINES = ["-iquote", "include", "-Wno-trigraphs", "-DMODERN=1", "-DTESTING=0", "-DEMERALD", "-std=gnu17",
           "-DMP_LOCAL_RESET_DEVELOPMENT=0", "-DMP_LOCAL_RESET_STATIC=1", "-DMP_LOCAL_RESET_NEW_MAUVILLE_PILOT=0"]
MARKER = re.compile(r'^# (\d+) "([^"]*)"')


def own_lines(source: Path, display: str, real: Path) -> list[tuple[int, str]]:
    """(line number, text without whitespace) of the file's own non-empty lines;
    the base copy's name is read as the real file's (__FILE__)."""
    out = subprocess.run(["arm-none-eabi-cpp", *DEFINES, str(source)], capture_output=True, text=True)
    if out.returncode != 0:
        raise SystemExit(f"cpp failed for {display}:\n{out.stderr[-2000:]}")
    lines, current, number = [], None, 0
    for text in out.stdout.replace(str(source), str(real)).splitlines():
        match = MARKER.match(text)
        if match:
            number, current = int(match.group(1)), match.group(2)
            continue
        if current is not None and Path(current).name == real.name:
            squeezed = "".join(text.split())
            if squeezed:
                lines.append((number, squeezed))
        number += 1
    return lines


def check(base: str, path: str) -> list[str]:
    old_text = subprocess.run(["git", "show", f"{base}:{path}"], capture_output=True, text=True)
    if old_text.returncode != 0:
        return []  # a new file: not part of the GBA build's history
    source = Path(path)
    # The base copy sits beside the file so its own relative includes resolve.
    with tempfile.NamedTemporaryFile("w", suffix=source.suffix, dir=source.parent, prefix=".gbaline-",
                                     delete=False) as handle:
        handle.write(old_text.stdout)
        temp = Path(os.path.relpath(handle.name))
    try:
        old = own_lines(temp, f"{base}:{path}", source)
    finally:
        os.unlink(temp)
    new = own_lines(source, path, source)
    if old == new:
        return []
    problems = []
    for index, (a, b) in enumerate(zip(old, new)):
        if a != b:
            problems.append(f"{path}: line {a[0]} '{a[1][:60]}' became line {b[0]} '{b[1][:60]}'")
            break
    if len(old) != len(new) and not problems:
        problems.append(f"{path}: {len(old)} GBA lines became {len(new)}")
    return problems


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--base", required=True)
    parser.add_argument("--changed", action="store_true", help="every src/*.c changed since the base")
    parser.add_argument("files", nargs="*")
    args = parser.parse_args()
    files = list(args.files)
    if args.changed:
        diff = subprocess.run(["git", "diff", "--name-only", args.base, "--", "src"], capture_output=True, text=True,
                              check=True).stdout.split()
        files += [f for f in diff if f.endswith(".c") and not f.startswith(("src/platform/", "src/diorama/",
                                                                          "src/native_ui/"))]
    problems = []
    for path in sorted(set(files)):
        if Path(path).exists():
            problems += check(args.base, path)
    for problem in problems:
        print("MOVED", problem)
    print(f"{len(set(files))} files, {len(problems)} with moved GBA lines")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
