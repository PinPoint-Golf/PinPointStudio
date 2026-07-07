#!/usr/bin/env python3
"""montage_shaft_track.py — a corpus montage of shaft-tracker swings.

One panel per swing (the `plot_shaft_track.plot_swing` strobe), ordered by the
tracker's SELF-CONFIDENCE — coverage x band-fraction x mean-conf — most confident
first, so a corpus reads best-tracked -> worst at a glance. Each panel is
annotated with that score plus coverage, the tier breakdown, the release
psi-violation count, and (where v2 truth exists) the median theta error as an
independent accuracy check. Run it once per tracker iteration to get one montage
figure per row of the research doc's results table.

  montage_shaft_track.py --lab-dir <tape_dir> --out-root <dir with sNN_on/> \
      [--suffix _on] [--truth-remap C:/PinPointStudio=/mnt/swingdata] \
      [--cols 5] [--label "v3.0-r1 (impact-only)"] [--out montage.png]
"""
import argparse, csv, json, math, os
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.lines import Line2D

import plot_shaft_track as P   # plot_swing + loaders live here (same dir)

# free window for the release psi-violation count (PSI_WIN_BACK / _FWD in the tool)
PSI_BACK, PSI_FWD = 3, 12


def wd(a, b):
    return abs((a - b + 180.0) % 360.0 - 180.0)


def release_psi_viol(rows, phases_csv, top):
    """Re-hinge count on the output track through downswing/impact/thru, outside
    the free top window. phi_s comes from the *_phases.csv the tool emits."""
    if not os.path.exists(phases_csv):
        return None
    phi = {int(x["frame"]): float(x["phi_s"]) for x in csv.DictReader(open(phases_csv))}
    th = {int(r["frame"]): float(r["theta_deg"]) for r in rows}
    lo, hi = top - PSI_BACK, top + PSI_FWD
    fs = sorted(int(r["frame"]) for r in rows
                if r["phase"] in ("downswing", "impact", "thru")
                and not (lo <= int(r["frame"]) <= hi))
    v = 0
    for a, b in zip(fs, fs[1:]):
        if b != a + 1 or b not in phi or a not in phi:
            continue
        d = ((th[b] - phi[b]) - (th[a] - phi[a]) + 180) % 360 - 180
        if d > 2.0:
            v += 1
    return v


def accuracy_vs_truth(rows, clipmeta, remap, lo, hi):
    """Median |theta - theta_v2| over swing frames, if the swing's truth.json
    (v2 fusion) is reachable. Returns (median_deg, n) or (None, 0)."""
    swingdir = clipmeta.get("swingDir", "")
    for a, b in remap:
        if swingdir.startswith(a):
            swingdir = b + swingdir[len(a):]
    tj = os.path.join(swingdir, "truth.json")
    if not os.path.exists(tj):
        return None, 0
    tt = np.array(clipmeta["t_us"], float)
    truth = {int(np.argmin(np.abs(tt - e["t_us"]))): math.degrees(e["theta"]) % 360.0
             for e in json.load(open(tj))["shaft"]}
    th = {int(r["frame"]): float(r["theta_deg"]) for r in rows}
    errs = [wd(th[f], truth[f]) for f in truth if lo <= f <= hi and f in th]
    return (float(np.median(errs)), len(errs)) if errs else (None, 0)


def gather(lab_dir, out_root, suffix, remap):
    """One record per swing: rows, anchors, bg, geometry, stats, confidence, accuracy."""
    swings = sorted(d for d in os.listdir(lab_dir)
                    if d.startswith("s") and d[1:].isdigit()
                    and os.path.isdir(os.path.join(lab_dir, d)))
    recs = []
    for s in swings:
        v3 = os.path.join(out_root, f"{s}{suffix}", "faceon_swing_v3.csv")
        sd = os.path.join(lab_dir, s)
        anch_p = os.path.join(sd, "anchors.csv")
        if not (os.path.exists(v3) and os.path.exists(anch_p)):
            continue
        rows = P.load_track(v3)
        anchors = P.load_anchors(anch_p)
        cm = json.load(open(os.path.join(sd, "clipmeta.json")))
        top = P.find_top(rows)
        lo, hi = P.swing_span(rows)
        Lmed = P.shaft_length_px(rows, anchors)
        st = P.swing_stats(rows, lo, hi)
        band_frac = st["band"] / st["meas"] if st["meas"] else 0.0
        conf = st["coverage"] * band_frac * st["mean_conf"]          # self-confidence
        viol = release_psi_viol(rows, os.path.join(out_root, f"{s}{suffix}",
                                                   "faceon_swing_v3_phases.csv"), top)
        err, nerr = accuracy_vs_truth(rows, cm, remap, lo, hi)
        bg = P.read_bg_frame(os.path.join(sd, "faceon_swing.avi"), lo)
        recs.append(dict(s=s, rows=rows, anchors=anchors, top=top, lo=lo, hi=hi,
                         Lmed=Lmed, bg=bg, W=cm.get("W"), H=cm.get("H"),
                         st=st, conf=conf, viol=viol, err=err))
    return recs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--lab-dir", required=True)
    ap.add_argument("--out-root", required=True, help="dir holding sNN<suffix>/faceon_swing_v3.csv")
    ap.add_argument("--suffix", default="_on")
    ap.add_argument("--truth-remap", default="C:/PinPointStudio=/mnt/swingdata",
                    help="FROM=TO prefix remap to reach each swing's truth.json")
    ap.add_argument("--cols", type=int, default=3,
                    help="panels per row (3 keeps them legible; 12+ swings tile 4x3)")
    ap.add_argument("--label", default="")
    ap.add_argument("--show-err", action="store_true",
                    help="annotate per-swing median error vs v2 truth (only where "
                         "truth is reachable; off by default so panels stay consistent)")
    ap.add_argument("--out", default="montage.png")
    a = ap.parse_args()

    remap = []
    if a.truth_remap:
        f, t = a.truth_remap.split("=", 1); remap = [(f, t)]

    recs = gather(a.lab_dir, a.out_root, a.suffix, remap)
    recs.sort(key=lambda r: -r["conf"])                              # most confident first
    n = len(recs)
    cols = min(a.cols, n) or 1
    rows_n = math.ceil(n / cols)
    fig, axes = plt.subplots(rows_n, cols, figsize=(cols * 3.35, rows_n * 3.9), dpi=140)
    axes = np.atleast_1d(axes).ravel()

    for i, ax in enumerate(axes):
        if i >= n:
            ax.axis("off"); continue
        r = recs[i]
        P.plot_swing(ax, r["rows"], r["anchors"], r["top"], r["Lmed"], r["lo"], r["hi"],
                     bg=r["bg"], W=r["W"], H=r["H"], title=None, grip_path=True)
        ax.set_title(f"{r['s']}  ·  #{i+1}   conf {r['conf']:.3f}", fontsize=9.5,
                     fontweight="bold")
        st = r["st"]
        line = (f"cov {100*st['coverage']:.0f}%   b{st['band']} r{st['ray']} "
                f"p{st['pred']} rc{st['recon']}")
        extra = []
        if r["viol"] is not None:
            extra.append(f"ψ-viol {r['viol']}")
        if a.show_err and r["err"] is not None:
            extra.append(f"err {r['err']:.1f}°")
        sub = line + ("\n" + "   ".join(extra) if extra else "")
        ax.text(0.015, 0.015, sub, transform=ax.transAxes, fontsize=7.6, va="bottom",
                ha="left", family="monospace",
                bbox=dict(boxstyle="round,pad=0.3", fc="white", ec="none", alpha=0.78))

    legend = [
        Line2D([0], [0], color=P.C_BACK, lw=3, ls="-", label="backswing"),
        Line2D([0], [0], color=P.C_DOWN, lw=3, ls="-", label="downswing–impact"),
        Line2D([0], [0], color=P.C_POST, lw=3, ls="-", label="post-impact"),
        Line2D([0], [0], color="#555", lw=2.4, ls="-", label="estimated"),
        Line2D([0], [0], color="#555", lw=2.2, ls=(0, (4, 2.5)), label="predicted"),
    ]
    fig.legend(handles=legend, loc="lower center", ncol=5, fontsize=9,
               frameon=False, bbox_to_anchor=(0.5, 0.005))
    sup = "Shaft-tracker corpus — ordered by self-confidence (coverage × band-fraction × mean-conf)"
    if a.label:
        sup += f"\n{a.label}"
    fig.suptitle(sup, fontsize=12, fontweight="bold")
    fig.tight_layout(rect=[0, 0.05, 1, 0.95], h_pad=3.2, w_pad=1.4)
    fig.savefig(a.out, bbox_inches="tight")
    print(f"wrote {a.out}  ({n} swings)")
    print(f"{'rank':>4} {'swing':6} {'conf':>7} {'cov%':>5} {'band':>4} {'ray':>4} "
          f"{'pred':>4} {'recon':>5} {'ψviol':>5} {'err°':>5}")
    for i, r in enumerate(recs):
        st = r["st"]
        vstr = "-" if r["viol"] is None else str(r["viol"])
        estr = "-" if r["err"] is None else f"{r['err']:.1f}"
        print(f"{i+1:>4} {r['s']:6} {r['conf']:>7.3f} {100*st['coverage']:>5.0f} "
              f"{st['band']:>4} {st['ray']:>4} {st['pred']:>4} {st['recon']:>5} "
              f"{vstr:>5} {estr:>5}")


if __name__ == "__main__":
    main()
