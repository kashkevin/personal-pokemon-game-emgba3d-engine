#!/usr/bin/env python3
"""Multiplayer battle latency from the games' own timelines (MP+3D update 2026-10-26).

Each game run with PKMN_LINK_TRACE set writes every breadcrumb of src/platform/crash_report.c to its stderr (the
launcher's runtime.log) as "link-trace MS FRAME KIND A B", MS on the machine's monotonic clock, which every process on
one machine shares. This reads the runtime.log of every game of one battle and measures:

- choice -> response: a choice of the player controller leaving (FIGHT, a move, a target, a Pokemon...) until the game
  shows the host's answer (the next menu, or the turn's first message). In a link battle that is a round trip: the
  choice goes to the host, the master game's battle engine takes it, and its answer comes back.
- input -> first visible feedback: the A press that made the choice until anything on screen answers it - the response
  above, or (the 2026-10-26 change) the HUD's "Choice sent" note, which goes up two frames after the choice leaves.
- each response split where the trace allows it: this game until its block leaves (client), the host and the network to
  the master game (up), the master game until it sends again (master), back (down), and this game until it draws (render).
- message waits: a link battle's message waiting for its time to pass (49 frames before the change, A or B now).

    link_latency.py RUNTIME_LOG... [--json OUT]

Read-only: nothing in the games changes. KIND numbers are enum CrashCrumb (include/platform/crash_report.h); choices
enum NativeBattleInput (include/native_ui/ui_engine.h).
"""
from __future__ import annotations

import argparse
import json
import statistics
import sys
from pathlib import Path

LINK_SEND, LINK_DELIVER, CHOICE, MESSAGE, KEY = 4, 5, 9, 10, 11
NONE, ACTION_WAIT, ACTION, MOVE_WAIT, MOVE, MOVE_SWITCH, TARGET, TARGETS_SHOWN, YES_NO, PARTY, BAG = range(11)
SENT_SHOWN = 255
FINAL = {ACTION: "action", MOVE: "move", TARGET: "target", TARGETS_SHOWN: "targets", YES_NO: "yes/no",
         PARTY: "party", BAG: "bag"}


def read(path):
    rows = []
    for line in Path(path).read_text(errors="replace").splitlines():
        parts = line.split()
        if len(parts) == 6 and parts[0] == "link-trace":
            try:
                rows.append((float(parts[1]), int(parts[2]), int(parts[3]), int(parts[4]), int(parts[5])))
            except ValueError:
                pass
    return rows


def first(rows, start, test, end=None):
    for row in rows:
        if row[0] > start and (end is None or row[0] <= end) and test(row):
            return row
    return None


def last_before(rows, end, test, start):
    found = None
    for row in rows:
        if row[0] > end:
            break
        if row[0] >= start and test(row):
            found = row
    return found


def stats(values):
    if not values:
        return None
    ordered = sorted(values)
    return dict(n=len(values), median=round(statistics.median(values), 1), mean=round(statistics.fmean(values), 1),
                p90=round(ordered[min(len(ordered) - 1, int(.9 * len(ordered)))], 1), max=round(ordered[-1], 1),
                min=round(ordered[0], 1))


def analyse(paths):
    games = {str(p): read(p) for p in paths}
    games = {name: rows for name, rows in games.items() if rows}
    responses, feedback, waits, legs = [], [], [], {k: [] for k in ("client", "up", "master", "down", "render")}
    per_kind = {}
    for name, rows in games.items():
        others = [r for n, r in games.items() if n != name]
        for i, row in enumerate(rows):
            ms, _, kind, a, b = row
            if kind == CHOICE and a == NONE and b in FINAL:
                # the choice left at `ms`; the answer: the next menu or a new message
                answer = first(rows, ms, lambda r: (r[2] == CHOICE and r[3] not in (NONE, SENT_SHOWN))
                               or (r[2] == MESSAGE and r[3] == 1))
                if answer is None:
                    continue
                round_trip = answer[0] - ms
                if round_trip > 60000:
                    continue  # the battle ended or waited on a person
                key = last_before(rows, ms, lambda r: r[2] == KEY and r[3] & 1, ms - 2000)
                shown = first(rows, ms, lambda r: r[2] == CHOICE and r[3] == SENT_SHOWN, answer[0])
                responses.append(round_trip)
                per_kind.setdefault(FINAL[b], []).append(round_trip)
                if key is not None:
                    visible = (shown[0] if shown is not None else answer[0]) - key[0]
                    feedback.append(visible)
                sent = first(rows, ms, lambda r: r[2] == LINK_SEND, answer[0])
                if sent is not None and others:
                    delivered = min((d for d in (first(o, sent[0], lambda r: r[2] == LINK_DELIVER) for o in others)
                                     if d is not None and d[0] < answer[0]), default=None, key=lambda r: r[0])
                    if delivered is not None:
                        master = next(o for o in others if delivered in o)
                        reply = first(master, delivered[0], lambda r: r[2] == LINK_SEND, answer[0])
                        if reply is not None:
                            back = first(rows, reply[0], lambda r: r[2] == LINK_DELIVER, answer[0])
                            if back is not None:
                                legs["client"].append(sent[0] - ms)
                                legs["up"].append(delivered[0] - sent[0])
                                legs["master"].append(reply[0] - delivered[0])
                                legs["down"].append(back[0] - reply[0])
                                legs["render"].append(answer[0] - back[0])
            elif kind == MESSAGE and a == 2 and b:
                done = first(rows, ms, lambda r: r[2] == MESSAGE and r[3] in (1, 3))
                if done is not None and done[0] - ms < 30000:
                    waits.append(done[0] - ms)
    span = [r[0] for rows in games.values() for r in rows if r[2] == CHOICE]
    return dict(games=len(games),
                battle_span_seconds=round((max(span) - min(span)) / 1000, 1) if span else None,
                choice_to_response_ms=stats(responses),
                by_choice={k: stats(v) for k, v in sorted(per_kind.items())},
                input_to_first_feedback_ms=stats(feedback),
                response_split_ms={k: stats(v) for k, v in legs.items()},
                link_message_wait_ms=stats(waits))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("logs", nargs="+", type=Path)
    parser.add_argument("--json", type=Path)
    args = parser.parse_args()
    result = analyse(args.logs)
    text = json.dumps(result, indent=2)
    print(text)
    if args.json:
        args.json.write_text(text + "\n")
    return 0 if result["games"] else 1


if __name__ == "__main__":
    sys.exit(main())
