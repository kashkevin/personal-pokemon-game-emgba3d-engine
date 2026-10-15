#!/usr/bin/env python3
"""The number of maps in each map group, from include/constants/map_groups.h.

The Town Map markers (src/region_map.c) look up the header of the map another player
stands on; that player's map ids arrive over the network, so the lookup is bounded by
this table. `--check` compares the table committed in src/region_map.c with the header
(make check-pokenav-map); with no argument the table is printed.
"""
import re
import sys

HEADER = 'include/constants/map_groups.h'
SOURCE = 'src/region_map.c'
ENTRY = re.compile(r'^\s+MAP_\w+\s*=\s*\((\d+)\s*\|\s*\((\d+)\s*<<\s*8\)\)', re.M)


def counts():
    text = open(HEADER).read()
    total = int(re.search(r'#define MAP_GROUPS_COUNT (\d+)', text).group(1))
    result = [0] * total
    for number, group in ENTRY.findall(text):
        result[int(group)] = max(result[int(group)], int(number) + 1)
    return result


def table(values):
    lines = []
    for i in range(0, len(values), 16):
        lines.append('    ' + ', '.join(str(v) for v in values[i:i + 16]) + ',')
    return '\n'.join(lines)


if __name__ == '__main__':
    values = counts()
    if sys.argv[1:] == ['--check']:
        source = open(SOURCE).read()
        match = re.search(r'sMapGroupCounts\[MAP_GROUPS_COUNT\] =\n\{\n(.*?)\n\};', source, re.S)
        committed = [int(v) for v in re.findall(r'\d+', match.group(1))] if match else None
        if committed != values:
            print('src/region_map.c sMapGroupCounts is out of date; run tools/pc_port/map_group_counts.py')
            sys.exit(1)
        print('map group counts: %d groups, %d maps, table matches the header' % (len(values), sum(values)))
    else:
        print(table(values))
