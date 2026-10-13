"""Collect every GC Quad shot in the corpus that carries the full set the flight fit needs.

Reads each swing document's launchMonitor block (read-only; nothing on the share is touched)
and writes one CSV row per usable shot to the path given. A shot is usable when it has ball
speed, launch angle, start direction, spin rate and spin axis IN, and carry, peak height and
descent angle OUT. Club labels are copied through for reference only: they are unreliable in
this corpus and the fit does not use them.

    python3 tools/lm_flight_fit/extract_gcquad_shots.py \
        /mnt/swingdata/corpus/swings  /path/outside/the/repo/gcquad_shots.csv
"""
import csv
import glob
import math
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))  # tools/
from pp_swingdoc import load_swing  # noqa: E402

NEED = ("ballSpeed", "launchAngle", "spinRate", "spinAxis",
        "carryDistance", "peakHeight", "descentAngle")
COLS = ("session", "swing", "deviceShotId", "deviceClub", "ballSpeed", "launchAngle",
        "launchDirection", "spinRate", "spinAxis", "carryDistance", "peakHeight",
        "descentAngle", "offline", "totalDistance")


def _num(v):
    try:
        f = float(v)
    except (TypeError, ValueError):
        return None
    return f if math.isfinite(f) else None


def main(root, out_csv):
    rows, seen, skipped = [], 0, 0
    for sw in sorted(glob.glob(os.path.join(root, "*", "swing_*"))):
        if not os.path.isdir(sw):
            continue
        try:
            doc = load_swing(sw)
        except Exception as e:  # a swing that will not load is reported, not fatal
            print(f"skip {sw}: {e}", file=sys.stderr)
            continue
        lm = doc.get("launchMonitor") or {}
        if str(lm.get("kind", "")).lower() != "gcquad":
            continue
        seen += 1
        vals = {k: _num(lm.get(k)) for k in NEED}
        if any(v is None for v in vals.values()) or vals["ballSpeed"] <= 0 \
                or vals["launchAngle"] <= 0 or vals["carryDistance"] <= 0:
            skipped += 1
            continue
        rows.append({
            "session": os.path.basename(os.path.dirname(sw)),
            "swing": os.path.basename(sw),
            "deviceShotId": lm.get("deviceShotId", ""),
            "deviceClub": lm.get("deviceClub", ""),
            **vals,
            "launchDirection": _num(lm.get("launchDirection")) or 0.0,
            "offline": _num(lm.get("offline")),
            "totalDistance": _num(lm.get("totalDistance")),
        })
    with open(out_csv, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=COLS)
        w.writeheader()
        w.writerows(rows)
    print(f"{len(rows)} usable GC Quad shots of {seen} seen ({skipped} incomplete) -> {out_csv}")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
