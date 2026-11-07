#!/usr/bin/env python3
"""Variability calibration: a read-only copy of each swing's kinematic-sequence ORDER, as the app stored it, one JSON
per session, for variability_calibrate's ordinal rows (docs/design/variability-mark/spec.md §8.4). The consistency
extract (consistency_extract.py) leaves kinematicSequence out, and ranking raw curve peaks is not the app's order: it
ranks segments the analysis refused to place. Run it on the cabin PC, where the share is local:

    python variability_ks_extract.py <athlete dir> <out dir> [<repo tools dir>]

<repo tools dir> is where pp_swingdoc.py lives (default: this file's parent's parent). It writes only into <out dir>:
{session, swings: {swing_dir: {order: [...], orderResolved, verdict, placed: [...]}}}. A swing with no
kinematicSequence block is left out.
"""
import sys, os, json, glob

ROOT = sys.argv[1]
OUT = sys.argv[2]
TOOLS = sys.argv[3] if len(sys.argv) > 3 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
sys.path.insert(0, TOOLS)
from pp_swingdoc import load_swing, has_swing  # noqa: E402

os.makedirs(OUT, exist_ok=True)
for sd in sorted(glob.glob(os.path.join(ROOT, "2026-*"))):
    if not os.path.isdir(sd):
        continue
    name = os.path.basename(sd)
    swings = {}
    for wd in sorted(glob.glob(os.path.join(sd, "swing_*"))):
        if not os.path.isdir(wd) or not has_swing(wd):
            continue
        try:
            ks = ((load_swing(wd).get("analysis") or {}).get("kinematicSequence")) or {}
        except Exception as e:
            print("skip", wd, e, flush=True)
            continue
        if not ks:
            continue
        swings[os.path.basename(wd)] = {
            "order": ks.get("order") or [],
            "orderResolved": bool(ks.get("orderResolved")),
            "verdict": ks.get("verdict") or "",
            "placed": [n.get("segment") for n in ks.get("nodes") or [] if n.get("placed")],
        }
    with open(os.path.join(OUT, name + ".json"), "w", encoding="utf-8") as f:
        json.dump({"session": name, "swings": swings}, f, separators=(",", ":"), ensure_ascii=False)
    resolved = sum(s["orderResolved"] for s in swings.values())
    print(name, len(swings), "resolved", resolved, flush=True)
print("DONE", flush=True)
