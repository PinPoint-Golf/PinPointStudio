#!/usr/bin/env python3
"""Clubhead stage 2 — per-frame radial head MEASUREMENT (lab history:
head_v1.py, H1 2026-07-04). The temporal model / tiers live in
clubhead_annotate.py; this module owns measure_head().

WHAT THE MEASUREMENT IS (and why it is NOT the design-doc E_head peak)
  The design's peak-of-E_head formula (presence x shape channels) was built
  first and REJECTED by data: on real footage the area channels saturate
  (E_chg maxed over a lateral band discriminates nothing) and the E_end
  termination score fires on every mid-shaft specular break. The replacement
  came from a per-sample dump at 0008 f52: at address the club line is strong
  for ~100 px, then INVISIBLE for ~75 px (specular blowout — shaft luma ==
  blown-out mat luma), then the dark head is clearly present. So:

    measurement = GAP-TOLERANT ON-AXIS TERMINUS of the club line.
    per-sample support = (thin-line OR moving) AND (changed-vs-median OR
    moving) AND NOT permanent-line; candidate termini are the ends of
    locally-sustained support segments; the winner maximises
    tail-evidence-quality x length-model prior. Continuity is NEVER required
    ("40% visible in chunks still wins" — the stage-1 §4.3 lesson, radially).

HEAD-POINT CONVENTION (adjudicated on 0008 full-res zooms, all phases)
  The hand label sits at the visual END OF THE CLUB LINE (shaft-axis terminus
  at the hosel), NOT the head-mass centroid — the blade hangs off-axis below
  the line end. Output is therefore the ON-AXIS point
  grip + r_meas*dir(theta); there is deliberately NO lateral centroid step.

THE LENGTH-MODEL PRIOR (the CI role — see length_model.py)
  L_prior centres a Gaussian score on the expected projected length; sigma =
  max(0.30*L_prior, 40 px). The 40 px floor exists because a starved model
  (downswing kernel had L_pred = 95 px once) must never crush the search onto
  its own error. CRITICAL: the pass that FITS the model runs with
  L_prior=None — the model must never feed itself (design §2.4, no loops).

C++ PORT NOTES (module -> ClubheadMeasure)
  * epair() at three widths (5/12/24 px): thin shaft / bloomed over-exposed
    shaft (0009 top-of-swing halo defeats the 5 px pair) / head blade. Take
    the max — a ridge at ANY scale counts.
  * The permanence veto (pm_u) is the scene median's OWN edge-pair response:
    a line that exists in the median image is scenery (neon strip, mat edge),
    not the club. This is stage-1's F12 turned into a per-sample channel.
  * Per-offset hit with per-offset veto, then OR over offsets. Offsets default
    to (0,): blurred real footage holds the centre ray (stage-1 theta rides
    the blur) and a band ORs in clutter; sharp clips + noisy tracks (synth)
    need +/-12 px or the ray misses the shaft at large radii (2 deg error =
    ~10 px lateral at r=300).
  * Ambiguity-shaped confidence: a near-tied runner-up candidate halves
    certainty (sqrt of 1 - s2/s1) — honesty is the product requirement.
  * Angles in degrees at boundaries; float32 evidence; deterministic.

Decoupling: reads ONLY shaft-track contract v1 columns + video (design §2).

Usage (script mode = H1 output, superseded by clubhead_annotate.py):
  clubhead_measure.py <clip.mp4> --track <stage1_track.csv> [--out-dir out]
      [--debug-frames 10,20] [--off-factor 0.8] [--lat-max 0]
"""
import argparse
import csv
import math
import os
import sys

import cv2
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from clubhead_scan import (ray_edge_radius, scene_median,  # noqa: E402
                           BG_ALPHA, R_MIN_FRAC, TAU_CHG, TAU_M, TAU_P, SHAFT_W_PX,
                           EDGE_CENSOR_PX)
from length_model import (M4Kernel, load_track, phases_from_track,  # noqa: E402
                          self_fit_model)

# H1 measurement constants
ANNULUS_LO = 0.5      # search annulus in units of L_pred
ANNULUS_HI = 1.6      # generous until H2's KF tightens the CI (self-fit ~20-25% noise)
HIT_THR = 0.3
PERM_THR = 0.5        # scene-median edge-pair above this = permanent line (veto)
LOCAL_WIN = 8         # local sustainment boxcar (px)
LOCAL_FRAC = 0.5
SUPPORT_MIN = 0.30    # accumulated support fraction grip-run..terminus (gaps allowed)
START_FRAC = 0.55     # first support must begin within this fraction of the terminus


def measure_head(gray, prev_gray, bg, scene_med, gxs, gys, mxs, mys,
                 gx, gy, th_deg, r_lo, r_hi, L_prior=None, dbg=None,
                 offsets=(0.0,), r_floor=None):
    """Gap-tolerant on-axis terminus of the club line (H1, data-grounded).

    f52 lesson: the address shaft has a ~75 px specular BLOWOUT gap (shaft
    invisible against the blown mat) between the visible line and the dark
    head — continuity must not be required. Accumulate per-sample support:
      support = (thin-line OR moving) AND (changed-vs-median OR moving)
                AND NOT permanent-line (scene median's own edge-pair)
    r_meas = last locally-sustained supported sample in the annulus, gated on
    grip connection (first support within START_FRAC of it) and accumulated
    support fraction (SUPPORT_MIN — '40% visible in chunks still wins').
    Output is ON-AXIS per the adjudicated head-point convention.
    """
    nr = int(r_hi - r_lo)
    if nr < LOCAL_WIN + 4:
        return float("nan"), 0.0, None
    R = r_lo + np.arange(nr, dtype=np.float32)
    th = math.radians(th_deg)
    c, s = math.cos(th), math.sin(th)
    nx, ny = -s, c

    def samp(img, Xs, Ys):
        return cv2.remap(img, np.float32(Xs).reshape(1, -1), np.float32(Ys).reshape(1, -1),
                         cv2.INTER_LINEAR, borderMode=cv2.BORDER_CONSTANT, borderValue=0)[0]

    # per-offset hit, OR over lateral offsets (real footage: (0,) — the blurred
    # shaft holds the center ray; sharp/noisy tracks need a band, H0 finding)
    hit = np.zeros(nr, bool)
    ep = np.zeros(nr, np.float32)
    e_chg = np.zeros(nr, np.float32)
    e_mot = np.zeros(nr, np.float32)
    ep_med = np.zeros(nr, np.float32)
    for u in offsets:
        X, Y = gx + R * c + u * nx, gy + R * s + u * ny

        def epair(gxs_, gys_, w):
            dxw, dyw = nx * (w * 0.5), ny * (w * 0.5)
            dm = samp(gxs_, X - dxw, Y - dyw) * nx + samp(gys_, X - dxw, Y - dyw) * ny
            dp = samp(gxs_, X + dxw, Y + dyw) * nx + samp(gys_, X + dxw, Y + dyw) * ny
            return np.clip(np.sqrt(np.maximum(0.0, -dm * dp)) / TAU_P, 0, 1)

        # multi-width ridge: 5 px thin shaft, ~12 px bloomed/over-exposed shaft
        # (0009 top-of-swing halo killed the 5 px pair), ~24 px head blade
        ep_u = np.maximum.reduce([epair(gxs, gys, w) for w in (SHAFT_W_PX, 12.0, 24.0)])
        pm_u = np.maximum.reduce([epair(mxs, mys, w) for w in (SHAFT_W_PX, 12.0, 24.0)]) \
            if mxs is not None else np.zeros_like(ep_u)
        I = samp(gray, X, Y)
        ch_u = np.clip(np.abs(I - samp(scene_med, X, Y)) / TAU_CHG, 0, 1) \
            if scene_med is not None else np.ones_like(ep_u)
        mo_u = np.clip(np.minimum(np.abs(I - samp(bg, X, Y)),
                                  np.abs(I - samp(prev_gray, X, Y))) / TAU_M, 0, 1)
        hit_u = (np.maximum(ep_u, mo_u) > HIT_THR) \
            & (np.maximum(ch_u, mo_u) > HIT_THR) \
            & (pm_u < PERM_THR)
        hit |= hit_u
        np.maximum(ep, ep_u, out=ep)
        np.maximum(e_chg, ch_u, out=e_chg)
        np.maximum(e_mot, mo_u, out=e_mot)
        np.maximum(ep_med, pm_u, out=ep_med)
    local = np.convolve(hit.astype(np.float32),
                        np.ones(LOCAL_WIN) / LOCAL_WIN, "same") >= LOCAL_FRAC
    if dbg is not None:
        dbg.update(R=R, ep=ep, ep_med=ep_med, e_chg=e_chg, e_mot=e_mot,
                   hit=hit, local=local)
    if not local.any():
        return float("nan"), 0.0, dbg
    first = int(local.argmax())
    # candidate termini = ends of contiguous locally-sustained segments; the
    # gap-tolerant "always take the LAST" over-ran ~+50 px into moving-shadow
    # junk — instead score each candidate by evidence quality at its tail
    # (thin-line + change strength; shadows are soft/pair-less) and, in the
    # output pass, by the length-model prior (the CI role of the model). The
    # fitting pass runs with L_prior=None so the model never feeds itself.
    dl = np.diff(local.astype(np.int8))
    seg_s = list(np.flatnonzero(dl == 1) + 1)
    seg_e = list(np.flatnonzero(dl == -1) + 1)
    if local[0]:
        seg_s.insert(0, 0)
    if local[-1]:
        seg_e.append(len(local))
    q_ec = 0.5 * ep + 0.5 * np.minimum(np.maximum(e_chg, e_mot), 1.0)
    best, cands = None, []
    for s0, e0 in zip(seg_s, seg_e):
        e_i = e0 - 1
        if r_floor is not None and R[e_i] < r_floor:
            continue    # club-longer-than-arm plausibility (standing rule)
        if first > START_FRAC * e_i:
            continue
        support = float(hit[first:e_i + 1].mean())
        if support < SUPPORT_MIN:
            continue
        tail_q = float(q_ec[max(s0, e_i - 12):e_i + 1].mean())
        prior = 1.0
        if L_prior is not None and np.isfinite(L_prior):
            sig = max(0.30 * L_prior, 40.0)   # floor: a starved model must not crush the search
            prior = math.exp(-0.5 * ((R[e_i] - L_prior) / sig) ** 2)
        score = (0.6 * tail_q + 0.4 * min(support / 0.6, 1.0)) * prior
        cand = (score, e_i, support, tail_q)
        cands.append(cand)
        if best is None or score > best[0]:
            best = cand
    if best is None:
        return float("nan"), 0.0, dbg
    scores = sorted((c[0] for c in cands), reverse=True)
    _, last, support, tail_q = best
    conf = float(np.clip(0.5 * min(support / 0.6, 1.0) + 0.5 * tail_q, 0, 1))
    # ambiguity shaping: a near-tied runner-up candidate means the pick is a
    # coin flip — confidence must say so (honesty, pre-H2)
    if len(scores) > 1 and scores[0] > 1e-9:
        conf *= float(np.clip(1.0 - scores[1] / scores[0], 0.15, 1.0)) ** 0.5
    if dbg is not None:
        dbg.update(first=first, last=last, support=support, tail=tail_q)
    return float(R[last]), conf, dbg


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("clip")
    ap.add_argument("--track", required=True)
    ap.add_argument("--out-dir", default="out_head1")
    ap.add_argument("--off-factor", type=float, default=0.8)
    ap.add_argument("--lat-max", type=float, default=0.0,
                    help="lateral band half-width (px); 0 = center ray (real "
                         "blurred footage); 12 for sharp clips / noisy tracks (synth)")
    ap.add_argument("--debug-frames", default="")
    a = ap.parse_args()
    dbg_frames = {int(x) for x in a.debug_frames.split(",") if x.strip()}
    offs = (0.0,) if a.lat_max <= 0 else \
        tuple(float(u) for u in range(-int(a.lat_max), int(a.lat_max) + 1, 4))

    track = load_track(a.track)
    fr2i = {f: i for i, f in enumerate(track["frame"])}
    ph_all, addr_th, top_i, impact_i = phases_from_track(track)

    cap = cv2.VideoCapture(a.clip)
    if not cap.isOpened():
        sys.exit(f"cannot open {a.clip}")
    W = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
    H = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
    fps = cap.get(cv2.CAP_PROP_FPS) or 30.0
    r_min = max(20.0, R_MIN_FRAC * H)
    scene_med = scene_median(a.clip)
    mxs = cv2.Sobel(scene_med, cv2.CV_32F, 1, 0, ksize=3) if scene_med is not None else None
    mys = cv2.Sobel(scene_med, cv2.CV_32F, 0, 1, ksize=3) if scene_med is not None else None

    # ---------------- pass 1: gap-tolerant termini -> self-fit length model ----------------
    Lvis, cens, redge = {}, {}, {}
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
            lv, _, _ = measure_head(gray, prev, bg, scene_med, gxs, gys, mxs, mys,
                                    gx, gy, th, r_min, re, offsets=offs)
            Lvis[fi], redge[fi] = lv, re
            cens[fi] = bool(np.isfinite(lv) and (re - lv) < EDGE_CENSOR_PX)
        cv2.accumulateWeighted(gray, bg, BG_ALPHA)
        prev = gray
        fi += 1
    cap.release()

    # PRIOR fit pools back+down into one phase: the downswing sweeps the same
    # theta range the backswing covered densely, stage-1 meas coverage in the
    # fast phase is sparse (kernel starvation produced L_pred=95 px and the
    # prior then dragged measurements onto its own error), and both labeled
    # swings fit near-identical back/down planes. 'thru' stays separate.
    POOL = {"back": "back", "down": "back", "thru": "thru"}
    fit = [(track["theta_u"][fr2i[f]], POOL[ph_all[fr2i[f]]], lv, cens[f])
           for f, lv in Lvis.items()
           if np.isfinite(lv) and track["kind"][fr2i[f]] == "meas"]
    model = None
    if sum(1 for t in fit if not t[3]) >= 8:
        model = self_fit_model(M4Kernel(),
                               np.array([t[0] for t in fit]),
                               np.array([t[1] for t in fit]),
                               np.array([t[2] for t in fit]),
                               np.array([t[3] for t in fit]))
        print(f"[selffit] {model.name} on n={len(fit)} run-ends: {model.params_str()}")

    # ---------------- pass 2: radial measurement ----------------
    cap = cv2.VideoCapture(a.clip)
    rows = []
    bg, prev, fi = None, None, 0
    counts = {"meas": 0, "pred": 0, "off": 0}
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
            re = redge.get(fi, ray_edge_radius(gx, gy, th, W, H))
            lp = float("nan")
            if model is not None:
                lp = float(model.predict(np.array([th]), np.array([POOL[ph_all[i]]]))[0])
            kind_h, r_h, conf_h = "pred", float("nan"), 0.0
            if np.isfinite(lp) and re < a.off_factor * lp:
                kind_h = "off"
            else:
                r_lo = max(r_min, ANNULUS_LO * lp) if np.isfinite(lp) else r_min
                r_hi = min(re, ANNULUS_HI * lp) if np.isfinite(lp) else re
                gxs = cv2.Sobel(gray, cv2.CV_32F, 1, 0, ksize=3)
                gys = cv2.Sobel(gray, cv2.CV_32F, 0, 1, ksize=3)
                dbg = {} if fi in dbg_frames else None
                r_meas, conf, dbg = measure_head(gray, prev, bg, scene_med,
                                                 gxs, gys, mxs, mys, gx, gy, th,
                                                 r_lo, r_hi, L_prior=lp, dbg=dbg,
                                                 offsets=offs)
                if dbg is not None and "R" in dbg:
                    print(f"[dbg f{fi}] th={th:.1f} L_pred={lp:.0f} "
                          f"annulus=[{r_lo:.0f},{r_hi:.0f}] r_meas={r_meas:.1f} "
                          f"support={dbg.get('support', float('nan')):.2f} "
                          f"tail={dbg.get('tail', float('nan')):.2f}")
                    for r0 in range(0, len(dbg["R"]), max(1, len(dbg["R"]) // 14)):
                        print(f"    r={dbg['R'][r0]:5.0f} ep={dbg['ep'][r0]:.2f} "
                              f"chg={dbg['e_chg'][r0]:.2f} mot={dbg['e_mot'][r0]:.2f} "
                              f"perm={dbg['ep_med'][r0]:.2f} hit={int(dbg['hit'][r0])} "
                              f"loc={int(dbg['local'][r0])}")
                if np.isfinite(r_meas):
                    kind_h, r_h, conf_h = "meas", r_meas, conf
                elif np.isfinite(lp):
                    kind_h, r_h, conf_h = "pred", min(lp, re), 0.2
            hx = hy = ""
            if kind_h != "off" and np.isfinite(r_h):
                hx = f"{gx + r_h * math.cos(math.radians(th)):.1f}"
                hy = f"{gy + r_h * math.sin(math.radians(th)):.1f}"
            counts[kind_h] += 1
            rows.append([int(fi), f"{r_h:.1f}" if np.isfinite(r_h) else "", hx, hy,
                         f"{conf_h:.2f}", kind_h,
                         f"{Lvis.get(fi, float('nan')):.1f}" if np.isfinite(Lvis.get(fi, float('nan'))) else "",
                         f"{lp:.1f}" if np.isfinite(lp) else "",
                         f"{re:.1f}", int(cens.get(fi, False)),
                         track["kind"][i], ph_all[i]])
        cv2.accumulateWeighted(gray, bg, BG_ALPHA)
        prev = gray
        fi += 1
    cap.release()

    os.makedirs(a.out_dir, exist_ok=True)
    stem = os.path.splitext(os.path.basename(a.clip))[0]
    head_csv = os.path.join(a.out_dir, f"{stem}_head.csv")
    with open(head_csv, "w", newline="") as fo:
        w = csv.writer(fo)
        w.writerow(["frame", "r_h", "head_x", "head_y", "conf_h", "kind_h",
                    "L_vis", "L_pred", "r_edge", "censored", "s1_kind", "phase"])
        w.writerows(rows)

    # ---------------- pass 3: overlay ----------------
    by_frame = {r[0]: r for r in rows}
    cap = cv2.VideoCapture(a.clip)
    out_mp4 = os.path.join(a.out_dir, f"{stem}_head_annotated.mp4")
    vw = cv2.VideoWriter(out_mp4, cv2.VideoWriter_fourcc(*"mp4v"), fps, (W, H))
    fi = 0
    while True:
        ok, frame = cap.read()
        if not ok:
            break
        r = by_frame.get(fi)
        if r is not None:
            i = fr2i[fi]
            th, (gx, gy) = track["theta_u"][i], track["grip"][i]
            kind_h = r[5]
            re = float(r[8])
            ex = (int(gx + re * math.cos(math.radians(th))),
                  int(gy + re * math.sin(math.radians(th))))
            cv2.line(frame, (int(gx), int(gy)), ex, (90, 90, 90), 1)
            if kind_h == "meas" and r[2] != "":
                cv2.circle(frame, (int(float(r[2])), int(float(r[3]))), 9, (0, 0, 255), 2)
            elif kind_h == "pred" and r[2] != "":
                cpt = (int(float(r[2])), int(float(r[3])))
                for adeg in range(0, 360, 45):
                    cv2.ellipse(frame, cpt, (9, 9), 0, adeg, adeg + 22, (255, 220, 0), 2)
            elif kind_h == "off":
                cv2.line(frame, (ex[0] - 8, ex[1] - 8), (ex[0] + 8, ex[1] + 8), (0, 165, 255), 2)
                cv2.line(frame, (ex[0] - 8, ex[1] + 8), (ex[0] + 8, ex[1] - 8), (0, 165, 255), 2)
            cv2.circle(frame, (int(gx), int(gy)), 4, (255, 0, 0), -1)
            hud = f"f{fi} r={r[1] or '-'} Lpred={r[7] or '-'} conf={r[4]} {kind_h.upper()}"
            cv2.putText(frame, hud, (10, 26), cv2.FONT_HERSHEY_SIMPLEX, 0.6,
                        (255, 255, 255), 1, cv2.LINE_AA)
        vw.write(frame)
        fi += 1
    vw.release()
    cap.release()
    print(f"[clubhead_measure] kinds meas/pred/off = {counts['meas']}/{counts['pred']}/{counts['off']}")
    print(f"[out] {head_csv}\n[out] {out_mp4}")


if __name__ == "__main__":
    main()
