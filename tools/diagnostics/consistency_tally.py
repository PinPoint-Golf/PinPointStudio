#!/usr/bin/env python3
"""Consistency: the rules run over consistency_dump's output.

    python3 tools/diagnostics/consistency_tally.py <dump dir> <out dir> [--min-shots N]
    python3 tools/diagnostics/consistency_tally.py <dump dir> <out dir> --check
    python3 tools/diagnostics/consistency_tally.py <dump dir> <out dir> --out-goldens path.json --golden-session name
    python3 tools/diagnostics/consistency_tally.py <dump dir> <out dir> --out-home-cells path.json

Takes each file's `shots` and `readings` as given: no sampling, no norms, no packs. Writes consistency_readings.csv (one
row per reading and column), consistency_cells.csv (the area table of each session), consistency_shots.csv (every
shot's launch-monitor flag and stand-outs) and prints the summary the design doc quotes. Needs numpy. Seeds are fixed,
so a run reproduces.

The rules' source is docs/design/consistency_design.md. Everything the build's contract settled on top of it (tie-breaks,
the guard, the shuffle) is absorbed here, so this file stands on its own.

This is the independent reference for the C++ rules. --check recomputes `result` for every file and lists each
difference from the file's own `result` block, exiting 1 if there are any. --out-goldens writes the `result` block of
one session plus a `home` block rolled up over every session in the directory, for the C++ golden tests.
--out-home-cells writes each file's own `result.cells`, newest session first, just area, column and word: what goldenHome
in session_consistency_test.cpp rolls up to meet that `home` block. None of the three writes the CSVs; out dir is then
unused. No dump files is an error. tools/diagnostics/consistency_make_goldens.sh runs the last two together.
"""
import argparse, csv, glob, json, os, sys, warnings
from collections import Counter, defaultdict
import numpy as np

REPO = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
warnings.filterwarnings("ignore", category=RuntimeWarning)   # medians of nothing are NaN, as the rules expect
np.seterr(all="ignore")                                      # a norm of 0 divides to inf, as it does in C++

# ── the rules ───────────────────────────────────────────────────────────────────────────────
LOOSE_AT   = 0.75     # a reading is loose at this share of its norm's sigma
CELL_LOOSE = 3        # a cell is loose when one in this many of its judged readings is loose
MIN_SHOTS  = 5        # the fewest shots a spread is worked from
MIN_SPANS  = 3        # the fewest of a span's K samples with a spread
MIN_CLUB   = 3        # a club with fewer shots has no median to compare with
MIN_OTHERS = 6        # the fewest other shots a stand-out is compared with
MIN_BALL   = 8        # the fewest shots of a club with a launch-monitor reading
STAND_Z    = 3.5      # a stand-out: this many robust spreads from the other shots ...
STAND_SIG  = 2.0      # ... and this many sigma, where the reading has one
SHUFFLES   = 30       # shuffled sessions behind the stand-out yardstick
SEED       = 5
K          = 5        # samples per span
COLS = ["P1", "P1-P2", "P2-P3", "P3-P4", "P4-P5", "P5-P6", "P6-P7", "P7-P8"]
ORDER = ["setup", "posture", "armsAndClub", "sequence", "lateral", "release", "impact", "finish"]
AREA_LABEL = {"setup": "Setup", "posture": "Posture", "armsAndClub": "Arms and club", "sequence": "Sequence",
              "lateral": "Lateral motion", "release": "Release", "impact": "Impact", "finish": "Finish"}
APPLIES = {"setup": [0], "posture": [0, 1, 2, 3, 4, 5, 6, 7], "armsAndClub": [1, 2, 3, 4, 5, 6, 7], "sequence": [2, 3, 4, 5, 6],
           "lateral": [1, 2, 3, 4, 5, 6, 7], "release": [5, 6, 7], "impact": [6], "finish": [7]}
BACKSWING_TIMING = ("tempoBackswing", "tempoRatio")   # positions, but they rest on the P-ladder like a span does
TOLERANCE = 1e-6      # --check, on spreads, z and the shuffle's figures
U64 = (1 << 64) - 1

def num(x): return np.nan if x is None else float(x)

def splitmix64(state):
    """The shared generator: C++ draws the same numbers, so its shuffle is the same shuffle."""
    while True:
        state = (state + 0x9E3779B97F4A7C15) & U64
        z = state
        z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & U64
        z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & U64
        yield z ^ (z >> 31)

def fisher_yates(n, rng):
    perm = list(range(n))
    for i in range(n - 1, 0, -1):
        j = next(rng) % (i + 1)
        perm[i], perm[j] = perm[j], perm[i]
    return perm

def residuals(M, clubs):
    """Each value less its club's median: like against like. A club under MIN_CLUB shots leaves NaN."""
    out = np.full_like(M, np.nan)
    for c in set(clubs):
        ix = [i for i, x in enumerate(clubs) if x == c]
        if len(ix) >= MIN_CLUB: out[ix] = M[ix] - np.nanmedian(M[ix], axis=0)
    return out

def spread(v):
    v = v[np.isfinite(v)]
    if len(v) < MIN_SHOTS: return np.nan
    q1, q3 = np.percentile(v, [25, 75])
    return float((q3 - q1) / 1.349)

def load(path):
    d = json.load(open(path)); shots = d["shots"]; n = len(shots); clubs = [s["club"] for s in shots]; rds = []
    for r in sorted(d["readings"], key=lambda r: (r["key"], r["column"])):
        V = np.array(r["values"], float).reshape(n, K if r["kind"] == "span" else 1)
        rds.append(dict(key=r["key"], col=r["column"], kind=r["kind"], area=r.get("area") or "", unit=r.get("unit") or "",
                        own=num(r["ownSigma"]), norm=num(r["normSigma"]), V=V, R=residuals(V, clubs)))
    return dict(name=d.get("session") or os.path.basename(path)[:-5], shots=shots, n=n, clubs=clubs, rds=rds, result=d.get("result"),
                guarded={i for i, s in enumerate(shots) if s.get("guarded")},
                carry=np.array([num(s.get("carry")) for s in shots]), offline=np.array([num(s.get("offline")) for s in shots]))

def verdict(sd, norm, own):
    """steady | loose | nearError | unjudged. Noise can only widen a spread, so a small spread is steady whatever sigma is."""
    if norm != norm or sd != sd: return "unjudged"
    if np.float64(sd) / norm < LOOSE_AT: return "steady"
    return "nearError" if own == own and sd <= own else "loose"

def readings_table(S, idx=None):
    """The readings that reach the table, {(key, column): reading plus sd and verdict}. idx takes a subset of the shots."""
    clubs = S["clubs"] if idx is None else [S["clubs"][i] for i in idx]; out = {}
    for r in S["rds"]:
        V, R = (r["V"], r["R"]) if idx is None else (r["V"][idx], residuals(r["V"][idx], clubs))
        if r["kind"] == "position":
            if np.isfinite(V).sum() < MIN_SHOTS: continue
            sd = spread(R[:, 0])
        else:
            sds = [spread(R[:, j]) for j in range(K)]
            if np.isfinite(sds).sum() < MIN_SPANS: continue
            sd = float(np.nanmedian(sds))
        out[(r["key"], r["col"])] = {**r, "sd": sd, "verdict": verdict(sd, r["norm"], r["own"])}
    return out

def cell(vs):
    """vs: the verdicts of an area and column. The word, then judged, loose, nearError, unjudged."""
    n = sum(x != "unjudged" for x in vs); lo = vs.count("loose"); ne = vs.count("nearError")
    if not n: w = "dash"
    elif lo >= max(1, n / CELL_LOOSE): w = "loose"
    elif lo >= 1: w = "some"
    elif ne >= max(1, n / CELL_LOOSE): w = "nearError"
    else: w = "steady"
    return dict(word=w, judged=n, loose=lo, nearError=ne, unjudged=len(vs) - n)

def cells(tab):
    """A record for every cell whose column applies to its area, readings or none; the rest have no record."""
    return {(a, col): cell([c["verdict"] for c in tab.values() if c["area"] == a and c["col"] == col]) for a in ORDER for col in APPLIES[a]}

def applicable(tab):
    """What stand-outs are read from: the readings that reached the table, where their area applies, by (key, column)."""
    return [c for k, c in sorted(tab.items()) if c["col"] in APPLIES.get(c["area"], [])]

def weakest(R, i):
    """Shot i against the others (itself left out), sample by sample: the (z, dv, j) nearest the pattern, so the whole span must be out."""
    best = None
    for j in range(R.shape[1]):
        x = R[i, j]
        if not np.isfinite(x): continue
        o = np.delete(R[:, j], i); o = o[np.isfinite(o)]
        if len(o) < MIN_OTHERS: continue
        q1, q3 = np.percentile(o, [25, 75]); s = (q3 - q1) / 1.349
        if s > 0:
            m = np.median(o); z = (x - m) / s
            if best is None or abs(z) < abs(best[0]): best = (float(z), float(x - m), j)
    return best

def standouts(rds, n, guarded=()):
    """rds: (reading, residuals) by (key, column). Per shot, one row per metric, biggest first. Guarded shots lose what rests on their P-ladder."""
    out = [{} for _ in range(n)]
    for r, R in rds:
        if np.isfinite(R).sum(axis=0).max() <= MIN_OTHERS: continue      # no sample has a shot and six others
        for i in range(n):
            if i in guarded and (r["kind"] == "span" or r["key"] in BACKSWING_TIMING): continue
            w = weakest(R, i)
            if w is None: continue
            z, dv, j = w; k = r["key"]
            if abs(z) >= STAND_Z and not (r["own"] == r["own"] and abs(dv) < STAND_SIG * r["own"]):
                if k not in out[i] or abs(z) > abs(out[i][k]["z"]): out[i][k] = dict(key=k, col=r["col"], z=z, dv=dv, k=j)
    return [sorted(o.values(), key=lambda t: (-abs(t["z"]), t["key"], t["col"])) for o in out]

def shuffled(S):
    """Stand-out counts per shuffle and shot, each reading's shots shuffled on their own, guard ignored. A new row i is old row perm[i]."""
    rds = applicable(S["tab"]); rng = splitmix64(SEED); nulls = []
    for _ in range(SHUFFLES):
        nulls.append([len(x) for x in standouts([(r, r["R"][fisher_yates(S["n"], rng)]) for r in rds], S["n"])])
    return np.array(nulls, float).reshape(SHUFFLES, S["n"])

def fence(v):
    f = v[np.isfinite(v)]; q1, q3 = np.percentile(f, [25, 75]); i = q3 - q1
    return q1 - 1.5 * i, q3 + 1.5 * i

def ball_flags(S):
    """{shot index: flags}, club by club."""
    out = defaultdict(list)
    for club in sorted(set(S["clubs"])):
        ix = [i for i, c in enumerate(S["clubs"]) if c == club]
        cy, of = S["carry"][ix], S["offline"][ix]
        short = fence(cy)[0] if np.isfinite(cy).sum() >= MIN_BALL else -np.inf
        lo, hi = fence(of) if np.isfinite(of).sum() >= MIN_BALL else (-np.inf, np.inf)
        for i, c, o in zip(ix, cy, of):
            if c < short: out[i].append("short")
            if o < lo or o > hi: out[i].append("offline")
    return dict(out)

def analyse(S):
    S["tab"] = readings_table(S); S["cells"] = cells(S["tab"]); S["flags"] = ball_flags(S)
    S["st"] = standouts([(r, r["R"]) for r in applicable(S["tab"])], S["n"], S["guarded"])
    S["nulls"] = shuffled(S)
    return S

def result_block(S):
    """`result`, as consistency_dump writes it."""
    nulls = S["nulls"]; o = [s["ordinal"] for s in S["shots"]]; null = lambda x: None if x != x else x
    return dict(cells=[dict(area=a, column=c, **w) for (a, c), w in S["cells"].items()],
                readings=[dict(key=k, column=c, spread=null(r["sd"]), verdict=r["verdict"]) for (k, c), r in S["tab"].items()],
                standouts=[dict(ordinal=o[i], key=t["key"], column=t["col"], z=t["z"], dv=t["dv"], k=t["k"]) for i, st in enumerate(S["st"]) for t in st],
                flags=[dict(ordinal=o[i], flags=f) for i, f in sorted(S["flags"].items())],
                shuffledMean=float(nulls.mean()) if S["n"] else 0.0, p95=float(np.percentile(nulls.max(axis=1), 95)) if S["n"] else 0.0)

# ── --check ─────────────────────────────────────────────────────────────────────────────────
def close(a, b):
    a, b = num(a), num(b)
    return (a != a and b != b) or abs(a - b) <= TOLERANCE

def diff(cpp, py):
    """Every way the file's result differs from ours, one line each. A row twice over counts; a shot either side leaves out has no flags."""
    out = []
    def side(block, key, what):
        for k, c in Counter(key(x) for x in block).items():
            if c > 1: out.append(f"{what} {k} appears {c} times")
        return {key(x): x for x in block}
    cell_key = lambda x: (x["area"], x["column"])
    A, B = side(cpp["cells"], cell_key, "cell"), side(py["cells"], cell_key, "cell")
    for k in sorted(set(A) | set(B), key=lambda t: (ORDER.index(t[0]) if t[0] in ORDER else 99, t[1])):
        if k not in A or k not in B: out.append(f"cell {k[0]} {COLS[k[1]]}: only in {'C++' if k in A else 'tally'}"); continue
        for f in ("word", "judged", "loose", "nearError", "unjudged"):
            if A[k].get(f) != B[k][f]: out.append(f"cell {k[0]} {COLS[k[1]]} {f}: C++ {A[k].get(f)}, tally {B[k][f]}")
    reading_key = lambda x: (x["key"], x["column"])
    A, B = side(cpp["readings"], reading_key, "reading"), side(py["readings"], reading_key, "reading")
    for k in sorted(set(A) | set(B)):
        if k not in A or k not in B: out.append(f"reading {k[0]} {COLS[k[1]]}: only in {'C++' if k in A else 'tally'}"); continue
        if not close(A[k]["spread"], B[k]["spread"]): out.append(f"reading {k[0]} {COLS[k[1]]} spread: C++ {A[k]['spread']}, tally {B[k]['spread']}")
        if A[k]["verdict"] != B[k]["verdict"]: out.append(f"reading {k[0]} {COLS[k[1]]} verdict: C++ {A[k]['verdict']}, tally {B[k]['verdict']}")
    key = lambda s: (s["ordinal"], s["key"], s["column"])
    A, B = side(cpp["standouts"], key, "stand-out"), side(py["standouts"], key, "stand-out")
    for k in sorted(set(A) | set(B)):
        where = f"stand-out shot {k[0]} {k[1]} {COLS[k[2]]}"
        if k not in A or k not in B: out.append(f"{where}: only in {'C++' if k in A else 'tally'}, z {(A if k in A else B)[k]['z']}"); continue
        if A[k]["k"] != B[k]["k"]: out.append(f"{where} sample: C++ {A[k]['k']}, tally {B[k]['k']}")
        for f in ("z", "dv"):
            if not close(A[k][f], B[k][f]): out.append(f"{where} {f}: C++ {A[k][f]}, tally {B[k][f]}")
    for o in sorted({s["ordinal"] for s in py["standouts"]}):      # the order a shot lists them in
        a, b = [key(s) for s in cpp["standouts"] if s["ordinal"] == o], [key(s) for s in py["standouts"] if s["ordinal"] == o]
        if a != b and sorted(a) == sorted(b): out.append(f"stand-outs on shot {o} in a different order")
    ordinal = lambda f: f["ordinal"]
    A, B = side([f for f in cpp["flags"] if f["flags"]], ordinal, "flags of shot"), side(py["flags"], ordinal, "flags of shot")
    for o in sorted(set(A) | set(B)):
        a, b = sorted(A[o]["flags"]) if o in A else None, sorted(B[o]["flags"]) if o in B else None
        if a != b: out.append(f"flags shot {o}: C++ {a}, tally {b}")
    for f in ("shuffledMean", "p95"):
        if not close(cpp[f], py[f]): out.append(f"{f}: C++ {cpp[f]}, tally {py[f]}")
    return out

def check(sessions):
    total = 0
    for S in sessions:
        lines = ["no result block"] if not S["result"] else diff(S["result"], result_block(analyse(S)))
        for l in lines: print(f"{S['name']}: {l}")
        total += len(lines)
    print(f"CHECK: {total} differences across {len(sessions)} sessions")
    return 1 if total else 0

# ── the summaries ───────────────────────────────────────────────────────────────────────────
def rank(a):
    a = np.asarray(a, float); o = np.argsort(a); r = np.empty(len(a)); r[o] = np.arange(len(a)); return r
def spearman(a, b): return float(np.corrcoef(rank(a), rank(b))[0, 1])

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

def camera_shots(S):
    """Shots with a finite value on any span reading: the ones the cameras read."""
    spans = [np.isfinite(r["V"]).any(axis=1) for r in S["rds"] if r["kind"] == "span"]
    return int(np.any(spans, axis=0).sum()) if spans else 0

def swing_name(s): return s.get("swing") or f"swing_{s['ordinal']:04d}"
def blank_if_nan(x, f=lambda x: x): return "" if x != x else f(x)

def within(X, Y):
    """Which span is loosest, within a metric: each metric's log spreads less their mean, ranked across metrics."""
    xs, ys = [], []
    for m in sorted({k for k, c in X if c >= 1}):
        cs = [c for c in range(1, 8) if (m, c) in X and (m, c) in Y and X[(m, c)]["sd"] > 0 and Y[(m, c)]["sd"] > 0]
        if len(cs) < 5: continue
        a, b = np.log([X[(m, c)]["sd"] for c in cs]), np.log([Y[(m, c)]["sd"] for c in cs])
        xs += list(a - a.mean()); ys += list(b - b.mean())
    return spearman(xs, ys), len(xs)

def home_rollup(sessions):
    """{area: word, score, loosest, value}. Newest session first, fading .75 a session, like the work-ons. P7-P8 is after
    the ball has gone: it counts only for Finish, which has nothing else. An area no session has a word for has None."""
    order = sorted(sessions, key=lambda S: S["name"], reverse=True); sc = {"steady": 0, "nearError": 0, "some": .5, "loose": 1}; out = {}
    for a in ORDER:
        val = {}
        for col in ([c for c in APPLIES[a] if c != 7] or APPLIES[a]):
            top = bot = 0
            for j, S in enumerate(order):
                w = S["cells"][(a, col)]["word"]
                if w in sc: top += .75 ** j * sc[w]; bot += .75 ** j
            if bot: val[col] = top / bot
        if not val: out[a] = dict(word=None, score=None, loosest=None, value=None); continue
        mean = float(np.mean(list(val.values()))); top = max(val, key=val.get)
        word = "Loose" if mean >= .5 else "Some" if mean >= .2 else "Steady"
        out[a] = dict(word=word, score=mean, loosest=None if word == "Steady" else COLS[top], value=val[top])
    return out

def home_cells(sessions):
    """The input of C++'s goldenHome: the cells C++ wrote, newest first. --check holds them equal to the cells of home_rollup."""
    return dict(sessions=[dict(session=S["name"], cells=[dict(area=c["area"], column=c["column"], word=c["word"]) for c in S["result"]["cells"]])
                          for S in sorted(sessions, key=lambda S: S["name"], reverse=True)])

def home_column(big):
    for a, h in home_rollup(big).items():
        print(f"  {AREA_LABEL[a]:15s} " + ("-" if h["word"] is None else f"{h['word']:7s} {h['score']:.2f}" + (f"  loosest {h['loosest']} {h['value']:.2f}" if h["loosest"] else "")))

def summarise(sessions, out, min_shots):
    os.makedirs(out, exist_ok=True)
    apart_today()
    print("sessions", len(sessions), "shots", sum(S["n"] for S in sessions))
    for S in sessions:
        n = S["name"]
        print(f"  {n[:10]} {n[-2:]}  shots {S['n']:2d}  camera {camera_shots(S):2d}  carry {int(np.isfinite(S['carry']).sum()):2d}  offline {int(np.isfinite(S['offline']).sum()):2d}  clubs {dict(Counter(S['clubs']))}")
    big = [S for S in sessions if camera_shots(S) >= min_shots]
    print(f"sessions judged ({min_shots}+ camera shots):", [S["name"][:10] for S in big])
    for S in big: analyse(S)
    def table_file(name, head, rows):
        with open(f"{out}/{name}", "w", newline="") as f:
            w = csv.writer(f); w.writerow(head.split()); w.writerows(rows)
    table_file("consistency_readings.csv", "session metric column area unit spread sigma norm_sigma spread_over_norm verdict",
               [[S["name"], k, COLS[col], c["area"], c["unit"], round(c["sd"], 3), blank_if_nan(c["own"], lambda x: round(x, 3)), blank_if_nan(c["norm"]),
                 blank_if_nan(c["norm"], lambda x: round(c["sd"] / x, 3)), c["verdict"]] for S in big for (k, col), c in sorted(S["tab"].items())])
    table_file("consistency_cells.csv", "session area column word readings_judged readings_loose readings_near_error readings_unjudged",
               [[S["name"], a, COLS[col], w["word"], w["judged"], w["loose"], w["nearError"], w["unjudged"]] for S in big for (a, col), w in S["cells"].items()])
    for S in big:
        C = S["tab"].values(); counts = Counter(w["word"] for w in S["cells"].values())
        with_sig = [c for c in C if c["own"] == c["own"] and c["own"] > 0 and c["sd"] == c["sd"]]
        print(f"{S['name'][:10]} n={S['n']} readings {len(C)} verdicts {dict(Counter(c['verdict'] for c in C))} | spread<=sigma {sum(c['sd'] <= c['own'] for c in with_sig)}/{len(with_sig)} | cells {dict(counts)}")
    print("\nThe area tables")
    for S in big:
        print(S["name"][:10]); print("  %-15s" % "", *["%-7s" % c for c in COLS])
        for a in ORDER:
            print("  %-15s" % AREA_LABEL[a], *["%-7s" % ({"nearError": "warn", "dash": "-"}.get(w, w) if (w := S["cells"].get((a, c), {}).get("word")) else ".") for c in range(8)])
    # how repeatable the table is
    print("\nRepeatability")
    for S in big:
        rng = np.random.default_rng(3); rs = []
        for _ in range(8):
            p = rng.permutation(S["n"]); A, B = readings_table(S, list(p[::2])), readings_table(S, list(p[1::2]))
            ks = [k for k in A if k in B and A[k]["norm"] == A[k]["norm"] and A[k]["sd"] == A[k]["sd"] and B[k]["sd"] == B[k]["sd"]]
            rs.append(spearman([A[k]["sd"] / A[k]["norm"] for k in ks], [B[k]["sd"] / B[k]["norm"] for k in ks]))
        print(f"  split-half {S['name'][:10]} {np.mean(rs):.2f}")
    for i in range(len(big)):
        for j in range(i + 1, len(big)):
            r, m = within(big[i]["tab"], big[j]["tab"])
            print(f"  which span is loosest, within a metric: {big[i]['name'][:10]} v {big[j]['name'][:10]}  r {r:.2f}  ({m} spans)")
    # stand-outs: real against shuffled
    print("\nStand-outs")
    for S in big:
        cnt = [len(x) for x in S["st"]]; nulls = S["nulls"]
        print(f"  {S['name'][:10]} real median {np.median(cnt):.1f} mean {np.mean(cnt):.2f} none on {sum(c == 0 for c in cnt)}/{S['n']} max {max(cnt)} (swing {swing_name(S['shots'][int(np.argmax(cnt))])[-4:]}) | shuffled mean {nulls.mean():.2f} none {100 * (nulls == 0).mean():.0f}% session max p95 {np.percentile(nulls.max(axis=1), 95):.2f}")
    # the ball
    print("\nBall flags (club by club, Tukey fences, clubs of 8+ shots with the reading)")
    rows = []
    for S in sessions:
        flags = ball_flags(S); st = S.get("st") or [[] for _ in S["shots"]]; blank = lambda x: "" if x is None else x
        top = lambda i, m: [f"{t['key']} {COLS[t['col']]}" + (f" z{t['z']:.1f}" if m == 6 else f" {t['z']:.1f}") for t in st[i][:m]]
        for i, s in enumerate(S["shots"]):
            rows.append([S["name"], swing_name(s), s["club"], blank(s.get("carry")), blank(s.get("offline")), "+".join(flags.get(i, [])), len(st[i]), "; ".join(top(i, 6))])
        if flags: print(f"  {S['name'][:10]} {[(swing_name(S['shots'][i])[-4:], '+'.join(w), S['clubs'][i], S['shots'][i].get('carry'), S['shots'][i].get('offline'), top(i, 4)) for i, w in sorted(flags.items())]}")
    table_file("consistency_shots.csv", "session swing club carry_yd offline_yd flag stand_outs top_stand_outs", rows)
    print("\nHome column")
    home_column(big)

def main():
    ap = argparse.ArgumentParser(description="The Consistency rules, run over consistency_dump's output.")
    ap.add_argument("dumps"); ap.add_argument("out")
    ap.add_argument("--check", action="store_true", help="diff each file's own result block against the recomputed one")
    ap.add_argument("--min-shots", type=int, default=15, help="camera shots for a session to be judged")
    ap.add_argument("--out-goldens", help="write the result block of --golden-session, and the home block of every session, to this path")
    ap.add_argument("--golden-session")
    ap.add_argument("--out-home-cells", help="write every session's cells, newest first, for goldenHome, to this path")
    a = ap.parse_args()
    paths = glob.glob(a.dumps + "/*.json")
    if not paths: sys.exit(f"no dump files in {a.dumps}")
    sessions = sorted((load(p) for p in paths), key=lambda S: S["name"])
    if a.out_home_cells:
        with open(a.out_home_cells, "w") as f: json.dump(home_cells(sessions), f, indent=0)
    if a.out_goldens:
        S = next((S for S in sessions if S["name"] == a.golden_session), None)
        if S is None: sys.exit(f"no session {a.golden_session!r} in {a.dumps}")
        for s in sessions: s["cells"] = cells(readings_table(s))
        home = {k: {f: h[f] for f in ("word", "score", "loosest")} for k, h in home_rollup(sessions).items()}
        with open(a.out_goldens, "w") as f: json.dump({**result_block(analyse(S)), "home": home}, f, indent=1)
    if a.out_goldens or a.out_home_cells: return 0
    if a.check: return check(sessions)
    summarise(sessions, a.out, a.min_shots)
    return 0

if __name__ == "__main__":
    sys.exit(main())
