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

Every number here is reproducible from the two run roots and nothing else.
"""
import argparse, glob, json, math, os, re, sys

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
        print(f"{sid}: {'OK' if not msgs else 'DIFF ' + '; '.join(msgs)}")
        ok = ok and not msgs
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("base"); ap.add_argument("new")
    ap.add_argument("--truth", default=None)
    ap.add_argument("--md", default=None)
    ap.add_argument("--identical", action="store_true")
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
    if a.md:
        with open(a.md, "w") as f:
            f.write("\n".join(lines) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
