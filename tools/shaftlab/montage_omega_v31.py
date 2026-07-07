#!/usr/bin/env python3
"""montage_omega_v31.py — v3.1 impact-zone ω corpus montage.

One panel per swing: the impact-zone angular speed |ω|(t) from two INDEPENDENT
measures — the v3.0 DP **track** (|central-difference θ|) and the **exposure-arc**
(the intra-frame streak extent, which needs no DP) — plus the emitted smoothed ω.
Their agreement IS the v3.1 result: an independent confirmation of the impact
speed. Panels are ordered by that agreement (tightest first). Peaks are annotated
in deg/frame and in clubhead mph.

  montage_omega_v31.py --corpus-dir <dir with sNN/faceon_swing_v31_impact.csv> \
      --lab-dir <tape_dir> --clubs clubs.json --club "7 IRON" \
      [--cols 3] [--label ...] [--out montage.png]
"""
import argparse, csv, json, math, os
import numpy as np
from scipy.ndimage import median_filter, gaussian_filter1d
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.lines import Line2D


def _smooth(y):
    """Interpolate NaNs then median+Gaussian — the same de-spike the tool applies
    to the raw exposure-arc before reading its peak (so montage peaks match)."""
    y = np.asarray(y, float)
    ok = ~np.isnan(y)
    if ok.sum() < 3:
        return y
    yi = np.interp(np.arange(len(y)), np.flatnonzero(ok), y[ok])
    return gaussian_filter1d(median_filter(yi, 3), 1.2)

C_TRACK = "#B8B8B8"      # DP track |ω|
C_ARC = "#E8A200"        # exposure-arc (independent)
C_EMIT = "#EF5B4C"       # emitted ω


def to_mph(omega_degf, fps, R_m):
    return abs(omega_degf) * fps * math.radians(1.0) * R_m * 2.2369363


def load_swing(cdir, s, lab_dir, R_m):
    p = os.path.join(cdir, s, "faceon_swing_v31_impact.csv")
    if not os.path.exists(p):
        return None
    rows = list(csv.DictReader(open(p)))
    if not rows:
        return None
    cm = json.load(open(os.path.join(lab_dir, s, "clipmeta.json")))
    fps = cm["fps"]
    def fnum(x):
        return float(x) if x not in ("", None) else np.nan
    f = np.array([int(r["frame"]) for r in rows])
    wt = np.abs(np.array([fnum(r["omega_track"]) for r in rows]))   # raw DP-track |dθ/dt|
    ar = np.array([fnum(r["omega_exparc"]) for r in rows])          # raw exposure-arc
    em = np.array([fnum(r["omega_emit"]) for r in rows])            # emitted (smoothed track)
    ar_s = _smooth(ar)                                              # smoothed exposure-arc
    impf = int(round(float(np.median(f))))            # window is symmetric on impact
    pk_track, pk_arc = float(np.nanmax(em)), float(np.nanmax(ar_s))  # smoothed peaks
    agree = abs(pk_track - pk_arc)                     # PEAK agreement (the number §4.5 reports)
    return dict(s=s, f=f, wt=wt, ar=ar, ar_s=ar_s, em=em, impf=impf, fps=fps, agree=agree,
                pk_track=pk_track, pk_arc=pk_arc,
                mph_track=to_mph(pk_track, fps, R_m), mph_arc=to_mph(pk_arc, fps, R_m))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--corpus-dir", required=True)
    ap.add_argument("--lab-dir", required=True)
    ap.add_argument("--clubs", required=True)
    ap.add_argument("--club", required=True)
    ap.add_argument("--cols", type=int, default=3)
    ap.add_argument("--label", default="")
    ap.add_argument("--out", default="montage_omega.png")
    a = ap.parse_args()

    R_m = float(json.load(open(a.clubs))[a.club]["lengthMm"]) / 1000.0
    swings = sorted(d for d in os.listdir(a.corpus_dir)
                    if d.startswith("s") and d[1:].isdigit())
    recs = [r for r in (load_swing(a.corpus_dir, s, a.lab_dir, R_m) for s in swings) if r]
    recs.sort(key=lambda r: r["agree"])                # tightest agreement first
    n = len(recs)
    cols = min(a.cols, n) or 1
    rows_n = math.ceil(n / cols)
    plt.style.use("dark_background")
    fig, axes = plt.subplots(rows_n, cols, figsize=(cols * 3.9, rows_n * 3.0), dpi=140)
    axes = np.atleast_1d(axes).ravel()
    # y-scale from the SMOOTHED series (raw-arc noise spikes clip, they're outliers)
    ymax = max(max(np.nanmax(r["em"]), np.nanmax(r["ar_s"])) for r in recs) * 1.25

    for i, ax in enumerate(axes):
        if i >= n:
            ax.axis("off"); continue
        r = recs[i]
        x = r["f"] - r["impf"]
        ax.plot(x, r["wt"], color=C_TRACK, lw=0, alpha=0.7, marker=".", ms=4)          # raw track
        ax.plot(x, r["ar"], color=C_ARC, lw=0, alpha=0.45, marker=".", ms=4)           # raw arc
        ax.plot(x, r["ar_s"], color=C_ARC, lw=2.0, alpha=0.95)                          # smoothed arc
        ax.plot(x, r["em"], color=C_EMIT, lw=2.4, alpha=0.97)                           # emitted ω
        ax.axvline(0, color="#E0554A", lw=0.8, alpha=0.55)
        ax.set_ylim(0, ymax)
        ax.set_xlabel("frame − impact", fontsize=7.5, color="#AAA")
        ax.tick_params(labelsize=7, colors="#999")
        ax.set_title(f"{r['s']}  ·  #{i+1}   agree {r['agree']:.1f}°/f", fontsize=9.5,
                     fontweight="bold")
        txt = (f"peak  track {r['mph_track']:.0f} · arc {r['mph_arc']:.0f} mph\n"
               f"      ({r['pk_track']:.1f} · {r['pk_arc']:.1f} °/f)")
        ax.text(0.02, 0.97, txt, transform=ax.transAxes, fontsize=7.4, va="top",
                ha="left", family="monospace", color="#DDD")
        for sp in ("top", "right"):
            ax.spines[sp].set_visible(False)

    legend = [
        Line2D([0], [0], color=C_TRACK, lw=0, marker=".", ms=9, label="DP track  |dθ/dt| (raw)"),
        Line2D([0], [0], color=C_ARC, lw=2.4, marker=".", label="exposure-arc, smoothed (independent of DP)"),
        Line2D([0], [0], color=C_EMIT, lw=2.6, label="emitted ω"),
    ]
    fig.legend(handles=legend, loc="lower center", ncol=3, fontsize=9.5,
               frameon=False, bbox_to_anchor=(0.5, 0.004))
    sup = "Shaft-tracker corpus — v3.1 impact-zone ω: the exposure-arc independently confirms the DP-track speed"
    if a.label:
        sup += f"\n{a.label}"
    fig.suptitle(sup, fontsize=12, fontweight="bold")
    fig.tight_layout(rect=[0, 0.05, 1, 0.95], h_pad=3.0, w_pad=1.6)
    fig.savefig(a.out, bbox_inches="tight", facecolor=fig.get_facecolor())
    print(f"wrote {a.out}  ({n} swings)")
    for i, r in enumerate(recs):
        print(f"  #{i+1} {r['s']}  agree {r['agree']:.2f}°/f  "
              f"peak track {r['mph_track']:.0f}mph / arc {r['mph_arc']:.0f}mph")


if __name__ == "__main__":
    main()
