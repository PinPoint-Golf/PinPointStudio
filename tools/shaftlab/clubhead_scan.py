#!/usr/bin/env python3
"""Clubhead stage 2 — scan primitives + the H0 zeroth-order baseline tool.

ROLE IN THE PIPELINE (lab history: head_v0.py, H0 2026-07-03)
  This module owns the three low-level primitives every later stage builds on:
    scene_median()     the club-free scene reference (F12 as a channel)
    ray_edge_radius()  where the stage-1 ray exits the frame (off/censoring)
    run_end_scan()     the boxcar run-end along the ray — the ZEROTH-ORDER
                       length observation (superseded for output by
                       clubhead_measure.measure_head, but kept: it is the H0
                       baseline every later phase is scored against)
  Run as a script it produces the H0 zeroth-order head + a lengths CSV for
  length_model selffit. The production stage-2 tool is clubhead_annotate.py.

DECOUPLING (design §2 — the load-bearing requirement)
  Reads ONLY the stage-1 shaft-track contract v1 columns
  (frame, grip_x, grip_y, theta_out, kind, conf) + the clip video. No stage-1
  code imports anywhere in stage 2; the edge-pair math is an independent
  reimplementation (reference: shaft_annotate.py evidence_scan), so stage-1
  internals can change freely without stage 2 noticing.

C++ PORT NOTES (module -> ClubheadScan / scene utilities)
  * Angles are DEGREES at every module boundary; radians only inside trig.
    theta comes from length_model.load_track's `theta_u` (locally re-unwrapped
    — do NOT trust theta_out's winding across stage-1 re-inits).
  * All image sampling is bilinear with constant-0 border (cv2.remap
    INTER_LINEAR + BORDER_CONSTANT). Sub-pixel sampling is load-bearing:
    the edge-pair offsets are +/-2.5 px around the ray.
  * Determinism: no RNG, no wall clock anywhere in stage 2 — same inputs must
    give byte-identical CSVs (regression contract, same rule as stage 1).
  * The per-sample evidence values are float32; keep the accumulation order
    (max over offsets, then threshold) or outputs will drift.

Per frame (script mode):
  L_vis   — radial terminus of the sustained thin-line evidence run along
            theta_out (edge-pair width prior OR anti-ghost motion, boxcar run)
  r_edge  — radius at which the ray exits the frame
  L_pred  — per-swing self-fit length model evaluated at this frame's
            theta/phase (see length_model.self_fit_model for the q=0.5 story)
  kind_h  — off  : r_edge < off_factor * L_pred (expected head outside frame)
            meas : evidence run found (zeroth-order baseline semantics)
            pred : no run; head placed at L_pred along the ray
  head    — grip + L * dir(theta_out), L = L_vis (meas) or L_pred (pred);
            empty for off

Outputs (sibling files, never merged into the stage-1 CSV):
  <stem>_head.csv        frame, r_h, head_x, head_y, conf_h, kind_h, L_vis,
                         L_pred, r_edge, censored, s1_kind, phase
  <stem>_lengths.csv     frame, L_vis, censored   (length_model selffit input)
  <stem>_head_annotated.mp4   o meas / dashed-o pred / x off

Usage:
  clubhead_scan.py <clip.mp4> --track <stage1_track.csv> [--out-dir out]
      [--pred-model m4|m2|m0] [--off-factor 0.8] [--debug-frames 10,20]
"""
import argparse
import csv
import math
import os
import sys

import cv2
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from length_model import (M0Const, M2PlanePerPhase, M4Kernel,  # noqa: E402
                          load_track, phases_from_track, self_fit_model)

# scan constants (mirror the stage-1 validated values; independent code).
# Provenance matters for the port — none of these are arbitrary:
SHAFT_W_PX = 5.0        # projected shaft width at 720p-1080p (stage-1 value)
TAU_P = 90.0            # edge-pair normalisation (grey-level gradient scale)
TAU_M = 20.0            # motion normalisation (~4x sensor noise MAD)
TAU_CHG = 20.0          # change-vs-scene-median normalisation
RUN_LEN = 8             # boxcar window (px) for "locally sustained" evidence
RUN_FRAC = 0.75         # >=6 of 8 samples hit inside the boxcar
S_HIT = 0.3             # per-sample evidence threshold
PRESENCE_THR = 0.3      # presence (changed OR moving) threshold
R_MIN_FRAC = 0.06       # skip the hands: r_min = max(20, 0.06*H) px
BG_ALPHA = 0.02         # running-background EMA rate (stage-1 value)
EDGE_CENSOR_PX = 12.0   # run ending this close to the frame edge = censored
GAP_MERGE_PX = 25       # specular-dropout gaps to bridge within the club run
START_FRAC = 0.55       # club run must begin within this fraction of the ray
                        # ("attached to the hands" — stage-1 F1, applied radially)
# design §2.2/§4: the scan must tolerate stage-1 theta error (a 2 deg ray
# error is ~10 px lateral at r=300); channels take the max over lateral
# offsets across the ray normal. Band half-width is a CLI knob (--lat-max):
# clean/sharp footage needs the full band (synth: ray-miss decapitates the
# scan); cluttered real footage prefers a narrow band until the H1 shape
# channels can reject clutter.
def lat_offsets(lat_max):
    if lat_max <= 0:
        return (0.0,)
    return tuple(float(u) for u in range(-int(lat_max), int(lat_max) + 1, 4))


def scene_median(path, k=11, lo=None, hi=None):
    """Pixel-median of k frames evenly spanning [lo, hi] (default: whole clip)
    — a mostly club-free scene reference (stage 2 computes its own; F12-style).

    "Club-free by construction" holds only if no club pose dominates the
    sampled span. On the 2026-07-03 corpus the ADDRESS HOLD covers ~60% of the
    clip, so a whole-clip median CONTAINED the address club and the presence
    gate killed the club at address (chg==0 on the club itself — stage-1's
    §6.4 lesson: a veto's reference must not contain the target). Callers that
    know the phase split should pass lo=impact_frame so samples come from
    post-impact scenes where the club has left the address region."""
    cap = cv2.VideoCapture(path)
    n = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
    a = 0 if lo is None else max(0, int(lo))
    b = n - 1 if hi is None else min(n - 1, int(hi))
    stack = []
    if b - a + 1 > k:
        for i in range(k):
            cap.set(cv2.CAP_PROP_POS_FRAMES, int(a + i * (b - a) / (k - 1)))
            ok, f = cap.read()
            if ok:
                stack.append(cv2.cvtColor(f, cv2.COLOR_BGR2GRAY).astype(np.float32))
    cap.release()
    return np.median(np.stack(stack), axis=0) if len(stack) >= 5 else None


def ray_edge_radius(gx, gy, th_deg, W, H):
    """Radius at which the ray grip + r*dir(theta) leaves the frame."""
    c, s = math.cos(math.radians(th_deg)), math.sin(math.radians(th_deg))
    ts = []
    if abs(c) > 1e-9:
        for x in (0.0, W - 1.0):
            t = (x - gx) / c
            if t > 0:
                y = gy + t * s
                if -1 <= y <= H:
                    ts.append(t)
    if abs(s) > 1e-9:
        for y in (0.0, H - 1.0):
            t = (y - gy) / s
            if t > 0:
                x = gx + t * c
                if -1 <= x <= W:
                    ts.append(t)
    return min(ts) if ts else 0.0


def run_end_scan(gray, gxs, gys, motion, scene_med, gx, gy, th_deg, r_min, r_edge, offsets=(0.0,)):
    """Edge-pair + motion evidence along one ray, gated on presence
    (changed-vs-scene-median OR moving — kills permanent collinear structure
    like neon strips / mat edges, the F12 distractor class).
    Returns (L_vis, density, n_r): L_vis = radial distance (px) of the end of
    the last sustained evidence run (boxcar >= RUN_FRAC over RUN_LEN samples),
    or nan if no run.
    """
    nr = int(max(0, math.floor(r_edge - r_min)))
    if nr < RUN_LEN + 2:
        return float("nan"), 0.0, nr
    R = r_min + np.arange(nr, dtype=np.float32)
    th = math.radians(th_deg)
    c, s = math.cos(th), math.sin(th)
    nx, ny = -s, c
    Xc, Yc = gx + R * c, gy + R * s

    def samp(img, X, Y):
        return cv2.remap(img, np.float32(X).reshape(1, -1), np.float32(Y).reshape(1, -1),
                         cv2.INTER_LINEAR, borderMode=cv2.BORDER_CONSTANT,
                         borderValue=0)[0]

    # channels = max over lateral offsets across the ray normal (absorbs
    # stage-1 theta/grip error growing with radius — design §2.2/§4)
    ev = np.zeros(nr, np.float32)
    e_pres = np.zeros(nr, np.float32)
    dxw, dyw = nx * (SHAFT_W_PX * 0.5), ny * (SHAFT_W_PX * 0.5)
    for u in offsets:
        Xu, Yu = Xc + nx * u, Yc + ny * u
        dot_m = samp(gxs, Xu - dxw, Yu - dyw) * nx + samp(gys, Xu - dxw, Yu - dyw) * ny
        dot_p = samp(gxs, Xu + dxw, Yu + dyw) * nx + samp(gys, Xu + dxw, Yu + dyw) * ny
        e_pair = np.clip(np.sqrt(np.maximum(0.0, -dot_m * dot_p)) / TAU_P, 0.0, 1.0)
        e_mot = np.clip(samp(motion, Xu, Yu) / TAU_M, 0.0, 1.0)
        np.maximum(ev, np.maximum(e_pair, e_mot), out=ev)
        if scene_med is not None:
            e_chg = np.clip(np.abs(samp(gray, Xu, Yu) - samp(scene_med, Xu, Yu)) / TAU_CHG, 0.0, 1.0)
            np.maximum(e_pres, np.maximum(e_chg, e_mot), out=e_pres)

    hit = ev > S_HIT
    if scene_med is not None:
        hit &= e_pres > PRESENCE_THR
    hit = hit.astype(np.float32)
    runav = np.convolve(hit, np.ones(RUN_LEN) / RUN_LEN, mode="same")
    isrun = runav >= RUN_FRAC
    if not isrun.any():
        return float("nan"), 0.0, nr
    # contiguous run segments; merge small (dropout) gaps; the CLUB run is the
    # one connected to the grip (begins within START_FRAC of the ray) — a
    # later, radially-disjoint run is body/mat clutter, not the club
    d = np.diff(isrun.astype(np.int8))
    starts = list(np.flatnonzero(d == 1) + 1)
    ends = list(np.flatnonzero(d == -1) + 1)
    if isrun[0]:
        starts.insert(0, 0)
    if isrun[-1]:
        ends.append(len(isrun))
    merged = []
    for s, e in zip(starts, ends):
        if merged and (s - merged[-1][1]) < GAP_MERGE_PX:
            merged[-1][1] = e
        else:
            merged.append([s, e])
    for s, e in merged:
        if s <= START_FRAC * len(isrun):
            first, last = s, e - 1
            break
    else:
        return float("nan"), 0.0, nr
    density = float(hit[first:last + 1].mean()) if last > first else 0.0
    return float(R[last]), density, nr


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("clip")
    ap.add_argument("--track", required=True)
    ap.add_argument("--out-dir", default="out_head")
    ap.add_argument("--pred-model", choices=["m4", "m2", "m0"], default="m4")
    ap.add_argument("--off-factor", type=float, default=0.8)
    ap.add_argument("--lat-max", type=float, default=0.0,
                    help="lateral band half-width (px); 0 = center ray only. "
                         "H0 finding: real (blurred) footage scores best at 0 "
                         "(29 vs 67 px median — the band ORs in clutter); sharp "
                         "synth needs 12 (theta-noise ray-miss). H1's wedge "
                         "measurement with shape channels resolves this properly.")
    ap.add_argument("--debug-frames", default="")
    a = ap.parse_args()
    dbg = {int(x) for x in a.debug_frames.split(",") if x.strip()}

    track = load_track(a.track)
    fr2i = {f: i for i, f in enumerate(track["frame"])}

    cap = cv2.VideoCapture(a.clip)
    if not cap.isOpened():
        sys.exit(f"cannot open {a.clip}")
    W = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
    H = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
    fps = cap.get(cv2.CAP_PROP_FPS) or 30.0
    r_min = max(20.0, R_MIN_FRAC * H)

    # ---------------- pass 1: run-end scan per frame ----------------
    scene_med = scene_median(a.clip)
    Lvis, dens, redge, cens = {}, {}, {}, {}
    bg = None
    prev = None
    fi = 0
    while True:
        ok, frame = cap.read()
        if not ok:
            break
        gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY).astype(np.float32)
        if bg is None:
            bg = gray.copy()
        i = fr2i.get(fi)
        if i is not None and np.isfinite(track["theta_out"][i]):
            gx, gy = track["grip"][i]
            th = track["theta_u"][i]
            gxs = cv2.Sobel(gray, cv2.CV_32F, 1, 0, ksize=3)
            gys = cv2.Sobel(gray, cv2.CV_32F, 0, 1, ksize=3)
            d_prev = np.abs(gray - prev) if prev is not None else np.abs(gray - bg)
            motion = np.minimum(np.abs(gray - bg), d_prev)  # anti-ghost min
            re = ray_edge_radius(gx, gy, th, W, H)
            lv, dn, nr = run_end_scan(gray, gxs, gys, motion, scene_med, gx, gy, th, r_min, re,
                                      offsets=lat_offsets(a.lat_max))
            Lvis[fi], dens[fi], redge[fi] = lv, dn, re
            cens[fi] = bool(np.isfinite(lv) and (re - lv) < EDGE_CENSOR_PX)
            if fi in dbg:
                print(f"[dbg f{fi}] theta={th:.1f} grip=({gx:.0f},{gy:.0f}) "
                      f"r_edge={re:.0f} L_vis={lv:.1f} density={dn:.2f} censored={cens[fi]}")
        cv2.accumulateWeighted(gray, bg, BG_ALPHA)
        prev = gray
        fi += 1
    cap.release()
    n_frames = fi

    # ---------------- self-fit the length model ----------------
    ph_all, addr_th, top_i, impact_i = phases_from_track(track)
    fit_th, fit_ph, fit_L, fit_c = [], [], [], []
    for f, lv in Lvis.items():
        i = fr2i[f]
        if not np.isfinite(lv) or track["kind"][i] != "meas":
            continue
        fit_th.append(track["theta_u"][i])
        fit_ph.append(ph_all[i])
        fit_L.append(lv)
        fit_c.append(cens[f])
    fit_th, fit_ph = np.array(fit_th), np.array(fit_ph)
    fit_L, fit_c = np.array(fit_L), np.array(fit_c)
    ctor = {"m4": M4Kernel, "m2": M2PlanePerPhase, "m0": M0Const}[a.pred_model]
    model = None
    if (~fit_c).sum() >= 8:
        model = self_fit_model(ctor(), fit_th, fit_ph, fit_L, fit_c)
        print(f"[selffit] {model.name} on n={len(fit_L)} run-ends "
              f"({int(fit_c.sum())} censored): {model.params_str()}")
    else:
        print(f"[selffit] insufficient uncensored run-ends (n={(~fit_c).sum()}) — L_pred empty")

    # ---------------- assemble per-frame outputs ----------------
    os.makedirs(a.out_dir, exist_ok=True)
    stem = os.path.splitext(os.path.basename(a.clip))[0]
    rows = []
    for i, f in enumerate(track["frame"]):
        th = track["theta_u"][i] if np.isfinite(track["theta_out"][i]) else float("nan")
        gx, gy = track["grip"][i]
        lv = Lvis.get(int(f), float("nan"))
        re = redge.get(int(f), float("nan"))
        cn = cens.get(int(f), False)
        lp = float("nan")
        if model is not None and np.isfinite(th):
            lp = float(model.predict(np.array([th]), np.array([ph_all[i]]))[0])
        kind_h, r_h = "pred", float("nan")
        if np.isfinite(lp) and np.isfinite(re) and re < a.off_factor * lp:
            kind_h = "off"
        elif np.isfinite(lv):
            kind_h, r_h = "meas", lv
        elif np.isfinite(lp):
            kind_h, r_h = "pred", min(lp, re) if np.isfinite(re) else lp
        hx = hy = ""
        if kind_h != "off" and np.isfinite(r_h) and np.isfinite(th):
            hx = f"{gx + r_h * math.cos(math.radians(th)):.1f}"
            hy = f"{gy + r_h * math.sin(math.radians(th)):.1f}"
        conf_h = track["conf"][i] * (1.0 if kind_h == "meas" else 0.35)
        rows.append([int(f), f"{r_h:.1f}" if np.isfinite(r_h) else "", hx, hy,
                     f"{conf_h:.2f}", kind_h,
                     f"{lv:.1f}" if np.isfinite(lv) else "",
                     f"{lp:.1f}" if np.isfinite(lp) else "",
                     f"{re:.1f}" if np.isfinite(re) else "",
                     int(cn), track["kind"][i], ph_all[i]])

    head_csv = os.path.join(a.out_dir, f"{stem}_head.csv")
    with open(head_csv, "w", newline="") as fo:
        w = csv.writer(fo)
        w.writerow(["frame", "r_h", "head_x", "head_y", "conf_h", "kind_h",
                    "L_vis", "L_pred", "r_edge", "censored", "s1_kind", "phase"])
        w.writerows(rows)

    len_csv = os.path.join(a.out_dir, f"{stem}_lengths.csv")
    with open(len_csv, "w", newline="") as fo:
        w = csv.writer(fo)
        w.writerow(["frame", "L_vis", "censored"])
        for f in sorted(Lvis):
            if np.isfinite(Lvis[f]):
                w.writerow([f, f"{Lvis[f]:.1f}", int(cens[f])])

    # ---------------- pass 2: overlay ----------------
    by_frame = {r[0]: r for r in rows}
    cap = cv2.VideoCapture(a.clip)
    out_mp4 = os.path.join(a.out_dir, f"{stem}_head_annotated.mp4")
    vw = cv2.VideoWriter(out_mp4, cv2.VideoWriter_fourcc(*"mp4v"), fps, (W, H))
    fi = 0
    counts = {"meas": 0, "pred": 0, "off": 0}
    while True:
        ok, frame = cap.read()
        if not ok:
            break
        r = by_frame.get(fi)
        if r is not None:
            i = fr2i[fi]
            th, (gx, gy) = track["theta_u"][i], track["grip"][i]
            kind_h = r[5]
            counts[kind_h] += 1
            if np.isfinite(th):
                re = redge.get(fi, 0.0)
                ex = (int(gx + re * math.cos(math.radians(th))),
                      int(gy + re * math.sin(math.radians(th))))
                cv2.line(frame, (int(gx), int(gy)), ex, (90, 90, 90), 1)
                if kind_h == "meas" and r[2] != "":
                    c = (int(float(r[2])), int(float(r[3])))
                    cv2.circle(frame, c, 9, (0, 0, 255), 2)      # o measured (red)
                elif kind_h == "pred" and r[2] != "":
                    c = (int(float(r[2])), int(float(r[3])))
                    for adeg in range(0, 360, 45):               # dashed o predicted (cyan)
                        a0, a1 = adeg, adeg + 22
                        cv2.ellipse(frame, c, (9, 9), 0, a0, a1, (255, 220, 0), 2)
                elif kind_h == "off":
                    cv2.line(frame, (ex[0] - 8, ex[1] - 8), (ex[0] + 8, ex[1] + 8), (0, 165, 255), 2)
                    cv2.line(frame, (ex[0] - 8, ex[1] + 8), (ex[0] + 8, ex[1] - 8), (0, 165, 255), 2)
                cv2.circle(frame, (int(gx), int(gy)), 4, (255, 0, 0), -1)
            hud = f"f{fi} Lvis={r[6] or '-'} Lpred={r[7] or '-'} {kind_h.upper()}"
            cv2.putText(frame, hud, (10, 26), cv2.FONT_HERSHEY_SIMPLEX, 0.6,
                        (255, 255, 255), 1, cv2.LINE_AA)
        vw.write(frame)
        fi += 1
    vw.release()
    cap.release()

    n_run = sum(1 for v in Lvis.values() if np.isfinite(v))
    print(f"[clubhead_scan] frames={n_frames} run-end found={n_run} "
          f"kinds meas/pred/off = {counts['meas']}/{counts['pred']}/{counts['off']}")
    print(f"[out] {head_csv}\n[out] {len_csv}\n[out] {out_mp4}")


if __name__ == "__main__":
    main()
