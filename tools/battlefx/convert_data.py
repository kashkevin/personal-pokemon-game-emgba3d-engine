#!/usr/bin/env python3
"""Converts this game's battle animation scripts to the Battle FX data (game_moves.json, packed into
battlefx/bfx_moves.json by pack_data.py) with the Battle FX package's own converter,
<battlefx_kit>/prototype/build_preview_data.py, reading this repository (read only).

  python3 tools/battlefx/convert_data.py <battlefx_kit> --expansion <this repository> --out game_moves.json

One addition to the package's converter (v3.3), made here without changing the package: its compact
event rows keep a panse / panse_adjustnone / panse_adjustall command's pan step and frames between steps
(the scripts' 4th and 5th arguments; its script reader has them as pan_step and pan_delay) beside
pan_from and pan_to. The game's panse runs a sound task until the pan reaches its target
(battle_anim.c Task_PanFromInitialToTarget, counted in gAnimSoundTaskCount, which waitsound and the
script's end wait for); with the step and delay the core runs it too (bfx_game.c). The runtime ignores
fields it does not know, so the data still plays in the package's Lab. Everything else is the
package's converter as it is; its arguments pass through.

Its task rows also keep a createvisualtask's priority (the script's 2nd argument), as an 8th field (the
optional 6th and 7th, on-targets and the battler argument's index, are 0 where the row had none): the
game runs its tasks in priority order (task.c), and a battle animation's script runs inside a task of
priority 10 (bfx_game.c). The runtime reads rows by index, so the field changes nothing in the Lab.

And one fix to its script reader: GNU as ends a statement at ';' as at the end of a line (ARM), so
`goto ParabolicChargeHeal;` (battle_anim_scripts.s, Parabolic Charge's double-battle branch) is a goto
to ParabolicChargeHeal. The package's reader took the ';' as part of the label, found no such label and
ended the branch there (no healing stars, no fade back in a double battle). The scripts are read with
the ';' ending the statement, line numbers unchanged; a line with two statements stops the conversion.
"""
import importlib.util
import os
import sys
import tempfile
from pathlib import Path


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    kit = Path(sys.argv[1])
    path = kit / "prototype" / "build_preview_data.py"
    spec = importlib.util.spec_from_file_location("build_preview_data", path)
    bpd = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(bpd)
    compact = bpd.compact_events

    def compact_events(events, tt):
        out = compact(events, tt)
        # every sound event gives exactly one EV_SOUND row, in order (compact_events' sfx branch)
        sfx_in = [e for e in events if e.get("kind") == "sfx"]
        sfx_out = [r for r in out if r[1] == bpd.EV_SOUND]
        if len(sfx_in) != len(sfx_out):
            raise SystemExit("convert_data.py: the converter's sound rows no longer pair with its events")
        for e, row in zip(sfx_in, sfx_out):
            if str(e.get("op", "")).startswith("panse") and ("pan_step" in e or "pan_delay" in e):
                extra = dict(row[5] or {})
                extra["pan_step"] = e.get("pan_step")
                extra["pan_delay"] = e.get("pan_delay")
                row[5] = extra
        # every task event gives exactly one EV_TASK row, in order: the task's priority as the 8th field
        task_in = [e for e in events if e.get("kind") == "task"]
        task_out = [r for r in out if r[1] == bpd.EV_TASK]
        if len(task_in) != len(task_out):
            raise SystemExit("convert_data.py: the converter's task rows no longer pair with its events")
        for e, row in zip(task_in, task_out):
            if e.get("task") != row[2]:
                raise SystemExit("convert_data.py: a task row is not its event's (%s, %s)" % (e.get("task"), row[2]))
            if e.get("priority") is not None:
                while len(row) < 7:
                    row.append(0)
                row.append(int(e["priority"]))
        return out

    bpd.compact_events = compact_events

    # the IR's task events with the createvisualtask's priority (both macros: addr, priority, ...)
    conv = sys.modules["convert"]
    task_event = conv.Simulator._task_event

    def _task_event(self, ins, t, src_ref):
        ev = task_event(self, ins, t, src_ref)
        if len(ins.args) > 1:
            ev["priority"] = self.res.num(ins.args[1])
        return ev

    conv.Simulator._task_event = _task_event

    parse = bpd.parse_scripts

    def parse_scripts(script_path, expander, consts):
        lines = []
        for lineno, raw in enumerate(Path(script_path).read_text(encoding="utf-8").splitlines(), 1):
            code, sep, comment = raw.partition("@")
            if ";" in code:
                statements = [x for x in code.split(";") if x.strip()]
                if len(statements) > 1:
                    raise SystemExit("convert_data.py: %s line %d holds %d statements; the converter reads one a line"
                                     % (script_path, lineno, len(statements)))
                code = statements[0] if statements else ""
            lines.append(code + sep + comment)
        fd, tmp = tempfile.mkstemp(suffix=".s")
        try:
            with os.fdopen(fd, "w", encoding="utf-8") as f:
                f.write("\n".join(lines) + "\n")
            return parse(Path(tmp), expander, consts)
        finally:
            os.unlink(tmp)

    bpd.parse_scripts = parse_scripts
    sys.argv = [str(path)] + sys.argv[2:]
    return bpd.main()


if __name__ == "__main__":
    sys.exit(main())
