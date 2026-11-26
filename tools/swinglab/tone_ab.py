#!/usr/bin/env python3
"""tone_ab - #183's analysis A/B: the comparator and the verdict (plan sections 1, 3.2 and 6 step 7).

    python3 -I tools/swinglab/tone_ab.py --root <share>/scratch/i183-ab --out <folder>
    python3 -I tools/swinglab/tone_ab.py --self-test

Reads only the digests the studio PC's runner wrote (<root>/digests/<arm>/<id>.json), the manifest, strata.json
(written from B1 alone, before any curved arm ran) and the truth files of the input copies. Writes CSVs and
verdict.json into --out. Every rule is the plan's section 1, fixed before the run:

- Pairs: pose C1 against B1 (pose free); trackers PC against PB (the same pinned pose); the ball on the
  no-baseline pair (PBs/PCs) for the 80 swings with a stored baseline, and PB/PC for the other 68 (which never
  had one); metrics C1-B1 and PC-PB.
- Floor: B1-B2 (and C1-C2, PB-PB2 when those arms ran; an arm that did not run means the one-swing repeat was
  identical, floor 0). A change is beyond the floor when its 95 % session-bootstrap interval excludes zero
  and its median exceeds both the floor's 90th percentile and the margin.
- Yes-or-no outcomes as net flips: beyond the floor when |net| exceeds the baseline pair's own flips and 2.
- Windows on B1's phase times for every arm (P1 = Address, phase 0; P4 = Top, 2; P7 = Impact, 5).
"""
import argparse
import csv
import json
import math
import os
import random
import statistics
import sys

BODY = list(range(17))
TOP_JOINTS = list(range(5, 13))
DROP = 0.3
SEED = 183
NBOOT = 2000
FRAME_MS = 6.6
MARGIN = {"P1rel": 0.20, "P1count": 10, "P2rel": 0.10, "event_ms": FRAME_MS, "S1pp": 1.0, "S3": 1.0,
          "band_deg": 0.3, "hand_deg": 1.0, "B1pp": 2.0, "B2px": 0.1, "lm_rel": 0.10}
PH = {"P1": "0", "P4": "2", "P7": "5"}
TRUTH_P = {"P1": "p1_s", "P4": "p4_s", "P7": "p7_s"}
MEASURED_FO = {"ray", "band", "recon", "wedge", "seg"}
MEASURED_DTL = {"RAY", "SEG", "BAND"}


# ---------------------------------------------------------------- statistics

def median(xs):
    xs = [x for x in xs if x is not None and not (isinstance(x, float) and math.isnan(x))]
    return statistics.median(xs) if xs else None


def pct(xs, q):
    xs = sorted(x for x in xs if x is not None)
    if not xs:
        return None
    k = (len(xs) - 1) * q
    lo, hi = math.floor(k), math.ceil(k)
    return xs[lo] + (xs[hi] - xs[lo]) * (k - lo)


def boot(items, stat, n=NBOOT, seed=SEED):
    """95 % interval of stat(list of items) resampling whole sessions. items: [(session, payload)]."""
    by = {}
    for s, p in items:
        by.setdefault(s, []).append(p)
    keys = sorted(by)
    if len(keys) < 2:
        return None, None
    rng = random.Random(seed)
    vals = []
    for _ in range(n):
        pick = []
        for _k in keys:
            pick.extend(by[keys[rng.randrange(len(keys))]])
        v = stat(pick)
        if v is not None:
            vals.append(v)
    if len(vals) < n * 0.5:
        return None, None
    return pct(vals, 0.025), pct(vals, 0.975)


def judge(items, floor_vals, margin, better="lower"):
    """items: [(session, golfer, diff)] paired per swing (arm - baseline). Returns the judgement dict.
    better: 'lower' (a fall helps) or 'higher' (a rise helps)."""
    diffs = [d for _, _, d in items if d is not None]
    out = {"n": len(diffs), "median": median(diffs), "margin": margin}
    if not diffs:
        out.update(lo=None, hi=None, floorP90=None, beyond=False, direction="none")
        return out
    lo, hi = boot([(s, d) for s, _, d in items if d is not None], median)
    fl = pct([abs(f) for f in floor_vals if f is not None], 0.9) if floor_vals else 0.0
    fl = fl or 0.0
    m = out["median"]
    beyond = (lo is not None and hi is not None and (lo > 0 or hi < 0) and abs(m) > fl and abs(m) > margin)
    direction = "none"
    if beyond:
        good = (m < 0) if better == "lower" else (m > 0)
        direction = "better" if good else "worse"
    per_golfer = {}
    for g in sorted({g for _, g, _ in items}):
        gd = [d for _, gg, d in items if gg == g and d is not None]
        per_golfer[g] = {"n": len(gd), "median": median(gd)}
    out.update(lo=lo, hi=hi, floorP90=fl, beyond=beyond, direction=direction, perGolfer=per_golfer)
    out["golferSignsAgree"] = all((v["median"] or 0) * m > 0 for v in per_golfer.values() if v["n"] >= 5) if m else False
    return out


def net_flips(pairs_test, pairs_floor):
    """pairs: [(ok_baseline, ok_arm)] per swing. Net = newly succeed - newly fail."""
    up = sum(1 for a, b in pairs_test if not a and b)
    down = sum(1 for a, b in pairs_test if a and not b)
    fup = sum(1 for a, b in pairs_floor if a != b)
    net = up - down
    beyond = abs(net) > fup and abs(net) > 2
    return {"newlySucceed": up, "newlyFail": down, "net": net, "floorFlips": fup, "beyond": beyond,
            "direction": ("better" if net > 0 else "worse") if beyond else "none"}


# ---------------------------------------------------------------- per-swing measures

def frames(d, cam):
    p = ((d or {}).get("pose") or {}).get(cam) or {}
    return p.get("frames") or []


def top_drops(d, cam, t4):
    """(dropped joint-frames, joint-frames, mean conf) of joints 5-12 over P4 +- 50 ms."""
    n = k = 0
    cs = []
    for f in frames(d, cam):
        if abs(f[0] - t4) <= 50000:
            for j in TOP_JOINTS:
                c = f[1][3 * j + 2]
                n += 1
                k += c < DROP
                cs.append(c)
    return k, n, (sum(cs) / len(cs) if cs else None)


def dense_run(fr, t4):
    """The run of evenly spaced frames (5-8.5 ms apart) that holds P4, or the longest such run."""
    runs, cur = [], [0] if fr else []
    for i in range(1, len(fr)):
        dt = (fr[i][0] - fr[i - 1][0]) / 1000.0
        if 5.0 <= dt <= 8.5:
            cur.append(i)
        else:
            if len(cur) > 1:
                runs.append(cur)
            cur = [i]
    if len(cur) > 1:
        runs.append(cur)
    if not runs:
        return []
    for r in runs:
        if fr[r[0]][0] <= t4 <= fr[r[-1]][0]:
            return r
    return max(runs, key=len)


SG7 = [-2 / 21.0, 3 / 21.0, 6 / 21.0, 7 / 21.0, 6 / 21.0, 3 / 21.0, -2 / 21.0]   # quadratic, 7 points, centre value


def jitter(d, cam, t4, dims, conf_min=None):
    """Median |residual| (px) of body joints 0-16 from a 7-frame quadratic Savitzky-Golay fit, over the dense
    frames from the start of the dense span to P4 + 100 ms."""
    fr = frames(d, cam)
    if not fr or not dims:
        return None, 0
    w, h = dims
    run = [i for i in dense_run(fr, t4) if fr[i][0] <= t4 + 100000]
    res = []
    for c in range(3, len(run) - 3):
        idx = run[c - 3:c + 4]
        for j in BODY:
            if conf_min is not None and min(fr[i][1][3 * j + 2] for i in idx) < conf_min:
                continue
            xs = [fr[i][1][3 * j] * w for i in idx]
            ys = [fr[i][1][3 * j + 1] * h for i in idx]
            fx = sum(a * b for a, b in zip(SG7, xs))
            fy = sum(a * b for a, b in zip(SG7, ys))
            res.append(math.hypot(xs[3] - fx, ys[3] - fy))
    return (median(res), len(run)) if res else (None, len(run))


def dense_drops(d, cam, t4):
    fr = frames(d, cam)
    run = dense_run(fr, t4)
    n = k = 0
    for i in run:
        for j in TOP_JOINTS:
            n += 1
            k += fr[i][1][3 * j + 2] < DROP
    return k, n


def hands_conf(d, cam, t4):
    cs = [f[2] for f in frames(d, cam) if abs(f[0] - t4) <= 50000 and f[2] is not None]
    return sum(cs) / len(cs) if cs else None


def event_err(d, truth, key, t0):
    if not truth or d is None:
        return None
    want = (truth.get("events") or {}).get(TRUTH_P[key])
    got = (d.get("phases") or {}).get(PH[key])
    if want is None or got is None:
        return None
    return abs((got - t0) / 1000.0 - want * 1000.0)


def span_of(d):
    """The face-on tracker's span (trace summary spanLo..spanHi, frame indices) of a run, or None."""
    sm = ((d or {}).get("trace") or {}).get("summary") or {}
    lo, hi = sm.get("spanLo"), sm.get("spanHi")
    return (lo, hi) if isinstance(lo, int) and isinstance(hi, int) and hi > lo >= 0 else None


def shaft_fo(d, span=None):
    """S-1 to S-4 over the span frames (plan section 1.2). Both arms of a pair are counted over the BASELINE
    run's span (spanLo..spanHi), so a moved span cannot move the measure."""
    tr = (d or {}).get("trace")
    if not tr or not tr.get("lines"):
        return None
    lines = sorted(tr["lines"], key=lambda r: r[0])
    if span:
        lines = [r for r in lines if span[0] <= r[0] <= span[1]]
        if not lines:
            return None
    tiers = [r[1] for r in lines]
    meas = sum(t in MEASURED_FO for t in tiers)
    longest = cur = 0
    for t in tiers:
        cur = cur + 1 if t == "pred" else 0
        longest = max(longest, cur)
    counts = {}
    for t in tiers:
        counts[t] = counts.get(t, 0) + 1
    ref = (tr.get("summary") or {}).get("refused") or ""
    return {"frac": 100.0 * meas / len(tiers), "longestPred": longest, "refused": bool(ref), "refusedWhy": ref,
            "counts": counts, "rung": (tr.get("summary") or {}).get("projLenRung"), "n": len(tiers)}


def shaft_dtl(d, window=None):
    """The DTL tiers over B1's address-to-finish window (t_us), when given: the DTL trace carries no span."""
    tr = (d or {}).get("traceDtl")
    if not tr or not tr.get("tiers"):
        return None
    tl = sorted(tr["tiers"], key=lambda r: r[0] or 0)
    if window:
        tl = [r for r in tl if r[0] is not None and window[0] <= r[0] <= window[1]]
        if not tl:
            return None
    tiers = [t for _, t in tl]
    meas = sum(t in MEASURED_DTL for t in tiers)
    longest = cur = 0
    seen = False
    for t in tiers:
        if t in MEASURED_DTL:
            seen = True
            cur = 0
        elif seen:
            cur += 1
            longest = max(longest, cur)
    counts = {}
    for t in tiers:
        counts[t] = counts.get(t, 0) + 1
    return {"frac": 100.0 * meas / len(tiers), "longestUnmeasured": longest, "counts": counts, "n": len(tiers)}


def wrap(a):
    return (a + 180.0) % 360.0 - 180.0


def band_errors(ref, d):
    """On the reference run's band-tier frames: the arm's ray (theta_dp), segment (seg_theta) and published
    (theta_out) angles against the reference's band lock (band_theta), degrees; per-swing medians."""
    rt = (ref or {}).get("trace") or {}
    at = (d or {}).get("trace") or {}
    lock = {r[0]: r[5] for r in rt.get("lines") or [] if r[1] == "band" and r[5] is not None}
    if not lock:
        return None
    arm = {r[0]: r for r in at.get("lines") or []}
    ray, seg, pub = [], [], []
    for f, bt in lock.items():
        r = arm.get(f)
        if not r:
            continue
        if r[3] is not None:
            ray.append(abs(wrap(r[3] - bt)))
        if r[4] is not None:
            seg.append(abs(wrap(r[4] - bt)))
        if r[2] is not None:
            pub.append(abs(wrap(r[2] - bt)))
    return {"ray": median(ray), "seg": median(seg), "pub": median(pub), "nBand": len(lock)}


def hand_errors(d, truth):
    """The published track's angle against the hand marks, seen (P1-P6) and inferred (P7-P10) apart, degrees;
    segment_truth_score.py's matching (nearest sample within 4 ms)."""
    c = (d or {}).get("club") or {}
    sm = [s for s in c.get("samples") or [] if s[1] is not None]
    if not sm or not truth or not truth.get("shaft") or len(truth["shaft"]) > 120:
        return None
    ev = truth.get("events") or {}
    t0 = ev.get("t0_us", 0)
    pm = {int(k[1:-2]): t0 + v * 1e6 for k, v in ev.items() if k.startswith("p") and k.endswith("_s")}
    seen, inf = [], []
    for m in truth["shaft"]:
        s = min(sm, key=lambda q: abs(q[0] - m["t_us"]))
        if abs(s[0] - m["t_us"]) > 4000:
            continue
        p = min(pm, key=lambda k: abs(pm[k] - m["t_us"])) if pm else None
        if p is not None and abs(pm[p] - m["t_us"]) > 4000:
            p = None
        e = abs(wrap(math.degrees(s[1]) - math.degrees(m["theta"])))
        (inf if (p or 0) >= 7 else seen).append(e)
    return {"seen": median(seen), "inferred": median(inf), "nSeen": len(seen), "nInf": len(inf)}


def ball_measures(d, t1, t4, t7, dims):
    b = (d or {}).get("ball") or {}
    sm = b.get("samples") or []
    win = [s for s in sm if t1 <= s[0] <= t7]
    det = 100.0 * sum(s[1] for s in win) / len(win) if win else None
    still = [s for s in sm if t1 <= s[0] <= t4 and s[1]]
    sd = None
    if len(still) >= 3 and dims:
        w, h = dims
        xs = [s[2] * w for s in still]
        ys = [s[3] * h for s in still]
        sd = math.sqrt(statistics.pvariance(xs) + statistics.pvariance(ys))
    lt = b.get("launchTUs")
    return {"det": det, "sd": sd, "launch": bool(lt and lt > 0), "launchTUs": lt, "valid": b.get("valid"),
            "nWin": len(win)}


def metric_samples(d):
    out = {}
    for k, m in ((d or {}).get("metrics") or {}).items():
        sig = m.get("sigma")
        if m.get("scalar") is not None:
            out[(k, "scalar")] = (m["scalar"], sig)
        for ph, v in (m.get("ph") or {}).items():
            out[(k, "P" + ph)] = (v, (m.get("phSigma") or {}).get(ph, sig))
    return out


# ---------------------------------------------------------------- the comparison

def load_digests(root):
    dd = {}
    base = os.path.join(root, "digests")
    for arm in sorted(os.listdir(base)) if os.path.isdir(base) else []:
        for f in sorted(os.listdir(os.path.join(base, arm))):
            if f.endswith(".json"):
                with open(os.path.join(base, arm, f)) as fh:
                    dd.setdefault(arm, {})[f[:-5]] = json.load(fh)
    return dd


def good(d):
    return d is not None and d.get("rc") == 0 and (d.get("checks") or {}).get("ok")


def write_csv(path, rows):
    if not rows:
        open(path, "w").close()
        return
    cols = []
    for r in rows:
        for k in r:
            if k not in cols:
                cols.append(k)
    with open(path, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=cols)
        w.writeheader()
        for r in rows:
            w.writerow({k: (round(v, 5) if isinstance(v, float) else v) for k, v in r.items()})


def pose_stage(man, dg, strata, truths, out):
    rows, verdict = [], {}
    B1, B2, C1, C2 = (dg.get(a, {}) for a in ("B1", "B2", "C1", "C2"))
    dark = strata_split(strata, "torsoMedianLuma")
    light = strata_split(strata, "sceneMedianLuma")
    for cam in ("faceOn", "dtl"):
        items1, items2, f1, f2 = [], [], [], []
        pool = {"B1": [0, 0], "C1": [0, 0], "B2": [0, 0]}
        pool_items = []
        ev_items = {k: [] for k in PH}
        for r in man:
            if cam == "dtl" and not r["twoCamera"]:
                continue
            sid = r["id"]
            b1, b2, c1, c2 = B1.get(sid), B2.get(sid), C1.get(sid), C2.get(sid)
            if not (good(b1) and good(c1)):
                rows.append({"id": sid, "camera": cam, "why": "missing or failed run"})
                continue
            t4 = (b1.get("phases") or {}).get(PH["P4"])
            if t4 is None:
                rows.append({"id": sid, "camera": cam, "why": "B1 has no P4"})
                continue
            dims = (b1.get("dims") or {}).get(cam)
            kb, nb, cb = top_drops(b1, cam, t4)
            kc, nc, cc = top_drops(c1, cam, t4)
            row = {"id": sid, "camera": cam, "golfer": r["golfer"], "session": r["session"],
                   "darkTop": dark.get(sid), "light": light.get(sid),
                   "B1_dropTop": kb, "B1_jfTop": nb, "C1_dropTop": kc, "C1_jfTop": nc,
                   "B1_confTop": cb, "C1_confTop": cc}
            pool["B1"][0] += kb; pool["B1"][1] += nb
            pool["C1"][0] += kc; pool["C1"][1] += nc
            pool_items.append((r["session"], (kb, nb, kc, nc, dark.get(sid), r["golfer"])))
            jb, nrb = jitter(b1, cam, t4, dims)
            jc, nrc = jitter(c1, cam, t4, dims)
            jb_c, _ = jitter(b1, cam, t4, dims, DROP)
            jc_c, _ = jitter(c1, cam, t4, dims, DROP)
            row.update(B1_jitterPx=jb, C1_jitterPx=jc, B1_denseFrames=nrb, C1_denseFrames=nrc,
                       B1_jitterConfPx=jb_c, C1_jitterConfPx=jc_c,
                       B1_hands=hands_conf(b1, cam, t4), C1_hands=hands_conf(c1, cam, t4))
            kdb, ndb = dense_drops(b1, cam, t4)
            kdc, ndc = dense_drops(c1, cam, t4)
            row.update(B1_dropDense=kdb, B1_jfDense=ndb, C1_dropDense=kdc, C1_jfDense=ndc)
            rel = (jc - jb) / jb if (jb and jc is not None) else None
            row["jitterRel"] = rel
            items2.append((r["session"], r["golfer"], rel, dark.get(sid)))
            if good(b2):
                jb2, _ = jitter(b2, cam, t4, dims)
                k2, n2, _ = top_drops(b2, cam, t4)
                pool["B2"][0] += k2; pool["B2"][1] += n2
                f2.append((jb2 - jb) / jb if (jb and jb2 is not None) else None)
                row["B2_jitterPx"] = jb2
                row["B2_dropTop"] = k2
            if good(c2):
                jc2, _ = jitter(c2, cam, t4, dims)
                row["C2_jitterPx"] = jc2
            if cam == "faceOn":
                t = truths.get(sid)
                t0 = (t or {}).get("events", {}).get("t0_us", 0)
                for k in PH:
                    eb, ec = event_err(b1, t, k, t0), event_err(c1, t, k, t0)
                    row["B1_err" + k + "ms"] = eb
                    row["C1_err" + k + "ms"] = ec
                    if eb is not None and ec is not None:
                        ev_items[k].append((r["session"], r["golfer"], ec - eb))
            rows.append(row)
        # P-1, pooled
        rb = pool["B1"][0] / pool["B1"][1] if pool["B1"][1] else None
        rc = pool["C1"][0] / pool["C1"][1] if pool["C1"][1] else None
        rb2 = pool["B2"][0] / pool["B2"][1] if pool["B2"][1] else None

        def pooled_diff(items):
            kb = sum(p[0] for p in items); nb = sum(p[1] for p in items)
            kc = sum(p[2] for p in items); nc = sum(p[3] for p in items)
            return (kc / nc - kb / nb) if nb and nc else None
        lo, hi = boot(pool_items, pooled_diff)
        p1 = {"B1pooled": rb, "C1pooled": rc, "B2pooled": rb2, "B1count": pool["B1"][0], "C1count": pool["C1"][0],
              "jointFrames": [pool["B1"][1], pool["C1"][1]], "diff": (rc - rb) if rb is not None and rc is not None else None,
              "lo": lo, "hi": hi, "floor": abs(rb2 - rb) if rb2 is not None and rb is not None else 0.0}
        p1["testable"] = pool["B1"][0] >= MARGIN["P1count"]
        if p1["testable"] and p1["diff"] is not None:
            need = max(MARGIN["P1rel"] * rb, p1["floor"])
            cnt = abs(pool["C1"][0] - pool["B1"][0]) >= MARGIN["P1count"]
            p1["beyond"] = bool(lo is not None and (lo > 0 or hi < 0) and abs(p1["diff"]) > need and cnt)
            p1["direction"] = ("better" if p1["diff"] < 0 else "worse") if p1["beyond"] else "none"
        else:
            p1["beyond"], p1["direction"] = False, "not testable" if not p1["testable"] else "none"
        # strata and golfers for P-1
        p1["darkTop"] = {}
        for lab in ("dark", "mid", "bright"):
            sub = [it for it in pool_items if it[1][4] == lab]
            p1["darkTop"][lab] = {"n": len(sub), "diff": pooled_diff([s[1] for s in sub]),
                                  "B1count": sum(s[1][0] for s in sub), "C1count": sum(s[1][2] for s in sub)}
        p1["perGolfer"] = {}
        for g in sorted({it[1][5] for it in pool_items}):
            sub = [it[1] for it in pool_items if it[1][5] == g]
            p1["perGolfer"][g] = {"n": len(sub), "diff": pooled_diff(sub), "B1count": sum(s[0] for s in sub),
                                  "C1count": sum(s[2] for s in sub)}
        p2 = judge([(s, g, d) for s, g, d, _ in items2], f2, MARGIN["P2rel"], "lower")
        p2["darkTop"] = {lab: {"n": len([1 for *_, dk in items2 if dk == lab]),
                               "median": median([d for _, _, d, dk in items2 if dk == lab])}
                         for lab in ("dark", "mid", "bright")}
        p2["light"] = {}
        for lab in ("dim", "middle", "bright"):
            ids = {k for k, v in light.items() if v == lab}
            p2["light"][lab] = median([row["jitterRel"] for row in rows if row.get("camera") == cam
                                       and row.get("id") in ids and row.get("jitterRel") is not None])
        events = {k: judge(v, [], MARGIN["event_ms"], "lower") for k, v in ev_items.items()} if cam == "faceOn" else {}
        truth_ok = all(not (e["n"] and e["median"] is not None and e["median"] > MARGIN["event_ms"]) for e in events.values())
        dark_holds = (p1["darkTop"]["dark"]["diff"] is not None and p1["darkTop"]["dark"]["diff"] < 0) \
            if p1["testable"] else ((p2["darkTop"]["dark"]["median"] or 0) < 0)
        golfer_ok = all((v["diff"] or 0) < 0 for v in p1["perGolfer"].values() if v["n"] >= 5) if p1["testable"] \
            else p2.get("golferSignsAgree", False)
        if p1["direction"] == "worse" or p2["direction"] == "worse" or not truth_ok:
            v = "hurts"
        elif ((p1["direction"] == "better" or not p1["testable"]) and p2["direction"] != "worse"
              and (p1["testable"] or p2["direction"] == "better") and dark_holds and golfer_ok and truth_ok):
            v = "helps"
        else:
            v = "neutral"
        verdict[cam] = {"P1": p1, "P2": p2, "events": events, "truthHolds": truth_ok, "darkTopHolds": dark_holds,
                        "golferSignsAgree": golfer_ok, "verdict": v}
    write_csv(os.path.join(out, "pose.csv"), rows)
    vs = [verdict[c]["verdict"] for c in verdict]
    overall = "hurts" if "hurts" in vs else ("helps" if verdict["faceOn"]["verdict"] == "helps" and "hurts" not in vs
                                            else "neutral")
    return {"cameras": verdict, "verdict": overall}


def strata_split(strata, key):
    vals = sorted((r[key], r["id"]) for r in strata if r.get(key) is not None)
    n = len(vals)
    labs = ("dark", "mid", "bright") if key == "torsoMedianLuma" else ("dim", "middle", "bright")
    out = {}
    for i, (_, sid) in enumerate(vals):
        out[sid] = labs[min(2, i * 3 // n)]
    return out


def shaft_stage(man, dg, truths, out):
    PB, PC, PB2 = (dg.get(a, {}) for a in ("PB", "PC", "PB2"))
    rows = []
    s1, s3, s1d, s3d, flips, flips_f, band, hand = [], [], [], [], [], [], {"ray": [], "seg": [], "pub": []}, []
    f1, f3, f1d, f3d = [], [], [], []
    for r in man:
        sid = r["id"]
        pb, pc, pb2 = PB.get(sid), PC.get(sid), PB2.get(sid)
        if not (good(pb) and good(pc)):
            rows.append({"id": sid, "why": "missing or failed run"})
            continue
        span = span_of(pb)
        a, b = shaft_fo(pb, span), shaft_fo(pc, span)
        b1 = dg.get("B1", {}).get(sid) or {}
        ph = b1.get("phases") or {}
        win = (ph["0"], ph["7"]) if "0" in ph and "7" in ph else None
        row = {"id": sid, "golfer": r["golfer"], "session": r["session"], "spanLo": span and span[0],
               "spanHi": span and span[1], "PB_spanMovedInPC": span != span_of(pc)}
        if a and b:
            row.update(PB_frac=a["frac"], PC_frac=b["frac"], PB_longestPred=a["longestPred"],
                       PC_longestPred=b["longestPred"], PB_refused=a["refusedWhy"], PC_refused=b["refusedWhy"],
                       PB_rung=a["rung"], PC_rung=b["rung"], PB_tiers=json.dumps(a["counts"], sort_keys=True),
                       PC_tiers=json.dumps(b["counts"], sort_keys=True), PB_n=a["n"], PC_n=b["n"])
            s1.append((r["session"], r["golfer"], b["frac"] - a["frac"]))
            s3.append((r["session"], r["golfer"], b["longestPred"] - a["longestPred"]))
            flips.append((not a["refused"], not b["refused"]))
            if good(pb2):
                c = shaft_fo(pb2, span)
                if c:
                    f1.append(c["frac"] - a["frac"])
                    f3.append(c["longestPred"] - a["longestPred"])
                    flips_f.append((not a["refused"], not c["refused"]))
            be = band_errors(pb, pb), band_errors(pb, pc)
            if be[0] and be[1]:
                row.update(nBand=be[0]["nBand"], PB_bandRay=be[0]["ray"], PC_bandRay=be[1]["ray"],
                           PB_bandSeg=be[0]["seg"], PC_bandSeg=be[1]["seg"], PB_bandPub=be[0]["pub"],
                           PC_bandPub=be[1]["pub"])
                for k in band:
                    if be[0][k] is not None and be[1][k] is not None:
                        band[k].append((r["session"], r["golfer"], be[1][k] - be[0][k]))
            elif truths.get(sid):
                he = hand_errors(pb, truths[sid]), hand_errors(pc, truths[sid])
                if he[0] and he[1]:
                    row.update(PB_handSeen=he[0]["seen"], PC_handSeen=he[1]["seen"], PB_handInf=he[0]["inferred"],
                               PC_handInf=he[1]["inferred"], nSeen=he[0]["nSeen"])
                    if he[0]["seen"] is not None and he[1]["seen"] is not None:
                        hand.append((r["session"], r["golfer"], he[1]["seen"] - he[0]["seen"]))
        if r["twoCamera"]:
            a, b = shaft_dtl(pb, win), shaft_dtl(pc, win)
            if a and b:
                row.update(PB_dtlFrac=a["frac"], PC_dtlFrac=b["frac"], PB_dtlLongest=a["longestUnmeasured"],
                           PC_dtlLongest=b["longestUnmeasured"], PB_dtlTiers=json.dumps(a["counts"], sort_keys=True),
                           PC_dtlTiers=json.dumps(b["counts"], sort_keys=True))
                s1d.append((r["session"], r["golfer"], b["frac"] - a["frac"]))
                s3d.append((r["session"], r["golfer"], b["longestUnmeasured"] - a["longestUnmeasured"]))
                if good(pb2):
                    c = shaft_dtl(pb2, win)
                    if c:
                        f1d.append(c["frac"] - a["frac"])
                        f3d.append(c["longestUnmeasured"] - a["longestUnmeasured"])
        rows.append(row)
    write_csv(os.path.join(out, "shaft.csv"), rows)
    res = {"faceOn": {"S1": judge(s1, f1, MARGIN["S1pp"], "higher"), "S2": net_flips(flips, flips_f),
                      "S3": judge(s3, f3, MARGIN["S3"], "lower")},
           "dtl": {"S1": judge(s1d, f1d, MARGIN["S1pp"], "higher"), "S3": judge(s3d, f3d, MARGIN["S3"], "lower")},
           "truth": {"band": {k: judge(v, [], MARGIN["band_deg"], "lower") for k, v in band.items()},
                     "handSeen": judge(hand, [], MARGIN["hand_deg"], "lower")}}
    bt = res["truth"]
    truth_ok = all(not (bt["band"][k]["n"] and bt["band"][k]["median"] is not None
                        and bt["band"][k]["median"] > MARGIN["band_deg"]) for k in ("ray", "seg"))
    truth_ok = truth_ok and not (bt["handSeen"]["n"] and (bt["handSeen"]["median"] or 0) > MARGIN["hand_deg"])
    res["truthHolds"] = truth_ok
    for cam in ("faceOn", "dtl"):
        c = res[cam]
        worse = c["S1"]["direction"] == "worse" or c["S3"]["direction"] == "worse" or \
            (cam == "faceOn" and c["S2"]["direction"] == "worse")
        better = c["S1"]["direction"] == "better" or c["S3"]["direction"] == "better"
        c["verdict"] = "hurts" if worse or (cam == "faceOn" and not truth_ok) else (
            "helps" if better and (cam == "dtl" or c["S2"]["direction"] != "worse") and truth_ok else "neutral")
    vs = (res["faceOn"]["verdict"], res["dtl"]["verdict"])
    res["verdict"] = "hurts" if "hurts" in vs else ("helps" if "helps" in vs else "neutral")
    return res


def ball_stage(man, dg, out):
    rows = []
    det, sd, fl, det_wb, sd_wb, fl_wb = [], [], [], [], [], []
    both_fail = 0
    nb = 0
    flips_detail = []
    for r in man:
        sid = r["id"]
        b1 = dg.get("B1", {}).get(sid)
        if not good(b1):
            continue
        ph = b1.get("phases") or {}
        t1, t4, t7 = ph.get(PH["P1"]), ph.get(PH["P4"]), ph.get(PH["P7"])
        if None in (t1, t4, t7):
            rows.append({"id": sid, "why": "B1 lacks P1, P4 or P7"})
            continue
        dims = (b1.get("dims") or {}).get("faceOn")
        pa, pc = ("PBs", "PCs") if r["baseline"] else ("PB", "PC")
        x, y = dg.get(pa, {}).get(sid), dg.get(pc, {}).get(sid)
        row = {"id": sid, "golfer": r["golfer"], "pair": pa + "/" + pc, "storedBaseline": r["baseline"]}
        if good(x) and good(y):
            a, b = ball_measures(x, t1, t4, t7, dims), ball_measures(y, t1, t4, t7, dims)
            row.update(base_det=a["det"], curved_det=b["det"], base_sd=a["sd"], curved_sd=b["sd"],
                       base_launch=a["launch"], curved_launch=b["launch"])
            if a["det"] is not None and b["det"] is not None:
                det.append((r["session"], r["golfer"], b["det"] - a["det"]))
            if a["sd"] is not None and b["sd"] is not None:
                sd.append((r["session"], r["golfer"], b["sd"] - a["sd"]))
            fl.append((a["launch"], b["launch"]))
            row.update(base_launchMinusImpactMs=(a["launchTUs"] - t7) / 1000.0 if a["launch"] else None,
                       curved_launchMinusImpactMs=(b["launchTUs"] - t7) / 1000.0 if b["launch"] else None)
            if a["launch"] != b["launch"]:
                flips_detail.append((a["launch"], b["launch"], row["base_launchMinusImpactMs"],
                                     row["curved_launchMinusImpactMs"]))
            if r["baseline"]:
                nb += 1
                if not a["launch"] and not b["launch"] and (a["det"] or 0) < 10 and (b["det"] or 0) < 10:
                    both_fail += 1
        if r["baseline"]:
            x, y = dg.get("PB", {}).get(sid), dg.get("PC", {}).get(sid)
            if good(x) and good(y):
                a, b = ball_measures(x, t1, t4, t7, dims), ball_measures(y, t1, t4, t7, dims)
                row.update(wb_base_det=a["det"], wb_curved_det=b["det"], wb_base_sd=a["sd"], wb_curved_sd=b["sd"],
                           wb_base_launch=a["launch"], wb_curved_launch=b["launch"])
                if a["det"] is not None and b["det"] is not None:
                    det_wb.append((r["session"], r["golfer"], b["det"] - a["det"]))
                if a["sd"] is not None and b["sd"] is not None:
                    sd_wb.append((r["session"], r["golfer"], b["sd"] - a["sd"]))
                fl_wb.append((a["launch"], b["launch"]))
        rows.append(row)
    write_csv(os.path.join(out, "ball.csv"), rows)
    res = {"noBaseline": {"B1": judge(det, [], MARGIN["B1pp"], "higher"), "B2": judge(sd, [], MARGIN["B2px"], "lower"),
                          "B3": net_flips(fl, [])},
           "withBaseline": {"B1": judge(det_wb, [], MARGIN["B1pp"], "higher"),
                            "B2": judge(sd_wb, [], MARGIN["B2px"], "lower"), "B3": net_flips(fl_wb, [])},
           "noBaselineBothFail": [both_fail, nb]}
    # Reported, not deciding: how many swings' detection moved, whether the still-ball spread can move at all,
    # and whether each flipped launch sits near impact (within 3 frames, 20 ms) - a launch far from impact is
    # not a ball leaving the club.
    moved = [d for _, _, d in det if d != 0]
    res["noBaseline"]["B1moved"] = {"swings": len(moved), "up": sum(d > 0 for d in moved),
                                    "down": sum(d < 0 for d in moved), "min": min(moved) if moved else None,
                                    "max": max(moved) if moved else None}
    res["noBaseline"]["B2nonzeroBaselineSpread"] = sum(1 for rr in rows if (rr.get("base_sd") or 0) > 0)
    res["noBaseline"]["B3flips"] = [{"base": a, "curved": b, "baseMinusImpactMs": x, "curvedMinusImpactMs": y,
                                     "nearImpact": (abs(y) <= 20) if b else (abs(x) <= 20)}
                                    for a, b, x, y in flips_detail]
    c = res["noBaseline"]
    worse = any(c[k]["direction"] == "worse" for k in ("B1", "B2", "B3"))
    better = any(c[k]["direction"] == "better" for k in ("B1", "B2", "B3"))
    res["notTestableOnStoredBaselineSwings"] = nb > 0 and both_fail > nb / 2
    res["verdict"] = "hurts" if worse else ("helps" if better else "neutral")
    return res


def metric_stage(man, dg, out):
    rows = []
    per_key = {}
    lm_rows = []
    for arm_b, arm_c, arm_f in (("B1", "C1", "B2"), ("PB", "PC", "PB2")):
        for r in man:
            sid = r["id"]
            x, y, f = dg.get(arm_b, {}).get(sid), dg.get(arm_c, {}).get(sid), dg.get(arm_f, {}).get(sid)
            if not (good(x) and good(y)):
                continue
            mx, my = metric_samples(x), metric_samples(y)
            mf = metric_samples(f) if good(f) else {}
            for k in set(mx) | set(my):
                st = per_key.setdefault((arm_b + "-" + arm_c, k[0], k[1]),
                                        {"n": 0, "flagged": 0, "appeared": 0, "disappeared": 0, "noSigmaMoved": 0,
                                         "absDelta": [], "sigma": []})
                st["n"] += 1
                if k not in mx:
                    st["appeared"] += 1
                    st["flagged"] += 1
                    continue
                if k not in my:
                    st["disappeared"] += 1
                    st["flagged"] += 1
                    continue
                (vx, sx), (vy, _) = mx[k], my[k]
                dlt = abs(vy - vx)
                floor = abs(mf[k][0] - vx) if k in mf else 0.0
                st["absDelta"].append(dlt)
                if sx is None:
                    if dlt > floor:
                        st["noSigmaMoved"] += 1
                else:
                    st["sigma"].append(sx)
                    if dlt > max(sx, floor):
                        st["flagged"] += 1
        # the launch-monitor check, clubhead speed at impact
    lm = {}
    for arm_b, arm_c in (("B1", "C1"), ("PB", "PC")):
        pairs = []
        for r in man:
            if r.get("lmClubheadMph") is None:
                continue
            x, y = dg.get(arm_b, {}).get(r["id"]), dg.get(arm_c, {}).get(r["id"])
            if not (good(x) and good(y)):
                continue
            lmv = r["lmClubheadMph"] - (2.0 if r.get("lmKind") == "gcquad" else 0.0)
            vx = metric_samples(x).get(("clubheadSpeed", "P5"), (None,))[0]
            vy = metric_samples(y).get(("clubheadSpeed", "P5"), (None,))[0]
            lm_rows.append({"pair": arm_b + "-" + arm_c, "id": r["id"], "lmCorrected": lmv, "base": vx, "curved": vy})
            if vx is not None and vy is not None:
                pairs.append((r["session"], (vx - lmv, vy - lmv)))

        def width(items, i):
            e = [p[i] for p in items]
            return 2 * 1.96 * statistics.pstdev(e) if len(e) >= 3 else None

        def dwidth(items):
            a, b = width(items, 0), width(items, 1)
            return (b - a) / a if a else None
        wa, wb = width([p for _, p in pairs], 0), width([p for _, p in pairs], 1)
        lo, hi = boot(pairs, dwidth)
        rel = (wb - wa) / wa if wa else None
        fails = rel is not None and rel > MARGIN["lm_rel"] and lo is not None and lo > 0
        lm[arm_b + "-" + arm_c] = {"n": len(pairs), "biasBase": median([p[0] for _, p in pairs]),
                                   "biasCurved": median([p[1] for _, p in pairs]), "loaWidthBase": wa,
                                   "loaWidthCurved": wb, "relChange": rel, "lo": lo, "hi": hi, "fails": fails}
    for (pair, key, where), st in sorted(per_key.items()):
        rows.append({"pair": pair, "metric": key, "sample": where, "swings": st["n"], "flagged": st["flagged"],
                     "appeared": st["appeared"], "disappeared": st["disappeared"],
                     "movedNoSigma": st["noSigmaMoved"], "medianAbsDelta": median(st["absDelta"]),
                     "p90AbsDelta": pct(st["absDelta"], 0.9), "medianSigma": median(st["sigma"]),
                     "sigma": "recorded" if st["sigma"] else "not characterised"})
    write_csv(os.path.join(out, "metrics.csv"), rows)
    write_csv(os.path.join(out, "lm.csv"), lm_rows)
    return {"lm": lm, "keys": len({(r["metric"], r["sample"]) for r in rows}),
            "flaggedSamples": {p: sum(r["flagged"] for r in rows if r["pair"] == p) for p in ("B1-C1", "PB-PC")},
            "comparedSamples": {p: sum(r["swings"] for r in rows if r["pair"] == p) for p in ("B1-C1", "PB-PC")},
            "fails": lm.get("B1-C1", {}).get("fails", False)}


def floors(man, dg):
    out = {}
    for a, b in (("B1", "B2"), ("C1", "C2"), ("PB", "PB2")):
        A, B = dg.get(a, {}), dg.get(b, {})
        if not B:
            out[a + "-" + b] = "not run"
            continue
        same = diff = 0
        blocks = {}
        for sid, y in B.items():
            x = A.get(sid)
            if not (good(x) and good(y)):
                continue
            if x.get("contentSha1") == y.get("contentSha1"):
                same += 1
            else:
                diff += 1
                for k, v in (x.get("blockSha1") or {}).items():
                    if (y.get("blockSha1") or {}).get(k) != v:
                        blocks[k] = blocks.get(k, 0) + 1
        out[a + "-" + b] = {"identical": same, "different": diff, "blocksThatMoved": blocks}
    return out


def run_failures(man, dg):
    out = {}
    for arm, d in sorted(dg.items()):
        bad = [(sid, (x.get("checks") or {}).get("why")) for sid, x in sorted(d.items()) if not good(x)]
        out[arm] = {"runs": len(d), "failed": len(bad), "examples": bad[:8]}
    return out


def hist_check(dg):
    """Every curved run's first-frame histogram is its baseline's mapped through the table (C1 against B1,
    PC against PB, PCs against PBs)."""
    lut = [min(255, int(255.0 * (v / 255.0) ** 0.80 + 0.5)) for v in range(256)]
    res = {}
    for b, c in (("B1", "C1"), ("PB", "PC"), ("PBs", "PCs")):
        ok = bad = 0
        for sid, y in dg.get(c, {}).items():
            x = dg.get(b, {}).get(sid)
            try:
                hb = {k["alias"]: k["firstFrameHist"] for k in x["runmeta"]["tone"]["cameras"]}
                hc = {k["alias"]: k["firstFrameHist"] for k in y["runmeta"]["tone"]["cameras"]}
            except (TypeError, KeyError):
                bad += 1
                continue
            for al, h in hb.items():
                m = [0] * 256
                for v, n in enumerate(h):
                    m[lut[v]] += n
                if m == hc.get(al) and h != hc.get(al):
                    ok += 1
                else:
                    bad += 1
        res[c] = {"camerasOk": ok, "camerasBad": bad}
    return res


def compare(root, out):
    os.makedirs(out, exist_ok=True)
    man = json.load(open(os.path.join(root, "manifest.json")))["swings"]
    strata = json.load(open(os.path.join(root, "strata.json")))["rows"]
    truths = {}
    for r in man:
        p = os.path.join(root, "truth", r["id"] + ".json")      # kept beside the digests
        if not os.path.exists(p):
            p = os.path.join(root, "in", r["session"], r["swing"], "truth.json")
        if os.path.exists(p):
            truths[r["id"]] = json.load(open(p))
    dg = load_digests(root)
    dark, light = strata_split(strata, "torsoMedianLuma"), strata_split(strata, "sceneMedianLuma")
    srows = []
    for s in strata:
        s = dict(s)
        s["darkTop"] = dark.get(s["id"])
        s["light"] = light.get(s["id"])
        mr = next((r for r in man if r["id"] == s["id"]), {})
        s["taped"] = None
        pbd = dg.get("PB", {}).get(s["id"])
        if pbd and pbd.get("trace"):
            s["taped"] = any(l[1] == "band" for l in pbd["trace"]["lines"])
        s["exposure"] = "6.57 ms (limit)" if s["id"][:10] >= "2026-07-05" else "not recorded"
        s["pinSource"] = mr.get("pinSource")
        srows.append(s)
    write_csv(os.path.join(out, "strata.csv"), srows)
    v = {"floors": floors(man, dg), "runs": run_failures(man, dg), "histogram": hist_check(dg)}
    v["pose"] = pose_stage(man, dg, strata, truths, out)
    v["shaft"] = shaft_stage(man, dg, truths, out)
    v["ball"] = ball_stage(man, dg, out)
    v["metrics"] = metric_stage(man, dg, out)
    p, s, b = v["pose"]["verdict"], v["shaft"]["verdict"], v["ball"]["verdict"]
    if v["metrics"]["fails"]:
        overall = "hurts"
    elif p == "helps" and s in ("helps", "neutral") and b in ("helps", "neutral"):
        overall = "helps"
    elif p == "helps" and "hurts" in (s, b):
        overall = "mixed (counts as hurts)"
    elif "hurts" in (p, s, b):
        overall = "hurts"
    else:
        overall = "neutral"
    v["overall"] = overall
    with open(os.path.join(out, "verdict.json"), "w") as f:
        json.dump(v, f, indent=1, sort_keys=True, default=str)
    write_csv(os.path.join(out, "verdict.csv"), [{"stage": "pose", "verdict": p}, {"stage": "shaft", "verdict": s},
                                                 {"stage": "ball", "verdict": b},
                                                 {"stage": "clubhead speed vs LM", "verdict": "fails" if v["metrics"]["fails"] else "holds"},
                                                 {"stage": "overall", "verdict": overall}])
    print(json.dumps({"pose": p, "shaft": s, "ball": b, "lmFails": v["metrics"]["fails"], "overall": overall}))
    return 0


# ---------------------------------------------------------------- self-test

def self_test():
    bad = 0

    def check(name, got, want):
        nonlocal bad
        ok = got == want
        bad += not ok
        print("%-4s %-60s got %r, want %r" % ("ok" if ok else "FAIL", name, got, want))
    # judge: a clear fall beyond the margin across sessions
    items = [("s%d" % (i % 6), "A" if i % 2 else "B", -0.3 + 0.01 * (i % 3)) for i in range(30)]
    j = judge(items, [0.0] * 30, 0.10, "lower")
    check("judge: a consistent -30 % fall is beyond and better", (j["beyond"], j["direction"]), (True, "better"))
    j = judge(items, [0.5] * 30, 0.10, "lower")
    check("judge: the same fall inside a 50 % floor is not beyond", j["beyond"], False)
    j = judge([("s%d" % (i % 6), "A", 0.05) for i in range(30)], [0.0] * 30, 0.10, "lower")
    check("judge: a 5 % rise under a 10 % margin is not beyond", j["beyond"], False)
    mixed = [("s%d" % (i % 6), "A", (-1) ** i * 0.3) for i in range(30)]
    j = judge(mixed, [], 0.10, "lower")
    check("judge: a change whose interval holds zero is not beyond", j["beyond"], False)
    j = judge([("s%d" % (i % 6), "A", 2.0) for i in range(30)], [], 1.0, "higher")
    check("judge: a rise where higher is better reads better", j["direction"], "better")
    check("net flips: 2 up 0 down is not beyond (needs > 2)", net_flips([(False, True)] * 2 + [(True, True)] * 9, [])["beyond"], False)
    check("net flips: 3 down is beyond and worse", net_flips([(True, False)] * 3, [])["direction"], "worse")
    check("net flips: 3 up against 4 floor flips is not beyond",
          net_flips([(False, True)] * 3, [(True, False)] * 4)["beyond"], False)
    # sigma rule: a missing sigma is never zero
    man = [{"id": "x", "session": "s", "golfer": "A", "lmClubheadMph": None}]
    dg = {"B1": {"x": {"rc": 0, "checks": {"ok": True}, "metrics": {"m": {"scalar": 1.0, "sigma": None, "ph": {}}}}},
          "C1": {"x": {"rc": 0, "checks": {"ok": True}, "metrics": {"m": {"scalar": 1.5, "sigma": None, "ph": {}}}}}}
    import tempfile
    td = tempfile.mkdtemp()
    metric_stage(man, dg, td)
    rows = list(csv.DictReader(open(os.path.join(td, "metrics.csv"))))
    r = [x for x in rows if x["pair"] == "B1-C1"][0]
    check("sigma rule: a moved metric with no sigma is 'not characterised'", (r["sigma"], r["movedNoSigma"]),
          ("not characterised", "1"))
    dg["C1"]["x"]["metrics"] = {}
    metric_stage(man, dg, td)
    r = [x for x in csv.DictReader(open(os.path.join(td, "metrics.csv"))) if x["pair"] == "B1-C1"][0]
    check("a key that disappears is always flagged", (r["disappeared"], r["flagged"]), ("1", "1"))
    # SG residual of a quadratic path is zero; of an alternating jitter is not
    fr = [[i * 6700, sum(([0.5 + 0.001 * i * i, 0.5, 0.9] for _ in range(17)), []), 0.5, 0, 42] for i in range(20)]
    jj, _ = jitter({"pose": {"faceOn": {"frames": fr}}}, "faceOn", 60000, (1000, 1000))
    check("jitter: a quadratic path has zero residual", round(jj, 9), 0.0)
    fr2 = [[i * 6700, sum(([0.5 + 0.002 * (-1) ** i, 0.5, 0.9] for _ in range(17)), []), 0.5, 0, 42] for i in range(20)]
    jj, _ = jitter({"pose": {"faceOn": {"frames": fr2}}}, "faceOn", 60000, (1000, 1000))
    check("jitter: an alternating 2 px jitter reads above 1 px", jj > 1.0, True)
    # pooled drops at the top
    fr3 = [[400000 + i * 6700, sum(([0.5, 0.5, 0.1 if j in (7, 9) else 0.9] for j in range(17)), []), 0.5, 0, 42]
           for i in range(15)]
    k, n, _ = top_drops({"pose": {"faceOn": {"frames": fr3}}}, "faceOn", 450000)
    check("top drops: joints 7 and 9 dropped over P4 +- 50 ms", (k, n), (2 * 15, 8 * 15))
    tr = {"trace": {"lines": [[i, "pred" if (i < 10 or i > 19) else ("ray" if i % 2 else "pred"), 0, 0, None, None, 0]
                              for i in range(40)], "summary": {"spanLo": 10, "spanHi": 19, "refused": ""}}}
    m = shaft_fo(tr, span_of(tr))
    check("shaft: S-1 and S-3 count only the span frames", (m["n"], m["frac"], m["longestPred"]), (10, 50.0, 1))
    print("self-test: %s" % ("all hold" if not bad else "%d FAILED" % bad))
    return 1 if bad else 0


def main():
    if sys.argv[1:] == ["--self-test"]:
        return self_test()
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--root", required=True, help="the run's root on the share (<share>/scratch/i183-ab)")
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    return compare(a.root, a.out)


if __name__ == "__main__":
    sys.exit(main())
