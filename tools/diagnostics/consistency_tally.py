#!/usr/bin/env python3
"""Consistency: the rules of docs/design/consistency_design.md run over consistency_extract.py's output.

    python3 tools/diagnostics/consistency_tally.py <extract dir> <out dir> [--mock data.js]

Writes consistency_readings.csv (one row per reading and column), consistency_cells.csv (the area table of each
session), consistency_shots.csv (every shot's launch-monitor flag and stand-outs) and prints the summary the design
doc quotes. Needs numpy. Reads core.json and norms.json from the repository. Seeds are fixed, so a run reproduces.
This is the reference for the C++ build: its goldens come from here.
"""
import csv, glob, json, os, sys, warnings
from collections import Counter, defaultdict
import numpy as np

REPO = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
core  = json.load(open(REPO + "/src/Resources/diagnostics/core.json"))
norms = json.load(open(REPO + "/src/Resources/diagnostics/norms.json"))["norms"]
MEAS = {m["id"]: m for m in core["measures"]}
SIG  = {s["id"]: s for s in core["signals"]}

# ── the rules ───────────────────────────────────────────────────────────────────────────────
LOOSE_AT   = 0.75     # a reading is loose at this share of its norm's sigma
CELL_LOOSE = 1 / 3    # a cell is loose when this share of its judged readings is loose
MIN_SHOTS  = 5        # the fewest shots a spread is worked from
MIN_CLUB   = 3        # a club with fewer shots has no median to compare with
STAND_Z    = 3.5      # a stand-out: this many robust spreads from the other shots ...
STAND_SIG  = 2.0      # ... and this many sigma, where the reading has one
COLS = ["P1", "P1-P2", "P2-P3", "P3-P4", "P4-P5", "P5-P6", "P6-P7", "P7-P8"]   # index = col + 1
PID  = [0, 12, 8, 2, 13, 9, 5, 14]    # P1..P8 as Phase ids
ORDER = ["setup", "posture", "armsAndClub", "sequence", "lateral", "release", "impact", "finish"]
AREA_LABEL = {"setup": "Setup", "posture": "Posture", "armsAndClub": "Arms and club", "sequence": "Sequence",
              "lateral": "Lateral motion", "release": "Release", "impact": "Impact", "finish": "Finish"}
# where each area is read; col -1 is the P1 position, 0..6 the spans
APPLIES = {"setup": [-1], "posture": [-1, 0, 1, 2, 3, 4, 5, 6], "armsAndClub": [0, 1, 2, 3, 4, 5, 6],
           "sequence": [1, 2, 3, 4, 5], "lateral": [0, 1, 2, 3, 4, 5, 6], "release": [4, 5, 6], "impact": [5], "finish": [6]}
# readings no condition detects on, placed by hand (design doc, Appendix B)
HAND = {"clubAngularSpeed": "sequence", "clubheadSpeed": "sequence", "handSpeed": "sequence", "leadArmAngularSpeed": "sequence",
        "thoraxAngularSpeed": "sequence", "thoraxRotationSigned": "armsAndClub", "xFactor": "sequence", "elbowAlignment": "armsAndClub",
        "handPlaneCurve": "armsAndClub", "swingPlaneCurve": "armsAndClub", "shoulderPlaneAngle": "armsAndClub",
        "trailWristFlexExt": "release", "headTilt": "posture", "spineSideBend": "posture", "trunkRise": "posture",
        "plumbBobDistance": "lateral", "attackAngle": "impact", "handPlaneSteepening": "armsAndClub", "lowPointFromAttack": "impact",
        "tempoBackswing": "sequence", "ballBodyDistance": "setup", "stanceWidthMm": "setup", "toeLineAngle": "setup",
        "leadFootFlare": "setup", "trailFootFlare": "setup", "hipDepthButt": "posture", "headPitch": "posture", "spineBackLine": "posture"}
BACKSWING_TIMING = ("tempoBackswing", "tempoRatio")   # stamped at impact, but they time the backswing: read at P3-P4

def measure_keys(mid, seen=None):
    seen = seen or set()
    if mid in seen or mid not in MEAS: return set()
    seen.add(mid); m = MEAS[mid]
    return {m["metricKey"]} if m.get("metricKey") else set()

def key_areas():
    ka = defaultdict(Counter)
    for c in core["conditions"]:
        g = c.get("group")
        if g not in APPLIES: continue
        for sid in c.get("detectedBy") or []:
            s = SIG.get(sid if isinstance(sid, str) else sid.get("signal"))
            for mid in (s or {}).get("measures", []):
                for k in measure_keys(mid): ka[k][g] += 1
    return ka

def norm_sigma():
    ns = defaultdict(list)
    for n in norms:
        m = MEAS.get(n.get("measure"))
        if not m or not m.get("metricKey") or n.get("sigmaLo") is None: continue
        hi = n.get("sigmaHi", n["sigmaLo"])
        ns[m["metricKey"]].append(0.5 * (n["sigmaLo"] + (hi if hi is not None else n["sigmaLo"])))
    return {k: float(np.median(v)) for k, v in ns.items()}

KA, NS = key_areas(), norm_sigma()
def area_of(k): return KA[k].most_common(1)[0][0] if k in KA else HAND.get(k)

def residuals(M, clubs):
    """Each value less its club's median: like against like. A club under MIN_CLUB shots leaves NaN."""
    M = np.array(M, float); out = np.full_like(M, np.nan)
    for c in set(clubs):
        ix = [i for i, x in enumerate(clubs) if x == c]
        if len(ix) < MIN_CLUB: continue
        with warnings.catch_warnings():
            warnings.simplefilter("ignore")
            out[ix] = M[ix] - np.nanmedian(M[ix], axis=0)
    return out

def spread(v):
    v = np.asarray(v, float); v = v[np.isfinite(v)]
    if len(v) < MIN_SHOTS: return np.nan
    q1, q3 = np.percentile(v, [25, 75])
    return (q3 - q1) / 1.349

def cells(S, idx=None):
    idx = list(range(len(S))) if idx is None else list(idx)
    Sx = [S[i] for i in idx]; clubs = [s["club"] for s in Sx]; out = {}
    for k in sorted({k for s in Sx for k in s["m"]}):
        unit = next((s["m"][k]["u"] for s in Sx if k in s["m"]), "")
        sg = [s["m"][k]["s"] for s in Sx if k in s["m"] and s["m"][k].get("s")]
        sigma = float(np.median(sg)) if sg else np.nan
        curve = any(s["m"].get(k, {}).get("sp") for s in Sx)
        def at(pid):
            v = [(s["m"].get(k, {}).get("at") or {}).get(str(pid)) for s in Sx]
            vals = np.array([x[0] if x else np.nan for x in v], float)
            sgs = [x[1] for x in v if x and x[1]]
            return vals, (float(np.median(sgs)) if sgs else sigma)
        def position(col, pid):
            vals, sg2 = at(pid)
            if np.isfinite(vals).sum() < MIN_SHOTS: return
            R = residuals(vals.reshape(-1, 1), clubs)
            out[(k, col)] = dict(sd=spread(R[:, 0]), sig=sg2, unit=unit, res=R)
        if area_of(k) == "impact":          # impact reads the position itself, not a curve on its way there
            position(5, 5); continue
        if k in BACKSWING_TIMING:
            position(2, 5); continue
        if curve:
            for c in range(7):
                M = []
                for s in Sx:
                    sp = (s["m"].get(k) or {}).get("sp"); v = sp[c] if sp and sp[c] else None
                    M.append([np.nan if x is None else x for x in v] if v else [np.nan] * 5)
                R = residuals(M, clubs)
                sds = [spread(R[:, j]) for j in range(5)]
                if np.isfinite(sds).sum() >= 3:
                    out[(k, c)] = dict(sd=float(np.nanmedian(sds)), sig=sigma, unit=unit, res=R)
            position(-1, PID[0])
        else:
            for ci, pid in enumerate(PID): position(ci - 1, pid)
    return out

def verdict(k, c):
    """steady | loose | nearError | unjudged. Noise can only widen a spread, so a small spread is steady whatever sigma is."""
    if k not in NS or c["sd"] != c["sd"]: return "unjudged"
    if c["sd"] / NS[k] < LOOSE_AT: return "steady"
    return "nearError" if c["sig"] == c["sig"] and c["sd"] <= c["sig"] else "loose"

def cellword(vs):
    v = [x for x in vs if x != "unjudged"]
    if not v: return ""
    n = len(v); lo = v.count("loose"); ne = v.count("nearError")
    if lo >= max(1, n * CELL_LOOSE): return "loose"
    if lo >= 1: return "some"
    if ne >= max(1, n * CELL_LOOSE): return "nearError"
    return "steady"

def table(C):
    t = {}
    for a in ORDER:
        for col in APPLIES[a]:
            vs = [(k, verdict(k, c), c) for (k, cc), c in C.items() if cc == col and area_of(k) == a]
            t[(a, col)] = (cellword([v[1] for v in vs]), vs)
    return t

def standouts(S, C):
    """Per shot, one row per metric: the shot against the others (itself left out), the whole span out, past 2 sigma."""
    out = [{} for _ in S]
    for (k, col), c in C.items():
        a = area_of(k)
        if a is None or col not in APPLIES.get(a, []): continue
        R = c["res"]
        for i in range(len(S)):
            zs = []
            for j in range(R.shape[1]):
                x = R[i, j]
                if x != x: continue
                o = np.delete(R[:, j], i); o = o[np.isfinite(o)]
                if len(o) < 6: continue
                q1, q3 = np.percentile(o, [25, 75]); s = (q3 - q1) / 1.349
                if s > 0: zs.append(((x - np.median(o)) / s, x - np.median(o)))
            if not zs: continue
            z, dv = min(zs, key=lambda t: abs(t[0]))
            if abs(z) >= STAND_Z and not (c["sig"] == c["sig"] and abs(dv) < STAND_SIG * c["sig"]):
                if k not in out[i] or abs(z) > abs(out[i][k][2]): out[i][k] = (k, col, float(z), float(dv), c["unit"], a)
    return [sorted(o.values(), key=lambda t: -abs(t[2])) for o in out]

def rank(a):
    a = np.asarray(a, float); o = np.argsort(a); r = np.empty(len(a)); r[o] = np.arange(len(a)); return r
def spearman(a, b): return float(np.corrcoef(rank(a), rank(b))[0, 1])
def fence(v):
    f = np.array([x for x in v if x == x], float); q1, m, q3 = np.percentile(f, [25, 50, 75]); i = q3 - q1
    return q1 - 1.5 * i, m, q3 + 1.5 * i, q1, q3, len(f)

def apart_today():
    """What the shipped apartFrom() gives: past the Tukey fences, on the trimmed 4 July fixture, against shuffling."""
    d = json.load(open(REPO + "/src/Gui/tests/data/charts/session_0704.json")); shots = d["shots"]; ra = d.get("readAt", {}); n = len(shots)
    R = []
    for k in {k for s in shots for k in s["metrics"]}:
        by = defaultdict(lambda: [np.nan] * n)
        for i, s in enumerate(shots):
            for a in (s["metrics"].get(k) or {}).get("at", []):
                if a[2] is not None: by[a[0]][i] = a[2]
        for p in ([p for p in ra.get(k, []) if p in by] or sorted(by)[-1:]): R.append(np.array(by[p], float))
    def past(v, w):
        f = v[np.isfinite(v)]
        if len(f) < 3: return np.zeros(n, int)
        q1, q3 = np.percentile(f, [25, 75]); i = q3 - q1
        return ((w < q1 - 1.5 * i) | (w > q3 + 1.5 * i)).astype(int) if i > 0 else np.zeros(n, int)
    cnt = sum(past(v, v) for v in R); rng = np.random.default_rng(1)
    perm = np.array([sum(past(v, rng.permutation(v)) for v in R) for _ in range(300)])
    print(f"Apart from the session today (fixture, {n} shots, {len(R)} readings): per shot {list(map(int, cnt))}; with any {int((cnt > 0).sum())} of {n}; shuffled mean {perm.mean():.1f}, session max p95 {np.percentile(perm.max(axis=1), 95):.0f}")

def main():
    ex, out = sys.argv[1], sys.argv[2]; os.makedirs(out, exist_ok=True)
    apart_today()
    names = sorted(os.path.basename(p)[:-5] for p in glob.glob(ex + "/*.json"))
    load = lambda n: json.load(open(f"{ex}/{n}.json"))["shots"]
    print("sessions", len(names), "shots", sum(len(load(n)) for n in names))
    for n in names:
        S = load(n); carry = sum("lm.carryDistance" in s["lm"] for s in S); off = sum("lm.offline" in s["lm"] for s in S)
        cam = sum(any(m.get("sp") for m in s["m"].values()) for s in S)
        print(f"  {n[:10]} {n[-2:]}  shots {len(S):2d}  camera {cam:2d}  carry {carry:2d}  offline {off:2d}  clubs {dict(Counter(s['club'] for s in S))}")
    big = [n for n in names if sum(any(m.get("sp") for m in s["m"].values()) for s in load(n)) >= 15]
    print("sessions judged (15+ camera shots):", [n[:10] for n in big])
    rd = csv.writer(open(out + "/consistency_readings.csv", "w", newline="")); rd.writerow("session metric column area unit spread sigma norm_sigma spread_over_norm verdict".split())
    cl = csv.writer(open(out + "/consistency_cells.csv", "w", newline="")); cl.writerow("session area column word readings_judged readings_loose readings_near_error readings_unjudged".split())
    sh = csv.writer(open(out + "/consistency_shots.csv", "w", newline="")); sh.writerow("session swing club carry_yd offline_yd flag stand_outs top_stand_outs".split())
    allC, T = {}, {}
    for n in big:
        S = load(n); C = cells(S); allC[n] = C; t = table(C); T[n] = t
        for (k, col), c in sorted(C.items()):
            rd.writerow([n, k, COLS[col + 1], area_of(k) or "", c["unit"], round(c["sd"], 3), "" if c["sig"] != c["sig"] else round(c["sig"], 3), NS.get(k, ""), round(c["sd"] / NS[k], 3) if k in NS else "", verdict(k, c)])
        for (a, col), (w, vs) in t.items():
            v = [x[1] for x in vs]
            cl.writerow([n, a, COLS[col + 1], w, sum(x != "unjudged" for x in v), v.count("loose"), v.count("nearError"), v.count("unjudged")])
        counts = Counter(w or "blank" for w, _ in t.values())
        with_sig = [c for c in C.values() if c["sig"] == c["sig"] and c["sig"] > 0 and c["sd"] == c["sd"]]
        le = sum(c["sd"] <= c["sig"] for c in with_sig)
        vc = Counter(verdict(k, c) for (k, _), c in C.items())
        print(f"{n[:10]} n={len(S)} readings {len(C)} verdicts {dict(vc)} | spread<=sigma {le}/{len(with_sig)} | cells {dict(counts)}")
    print("\nThe area tables")
    for n in big:
        print(n[:10]); print("  %-15s" % "", *["%-7s" % c for c in COLS])
        for a in ORDER:
            print("  %-15s" % AREA_LABEL[a], *["%-7s" % ({"nearError": "warn"}.get(T[n][(a, c)][0], T[n][(a, c)][0]) if (a, c) in T[n] else ".") for c in range(-1, 7)])
    # how repeatable the table is
    print("\nRepeatability")
    for n in big:
        S = load(n); rng = np.random.default_rng(3); rs = []
        for _ in range(8):
            p = rng.permutation(len(S)); A, B = cells(S, p[::2]), cells(S, p[1::2])
            ks = [k for k in A if k in B and k[0] in NS and A[k]["sd"] == A[k]["sd"] and B[k]["sd"] == B[k]["sd"]]
            rs.append(spearman([A[k]["sd"] / NS[k[0]] for k in ks], [B[k]["sd"] / NS[k[0]] for k in ks]))
        print(f"  split-half {n[:10]} {np.mean(rs):.2f}")
    def within(X, Y):
        xs, ys = [], []
        for m in {k for k, c in X if c >= 0}:
            cs = [c for c in range(7) if (m, c) in X and (m, c) in Y and X[(m, c)]["sd"] > 0 and Y[(m, c)]["sd"] > 0]
            if len(cs) < 5: continue
            a, b = np.log([X[(m, c)]["sd"] for c in cs]), np.log([Y[(m, c)]["sd"] for c in cs])
            xs += list(a - a.mean()); ys += list(b - b.mean())
        return spearman(xs, ys), len(xs)
    for i in range(len(big)):
        for j in range(i + 1, len(big)):
            r, m = within(allC[big[i]], allC[big[j]])
            print(f"  which span is loosest, within a metric: {big[i][:10]} v {big[j][:10]}  r {r:.2f}  ({m} spans)")
    # stand-outs: real against shuffled
    print("\nStand-outs")
    for n in big:
        S = load(n); C = allC[n]; st = standouts(S, C); cnt = [len(x) for x in st]
        rng = np.random.default_rng(5); nulls = []
        for _ in range(30):
            C2 = {k: {**c, "res": c["res"][rng.permutation(len(S))]} for k, c in C.items()}
            nulls.append([len(x) for x in standouts(S, C2)])
        nulls = np.array(nulls)
        print(f"  {n[:10]} real median {np.median(cnt):.0f} mean {np.mean(cnt):.2f} none on {sum(c == 0 for c in cnt)}/{len(S)} max {max(cnt)} (swing {S[int(np.argmax(cnt))]['swing'][-4:]}) | shuffled mean {nulls.mean():.2f} none {100 * (nulls == 0).mean():.0f}% session max p95 {np.percentile(nulls.max(axis=1), 95):.0f}")
    # the ball
    print("\nBall flags (club by club, Tukey fences, clubs of 8+ shots with the reading)")
    for n in names:
        S = load(n); hits = []
        for club in sorted({s["club"] for s in S}):
            ix = [i for i, s in enumerate(S) if s["club"] == club]
            cy = [S[i]["lm"].get("lm.carryDistance", np.nan) for i in ix]; of = [S[i]["lm"].get("lm.offline", np.nan) for i in ix]
            for i, cv, ov in zip(ix, cy, of):
                w = []
                if sum(x == x for x in cy) >= 8 and cv == cv and cv < fence(cy)[0]: w.append("short")
                if sum(x == x for x in of) >= 8 and ov == ov and (ov < fence(of)[0] or ov > fence(of)[2]): w.append("offline")
                if w: hits.append((i, w))
        flags = dict(hits); C = allC.get(n); st = standouts(S, C) if C else [[] for _ in S]
        for i, s in enumerate(S):
            sh.writerow([n, s["swing"], s["club"], s["lm"].get("lm.carryDistance", ""), s["lm"].get("lm.offline", ""), "+".join(flags.get(i, [])), len(st[i]), "; ".join(f"{k} {COLS[c + 1]} z{z:.1f}" for k, c, z, *_ in st[i][:6])])
        if hits: print(f"  {n[:10]} {[(S[i]['swing'][-4:], '+'.join(w), S[i]['club'], S[i]['lm'].get('lm.carryDistance'), S[i]['lm'].get('lm.offline'), [f'{k} {COLS[c + 1]} {z:.1f}' for k, c, z, *_ in st[i][:4]]) for i, w in hits]}")
    # Home's column: newest session first, fading .75 a session, like the work-ons
    print("\nHome column")
    order = sorted(big, reverse=True); sc = {"steady": 0, "nearError": 0, "some": .5, "loose": 1}
    for a in ORDER:
        val = {}
        # P7-P8 is after the ball has gone: it counts only for Finish, which has nothing else
        for col in ([c for c in APPLIES[a] if c != 6] or APPLIES[a]):
            top = bot = 0
            for j, n in enumerate(order):
                w = T[n][(a, col)][0]
                if w: top += .75 ** j * sc[w]; bot += .75 ** j
            if bot: val[col] = top / bot
        mean = float(np.mean(list(val.values()))); top = max(val, key=val.get)
        print(f"  {AREA_LABEL[a]:15s} {'Loose' if mean >= .5 else 'Some' if mean >= .2 else 'Steady':7s} {mean:.2f}  loosest {COLS[top + 1]} {val[top]:.2f}")

if __name__ == "__main__":
    main()
