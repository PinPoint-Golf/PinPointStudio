#!/usr/bin/env python3
"""Raw vs mp4 re-analysis: how much does analysing the encoded mp4 cost, measured
against the pipeline's own run-to-run noise?

Inputs are `swinglab_run --out` run trees, one per arm, laid out as
<arm>/<session>/<swing>/result.json:

  R1, R2  from the .raw frames, twice (unpinned)  -> R1~R2 is the raw noise floor
  M1, M2  from the .mp4, twice (unpinned)         -> M1~M2 is the mp4 noise floor
  PR, PM  raw and mp4 with --pose pinned to R1's pose, so shaft/ball/phases are
          compared with pose held fixed

Writes one long CSV row per (swing, item, pair) plus a summary CSV of median/p90
|delta| per (item, pair). Pose is in pixels on the 1280x1024 face-on frame,
hands reported apart from the body (hand confidence is unreliable on this
model), club theta in degrees, positions in px, times in ms.

    raw_vs_mp4.py --runs build/rawmp4 --out docs/implementation/swing_storage
"""
import argparse
import csv
import json
import math
import os
import statistics

W, H = 1280, 1024
BODY = range(0, 23)          # COCO body + feet of the 133-point WholeBody layout
HANDS = range(91, 133)
KP_CONF = 0.3
PAIRS = [("R1", "M1"), ("R1", "R2"), ("M1", "M2"), ("PR", "PM")]

ap = argparse.ArgumentParser()
ap.add_argument("--runs", required=True)
ap.add_argument("--out", required=True)
a = ap.parse_args()


def load(arm, sw):
    p = os.path.join(a.runs, "out_" + arm, sw, "result.json")
    if not os.path.exists(p):
        return None
    with open(p) as f:
        return json.load(f)["analysis"]


def frames_meta(arm, sw):
    p = os.path.join(a.runs, "out_" + arm, sw, "runmeta.json")
    with open(p) as f:
        return json.load(f).get("frames")


def pct(xs, q):
    xs = sorted(xs)
    if not xs:
        return None
    k = (len(xs) - 1) * q
    lo, hi = math.floor(k), math.ceil(k)
    return xs[lo] + (xs[hi] - xs[lo]) * (k - lo)


def pose_deltas(x, y, joints):
    fx = {f["t_us"]: f["kp"] for f in x.get("pose2d", {}).get("frames", [])}
    fy = {f["t_us"]: f["kp"] for f in y.get("pose2d", {}).get("frames", [])}
    d = []
    for t in fx.keys() & fy.keys():
        kx, ky = fx[t], fy[t]
        for j in joints:
            if kx[3 * j + 2] >= KP_CONF and ky[3 * j + 2] >= KP_CONF:
                d.append(math.hypot((kx[3 * j] - ky[3 * j]) * W, (kx[3 * j + 1] - ky[3 * j + 1]) * H))
    return d, len(fx.keys() & fy.keys()), len(fx), len(fy)


def club_deltas(x, y):
    sx = {s["t_us"]: s for s in x.get("club", {}).get("samples", [])}
    sy = {s["t_us"]: s for s in y.get("club", {}).get("samples", [])}
    th, hd, both, onlyone = [], [], 0, 0
    for t in sx.keys() | sy.keys():
        p, q = sx.get(t), sy.get(t)
        okp = p is not None and p.get("conf", 0) > 0
        okq = q is not None and q.get("conf", 0) > 0
        if okp and okq:
            both += 1
            dth = abs(p["theta"] - q["theta"]) % math.pi
            th.append(math.degrees(min(dth, math.pi - dth)))
            hd.append(math.hypot((p["head"][0] - q["head"][0]) * W, (p["head"][1] - q["head"][1]) * H))
        elif okp or okq:
            onlyone += 1
    return th, hd, both, onlyone


def ball_items(x, y):
    bx = {s["t_us"]: s for s in x.get("ball", {}).get("samples", [])}
    by = {s["t_us"]: s for s in y.get("ball", {}).get("samples", [])}
    common = bx.keys() & by.keys()
    agree = sum(1 for t in common if bool(bx[t].get("found")) == bool(by[t].get("found")))
    pos = [math.hypot((bx[t]["x"] - by[t]["x"]) * W, (bx[t]["y"] - by[t]["y"]) * H)
           for t in common if bx[t].get("found") and by[t].get("found")]
    lx, ly = x.get("ball", {}).get("launchTUs"), y.get("ball", {}).get("launchTUs")
    launch = abs(lx - ly) / 1000.0 if (lx and ly and lx > 0 and ly > 0) else None
    launch_found = (bool(lx and lx > 0), bool(ly and ly > 0))
    return (agree / len(common) if common else None), pos, launch, launch_found


def phase_deltas(x, y):
    px = {p["phase"]: p["t_us"] for p in x.get("phases", []) if p.get("t_us", -1) >= 0}
    py = {p["phase"]: p["t_us"] for p in y.get("phases", []) if p.get("t_us", -1) >= 0}
    return {ph: abs(px[ph] - py[ph]) / 1000.0 for ph in px.keys() & py.keys()}, len(px), len(py)


def metric_deltas(x, y):
    """Scalar metrics (one value) and per-phase samples, by key."""
    def index(an):
        out = {}
        for m in an.get("metrics", []):
            k = m.get("key")
            v = m.get("value")
            if isinstance(v, list) and len(v) == 1:
                out[(k, "scalar")] = v[0]
            elif isinstance(v, (int, float)):
                out[(k, "scalar")] = v
            for s in m.get("phaseSamples", []) or []:
                if isinstance(s.get("value"), (int, float)):
                    out[(k, "P%d" % s["phase"])] = s["value"]
        return out

    ix, iy = index(x), index(y)
    for k in ix.keys() & iy.keys():
        MAGNITUDE.setdefault(k[0], []).append(abs(ix[k]))
    return {k: abs(ix[k] - iy[k]) for k in ix.keys() & iy.keys()}, len(ix), len(iy)


MAGNITUDE = {}   # metric key -> |value| samples, for scale


swings = sorted(
    os.path.join(s, w)
    for s in os.listdir(os.path.join(a.runs, "out_R1"))
    for w in os.listdir(os.path.join(a.runs, "out_R1", s))
)
rows = []


def emit(sw, pair, item, value, n=None):
    rows.append({"swing": sw, "pair": "%s-%s" % pair, "item": item,
                 "value": "" if value is None else round(value, 4), "n": "" if n is None else n})


coverage = {}
for sw in swings:
    for arm in ("R1", "R2", "M1", "M2", "PR", "PM"):
        ok = load(arm, sw) is not None
        coverage.setdefault(arm, 0)
        coverage[arm] += ok
        want = "raw" if arm in ("R1", "R2", "PR") else "mp4"
        if ok and frames_meta(arm, sw) != want:
            raise SystemExit(f"{arm} {sw}: runmeta frames={frames_meta(arm, sw)!r}, expected {want}")
    for pair in PAIRS:
        x, y = load(pair[0], sw), load(pair[1], sw)
        if x is None or y is None:
            continue
        for label, joints in (("pose_body_px", BODY), ("pose_hands_px", HANDS)):
            d, common, nx, ny = pose_deltas(x, y, joints)
            emit(sw, pair, label + "_mean", statistics.fmean(d) if d else None, len(d))
            emit(sw, pair, label + "_p95", pct(d, 0.95), len(d))
        emit(sw, pair, "pose_frames_common", common, None)
        th, hd, both, onlyone = club_deltas(x, y)
        emit(sw, pair, "club_theta_deg_median", statistics.median(th) if th else None, both)
        emit(sw, pair, "club_theta_deg_p95", pct(th, 0.95), both)
        emit(sw, pair, "club_head_px_median", statistics.median(hd) if hd else None, both)
        emit(sw, pair, "club_head_px_p95", pct(hd, 0.95), both)
        emit(sw, pair, "club_frames_tracked_in_one_only", onlyone, both)
        agree, pos, launch, lf = ball_items(x, y)
        emit(sw, pair, "ball_found_agreement", agree)
        emit(sw, pair, "ball_pos_px_median", statistics.median(pos) if pos else None, len(pos))
        emit(sw, pair, "ball_launch_ms", launch)
        emit(sw, pair, "ball_launch_found_mismatch", float(lf[0] != lf[1]))
        ph, nx, ny = phase_deltas(x, y)
        for p, v in sorted(ph.items()):
            emit(sw, pair, "phase_P%02d_ms" % p, v)
        emit(sw, pair, "phase_count_diff", abs(nx - ny))
        md, nx, ny = metric_deltas(x, y)
        emit(sw, pair, "metric_value_count_diff", abs(nx - ny))   # scalars + per-phase samples
        kx = {m.get("key") for m in x.get("metrics", [])}
        ky = {m.get("key") for m in y.get("metrics", [])}
        emit(sw, pair, "metric_keys_only_in_first", len(kx - ky))
        emit(sw, pair, "metric_keys_only_in_second", len(ky - kx))
        for (k, where), v in sorted(md.items()):
            emit(sw, pair, "metric:%s:%s" % (k, where), v)
        sx, sy = x.get("score", {}).get("overall"), y.get("score", {}).get("overall")
        if isinstance(sx, (int, float)) and isinstance(sy, (int, float)):
            emit(sw, pair, "score_overall", abs(sx - sy))

os.makedirs(a.out, exist_ok=True)
with open(os.path.join(a.out, "raw_vs_mp4.csv"), "w", newline="") as f:
    w = csv.DictWriter(f, fieldnames=["swing", "pair", "item", "value", "n"])
    w.writeheader()
    w.writerows(rows)

# Summary: per item, per pair, over swings.
by = {}
for r in rows:
    if r["value"] == "":
        continue
    by.setdefault(r["item"], {}).setdefault(r["pair"], []).append(float(r["value"]))
pairs = ["%s-%s" % p for p in PAIRS]
with open(os.path.join(a.out, "raw_vs_mp4_summary.csv"), "w", newline="") as f:
    w = csv.writer(f)
    w.writerow(["item"] + [f"{p}_{s}" for p in pairs for s in ("n", "median", "p90")])
    for item in sorted(by):
        line = [item]
        for p in pairs:
            xs = by[item].get(p, [])
            line += [len(xs), round(statistics.median(xs), 4) if xs else "", round(pct(xs, 0.9), 4) if xs else ""]
        w.writerow(line)

# Per metric: the typical change beside the metric's typical size.
per = {}
for r in rows:
    if r["value"] == "" or not r["item"].startswith("metric:"):
        continue
    key = r["item"].split(":")[1]
    per.setdefault(key, {}).setdefault(r["pair"], []).append(float(r["value"]))
with open(os.path.join(a.out, "raw_vs_mp4_metrics.csv"), "w", newline="") as f:
    w = csv.writer(f)
    w.writerow(["metric", "median_abs_value"] + [f"{p}_{s}" for p in pairs for s in ("n", "median_delta", "p90_delta")])
    for key in sorted(per):
        mag = statistics.median(MAGNITUDE.get(key, [0.0]))
        line = [key, round(mag, 4)]
        for p in pairs:
            xs = per[key].get(p, [])
            line += [len(xs), round(statistics.median(xs), 4) if xs else "", round(pct(xs, 0.9), 4) if xs else ""]
        w.writerow(line)

print("swings:", len(swings), " arm coverage:", coverage)
print("rows:", len(rows))
