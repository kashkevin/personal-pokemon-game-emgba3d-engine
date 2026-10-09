#!/usr/bin/env python3
"""Keep the GBA build's __LINE__ values unchanged in files edited for the PC port.

Assertions and allocation tags embed __LINE__ in the GBA ROM, so inserting
PORTABLE-only lines shifts those values and changes ROM bytes. This tool compares
each file with the emulator-baseline commit and adds a `#line N` directive after
every edit that changed the line count, so the original lines keep their numbers.

    python3 tools/pc_port/keep_gba_line_numbers.py [--base COMMIT] FILE...

Idempotent: directives it added end with `// gba-line` and are recomputed.
"""
import argparse
import difflib
import subprocess

MARK = ' // gba-line'


def process(path, base):
    original = subprocess.run(['git', 'show', '%s:%s' % (base, path)], capture_output=True,
                              text=True, check=True).stdout.split('\n')
    current = [l for l in open(path).read().split('\n') if not l.endswith(MARK)]
    out = []
    matcher = difflib.SequenceMatcher(a=original, b=current, autojunk=False)
    for tag, i1, i2, j1, j2 in matcher.get_opcodes():
        out.extend(current[j1:j2])
        if tag in ('insert', 'replace') and (j2 - j1) != (i2 - i1) and i2 < len(original):
            # Continuation lines cannot be interrupted by a directive.
            if out and out[-1].rstrip().endswith('\\'):
                continue
            out.append('#line %d%s' % (i2 + 1, MARK))
    open(path, 'w').write('\n'.join(out))


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('--base', default='emulator-baseline-20261009')
    ap.add_argument('files', nargs='+')
    args = ap.parse_args()
    for f in args.files:
        process(f, args.base)
