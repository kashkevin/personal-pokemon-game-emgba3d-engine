#!/usr/bin/env python3
"""Read a battle sprites trace (PKMN_BATTLE_SPRITES_TRACE, src/platform/battle_sprites.c): the battle frames
in which a move's own picture of a Pokemon is up (battler 4, "move_copy": Role Play's silhouette, the runner
of Snatch's stolen move, Hyperspace Hole and Fury, Skitter Smack), which pack facing drew it, and how many
of its OAM entries were drawn from the pack and how many as the GBA's picture (KNOWN_ISSUES S7).

    python3 tools/battlefx/move_copy_trace.py trace.jsonl [--expect N]

--expect N: exit 1 unless there are N runs of frames with the copy up, every entry of them from the pack.
"""
import argparse
import json


def runs_of(path):
    runs = []   # [key, first battle frame, last, entries from the pack, entries as the GBA's, fades]
    with open(path) as f:
        for line in f:
            if not line.strip():
                continue
            frame = json.loads(line)
            copies = [b for b in frame["battlers"] if b.get("move_copy")]
            if not copies:
                continue
            look = copies[0]
            key = (look["id"], look["back"], look.get("facing"), look.get("gba"))
            entries = [o for o in frame["obj"] if o["battler"] == 4]
            if not runs or runs[-1][0] != key or frame["battle_frame"] != runs[-1][2] + 1:
                runs.append([key, frame["battle_frame"], frame["battle_frame"], 0, 0, set()])
            run = runs[-1]
            run[2] = frame["battle_frame"]
            run[3] += sum(1 for o in entries if o["pack"])
            run[4] += sum(1 for o in entries if not o["pack"])
            run[5].update(round(o["fade"], 2) for o in entries if o["pack"] and "fade" in o)
    return runs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("trace")
    ap.add_argument("--expect", type=int, default=None)
    args = ap.parse_args()
    runs = runs_of(args.trace)
    for key, first, last, packed, gba, fades in runs:
        print("battle frames %d-%d: %s %s, pack facing %s%s; entries from the pack %d, as the GBA's %d; fades %s"
              % (first, last, key[0], "back" if key[1] else "front", key[2],
                 " (GBA: %s)" % key[3] if key[3] else "", packed, gba, sorted(fades)))
    if args.expect is not None:
        ok = len(runs) == args.expect and all(r[3] > 0 and r[4] == 0 for r in runs)
        print("expected %d runs, every entry from the pack: %s" % (args.expect, "yes" if ok else "NO"))
        raise SystemExit(0 if ok else 1)


if __name__ == "__main__":
    main()
