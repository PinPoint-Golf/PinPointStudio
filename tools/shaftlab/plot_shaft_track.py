#!/usr/bin/env python3
"""plot_shaft_track.py — visualise a club_track_v3 shaft track for one swing.

Draws the shaft line for every frame of the swing arc over an optional background
frame, so a whole swing reads as a strobe fan:

  * colour       — backswing (address→top) vs release (top→finish)
  * line style   — ESTIMATED (band/ray, measured from evidence) = solid;
                   PREDICTED (pred/recon, DP-bridged / arm-witness) = dashed
  * weight/alpha — band (pinned) heaviest, ray next, predicted faint

The per-swing renderer is `plot_swing(ax, ...)`; the corpus montage reuses it.

  plot_shaft_track.py <v3_csv> --anchors anchors.csv [--clip video.avi]
      [--clipmeta m.json] [--bg-frame N] [--out out.png] [--title T] [--full]
"""
import argparse, csv, json, math, os
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.patheffects as pe
from matplotlib.lines import Line2D

EST_TIERS = ("band", "ray")            # measured -> solid
PRED_TIERS = ("pred", "recon")         # inferred -> dashed
SWING_PHASES = ("backswing", "top", "downswing", "impact", "thru")

# three phase groups
C_BACK = "#E23B3B"                     # backswing (address -> top)        RED
C_DOWN = "#2E9E4F"                     # downswing -> impact               GREEN
C_POST = "#EAB308"                     # post-impact (thru -> finish)      YELLOW
PHASE_COLOR = {
    "addr": C_BACK, "backswing": C_BACK, "top": C_BACK,
    "downswing": C_DOWN, "impact": C_DOWN,
    "thru": C_POST, "finish": C_POST,
}
_HALO = [pe.Stroke(linewidth=2.0, foreground="black", alpha=0.32), pe.Normal()]


# ---- loading ----------------------------------------------------------
def load_track(path):
    return list(csv.DictReader(open(path)))


def load_anchors(path):
    d = {}
    for row in csv.reader(open(path)):
        if row and row[0].strip().lstrip("-").isdigit():
            d[int(row[0])] = (float(row[1]), float(row[2]))
    return d


def find_top(rows):
    tops = [int(r["frame"]) for r in rows if r["phase"] == "top"]
    if tops:
        return int(round(np.median(tops)))
    bs = [int(r["frame"]) for r in rows if r["phase"] == "backswing"]
    return max(bs) if bs else len(rows) // 2


def swing_span(rows):
    fs = [int(r["frame"]) for r in rows if r["phase"] in SWING_PHASES]
    return (min(fs), max(fs)) if fs else (0, len(rows) - 1)


def shaft_length_px(rows, anchors):
    """Representative grip->head length in px, from the band (exact-head) frames."""
    ls = []
    for r in rows:
        f = int(r["frame"])
        if r["tier"] == "band" and r["head_x"] and f in anchors:
            gx, gy = anchors[f]
            ls.append(math.hypot(float(r["head_x"]) - gx, float(r["head_y"]) - gy))
    return float(np.median(ls)) if ls else 280.0


def read_bg_frame(clip, frame_idx):
    import cv2
    cap = cv2.VideoCapture(clip)
    if not cap.isOpened():
        return None
    cap.set(cv2.CAP_PROP_POS_FRAMES, max(0, int(frame_idx)))
    ok, fr = cap.read()
    cap.release()
    if not ok:
        return None
    return cv2.cvtColor(fr, cv2.COLOR_BGR2GRAY)


# ---- per-swing render (reused by the montage) -------------------------
def plot_swing(ax, rows, anchors, top, Lmed, lo, hi, bg=None, W=None, H=None,
               title=None, grip_path=True):
    if bg is not None:
        ax.imshow(bg, cmap="gray", alpha=0.4, zorder=0, aspect="equal", vmax=300)
    for r in rows:
        f = int(r["frame"])
        if f < lo or f > hi or f not in anchors:
            continue
        gx, gy = anchors[f]
        th = math.radians(float(r["theta_deg"]))
        if r["tier"] == "band" and r["head_x"]:
            hx, hy = float(r["head_x"]), float(r["head_y"])
        else:
            hx, hy = gx + Lmed * math.cos(th), gy + Lmed * math.sin(th)
        est = r["tier"] in EST_TIERS
        col = PHASE_COLOR.get(r["phase"], C_BACK)
        ls = "-" if est else (0, (4, 2.5))
        lw = 2.55 if r["tier"] == "band" else (1.65 if est else 1.8)   # 50% thicker
        a = 0.9 if r["tier"] == "band" else (0.62 if est else 0.72)
        ax.plot([gx, hx], [gy, hy], color=col, linestyle=ls, linewidth=lw,
                alpha=a, solid_capstyle="round", zorder=3, path_effects=_HALO)
    if grip_path:
        gs = [anchors[int(r["frame"])] for r in rows
              if lo <= int(r["frame"]) <= hi and int(r["frame"]) in anchors]
        if gs:
            ax.plot([p[0] for p in gs], [p[1] for p in gs], color="#999",
                    lw=0.8, alpha=0.45, zorder=2)
    ax.set_aspect("equal")
    if W and H:
        ax.set_xlim(0, W); ax.set_ylim(H, 0)
    else:
        # auto range from drawn geometry, y down
        ax.invert_yaxis()
    ax.set_xticks([]); ax.set_yticks([])
    for s in ax.spines.values():
        s.set_visible(False)
    if title:
        ax.set_title(title, fontsize=10)


# ---- stats (annotation source; the montage will order on these) -------
def swing_stats(rows, lo, hi):
    win = [r for r in rows if lo <= int(r["frame"]) <= hi]
    n = len(win)
    c = {t: sum(1 for r in win if r["tier"] == t) for t in ("band", "ray", "pred", "recon")}
    meas = c["band"] + c["ray"]
    confs = [float(r["conf"]) for r in win if r["tier"] in EST_TIERS]
    return dict(n=n, coverage=meas / n if n else 0.0, meas=meas, **c,
                mean_conf=float(np.mean(confs)) if confs else 0.0)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("v3_csv")
    ap.add_argument("--anchors", required=True)
    ap.add_argument("--clip", default=None, help="video for a faint background frame")
    ap.add_argument("--clipmeta", default=None, help="clipmeta.json (for W,H)")
    ap.add_argument("--bg-frame", type=int, default=None,
                    help="frame index for the background (default: takeaway start)")
    ap.add_argument("--out", default="shaft_track.png")
    ap.add_argument("--title", default=None)
    ap.add_argument("--full", action="store_true", help="draw the whole clip, not just the swing arc")
    a = ap.parse_args()

    rows = load_track(a.v3_csv)
    anchors = load_anchors(a.anchors)
    top = find_top(rows)
    lo, hi = (int(rows[0]["frame"]), int(rows[-1]["frame"])) if a.full else swing_span(rows)
    Lmed = shaft_length_px(rows, anchors)
    W = H = None
    if a.clipmeta and os.path.exists(a.clipmeta):
        cm = json.load(open(a.clipmeta)); W, H = cm.get("W"), cm.get("H")
    bg = None
    if a.clip:
        bg = read_bg_frame(a.clip, a.bg_frame if a.bg_frame is not None else lo)
        if bg is not None and (W is None or H is None):
            H, W = bg.shape[:2]

    st = swing_stats(rows, lo, hi)
    fig, ax = plt.subplots(figsize=(7.5, 6.0), dpi=140)
    plot_swing(ax, rows, anchors, top, Lmed, lo, hi, bg=bg, W=W, H=H,
               title=a.title or os.path.basename(os.path.dirname(a.v3_csv)) or "shaft track")

    legend = [
        Line2D([0], [0], color=C_BACK, lw=3, ls="-", label="backswing"),
        Line2D([0], [0], color=C_DOWN, lw=3, ls="-", label="downswing–impact"),
        Line2D([0], [0], color=C_POST, lw=3, ls="-", label="post-impact"),
        Line2D([0], [0], color="#555", lw=2.4, ls="-", label="estimated"),
        Line2D([0], [0], color="#555", lw=2.2, ls=(0, (4, 2.5)), label="predicted"),
    ]
    ax.legend(handles=legend, loc="upper left", fontsize=8, framealpha=0.85,
              facecolor="white", edgecolor="none", ncol=1)
    stat = (f"swing f{lo}–{hi}  ({st['n']} frames)\n"
            f"coverage {100*st['coverage']:.0f}%  (band {st['band']} · ray {st['ray']} · "
            f"pred {st['pred']} · recon {st['recon']})\n"
            f"mean conf {st['mean_conf']:.2f}")
    ax.text(0.01, 0.01, stat, transform=ax.transAxes, fontsize=8, va="bottom",
            ha="left", family="monospace",
            bbox=dict(boxstyle="round,pad=0.4", fc="white", ec="none", alpha=0.8))
    fig.tight_layout()
    fig.savefig(a.out, bbox_inches="tight")
    print(f"wrote {a.out}  (swing f{lo}-{hi}, top f{top}, Lmed {Lmed:.0f}px, "
          f"coverage {100*st['coverage']:.0f}%)")


if __name__ == "__main__":
    main()
