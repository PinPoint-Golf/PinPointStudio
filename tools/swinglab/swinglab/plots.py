# Contact sheets — how a model (or a human in a hurry) "watches" a swing:
# the recovered track overlaid on key video frames, plus theta/L/rate panels
# with the segmentation ladder. One PNG per swing at a stable path.

import math

import numpy as np

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

from . import RunResult, Swing

PHASE_TAGS = {0: "ADR", 1: "TKW", 2: "TOP", 3: "TRN", 4: "DWN", 5: "IMP",
              6: "REL", 7: "FIN", 8: "MBK", 9: "DLV", 10: "SPD", 11: "FLW"}


def _frame_at(cap, frame_ts, t_us):
    import cv2
    idx = int(np.clip(np.searchsorted(frame_ts, t_us), 0, len(frame_ts) - 1))
    cap.set(cv2.CAP_PROP_POS_FRAMES, idx)
    ok, img = cap.read()
    return (cv2.cvtColor(img, cv2.COLOR_BGR2RGB) if ok else None), idx


def contact_sheet(run_dir, swing_dir, out_png):
    import cv2
    run = RunResult(run_dir)
    swing = Swing(swing_dir)
    club = run.club
    samples = run.club_samples()
    an = run.analysis
    phases = sorted(an.get("phases", []), key=lambda p: p["t_us"])

    video = swing.face_on()
    cap = cv2.VideoCapture(str(swing.path / video["file"])) if video else None
    frame_ts = np.array(video["frames"]["t_us"], dtype=np.int64) if video else None
    Wpx = club.get("frameWidth") or video["encoded"]["width"]
    Hpx = club.get("frameHeight") or video["encoded"]["height"]

    t = np.array([s["t_us"] for s in samples], dtype=np.int64) if samples else np.array([])
    th = np.array([s["theta"] for s in samples]) if samples else np.array([])
    lens = np.array([s.get("lenPx", 0) for s in samples]) if samples else np.array([])

    fig = plt.figure(figsize=(16, 9), dpi=110)
    fig.suptitle(f"{swing.name} — valid={club.get('valid')} cov={club.get('coverage', 0):.2f} "
                 f"corr={club.get('imuVisionCorr', 0):.2f} ({run.meta.get('frames')})",
                 fontsize=11)

    # Top row: 5 overlay frames at the ladder's key instants.
    key_phases = [p for p in phases if p["phase"] in (0, 2, 5, 6, 7)][:5]
    if not key_phases and len(t):
        idxs = np.linspace(0, len(t) - 1, 5).astype(int)
        key_phases = [{"phase": -1, "t_us": int(t[i])} for i in idxs]
    for col, p in enumerate(key_phases):
        ax = fig.add_subplot(3, 5, col + 1)
        ax.set_axis_off()
        if cap is not None:
            img, _ = _frame_at(cap, frame_ts, p["t_us"])
            if img is not None:
                ax.imshow(img)
        if len(t):
            i = int(np.clip(np.searchsorted(t, p["t_us"]), 0, len(t) - 1))
            s = samples[i]
            g = (s["grip"][0] * Wpx, s["grip"][1] * Hpx)
            h = (s["head"][0] * Wpx, s["head"][1] * Hpx)
            ax.plot([g[0], h[0]], [g[1], h[1]], "-", color="lime", lw=2)
            ax.plot(*h, "o", color="red", ms=4)
        ax.set_title(PHASE_TAGS.get(p["phase"], "?"), fontsize=9)

    # Middle: theta(t) (+ truth when present) with the event ladder.
    ax = fig.add_subplot(3, 1, 2)
    if len(t):
        tt = (t - t[0]) / 1e6
        ax.plot(tt, np.degrees(np.unwrap(th)), lw=1.2, label="theta (track)")
        truth = swing.truth()
        if truth and "shaft" in truth:
            tr_t = np.array([f["t_us"] for f in truth["shaft"]], dtype=np.int64)
            tr_th = np.unwrap([f["theta"] for f in truth["shaft"]])
            m = (tr_t >= t[0]) & (tr_t <= t[-1])
            ax.plot((tr_t[m] - t[0]) / 1e6, np.degrees(tr_th[m]), "--", lw=1,
                    label="theta (truth)")
        for p in phases:
            x = (p["t_us"] - t[0]) / 1e6
            ax.axvline(x, color="k", alpha=0.25, lw=0.8)
            ax.text(x, ax.get_ylim()[1], PHASE_TAGS.get(p["phase"], "?"),
                    fontsize=7, rotation=90, va="top")
        ax.legend(fontsize=8)
    ax.set_ylabel("theta [deg]")

    # Bottom: visible length + per-sample flags.
    ax = fig.add_subplot(3, 1, 3)
    if len(t):
        tt = (t - t[0]) / 1e6
        ax.plot(tt, lens, lw=1, label="visible len [px]")
        flags = np.array([s.get("flags", 0) for s in samples])
        coast = (flags & 0x04) != 0
        if np.any(coast):
            ax.scatter(tt[coast], lens[coast], s=6, color="red", label="coasted")
        ax.legend(fontsize=8)
    ax.set_xlabel("t [s]")
    ax.set_ylabel("L [px]")

    fig.tight_layout(rect=(0, 0, 1, 0.96))
    fig.savefig(out_png)
    plt.close(fig)
    if cap is not None:
        cap.release()
    return out_png
