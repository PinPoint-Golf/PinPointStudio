"""The swing-by-measure table a golfer's sessions already hold.

Reads each session's diagnostics.json (the ledger the panel writes) and the diagnostics pack, and
returns one row per swing and one column per MEASURE, holding the measured value wherever a
condition read it — on clean swings as well as fired ones. Nothing is re-analysed.

Since schema 4 a row read by several signals carries every measure (`readings`); older rows carry
the driving measure alone. Two conditions that read the same measure (early extension and backing
off both read the pelvis line) give one column, not two.

    from theme_data import load_table
    t = load_table(["/mnt/swingdata/Mark-Liversedge/2026-10-08_..."])
    t.values          # ndarray swings × measures, NaN where not read
    t.session         # session index per swing
    t.measures        # measure ids, column order
    t.meta[mid]       # label, unit, metricKey, window, conditions that read it
"""
from __future__ import annotations

import json
import os
from dataclasses import dataclass, field

import numpy as np

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
PACK = os.path.join(REPO, "src", "Resources", "diagnostics", "core.json")

# Ledger phase anchors → position in the swing, for ordering measures in time.
PHASE_ORDER = {"p1": 1, "p2": 2, "p3": 3, "p4": 4, "p5": 5, "p6": 6, "p7": 7, "p8": 8,
               "p9": 9, "p10": 10}


@dataclass
class Table:
    values: np.ndarray
    session: np.ndarray
    sessions: list
    swings: list            # (session dir name, shotId)
    measures: list
    meta: dict = field(default_factory=dict)
    fired: dict = field(default_factory=dict)   # condition id -> ndarray of 1/0/NaN per swing


def load_pack(path: str = PACK) -> dict:
    with open(path) as f:
        return json.load(f)


def measure_meta(pack: dict) -> dict:
    """label, unit, metricKey, view, swing position (latest phase the reducer reads), conditions."""
    signals = {s["id"]: s for s in pack["signals"]}
    meta = {}
    for m in pack["measures"]:
        r = m.get("reducer") or {}
        phases = [p for p in ([r.get("anchor")] + list(r.get("window") or [])) if p]
        when = max((PHASE_ORDER.get(p, 0) for p in phases), default=0)
        meta[m["id"]] = {
            "label": m.get("label", m["id"]), "unit": m.get("unit", ""),
            "metricKey": m.get("metricKey", ""), "view": m.get("viewNeeded", ""),
            "reducer": r, "when": when, "shape": m.get("shape", ""),
            "highMeans": m.get("highMeans", ""), "conditions": [],
        }
    for c in pack["conditions"]:
        for sid in c.get("detectedBy", []):
            s = signals.get(sid)
            if not s:
                continue
            for mid in s.get("measures", [])[:1]:
                if mid in meta and c["id"] not in meta[mid]["conditions"]:
                    meta[mid]["conditions"].append(c["id"])
    return meta


def _row_readings(row: dict):
    """(measureId, value) for every measure behind a row; the driving one on an older row."""
    if row.get("state") == "notAssessable":
        return []
    rs = row.get("readings")
    if rs:
        return [(r["measureId"], r["value"]) for r in rs if r.get("measureId")]
    mid = row.get("drivingMeasureId")
    return [(mid, row.get("value"))] if mid else []


def load_table(session_dirs, pack: dict | None = None, min_coverage: float = 0.0) -> Table:
    pack = pack or load_pack()
    meta = measure_meta(pack)
    cells, swings, sess, fired = [], [], [], {}
    sessions = []
    for si, d in enumerate(sorted(session_dirs)):
        p = os.path.join(d, "diagnostics.json")
        if not os.path.exists(p):
            continue
        with open(p) as f:
            led = json.load(f)["ledger"]
        sessions.append(os.path.basename(os.path.normpath(d)))
        for shot in led["shots"]:
            rec, fr = {}, {}
            for row in shot["rows"]:
                for mid, v in _row_readings(row):
                    if v is not None and np.isfinite(v):
                        rec.setdefault(mid, float(v))
                st = row.get("state")
                fr[row["conditionId"]] = 1.0 if st == "fired" else 0.0 if st == "clean" else np.nan
            cells.append(rec)
            fired_row = fr
            for k, v in fired_row.items():
                fired.setdefault(k, {})[len(swings)] = v
            swings.append((sessions[-1], shot["shotId"]))
            sess.append(len(sessions) - 1)
    measures = sorted({m for rec in cells for m in rec})
    X = np.full((len(cells), len(measures)), np.nan)
    idx = {m: j for j, m in enumerate(measures)}
    for i, rec in enumerate(cells):
        for m, v in rec.items():
            X[i, idx[m]] = v
    if min_coverage > 0:
        keep = np.mean(np.isfinite(X), axis=0) >= min_coverage
        X, measures = X[:, keep], [m for m, k in zip(measures, keep) if k]
    n = len(swings)
    fired_arr = {k: np.array([v.get(i, np.nan) for i in range(n)]) for k, v in fired.items()}
    return Table(X, np.array(sess), sessions, swings, measures,
                 {m: meta.get(m, {"label": m, "conditions": [], "when": 0}) for m in measures},
                 fired_arr)


def centre_per_session(X: np.ndarray, session: np.ndarray, robust: bool = True) -> np.ndarray:
    """Subtract each session's median (or mean) per measure, then scale each column by its pooled
    within-session spread (MAD×1.4826, or SD). Removes camera/club/day drift so what is left is
    swing-to-swing variation — the thing a golfer's own patterns are made of."""
    Z = np.full_like(X, np.nan)
    for s in np.unique(session):
        rows = session == s
        sub = X[rows]
        c = np.nanmedian(sub, axis=0) if robust else np.nanmean(sub, axis=0)
        Z[rows] = sub - c
    if robust:
        spread = 1.4826 * np.nanmedian(np.abs(Z), axis=0)
    else:
        spread = np.nanstd(Z, axis=0)
    spread[~(spread > 0)] = np.nan
    return Z / spread


if __name__ == "__main__":
    import glob, sys
    dirs = sys.argv[1:] or sorted(glob.glob("/mnt/swingdata/Mark-Liversedge/*/"))
    t = load_table(dirs)
    cov = np.mean(np.isfinite(t.values), axis=0)
    print(f"{len(t.swings)} swings × {len(t.measures)} measures over {len(t.sessions)} sessions")
    for s_i, s in enumerate(t.sessions):
        print(f"  {s}: {np.sum(t.session == s_i)} swings")
    print("coverage ≥ 0.8:", int(np.sum(cov >= 0.8)), " ≥ 0.5:", int(np.sum(cov >= 0.5)))
    for j in np.argsort(-cov)[:60]:
        m = t.measures[j]
        sd = np.nanstd(t.values[:, j])
        print(f"  {cov[j]:.2f}  sd {sd:8.3f}  {m:32s} {t.meta[m].get('label','')[:60]}")
