#!/usr/bin/env python3
"""fuse_grade.py -- grade a shaft-fusion run against its fusion-off control.

    fuse_grade.py <run-root> <control-root> [--md]

Both roots hold one folder per swing with a swinglab_run result.json. Reports, per swing:
the fused planes and the disagreement counts (analysis.club3d), the club node of the
kinematic sequence through the fused plane against the control's ellipse plane, and a
PARITY line: every key of the two result.json documents that differs, so "fusion moved
the club rate and nothing else" is a measurement and not a hope.
"""
import json, os, sys

IGNORE = {"timings"}


def load(p):
    d = json.load(open(p))
    return d.get("analysis", d)


def node(a, seg="club"):
    ks = a.get("kinematicSequence") or {}
    for n in ks.get("nodes", []):
        if str(n.get("segment", "")).lower().startswith(seg):
            return n
    return {}


def diff_keys(a, b, path=""):
    out = []
    if isinstance(a, dict) and isinstance(b, dict):
        for k in sorted(set(a) | set(b)):
            if path == "" and k in IGNORE:
                continue
            if k not in a or k not in b:
                out.append(path + k + (" (only run)" if k in a else " (only control)"))
            else:
                out += diff_keys(a[k], b[k], path + k + ".")
    elif a != b:
        out.append(path.rstrip("."))
    return out


def top(keys):
    """collapse to the first two path elements; metrics[] series are named by key"""
    s = set()
    for k in keys:
        parts = k.split(".")
        s.add(".".join(parts[:2]))
    return sorted(s)


def main():
    run, ctl = sys.argv[1], sys.argv[2]
    rows = []
    for sw in sorted(os.listdir(run)):
        rp, cp = os.path.join(run, sw, "result.json"), os.path.join(ctl, sw, "result.json")
        if not (os.path.isfile(rp) and os.path.isfile(cp)):
            continue
        a, c = load(rp), load(cp)
        f = a.get("club3d") or {}
        pl, sm = f.get("planes", {}), f.get("summary", {})
        dn, bk = pl.get("down", {}), pl.get("back", {})
        na, nc = node(a), node(c)
        cp_ = (c.get("club") or {}).get("plane", {})
        ch = cp_.get("synth" if cp_.get("channel") == 1 else "measured", {})
        # metrics: compare by series key
        ma = {m["key"]: m for m in a.get("metrics", [])} if isinstance(a.get("metrics"), list) else {}
        mc = {m["key"]: m for m in c.get("metrics", [])} if isinstance(c.get("metrics"), list) else {}
        mdiff = sorted(k for k in set(ma) | set(mc) if ma.get(k) != mc.get(k))
        a2 = {k: v for k, v in a.items() if k != "metrics"}
        c2 = {k: v for k, v in c.items() if k != "metrics"}
        rows.append(dict(sw=sw.replace("_Mark-Liversedge_Wrist_01__swing_", " s"), dn=dn, bk=bk, sm=sm,
                         na=na, nc=nc, ch=ch, other=top(diff_keys(a2, c2)), mdiff=mdiff))
    g = lambda d, k, f="%.1f": (f % d[k]) if isinstance(d.get(k), (int, float)) else "-"
    print("| swing | fused/pub | bridged | sign | offPl | back incl/rms | down incl | heading | rms | n | offered | ellipse k@node | fused k@node | club peak °/s ctl→fused | club t ms ctl→fused | route |")
    print("|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|")
    for r in rows:
        sm, dn, bk, na, nc, ch = r["sm"], r["dn"], r["bk"], r["na"], r["nc"], r["ch"]
        tms = lambda n: ("%.0f" % n["tPeakMs"]) if isinstance(n.get("tPeakMs"), (int, float)) else ("%.0f" % (n["tPeakUs"] / 1e3) if isinstance(n.get("tPeakUs"), (int, float)) else "-")
        print("| %s | %s/%s | %s | %s | %s | %s/%s%s | %s | %s | %s | %s | %s | %s@%s | %s@%s | %s→%s | %s→%s | %s |" % (
            r["sw"], sm.get("nFused", "-"), sm.get("nDtlPublished", "-"), sm.get("nBridged", "-"),
            sm.get("nSignDisagree", "-"), sm.get("nOffPlane", "-"), g(bk, "inclDeg"), g(bk, "oopRmsDeg"),
            " ⚠" if sm.get("backIncoherent") else "", g(dn, "inclDeg"), g(dn, "azimDeg"), g(dn, "oopRmsDeg", "%.2f"),
            dn.get("n", "-"), "yes" if dn.get("offered") else "NO", g(ch, "ratioDown", "%.3f"), g(ch, "nodeDownDeg", "%.0f"),
            g(dn, "foRatio", "%.3f"), g(dn, "foNodeDeg", "%.0f"), g(nc, "peakDps", "%.0f"), g(na, "peakDps", "%.0f"),
            tms(nc), tms(na), na.get("routeId", "-")))
    print()
    print("PARITY — what differs from the control besides `timings`:")
    from collections import Counter
    co, cm = Counter(), Counter()
    for r in rows:
        co[tuple(r["other"])] += 1
        cm[tuple(r["mdiff"])] += 1
    for k, n in co.most_common():
        print("  %2d swings: analysis keys %s" % (n, list(k)))
    for k, n in cm.most_common():
        print("  %2d swings: metric series %s" % (n, list(k)))


if __name__ == "__main__":
    main()
