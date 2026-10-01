#!/usr/bin/env python3
"""Grade a continuous-track DTL run root against a base run root
(docs/design/dtl_continuous_track_design_update.md §3.1; the gate in
docs/research/data/markerless/dtl_continuous_20261002.md).

  dtl_continuous_grade.py BASE_ROOT NEW_ROOT [--truth DIR] [--md out.md] [--identical]

For every swing present in both roots (a directory holding club_dtl.json, or
result.json with analysis.clubDtl):

  bands     per band FAMILY (address / mid-backswing / delivery / impact / after,
            by the band name's P range): frames, measured, held, coverage measured
            (base → new) and coverage DRAWN (measured + held, new), edge bands
  truth     against --truth/<swing>.json (tools/shaftlab/dtl_band_truth.py output,
            the held-out band truth): |Δθ| p50 / p90 / max on the paired frames
            (within 4 ms) for base and new, and the count of frames WORSE in new
            (error grew by more than 0.5°, the grid) — the gate is 0
  pins      publishedInEndOn (base, new), lateEscapesRefused, endOnBeforeQuarantine
  fusion    club3d back / down plane inclDeg base → new (from result.json), Δ
  face-on   analysis.club.samples θ byte-identical? metric count base → new

--identical: instead of grading, assert the NEW root's frames[] / bands[] and the
summary minus configHash / stageVersion / continuous are identical to BASE's — the
bit-identical-OFF gate. Prints per swing OK / DIFF with the first differing frame.
Also compares club3d's frames (minus the §3.2a keys) and its planes.

--items: the per-item columns of dtl_continuous_track_design_update.md §3.2a, from the
NEW root's club3d (and BASE's for the before/after):
  A   the out-of-plane curve η(t): per band family, |Δθ_D| on the tracker's MEASURED
      frames when the face-on angle is de-projected in-plane vs through η — leave-one-
      band-out (the design's gate; the prior's reach as run, and at 400 ms to show the
      slope), the band-edge hold-out (the last/first 6 frames of each band predicted
      from the rest), and the full in-band fit; the C++ knots vs this file's refit
      (fusion_geom.fit_eta) at the knots; the synth3d line on measured frames base→new
  C   the DTL anchor: per swing, bridged frames in the phase windows, anchored /
      refused, the conditioning, and where anchored the angle to the bridged value and
      which of the two lies nearer the neighbouring measured fused frames
  D   the reflected band: per swing, backIncoherent and the backswing rms / inclination
      base→new, the bands reflected, and that no coherent swing changed

Every number here is reproducible from the two run roots and nothing else.
"""
import argparse, glob, json, math, os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
try:
    import numpy as np
    import fusion_geom as fg
except ImportError:      # --items needs numpy; the §3.1 grading does not
    np = fg = None

PUB = ("RAY", "SEG", "BAND")
FAMILIES = ("address", "mid-backswing", "delivery", "impact", "after")


def family(name):
    if name.startswith("addr"):
        return "address"
    m = re.findall(r"P([0-9.]+)", name)
    if not m:
        return "after"
    a = float(m[0]); b = float(m[-1]) if len(m) > 1 else a
    mid = (a + b) / 2.0
    if mid < 4.0:
        return "mid-backswing"
    if mid < 6.2:
        return "delivery"
    if mid < 8.0:
        return "impact"
    return "after"


def load_club_dtl(run):
    p = os.path.join(run, "club_dtl.json")
    if os.path.exists(p):
        return json.load(open(p))
    p = os.path.join(run, "result.json")
    if os.path.exists(p):
        d = json.load(open(p))
        return d.get("analysis", d).get("clubDtl")
    return None


def load_result(run):
    p = os.path.join(run, "result.json")
    return json.load(open(p)) if os.path.exists(p) else None


def pct(v, q):
    if not v:
        return float("nan")
    s = sorted(v)
    k = (len(s) - 1) * q / 100.0
    f = int(math.floor(k)); c = min(f + 1, len(s) - 1)
    return s[f] + (s[c] - s[f]) * (k - f)


def wrap_deg(d):
    return (d + 180.0) % 360.0 - 180.0


def band_stats(c):
    """per band: (family, name, n, measured, held, edge)"""
    out = []
    frames = c.get("frames", [])
    for bi, b in enumerate(c.get("bands", [])):
        n = pub = held = 0
        for f in frames:
            if f.get("band") != bi:
                continue
            n += 1
            if f["tier"] in PUB:
                pub += 1
            elif f["tier"] == "HELD":
                held += 1
        out.append((family(b["name"]), b["name"], n, pub, held, bool(b.get("edge", False))))
    return out


def truth_errors(c, truth):
    """|Δθ| (deg) per truth frame paired within 4 ms to a MEASURED frame; None where unpaired."""
    if not truth:
        return {}
    by_t = [(f["t_us"], f) for f in c.get("frames", []) if f["tier"] in PUB and f.get("theta") is not None]
    by_t.sort()
    ts = [t for t, _ in by_t]
    import bisect
    errs = {}
    for tf in truth.get("frames", []):
        t = tf["t_us"]
        i = bisect.bisect_left(ts, t)
        best = None
        for j in (i - 1, i):
            if 0 <= j < len(ts) and abs(ts[j] - t) <= 4000:
                if best is None or abs(ts[j] - t) < abs(ts[best] - t):
                    best = j
        if best is None:
            errs[t] = None
        else:
            errs[t] = abs(wrap_deg(math.degrees(by_t[best][1]["theta"] - tf["theta"])))
    return errs


def planes(res):
    if not res:
        return (float("nan"), float("nan"))
    c3 = res.get("analysis", res).get("club3d") or {}
    p = c3.get("planes") or {}
    def incl(k):
        q = p.get(k) or {}
        v = q.get("inclDeg")
        return float(v) if isinstance(v, (int, float)) else float("nan")
    return (incl("back"), incl("down"))


def face_on_identity(ra, rb):
    if not ra or not rb:
        return ("-", "-", "-")
    a = ra.get("analysis", ra); b = rb.get("analysis", rb)
    sa = a.get("club", {}).get("samples", []); sb = b.get("club", {}).get("samples", [])
    same = len(sa) == len(sb) and all(x.get("theta") == y.get("theta") and x.get("t_us") == y.get("t_us") for x, y in zip(sa, sb))
    ma = len(a.get("metrics", {})) if isinstance(a.get("metrics"), dict) else len(a.get("metrics", []))
    mb = len(b.get("metrics", {})) if isinstance(b.get("metrics"), dict) else len(b.get("metrics", []))
    return ("identical" if same else "DIFFER", ma, mb)


def swings_in(root):
    out = {}
    for d in sorted(glob.glob(os.path.join(root, "*"))):
        if os.path.isdir(d) and (os.path.exists(os.path.join(d, "club_dtl.json")) or os.path.exists(os.path.join(d, "result.json"))):
            out[os.path.basename(d)] = d
    return out


def identical(base, new):
    ok = True
    for sid, nd in sorted(swings_in(new).items()):
        bd = swings_in(base).get(sid)
        if not bd:
            continue
        cb, cn = load_club_dtl(bd), load_club_dtl(nd)
        if cb is None or cn is None:
            print(f"{sid}: missing clubDtl"); ok = False; continue
        msgs = []
        if cb["frames"] != cn["frames"]:
            for i, (x, y) in enumerate(zip(cb["frames"], cn["frames"])):
                if x != y:
                    keys = [k for k in set(x) | set(y) if x.get(k) != y.get(k)]
                    msgs.append(f"frame {i} differs in {keys}: {[(k, x.get(k), y.get(k)) for k in keys][:3]}")
                    break
            if len(cb["frames"]) != len(cn["frames"]):
                msgs.append(f"frame count {len(cb['frames'])} vs {len(cn['frames'])}")
        bb = [{k: v for k, v in b.items() if k != "edge"} for b in cb["bands"]]
        bn = [{k: v for k, v in b.items() if k != "edge"} for b in cn["bands"]]
        if bb != bn:
            msgs.append("bands differ")
        drop = ("configHash", "continuous")
        sb = {k: v for k, v in cb["summary"].items() if k not in drop}
        sn = {k: v for k, v in cn["summary"].items() if k not in drop}
        if sb != sn:
            msgs.append("summary differs: " + str([k for k in set(sb) | set(sn) if sb.get(k) != sn.get(k)]))
        rb, rn = load_result(bd), load_result(nd)
        fo = face_on_identity(rb, rn)
        if fo[0] == "DIFFER":
            msgs.append("face-on θ differs")
        pb, pn = planes(rb), planes(rn)
        if any(not (math.isnan(x) and math.isnan(y)) and x != y for x, y in zip(pb, pn)):
            msgs.append(f"fusion planes differ {pb} vs {pn}")
        # club3d frames minus the §3.2a keys (written only where an item ran; an OFF run writes none)
        c3b = (rb or {}).get("analysis", rb or {}).get("club3d") or {}
        c3n = (rn or {}).get("analysis", rn or {}).get("club3d") or {}
        newk = ("etaDeg", "anchorCond", "uBridged")
        fb = [{k: v for k, v in f.items() if k not in newk} for f in c3b.get("frames", [])]
        fn = [{k: v for k, v in f.items() if k not in newk} for f in c3n.get("frames", [])]
        if fb != fn:
            first = next((i for i, (x, y) in enumerate(zip(fb, fn)) if x != y), None)
            msgs.append(f"club3d frames differ (first at {first}, counts {len(fb)}/{len(fn)})")
        sumk = ("nDtlAnchored", "nDtlAnchorRefused", "reflectedBands", "backRmsBeforeReflectDeg")
        sb3 = {k: v for k, v in (c3b.get("summary") or {}).items() if k not in sumk}
        sn3 = {k: v for k, v in (c3n.get("summary") or {}).items() if k not in sumk}
        if sb3 != sn3:
            msgs.append("club3d summary differs: " + str([k for k in set(sb3) | set(sn3) if sb3.get(k) != sn3.get(k)]))
        print(f"{sid}: {'OK' if not msgs else 'DIFF ' + '; '.join(msgs)}")
        ok = ok and not msgs
    return 0 if ok else 1




# ── §3.2a per-item columns ───────────────────────────────────────────────────
def phase_times(res):
    a = res.get("analysis", res)
    ph = {}
    for e in a.get("phases", []):
        if isinstance(e, dict) and "phase" in e:
            ph.setdefault(e["phase"], e["t_us"])
    return ph.get(0), ph.get(2), ph.get(5)     # address, top, impact


def club3d_of(res):
    return (res or {}).get("analysis", res or {}).get("club3d") or {}


def err_deg(cf, cd, f, n, eta):
    u, _ = fg.deproject_eta(cf, f["thetaF"], n, eta)
    if u is None:
        return float("nan")
    th, _ = fg.project(cd, u)
    return abs(fg.wrap_deg(math.degrees(th - f["thetaD"])))


def item_a(sid, rb, rn, cdtl_n, fam_acc, lines):
    """Per swing: the C++ curve vs the refit; per band: in-plane / LOBO / edge / full errors."""
    c3 = club3d_of(rn)
    eta = c3.get("eta") or {}
    if not eta.get("fitted"):
        lines.append(f"| {sid.split('__swing_')[-1]} | no curve | | | | |")
        return
    addr, top, imp = phase_times(rn)
    cam = c3.get("camera") or {}
    cf = fg.face_on_camera(); cd = fg.dtl_camera(cam.get("yawDeg", 0), cam.get("pitchDeg", 0), cam.get("rollDeg", 0))
    nb = np.array(c3["planes"]["back"]["normal"]) if c3["planes"]["back"].get("fitted") else None
    nd = np.array(c3["planes"]["down"]["normal"]) if c3["planes"]["down"].get("fitted") else None
    plane = lambda t: nd if t >= top else nb
    F = c3["frames"]
    t0 = 0
    meas = [f for f in F if f["foSrc"] == "measured" and not [x for x in f["flags"] if x != "reflected"] and f.get("oopDeg") is not None and plane(f["t_us"]) is not None]
    if len(meas) < 4:
        lines.append(f"| {sid.split('__swing_')[-1]} | {len(meas)} frames | | | | |")
        return
    t = np.array([f["t_us"] for f in meas], float); oop = np.array([f["oopDeg"] for f in meas]); bd = np.array([f["dtlBand"] for f in meas])
    knots = np.array(eta["knotsUs"], float)
    lam, reach = eta.get("lambda", 4.0), eta.get("priorReachMs", 150.0) * 1000.0
    far, nearw, clampd = eta.get("priorFar", 1.0), eta.get("priorNear", 1e-3), eta.get("maxAbsDeg", 25.0)
    def fit(mask, reach_us=reach):
        v, _ = fg.fit_eta(t[mask], oop[mask], np.ones(int(mask.sum())), knots, lam=lam, prior_reach_us=reach_us, prior_far=far, prior_near=nearw)
        return np.clip(v, -clampd, clampd)
    v_all = fit(np.ones(len(t), bool))
    dknot = float(np.max(np.abs(v_all - np.array(eta["values"]))))
    # the C++ etaDeg on the frames vs this refit evaluated there
    dframe = max(abs(fg.eval_eta(knots, v_all, f["t_us"]) - f["etaDeg"]) for f in meas if f.get("etaDeg") is not None) if any(f.get("etaDeg") is not None for f in meas) else float("nan")
    lines.append(f"| {sid.split('__swing_')[-1]}{'w' if '06-11' in sid else 'i'} | n {eta['n']} knots {len(knots)} rms {eta['rmsDeg']:.2f}° | C++ vs refit {dknot:.4f}° at knots, {dframe:.4f}° on frames | | | |")
    bands = cdtl_n.get("bands", [])
    for b in sorted(set(bd)):
        sel = bd == b
        if sel.sum() < 3:
            continue
        fam = family(bands[b]["name"]) if 0 <= b < len(bands) else "after"
        v_lobo = fit(~sel); v_400 = fit(~sel, 400000.0)
        idx = np.where(sel)[0]
        edge = sorted(set(idx[:6]) | set(idx[-6:])) if len(idx) > 12 else list(idx)
        keep = np.ones(len(t), bool); keep[edge] = False
        v_edge = fit(keep)
        acc = fam_acc.setdefault(fam, {"inplane": [], "lobo": [], "lobo400": [], "full": [], "edge_in": [], "edge_eta": [], "bands": 0})
        acc["bands"] += 1
        for i in idx:
            f = meas[i]; n = plane(f["t_us"])
            acc["inplane"].append(err_deg(cf, cd, f, n, 0.0))
            acc["lobo"].append(err_deg(cf, cd, f, n, fg.eval_eta(knots, v_lobo, f["t_us"])))
            acc["lobo400"].append(err_deg(cf, cd, f, n, fg.eval_eta(knots, v_400, f["t_us"])))
            acc["full"].append(err_deg(cf, cd, f, n, fg.eval_eta(knots, v_all, f["t_us"])))
        for i in edge:
            f = meas[i]; n = plane(f["t_us"])
            acc["edge_in"].append(err_deg(cf, cd, f, n, 0.0))
            acc["edge_eta"].append(err_deg(cf, cd, f, n, fg.eval_eta(knots, v_edge, f["t_us"])))


def synth_on_measured(cdtl, fam_acc):
    """|Δθ_D| between the synth3d line and the tracker on the frames the tracker MEASURED, per family."""
    if not cdtl or not cdtl.get("synth3d"):
        return
    bands = cdtl.get("bands", [])
    meas = {f["t_us"]: f for f in cdtl["frames"] if f["tier"] in PUB and f.get("theta") is not None}
    for s in cdtl["synth3d"]:
        f = meas.get(s["t_us"])
        if not f or s.get("theta") is None:
            continue
        b = f.get("band", -1)
        fam = family(bands[b]["name"]) if 0 <= b < len(bands) else "after"
        fam_acc.setdefault(fam, []).append(abs(wrap_deg(math.degrees(s["theta"] - f["theta"]))))


def item_c(sid, rn, lines, tot):
    c3 = club3d_of(rn)
    sm = c3.get("summary") or {}
    addr, top, imp = phase_times(rn)
    F = c3.get("frames", [])
    conds = [f["anchorCond"] for f in F if f.get("anchorCond") is not None]
    anch = [f for f in F if "dtlAnchored" in f.get("flags", [])]
    meas = [(f["t_us"], np.array(f["u"])) for f in F if f["foSrc"] == "measured" and not f["flags"]]
    dang, nearer_a, nearer_b = [], 0, 0
    for f in anch:
        ua, ub = np.array(f["u"]), np.array(f["uBridged"])
        dang.append(fg.angle_deg(ua, ub))
        before = [m for m in meas if m[0] < f["t_us"]]; after = [m for m in meas if m[0] > f["t_us"]]
        ref = None
        if before and after and after[0][0] - before[-1][0] <= 200000:
            w = (f["t_us"] - before[-1][0]) / (after[0][0] - before[-1][0]); ref = fg.slerp(before[-1][1], after[0][1], w)
        elif before and f["t_us"] - before[-1][0] <= 150000:
            ref = before[-1][1]
        elif after and after[0][0] - f["t_us"] <= 150000:
            ref = after[0][1]
        if ref is not None:
            if fg.angle_deg(ua, ref) < fg.angle_deg(ub, ref): nearer_a += 1
            else: nearer_b += 1
    nb = sum(1 for f in F if f["foSrc"] == "bridged" and f.get("oopDeg") is not None)
    tot["bridged"] += nb; tot["anchored"] += sm.get("nDtlAnchored", 0); tot["refused"] += sm.get("nDtlAnchorRefused", 0)
    tot["conds"] += conds; tot["dang"] += dang; tot["nearer_a"] += nearer_a; tot["nearer_b"] += nearer_b
    lines.append(f"| {sid.split('__swing_')[-1]}{'w' if '06-11' in sid else 'i'} | {nb} | {sm.get('nDtlAnchored', 0)} | {sm.get('nDtlAnchorRefused', 0)} | "
                 f"{pct(conds, 50):.3f} / {max(conds) if conds else float('nan'):.3f} | {pct(dang, 50):.2f} | {nearer_a} / {nearer_b} |")


def item_d(sid, rb, rn, cdtl_n, lines, tot):
    cb, cn = club3d_of(rb), club3d_of(rn)
    def back(c):
        p = (c.get("planes") or {}).get("back") or {}
        return (bool((c.get("summary") or {}).get("backIncoherent")), p.get("oopRmsDeg"), p.get("inclDeg"), p.get("n"))
    ib, rb_, incb, nb_ = back(cb); inn, rn_, incn, nn_ = back(cn)
    refl = (cn.get("summary") or {}).get("reflectedBands") or []
    bands = cdtl_n.get("bands", [])
    names = ", ".join(bands[b]["name"] if 0 <= b < len(bands) else str(b) for b in refl)
    dwn = ((cb.get("planes") or {}).get("down") or {}).get("inclDeg") == ((cn.get("planes") or {}).get("down") or {}).get("inclDeg")
    f = lambda x: "-" if x is None else f"{x:.1f}"
    changed = (rb_ != rn_) or (incb != incn)
    if not ib and changed: tot["coherent_changed"] += 1
    if ib: tot["incoherent"] += 1
    if ib and not inn: tot["repaired"] += 1
    if refl: tot["with_reflection"] += 1
    lines.append(f"| {sid.split('__swing_')[-1]}{'w' if '06-11' in sid else 'i'} | {'INCOHERENT' if ib else 'coherent'} → {'INCOHERENT' if inn else 'coherent'} | "
                 f"{f(rb_)} → {f(rn_)} | {f(incb)} → {f(incn)} | {nb_} → {nn_} | {names or '-'} | {'same' if dwn else 'MOVED'} |")


def items_report(bases, news, out):
    if fg is None:
        out("(--items needs numpy and tools/shaftlab/fusion_geom.py)"); return
    a_lines, a_fam, a_syn_b, a_syn_n = [], {}, {}, {}
    c_lines, c_tot = [], {"bridged": 0, "anchored": 0, "refused": 0, "conds": [], "dang": [], "nearer_a": 0, "nearer_b": 0}
    d_lines, d_tot = [], {"coherent_changed": 0, "incoherent": 0, "repaired": 0, "with_reflection": 0}
    ran = {"eta": 0, "dtlAnchor": 0, "reflectBands": 0}
    for sid in sorted(news):
        if sid not in bases:
            continue
        rb, rn = load_result(bases[sid]), load_result(news[sid])
        cb, cn = load_club_dtl(bases[sid]), load_club_dtl(news[sid])
        if rn is None or cn is None:
            continue
        items = club3d_of(rn).get("items") or {}
        for k in ran: ran[k] += bool(items.get(k))
        if items.get("eta"):
            item_a(sid, rb, rn, cn, a_fam, a_lines)
            synth_on_measured(cb, a_syn_b); synth_on_measured(cn, a_syn_n)
        if items.get("dtlAnchor"):
            item_c(sid, rn, c_lines, c_tot)
        if items.get("reflectBands"):
            item_d(sid, rb, rn, cn, d_lines, d_tot)
    q = lambda v: f"{pct(v, 50):.2f} / {pct(v, 90):.2f}" if v else "-"
    if ran["eta"]:
        out(); out(f"### (A) η(t) — {ran['eta']} swings with the curve on")
        out("| swing | curve | C++ vs this refit | | | |"); out("|---|---|---|---|---|---|")
        for l in a_lines: out(l)
        out(); out("|Δθ_D| on the tracker's MEASURED frames, p50 / p90 (°), the face-on angle de-projected through the phase plane:")
        out("| family | bands | frames | in-plane | η leave-one-band-out (reach as run) | η LOBO, reach 400 ms | η full fit (in-band) | edge frames | edge in-plane | edge η |")
        out("|---|---|---|---|---|---|---|---|---|---|")
        for fam in FAMILIES:
            a = a_fam.get(fam)
            if not a: continue
            out(f"| {fam} | {a['bands']} | {len(a['inplane'])} | {q(a['inplane'])} | {q(a['lobo'])} | {q(a['lobo400'])} | {q(a['full'])} | {len(a['edge_in'])} | {q(a['edge_in'])} | {q(a['edge_eta'])} |")
        def better(fam):
            a = a_fam.get(fam)
            return a and pct(a["lobo"], 50) < pct(a["inplane"], 50) - 0.1
        def noworse():
            return all(pct(a["lobo"], 50) <= pct(a["inplane"], 50) + 0.1 for a in a_fam.values() if a["inplane"])
        out(f"gate (design §3.2a A): LOBO beats in-plane on mid-backswing: {'YES' if better('mid-backswing') else 'NO'}; on delivery: {'YES' if better('delivery') else 'NO'}; no family worse: {'YES' if noworse() else 'NO'}")
        if a_syn_n:
            out(); out("synth3d line vs the tracker on MEASURED frames, |Δθ_D| p50 / p90 (°), base → new:")
            out("| family | n base → new | base | new |"); out("|---|---|---|---|")
            for fam in FAMILIES:
                if fam in a_syn_n or fam in a_syn_b:
                    out(f"| {fam} | {len(a_syn_b.get(fam, []))} → {len(a_syn_n.get(fam, []))} | {q(a_syn_b.get(fam, []))} | {q(a_syn_n.get(fam, []))} |")
    if ran["dtlAnchor"]:
        out(); out(f"### (C) the DTL anchor — {ran['dtlAnchor']} swings with the anchor on")
        out("| swing | bridged frames in the windows | anchored | refused | cond p50 / max | ∠(anchored, bridged) p50 | nearer the neighbours: anchored / bridged |")
        out("|---|---|---|---|---|---|---|")
        for l in c_lines: out(l)
        out(f"total: bridged {c_tot['bridged']}, anchored {c_tot['anchored']}, refused {c_tot['refused']}; conditioning p50 {pct(c_tot['conds'], 50):.3f}, p90 {pct(c_tot['conds'], 90):.3f}, max {max(c_tot['conds']) if c_tot['conds'] else float('nan'):.3f} (refused below {0.26}); "
            f"nearer the neighbours: anchored {c_tot['nearer_a']} / bridged {c_tot['nearer_b']}")
    if ran["reflectBands"]:
        out(); out(f"### (D) the reflected band — {ran['reflectBands']} swings with the repair on")
        out("| swing | backswing base → new | back rms base → new | back incl base → new | n base → new | bands reflected | down plane |")
        out("|---|---|---|---|---|---|---|")
        for l in d_lines: out(l)
        out(f"incoherent swings {d_tot['incoherent']}, made coherent {d_tot['repaired']}, with a reflection kept {d_tot['with_reflection']}; coherent swings changed: {d_tot['coherent_changed']} (gate: 0)")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("base"); ap.add_argument("new")
    ap.add_argument("--truth", default=None)
    ap.add_argument("--md", default=None)
    ap.add_argument("--identical", action="store_true")
    ap.add_argument("--items", action="store_true", help="the §3.2a per-item columns (A / C / D)")
    a = ap.parse_args()
    if a.identical:
        return identical(a.base, a.new)

    lines = []
    def out(s=""):
        print(s); lines.append(s)

    fam_tot = {f: [0, 0, 0, 0, 0, 0] for f in FAMILIES}   # n, pubBase, pubNew, heldNew, bandsBase, bandsNew
    truth_tot = {"paired_base": 0, "paired_new": 0, "worse": 0, "errs_base": [], "errs_new": []}
    pins = {"endonBase": 0, "endonNew": 0, "lateEsc": 0, "endOnFirst": 0, "edgeBands": 0}
    plane_moves = []
    fo_rows = []
    out("| swing | band | fam | n | meas base→new | held | cov base→new | drawn | edge |")
    out("|---|---|---|---|---|---|---|---|---|")
    news = swings_in(a.new); bases = swings_in(a.base)
    for sid in sorted(news):
        if sid not in bases:
            continue
        cb, cn = load_club_dtl(bases[sid]), load_club_dtl(news[sid])
        if cb is None or cn is None:
            out(f"| {sid} | missing clubDtl |"); continue
        sb, sn = band_stats(cb), band_stats(cn)
        # pair bands by name; a new band with no base twin (edge) is its own row
        bmap = {b[1]: b for b in sb}
        for fam, name, n, pub, held, edge in sn:
            base = bmap.get(name)
            pb = base[3] if base else 0
            nb = base[2] if base else n
            covb = f"{pb / nb:.2f}" if base and nb else "-"
            covn = f"{pub / n:.2f}" if n else "-"
            drawn = f"{(pub + held) / n:.2f}" if n else "-"
            short = sid.split("__swing_")[-1] + ("w" if "06-11" in sid else "i")
            out(f"| {short} | {name} | {fam} | {n} | {pb}→{pub} | {held} | {covb}→{covn} | {drawn} | {'edge' if edge else ''} |")
            t = fam_tot[fam]
            t[0] += n; t[1] += pb; t[2] += pub; t[3] += held; t[5] += 1
            if base: t[4] += 1
        # base bands that vanished in new
        nmap = {b[1] for b in sn}
        for fam, name, n, pub, held, edge in sb:
            if name not in nmap:
                short = sid.split("__swing_")[-1] + ("w" if "06-11" in sid else "i")
                out(f"| {short} | {name} | {fam} | {n} | {pub}→GONE | - | - | - | |")
        pins["endonBase"] += cb["summary"].get("publishedInEndOn", 0)
        pins["endonNew"] += cn["summary"].get("publishedInEndOn", 0)
        cont = cn["summary"].get("continuous", {})
        pins["lateEsc"] += cont.get("lateEscapesRefused", 0)
        pins["endOnFirst"] += cont.get("endOnBeforeQuarantine", 0)
        pins["edgeBands"] += cont.get("edgeBands", 0)
        # truth
        if a.truth:
            tp = os.path.join(a.truth, sid + ".json")
            if os.path.exists(tp):
                tr = json.load(open(tp))
                eb, en = truth_errors(cb, tr), truth_errors(cn, tr)
                worse = 0
                for t, e1 in eb.items():
                    e2 = en.get(t)
                    if e1 is not None and e2 is not None and e2 > e1 + 0.5:
                        worse += 1
                    if e1 is not None and e2 is None:
                        worse += 1   # a truth frame that lost its pairing counts as worse
                pb_ = [e for e in eb.values() if e is not None]; pn_ = [e for e in en.values() if e is not None]
                truth_tot["paired_base"] += len(pb_); truth_tot["paired_new"] += len(pn_)
                truth_tot["worse"] += worse
                truth_tot["errs_base"] += pb_; truth_tot["errs_new"] += pn_
                out(f"|   truth {sid.split('__swing_')[-1]} | {len(tr.get('frames', []))} frames | paired {len(pb_)}→{len(pn_)} | p50 {pct(pb_, 50):.2f}→{pct(pn_, 50):.2f} | p90 {pct(pb_, 90):.2f}→{pct(pn_, 90):.2f} | max {max(pb_) if pb_ else float('nan'):.2f}→{max(pn_) if pn_ else float('nan'):.2f} | worse {worse} | | |")
        rb, rn = load_result(bases[sid]), load_result(news[sid])
        pbk, pdn = planes(rb); nbk, ndn = planes(rn)
        plane_moves.append((sid, pbk, nbk, pdn, ndn))
        fo_rows.append((sid,) + face_on_identity(rb, rn))

    out()
    out("| family | bands base→new | frames | measured base→new | cov base→new | held | drawn cov |")
    out("|---|---|---|---|---|---|---|")
    for fam in FAMILIES:
        n, pb, pn, h, bb, bn = fam_tot[fam]
        if n:
            out(f"| {fam} | {bb}→{bn} | {n} | {pb}→{pn} | {pb / n:.2f}→{pn / n:.2f} | {h} | {(pn + h) / n:.2f} |")
    out()
    out(f"publishedInEndOn base {pins['endonBase']} → new {pins['endonNew']}; lateEscapesRefused {pins['lateEsc']}; "
        f"endOnBeforeQuarantine {pins['endOnFirst']}; edgeBands {pins['edgeBands']}")
    if a.truth:
        eb, en = truth_tot["errs_base"], truth_tot["errs_new"]
        out(f"truth: paired {truth_tot['paired_base']}→{truth_tot['paired_new']}, p50 {pct(eb, 50):.2f}→{pct(en, 50):.2f}°, "
            f"p90 {pct(eb, 90):.2f}→{pct(en, 90):.2f}°, frames worse by >0.5°: {truth_tot['worse']}")
    out()
    out("| swing | back incl base→new | Δ | down incl base→new | Δ | face-on θ | metrics base→new |")
    out("|---|---|---|---|---|---|---|")
    fo_map = {r[0]: r for r in fo_rows}
    maxd = 0.0
    for sid, pbk, nbk, pdn, ndn in plane_moves:
        d1 = nbk - pbk if not (math.isnan(nbk) or math.isnan(pbk)) else float("nan")
        d2 = ndn - pdn if not (math.isnan(ndn) or math.isnan(pdn)) else float("nan")
        for d in (d1, d2):
            if not math.isnan(d):
                maxd = max(maxd, abs(d))
        fo = fo_map[sid]
        out(f"| {sid.split('__swing_')[-1]}{'w' if '06-11' in sid else 'i'} | {pbk:.1f}→{nbk:.1f} | {d1:+.2f} | {pdn:.1f}→{ndn:.1f} | {d2:+.2f} | {fo[1]} | {fo[2]}→{fo[3]} |")
    out(f"\nlargest fused-plane move: {maxd:.2f}°; face-on θ identical on {sum(1 for r in fo_rows if r[1] == 'identical')}/{len(fo_rows)} swings")
    if a.items:
        items_report(bases, news, out)
    if a.md:
        with open(a.md, "w") as f:
            f.write("\n".join(lines) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
