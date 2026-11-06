#!/usr/bin/env python3
"""Consistency: a compact, read-only extract of an athlete's sessions, one JSON per session
(docs/design/consistency_design.md, Appendix A). Run it on the cabin PC, where the share is local:

    python tools\\diagnostics\\consistency_extract.py C:\\PinPointStudio\\Mark-Liversedge C:\\PinPointStudio\\scratch\\consistency\\extract

Each shot keeps its club, rating, P-position times, launch-monitor readings and, per metric, the unit, sigma, the
phase samples and K samples inside each span P1-P2 .. P7-P8. Each span is sampled by its own start and end time, so
the shots are aligned on their own P-positions. Only samples marked valid are used. consistency_tally.py reads the result.
"""
import sys, os, json, glob, time, bisect
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from pp_swingdoc import load_swing, has_swing
ROOT = sys.argv[1]
OUT  = sys.argv[2]
ONLY = sys.argv[3] if len(sys.argv) > 3 else ""    # one session folder name, for a quick check
os.makedirs(OUT, exist_ok=True)
P = [0, 12, 8, 2, 13, 9, 5, 14]   # P1..P8 as Phase ids
K = 5                             # samples inside each span
def rnd(x):
    return None if x is None or x != x else round(float(x), 4)
def interp(ts, vs, t):
    i = bisect.bisect_left(ts, t)
    if i <= 0 or i >= len(ts): return None
    t0, t1 = ts[i-1], ts[i]
    if t1 == t0: return vs[i]
    return vs[i-1] + (vs[i]-vs[i-1]) * (t-t0)/(t1-t0)
t00 = time.time()
for sd in sorted(glob.glob(os.path.join(ROOT, "2026-*"))):
    name = os.path.basename(sd); shots = []
    if ONLY and name != ONLY: continue
    for wd in sorted(glob.glob(os.path.join(sd, "swing_*"))):
        if not has_swing(wd): continue
        try: d = load_swing(wd)
        except Exception as e:
            print("skip", wd, e, flush=True); continue
        a = d.get("analysis") or {}
        ms = a.get("metrics") or []
        if not ms: continue
        review = d.get("review") or {}; cap = d.get("capture") or {}
        club = review.get("club") or (cap.get("club") or {}).get("name") or ""
        ph = {}
        for p in a.get("phases") or []:
            if p.get("phase") in P and p["phase"] not in ph: ph[p["phase"]] = p["t_us"]
        times = [ph.get(x) for x in P]
        rec = {"swing": os.path.basename(wd), "club": club, "rating": review.get("rating", 0),
               "p": times, "lm": {}, "m": {}}
        for m in ms:
            k = m.get("key")
            if not k: continue
            at = {}
            for s in m.get("phaseSamples") or []:
                if s.get("value") is None: continue
                at[str(s["phase"])] = [rnd(s["value"]), rnd(s.get("sigma"))]
            if k.startswith("lm."):
                for s in m.get("phaseSamples") or []:
                    if s.get("value") is not None: rec["lm"][k] = rnd(s["value"]); break
                continue
            ts, vs, valid = m.get("t_us") or [], m.get("value") or [], m.get("valid") or []
            if len(ts) >= 2 and valid and len(valid) == len(ts):
                pairs = [(t, v) for t, v, ok in zip(ts, vs, valid) if ok and v is not None]
                ts = [p[0] for p in pairs]; vs = [p[1] for p in pairs]
            else:
                pairs = [(t, v) for t, v in zip(ts, vs) if v is not None]
                ts = [p[0] for p in pairs]; vs = [p[1] for p in pairs]
            spans = []
            for i in range(7):
                a0, b0 = times[i], times[i+1]
                if a0 is None or b0 is None or b0 <= a0 or len(ts) < 2:
                    spans.append(None); continue
                vals = [interp(ts, vs, a0 + (b0-a0)*(j+0.5)/K) for j in range(K)]
                spans.append([rnd(v) for v in vals])
            rec["m"][k] = {"u": m.get("unit", ""), "s": rnd(m.get("sigma")), "sk": m.get("sigmaKind"),
                           "l": m.get("label", ""), "at": at, "sp": spans if any(spans) else None}
        shots.append(rec)
    with open(os.path.join(OUT, name + ".json"), "w") as f:
        json.dump({"session": name, "shots": shots}, f, separators=(",", ":"))
    print(name, len(shots), round(time.time()-t00, 1), flush=True)
print("DONE", round(time.time()-t00, 1), flush=True)
