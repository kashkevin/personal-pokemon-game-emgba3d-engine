#!/usr/bin/env python3
"""Battle Tower: Legends floor generator (docs/battle-tower-legends.md section 7 in the app repository).

    tools/tower_legends/generate.py --data FILE [--check] [--verify-trainerproc]

FILE is the native game's own data (`./pokeemerald-mp --dump-tower-data FILE`). The generator builds the 100 floors
(300 trainers: each floor's solo trainer, Duo A and Duo B) from the hand-written bosses (bosses.py) and the curated
pools (pools.py) with a fixed seed, validates every rule (validate.py), prints a summary and writes:

    src/data/tower_legends.party          the trainers, in the game's Showdown-style format (trainerproc)
    include/constants/tower_legends.h     the trainer ids, the floor count and kinds, TOWER_LEGENDS_TABLE_HASH
    src/data/tower_legends_floors.h       gTowerLegendsFloors: each floor's kind, trainers, music and texts
    tools/tower_legends/tower_legends.json    the same data and its hash, for the app and the tests
    tools/tower_legends/tower_floors_app.py   the app's emerald_multiplayer.tower_floors (FLOORS, prize pools)
    tools/tower_legends/FLOORS.md         every floor in readable form, the boss roster, the deviations

--check regenerates in memory and exits 1 when a written file differs (or a rule fails); nothing is written.
--verify-trainerproc also runs the C preprocessor and tools/trainerproc/trainerproc (as trainer_rules.mk does) on
the generated .party in a temporary directory and checks that every constant it writes is the intended one.
Exit status: 0 when every rule holds (and, with --check, nothing differs), 1 otherwise, 2 on bad input.
"""
import argparse
import hashlib
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import assemble  # noqa: E402
import bosses  # noqa: E402
import builder as builder_mod  # noqa: E402
import gamedata  # noqa: E402
import movedex  # noqa: E402
import pools  # noqa: E402
import typechart  # noqa: E402
import validate  # noqa: E402
import writers  # noqa: E402

ENGINE = os.path.normpath(os.path.join(HERE, "..", ".."))
OUTPUTS = {
    "party": "src/data/tower_legends.party",
    "constants": "include/constants/tower_legends.h",
    "floors_h": "src/data/tower_legends_floors.h",
    "json": "tools/tower_legends/tower_legends.json",
    "app": "tools/tower_legends/tower_floors_app.py",
    "md": "tools/tower_legends/FLOORS.md",
}


def pic_genders():
    out = {}
    for c in pools.CLASSES:
        for pic, gender in c.pics:
            out[pic] = "Male" if gender == "M" else "Female"
    for b in bosses.BOSSES.values():
        for t in (b["solo"], b["partner"]):
            out[t["pic"]] = t["gender"]
    return out


def fully_evolved(g, sid):
    """No evolution into another Gen 1-6 species (Ursaring's Ursaluna and the like are Gen 8+)."""
    for e in g.species[sid].evolutions:
        t = g.species.get(e["target"])
        if t is not None and t.id == t.dex and t.dex <= 721:
            return False
    return True


def prize_pools(g, b, nm):
    milestone, legendary = [], []
    for sid in sorted(g.species):
        s = g.species[sid]
        if s.dex > 721 or s.id != s.dex or not s.all_pics:
            continue
        if any(not g.species[f[1]].all_pics for f in g.battle_form_changes(sid) if f[1] in g.species):
            continue
        entry = (sid, s.const, nm.display_species(s.const), g.min_level(sid))
        f = s.flags
        if f["mythical"] or f["ultraBeast"] or f["paradox"]:
            continue
        if f["restricted"] or f["sub"]:
            legendary.append(entry)
        elif fully_evolved(g, sid):
            milestone.append(entry)
    return milestone, legendary


def build(dump_path):
    g = gamedata.Game(dump_path, ENGINE)
    md = movedex.Movedex(g)
    tc = typechart.TypeChart(ENGINE, g.type_values)
    b = builder_mod.Builder(g, md, tc)
    asm = assemble.Assembler(g, b)
    floors = asm.build()
    report = validate.validate(g, b, md, floors, pic_genders())
    fj = writers.floors_json(g, floors)
    h = writers.table_hash(fj)
    nm = writers.Names(g)
    milestone, legendary = prize_pools(g, b, nm)
    with open(dump_path, "rb") as f:
        dump_sha = hashlib.sha256(f.read()).hexdigest()
    texts = {
        "party": writers.party_text(g, floors, h),
        "constants": writers.constants_text(floors, h),
        "floors_h": writers.floors_h_text(floors, h),
        "json": writers.json_text(fj, h),
        "app": writers.app_module_text(fj, h, milestone, legendary),
        "md": writers.floors_md_text(g, floors, h, report, asm.notes, ""),
    }
    check_outputs(report, g, fj, h, texts)
    return g, floors, report, h, texts, dump_sha, (milestone, legendary)


def check_outputs(report, g, fj, h, texts):
    """The written forms agree with each other: the app module and the JSON give the same floors and hash."""
    ns = {}
    exec(compile(texts["app"], "tower_floors_app.py", "exec"), ns)
    report.check("app module FLOORS equal the JSON floors", list(ns["FLOORS"]) == fj, "tower_floors_app.py")
    report.check("app module TABLE_HASH is the hash", ns["TABLE_HASH"] == h, "tower_floors_app.py")
    report.check("hash recomputed from the app module", writers.table_hash(list(ns["FLOORS"])) == h, "")
    report.check("constants header carries the hash", ("0x%08xu" % h) in texts["constants"], "")
    ids = re.findall(r"^#define (TOWER_TRAINER_F\d{3}(?:_DUO_[AB])?)\s+(\d+)$", texts["constants"], re.M)
    report.check("300 trainer index constants 0..299 in order",
                 [int(v) for _, v in ids] == list(range(300)) and len(ids) == 300, str(len(ids)))
    party_ids = re.findall(r"^=== (TOWER_TRAINER_F\d{3}(?:_DUO_[AB])?) ===$", texts["party"], re.M)
    report.check(".party trainers in the constants' order", party_ids == [k for k, _ in ids], "")
    report.check("milestone prize pool not empty", "MILESTONE_SPECIES = (\n    (" in texts["app"], "")


def run_trainerproc(party_text, report, g, floors):
    """cpp + trainerproc on the .party (as trainer_rules.mk), then every emitted constant checked."""
    tp = os.path.join(ENGINE, "tools", "trainerproc", "trainerproc")
    if not os.path.exists(tp):
        report.check("trainerproc available", False, tp)
        return None
    cpp = "arm-none-eabi-cpp"
    flags = ["-iquote", os.path.join(ENGINE, "include"), "-Wno-trigraphs", "-DMODERN=1", "-DTESTING=0", "-DEMERALD",
             "-std=gnu17", "-DMP_LOCAL_RESET_DEVELOPMENT=0", "-DMP_LOCAL_RESET_STATIC=1",
             "-DMP_LOCAL_RESET_NEW_MAUVILLE_PILOT=0", "-traditional-cpp", "-"]
    with tempfile.TemporaryDirectory() as tmp:
        src = os.path.join(tmp, "tower_legends.party")
        out = os.path.join(tmp, "tower_legends.h")
        with open(src, "w", encoding="utf-8") as f:
            f.write(party_text)
        pre = subprocess.run([cpp] + flags, input=party_text.encode(), capture_output=True)
        if pre.returncode:
            report.check("C preprocessor accepts the .party", False, pre.stderr.decode()[:500])
            return None
        res = subprocess.run([tp, "-o", out, "-i", "src/data/tower_legends.party", "-"], input=pre.stdout,
                             capture_output=True)
        ok = res.returncode == 0 and not res.stderr
        report.check("trainerproc converts the .party without an error", ok, res.stderr.decode()[:2000])
        if res.returncode:
            return None
        with open(out, encoding="utf-8") as f:
            header = f.read()
    verify_header(header, report, g, floors)
    return header


def verify_header(header, report, g, floors):
    blocks = re.split(r"\n(?=#line \d+\n    \[DIFFICULTY_)", header)
    parsed = {}
    for blk in blocks:
        m = re.search(r"\[DIFFICULTY_NORMAL\]\[(TOWER_TRAINER_F\d{3}(?:_DUO_[AB])?)\]", blk)
        if not m:
            continue
        parsed[m.group(1)] = blk
    trainers = [t for f in floors for t in (f["solo"], f["duoA"], f["duoB"])]
    report.check("trainerproc wrote all 300 trainers", len(parsed) == 300, str(len(parsed)))
    for t in trainers:
        blk = parsed.get(t.id, "")
        w = "trainerproc " + t.id

        def field(name):
            mm = re.search(r"\." + name + r" = ([^,\n]+),", blk)
            return mm.group(1).strip() if mm else None
        report.check("trainerproc: name", field("trainerName") == '_("%s")' % t.name, w)
        report.check("trainerproc: class", field("trainerClass") == t.trainer_class, w)
        report.check("trainerproc: picture", field("trainerPic") == t.pic, w)
        report.check("trainerproc: gender", field("gender") == "TRAINER_GENDER_" + t.gender.upper(), w)
        report.check("trainerproc: encounter music", field("encounterMusic") == t.music, w)
        report.check("trainerproc: singles", field("battleType") == "TRAINER_BATTLE_TYPE_SINGLES", w)
        report.check("trainerproc: AI flags", field("aiFlags") == " | ".join(t.ai), w)
        report.check("trainerproc: party size", field("partySize") == str(len(t.mons)), w)
        report.check("trainerproc: no bag items", ".items = " not in blk, w)
        mons = re.findall(r"(?<=\n)            \{\n(.*?)\n            \},(?=\n            \{\n|\n        \},)", blk,
                          re.S)
        report.check("trainerproc: Pokemon count", len(mons) == len(t.mons), w)
        for mon, mb in zip(t.mons, mons):
            def mfield(name):
                mm = re.search(r"\." + name + r" = ([^\n]+?),\n", mb)
                return mm.group(1).strip() if mm else None
            mw = "%s %s" % (w, mon.species)
            report.check("trainerproc: species", mfield("species") == mon.species, mw)
            report.check("trainerproc: held item", mfield("heldItem") == (mon.item or None), mw)
            report.check("trainerproc: ability", mfield("ability") == mon.ability, mw)
            report.check("trainerproc: level", mfield("lvl") == str(mon.level), mw)
            report.check("trainerproc: nature", mfield("nature") == mon.nature, mw)
            iv = mfield("iv")
            want_iv = "TRAINER_PARTY_IVS(%d, %d, %d, %d, %d, %d)" % (mon.ivs[0], mon.ivs[1], mon.ivs[2], mon.ivs[5],
                                                                  mon.ivs[3], mon.ivs[4])
            report.check("trainerproc: IVs", iv == want_iv, "%s %s %s" % (mw, iv, want_iv))
            ev = mfield("ev")
            if any(mon.evs):
                want_ev = "TRAINER_PARTY_EVS(%d, %d, %d, %d, %d, %d)" % (mon.evs[0], mon.evs[1], mon.evs[2],
                                                                      mon.evs[5], mon.evs[3], mon.evs[4])
                report.check("trainerproc: EVs", ev == want_ev, "%s %s %s" % (mw, ev, want_ev))
            else:
                report.check("trainerproc: EVs", ev is None, mw)
            mv = re.search(r"\.moves = \{\n#line \d+\n(.*?)\n            \},", mb, re.S)
            got = re.findall(r"(MOVE_[A-Z0-9_]+),", mv.group(1)) if mv else []
            report.check("trainerproc: moves", got == list(mon.moves), "%s %s %s" % (mw, got, mon.moves))


def summary(report, h, texts, dump_sha, pools_, quiet=False):
    lines = ["Battle Tower: Legends: 100 floors, 300 trainers, table hash 0x%08x (dump sha256 %s...)"
             % (h, dump_sha[:16]),
             "%d rule checks over %d rules, %d violations" % (sum(report.counts.values()), len(report.counts),
                                                             len(report.errors))]
    width = max(len(k) for k in report.counts)
    for k, v in report.counts.items():
        failed = sum(1 for e in report.errors if e.startswith(k + ":"))
        if failed or not quiet:
            lines.append("  %-*s %6d checked%s" % (width, k, v, ("   %d FAILED" % failed) if failed else ""))
    if not quiet:
        lines.append("Megas: %s" % report.facts.get("megas"))
        lines.append("Prize pools: %d milestone species, %d legendaries" % (len(pools_[0]), len(pools_[1])))
    if report.errors:
        lines.append("VIOLATIONS:")
        lines += ["  " + e for e in report.errors[:200]]
        if len(report.errors) > 200:
            lines.append("  ... and %d more" % (len(report.errors) - 200))
    return "\n".join(lines)


def write_atomic(path, text):
    """Write the file through a temporary file; an unchanged file is left alone (make sees no new timestamp)."""
    try:
        with open(path, encoding="utf-8", newline="") as f:
            if f.read() == text:
                return False
    except FileNotFoundError:
        pass
    tmp = path + ".tmp-tower"
    with open(tmp, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    os.replace(tmp, path)
    return True


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--data", required=True, help="the game's dump (pokeemerald-mp --dump-tower-data FILE)")
    ap.add_argument("--check", action="store_true", help="compare with the written files; write nothing")
    ap.add_argument("--verify-trainerproc", action="store_true",
                    help="run cpp + trainerproc on the .party in a temporary directory and check its output")
    ap.add_argument("--quiet", action="store_true", help="print only the totals and the violations")
    args = ap.parse_args(argv)
    if not os.path.exists(args.data):
        print("generate.py: no such dump: %s" % args.data, file=sys.stderr)
        return 2
    try:
        g, floors, report, h, texts, dump_sha, pools_ = build(args.data)
    except gamedata.DataError as e:
        print("generate.py: the dump does not match this engine: %s" % e, file=sys.stderr)
        return 2
    if args.verify_trainerproc:
        run_trainerproc(texts["party"], report, g, floors)
    print(summary(report, h, texts, dump_sha, pools_, quiet=args.quiet))
    status = 1 if report.errors else 0
    if args.check:
        stale = []
        for key, rel in OUTPUTS.items():
            path = os.path.join(ENGINE, rel)
            try:
                with open(path, encoding="utf-8", newline="") as f:
                    current = f.read()
            except FileNotFoundError:
                current = None
            if current != texts[key]:
                stale.append(rel)
        for rel in stale:
            print("generate.py --check: %s differs from the generator's output" % rel)
        if stale:
            status = 1
        if not stale:
            print("generate.py --check: every output is up to date")
        return status
    if report.errors:
        print("generate.py: nothing written (rule violations above)", file=sys.stderr)
        return 1
    written = [rel for key, rel in OUTPUTS.items() if write_atomic(os.path.join(ENGINE, rel), texts[key])]
    print("written: " + (", ".join(written) if written else "nothing (every output was already up to date)"))
    return status


if __name__ == "__main__":
    sys.exit(main())
