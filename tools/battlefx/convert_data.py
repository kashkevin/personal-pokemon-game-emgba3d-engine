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
"""
import importlib.util
import sys
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
        return out

    bpd.compact_events = compact_events
    sys.argv = [str(path)] + sys.argv[2:]
    return bpd.main()


if __name__ == "__main__":
    sys.exit(main())
