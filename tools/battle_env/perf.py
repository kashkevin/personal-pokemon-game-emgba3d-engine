#!/usr/bin/env python3
"""What a 3D battle's world costs at 1080p, 1440p and 4K: the survey's battles at each size.

    python3 tools/battle_env/perf.py --exe pokeemerald-mp-diorama-envtest --save town.sav \\
        --out build/battle_env/perf [--maps MAP_ROUTE101,...] [--sizes 1920x1080:99,2560x1440:98,3840x2160:97]
        [--redraw both|on|off] [--hold 240]

For each map and window size (each on its own X display of that size: Xvfb :99 1920x1080,
:98 2560x1440, :97 3840x2160) it runs the survey's battle (survey.py) with the window at that
resolution and holds the action menu for `--hold` frames before the picture. With --redraw on
the world is drawn afresh every frame (PKMN_DIORAMA_BATTLE_KEEP=0: the cost of a frame that
moves the camera or animates the water), off is the game's own (the world kept while nothing in
it moves), both runs each. From cap.json: the world's draw calls (terrain, buildings) and
triangles of the capture's fresh draw, and the battle's average frame times so far (kept,
redrawn, the camera's flight).

Writes OUT/perf.json and OUT/perf.md (a table). These are the cloud computer's software
renderer's times (Mesa llvmpipe on 2 cores): they compare sizes and places with one another;
what a graphics card does is measured on one (tools/battle_env/README.md).
TEST TOOL.
"""

import argparse
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_MAPS = ('MAP_ROUTE101,MAP_PETALBURG_WOODS,MAP_EVER_GRANDE_CITY_SIDNEYS_ROOM,'
                'MAP_MAUVILLE_CITY_GYM,MAP_GRANITE_CAVE_1F,MAP_NEW_MAUVILLE_INSIDE')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--exe', required=True)
    ap.add_argument('--save', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--maps', default=DEFAULT_MAPS)
    ap.add_argument('--sizes', default='1920x1080:99,2560x1440:98,3840x2160:97')
    ap.add_argument('--redraw', default='both', choices=('both', 'on', 'off'))
    ap.add_argument('--hold', type=int, default=240)
    ap.add_argument('--timeout', type=float, default=900.0)
    args = ap.parse_args()
    rows = []
    modes = {'both': ('on', 'off'), 'on': ('on',), 'off': ('off',)}[args.redraw]
    for size in args.sizes.split(','):
        res, display = size.split(':')
        for mode in modes:
            out = os.path.join(args.out, '%s-redraw-%s' % (res, mode))
            cmd = [sys.executable, os.path.join(HERE, 'survey.py'), '--exe', args.exe,
                   '--save', args.save, '--out', out, '--display', ':' + display,
                   '--maps', args.maps, '--delay', '100', '--capture-delay', str(args.hold),
                   '--timeout', str(args.timeout), '--skip-done',
                   '--cfg-extra', 'resolution=%s\\n' % res]
            if mode == 'on':
                cmd += ['--env', 'PKMN_DIORAMA_BATTLE_KEEP=0']
            print(' '.join(cmd), flush=True)
            subprocess.run(cmd)
            try:
                results = json.load(open(os.path.join(out, 'results.json')))
            except (OSError, ValueError):
                results = []
            for r in results:
                row = {'map': r['map'], 'resolution': res, 'redraw': mode, 'status': r['status']}
                try:
                    cap = json.load(open(os.path.join(r['dir'], 'cap.json')))
                except (OSError, ValueError):
                    cap = None
                if cap:
                    row['window'] = cap.get('window')
                    row['scene'] = cap.get('scene')
                    row.update(cap.get('perf', {}))
                rows.append(row)
    os.makedirs(args.out, exist_ok=True)
    json.dump(rows, open(os.path.join(args.out, 'perf.json'), 'w'), indent=1)
    with open(os.path.join(args.out, 'perf.md'), 'w') as f:
        f.write('| map | window | 3D view | world redrawn every frame | redrawn frame ms (avg, max) '
                '| kept frame ms | terrain draws | building draws | terrain triangles | building vertices |\n')
        f.write('|---|---|---|---|---|---|---|---|---|---|\n')
        for r in rows:
            if r['status'] != 'captured' or 'terrain_draws' not in r:
                f.write('| %s | %s | | %s | %s | | | | | |\n' % (r['map'][4:], r['resolution'],
                                                             r['redraw'], r['status']))
                continue
            f.write('| %s | %s | %s | %s | %.1f, %.1f (%d frames) | %.1f (%d frames) | %d | %d | %d | %d |\n' % (
                r['map'][4:], 'x'.join(map(str, r['window'])), 'x'.join(map(str, r['scene'])),
                r['redraw'], r['redraw_ms'], r['redraw_max_ms'], r['redraw_frames'], r['kept_ms'],
                r['kept_frames'], r['terrain_draws'], r['building_draws'], r['terrain_triangles'],
                r['building_vertices']))
    print(open(os.path.join(args.out, 'perf.md')).read())


if __name__ == '__main__':
    main()
