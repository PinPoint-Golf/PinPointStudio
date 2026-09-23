#!/usr/bin/env python3
"""Prove a library conversion with the SECOND decoder.

pps_convert_library verifies every swing with the C++ decoder before it deletes the JSON. This
repeats the check with libppswing's pure-Python reader — written independently from the spec — so
a bug shared by the C++ writer and the C++ reader cannot pass unnoticed (the same two-decoder rule
the 170-swing corpus proof used).

For every swing.json in the pre-conversion backup tarball it loads the swing.ppsw now at the same
place, removes the `summary` block the conversion added, and compares with spec §1.1 semantics
(ppswing.semantic_diff: exact doubles, int vs float, -0.0). The tarball is streamed; nothing is
extracted to disk.

    verify_conversion.py --backup swingdocs-pre-ppsw.tgz --root /mnt/swingdata [--csv out.csv]

Member names are relative to the share root (corpus/swings/..., Mark-Liversedge/...); Windows tar
may store them with backslashes, which are normalised. Exit status 0 only if every swing is EQUAL.
"""
import argparse
import csv
import json
import os
import sys
import tarfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from pp_swingdoc import _ppswing as ppswing  # noqa: E402  (the reader pp_swingdoc located)

ap = argparse.ArgumentParser()
ap.add_argument("--backup", required=True)
ap.add_argument("--root", required=True, help="the share root the member names are relative to")
ap.add_argument("--csv")
a = ap.parse_args()

rows, equal, differ, missing = [], 0, 0, 0
with tarfile.open(a.backup, "r:gz") as tar:
    for m in tar:
        name = m.name.replace("\\", "/")
        if not m.isfile() or not name.endswith("/swing.json"):
            continue
        swing_dir = os.path.join(a.root, os.path.dirname(name))
        ppsw = os.path.join(swing_dir, "swing.ppsw")
        original = json.load(tar.extractfile(m))
        if not os.path.exists(ppsw):
            missing += 1
            rows.append((os.path.dirname(name), "MISSING", ""))
            print("MISSING", ppsw, flush=True)
            continue
        converted = ppswing.Reader(ppsw).load(plain=True)
        converted.pop("summary", None)
        diff = ppswing.semantic_diff(original, converted)
        if diff is None:
            equal += 1
            rows.append((os.path.dirname(name), "EQUAL", ""))
        else:
            differ += 1
            rows.append((os.path.dirname(name), "DIFFER", diff))
            print("DIFFER", ppsw, "at", diff, flush=True)

if a.csv:
    with open(a.csv, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["swing", "result", "first_difference"])
        w.writerows(sorted(rows))
print(f"{equal} EQUAL, {differ} DIFFER, {missing} MISSING of {len(rows)}")
sys.exit(0 if differ == 0 and missing == 0 and equal > 0 else 1)
