#!/usr/bin/env python3
"""Compare GBA and native PC memory layouts of ABI-boundary types.

The GBA build compiles with -mabi=apcs-gnu, where every struct and union is at
least 4-byte aligned and its size is padded to a multiple of 4. The native x86
build uses natural alignment, so the same C definitions can produce different
byte layouts. Save data, companion mailboxes and battle link packets must keep
the GBA layout, so the types they contain are marked ABI_ALIGN (see
include/gba/defines.h).

This tool compiles tools/pc_port/abi_probe.c for both targets with DWARF debug
info, walks every struct/union reachable from the probe's root variables, and
reports any type whose size or member offsets differ.

    python3 tools/pc_port/abi_layout.py           # report; exit 1 on mismatch
    python3 tools/pc_port/abi_layout.py --fix     # insert ABI_ALIGN, then re-check
"""
import argparse
import os
import re
import subprocess
import sys

from elftools.elf.elffile import ELFFile

ENGINE = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PROBE = 'tools/pc_port/abi_probe.c'
OUT_DIR = os.path.join(ENGINE, 'build', 'abi')

GAME_CPPFLAGS = ['-iquote', 'include', '-Wno-trigraphs', '-DMODERN=1', '-DTESTING=0', '-DEMERALD',
                 '-std=gnu17', '-DMP_LOCAL_RESET_DEVELOPMENT=0', '-DMP_LOCAL_RESET_STATIC=1',
                 '-DMP_LOCAL_RESET_NEW_MAUVILLE_PILOT=0']
TARGETS = {
    'gba': {
        'cpp': ['arm-none-eabi-gcc', '-E'] + GAME_CPPFLAGS,
        'cc': ['arm-none-eabi-gcc', '-mthumb', '-mthumb-interwork', '-mabi=apcs-gnu',
               '-mtune=arm7tdmi', '-march=armv4t', '-std=gnu17', '-O0', '-gdwarf-4', '-w'],
    },
    'pc': {
        'cpp': ['gcc', '-m32', '-E'] + GAME_CPPFLAGS + ['-DPORTABLE', '-DNONMATCHING', '-DUBFIX'],
        'cc': ['gcc', '-m32', '-std=gnu17', '-O0', '-gdwarf-4', '-w', '-fno-pie'],
    },
}


def build(target):
    os.makedirs(OUT_DIR, exist_ok=True)
    obj = os.path.join(OUT_DIR, 'abi_probe_%s.o' % target)
    t = TARGETS[target]
    cpp = subprocess.run(t['cpp'] + [PROBE], cwd=ENGINE, check=True, capture_output=True)
    pre = subprocess.run(['tools/preproc/preproc', '-i', '-p', PROBE, 'charmap.txt'], cwd=ENGINE,
                         input=cpp.stdout, check=True, capture_output=True)
    subprocess.run(t['cc'] + ['-x', 'cpp-output', '-c', '-o', obj, '-'], cwd=ENGINE,
                   input=pre.stdout, check=True)
    return obj


def strip_type(die):
    """Follow typedef/const/volatile to the underlying type DIE."""
    while die is not None and die.tag in ('DW_TAG_typedef', 'DW_TAG_const_type',
                                          'DW_TAG_volatile_type', 'DW_TAG_restrict_type'):
        die = die.get_DIE_from_attribute('DW_AT_type') if 'DW_AT_type' in die.attributes else None
    return die


def attr(die, name, default=None):
    a = die.attributes.get(name)
    return a.value if a is not None else default


class Model:
    """Struct/union layouts reachable from the probe roots, keyed by declaration site."""

    def __init__(self, path):
        self.f = open(path, 'rb')
        self.elf = ELFFile(self.f)
        self.dwarf = self.elf.get_dwarf_info()
        self.types = {}   # key -> dict(name, kind, size, members)
        self.roots = {}   # root name -> key
        for cu in self.dwarf.iter_CUs():
            lineprog = self.dwarf.line_program_for_CU(cu)
            files = lineprog.header['file_entry']
            dirs = lineprog.header['include_directory']
            self.files = []
            for fe in files:
                d = dirs[fe.dir_index - 1].decode() if fe.dir_index > 0 else ''
                self.files.append(os.path.normpath(os.path.join(d, fe.name.decode())))
            for die in cu.get_top_DIE().iter_children():
                if die.tag == 'DW_TAG_variable':
                    name = attr(die, 'DW_AT_name', b'').decode()
                    if name.startswith('gAbiProbe_'):
                        t = strip_type(die.get_DIE_from_attribute('DW_AT_type'))
                        self.roots[name[len('gAbiProbe_'):]] = self.visit(t)

    def site(self, die):
        fidx = attr(die, 'DW_AT_decl_file')
        line = attr(die, 'DW_AT_decl_line')
        path = self.files[fidx - 1] if fidx else '?'
        if path.startswith(ENGINE + '/'):
            path = path[len(ENGINE) + 1:]
        return path, line

    def key(self, die):
        path, line = self.site(die)
        name = attr(die, 'DW_AT_name', b'').decode() or '<anon>'
        return '%s %s:%s:%s' % (die.tag[7:], path, line, name)

    def type_desc(self, t):
        t = strip_type(t)
        if t is None:
            return ('void', 0)
        if t.tag in ('DW_TAG_structure_type', 'DW_TAG_union_type'):
            return (self.visit(t), attr(t, 'DW_AT_byte_size', 0))
        if t.tag == 'DW_TAG_array_type':
            elem = self.type_desc(t.get_DIE_from_attribute('DW_AT_type'))
            dims = []
            for sub in t.iter_children():
                if sub.tag == 'DW_TAG_subrange_type':
                    ub = attr(sub, 'DW_AT_upper_bound')
                    cnt = attr(sub, 'DW_AT_count')
                    dims.append(cnt if cnt is not None else (ub + 1 if isinstance(ub, int) else 0))
            return (('array', tuple(dims), elem[0]), elem[1] * eval('*'.join(map(str, dims or [0]))))
        if t.tag == 'DW_TAG_pointer_type':
            return ('ptr', 4)
        if t.tag == 'DW_TAG_enumeration_type':
            return ('enum', attr(t, 'DW_AT_byte_size', 0))
        if t.tag == 'DW_TAG_base_type':
            return ('base:%s' % attr(t, 'DW_AT_name', b'').decode(), attr(t, 'DW_AT_byte_size', 0))
        return (t.tag, attr(t, 'DW_AT_byte_size', 0))

    def visit(self, die):
        key = self.key(die)
        if key in self.types:
            return key
        entry = {'kind': die.tag, 'size': attr(die, 'DW_AT_byte_size', 0), 'members': [],
                 'site': self.site(die), 'name': attr(die, 'DW_AT_name', b'').decode()}
        self.types[key] = entry
        for m in die.iter_children():
            if m.tag != 'DW_TAG_member':
                continue
            loc = attr(m, 'DW_AT_data_member_location', 0)
            bitsize = attr(m, 'DW_AT_bit_size')
            bitoff = attr(m, 'DW_AT_data_bit_offset')
            if bitoff is None and bitsize is not None and 'DW_AT_bit_offset' in m.attributes:
                # DWARF 2/3 style: big-endian-relative bit offset within the storage unit
                bsz = attr(m, 'DW_AT_byte_size')
                bitoff = loc * 8 + bsz * 8 - attr(m, 'DW_AT_bit_offset') - bitsize
            pos = bitoff if bitoff is not None else (loc or 0) * 8
            desc, size = self.type_desc(m.get_DIE_from_attribute('DW_AT_type'))
            entry['members'].append((attr(m, 'DW_AT_name', b'').decode() or '<anon>', pos, bitsize, desc, size))
        return key


def compare(gba, pc):
    """Return {key: reason} for every reachable type whose layout differs."""
    bad = {}
    seen = set()

    by_name = {}
    for k, t in pc.types.items():
        if t['name']:
            by_name.setdefault((t['kind'], t['name']), t)

    def walk(key):
        if key in seen or not isinstance(key, str):
            return
        seen.add(key)
        g, p = gba.types.get(key), pc.types.get(key)
        if p is None and g is not None and g['name']:
            # The PC build may declare the same type in a platform header
            # (e.g. gba/m4a_internal_pc.h); match it by name.
            p = by_name.get((g['kind'], g['name']))
        if g is None or p is None:
            bad[key] = 'type only present on one target'
            return
        if g['size'] != p['size']:
            bad[key] = 'size gba=%d pc=%d' % (g['size'], p['size'])
        if len(g['members']) != len(p['members']):
            bad[key] = 'member count differs'
            return
        shifted = False  # an earlier member already moved; later offsets are consequences
        for gm, pm in zip(g['members'], p['members']):
            sub_g = gm[3]
            while isinstance(sub_g, tuple) and sub_g and sub_g[0] == 'array':
                sub_g = sub_g[2]
            is_aggregate = isinstance(sub_g, str) and sub_g.startswith(('structure', 'union'))
            if (gm[1] != pm[1] or gm[2] != pm[2]) and not shifted:
                shifted = True
                if is_aggregate and gm[4] == pm[4]:
                    # First displaced member, same size: its type's alignment
                    # differs (apcs-gnu aligns every non-packed aggregate to 4).
                    bad.setdefault(sub_g, 'alignment: %s placed at bit %d (gba) vs %d (pc) in %s'
                                   % (gm[0], gm[1], pm[1], key))
                else:
                    bad.setdefault(key, 'member %s at bit %d (gba) vs %d (pc)' % (gm[0], gm[1], pm[1]))
            if gm[4] != pm[4]:
                shifted = True
            if is_aggregate:
                walk(sub_g)
    for root, key in gba.roots.items():
        if root not in pc.roots:
            bad[key] = 'root missing on pc'
            continue
        walk(key)
    return bad


def insert_abi_align(path, line, kind):
    """Insert ABI_ALIGN after the struct/union keyword that opens the declaration."""
    full = os.path.join(ENGINE, path)
    with open(full) as f:
        lines = f.readlines()
    keyword = 'struct' if kind == 'DW_TAG_structure_type' else 'union'
    for idx in (line - 1, line - 2, line):
        if 0 <= idx < len(lines):
            text = lines[idx]
            if 'ABI_ALIGN' in text:
                return 'already marked'
            m = re.search(r'\b%s\b(?!\s*\w+\s*[;*])' % keyword, text)
            if m:
                lines[idx] = text[:m.end()] + ' ABI_ALIGN' + text[m.end():]
                with open(full, 'w') as f:
                    f.writelines(lines)
                return 'marked line %d' % (idx + 1)
    return None


def run(fix):
    for attempt in range(12):
        gba = Model(build('gba'))
        pc = Model(build('pc'))
        bad = compare(gba, pc)
        missing = sorted(set(gba.roots) - set(pc.roots))
        for root in sorted(gba.roots):
            g = gba.types[gba.roots[root]]['size']
            p = pc.types.get(pc.roots.get(root), {}).get('size')
            print('%-36s gba=%-6d pc=%-6s %s' % (root, g, p, 'ok' if g == p else 'DIFF'))
        if missing:
            print('roots missing on pc:', missing)
        if not bad:
            print('ABI layout parity: OK (%d types checked)' % len(gba.types))
            return 0
        print('%d type(s) differ:' % len(bad))
        for key, why in sorted(bad.items()):
            print('  %s  -- %s' % (key, why))
        if not fix:
            return 1
        changed = 0
        for key in sorted(bad):
            t = gba.types.get(key)
            if t is None:
                continue
            path, line = t['site']
            result = insert_abi_align(path, line, t['kind'])
            print('  fix %s:%s -> %s' % (path, line, result))
            if result and result.startswith('marked'):
                changed += 1
        if not changed:
            print('No automatic fix applied; remaining differences need manual review.')
            return 1
    return 1


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--fix', action='store_true', help='insert ABI_ALIGN on differing types and re-check')
    sys.exit(run(ap.parse_args().fix))
