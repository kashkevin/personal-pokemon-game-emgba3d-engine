"""The battle-environment report as a page: report.py --out OUT writes OUT/page/index.html and
OUT/page/img/<MAP>.jpg (one picture per map: before | after, and below them the extra surveys -
night, the Diorama camera, a double battle - where there are any).

The page has no <html>/<head>/<body> of its own (a browser takes it as it is; published as an
artifact, the host wraps it). TEST TOOL.
"""

import collections
import html
import json
import os
import re

from PIL import Image, ImageDraw, ImageFont

CLASSES = ('A', 'B', 'C', 'D')
PANEL = (640, 360)
FONT_PATHS = ('/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf',
              '/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf')
CLASS_TEXT = {
    'A': 'complete 3D',
    'B': 'needs polish',
    'C': 'partially 3D',
    'D': 'broken or mostly flat',
}
STATE_TEXT = {
    'reviewed': 'looked at, classed',
    'tested': 'photographed, not classed yet',
    'untested': 'no picture of a battle there',
    'excluded': 'not a place a battle is fought as it stands',
}


def pretty(rec):
    """'EverGrandeCity_SidneysRoom' -> 'Ever Grande City · Sidneys Room'."""
    name = rec.get('name') or rec['map'][4:]
    name = re.sub(r'_Frlg$', '', name)
    parts = []
    for part in name.split('_'):
        part = re.sub(r'(?<=[a-z])(?=[A-Z0-9])|(?<=[0-9])(?=[A-Z])', ' ', part)
        parts.append(part)
    return ' · '.join(p for p in parts if p)


def _font(size):
    for path in FONT_PATHS:
        if os.path.exists(path):
            return ImageFont.truetype(path, size)
    return ImageFont.load_default()


def _panel(path, label, why=None):
    font = _font(22)
    if path:
        try:
            im = Image.open(path).convert('RGB').resize(PANEL, Image.LANCZOS)
        except OSError:
            im, why = None, 'picture unreadable'
    else:
        im = None
    if im is None:
        im = Image.new('RGB', PANEL, (58, 66, 61))
        d = ImageDraw.Draw(im)
        text = 'no picture' + (' (%s)' % why if why else '')
        w = d.textlength(text, font=font)
        d.text(((PANEL[0] - w) / 2, PANEL[1] / 2 - 12), text, fill=(214, 222, 217), font=font)
    d = ImageDraw.Draw(im)
    w = d.textlength(label, font=font)
    d.rectangle((0, 0, w + 20, 34), fill=(16, 24, 20))
    d.text((10, 5), label, fill=(240, 246, 242), font=font)
    return im


def composite(row, out_path):
    panels = [('BEFORE', row['before_png'], None if row['before_png'] else row['before_status']),
              ('AFTER', row['after_png'], None if row['after_png'] else row['after_status'])]
    for label, png in row['extras'].items():     # in the order the surveys were given
        panels.append((label.upper(), png, None))
    if not row['before_png'] and not row['after_png'] and not row['extras']:
        return None
    cols = 2 if len(panels) <= 4 else 3
    rows = (len(panels) + cols - 1) // cols
    sheet = Image.new('RGB', (PANEL[0] * cols + 6 * (cols - 1), PANEL[1] * rows + 6 * (rows - 1)),
                      (16, 24, 20))
    for i, (label, png, why) in enumerate(panels):
        x, y = (i % cols) * (PANEL[0] + 6), (i // cols) * (PANEL[1] + 6)
        sheet.paste(_panel(png, label, why), (x, y))
    sheet.save(out_path, 'JPEG', quality=80, optimize=True, progressive=True)
    return sheet.size


def _chip(cls, prefix=''):
    if cls in CLASSES:
        return '<span class="cls c%s" title="%s%s: %s">%s</span>' % (
            cls, prefix, cls, CLASS_TEXT[cls], cls)
    return '<span class="cls cx" title="%snot classed">–</span>' % prefix


def _bar(counter, keys, total, css):
    out = []
    for k in keys:
        n = counter.get(k, 0)
        if n:
            out.append('<span class="seg %s%s" style="flex-grow:%d" title="%s: %d">%s</span>' % (
                css, k, n, k, n, n))
    return '<div class="bar">%s</div>' % ''.join(out)


STYLE = r"""
:root {
  /* a field index: the coverage first, then every map as a card - its class before and after
     and the two pictures - filtered in place */
  --bg: #eef2ee; --surface: #fbfdfb; --ink: #17241c; --muted: #56655c; --line: #cfd9d2;
  --accent: #1d7a4a; --accent-ink: #f2fbf5; --shade: #e3eae5;
  --a-bg: #d9efe1; --a-fg: #165a35; --b-bg: #dae6f5; --b-fg: #1d4777;
  --c-bg: #f8e7c6; --c-fg: #6e4600; --d-bg: #f6d8d3; --d-fg: #8c2a1f; --x-bg: #e2e6e3; --x-fg: #4b554f;
  --rev: #1d7a4a; --tst: #c08a16; --tst-ink: #1d1503; --unt: #9aa59f; --shot: #101814; --shot-ink: #c9d4cd;
  --display: "Silkscreen", "Courier New", monospace;
  --body: "Atkinson Hyperlegible", "Segoe UI", system-ui, sans-serif;
  --mono: "IBM Plex Mono", ui-monospace, "Cascadia Mono", Menlo, monospace;
}
@media (prefers-color-scheme: dark) { :root:not([data-theme="light"]) {
  --bg: #0e1511; --surface: #151f19; --ink: #e2ebe5; --muted: #98a89e; --line: #2b3830;
  --accent: #52c58b; --accent-ink: #0b1a12; --shade: #1b2720;
  --a-bg: #163b27; --a-fg: #94dcb2; --b-bg: #172d48; --b-fg: #a5c8f0; --c-bg: #3f2f0d; --c-fg: #f1c76f;
  --d-bg: #451914; --d-fg: #f3a69b; --x-bg: #26302a; --x-fg: #a9b5ad;
  --rev: #52c58b; --tst: #e0ac3a; --unt: #56625b; color-scheme: dark } }
:root[data-theme="dark"] {
  --bg: #0e1511; --surface: #151f19; --ink: #e2ebe5; --muted: #98a89e; --line: #2b3830;
  --accent: #52c58b; --accent-ink: #0b1a12; --shade: #1b2720;
  --a-bg: #163b27; --a-fg: #94dcb2; --b-bg: #172d48; --b-fg: #a5c8f0; --c-bg: #3f2f0d; --c-fg: #f1c76f;
  --d-bg: #451914; --d-fg: #f3a69b; --x-bg: #26302a; --x-fg: #a9b5ad;
  --rev: #52c58b; --tst: #e0ac3a; --unt: #56625b; color-scheme: dark }
body { background: var(--bg); color: var(--ink); font: 15px/1.5 var(--body); }
.wrap { max-width: 1240px; margin: 0 auto; padding-inline: 16px; padding-block: 28px 64px;
  display: grid; grid-template-columns: minmax(0, 1fr); gap: 28px; }
a { color: var(--accent); text-underline-offset: 2px; }
header h1 { font: 400 clamp(24px, 4.2vw, 40px)/1.1 var(--display); margin: 0; letter-spacing: .5px;
  text-wrap: balance; }
header p { margin: 8px 0 0; max-width: 68ch; color: var(--muted); }
header .meta { font: 13px/1.4 var(--mono); color: var(--muted); margin-top: 10px; }
h2 { font: 700 19px/1.3 var(--body); margin: 0 0 10px; text-wrap: balance; }
.panel { background: var(--surface); border: 1px solid var(--line); border-radius: 10px; padding: 18px; }
.summary { display: grid; grid-template-columns: repeat(auto-fit, minmax(min(100%, 280px), 1fr)); gap: 22px; }
.summary > div, .panel { min-width: 0; }
.summary h3 { font: 700 13px/1.3 var(--body); text-transform: uppercase; letter-spacing: .08em;
  color: var(--muted); margin: 0 0 8px; }
.bar { display: flex; height: 26px; border-radius: 6px; overflow: hidden; background: var(--shade);
  font: 500 12px/26px var(--mono); }
.seg { display: block; min-width: 22px; text-align: center; overflow: hidden; }
.segA { background: var(--a-bg); color: var(--a-fg); } .segB { background: var(--b-bg); color: var(--b-fg); }
.segC { background: var(--c-bg); color: var(--c-fg); } .segD { background: var(--d-bg); color: var(--d-fg); }
.segreviewed { background: var(--rev); color: var(--accent-ink); }
.segtested { background: var(--tst); color: var(--tst-ink); } .seguntested { background: var(--unt); color: var(--ink); }
.segexcluded { background: var(--shade); color: var(--muted); }
.legend { display: flex; flex-wrap: wrap; gap: 6px 14px; margin-top: 8px; font-size: 13px; color: var(--muted); }
.note { font-size: 14px; color: var(--muted); max-width: 75ch; margin: 10px 0 0; }
.tablewrap { overflow-x: auto; }
table { border-collapse: collapse; width: 100%; font-size: 14px; font-variant-numeric: tabular-nums; }
th, td { text-align: left; padding: 6px 10px; border-bottom: 1px solid var(--line); white-space: nowrap; }
th.n { text-align: right; }
th { font-weight: 700; color: var(--muted); font-size: 12px; text-transform: uppercase; letter-spacing: .06em; }
td.n { text-align: right; font-family: var(--mono); }
.filters { display: flex; flex-wrap: wrap; gap: 10px 18px; align-items: center; position: sticky;
  top: env(safe-area-inset-top, 0px); z-index: 2; background: var(--bg); padding-block: 10px; }
.filters .group { display: flex; flex-wrap: wrap; gap: 6px; align-items: center; }
.filters .label { font-size: 12px; text-transform: uppercase; letter-spacing: .08em; color: var(--muted); }
.chipbtn { font: 500 13px/1 var(--body); padding: 7px 10px; border-radius: 999px; cursor: pointer;
  border: 1px solid var(--line); background: var(--surface); color: var(--ink); }
.chipbtn[aria-pressed="true"] { background: var(--accent); color: var(--accent-ink); border-color: var(--accent); }
.chipbtn:focus-visible, input:focus-visible, select:focus-visible { outline: 2px solid var(--accent); outline-offset: 2px; }
input[type=search], select { font: 14px var(--body); padding: 7px 10px; border-radius: 8px;
  border: 1px solid var(--line); background: var(--surface); color: var(--ink); max-width: 100%; }
.count { font: 13px var(--mono); color: var(--muted); }
.grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(min(100%, 480px), 1fr)); gap: 16px;
  align-items: start; }
.card { background: var(--surface); border: 1px solid var(--line); border-radius: 10px; overflow: hidden;
  display: grid; grid-template-rows: auto auto 1fr; min-width: 0; }
.card .head { padding: 12px 14px 8px; display: grid; gap: 4px; }
.card .title { display: flex; gap: 10px; align-items: baseline; justify-content: space-between; }
.card h3 { font: 700 16px/1.3 var(--body); margin: 0; min-width: 0; overflow-wrap: anywhere; }
.card .id { font: 12px/1.4 var(--mono); color: var(--muted); overflow-wrap: anywhere; }
.card .tags { display: flex; flex-wrap: wrap; gap: 4px 10px; font-size: 12.5px; color: var(--muted); }
.card .shot { background: var(--shot); }
.card .shot img { display: block; width: 100%; height: auto; cursor: zoom-in; }
.card .shot .none { aspect-ratio: 32 / 9; }
dialog.zoom { padding: 0; border: 0; background: var(--shot); max-width: 96vw; max-height: 96vh; }
dialog.zoom::backdrop { background: rgba(5, 10, 7, .78); }
dialog.zoom img { display: block; max-width: 96vw; max-height: 90vh; width: auto; height: auto; }
dialog.zoom .zoombar { display: flex; justify-content: space-between; align-items: center; gap: 12px;
  padding: 8px 12px; color: var(--shot-ink); font-size: 14px; }
dialog.zoom button { font: 500 14px var(--body); padding: 6px 12px; border-radius: 6px; cursor: pointer;
  border: 1px solid var(--line); background: var(--surface); color: var(--ink); }
.card .shot .none { display: grid; place-items: center; height: 100%; color: var(--shot-ink); font-size: 13px; }
.card .body { padding: 10px 14px 14px; font-size: 14px; display: grid; gap: 6px; align-content: start; }
.card .state { font-size: 12.5px; color: var(--muted); }
.trans { display: inline-flex; align-items: center; gap: 4px; flex: none; }
.trans .arrow { color: var(--muted); font-size: 13px; }
.cls { display: inline-grid; place-items: center; min-width: 26px; height: 26px; border-radius: 6px;
  font: 400 15px/1 var(--display); }
.cA { background: var(--a-bg); color: var(--a-fg); } .cB { background: var(--b-bg); color: var(--b-fg); }
.cC { background: var(--c-bg); color: var(--c-fg); } .cD { background: var(--d-bg); color: var(--d-fg); }
.cx { background: var(--x-bg); color: var(--x-fg); font-family: var(--body); }
.appendix { display: grid; gap: 18px; }
.appendix h2 { margin-top: 6px; }
.appendix p, .appendix li { max-width: 80ch; }
.appendix code { font: 13px var(--mono); background: var(--shade); padding: 1px 4px; border-radius: 4px; }
.empty { padding: 30px; text-align: center; color: var(--muted); }
@media (prefers-reduced-motion: no-preference) { .card { transition: border-color .15s; }
  .card:hover { border-color: var(--accent); } }
"""

SCRIPT = r"""
(function () {
  var state = { cls: new Set(), st: new Set(), cat: '', q: '' };
  var cards = Array.prototype.slice.call(document.querySelectorAll('.card'));
  var count = document.getElementById('shown');
  var empty = document.getElementById('empty');
  function apply() {
    var n = 0;
    cards.forEach(function (c) {
      var ok = (!state.cls.size || state.cls.has(c.dataset.cls)) &&
               (!state.st.size || state.st.has(c.dataset.state)) &&
               (!state.cat || c.dataset.group === state.cat) &&
               (!state.q || c.dataset.search.indexOf(state.q) >= 0);
      c.hidden = !ok; if (ok) n++;
    });
    count.textContent = n + ' of ' + cards.length + ' maps';
    empty.hidden = n !== 0;
  }
  document.querySelectorAll('[data-filter]').forEach(function (b) {
    b.addEventListener('click', function () {
      var set = state[b.dataset.filter], v = b.dataset.value;
      if (set.has(v)) set.delete(v); else set.add(v);
      b.setAttribute('aria-pressed', set.has(v) ? 'true' : 'false');
      apply();
    });
  });
  document.getElementById('cat').addEventListener('change', function (e) { state.cat = e.target.value; apply(); });
  document.getElementById('q').addEventListener('input', function (e) {
    state.q = e.target.value.trim().toLowerCase(); apply(); });
  document.querySelectorAll('[data-jump]').forEach(function (a) {
    a.addEventListener('click', function (e) {
      e.preventDefault();
      var sel = document.getElementById('cat'); sel.value = a.dataset.jump; state.cat = a.dataset.jump; apply();
      document.getElementById('maps').scrollIntoView();
    });
  });
  var dlg = document.getElementById('zoom'), big = document.getElementById('zoomimg'),
      cap = document.getElementById('zoomcap');
  document.querySelectorAll('.card .shot img').forEach(function (im) {
    im.addEventListener('click', function () {
      big.src = im.src; big.alt = im.alt; cap.textContent = im.alt;
      if (dlg.showModal) dlg.showModal(); else dlg.setAttribute('open', '');
    });
  });
  dlg.addEventListener('click', function (e) { if (e.target === dlg) dlg.close(); });
  document.getElementById('zoomclose').addEventListener('click', function () { dlg.close(); });
  apply();
})();
"""

# categories grouped for the filter (the inventory's categories are finer)
GROUPS = [
    ('League and Gyms', ('elite_four', 'champion', 'gym')),
    ('Battle Frontier and Tents', ('frontier', 'frontier_outdoor', 'frontier_pyramid', 'battle_tent',
                                   'trainer_hill', 'link_room')),
    ('Caves and tunnels', ('cave', 'cave_water', 'cave_ice', 'cave_heat', 'tunnel', 'ruins_chamber')),
    ('Hideouts, towers, ships', ('hideout_aqua', 'hideout_magma', 'tower', 'sky_pillar_top', 'ship',
                                 'power_plant', 'trick_house', 'building', 'house', 'secret_base')),
    ('Routes and land', ('route', 'route_grass', 'rainy_route', 'forest', 'desert', 'mountain',
                         'mountain_fog', 'volcanic_ash', 'safari')),
    ('Towns and cities', ('town', 'city', 'rainy_town', 'beach_town')),
    ('Sea, shore, underwater', ('ocean', 'coastal', 'beach', 'island', 'underwater')),
]


def group_of(category):
    for name, cats in GROUPS:
        if category in cats:
            return name
    return 'Other'


def write_page(rows, out, by_map, args):
    os.makedirs(os.path.join(out, 'img'), exist_ok=True)
    counts = collections.Counter(r['state'] for r in rows)
    cls_after = collections.Counter(r['after_class'] for r in rows if r['after_class'])
    cls_before = collections.Counter(r['before_class'] for r in rows if r['before_class'] and r['after_class'])
    both = sum(1 for r in rows if r['before_class'] and r['after_class'])
    # the class now (report.py current_class: round 4's, else round 3's, else with the objects, else after)
    cls_now = collections.Counter(r.get('current_class') for r in rows if r.get('current_class'))
    total = len(rows)

    cards = []
    for r in rows:
        rec = by_map[r['map']]
        name = pretty(rec)
        stem = r['map'][4:]
        size = composite(r, os.path.join(out, 'img', stem + '.jpg'))
        if size:
            shot = '<div class="shot"><img src="img/%s.jpg" alt="%s: a battle there, before and after" ' \
                   'width="%d" height="%d" loading="lazy" decoding="async"></div>' % (
                       html.escape(stem), html.escape(name), size[0], size[1])
        else:
            shot = '<div class="shot"><div class="none">no picture (%s)</div></div>' % html.escape(
                r['after_status'])
        void = (r.get('after_band') or {}).get('void')
        tags = [html.escape(r['category'].replace('_', ' ')), html.escape(', '.join(r['battles']) or 'battle')]
        if isinstance(void, (int, float)) and void >= 0.05:
            tags.append('void %d%% of the upper band' % round(void * 100))
        status = STATE_TEXT[r['state']]
        if r['after_status'] not in ('captured', 'not surveyed'):
            status += '; survey: ' + r['after_status']
        elif r['after_status'] == 'not surveyed':
            status += '; not surveyed'
        cards.append(
            '<article class="card" data-cls="%s" data-state="%s" data-group="%s" data-search="%s">'
            '<div class="head"><div class="title"><h3>%s</h3><span class="trans">%s<span class="arrow">→</span>%s%s</span></div>'
            '<div class="id">%s</div><div class="tags">%s</div></div>%s'
            '<div class="body"><div class="state">%s</div>%s</div></article>' % (
                r.get('current_class') or 'none', r['state'], html.escape(group_of(r['category'])),
                html.escape((name + ' ' + r['map'] + ' ' + r['category'] + ' ' + r['notes']).lower()),
                html.escape(name), _chip(r['before_class'], 'before '), _chip(r['after_class'], 'after '),
                (('<span class="arrow" title="with the overworld\'s detail objects">+</span>%s' % _chip(
                    r['props_class'], 'with the overworld\'s objects ')) if r.get('props_class') else '')
                + (('<span class="arrow" title="round 3 (claude/battle-environments-2)">→</span>%s' % _chip(
                    r['now_class'], 'round 3 ')) if r.get('now_class') else '')
                + (('<span class="arrow" title="now: round 4 (claude/battle-environments-3)">→</span>%s' % _chip(
                    r['r4_class'], 'now ')) if r.get('r4_class') else ''),
                html.escape(r['map']), ''.join('<span>%s</span>' % t for t in tags), shot,
                html.escape(status),
                ('<p style="margin:0">%s</p>' % html.escape(r['notes']) if r['notes'] else '')
                + ('<p style="margin:0"><strong>With the overworld\'s objects:</strong> %s</p>' % html.escape(
                    r['props_notes']) if r.get('props_notes') else '')
                + ('<p style="margin:0"><strong>Round 3:</strong> %s</p>' % html.escape(
                    r['now_notes']) if r.get('now_notes') else '')
                + ('<p style="margin:0"><strong>Now (round 4):</strong> %s</p>' % html.escape(
                    r['r4_notes']) if r.get('r4_notes') else '')))

    # the category table
    by_group = collections.OrderedDict((g, []) for g, _ in GROUPS)
    for r in rows:
        by_group.setdefault(group_of(r['category']), []).append(r)
    trs = []
    for g, rs in by_group.items():
        if not rs:
            continue
        c = collections.Counter(r.get('current_class') for r in rs if r.get('current_class'))
        s = collections.Counter(r['state'] for r in rs)
        trs.append('<tr><td><a href="#maps" data-jump="%s">%s</a></td><td class="n">%d</td>%s'
                   '<td class="n">%s</td><td class="n">%s</td><td class="n">%s</td></tr>' % (
                       html.escape(g), html.escape(g), len(rs),
                       ''.join('<td class="n">%s</td>' % (c[k] or '·') for k in CLASSES),
                       s['tested'] or '·', s['untested'] or '·', s['excluded'] or '·'))
    table = ('<div class="tablewrap"><table><thead><tr><th>Places</th><th class="n">Maps</th><th class="n">A</th>'
             '<th class="n">B</th><th class="n">C</th><th class="n">D</th><th class="n">Photographed only</th>'
             '<th class="n">No picture</th><th class="n">Excluded</th></tr></thead><tbody>%s'
             '</tbody></table></div>' % ''.join(trs))

    groups_opts = ''.join('<option value="%s">%s</option>' % (html.escape(g), html.escape(g))
                          for g, rs in by_group.items() if rs)
    cls_btns = ''.join('<button type="button" class="chipbtn" data-filter="cls" data-value="%s" '
                       'aria-pressed="false">%s %s</button>' % (k, k, CLASS_TEXT[k]) for k in CLASSES)
    cls_btns += ('<button type="button" class="chipbtn" data-filter="cls" data-value="none" '
                 'aria-pressed="false">not classed</button>')
    st_btns = ''.join('<button type="button" class="chipbtn" data-filter="st" data-value="%s" '
                      'aria-pressed="false">%s</button>' % (k, k) for k in ('reviewed', 'tested', 'untested', 'excluded') if counts[k])
    props_rows = [r for r in rows if r.get('props_class')]
    props_summary = ''
    if props_rows:
        cp = collections.Counter(r['props_class'] for r in props_rows)
        ca = collections.Counter(r['after_class'] for r in props_rows if r['after_class'])
        props_summary = ('<div><h3>With the overworld\'s detail objects, %d maps</h3>%s'
                         '<div class="legend"><span>the same maps without them: %s</span></div></div>' % (
                             len(props_rows), _bar(cp, CLASSES, len(props_rows), 'seg'),
                             ', '.join('%s %d' % (k, ca[k]) for k in CLASSES)))
    appendix = ''
    if args.appendix and os.path.exists(args.appendix):
        appendix = '<section class="appendix panel">%s</section>' % open(args.appendix).read()

    page = (
        '<title>%s</title>\n'
        '<link rel="preconnect" href="https://fonts.googleapis.com">'
        '<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>'
        '<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Silkscreen&'
        'family=Atkinson+Hyperlegible:wght@400;700&family=IBM+Plex+Mono:wght@400;500&display=swap">\n'
        '<style>%s</style>\n<div class="wrap">'
        '<header><h1>%s</h1><p>Every map where a battle can start, photographed in a real battle of the '
        '3D build and classed by looking at the picture: the class before this work and after it.</p>'
        '<div class="meta">%s</div></header>'
        '<section class="panel summary">'
        '<div><h3>Coverage, %d maps</h3>%s<div class="legend"><span>reviewed: %s</span><span>tested: %s</span>'
        '<span>untested: %s</span><span>excluded: %s</span></div></div>'
        '<div><h3>Before, the %d maps classed both times</h3>%s</div>'
        '<div><h3>Now, all %d classed maps</h3>%s<div class="legend">%s</div></div>'
        '%s</section>'
        '<section class="panel"><h2>By kind of place</h2>%s'
        '<p class="note">A: complete 3D. B: dimensional, with flat details or a small fault. C: some of it '
        'stands, much that should have depth is flat. D: flat tile art, a void, or no 3D battle.</p></section>'
        '<section id="maps"><h2>Maps</h2><div class="filters" role="group" aria-label="Filter the maps">'
        '<div class="group"><span class="label">Class</span>%s</div>'
        '<div class="group"><span class="label">State</span>%s</div>'
        '<div class="group"><label class="label" for="cat">Places</label><select id="cat">'
        '<option value="">all</option>%s</select></div>'
        '<div class="group"><label class="label" for="q">Find</label>'
        '<input id="q" type="search" placeholder="Sidney, cave, Route 119…" autocomplete="off"></div>'
        '<span class="count" id="shown"></span></div>'
        '<div class="grid">%s</div><p class="empty" id="empty" hidden>No map matches these filters.</p>'
        '</section>%s</div>'
        '<dialog class="zoom" id="zoom"><img id="zoomimg" alt=""><div class="zoombar"><span id="zoomcap"></span>'
        '<button type="button" id="zoomclose">Close</button></div></dialog>\n<script>%s</script>\n' % (
            html.escape(args.title), STYLE, html.escape(args.title), html.escape(args.meta),
            total, _bar(counts, ('reviewed', 'tested', 'untested', 'excluded'), total, 'seg'),
            counts['reviewed'], counts['tested'], counts['untested'], counts['excluded'],
            both, _bar(cls_before, CLASSES, both, 'seg'),
            sum(cls_now.values()), _bar(cls_now, CLASSES, sum(cls_now.values()), 'seg'),
            ''.join('<span>%s %s</span>' % (k, CLASS_TEXT[k]) for k in CLASSES),
            props_summary, table, cls_btns, st_btns, groups_opts, ''.join(cards), appendix, SCRIPT))
    with open(os.path.join(out, 'index.html'), 'w') as f:
        f.write(page)
    json.dump({'maps': total, 'states': counts, 'now': cls_now, 'after': cls_after, 'before': cls_before},
              open(os.path.join(out, 'summary.json'), 'w'), indent=1)
