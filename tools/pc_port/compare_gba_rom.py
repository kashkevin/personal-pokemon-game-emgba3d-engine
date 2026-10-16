#!/usr/bin/env python3
"""Compare a freshly built GBA ROM with a reference ROM and name the symbols that differ.

    python3 tools/pc_port/compare_gba_rom.py REFERENCE.gba [pokedev.gba pokedev.elf]

The native port keeps PC-only changes behind PORTABLE, so the GBA ROM should be
byte-identical to the emulator baseline. Exit status 0 means identical.
"""
import bisect
import subprocess
import sys

ref_path = sys.argv[1]
rom_path = sys.argv[2] if len(sys.argv) > 2 else 'pokedev.gba'
elf_path = sys.argv[3] if len(sys.argv) > 3 else 'pokedev.elf'
a = open(ref_path, 'rb').read()
b = open(rom_path, 'rb').read()
if a == b:
    print('GBA ROM identical to reference')
    sys.exit(0)
diffs = [i for i in range(min(len(a), len(b))) if a[i] != b[i]]
print('sizes %d vs %d, %d differing bytes' % (len(a), len(b), len(diffs)))
out = subprocess.run(['arm-none-eabi-nm', '-S', '--defined-only', '-n', elf_path],
                     capture_output=True, text=True).stdout
syms = []
for line in out.splitlines():
    p = line.split()
    if len(p) == 4:
        syms.append((int(p[0], 16), int(p[1], 16), p[3]))
syms.sort()
addrs = [s[0] for s in syms]
reported = {}
for off in diffs:
    addr = 0x08000000 + off
    i = bisect.bisect_right(addrs, addr) - 1
    name = syms[i][2] if i >= 0 and addr < syms[i][0] + syms[i][1] else '(no symbol)'
    reported.setdefault(name, []).append(addr)
for name, where in reported.items():
    ctx = b[where[0] - 0x08000000 - 8: where[0] - 0x08000000 + 24]
    print('%-40s %3d byte(s) at %s  context %r' % (name, len(where), hex(where[0]), ctx))
sys.exit(1)
