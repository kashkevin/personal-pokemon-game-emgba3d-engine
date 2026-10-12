#!/usr/bin/env python3
"""Battle-environment survey: photograph a real battle on every battle-capable map.

TEST TOOL (never shipped). Needs the test-only game build with the survey driver
(-DDIORAMA_BATTLE_ENV_TEST, see include/diorama/battle_env_test.h), an X display
(Xvfb), xdotool, and a save to start from (any save with a party of two or more;
no save is ever written back).

For each map of build/battle_env/inventory.json it copies the save with the
continue-warp set to the map's test position (as the game does after saving in
the Battle Frontier), starts the game, presses Continue, and lets the driver
start one battle once the field is ready: the map's own trainer, a legendary
where one waits, or a wild Pokemon from the map's own table. At the action menu
the game writes cap.png (the window) and cap.json (what the battle camera sees:
backdrop, floor, upright surfaces, depth of the scenery) and exits.

    python3 tools/battle_env/survey.py --exe pokeemerald-mp-diorama-envtest \\
        --save town.sav --out build/battle_env/survey [--maps MAP_ROUTE101,...]
        [--category gym,elite_four] [--hour 12] [--kind auto|trainer|wild_land|...]

Writes <out>/<MAP>-<kind>/{cap.png,cap.json,game.log,result.json} and
<out>/results.json (all runs).
"""

import argparse
import datetime
import json
import os
import re
import struct
import subprocess
import sys
import time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
KEYS = {'A': 'z', 'B': 'x', 'START': 'Return'}
TRAINER_FIRST = {'gym', 'elite_four', 'champion', 'hideout_aqua', 'hideout_magma', 'ship', 'tower',
                 'trick_house', 'building', 'house', 'mountain', 'battle_tent', 'trainer_hill'}


# ── constants ──────────────────────────────────────────────────────────────

def read_constants():
    species, trainers = {}, {}
    for m in re.finditer(r'\b(SPECIES_\w+)\s*=\s*(\d+)', open(os.path.join(ROOT, 'include/constants/species.h')).read()):
        species[m.group(1)] = int(m.group(2))
    for m in re.finditer(r'#define\s+(TRAINER_\w+)\s+(\d+)', open(os.path.join(ROOT, 'include/constants/opponents.h')).read()):
        trainers[m.group(1)] = int(m.group(2))
    return species, trainers


def first_species(map_id, kind):
    data = json.load(open(os.path.join(ROOT, 'src/data/wild_encounters.json')))
    for group in data['wild_encounter_groups']:
        for enc in group['encounters']:
            if enc.get('map') == map_id and kind in enc:
                mons = enc[kind].get('mons', [])
                if mons:
                    return mons[0]['species'], max(5, mons[0].get('max_level', 20))
    return None, None


# ── save ───────────────────────────────────────────────────────────────────

SECTOR = 0x1000
SB2_SIZE = 3884
SB1_SIZE = 15568
DATA = 3968
WARP_FLAGS = 0x09
CONTINUE_WARP = 0x0C


def checksum(data):
    total = sum(struct.unpack_from('<%dI' % (len(data) // 4), data)) & 0xFFFFFFFF
    return ((total >> 16) + total) & 0xFFFF


def make_save(src, dst, group, num, x, y):
    """The save with its continue-game warp set (SaveBlock1.continueGameWarp and the
    CONTINUE_GAME_WARP flag in SaveBlock2), in the newest slot, checksums fixed."""
    d = bytearray(open(src, 'rb').read())
    best = None
    for slot in range(2):
        secs = {}
        for i in range(14):
            off = (slot * 14 + i) * SECTOR
            sid, _, sig, ctr = struct.unpack_from('<HHII', d, off + 0xFF4)
            if sig == 0x08012025:
                secs[sid] = (off, ctr)
        if len(secs) == 14:
            ctr = max(c for _, c in secs.values())
            if best is None or ctr > best[1]:
                best = (secs, ctr)
    if best is None:
        raise SystemExit('no valid save slot in %s' % src)
    secs = best[0]

    def fix(sid, size):
        off = secs[sid][0]
        struct.pack_into('<H', d, off + 0xFF6, checksum(bytes(d[off:off + size])))

    d[secs[0][0] + WARP_FLAGS] |= 1
    fix(0, SB2_SIZE)
    struct.pack_into('<bbbxhh', d, secs[1][0] + CONTINUE_WARP, group, num, -1, x, y)
    fix(1, min(SB1_SIZE, DATA))
    open(dst, 'wb').write(d)


# ── the game ───────────────────────────────────────────────────────────────

def tz_for_hour(hour):
    """A POSIX TZ that puts the host clock's local time at about `hour` (the game's day
    and night follow the host clock)."""
    utc = datetime.datetime.now(datetime.timezone.utc)
    offset = (hour - utc.hour + 12) % 24 - 12
    return 'TST%+d' % (-offset)


class Run:
    def __init__(self, exe, rundir, display, env):
        self.rundir = rundir
        self.display = display
        self.env = dict(os.environ, DISPLAY=display, SDL_AUDIODRIVER='dummy', **env)
        self.log = open(os.path.join(rundir, 'game.log'), 'w')
        self.proc = subprocess.Popen([exe, '--save', 'test.sav'], cwd=rundir, env=self.env,
                                     stdout=self.log, stderr=subprocess.STDOUT)
        self.wid = None
        self.geo = None

    def x(self, *args):
        return subprocess.run(['xdotool'] + list(args), env=self.env, capture_output=True, text=True).stdout

    def find_window(self, timeout=30):
        t0 = time.time()
        while time.time() - t0 < timeout and self.proc.poll() is None:
            ids = self.x('search', '--name', 'Pokemon Emerald').split()
            if ids:
                self.wid = ids[-1]
                self.x('windowfocus', self.wid)
                g = dict(l.split('=') for l in self.x('getwindowgeometry', '--shell', self.wid).split())
                self.geo = int(g['X']), int(g['Y']), int(g['WIDTH']), int(g['HEIGHT'])
                return True
            time.sleep(0.3)
        return False

    def key(self, k, hold=0.12, after=0.3):
        k = KEYS.get(k, k)
        self.x('keydown', k)
        time.sleep(hold)
        self.x('keyup', k)
        time.sleep(after)

    def pixel(self, fx, fy):
        # where the window is now: a resolution that does not fit the screen moves and resizes
        # it after it opens (perf.py's sizes), and the first geometry no longer holds
        g = self.x('getwindowgeometry', '--shell', self.wid)
        if 'WIDTH=' in g:
            g = dict(l.split('=') for l in g.split())
            self.geo = int(g['X']), int(g['Y']), int(g['WIDTH']), int(g['HEIGHT'])
        X, Y, W, H = self.geo
        out = subprocess.run(['import', '-window', 'root', '-crop', '1x1+%d+%d' % (X + int(fx * W), Y + int(fy * H)),
                              '+repage', '-depth', '8', 'txt:-'], env=self.env, capture_output=True, text=True).stdout
        return out.splitlines()[-1].split()[2] if out.strip() else ''

    def log_text(self):
        try:
            return open(os.path.join(self.rundir, 'game.log'), errors='replace').read()
        except OSError:
            return ''

    def stop(self):
        if self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(5)
            except subprocess.TimeoutExpired:
                self.proc.kill()
        self.log.close()


def choose(rec, species, trainers, kind):
    """(label, battle spec, position) for a map."""
    pos = rec['test_positions']
    cat = rec['category']
    options = []
    if rec['legendary_species']:
        sp = rec['legendary_species'][0]
        p = pos.get('trainer') or pos.get('any') or next(iter(pos.values()))
        options.append(('legendary', 'legendary:%d:50' % species.get(sp, 1), p))
    if rec['trainers'] and ('trainer' in pos or 'any' in pos):
        tid = trainers.get(rec['trainers'][0])
        if tid:
            options.append(('trainer', 'trainer:%d' % tid, pos.get('trainer') or pos.get('any')))
    for wk, table in (('wild_land', 'land_mons'), ('wild_water', 'water_mons')):
        if wk in pos:
            sp, lv = first_species(rec['map'], table)
            if sp:
                options.append((wk, 'wild:%d:%d' % (species.get(sp, 1), min(lv, 50)), pos[wk]))
    if not options:
        p = pos.get('any') or pos.get('trainer') or next(iter(pos.values()))
        options.append(('wild', 'wild:%d:20' % species['SPECIES_ZIGZAGOON'], p))
    if kind != 'auto':
        chosen = [o for o in options if o[0] == kind]
        return chosen
    # one per map: the battle that map is known for
    order = ['legendary']
    order += ['trainer', 'wild_land', 'wild_water', 'wild'] if cat in TRAINER_FIRST else \
        (['wild_water', 'trainer', 'wild_land', 'wild'] if cat in ('ocean',) else
         ['wild_land', 'trainer', 'wild_water', 'wild'])
    options.sort(key=lambda o: order.index(o[0]) if o[0] in order else 9)
    return options[:1]


def run_map(args, rec, label, spec, p, results):
    name = '%s-%s' % (rec['map'][4:], label)
    rundir = os.path.join(args.out, name)
    os.makedirs(rundir, exist_ok=True)
    for f in ('cap.png', 'cap.json', 'result.json'):
        try:
            os.remove(os.path.join(rundir, f))
        except OSError:
            pass
    open(os.path.join(rundir, 'pokeemerald.cfg'), 'w').write(
        'borderBackground=0\nbackgroundOrder=2\nfullscreen=0\nwindowScale=%d\nintegerScale=0\nvsync=0\n'
        'border=1\nvolume=0\ncryVolume=0\n' % args.scale + args.cfg_extra.replace('\\n', '\n'))
    make_save(args.save, os.path.join(rundir, 'test.sav'), rec['group'], rec['num'], p['x'], p['y'])
    env = {'PKMN_TEST_BATTLE': spec, 'PKMN_TEST_CAPTURE': os.path.join(rundir, 'cap'), 'PKMN_TEST_EXIT': '1',
           'PKMN_TEST_BATTLE_DELAY': str(args.delay), 'PKMN_TEST_CAPTURE_DELAY': str(args.capture_delay),
           'PKMN_DIORAMA_LOG': '1', 'TZ': tz_for_hour(args.hour)}
    for kv in args.env:
        k, v = kv.split('=', 1)
        env[k] = v
    if args.autopilot:
        env['PKMN_TEST_AUTOPILOT'] = '1'
        env['PKMN_TEST_TIMEOUT'] = str(max(30, int(args.timeout) - 10))
    if args.fight:
        # A through the battle after the picture; the game reports the outcome and ends
        env['PKMN_TEST_FIGHT'] = str(args.fight)
    t0 = time.time()
    run = Run(args.exe, rundir, args.display, env)
    status = 'timeout'
    try:
        if not run.find_window():
            status = 'no window'
        elif args.autopilot:
            limit = args.timeout + (args.fight or 0)
            while time.time() - t0 < limit and run.proc.poll() is None:
                time.sleep(1.0)
            if os.path.exists(os.path.join(rundir, 'cap.json')):
                status = 'captured'
            elif run.proc.poll() is not None:
                status = 'exited without capture'
        else:
            time.sleep(1.5)
            menu = False
            for _ in range(40):
                if run.proc.poll() is not None:
                    break
                if run.pixel(220 / 960, 500 / 540) == '#8B94FF':
                    run.key('A', 0.15, 0.5)
                    menu = True
                    break
                run.key('START', 0.15, 1.0)
            # the battle's opening messages wait for a button: B moves them on and does
            # nothing in the field or at the action menu
            # before it: a map's own scene on arrival (the Champion walking in, a rival's
            # words) waits for a button too; B moves its text on and does nothing else in the
            # field
            idle = 0
            while time.time() - t0 < args.timeout and run.proc.poll() is None:
                text = run.log_text()
                if 'Diorama battle: 3D on' in text or 'Diorama battle: 2D' in text:
                    run.key('B', 0.08, 0.9)
                elif 'Battle env test: starting' not in text and idle >= 3:
                    run.key('B', 0.08, 0.5)
                    idle = 0
                else:
                    time.sleep(1.0)
                    idle += 1
            if os.path.exists(os.path.join(rundir, 'cap.json')):
                status = 'captured'
            elif run.proc.poll() is not None:
                status = 'exited without capture'
            elif not menu:
                status = 'no main menu'
    finally:
        run.stop()
    log = run.log_text()
    res = {'map': rec['map'], 'label': label, 'spec': spec, 'position': p, 'category': rec['category'],
           'status': status, 'seconds': round(time.time() - t0, 1), 'dir': rundir}
    if args.fight:
        m = re.search(r'Battle env test: battle over, outcome \d+ \((\w+)\)', log)
        res['fight'] = m.group(1) if m else ('crashed' if re.search(r'stack smashing|crashed|Aborted', log)
                                              else 'not over')
        status = res['status'] = '%s, %s' % (status, 'fought: ' + res['fight'])
    for pat, key in ((r'Diorama battle: 3D on (.*)', 'battle_3d'), (r'Diorama battle: (2D|refused|no world)(.*)', 'battle_2d'),
                     (r'Diorama battle: stage: (.*)', 'stage'), (r'Battle env test: starting (.*)', 'started'),
                     (r'Diorama battle: drawn by (.*)', 'renderer')):
        m = re.search(pat, log)
        if m:
            res[key] = m.group(0)
    # the game may resume somewhere else than the warp asked (a secret base: the field
    # comes back in Littleroot Town); a picture of another map is no picture of this one
    m = re.search(r'on map (\d+)\.(\d+) at', res.get('started') or '')
    if m and (int(m.group(1)), int(m.group(2))) != (rec['group'], rec['num']):
        res['status'] = status = 'wrong map %s.%s' % (m.group(1), m.group(2))
    if os.path.exists(os.path.join(rundir, 'cap.json')):
        try:
            res['capture'] = json.load(open(os.path.join(rundir, 'cap.json')))
        except ValueError:
            res['capture'] = None
    json.dump(res, open(os.path.join(rundir, 'result.json'), 'w'), indent=1)
    results.append(res)
    band = res.get('capture', {}) or {}
    band = band.get('band', {}) if isinstance(band, dict) else {}
    print('%-46s %-9s %-24s %5.1fs  void %.2f floor %.2f upright %.2f raised %.2f depth %.1f' % (
        name[:46], label, status, res['seconds'], band.get('void', -1), band.get('floor', -1),
        band.get('upright', -1), band.get('raised', -1), band.get('depth_median', -1)), flush=True)
    return res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--exe', required=True)
    ap.add_argument('--save', required=True)
    ap.add_argument('--out', default=os.path.join(ROOT, 'build/battle_env/survey'))
    ap.add_argument('--inventory', default=os.path.join(ROOT, 'build/battle_env/inventory.json'))
    ap.add_argument('--maps', default='')
    ap.add_argument('--category', default='')
    ap.add_argument('--kind', default='auto')
    ap.add_argument('--hour', type=int, default=12)
    ap.add_argument('--display', default=':99')
    ap.add_argument('--scale', type=int, default=3)
    ap.add_argument('--delay', type=int, default=150)
    ap.add_argument('--capture-delay', type=int, default=40)
    ap.add_argument('--timeout', type=float, default=150.0)
    ap.add_argument('--cfg-extra', default='')
    ap.add_argument('--env', action='append', default=[])
    ap.add_argument('--skip-done', action='store_true')
    ap.add_argument('--autopilot', action='store_true',
                    help='the game presses its own keys (PKMN_TEST_AUTOPILOT, held for frames, not '
                         'milliseconds): for sizes where it runs at a frame or two a second')
    ap.add_argument('--at', default='', help='X,Y: stand there instead of the map\'s test position '
                    '(with --maps; where two trainers see the test position)')
    ap.add_argument('--spec', default='', help='the battle to start instead of the map\'s own (a '
                    'PKMN_TEST_BATTLE spec; "none": start nothing, the map starts its own - two '
                    'trainers who see the player)')
    ap.add_argument('--label', default='', help='the run\'s name with --spec (default: the spec\'s kind)')
    ap.add_argument('--fight', type=int, default=0, help='seconds: after the picture, press A '
                    'through the battle and record how it ended (PKMN_TEST_FIGHT)')
    args = ap.parse_args()
    args.exe = os.path.abspath(args.exe)
    args.save = os.path.abspath(args.save)
    os.makedirs(args.out, exist_ok=True)

    inv = json.load(open(args.inventory))['maps']
    want = set('MAP_' + m if not m.startswith('MAP_') else m for m in args.maps.split(',') if m)
    cats = set(c for c in args.category.split(',') if c)
    species, trainers = read_constants()
    results_path = os.path.join(args.out, 'results.json')
    results = json.load(open(results_path)) if os.path.exists(results_path) else []
    for rec in inv:
        if want and rec['map'] not in want:
            continue
        if cats and rec['category'] not in cats:
            continue
        options = choose(rec, species, trainers, args.kind)
        if args.spec and options:
            # the map's test position, another battle
            options = [(args.label or args.spec.split(':')[0], '' if args.spec == 'none' else args.spec,
                        options[0][2])]
        for label, spec, p in options:
            if args.at:
                x, y = (int(v) for v in args.at.split(','))
                p = {'x': x, 'y': y, 'behaviour': '(--at)', 'environment': p.get('environment')}
            if args.skip_done and os.path.exists(os.path.join(args.out, '%s-%s' % (rec['map'][4:], label), 'cap.png')):
                continue
            results = [r for r in results if not (r['map'] == rec['map'] and r['label'] == label)]
            run_map(args, rec, label, spec, p, results)
            json.dump(results, open(results_path, 'w'), indent=1)
    print('wrote', results_path)


if __name__ == '__main__':
    main()
