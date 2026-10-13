#!/usr/bin/env python3
"""The battle-environment report: every battle map, its pictures before and after, its class.

    python3 tools/battle_env/report.py --before SURVEY_DIR --after SURVEY_DIR \\
        --review tools/battle_env/review.json --out build/battle_env/report \\
        [--extra LABEL=SURVEY_DIR ...]

Reads build/battle_env/inventory.json (every battle-capable map), the surveys' results.json and
cap.json (a picture of a real battle on each map), and the review: what a person (or Claude)
saw in each picture - the class and what is wrong. A map is

  * untested when no survey has a picture of a battle on it,
  * tested when a survey has one but nobody has looked at it (the review has no class),
  * reviewed when the review gives it a class from its picture:
      A  complete 3D: the place's walls, rock, furniture, trees and buildings stand in 3D, nothing
         important flat, no visible fault;
      B  needs polish: dimensional, with flat details left (flowers, grass, signs, fences, a door)
         or a small fault;
      C  partially 3D: some of it stands (trees, a back wall), but much that should have depth is
         flat;
      D  broken or mostly flat: flat tile art, a void, a battler in a wall, or no battle at all.

Writes OUT/report.md, OUT/report.json and OUT/report.html (the pictures as small JPEGs in the
page). TEST TOOL.
"""

import argparse
import base64
import collections
import html
import io
import json
import os
import re

from PIL import Image

from report_page import write_page

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
CLASSES = ('A', 'B', 'C', 'D')


def survey(path, inv=None):
    out = {}
    if not path:
        return out
    try:
        results = json.load(open(os.path.join(path, 'results.json')))
    except (OSError, ValueError):
        return out
    for r in results:
        # a picture taken on another map than the one asked for (the game resumed elsewhere:
        # a secret base comes back in Littleroot Town) is no picture of this map
        m = re.search(r'on map (\d+)\.(\d+) at', r.get('started') or '')
        rec = (inv or {}).get(r['map'])
        if m and rec and (int(m.group(1)), int(m.group(2))) != (rec['group'], rec['num']):
            r = dict(r, status='wrong map %s.%s' % (m.group(1), m.group(2)))
        cap = None
        try:
            cap = json.load(open(os.path.join(r['dir'], 'cap.json')))
        except (OSError, ValueError):
            pass
        r = dict(r, cap=cap, png=os.path.join(r['dir'], 'cap.png'))
        prev = out.get(r['map'])
        if prev is None or (prev['status'] != 'captured' and r['status'] == 'captured'):
            out[r['map']] = r
    return out


def thumb(path, width=320):
    try:
        im = Image.open(path).convert('RGB')
    except OSError:
        return None
    im = im.resize((width, int(im.height * width / im.width)), Image.LANCZOS)
    buf = io.BytesIO()
    im.save(buf, 'JPEG', quality=72)
    return 'data:image/jpeg;base64,' + base64.b64encode(buf.getvalue()).decode()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--inventory', default=os.path.join(ROOT, 'build/battle_env/inventory.json'))
    ap.add_argument('--before', required=True)
    ap.add_argument('--after', required=True)
    ap.add_argument('--extra', action='append', default=[])
    ap.add_argument('--review', default=os.path.join(ROOT, 'tools/battle_env/review.json'))
    ap.add_argument('--out', required=True)
    ap.add_argument('--no-html', action='store_true')
    ap.add_argument('--title', default='Hoenn Battle Environments')
    ap.add_argument('--meta', default='', help='a line under the title (branch, commit, date)')
    ap.add_argument('--appendix', help='an HTML fragment put after the maps (known issues, method)')
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    inv = json.load(open(args.inventory))['maps']
    by_map = {r['map']: r for r in inv}
    before, after = survey(args.before, by_map), survey(args.after, by_map)
    extras = [(label, survey(path, by_map)) for label, path in (e.split('=', 1) for e in args.extra)]
    try:
        review = json.load(open(args.review))
    except (OSError, ValueError):
        review = {}
    rows = []
    for rec in sorted(inv, key=lambda r: (r['category'], r['map'])):
        m = rec['map']
        rv = review.get(m, {})
        b, a = before.get(m), after.get(m)
        captured = a is not None and a['status'] == 'captured'
        # the class now: the combined development branch's (claude/combined-dev, 'combined') where
        # it was photographed there, else round 4's (claude/battle-environments-3, 'r4'), else
        # round 3's (claude/battle-environments-2, 'now') where it was looked at again, else with
        # the overworld's objects ('props'), else the first rounds' ('after')
        current = next((rv.get(k) for k in ('combined', 'r4', 'now', 'props', 'after')
                        if rv.get(k) in CLASSES), None)
        if rv.get('after') == 'n/a' and current is None:
            state = 'excluded'   # no battle is fought on this map as it stands (a template)
        elif current is not None:
            state = 'reviewed'
        elif captured:
            state = 'tested'
        else:
            state = 'untested'
        rows.append({
            'map': m, 'layout': rec['layout'], 'category': rec['category'],
            'map_type': rec['map_type'], 'battle_scene': rec['battle_scene'],
            'battles': sorted(set((['trainer'] if rec['trainers'] else [])
                                  + (['legendary'] if rec['legendary_species'] else [])
                                  + (['double'] if rec.get('double_trainers') else [])
                                  + (['wild'] if rec.get('wild') else []))),
            'before_status': b['status'] if b else 'not surveyed',
            'after_status': a['status'] if a else 'not surveyed',
            'before_class': rv.get('before') if rv.get('before') in CLASSES else None,
            'after_class': rv.get('after') if rv.get('after') in CLASSES else None,
            # the class with the overworld's detail objects drawn in the battle too (the branch
            # merged with graphics-wip-20261010-8), where that was looked at
            'props_class': rv.get('props') if rv.get('props') in CLASSES else None,
            'props_notes': rv.get('props_notes', ''),
            # round 3's class (claude/battle-environments-2) and round 4's (claude/battle-environments-3)
            'now_class': rv.get('now') if rv.get('now') in CLASSES else None,
            'now_notes': rv.get('now_notes', ''),
            'r4_class': rv.get('r4') if rv.get('r4') in CLASSES else None,
            'r4_notes': rv.get('r4_notes', ''),
            # the combined development branch (claude/combined-dev), where photographed there
            'combined_class': rv.get('combined') if rv.get('combined') in CLASSES else None,
            'combined_notes': rv.get('combined_notes', ''),
            'current_class': current,
            'state': state, 'notes': rv.get('notes', ''), 'problems': rv.get('problems', []),
            'before_png': b['png'] if b and b['status'] == 'captured' else None,
            'after_png': a['png'] if a and a['status'] == 'captured' else None,
            'after_band': (a or {}).get('cap', {}) and (a['cap'] or {}).get('band'),
            'after_staging': (a or {}).get('cap', {}) and (a['cap'] or {}).get('staging'),
            'extras': {label: (s.get(m) or {}).get('png') for label, s in extras
                       if s.get(m) and s[m]['status'] == 'captured'},
        })
    json.dump(rows, open(os.path.join(args.out, 'report.json'), 'w'), indent=1)
    counts = collections.Counter(r['state'] for r in rows)
    cls_after = collections.Counter(r['after_class'] for r in rows if r['after_class'])
    cls_before = collections.Counter(r['before_class'] for r in rows if r['before_class'])
    cls_now = collections.Counter(r['current_class'] for r in rows if r['current_class'])
    with open(os.path.join(args.out, 'report.md'), 'w') as f:
        f.write('# Battle environments: every battle map\n\n')
        f.write('%d maps: %s.\n\n' % (len(rows), ', '.join('%d %s' % (counts[k], k) for k in
                                                          ('reviewed', 'tested', 'untested', 'excluded'))))
        f.write('Classes now: %s. After the first rounds: %s. Before (where reviewed): %s.\n\n' % (
            ', '.join('%s %d' % (c, cls_now[c]) for c in CLASSES),
            ', '.join('%s %d' % (c, cls_after[c]) for c in CLASSES),
            ', '.join('%s %d' % (c, cls_before[c]) for c in CLASSES)))
        for cat in sorted({r['category'] for r in rows}):
            f.write('\n## %s\n\n| map | battles | before | after | now | state | notes |\n|---|---|---|---|---|---|---|\n' % cat)
            for r in rows:
                if r['category'] != cat:
                    continue
                f.write('| %s | %s | %s | %s | %s | %s | %s |\n' % (
                    r['map'][4:], ', '.join(r['battles']), r['before_class'] or '-',
                    r['after_class'] + (' (%s with objects)' % r['props_class'] if r['props_class'] else '')
                    if r['after_class'] else '-', r['current_class'] or '-',
                    r['state'] if r['after_status'] in ('captured', 'not surveyed')
                    else '%s (%s)' % (r['state'], r['after_status']),
                    (r['notes'] + (' With objects: ' + r['props_notes'] if r['props_notes'] else '')
                     + (' Round 3: ' + r['now_notes'] if r['now_notes'] else '')
                     + (' Round 4: ' + r['r4_notes'] if r['r4_notes'] else '')
                     + (' Combined: ' + r['combined_notes'] if r['combined_notes'] else '')).replace('|', '/')))
    if args.no_html:
        return
    write_page(rows, os.path.join(args.out, 'page'), by_map, args)


if __name__ == '__main__':
    main()
