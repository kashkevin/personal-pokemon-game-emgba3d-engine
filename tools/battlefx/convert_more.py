#!/usr/bin/env python3
"""Converts this game's battle animation scripts that the Battle FX package has no data for (the moves past its 621,
numbers 622 and up) into the Battle FX data format, and adds them to a copy of battlefx/bfx_moves.json.

    python3 tools/battlefx/convert_more.py --base battlefx/bfx_moves.json --out OUT.json [--moves MOVE_A,MOVE_B | --from 622]
    python3 tools/battlefx/convert_more.py --check [--moves ...]    re-converts moves the package has and compares

The package's own converter (battlefx_kit/prototype/build_preview_data.py) is not in this repository, so this is a
converter of its own that writes the same rows (bfx_moves.json meta.events) from the same sources, read only:
data/battle_anim_scripts.s (and its macros, asm/macros/battle_anim_script.inc), the sprite templates, OAM, animation
and affine tables of src/*.c, src/data/battle_anim.h (the graphics and palette of each tag, the backgrounds) and
src/data/moves_info.h. --check converts moves the package already has and compares them row by row (template names,
not indices; the time estimates of syncs and the end row aside), so the reading of the scripts is the package's.

The time model is the runtime's (bfx_core.js Player.runScript): commands at the same script time run in the same frame;
`delay N` moves the script N + 2 frames on (battle_anim.c Cmd_delay and WaitAnimFrameCount: N frames counted down, one
to see the count at zero, the commands the frame after); a sync (waitforvisualfinish, waitbgfadeout / in, waitsound)
waits for its condition in the runtime, which then counts the rows after it from the frame it was met (their times
are relative to the sync's time plus its estimate); `monbg`'s own frames are the runtime's (it shifts by 3). A branch on
a task's result (jumpargeq / jumpreteq after a createvisualtask, jumpifdoublebattle, choosetwoturnanim,
jumpifmoveturn) gives the move its variants as the package's data has them ({kind: 'turn' | 'arg', task, arg,
value}); `jumpifcontest` and AnimTask_IsContest are never taken in a battle.
"""
import argparse
import collections
import copy
import json
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
HEADERS = ["config/general.h", "config/battle.h", "constants/pokemon.h", "constants/items.h", "constants/moves.h",
           "constants/battle.h", "constants/battle_anim.h", "constants/rgb.h",
           "constants/songs.h", "constants/sound.h", "gba/io_reg.h"]
BATTLER_NAMES = {0: "attacker", 1: "target", 2: "attacker_partner", 3: "target_partner", 4: "player_left",
                 5: "opponent_left", 6: "player_right", 7: "opponent_right", 8: "attacker_force"}
# commands (asm/macros/battle_anim_script.inc: the macros that write an opcode); everything else is a macro expanded
PRIMS = {"unloadspritegfx", "unloadspritepal", "createsprite", "createvisualtask", "delay", "waitforvisualfinish", "nop",
         "nop2", "end", "playse", "monbg", "clearmonbg", "setalpha", "blendoff", "call", "return", "setarg",
         "choosetwoturnanim", "jumpifmoveturn", "goto", "fadetobg", "restorebg", "waitbgfadeout", "waitbgfadein",
         "changebg", "playsewithpan", "setpan", "panse", "loopsewithpan", "waitplaysewithpan", "setbldcnt",
         "createsoundtask", "waitsound", "jumpargeq", "monbg_static", "clearmonbg_static", "jumpifcontest",
         "fadetobgfromset", "panse_adjustnone", "panse_adjustall", "splitbgprio", "splitbgprio_all", "splitbgprio_foes",
         "invisible", "visible", "teamattack_moveback", "teamattack_movefwd", "stopsound", "createvisualtaskontargets",
         "createspriteontargets", "createspriteontargets_onpos", "jumpifmovetypeequal", "createdragondartsprite",
         "unloadallspritepals"}
# tasks that only answer a script's question (bfx_core.js LOGIC_TASKS and the game's BranchValue)
CONTEST_TASKS = {"AnimTask_IsContest"}
ARG_RET_ID = 7


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), text, flags=re.S)
    out = []
    for line in text.split("\n"):
        line = re.sub(r"//.*$", "", line)
        out.append(line)
    return "\n".join(out)


# ------------------------------------------------------------------ constants
class Consts:
    """#define and enum values of the headers the scripts include (cpp -dM; the enums compiled and printed)."""

    def __init__(self, root, idents):
        self.root = root
        inc = "".join('#include "%s"\n' % h for h in HEADERS)
        args = ["gcc", "-E", "-DMODERN=1", "-iquote", os.path.join(root, "include"), "-I", os.path.join(root, "include"),
                "-I", root, "-x", "c", "-"]
        dm = subprocess.run(args[:2] + ["-dM"] + args[2:], input=inc, capture_output=True, text=True, check=True).stdout
        self.fmacros = {}
        defines = {}
        for line in dm.splitlines():
            m = re.match(r"#define (\w+)\(([^)]*)\)\s*(.*)$", line)
            if m:
                self.fmacros[m.group(1)] = ([p.strip() for p in m.group(2).split(",") if p.strip()], m.group(3))
                continue
            m = re.match(r"#define (\w+)(?:\s+(.*))?$", line)
            if m:
                defines[m.group(1)] = m.group(2) or ""
        pre = subprocess.run(args + ["-P"], input=inc, capture_output=True, text=True, check=True).stdout
        enums = set()
        for body in re.findall(r"\benum\b[^{;]*\{([^}]*)\}", pre):
            for part in body.split(","):
                m = re.match(r"\s*([A-Za-z_]\w*)", part)
                if m:
                    enums.add(m.group(1))
        want = sorted((set(idents) & (set(defines) | enums)) - set(self.fmacros))
        self.val = {"TRUE": 1, "FALSE": 0}   # gba/defines.h; the assembler's (constants/global.inc)
        self._compile(inc, want)

    def _compile(self, inc, names):
        names = list(names)
        for _ in range(8):
            if not names:
                return
            prog = inc + "#include <stdio.h>\nint main(void)\n{\n"
            for n in names:
                prog += '    printf("%%s %%lld\\n", "%s", (long long)(%s));\n' % (n, n)
            prog += "    return 0;\n}\n"
            with tempfile.TemporaryDirectory() as tmp:
                src = os.path.join(tmp, "c.c")
                exe = os.path.join(tmp, "c")
                with open(src, "w") as f:
                    f.write(prog)
                r = subprocess.run(["gcc", "-w", "-DMODERN=1", "-iquote", os.path.join(self.root, "include"), "-I",
                                    os.path.join(self.root, "include"), "-I", self.root, src, "-o", exe],
                                   capture_output=True, text=True)
                if r.returncode != 0:
                    bad = set()
                    for line in r.stderr.splitlines():
                        m = re.search(r"c\.c:(\d+):", line)
                        if m:
                            ln = int(m.group(1))
                            text = prog.split("\n")[ln - 1]
                            m2 = re.search(r'"(\w+)"', text)
                            if m2:
                                bad.add(m2.group(1))
                    if not bad:
                        raise SystemExit("convert_more.py: the constants program does not compile:\n" + r.stderr[:2000])
                    names = [n for n in names if n not in bad]
                    continue
                out = subprocess.run([exe], capture_output=True, text=True, check=True).stdout
            for line in out.splitlines():
                n, v = line.split()
                self.val[n] = int(v)
            return

    def expand(self, expr, depth=0):
        """Function-like macros (RGB(...)) expanded as cpp does."""
        if depth > 20:
            return expr
        for name, (params, body) in self.fmacros.items():
            pos = 0
            while True:
                m = re.search(r"\b%s\s*\(" % re.escape(name), expr[pos:])
                if not m:
                    break
                start = pos + m.start()
                i = pos + m.end()
                level, argsl, cur = 1, [], ""
                while i < len(expr) and level:
                    ch = expr[i]
                    if ch == "(":
                        level += 1
                    elif ch == ")":
                        level -= 1
                        if level == 0:
                            break
                    if ch == "," and level == 1:
                        argsl.append(cur)
                        cur = ""
                    else:
                        cur += ch
                    i += 1
                argsl.append(cur)
                sub = body
                for p, a in zip(params, argsl):
                    sub = re.sub(r"\b%s\b" % re.escape(p), "(" + a.strip() + ")", sub)
                expr = expr[:start] + "(" + sub + ")" + expr[i + 1:]
                pos = start
        return expr

    def ensure(self, names):
        """Adds the values of more #define / enum names (the moves' data: GEN_CHAMPIONS, B_UPDATED_MOVE_DATA ...)."""
        want = sorted(set(names) - set(self.val) - set(self.fmacros))
        if want:
            self._compile("".join('#include "%s"\n' % h for h in HEADERS), want)

    def eval(self, expr):
        """An integer expression of the scripts (C operators, one `?:`); None when a name is not a constant."""
        e = str(expr).strip()
        m = re.match(r"^(.*?)\?(.*?):(.*)$", e)
        if m and "?" not in m.group(2) and "?" not in m.group(3):
            c = self.eval(m.group(1))
            return None if c is None else self.eval(m.group(2) if c else m.group(3))
        e = self.expand(e)
        names = set(re.findall(r"\b[A-Za-z_]\w*\b", e))
        for n in sorted(names, key=len, reverse=True):
            if n not in self.val:
                return None
            e = re.sub(r"\b%s\b" % n, "(%d)" % self.val[n], e)
        e = re.sub(r"\b0x([0-9a-fA-F]+)[uUlL]*\b", lambda m: str(int(m.group(1), 16)), e)
        e = re.sub(r"\b(\d+)[uUlL]+\b", r"\1", e)
        e = e.replace("&&", " and ").replace("||", " or ")
        e = re.sub(r"!(?!=)", " not ", e)
        e = e.replace("/", "//")
        try:
            return int(eval(e, {"__builtins__": {}}, {}))
        except Exception:
            return None


# ------------------------------------------------------------------ macros and scripts
def split_args(text):
    """GNU as macro arguments: commas (outside parentheses), or blanks between two operands ("SOUND_PAN_TARGET 10 2")."""
    parts, cur, level = [], "", 0
    for ch in text:
        if ch == "(":
            level += 1
        elif ch == ")":
            level -= 1
        if ch == "," and level == 0:
            parts.append(cur)
            cur = ""
        else:
            cur += ch
    parts.append(cur)
    out = []
    for p in parts:
        p = p.strip()
        if re.match(r"^\w+\s*=(?!=)", p):
            out.append(p)
            continue
        toks = re.findall(r"\s+|[A-Za-z_\\][\w\\@]*|0x[0-9a-fA-F]+|\d+|<<|>>|==|!=|<=|>=|&&|\|\||.", p)
        pieces, cur, last, gap, level = [], "", None, False, 0
        for t in toks:
            if t.isspace():
                gap = True
                continue
            ends = last is not None and bool(re.match(r"[A-Za-z_\\0-9)]", last))
            starts = bool(re.match(r"[A-Za-z_\\0-9(]", t))
            if gap and level == 0 and ends and starts:
                pieces.append(cur)
                cur = ""
            elif gap and cur:
                cur += " "
            if t == "(":
                level += 1
            elif t == ")":
                level -= 1
            cur += t
            last, gap = t, False
        pieces.append(cur)
        out.extend(x.strip() for x in pieces)
    if len(out) == 1 and out[0] == "":
        return []
    return out


class Macro:
    def __init__(self, name, params, body):
        self.name = name
        self.params = params  # [(name, required, default, vararg)]
        self.body = body


def parse_macros(text):
    macros = {}
    lines = strip_comments(text).split("\n")
    i = 0
    while i < len(lines):
        m = re.match(r"\s*\.macro\s+(\w+)\s*(.*)$", lines[i])
        if not m:
            i += 1
            continue
        name, ptext = m.group(1), m.group(2)
        params = []
        for p in [x.strip() for x in re.split(r"[,\s]+(?=\w+(?::|=|,|$))", ptext) if x.strip()]:
            p = p.strip().rstrip(",")
            if not p:
                continue
            mm = re.match(r"(\w+)(?::(req|vararg))?(?:\s*=\s*(.*))?$", p)
            if mm:
                params.append((mm.group(1), mm.group(2) == "req", mm.group(3), mm.group(2) == "vararg"))
        body = []
        i += 1
        while i < len(lines) and not re.match(r"\s*\.endm\b", lines[i]):
            body.append(re.sub(r"@.*$", "", lines[i]))
            i += 1
        macros[name.lower()] = Macro(name, params, body)
        i += 1
    return macros


class Stmt:
    __slots__ = ("op", "args", "line", "kw")

    def __init__(self, op, args, line, kw=None):
        self.op, self.args, self.line, self.kw = op, args, line, kw or {}

    def __repr__(self):
        return "%s %s" % (self.op, ", ".join(self.args))


class Scripts:
    def __init__(self, root):
        self.root = root
        mtext = read(os.path.join(root, "asm/macros/battle_anim_script.inc"))
        self.macros = parse_macros(mtext)
        self.raw = read(os.path.join(root, "data/battle_anim_scripts.s"))
        idents = set(re.findall(r"\b[A-Z_][A-Z0-9_]*\b", self.raw)) | set(re.findall(r"\b[A-Z_][A-Z0-9_]*\b", mtext))
        self.consts = Consts(root, idents)
        self.stmts = []        # flat list of primitives
        self.labels = {}       # label -> index in stmts
        self._parse()

    def _expand(self, op, argtext, line, depth=0):
        op = op.lower()  # GNU as: macro names are not case sensitive (`Call CreatePinkHearts`)
        if op in PRIMS:
            return [Stmt(op, split_args(argtext) if argtext.strip() else [], line)]
        mac = self.macros.get(op)
        if mac is None:
            raise SystemExit("convert_more.py: line %d: unknown command %s" % (line, op))
        given = split_args(argtext) if argtext.strip() else []
        vals, pos = {}, 0
        names = [p[0] for p in mac.params]
        for g in given:
            m = re.match(r"^(\w+)\s*=(?!=)\s*(.*)$", g)
            if m and m.group(1) in names:
                vals[m.group(1)] = m.group(2).strip()
                continue
            while pos < len(mac.params) and mac.params[pos][0] in vals:
                pos += 1
            if pos < len(mac.params) and mac.params[pos][3]:
                vals[mac.params[pos][0]] = ", ".join(x for x in given[given.index(g):])
                break
            if pos < len(mac.params):
                vals[mac.params[pos][0]] = g
                pos += 1
        for (n, req, default, vararg) in mac.params:
            if n not in vals:
                if req:
                    raise SystemExit("convert_more.py: line %d: %s needs %s" % (line, op, n))
                vals[n] = default.strip() if default else ""
        out = []
        stack = [True]
        for b in mac.body:
            s = b
            for n in sorted(vals, key=len, reverse=True):
                s = s.replace("\\" + n, vals[n])
            s = s.strip()
            if not s:
                continue
            m = re.match(r"\.(if|ifb|ifnb|ifc|else|endif|error|elseif)\b\s*(.*)$", s)
            if m:
                d, rest = m.group(1), m.group(2)
                if d == "if":
                    v = self.consts.eval(rest)
                    stack.append(all(stack) and bool(v))
                elif d == "ifb":
                    stack.append(all(stack) and not rest.strip())
                elif d == "ifnb":
                    stack.append(all(stack) and bool(rest.strip()))
                elif d == "else":
                    cur = stack.pop()
                    stack.append(all(stack) and not cur)
                elif d == "endif":
                    stack.pop()
                elif d == "error":
                    if all(stack):
                        raise SystemExit("convert_more.py: line %d: %s: %s" % (line, op, rest))
                continue
            if not all(stack):
                continue
            if s.startswith("."):
                continue  # .byte / .2byte: only in commands, never reached
            mm = re.match(r"(\w+)\s*(.*)$", s)
            out.extend(self._expand(mm.group(1), mm.group(2), line, depth + 1))
        return out

    def _parse(self):
        text = strip_comments(self.raw)
        stack = [True]
        for ln, raw in enumerate(text.split("\n"), 1):
            line = re.sub(r"@.*$", "", raw).strip()
            if not line or line.startswith("#"):
                continue
            m = re.match(r"\.(if|else|endif|elseif)\b\s*(.*)$", line)
            if m:
                d = m.group(1)
                if d == "if":
                    stack.append(all(stack) and bool(self.consts.eval(m.group(2))))
                elif d == "else":
                    cur = stack.pop()
                    stack.append(all(stack) and not cur)
                elif d == "endif":
                    stack.pop()
                continue
            if not all(stack):
                continue
            for stmt in [x.strip() for x in line.split(";") if x.strip()]:
                m = re.match(r"^([A-Za-z_][\w.]*)::?\s*(.*)$", stmt)
                if m:
                    self.labels[m.group(1)] = len(self.stmts)
                    stmt = m.group(2).strip()
                    if not stmt:
                        continue
                if stmt.startswith("."):
                    continue
                m = re.match(r"(\w+)\s*(.*)$", stmt)
                self.stmts.extend(self._expand(m.group(1), m.group(2), ln))

    def num(self, a):
        v = self.consts.eval(a)
        if v is None:
            raise SystemExit("convert_more.py: cannot evaluate %r" % a)
        return v


# ------------------------------------------------------------------ the script, frame by frame (rows of meta.events)
class Path:
    def __init__(self):
        self.pc = 0
        self.t = 0.0
        self.calls = []
        self.argsrc = {}     # gBattleAnimArgs index -> ("const", v) | ("task", name)
        self.conds = []      # (kind, task, arg, value, label)
        self.rows = []
        self.pending = []    # estimated ends of the visuals since the last sync
        self.sounds = []
        self.ended = False
        self.steps = 0

    def fork(self):
        p = copy.copy(self)
        p.calls = list(self.calls)
        p.argsrc = dict(self.argsrc)
        p.conds = list(self.conds)
        p.rows = list(self.rows)
        p.pending = list(self.pending)
        p.sounds = list(self.sounds)
        return p


class Sim:
    """Walks a script from its label as battle_anim.c runs it; returns its paths (one per outcome of its branches)."""
    SPRITE_EST = 30.0
    TASK_EST = 20.0

    def __init__(self, scripts, templates):
        self.s = scripts
        self.tpl = templates     # Templates: name -> index (adds the ones the base data lacks)
        self.unknown = set()

    def num(self, a):
        return self.s.num(a)

    def battler_row(self, a):
        v = self.num(a)
        return BATTLER_NAMES.get(v)

    def sprite_args(self, args):
        # as the package's data has them (65535, not -1); `createvisualtask X, 2,` has no arguments (an empty .2byte)
        while args and not args[-1].strip():
            args = args[:-1]
        return [self.num(a) for a in args]

    def battler_code(self, a):
        """createsprite's battler: ANIM_BATTLER (Tera Starstorm) is no constant at all; the macro's `.if` then is not
        ANIM_TARGET, so the sprite is the attacker's, as the assembler builds it."""
        v = self.s.consts.eval(a)
        return 0 if v is None else v

    def run(self, label, turn_default=1):
        start = self.s.labels[label]
        p = Path()
        p.pc = start
        done, todo = [], [p]
        while todo:
            p = todo.pop()
            while not p.ended:
                p.steps += 1
                if p.steps > 20000:
                    raise SystemExit("convert_more.py: %s does not end" % label)
                st = self.s.stmts[p.pc]
                p.pc += 1
                fork = self.step(p, st)
                if fork:
                    todo.extend(fork)
            done.append(p)
        return done

    def setargs(self, p, values):
        for i, v in enumerate(values[:8]):
            p.argsrc[i] = ("const", v)

    def step(self, p, st):
        op, a, t = st.op, st.args, round(p.t, 6)
        if op in ("createsprite", "createspriteontargets", "createspriteontargets_onpos"):
            name = a[0]
            idx = self.tpl.index(name)
            battler = self.battler_code(a[1])   # the runtime takes 1 (ANIM_TARGET) as the target, any other as the attacker
            sub = self.num(a[2])
            if op == "createsprite":
                argv = self.sprite_args(a[3:])
                row = [t, 0, idx, battler, sub, argv, self.SPRITE_EST]
            else:
                bidx = self.num(a[3])
                argv = self.sprite_args(a[4:])
                row = [t, 0, idx, battler, sub, argv, self.SPRITE_EST, 2 if op == "createspriteontargets" else 1, bidx]
            self.setargs(p, argv)
            p.rows.append(row)
            p.pending.append(p.t + self.SPRITE_EST)
        elif op == "createdragondartsprite":
            raise SystemExit("convert_more.py: createdragondartsprite is not converted")
        elif op in ("createvisualtask", "createvisualtaskontargets"):
            name = a[0]
            prio = self.num(a[1])
            if op == "createvisualtask":
                argv = self.sprite_args(a[2:])
                row = [t, 1, name, argv, self.TASK_EST, 0, 0, prio]
            else:
                bidx = self.num(a[2])
                argv = self.sprite_args(a[3:])
                row = [t, 1, name, argv, self.TASK_EST, 1, bidx, prio]
            self.setargs(p, argv)
            p.argsrc[ARG_RET_ID] = ("task", name)
            p.rows.append(row)
            p.pending.append(p.t + self.TASK_EST)
        elif op == "delay":
            p.t += self.num(a[0]) + 2
        elif op in ("waitforvisualfinish", "waitbgfadeout", "waitbgfadein", "waitsound"):
            if op == "waitforvisualfinish":
                est = max([0.0] + [e - p.t for e in p.pending])
                p.pending = []
            elif op == "waitsound":
                est = max([0.0] + [e - p.t for e in p.sounds])
                p.sounds = []
            else:
                est = 24.0
            p.rows.append([t, 5, op, float(est)])
            p.t += est
        elif op == "end":
            p.rows.append([t, 6])
            p.ended = True
        elif op == "playse":
            p.rows.append([t, 2, "playse", a[0], None, None])
        elif op == "playsewithpan":
            p.rows.append([t, 2, "playsewithpan", a[0], a[1], None])
        elif op == "setpan":
            p.rows.append([t, 2, "setpan", None, a[0], None])
        elif op in ("panse", "panse_adjustnone", "panse_adjustall"):
            p.rows.append([t, 2, op, a[0], None, {"pan_from": a[1], "pan_to": a[2], "pan_step": a[3], "pan_delay": a[4]}])
        elif op == "loopsewithpan":
            every, times = self.num(a[2]), self.num(a[3])
            p.rows.append([t, 2, "loopsewithpan", a[0], a[1], {"repeat": {"every": every, "times": times}}])
            p.sounds.append(p.t + every * times)
        elif op == "waitplaysewithpan":
            p.rows.append([t, 2, "waitplaysewithpan", a[0], a[1], {"delay": self.num(a[2])}])
            p.sounds.append(p.t + self.num(a[2]))
        elif op == "createsoundtask":
            p.rows.append([t, 2, "createsoundtask", None, None, {"task": a[0], "args": list(a[1:])}])
            p.sounds.append(p.t + 30)
        elif op == "stopsound":
            p.rows.append([t, 2, "stopsound", None, None, None])
        elif op in ("monbg", "clearmonbg", "monbg_static", "clearmonbg_static", "splitbgprio", "splitbgprio_foes",
                    "invisible", "visible", "teamattack_moveback", "teamattack_movefwd"):
            p.rows.append([t, 4, op, [a[0]], self.battler_row(a[0])])
        elif op == "setalpha":
            p.rows.append([t, 4, "setalpha", [a[0], a[1]], None])
        elif op in ("blendoff", "splitbgprio_all"):
            p.rows.append([t, 4, op, [], None])
        elif op == "setbldcnt":
            p.rows.append([t, 4, "setbldcnt", [a[0]], None])
        elif op in ("fadetobg", "changebg"):
            p.rows.append([t, 3, op, [a[0]]])
        elif op == "restorebg":
            p.rows.append([t, 3, "restorebg", []])
        elif op == "fadetobgfromset":
            p.rows.append([t, 3, "fadetobgfromset", [a[0], a[1], a[2]]])
        elif op == "setarg":
            i, v = self.num(a[0]), self.num(a[1])
            p.rows.append([t, 7, i, v])
            p.argsrc[i] = ("const", v)
        elif op == "call":
            p.calls.append(p.pc)
            p.pc = self.s.labels[a[0]]
        elif op == "return":
            p.pc = p.calls.pop()
        elif op == "goto":
            p.pc = self.s.labels[a[0]]
        elif op == "jumpifcontest":
            pass  # never in a battle
        elif op == "choosetwoturnanim":
            q = p.fork()
            p.conds.append(("turn", None, None, 0, "Turn 0", a[0]))
            p.pc = self.s.labels[a[0]]
            q.conds.append(("turn", None, None, 1, "Turn 1", a[1]))
            q.pc = self.s.labels[a[1]]
            return [q]
        elif op == "jumpifmoveturn":
            v = self.num(a[0])
            known = [c for c in p.conds if c[0] == "turn"]
            eq = [c for c in known if not isinstance(c[3], tuple)]
            if eq:
                if eq[0][3] == v:
                    p.pc = self.s.labels[a[1]]
                return None
            excl = set()
            for c in known:
                excl |= set(c[3][1])
            if v in excl:
                return None
            q = p.fork()
            p.conds = [c for c in p.conds if c[0] != "turn"] + [("turn", None, None, v, "Turn %d" % v, a[1])]
            p.pc = self.s.labels[a[1]]
            q.conds = [c for c in q.conds if c[0] != "turn"] + [("turn", None, None, ("not", tuple(sorted(excl | {v}))), None, None)]
            return [q]
        elif op == "jumpargeq":
            i, v = self.num(a[0]), self.num(a[1])
            src = p.argsrc.get(i, ("const", 0))
            if src[0] == "const":
                if src[1] == v:
                    p.pc = self.s.labels[a[2]]
                return None
            task = src[1]
            if task in CONTEST_TASKS:
                if v == 0:
                    p.pc = self.s.labels[a[2]]
                return None
            # a decision on the task's answer, kept consistent along the path
            known = [c for c in p.conds if c[1] == task and c[2] == i]
            eq = [c for c in known if not isinstance(c[3], tuple)]
            if eq:
                if eq[0][3] == v:
                    p.pc = self.s.labels[a[2]]
                return None
            if any(isinstance(c[3], tuple) and v in c[3][1] for c in known):
                return None
            q = p.fork()
            excl = set()
            for c in known:
                excl |= set(c[3][1])
            p.conds = [c for c in p.conds if not (c[1] == task and c[2] == i)] + [("arg", task, i, v, None, a[2])]
            p.pc = self.s.labels[a[2]]
            q.conds = [c for c in q.conds if not (c[1] == task and c[2] == i)]
            q.conds.append(("arg", task, i, ("not", tuple(sorted(excl | {v}))), None, None))
            return [q]
        elif op == "jumpifmovetypeequal":
            raise SystemExit("convert_more.py: jumpifmovetypeequal is not converted")
        elif op in ("unloadspritegfx", "unloadspritepal", "unloadallspritepals", "nop", "nop2"):
            pass
        else:
            raise SystemExit("convert_more.py: %s is not converted" % op)
        return None


# the answer each branching task gives in the battle the default timeline is for: the player's left battler against the
# opponent's left, a single battle, the second turn (a two-turn move's strike), no weather (the runtime's own default)
DEFAULT_ANSWER = {"AnimTask_IsTargetPlayerSide": 0, "AnimTask_IsAttackerPlayerSide": 1, "AnimTask_GetAttackerSide": 0,
                  "AnimTask_GetTargetSide": 1, "AnimTask_IsDoubleBattle": 0, "AnimTask_IsTargetSameSide": 0,
                  "AnimTask_GetTargetIsAttackerPartner": 0, "AnimTask_IsHealingMove": 0, "AnimTask_IsPowerOver99": 0,
                  "AnimTask_GetWeather": 0}
DOMAIN = {"AnimTask_GetWeather": list(range(7)), "AnimTask_GetReturnPowerLevel": list(range(4)),
          "AnimTask_GetFrustrationPowerLevel": list(range(4)), "AnimTask_GetSeismicTossDamageLevel": list(range(3))}


def cond_value(c):
    """A path condition's single answer (None: 'any other' of a task with more answers than tested)."""
    kind, task, v = c[0], c[1], c[3]
    if not isinstance(v, tuple):
        return v
    excl = set(v[1])
    if kind == "turn":
        # a jump on the turn falls through for every other turn: the first one not taken (Dual Wingbeat's
        # `jumpifmoveturn 1` leaves turn 0, Triple Kick's turns 0 and 1 leave turn 2)
        t = 0
        while t in excl:
            t += 1
        return t
    dom = DOMAIN.get(task, [0, 1])
    rest = [x for x in dom if x not in excl]
    return rest[0] if len(rest) == 1 else None


def matches_default(c):
    kind, task, v = c[0], c[1], c[3]
    want = 1 if kind == "turn" else DEFAULT_ANSWER.get(task, 0)
    if isinstance(v, tuple):
        return want not in set(v[1])
    return v == want


def task_label(task, value):
    short = task[len("AnimTask_"):] if task and task.startswith("AnimTask_") else (task or "script arg")
    if task in ("AnimTask_GetWeather",):
        names = ["ANIM_WEATHER_NONE", "ANIM_WEATHER_SUN", "ANIM_WEATHER_RAIN", "ANIM_WEATHER_SANDSTORM",
                 "ANIM_WEATHER_HAIL", "ANIM_WEATHER_SNOW", "ANIM_WEATHER_FOG"]
        return "%s = %s" % (short, names[value] if 0 <= value < len(names) else value)
    if value in (0, 1) and DOMAIN.get(task) is None:
        return "%s = %s" % (short, "TRUE" if value else "FALSE")
    return "%s = %d" % (short, value)


def end_time(rows):
    return float(rows[-1][0]) if rows and rows[-1][1] == 6 else float(max([r[0] for r in rows] + [0]))


def assemble(paths):
    """The move's default timeline and its variants (the package's form: kind 'turn' | 'arg', task, arg, value).
    Every answer of every decision gets a variant, the path with that answer whose other answers are the default
    battle's ('when' lists them all); turns first - the game takes the first variant whose condition holds
    (bfx_game.c GameVariant) - and none for the default timeline's turn (a two-turn move's strike, as the package)."""
    default = None
    for p in paths:
        if all(matches_default(c) for c in p.conds):
            default = p
            break
    if default is None:
        default = paths[0]
    dturn = [cond_value(c) for c in default.conds if c[0] == "turn"]
    groups = collections.OrderedDict()
    for p in paths:
        for c in p.conds:
            v = cond_value(c)
            if v is None:
                continue
            key = (c[0], c[1], c[2], v)
            score = sum(1 for o in p.conds if o is not c and matches_default(o))
            if key not in groups or score > groups[key][0]:
                groups[key] = (score, p, c)
    var = []
    for key, (score, p, c) in sorted(groups.items(), key=lambda kv: 0 if kv[0][0] == "turn" else 1):
        kind, task, arg, v = key
        if kind == "turn":
            if dturn and v == dturn[0]:
                continue
            e = {"label": "Turn %d" % v, "kind": "turn", "value": v, "branch": c[5], "dur": end_time(p.rows), "ev": p.rows}
        else:
            e = {"label": task_label(task, v), "kind": "arg", "task": task, "arg": arg, "value": v,
                 "dur": end_time(p.rows), "ev": p.rows}
        if len(p.conds) > 1:
            e["when"] = {(o[1] or "turn"): cond_value(o) for o in p.conds}
        var.append(e)
    return default, var


def segments(rows, templates=None):
    """Rows as (syncs before it, frames after the last sync's estimate), the template by name: comparable."""
    out, base, nsync = [], 0.0, 0
    for r in rows:
        r = list(r)
        off = round(r[0] - base, 3)
        k = r[1]
        if k == 0:
            name = templates[r[2]]["n"] if templates is not None else r[2]
            key = ("sprite", nsync, off, name, r[3], r[4], tuple(r[5]), tuple(r[7:9]))
        elif k == 1:
            key = ("task", nsync, off, r[2], tuple(r[3]), tuple(r[5:7]) if len(r) > 5 else (0, 0), r[7] if len(r) > 7 else None)
        elif k == 5:
            key = ("sync", nsync, off, r[2])
            base = r[0] + (r[3] or 0)
            nsync += 1
        elif k == 6:
            key = ("end", nsync)
        elif k == 2:
            x = r[5]
            key = ("sound", nsync, off, r[2], r[3], r[4], json.dumps(x, sort_keys=True) if x else None)
        else:
            key = (["sprite", "task", "sound", "bg", "layer", "sync", "end", "setarg"][k], nsync, off) + tuple(json.dumps(x) for x in r[2:])
        out.append(key)
    return out


# ------------------------------------------------------------------ sprite templates, OAM, animation tables (src/*.c)
def c_blocks(text, typ):
    """{name: body} of `[static] const <typ> NAME[...] = { body };` definitions."""
    out = {}
    rx = re.compile(r"(?:static\s+)?const\s+" + typ + r"\s+(\w+)\s*(?:\[[^\]]*\])?\s*=\s*\{")
    for m in rx.finditer(text):
        i, level = m.end(), 1
        while i < len(text) and level:
            if text[i] == "{":
                level += 1
            elif text[i] == "}":
                level -= 1
            i += 1
        out[m.group(1)] = text[m.end():i - 1]
    return out


def c_fields(body):
    out = {}
    for m in re.finditer(r"\.(\w+)\s*=\s*([^,\n]+?)\s*(?:,|\n|$)", body):
        out[m.group(1)] = m.group(2).strip()
    return out


def c_calls(body):
    """MACRO(args) entries of an animation command array, in order."""
    out = []
    for m in re.finditer(r"\b([A-Z_]+)\s*(\(([^()]*(?:\([^()]*\)[^()]*)*)\))?", body):
        out.append((m.group(1), m.group(3)))
    return out


class Templates:
    def __init__(self, root, scripts, base):
        self.root, self.s, self.base = root, scripts, base
        self.byname = {t["n"]: i for i, t in enumerate(base["templates"])}
        self.added = []
        self._src = None

    def src(self):
        if self._src is None:
            text = []
            for d, _, files in os.walk(os.path.join(self.root, "src")):
                for f in files:
                    if f.endswith(".c") or f.endswith(".h"):
                        try:
                            text.append(strip_comments(read(os.path.join(d, f))))
                        except UnicodeDecodeError:
                            pass
            t = "\n".join(text)
            self._src = {
                "tpl": c_blocks(t, r"struct\s+SpriteTemplate"),
                "oam": c_blocks(t, r"struct\s+OamData"),
                "anim": c_blocks(t, r"union\s+AnimCmd"),
                "animtab": c_blocks(t, r"union\s+AnimCmd\s*\*\s*const"),
                "aff": c_blocks(t, r"union\s+AffineAnimCmd"),
                "afftab": c_blocks(t, r"union\s+AffineAnimCmd\s*\*\s*const"),
            }
        return self._src

    def index(self, name):
        if name in self.byname:
            return self.byname[name]
        t = self.convert(name)
        self.base["templates"].append(t)
        self.byname[name] = len(self.base["templates"]) - 1
        self.added.append(name)
        return self.byname[name]

    def tag(self, expr):
        expr = expr.strip()
        if expr in ("0", "TAG_NONE", ""):
            return "0"
        return expr

    def convert(self, name):
        src = self.src()
        body = src["tpl"].get(name)
        if body is None:
            raise SystemExit("convert_more.py: no sprite template %s" % name)
        f = c_fields(body)
        oam = c_fields(src["oam"].get(f.get("oam", "").lstrip("&").strip(), ""))
        w, h = 32, 32
        m = re.search(r"SPRITE_SIZE\((\d+)x(\d+)\)", oam.get("size", ""))
        if m:
            w, h = int(m.group(1)), int(m.group(2))
        am = oam.get("affineMode", "ST_OAM_AFFINE_OFF")
        aff = 2 if "DOUBLE" in am else 1 if "NORMAL" in am else 0
        om = oam.get("objMode", "ST_OAM_OBJ_NORMAL")
        blend = 2 if "WINDOW" in om else 1 if "BLEND" in om else 0
        t = {"n": name, "prof": [None, None, [], None, None], "w": w, "h": h, "aff": aff, "blend": blend,
             "tile": self.tag(f.get("tileTag", "0")), "pal": self.tag(f.get("paletteTag", "0")),
             "cb": f.get("callback", "SpriteCallbackDummy")}
        anims = f.get("anims", "gDummySpriteAnimTable")
        if anims and anims not in ("gDummySpriteAnimTable", "NULL"):
            t["anims"] = self.anim_table(anims)
        affs = f.get("affineAnims", "gDummySpriteAffineAnimTable")
        if affs and affs not in ("gDummySpriteAffineAnimTable", "NULL"):
            t["affs"] = self.aff_table(affs)
        return t

    def anim_seq(self, name):
        body = self.src()["anim"].get(name)
        if body is None:
            raise SystemExit("convert_more.py: no animation %s" % name)
        out = []
        for mac, args in c_calls(body):
            if mac == "ANIMCMD_FRAME":
                parts = [x.strip() for x in args.split(",")]
                img, dur = self.s.num(parts[0]), self.s.num(parts[1])
                hf = 1 if any(re.match(r"\.hFlip\s*=\s*TRUE", x) for x in parts[2:]) else 0
                vf = 1 if any(re.match(r"\.vFlip\s*=\s*TRUE", x) for x in parts[2:]) else 0
                out.append([0, img, dur, hf, vf])
            elif mac == "ANIMCMD_JUMP":
                out.append([-2, self.s.num(args)])
            elif mac == "ANIMCMD_LOOP":
                out.append([-3, self.s.num(args)])
            elif mac == "ANIMCMD_END":
                out.append([-1])
        return out

    def anim_table(self, name):
        tabs = self.base["animTables"]
        if name in tabs:
            return tabs[name]
        body = self.src()["animtab"].get(name)
        if body is None:
            raise SystemExit("convert_more.py: no animation table %s" % name)
        seqs = [self.anim_seq(x) for x in re.findall(r"\b(\w+)\b", body)]
        self.base["anims"].append(seqs)
        tabs[name] = len(self.base["anims"]) - 1
        return tabs[name]

    def aff_seq(self, name):
        if name in self.base["affArrays"]:
            return self.base["affArrays"][name]
        body = self.src()["aff"].get(name)
        if body is None:
            raise SystemExit("convert_more.py: no affine animation %s" % name)
        out = []
        for mac, args in c_calls(body):
            if mac == "AFFINEANIMCMD_FRAME":
                xs, ys, rot, dur = [self.s.num(x) for x in args.split(",")]
                out.append([0, xs & 0xFFFF, ys & 0xFFFF, rot, dur])
            elif mac == "AFFINEANIMCMD_JUMP":
                out.append([-2, self.s.num(args)])
            elif mac == "AFFINEANIMCMD_LOOP":
                out.append([-3, self.s.num(args)])
            elif mac == "AFFINEANIMCMD_END":
                out.append([-1])
        self.base["affArrays"][name] = out
        return out

    def aff_table(self, name):
        tabs = self.base["affTables"]
        if name in tabs:
            return tabs[name]
        body = self.src()["afftab"].get(name)
        if body is None:
            raise SystemExit("convert_more.py: no affine table %s" % name)
        seqs = [self.aff_seq(x) for x in re.findall(r"\b(\w+)\b", body)]
        self.base["affines"].append(seqs)
        tabs[name] = len(self.base["affines"]) - 1
        return tabs[name]


# ------------------------------------------------------------------ driver
def convert_move(sim, label):
    paths = sim.run(label)
    return assemble(paths)


def check(scripts, base, ids, verbose=False):
    """Converts moves the package has and compares their rows with the package's."""
    tpls = Templates(scripts.root, scripts, copy.deepcopy(base))
    sim = Sim(scripts, tpls)
    T = tpls.base["templates"]
    same = differ = failed = 0
    report = []
    added = set((base.get("meta", {}).get("more") or {}).get("moves", []))   # the moves this converter added
    for m in base["moves"]:
        if (ids and m["id"] not in ids) or m["id"] in added:
            continue
        try:
            dflt, var = convert_move(sim, m["script"])
        except SystemExit as e:
            failed += 1
            report.append("%s: %s" % (m["id"], e))
            continue
        problems = []
        a, b = segments(dflt.rows, T), segments(m["ev"], base["templates"])
        if a != b:
            problems.append(("default", a, b))
        pk = {(v["kind"], v.get("task"), v["value"]): v for v in m.get("var") or []}
        ours = {(v["kind"], v.get("task"), v["value"]) for v in var}
        for key in sorted(set(pk) - ours, key=str):
            problems.append(("package variant %s not ours" % pk[key]["label"], [], []))
        for v in var:
            key = (v["kind"], v.get("task"), v["value"])
            if key in pk:
                a2, b2 = segments(v["ev"], T), segments(pk[key]["ev"], base["templates"])
                if a2 != b2:
                    problems.append((v["label"], a2, b2))
        if problems:
            differ += 1
            line = "%s: %s" % (m["id"], ", ".join(p[0] for p in problems))
            if verbose:
                for name, a, b in problems:
                    for i in range(max(len(a), len(b))):
                        x = a[i] if i < len(a) else None
                        y = b[i] if i < len(b) else None
                        if x != y:
                            line += "\n    [%s] row %d\n      ours:    %s\n      package: %s" % (name, i, x, y)
                            break
            report.append(line)
        else:
            same += 1
    return same, differ, failed, report


TWO_TURN = {"EFFECT_SOLAR_BEAM", "EFFECT_TWO_TURNS_ATTACK", "EFFECT_SEMI_INVULNERABLE", "EFFECT_SKY_DROP",
            "EFFECT_GEOMANCY", "EFFECT_METEOR_BEAM", "EFFECT_SKULL_BASH", "EFFECT_ELECTRO_SHOT"}


class MoveInfo:
    """name, type, category, power, target, effect and the package's tags of each move (src/data/moves_info.h)."""

    def __init__(self, root, scripts):
        text = strip_comments(read(os.path.join(root, "src/data/moves_info.h")))
        self.s = scripts
        self.info = {}
        ents = list(re.finditer(r"\n    \[(MOVE_[A-Z0-9_]+)\]\s*=", text))
        for i, m in enumerate(ents):
            body = text[m.end(): ents[i + 1].start() if i + 1 < len(ents) else len(text)]
            self.info[m.group(1)] = body

    def value(self, body, field):
        m = re.search(r"\n        \." + field + r"\s*=\s*(.+?),\s*\n", body)
        return m.group(1).strip() if m else None

    def choose(self, expr):
        """`cond ? a : b` as the game's config has it."""
        m = re.match(r"^\(?(.*?)\)?\s*\?\s*(\w+)\s*:\s*(\w+)$", expr or "")
        if m and not re.match(r"^\d+$", expr):
            return m.group(2) if self.s.consts.eval(m.group(1)) else m.group(3)
        return expr

    def entry(self, mid, num):
        body = self.info[mid]
        name = re.search(r'\.name\s*=\s*COMPOUND_STRING\("([^"]*)"\)', body)
        pexpr = self.value(body, "power") or "0"
        self.s.consts.ensure(re.findall(r"\b[A-Z_][A-Z0-9_]*\b", pexpr))
        power = self.s.consts.eval(pexpr) or 0
        effect = self.choose(self.value(body, "effect"))
        target = self.choose(self.value(body, "target"))
        tags = []
        if re.search(r"\.makesContact\s*=\s*TRUE", body):
            tags.append("contact")
        if target in ("TARGET_BOTH", "TARGET_FOES_AND_ALLY"):
            tags.append("multi")
        if re.search(r"\.healingMove\s*=\s*TRUE", body):
            tags.append("healing")
        if re.search(r"\.soundMove\s*=\s*TRUE", body):
            tags.append("sound")
        if effect in TWO_TURN:
            tags.append("two_turn")
        if effect and "WEATHER" in effect:
            tags.append("weather")
        if effect and "TERRAIN" in effect:
            tags.append("terrain")
        return {"id": mid, "num": num, "name": name.group(1) if name else mid, "type": self.choose(self.value(body, "type")),
                "cat": self.choose(self.value(body, "category")), "pow": power, "target": target, "effect": effect,
                "tags": tags, "script": self.choose(self.value(body, "battleAnimScript"))}


def move_numbers(scripts):
    """MOVE_* -> number (include/constants/moves.h: its enum, each move numbered or the one before it plus one)."""
    text = strip_comments(read(os.path.join(scripts.root, "include/constants/moves.h")))
    body = text[re.search(r"enum\b[^{]*\bMove\b", text).start():]
    body = body[body.index("{") + 1:body.index("};")]
    nums, last = {}, -1
    names = {}
    for part in body.split(","):
        m = re.match(r"\s*(\w+)\s*(?:=\s*(.+))?$", part.strip(), re.S)
        if not m:
            continue
        name, val = m.group(1), m.group(2)
        if val is not None:
            val = val.strip()
            last = names[val] if val in names else int(val, 0) if re.match(r"^(0x[0-9a-fA-F]+|\d+)$", val) else None
            if last is None:
                raise SystemExit("convert_more.py: moves.h: %s = %s" % (name, val))
        else:
            last += 1
        names[name] = last
        if name.startswith("MOVE_"):
            nums.setdefault(name, last)
    return nums


def extend(scripts, base, ids, first):
    """The base data with the moves asked for added (each converted from its own script)."""
    info = MoveInfo(scripts.root, scripts)
    nums = move_numbers(scripts)
    have = {m["id"] for m in base["moves"]}
    havenum = {m["num"] for m in base["moves"]}
    tpls = Templates(scripts.root, scripts, base)
    sim = Sim(scripts, tpls)
    added, skipped = [], {}
    want = sorted((mid for mid in info.info if (mid in ids) or (not ids and first and nums.get(mid, 0) >= first)),
                  key=lambda x: nums.get(x, 99999))
    for mid in want:
        num = nums.get(mid)
        if mid in have or num is None or num in havenum:
            continue
        e = info.entry(mid, num)
        if not e["script"] or e["script"] not in scripts.labels:
            skipped[mid] = "no script"
            continue
        try:
            dflt, var = convert_move(sim, e["script"])
        except SystemExit as ex:
            skipped[mid] = str(ex).replace("convert_more.py: ", "")
            continue
        e["dur"] = end_time(dflt.rows)
        e["ev"] = dflt.rows
        if var:
            e["var"] = var
        base["moves"].append(e)
        havenum.add(num)
        added.append(mid)
    base["meta"]["moves"] = len(base["moves"])
    base["meta"].setdefault("more", {})
    base["meta"]["more"] = {"converter": "tools/battlefx/convert_more.py", "moves": added, "templates": tpls.added}
    return added, skipped, tpls.added


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--root", default=ROOT, help="the game's tree (read only)")
    ap.add_argument("--base", default=os.path.join(ROOT, "battlefx", "bfx_moves.json"))
    ap.add_argument("--out")
    ap.add_argument("--moves", default="")
    ap.add_argument("--from", dest="first", type=int, default=0)
    ap.add_argument("--check", action="store_true")
    ap.add_argument("-v", action="store_true")
    o = ap.parse_args()
    base = json.load(open(o.base))
    scripts = Scripts(o.root)
    ids = set(x for x in o.moves.split(",") if x)
    if o.check:
        same, differ, failed, report = check(scripts, base, ids, o.v)
        for line in report:
            print(line)
        print("same %d, different %d, not converted %d" % (same, differ, failed))
        return 0 if not differ and not failed else 1
    if not o.out or not (ids or o.first):
        ap.error("--out and --moves or --from")
    added, skipped, tpl = extend(scripts, base, ids, o.first)
    with open(o.out, "w") as f:
        json.dump(base, f, separators=(",", ":"))
    print("added %d moves%s" % (len(added), (", %d templates (%s)" % (len(tpl), ", ".join(tpl))) if tpl else ""))
    for mid, why in sorted(skipped.items()):
        print("  not converted: %s (%s)" % (mid, why))
    return 0


if __name__ == "__main__":
    sys.exit(main())
