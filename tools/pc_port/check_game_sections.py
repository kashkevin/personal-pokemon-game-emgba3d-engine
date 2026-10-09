#!/usr/bin/env python3
"""Check the soft-reset memory layout of the native PC build.

Makefile_pc moves the .data/.bss of every game object into dedicated sections
(game_data/game_bss on ELF, .data$game/.bss$game on PE) that
src/platform/game_memory.c snapshots and restores on a soft reset. Objects in
PRESERVED_OBJS (window, audio, cartridge save image and clock, platform tools)
keep ordinary sections and survive a reset.

A game object left with ordinary .data/.bss (or any other writable section)
would keep its values across a soft reset, unlike on the GBA. This check fails if:
  * a non-preserved object has a non-empty writable section other than the game
    sections (for example .data, .bss, or assembler data marked "aw"), or
  * a preserved object has game sections, or
  * in the linked program, a global variable of a game object lies outside the
    game memory range that game_memory.c restores, or a preserved object's global
    lies inside it (checked with nm on both ELF and PE).

Usage: check_game_sections.py --objdump OBJDUMP --nm NM --target PROGRAM
                              --preserved "a.o b.o" --objs-file objs.rsp
"""
import argparse
import re
import subprocess
import sys

GAME_ELF = {"game_data", "game_bss"}
GAME_PE = {".data$game", ".bss$game"}

FILE_RE = re.compile(r"^(\S.*?):\s+file format\s+(\S+)")
# objdump -h section line: "  0 .text  00000123  00000000  00000000  00000034  2**2"
# followed by a flags line: "                  CONTENTS, ALLOC, LOAD, READONLY, DATA"
SECT_RE = re.compile(r"^\s*\d+\s+(\S+)\s+([0-9a-fA-F]+)\s")
# PE objects do not mark read-only data as READONLY; the linker maps these names
# to the read-only .rdata section.
PE_READONLY_PREFIXES = (".rdata", ".rodata", ".eh_frame", ".xdata", ".pdata", ".debug_", ".ctors", ".dtors")


def section_info(objdump, paths):
    """Return {path: {section: (size, writable)}} using one objdump call per chunk."""
    result = {}
    chunk = 400
    for i in range(0, len(paths), chunk):
        part = paths[i:i + chunk]
        out = subprocess.run([objdump, "-h", *part], check=True,
                             capture_output=True, text=True).stdout
        current = None
        pe = False
        pending = None
        for line in out.splitlines():
            m = FILE_RE.match(line)
            if m:
                current = m.group(1)
                pe = m.group(2).startswith("pe-")
                result[current] = {}
                pending = None
                continue
            m = SECT_RE.match(line)
            if m and current is not None:
                pending = (m.group(1), int(m.group(2), 16))
                continue
            if pending is not None and current is not None:
                name, size = pending
                flags = {f.strip() for f in line.split(",")}
                writable = ("ALLOC" in flags and "READONLY" not in flags and "CODE" not in flags
                            and not (pe and name.startswith(PE_READONLY_PREFIXES)))
                result[current][name] = (size, writable)
                pending = None
    return result


NM_LINE = re.compile(r"^([0-9a-fA-F]+)\s+([A-Za-z])\s+(\S+)$")
# Range markers in the linked program: ELF uses the linker's __start_/__stop_
# symbols, PE the markers defined in src/platform/game_memory.c.
MARKERS = {
    "elf": ("__start_game_data", "__stop_game_data", "__start_game_bss", "__stop_game_bss"),
    "pe": ("_gGameDataBegin", "_gGameDataEnd", "_gGameBssBegin", "_gGameBssEnd"),
}


def nm_symbols(nm, paths):
    """Return {path: {name: (addr, type)}} for defined symbols."""
    result = {}
    chunk = 400
    for i in range(0, len(paths), chunk):
        part = paths[i:i + chunk]
        out = subprocess.run([nm, "--defined-only", *part], check=True,
                             capture_output=True, text=True).stdout
        current = part[0] if len(part) == 1 else None
        for line in out.splitlines():
            if line.endswith(":") and not line.startswith(" "):
                current = line[:-1]
                result.setdefault(current, {})
                continue
            m = NM_LINE.match(line.strip())
            if m and current is not None:
                result.setdefault(current, {})[m.group(3)] = (int(m.group(1), 16), m.group(2))
    return result


def check_linked_ranges(nm, target, objs, preserved):
    """Every global variable of a game object must be inside the restored range;
    every global variable of a preserved object must be outside it."""
    errors = []
    linked = nm_symbols(nm, [target]).get(target, {})
    kind = "pe" if target.endswith(".exe") else "elf"
    names = MARKERS[kind]
    if not all(n in linked for n in names):
        return [f"{target}: game memory markers missing ({[n for n in names if n not in linked]})"]
    data_lo, data_hi, bss_lo, bss_hi = (linked[n][0] for n in names)
    if kind == "pe":
        data_lo += 4  # the markers themselves are 4 bytes (see game_memory.c)
        bss_lo += 4

    def inside(addr):
        return data_lo <= addr < data_hi or bss_lo <= addr < bss_hi

    per_obj = nm_symbols(nm, objs)
    checked = 0
    for obj, syms in per_obj.items():
        for name, (_addr, kind_char) in syms.items():
            if kind_char not in "DB" or name not in linked:
                continue  # globals only: local names may repeat across objects
            addr = linked[name][0]
            checked += 1
            if obj in preserved and inside(addr) and name not in names:
                errors.append(f"{obj}: preserved global {name} lies in game memory")
            elif obj not in preserved and not inside(addr):
                errors.append(f"{obj}: game global {name} at {addr:#x} is outside game memory "
                              f"(data {data_lo:#x}-{data_hi:#x}, bss {bss_lo:#x}-{bss_hi:#x})")
    print(f"linked: game data {data_hi - data_lo} bytes, game bss {bss_hi - bss_lo} bytes, "
          f"{checked} global variables checked")
    if checked == 0:
        errors.append("no global variables were checked (nm output not understood)")
    return errors


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--objdump", default="objdump")
    ap.add_argument("--nm", default="nm")
    ap.add_argument("--target", required=True)
    ap.add_argument("--preserved", required=True)
    ap.add_argument("--objs-file", required=True)
    args = ap.parse_args()

    objs = open(args.objs_file).read().split()
    preserved = set(args.preserved.split())
    info = section_info(args.objdump, objs)

    errors = []
    missing = [o for o in objs if o not in info]
    if missing:
        errors.append(f"objdump did not report {len(missing)} objects, e.g. {missing[:3]}")

    game_bytes = 0
    for obj in objs:
        sects = info.get(obj, {})
        game = {s: n for s, (n, _) in sects.items() if s in GAME_ELF | GAME_PE}
        if obj in preserved:
            if game:
                errors.append(f"{obj}: preserved object has game sections {sorted(game)}")
        else:
            other = {s: n for s, (n, w) in sects.items()
                     if w and n > 0 and s not in GAME_ELF | GAME_PE}
            if other:
                errors.append(f"{obj}: writable sections outside game memory {other} "
                              "(would survive a soft reset)")
            game_bytes += sum(game.values())

    errors += check_linked_ranges(args.nm, args.target, objs, preserved)

    for e in errors:
        print("ERROR:", e)
    print(f"{len(objs)} objects, {len(preserved & set(objs))} preserved, "
          f"{game_bytes} bytes of game .data/.bss in objects")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
