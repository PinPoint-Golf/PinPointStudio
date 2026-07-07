#!/usr/bin/env python3
"""montage_address_v32.py — v3.2 address / resting-club recovery corpus montage.

One panel per swing: the hold-period STACK (the address integrated on the grip so
the still club sharpens while the swaying body/legs blur), with the shaft v3.2
recovers drawn on it — the resting club v3.0 deliberately punts to `pred`. On each
stack: **red = recovered resting shaft** (θ0), **blue = lead-arm φ** (the tight
address cone), **green = grip**. Ordered by how much of the hold v3.2 published
(most first). Annotated with θ0, the published-frame count, and the angular
stability (std over the hold).

  montage_address_v32.py --corpus-dir <dir with sNN/faceon_swing_v32_address.*> \
      [--cols 3] [--label ...] [--out montage.png]
"""
import argparse, csv, math, os
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.lines import Line2D


def swing_stats(cdir, s):
    d = os.path.join(cdir, s)
    png = os.path.join(d, "faceon_swing_v32_address.png")
    csvp = os.path.join(d, "faceon_swing_v32_address.csv")
    if not os.path.exists(csvp):
        return None
    rows = list(csv.DictReader(open(csvp)))            # may be empty (no hold detected)
    hold = [r for r in rows if r["tier"] == "hold"]
    tf = [float(r["theta_frame"]) for r in hold if r["theta_frame"]]
    return dict(s=s, png=png if os.path.exists(png) else None,
                pub=len(hold), n=len(rows),
                th0=float(hold[0]["theta_deg"]) if hold else float("nan"),
                std=float(np.std(tf)) if tf else float("nan"))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--corpus-dir", required=True)
    ap.add_argument("--cols", type=int, default=3)
    ap.add_argument("--label", default="")
    ap.add_argument("--out", default="montage_address.png")
    a = ap.parse_args()

    swings = sorted(d for d in os.listdir(a.corpus_dir)
                    if d.startswith("s") and d[1:].isdigit())
    recs = [r for r in (swing_stats(a.corpus_dir, s) for s in swings) if r]
    recs.sort(key=lambda r: (-r["pub"],                   # most published, then tightest
                             r["std"] if not math.isnan(r["std"]) else 1e9))
    n = len(recs)
    cols = min(a.cols, n) or 1
    rows_n = math.ceil(n / cols)
    fig, axes = plt.subplots(rows_n, cols, figsize=(cols * 3.4, rows_n * 3.7), dpi=140)
    axes = np.atleast_1d(axes).ravel()

    for i, ax in enumerate(axes):
        if i >= n:
            ax.axis("off"); continue
        r = recs[i]
        has_hold = not math.isnan(r["th0"])
        if r["png"]:
            ax.imshow(plt.imread(r["png"]))
        else:                                            # no address hold detected
            ax.imshow(np.full((10, 10), 0.07), cmap="gray", vmin=0, vmax=1)
            ax.text(0.5, 0.5, "no address hold\ndetected", transform=ax.transAxes,
                    ha="center", va="center", fontsize=11, color="#888")
        ax.set_xticks([]); ax.set_yticks([])
        for sp in ax.spines.values():
            sp.set_visible(False)
        ttl = f"{r['s']}  ·  #{i+1}   θ0 {r['th0']:.1f}°" if has_hold else f"{r['s']}  ·  #{i+1}"
        ax.set_title(ttl, fontsize=9.5, fontweight="bold")
        if r["pub"]:
            sub = f"published {r['pub']}/{r['n']}\nstd {r['std']:.1f}°"
        elif r["n"] > 0:                                 # hold found, gates rejected it
            sub = f"abstained (gates)\nhold {r['n']} frames"
        else:                                            # no stable hold at all
            sub = "abstained (no hold)"
        ax.text(0.03, 0.03, sub, transform=ax.transAxes, fontsize=7.8, va="bottom",
                ha="left", family="monospace", color="white",
                bbox=dict(boxstyle="round,pad=0.3", fc="black", ec="none", alpha=0.5))

    legend = [
        Line2D([0], [0], color="red", lw=3, label="recovered resting shaft (θ0)"),
        Line2D([0], [0], color="deepskyblue", lw=3, label="lead-arm φ (address cone)"),
        Line2D([0], [0], marker="o", color="lime", lw=0, ms=8, label="grip anchor"),
    ]
    fig.legend(handles=legend, loc="lower center", ncol=3, fontsize=9.5,
               frameon=False, bbox_to_anchor=(0.5, 0.004))
    sup = ("Shaft-tracker corpus — v3.2 address recovery: the resting club v3.0 punts to "
           "`pred`, measured off the hold-period stack")
    if a.label:
        sup += f"\n{a.label}"
    fig.suptitle(sup, fontsize=12, fontweight="bold")
    fig.tight_layout(rect=[0, 0.05, 1, 0.95], h_pad=2.6, w_pad=1.0)
    fig.savefig(a.out, bbox_inches="tight")
    print(f"wrote {a.out}  ({n} swings)")
    for i, r in enumerate(recs):
        print(f"  #{i+1} {r['s']}  θ0 {r['th0']:.1f}°  published {r['pub']}/{r['n']}  std {r['std']:.1f}°")


if __name__ == "__main__":
    main()
