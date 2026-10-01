#!/usr/bin/env python3
"""diag_uncertainty_prep.py — lay a swept run out as sessions the diagnostics panel can read.

session_diagnostics_design.md §A8.8. The gates run READ-ONLY against the library: swinglab sweeps
each library swing into a scratch run root (<session>__<swing>/result.json), and this script builds

    <out>/<session>/<swing>/swing.json

for every swing — the LIBRARY document (clock, capture, review, session, …, so the shot keeps its
club, context and capture instant) with only its `analysis` replaced by the swept one. The report
tool (diag_uncertainty_report) then runs the panel's own model over <out>/<session>, so nothing in
the library is ever written.

  diag_uncertainty_prep.py --run /mnt/swingdata/scratch/diagunc/lib \
                           --library /mnt/swingdata/Mark-Liversedge \
                           --out /mnt/swingdata/scratch/diagunc/sessions
"""
import argparse, json, os, sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import pp_swingdoc as psd


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--run", help="swept run root; omit to lay out the library documents as they are")
    ap.add_argument("--library", required=True)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    n = skipped = 0
    if not a.run:
        # The documents as they stand (e.g. a pre-change backup): no analysis is replaced.
        for session in sorted(os.listdir(a.library)):
            sd = os.path.join(a.library, session)
            if not os.path.isdir(sd):
                continue
            for swing in sorted(os.listdir(sd)):
                if not swing.startswith("swing_") or not psd.has_swing(os.path.join(sd, swing)):
                    continue
                doc = psd.load_swing(os.path.join(sd, swing))
                od = os.path.join(a.out, session, swing)
                os.makedirs(od, exist_ok=True)
                with open(os.path.join(od, "swing.json"), "w", encoding="utf-8") as f:
                    json.dump(doc, f)
                n += 1
        print(f"{n} swings laid out under {a.out} (documents as they are)")
        return
    for rid in sorted(os.listdir(a.run)):
        rd = os.path.join(a.run, rid)
        if "__swing_" not in rid or not os.path.isdir(rd):
            continue
        rp = os.path.join(rd, "result.json")
        if not os.path.exists(rp):
            skipped += 1
            continue
        session, swing = rid.split("__")
        doc = psd.load_swing(os.path.join(a.library, session, swing))
        doc["analysis"] = json.load(open(rp, encoding="utf-8"))["analysis"]
        od = os.path.join(a.out, session, swing)
        os.makedirs(od, exist_ok=True)
        with open(os.path.join(od, "swing.json"), "w", encoding="utf-8") as f:
            json.dump(doc, f)
        n += 1
    print(f"{n} swings laid out under {a.out}; {skipped} run dirs had no result.json")


if __name__ == "__main__":
    main()
