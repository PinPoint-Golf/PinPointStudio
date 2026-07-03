#!/usr/bin/env python3
"""Clubhead stage 2 — THE stage-2 tool: temporal model + output tiers on top
of clubhead_measure's per-frame terminus (lab history: head_v2.py, H2
2026-07-04; gates passed incl. honesty clauses).

PIPELINE (three video passes + one offline smoothing pass):
  pass 1  prior-FREE termini (measure_head, L_prior=None) + arm floor
          -> per-swing self-fit length model (length_model, censored q0.5)
  pass 2  prior-GUIDED measurements: annulus [0.5, 1.6]*L_pred, Gaussian
          length prior, arm floor, 180-deg flip check
  offline segmented 1-D KF + per-segment RTS -> tiers + confidence
  pass 3  overlay render

WHAT EACH PIECE IS FOR:
  * arm-length plausibility floor (user standing rule: the club is ALWAYS
    longer than the arm) — floor = ARM_FACTOR x projected shoulder->grip
    (skeleton.csv, max over both shoulders), applied ONLY in quasi-still
    frames before the top, where both club and arm lie near the image plane.
    NOT applied in wrap/finish phases: the PROJECTED club can legitimately be
    shorter than the projected arm under foreshortening. Fixes the
    saturated-mat address under-runs (the "impossibly short club at address").
  * 1-D Kalman filter on [r, rdot] — constant velocity, white-accel Q sized
    for foreshortening RECOVERY rates (~4e4 px/s^2: r swings ~95->305 px in
    ~0.1 s through impact), NOT for gentle motion (the stage-1 Q lesson).
    3-sigma innovation gate; speed-aware coast budget (12 frames slow, 4 when
    |rdot| > 800 px/s — blind drift at speed is the stage-1 F15 lesson);
    coasted tails are TRIMMED before smoothing (they carry no information).
  * segments split wherever stage-1's theta jumps > 20 deg/frame (a re-init
    moved the whole ray — radial continuity across it is meaningless) and on
    track loss. RTS runs PER SEGMENT, never across a boundary (stage-1's
    numerically-exploding-smoother lesson, verbatim).
  * output tiers (the honesty contract):
      meas — accepted measurement, conf >= 0.5, inside a confirmed run >= 4
             (single-frame dropout holes tolerated), past the re-init cap
             (0.35 until confirmed — F7/F9), AND the stage-1 frame is itself
             kind=meas. Stage-1 PRED frames never reach the meas tier: the
             ray is a kinematic guess and the emitted position cannot beat it
             (design §2.2) — their radial measurements still feed the filter.
      pred — smoothed/bridged r along the stage-1 ray; low-confidence
             detections are DISCARDED, never emitted as measurements.
      off  — r_edge < off_factor * L_hat: the expected head is outside the
             frame; NO position is emitted (never fabricate).
  * 180-deg flip check: stage-1 can lock a body line at high conf (s9v2 f190,
    follow-through). If the OPPOSITE ray decisively out-supports the forward
    one, the frame is demoted to pred. The ray is never corrected —
    decoupling: stage 2 refuses to bless, it does not fix.

C++ PORT NOTES (module -> ClubheadAnnotator / the §11.1 ClubheadDetector slot)
  * The KF/RTS is deliberately tiny (2-state, no wrapping — r is a plain
    scalar, unlike stage-1's circular theta). Keep the RTS gain computed from
    the STORED predicted covariance (hist tuples), same as stage 1.
  * dt must come from the TRUE fps (--fps-override; container fps is fake on
    prepped clips — see prep_swing.py). Getting dt wrong rescales Q silently.
  * frames enter the filter in track order; `accepted` marks which got a
    measurement update — tier logic depends on it, keep the bookkeeping.
  * Determinism: same video + track + skeleton -> byte-identical CSV.

Usage:
  clubhead_annotate.py <clip.mp4> --track <stage1_track.csv>
      [--skeleton skeleton.csv] [--out-dir out] [--fps-override F]
      [--lat-max 0] [--off-factor 0.8]
"""
import argparse
import csv
import math
import os
import sys

import cv2
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from clubhead_scan import ray_edge_radius, scene_median, BG_ALPHA, R_MIN_FRAC  # noqa: E402
from clubhead_measure import measure_head  # noqa: E402
from length_model import (M4Kernel, load_track, phases_from_track,  # noqa: E402
                          self_fit_model)

ARM_FACTOR = 1.05         # club > arm (shortest wedge ~0.9 m vs arm ~0.75 m)
STILL_SPEED_PX = 2.0      # grip speed below this = quasi-still (arm floor active)
SEG_THETA_JUMP = 20.0     # stage-1 theta jump (deg/frame) that splits segments
SIGMA_ACC = 4e4           # KF process accel noise (px/s^2) — foreshortening
                          # recovery through impact, not gentle motion
GATE_SIG = 3.0            # Mahalanobis innovation gate
COAST_SLOW, COAST_FAST = 12, 4   # coast budget frames (F15: speed-aware)
FAST_RDOT = 800.0         # px/s — above this the fast coast budget applies
CONF_MEAS_MIN = 0.35      # measurement conf floor to enter the filter
RUN_MIN = 4               # confirmed-run length for the meas tier (F9)
REINIT_CAP = 0.35         # conf cap until confirmation (F7)
SIG_MEAS_MAX = 10.0       # posterior sigma_r (px) below which an accepted,
                          # confirmed, s1-meas measurement is label-grade even
                          # if the instantaneous conf dipped (impact blur)
# The PRIOR fit pools back+down into one phase: the downswing sweeps the same
# theta range the backswing covered densely, stage-1 meas coverage in the fast
# phase is sparse (kernel starvation once produced L_pred=95 px and the prior
# dragged measurements onto its own error), and both labeled swings fit
# near-identical back/down planes. 'thru' stays separate (different geometry).
POOL = {"back": "back", "down": "back", "thru": "thru"}


def load_skeleton(path):
    """skeleton.csv rows: frame, then (x, y, conf) x 8 joints
    [Lsho, Rsho, Lhip, Rhip, Lknee, Rknee, Lank, Rank]. Returns
    {frame: [(x,y,conf) x 8]} or None."""
    if not path or not os.path.exists(path):
        return None
    out = {}
    with open(path) as f:
        for row in csv.reader(f):
            vals = [float(v) for v in row]
            out[int(vals[0])] = [(vals[1 + 3 * j], vals[2 + 3 * j], vals[3 + 3 * j])
                                 for j in range(8)]
    return out


def arm_proj(skel_row, gx, gy):
    """Max projected shoulder->grip distance (conf-gated) — the arm-length
    proxy for the plausibility floor."""
    best = None
    for j in (0, 1):
        x, y, cf = skel_row[j]
        if cf < 0.3:
            continue
        d = math.hypot(x - gx, y - gy)
        best = d if best is None else max(best, d)
    return best


class Kf1D:
    """[r, rdot] constant-velocity KF with white-accel Q."""

    def __init__(self, dt):
        self.dt = dt
        self.F = np.array([[1.0, dt], [0.0, 1.0]])
        q = SIGMA_ACC ** 2
        self.Q = q * np.array([[dt ** 4 / 4, dt ** 3 / 2], [dt ** 3 / 2, dt ** 2]])
        self.x = None
        self.P = None
        self.hist = []          # (x, P, x_pred, P_pred) per step

    def init(self, r):
        self.x = np.array([r, 0.0])
        self.P = np.diag([15.0 ** 2, 1500.0 ** 2])
        self.hist.append((self.x.copy(), self.P.copy(), self.x.copy(), self.P.copy()))

    def step(self, z, sig_r):
        xp = self.F @ self.x
        Pp = self.F @ self.P @ self.F.T + self.Q
        accepted = False
        if z is not None:
            S = Pp[0, 0] + sig_r ** 2
            innov = z - xp[0]
            if innov * innov <= (GATE_SIG ** 2) * S:
                K = Pp[:, 0] / S
                self.x = xp + K * innov
                self.P = Pp - np.outer(K, Pp[0, :])
                accepted = True
        if not accepted:
            self.x, self.P = xp, Pp
        self.hist.append((self.x.copy(), self.P.copy(), xp, Pp))
        return accepted

    def rts(self):
        """RTS over this segment's history; returns smoothed r array."""
        n = len(self.hist)
        xs = [h[0] for h in self.hist]
        Ps = [h[1] for h in self.hist]
        for k in range(n - 2, -1, -1):
            x1p = self.F @ xs[k]
            P1p = self.F @ Ps[k] @ self.F.T + self.Q
            C = Ps[k] @ self.F.T @ np.linalg.inv(P1p)
            xs[k] = xs[k] + C @ (xs[k + 1] - x1p)
            Ps[k] = Ps[k] + C @ (Ps[k + 1] - P1p) @ C.T
        return np.array([x[0] for x in xs]), np.array([P[0, 0] for P in Ps])


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("clip")
    ap.add_argument("--track", required=True)
    ap.add_argument("--skeleton", default=None,
                    help="skeleton.csv (default: sibling of the clip)")
    ap.add_argument("--out-dir", default="out_head2")
    ap.add_argument("--fps-override", type=float, default=0.0)
    ap.add_argument("--off-factor", type=float, default=0.8)
    ap.add_argument("--lat-max", type=float, default=0.0)
    a = ap.parse_args()
    offs = (0.0,) if a.lat_max <= 0 else \
        tuple(float(u) for u in range(-int(a.lat_max), int(a.lat_max) + 1, 4))
    skel_path = a.skeleton or os.path.join(os.path.dirname(os.path.abspath(a.clip)),
                                           "skeleton.csv")
    skel = load_skeleton(skel_path)

    track = load_track(a.track)
    fr2i = {f: i for i, f in enumerate(track["frame"])}
    ph_all, addr_th, top_i, impact_i = phases_from_track(track)

    cap = cv2.VideoCapture(a.clip)
    if not cap.isOpened():
        sys.exit(f"cannot open {a.clip}")
    W = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
    H = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
    fps_container = cap.get(cv2.CAP_PROP_FPS) or 30.0
    fps = a.fps_override or fps_container
    dt = 1.0 / fps
    r_min = max(20.0, R_MIN_FRAC * H)
    # Scene reference: whole-clip median. KNOWN LIMITATION, investigated and
    # deliberately left (2026-07-04, c1 hard-stratum session): on
    # address-hold-dominant clips the median CONTAINS the address club and the
    # presence gate suppresses it (§6.4: the reference must not contain the
    # target). A regime-gated fix (median from [impact, impact+1s], engaged
    # only when >55% of frames precede impact; old corpus byte-identical) was
    # built and REVERTED: on the exposure-limited dark-studio stratum it
    # un-suppressed bright-stub under-runs into the meas tier (label
    # honesty 1 -> 4 high-conf-bad per 100) — there, "where the evidence ends"
    # is not "where the club ends", and the accidental suppression was the
    # honest behaviour. Revisit WITH the backdrop/exposure capture stratum,
    # where the recovered address measurements are real (scene_median already
    # takes lo/hi for it; see its docstring for the full story).
    scene_med = scene_median(a.clip)
    mxs = cv2.Sobel(scene_med, cv2.CV_32F, 1, 0, ksize=3) if scene_med is not None else None
    mys = cv2.Sobel(scene_med, cv2.CV_32F, 0, 1, ksize=3) if scene_med is not None else None

    # quasi-still frames near address (grip speed) — where the arm floor applies
    grip = track["grip"]
    gspd = np.zeros(len(grip))
    gspd[1:] = np.hypot(np.diff(grip[:, 0]), np.diff(grip[:, 1]))
    gspd = np.convolve(gspd, np.ones(9) / 9.0, "same")
    still_addr = (gspd < STILL_SPEED_PX) & (np.arange(len(grip)) <= top_i)

    # ---------------- pass 1: prior-free termini (with arm floor) -> self-fit ----------------
    meas_r, meas_conf, redge, floors = {}, {}, {}, {}
    bg, prev, fi = None, None, 0
    while True:
        ok, frame = cap.read()
        if not ok:
            break
        gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY).astype(np.float32)
        if bg is None:
            bg = gray.copy()
        if prev is None:
            prev = gray
        i = fr2i.get(fi)
        if i is not None and np.isfinite(track["theta_out"][i]):
            gx, gy = track["grip"][i]
            th = track["theta_u"][i]
            gxs = cv2.Sobel(gray, cv2.CV_32F, 1, 0, ksize=3)
            gys = cv2.Sobel(gray, cv2.CV_32F, 0, 1, ksize=3)
            re = ray_edge_radius(gx, gy, th, W, H)
            floor = None
            if skel is not None and still_addr[i] and fi in skel:
                ap_ = arm_proj(skel[fi], gx, gy)
                if ap_:
                    floor = ARM_FACTOR * ap_
            lv, cf, _ = measure_head(gray, prev, bg, scene_med, gxs, gys, mxs, mys,
                                     gx, gy, th, r_min, re, offsets=offs,
                                     r_floor=floor)
            meas_r[fi], meas_conf[fi], redge[fi], floors[fi] = lv, cf, re, floor
        cv2.accumulateWeighted(gray, bg, BG_ALPHA)
        prev = gray
        fi += 1
    cap.release()

    fit = [(track["theta_u"][fr2i[f]], POOL[ph_all[fr2i[f]]], lv, False)
           for f, lv in meas_r.items()
           if np.isfinite(lv) and track["kind"][fr2i[f]] == "meas"]
    model = None
    if len(fit) >= 8:
        model = self_fit_model(M4Kernel(),
                               np.array([t[0] for t in fit]),
                               np.array([t[1] for t in fit]),
                               np.array([t[2] for t in fit]),
                               np.array([t[3] for t in fit]))
        print(f"[selffit] {model.name} on n={len(fit)} termini: {model.params_str()}")

    # ---------------- pass 2: prior-guided measurements ----------------
    cap = cv2.VideoCapture(a.clip)
    z, zconf, lpred = {}, {}, {}
    flip_suspect = set()
    bg, prev, fi = None, None, 0
    while True:
        ok, frame = cap.read()
        if not ok:
            break
        gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY).astype(np.float32)
        if bg is None:
            bg = gray.copy()
        if prev is None:
            prev = gray
        i = fr2i.get(fi)
        if i is not None and np.isfinite(track["theta_out"][i]):
            gx, gy = track["grip"][i]
            th = track["theta_u"][i]
            re = redge[fi]
            lp = float("nan")
            if model is not None:
                lp = float(model.predict(np.array([th]), np.array([POOL[ph_all[i]]]))[0])
                if floors.get(fi):
                    lp = max(lp, floors[fi])
            lpred[fi] = lp
            r_lo = max(r_min, 0.5 * lp) if np.isfinite(lp) else r_min
            r_hi = min(re, 1.6 * lp) if np.isfinite(lp) else re
            gxs = cv2.Sobel(gray, cv2.CV_32F, 1, 0, ksize=3)
            gys = cv2.Sobel(gray, cv2.CV_32F, 0, 1, ksize=3)
            r_meas, cf, _ = measure_head(gray, prev, bg, scene_med, gxs, gys, mxs, mys,
                                         gx, gy, th, r_lo, r_hi, L_prior=lp,
                                         offsets=offs, r_floor=floors.get(fi))
            # 180-deg flip check: stage-1 can flip mid-track at high conf
            # (s9v2 f80/f190, ray up the arms). If the OPPOSITE ray decisively
            # out-supports the forward one, this frame's ray is suspect —
            # never corrected (decoupling), only refused meas-tier blessing.
            if np.isfinite(r_meas):
                _, cf_opp, _ = measure_head(gray, prev, bg, scene_med, gxs, gys,
                                            mxs, mys, gx, gy, th + 180.0,
                                            r_lo, r_hi, L_prior=lp, offsets=offs)
                if cf_opp > max(1.3 * cf, 0.5):
                    flip_suspect.add(fi)
            z[fi], zconf[fi] = r_meas, cf
        cv2.accumulateWeighted(gray, bg, BG_ALPHA)
        prev = gray
        fi += 1
    cap.release()

    # ---------------- temporal model: segmented KF + per-segment RTS ----------------
    frames = [int(f) for f in track["frame"] if int(f) in z]
    th_u = {int(f): track["theta_u"][fr2i[int(f)]] for f in frames}
    seg_break = set()
    for k in range(1, len(frames)):
        if abs(th_u[frames[k]] - th_u[frames[k - 1]]) > SEG_THETA_JUMP:
            seg_break.add(frames[k])   # stage-1 re-init moved the whole ray

    segments = []        # (frame_list, kf)
    kf, seg_frames, coast = None, [], 0
    accepted = {}
    for f in frames:
        zi = z[f] if (np.isfinite(z[f]) and zconf[f] >= CONF_MEAS_MIN) else None
        if kf is not None and f in seg_break:
            segments.append((seg_frames, kf))
            kf, seg_frames, coast = None, [], 0
        if kf is None:
            if zi is not None:
                kf = Kf1D(dt)
                kf.init(zi)
                seg_frames = [f]
                accepted[f] = True
                coast = 0
            continue
        sig_r = 8.0 + (1.0 - zconf[f]) * 40.0
        ok_meas = kf.step(zi, sig_r)
        accepted[f] = bool(ok_meas and zi is not None)
        seg_frames.append(f)
        coast = 0 if accepted[f] else coast + 1
        budget = COAST_FAST if abs(kf.x[1]) > FAST_RDOT else COAST_SLOW
        if coast > budget:
            # trim the coasted tail — it carries no information
            trim = min(coast, len(seg_frames) - 1)
            for ftrim in seg_frames[len(seg_frames) - trim:]:
                accepted.pop(ftrim, None)
            kf.hist = kf.hist[:len(kf.hist) - trim]
            segments.append((seg_frames[:len(seg_frames) - trim], kf))
            kf, seg_frames, coast = None, [], 0
    if kf is not None and seg_frames:
        segments.append((seg_frames, kf))

    r_sm, sig_sm = {}, {}
    for seg_frames, seg_kf in segments:
        if len(seg_frames) < 2:
            continue
        rs, Ps = seg_kf.rts()
        for f, r, P in zip(seg_frames, rs, Ps):
            r_sm[f], sig_sm[f] = float(r), float(math.sqrt(max(P, 1.0)))

    # confirmed runs (F7/F9): accepted measurements in runs >= RUN_MIN
    conf_out = {}
    run = []
    def flush(run):
        for k, f in enumerate(run):
            c = zconf[f]
            if len(run) < RUN_MIN or k < 2:      # re-init cap until confirmed
                c = min(c, REINIT_CAP if len(run) < RUN_MIN else c)
            conf_out[f] = c
    miss = 0
    for f in frames:
        if accepted.get(f):
            run.append(f)
            miss = 0
        else:
            miss += 1
            if miss > 1:        # runs tolerate a single-frame dropout hole
                flush(run)
                run = []
                miss = 0
    flush(run)

    # ---------------- assemble tiers + outputs ----------------
    os.makedirs(a.out_dir, exist_ok=True)
    stem = os.path.splitext(os.path.basename(a.clip))[0]
    rows = []
    counts = {"meas": 0, "pred": 0, "off": 0}
    # bridge r for frames without a smoothed value: interp over frame index
    sm_f = sorted(r_sm)
    for i, fr in enumerate(track["frame"]):
        f = int(fr)
        if f not in z:
            continue
        th = th_u[f]
        gx, gy = track["grip"][fr2i[f]]
        re = redge[f]
        lhat = r_sm.get(f)
        if lhat is None and sm_f:
            lhat = float(np.interp(f, sm_f, [r_sm[q] for q in sm_f]))
        lp = lpred.get(f, float("nan"))
        if lhat is None and np.isfinite(lp):
            lhat = lp
        s1_pred = track["kind"][fr2i[f]] != "meas" or f in flip_suspect
        sig = sig_sm.get(f, float("nan"))
        kind_h, r_out, c_out = "pred", lhat, 0.2
        if lhat is not None and re < a.off_factor * lhat:
            kind_h, r_out = "off", None
        elif f in conf_out and accepted.get(f) and zconf[f] >= 0.5 \
                and conf_out[f] > REINIT_CAP and not s1_pred:
            kind_h, r_out, c_out = "meas", r_sm.get(f, z[f]), conf_out[f]
        elif f in conf_out and accepted.get(f) and not s1_pred \
                and zconf[f] >= CONF_MEAS_MIN and conf_out[f] > REINIT_CAP \
                and np.isfinite(sig) and sig <= SIG_MEAS_MAX:
            # posterior-sigma meas (design H2: conf from posterior σ_r): an
            # accepted measurement inside a confirmed, tightly-converged
            # segment is label-grade even when the single frame is blurry
            # (0008 impact f206/f207: smoothed r within 5–10 px of truth but
            # instantaneous conf 0.43/0.39). The zconf floor keeps genuinely
            # weak evidence from being blessed by run persistence alone.
            kind_h, r_out = "meas", r_sm.get(f, z[f])
            c_out = min(0.9, 0.5 * zconf[f] + 0.5 * (1.0 - sig / SIG_MEAS_MAX))
        elif f in conf_out and accepted.get(f):
            # stage-1 pred frames: the ray itself is a kinematic guess, so the
            # emitted position can't beat it — radial measurement still feeds
            # the filter, but the OUTPUT stays pred-tier (design §2.2)
            kind_h, r_out = "pred", r_sm.get(f, z[f])
            c_out = min(conf_out[f], REINIT_CAP)
        if r_out is not None and re > 0:
            r_out = min(r_out, re)
        hx = hy = ""
        if kind_h != "off" and r_out is not None:
            hx = f"{gx + r_out * math.cos(math.radians(th)):.1f}"
            hy = f"{gy + r_out * math.sin(math.radians(th)):.1f}"
        counts[kind_h] += 1
        rows.append([f, f"{r_out:.1f}" if r_out is not None else "", hx, hy,
                     f"{c_out:.2f}", kind_h,
                     f"{z[f]:.1f}" if np.isfinite(z[f]) else "",
                     f"{lp:.1f}" if np.isfinite(lp) else "",
                     f"{re:.1f}", int(bool(floors.get(f))),
                     track["kind"][fr2i[f]], ph_all[fr2i[f]],
                     f"{sig:.1f}" if np.isfinite(sig) else ""])

    head_csv = os.path.join(a.out_dir, f"{stem}_head.csv")
    with open(head_csv, "w", newline="") as fo:
        w = csv.writer(fo)
        w.writerow(["frame", "r_h", "head_x", "head_y", "conf_h", "kind_h",
                    "L_vis", "L_pred", "r_edge", "arm_floored", "s1_kind", "phase",
                    "sigma_r"])
        w.writerows(rows)

    # ---------------- overlay ----------------
    by_frame = {r[0]: r for r in rows}
    cap = cv2.VideoCapture(a.clip)
    out_mp4 = os.path.join(a.out_dir, f"{stem}_head_annotated.mp4")
    vw = cv2.VideoWriter(out_mp4, cv2.VideoWriter_fourcc(*"mp4v"), fps_container, (W, H))
    fi = 0
    while True:
        ok, frame = cap.read()
        if not ok:
            break
        r = by_frame.get(fi)
        if r is not None:
            i = fr2i[fi]
            th = math.radians(track["theta_u"][i])
            gx, gy = track["grip"][i]
            kind_h = r[5]
            s1col = (0, 0, 255) if r[10] == "meas" else (255, 220, 0)
            if r[1] != "":
                ex = (int(gx + float(r[1]) * math.cos(th)), int(gy + float(r[1]) * math.sin(th)))
                cv2.line(frame, (int(gx), int(gy)), ex, s1col,
                         2 if r[10] == "meas" else 1, cv2.LINE_AA)
            cv2.circle(frame, (int(gx), int(gy)), 4, (255, 0, 0), -1)
            if kind_h == "meas" and r[2] != "":
                cv2.circle(frame, (int(float(r[2])), int(float(r[3]))), 10, (0, 0, 255), 2, cv2.LINE_AA)
            elif kind_h == "pred" and r[2] != "":
                cpt = (int(float(r[2])), int(float(r[3])))
                for adeg in range(0, 360, 45):
                    cv2.ellipse(frame, cpt, (10, 10), 0, adeg, adeg + 22, (255, 220, 0), 2, cv2.LINE_AA)
            elif kind_h == "off":
                re_ = float(r[8])
                exo = (int(gx + re_ * math.cos(th)), int(gy + re_ * math.sin(th)))
                cv2.line(frame, (exo[0] - 9, exo[1] - 9), (exo[0] + 9, exo[1] + 9), (0, 165, 255), 2)
                cv2.line(frame, (exo[0] - 9, exo[1] + 9), (exo[0] + 9, exo[1] - 9), (0, 165, 255), 2)
            hud = f"f{fi} r={r[1] or '-'} conf={r[4]} {kind_h.upper()}" + (" ARM" if r[9] else "")
            cv2.putText(frame, hud, (10, 26), cv2.FONT_HERSHEY_SIMPLEX, 0.6,
                        (255, 255, 255), 1, cv2.LINE_AA)
        vw.write(frame)
        fi += 1
    vw.release()
    cap.release()
    nseg = len([s for s in segments if len(s[0]) >= 2])
    print(f"[clubhead_annotate] kinds meas/pred/off = {counts['meas']}/{counts['pred']}/{counts['off']}"
          f"  segments={nseg}  arm-floored frames={sum(1 for v in floors.values() if v)}")
    print(f"[out] {head_csv}\n[out] {out_mp4}")


if __name__ == "__main__":
    main()
