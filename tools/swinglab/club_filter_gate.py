#!/usr/bin/env python3
"""club_filter_gate.py — the club rate filter's gate (docs/design/club_rate_filter_design.md).

Reads swing documents (or `swinglab_run --out` result trees) and measures, per swing:
  * clubhead speed at impact (the Impact phase sample) over the launch monitor's club speed;
  * how often the KS club angular speed changes direction in the last 250 ms (humps);
  * when the club ω peaks against when the clubhead speed peaks;
  * how closely club ω and clubhead speed move together through the downswing;
  * attack angle, low point, lag and the P-position times, for the no-regression check.

Usage:
  club_filter_gate.py --label baseline --out DIR                 # the stored library + corpus
  club_filter_gate.py --label on --runs RUNROOT --out DIR        # RUNROOT/<session>/<swing>/result.json
  club_filter_gate.py --compare DIR/baseline.csv DIR/on.csv --out DIR

Swings are named <session>/<swing>; 08-18 W02 exists in both libraries and is read once (live).
The tuning set and the held-out set are fixed here so that no number is tuned on the held-out swings.
"""
import argparse, csv, math, os, statistics as st, sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from pp_swingdoc import load_swing, find_swing_dirs  # noqa: E402

ROOTS = ["/mnt/swingdata/Mark-Liversedge", "/mnt/swingdata/corpus/swings"]
TUNING = ("2026-10-05_Mark-Liversedge_Wrist_01", "2026-10-20_Jess-Ratcliffe_Wrist_01",
          "2026-10-20_Jess-Ratcliffe_Wrist_02", "2026-10-20_Jess-Ratcliffe_Wrist_03")
HELDOUT = ("2026-08-18_Mark-Liversedge_Wrist_02", "2026-10-20_Mark-Liversedge_Wrist_01",
           "2026-10-20_Nick-Henn_Wrist_02", "2026-10-20_Nick-Henn_Wrist_03")
IMPACT_PHASE = 5
FIELDS = ["swing", "set", "club", "lm_mph", "impact_mph", "ratio", "peak_mph", "head_peak_ms",
          "omega_peak_ms", "omega_humps", "omega_head_r", "attack", "lowpoint", "lag", "ptimes"]


def metric(a, key):
    for m in a.get("metrics", []):
        if m.get("key") == key:
            return m
    return None


def phase_value(m, phase=IMPACT_PHASE):
    if not m:
        return None
    for s in m.get("phaseSamples") or []:
        if s.get("phase") == phase and s.get("value") is not None:
            return float(s["value"])
    return None


def series(m):
    if not m or not m.get("t_us") or not m.get("value"):
        return [], []
    valid = m.get("valid") or [1] * len(m["value"])
    t, v = [], []
    for ti, vi, ok in zip(m["t_us"], m["value"], valid):
        if ok and vi is not None and not (isinstance(vi, float) and math.isnan(vi)):
            t.append(int(ti)); v.append(float(vi))
    return t, v


def humps(v, frac=0.4, prom=0.1):
    """Humps in a rate, counted exactly as countHumps() in src/Club/club_rate_filter.cpp: maxima
    (the window's ends count) above frac of the top that rise prom of the top above the trough
    separating them from a taller neighbour. A club that speeds up once has one."""
    n = len(v)
    if n == 0:
        return None
    top = max(v)
    if top <= 0:
        return 0
    count = 0
    for i in range(n):
        if not ((i == 0 or v[i] > v[i - 1]) and (i + 1 == n or v[i] >= v[i + 1]) and v[i] > frac * top):
            continue
        lmin = rmin = v[i]; l_end = r_end = True
        for j in range(i - 1, -1, -1):
            if v[j] > v[i]:
                l_end = False; break
            lmin = min(lmin, v[j])
        for j in range(i + 1, n):
            if v[j] > v[i]:
                r_end = False; break
            rmin = min(rmin, v[j])
        base = min(lmin, rmin) if (l_end and r_end) else rmin if l_end else lmin if r_end else max(lmin, rmin)
        if v[i] - base >= prom * top:
            count += 1
    return count


def pearson(x, y):
    if len(x) < 5:
        return None
    mx, my = st.mean(x), st.mean(y)
    sx = math.sqrt(sum((a - mx) ** 2 for a in x)); sy = math.sqrt(sum((b - my) ** 2 for b in y))
    return None if sx == 0 or sy == 0 else sum((a - mx) * (b - my) for a, b in zip(x, y)) / (sx * sy)


def interp(t, tv, vv):
    if not tv or t < tv[0] or t > tv[-1]:
        return None
    lo, hi = 0, len(tv) - 1
    while hi - lo > 1:
        mid = (lo + hi) // 2
        if tv[mid] <= t: lo = mid
        else: hi = mid
    if tv[hi] == tv[lo]:
        return vv[lo]
    f = (t - tv[lo]) / (tv[hi] - tv[lo])
    return vv[lo] + f * (vv[hi] - vv[lo])


def set_of(session):
    return "tuning" if session in TUNING else "heldout" if session in HELDOUT else "library"


def row_for(name, doc, lm_doc=None):
    a = doc.get("analysis") or {}
    session = name.split("/")[0]
    lm = phase_value(metric(a, "lm.clubheadSpeed"))
    if lm is None:
        src = lm_doc or doc
        lm = (src.get("launchMonitor") or {}).get("clubheadSpeed")
    imp_us = (a.get("kinematicSequence") or {}).get("impactUs")
    head = metric(a, "clubheadSpeed"); omega = metric(a, "clubAngularSpeed")
    ht, hv = series(head); ot, ov = series(omega)
    r = {"swing": name, "set": set_of(session),
         "club": (doc.get("review") or {}).get("club") or ((doc.get("capture") or {}).get("club") or {}).get("name") or ""}
    r["lm_mph"] = lm
    r["impact_mph"] = phase_value(head)
    r["ratio"] = (r["impact_mph"] / lm) if (lm and r["impact_mph"]) else None
    if imp_us and hv:
        w = [(t, v) for t, v in zip(ht, hv) if imp_us - 300000 <= t <= imp_us + 5000]
        if w:
            tp, vp = max(w, key=lambda x: x[1]); r["peak_mph"] = vp; r["head_peak_ms"] = (tp - imp_us) / 1000
    if imp_us and ov:
        w = [(t, v) for t, v in zip(ot, ov) if imp_us - 250000 <= t <= imp_us]
        if w:
            r["omega_humps"] = humps([v for _, v in w])
            tp, _ = max(w, key=lambda x: x[1]); r["omega_peak_ms"] = (tp - imp_us) / 1000
            pairs = [(v, interp(t, ht, hv)) for t, v in w]
            pairs = [(x, y) for x, y in pairs if y is not None]
            r["omega_head_r"] = pearson([p[0] for p in pairs], [p[1] for p in pairs])
    r["attack"] = phase_value(metric(a, "attackAngle"))
    r["lowpoint"] = phase_value(metric(a, "lowPointAhead"))
    r["lag"] = phase_value(metric(a, "lagAngle"))
    pos = (a.get("club") or {}).get("positions") or []
    r["ptimes"] = " ".join(f"P{p.get('p')}:{p.get('t_us')}" for p in pos)
    return r


def collect_stored():
    seen, rows = set(), []
    for root in ROOTS:
        for sd in find_swing_dirs(root):
            name = "/".join(sd.rstrip("/").split("/")[-2:])
            if name in seen:
                continue
            seen.add(name)
            try:
                rows.append(row_for(name, load_swing(sd)))
            except Exception as e:  # a swing with no analysis is reported, not fatal
                print(f"skip {name}: {e}", file=sys.stderr)
    return rows


def collect_runs(root):
    import json
    rows, sources = [], {}
    for r0 in ROOTS:
        for sd in find_swing_dirs(r0):
            sources.setdefault("/".join(sd.rstrip("/").split("/")[-2:]), sd)
    for session in sorted(os.listdir(root)):
        sdir = os.path.join(root, session)
        if not os.path.isdir(sdir):
            continue
        for swing in sorted(os.listdir(sdir)):
            p = os.path.join(sdir, swing, "result.json")
            if not os.path.isfile(p):
                continue
            name = f"{session}/{swing}"
            with open(p) as f:
                doc = json.load(f)
            src = load_swing(sources[name]) if name in sources else None
            rows.append(row_for(name, doc, src))
    return rows


def fnum(x, fmt="{:.3f}"):
    return "" if x is None else fmt.format(x)


def write_csv(rows, path):
    with open(path, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=FIELDS)
        w.writeheader()
        for r in rows:
            w.writerow({k: r.get(k, "") if r.get(k) is not None else "" for k in FIELDS})


def read_csv(path):
    out = []
    with open(path) as f:
        for r in csv.DictReader(f):
            for k in FIELDS:
                if k in ("swing", "set", "club", "ptimes"):
                    continue
                r[k] = float(r[k]) if r.get(k) not in (None, "") else None
            out.append(r)
    return out


def q(xs, p):
    xs = sorted(xs)
    if not xs:
        return float("nan")
    k = (len(xs) - 1) * p
    lo, hi = math.floor(k), math.ceil(k)
    return xs[lo] + (xs[hi] - xs[lo]) * (k - lo)


def lm_stats(rows):
    xs = [r for r in rows if r.get("ratio") is not None]
    if not xs:
        return None
    rat = [r["ratio"] for r in xs]
    err = [r["impact_mph"] - r["lm_mph"] for r in xs]
    return {"n": len(xs), "median": st.median(rat), "q1": q(rat, .25), "q3": q(rat, .75),
            "med_abs": st.median([abs(1 - x) for x in rat]),
            "sd_mph": st.pstdev(err) if len(err) > 1 else float("nan"),
            "bias_mph": st.mean(err), "lt07": sum(1 for x in rat if x < 0.7)}


def summary(rows, label):
    L = [f"# Club filter gate — {label}", ""]
    L += ["## Clubhead speed at impact against the launch monitor", "",
          "| set | n | median ratio | IQR | median \\|1−ratio\\| | bias mph | SD mph | < 0.7 |",
          "|---|---:|---:|---:|---:|---:|---:|---:|"]
    for name, sel in (("all pairs", lambda r: True), ("tuning", lambda r: r["set"] == "tuning"),
                      ("held-out", lambda r: r["set"] == "heldout")):
        s = lm_stats([r for r in rows if sel(r)])
        if s:
            L.append(f"| {name} | {s['n']} | {s['median']:.3f} | {s['q1']:.3f}–{s['q3']:.3f} | {s['med_abs']:.3f} | "
                     f"{s['bias_mph']:+.1f} | {s['sd_mph']:.1f} | {s['lt07']} |")
    om = [r for r in rows if r.get("omega_humps") is not None]
    multi = sum(1 for r in om if r["omega_humps"] >= 2)
    rr = [r["omega_head_r"] for r in om if r.get("omega_head_r") is not None]
    lead = [r["omega_peak_ms"] - r["head_peak_ms"] for r in om
            if r.get("omega_peak_ms") is not None and r.get("head_peak_ms") is not None]
    L += ["", "## Club angular speed (KS) through the downswing", "",
          f"- swings with a club ω series: {len(om)}",
          f"- two or more humps in the last 250 ms: {multi} ({100 * multi / max(len(om), 1):.0f} %)",
          f"- correlation of club ω with clubhead speed, last 250 ms: median {st.median(rr):.3f}, "
          f"10th percentile {q(rr, .1):.3f}" if rr else "- no correlation computed",
          f"- club ω peak minus clubhead-speed peak: median {st.median(lead):+.0f} ms, "
          f"IQR {q(lead, .25):+.0f}…{q(lead, .75):+.0f} ms" if lead else "",
          f"- club ω peak before impact: median {st.median([r['omega_peak_ms'] for r in om if r.get('omega_peak_ms') is not None]):+.0f} ms"
          if om else "", ""]
    return "\n".join(L)


def compare(a_rows, b_rows):
    A = {r["swing"]: r for r in a_rows}
    L = ["## Off → on, swing by swing (no-regression check)", "",
         "| metric | swings compared | changed | median \\|Δ\\| | max \\|Δ\\| |", "|---|---:|---:|---:|---:|"]
    for k, tol in (("attack", 0.05), ("lowpoint", 0.05), ("lag", 0.05), ("impact_mph", 0.05)):
        d = [abs(b[k] - A[b["swing"]][k]) for b in b_rows
             if b["swing"] in A and b.get(k) is not None and A[b["swing"]].get(k) is not None]
        if d:
            L.append(f"| {k} | {len(d)} | {sum(1 for x in d if x > tol)} | {st.median(d):.2f} | {max(d):.2f} |")
    same = sum(1 for b in b_rows if b["swing"] in A and b.get("ptimes") == A[b["swing"]].get("ptimes"))
    L += ["", f"P-position times identical on {same} of {sum(1 for b in b_rows if b['swing'] in A)} swings.", ""]
    return "\n".join(L)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--label", default="baseline")
    ap.add_argument("--runs", help="RUNROOT/<session>/<swing>/result.json; default = the stored documents")
    ap.add_argument("--compare", nargs=2, metavar=("OFF_CSV", "ON_CSV"))
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    if a.compare:
        off, on = read_csv(a.compare[0]), read_csv(a.compare[1])
        text = summary(off, "off") + "\n" + summary(on, "on") + "\n" + compare(off, on)
        with open(os.path.join(a.out, "compare.md"), "w") as f:
            f.write(text)
        print(text)
        return
    rows = collect_runs(a.runs) if a.runs else collect_stored()
    write_csv(rows, os.path.join(a.out, f"{a.label}.csv"))
    text = summary(read_csv(os.path.join(a.out, f"{a.label}.csv")), a.label)
    with open(os.path.join(a.out, f"{a.label}.md"), "w") as f:
        f.write(text)
    print(text)


if __name__ == "__main__":
    main()
