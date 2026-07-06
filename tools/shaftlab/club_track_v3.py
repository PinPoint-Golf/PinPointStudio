#!/usr/bin/env python3
"""club_track_v3.py — v3.0 constrained global shaft estimator.

Design: docs/design/club_tracking_v3_design.md; plan:
docs/implementation/shaft_detection_v3_impl.md. FULL PEDAGOGICAL EXPLANATION
(the reference "bible", written for a reader new to golf / vision / the DP /
the statistics; the durable record for the C++ port):
docs/design/club_track_v3_exemplar_explained.md.

Physics-first. The four fundamentals are promoted to first-class constraints
and the shaft direction theta(t) is estimated GLOBALLY (Viterbi DP over a theta
grid across the whole clip) rather than per frame:

  C1  butt-termination   band locks must place the butt within r0 in (0,260mm]
                         behind the grip; a ridge that continues (off-arm) behind
                         the grip is a scene line -> penalised.
  C2  body-overlap veto  per-frame body polygon from skeleton.csv (8 joints +
                         margin, temporally smoothed). A candidate whose shaft
                         ray lies majority-inside the body is vetoed in
                         takeaway->thru; admitted-not-sufficient at addr/impact/
                         finish.
  C3  phase-signed rot   phase model from the HANDS ALONE (anchors.csv): the
                         swing reverses once, at the top. theta increases through
                         the backswing and decreases (monotone, wrapping mod 360)
                         through downswing->impact->thru->finish. A 180 deg flip
                         becomes structurally impossible; the impact gap is
                         bridged as a monotone bounded-rate sweep.
  C4  reachable cone     psi = theta - phi (phi = lead-arm dir, anchors.csv).
                         EMPIRICAL (s01): raw phi from pose is noisy at the top /
                         in the blur zone (frame-to-frame jumps to ~87 deg), so
                         the cone is WIDE (a robustly-smoothed phi + generous
                         bounds) and is disabled at address/finish where psi is
                         unbounded. It removes the reverse half-circle and the
                         into-forearm direction; C3 + DP smoothness do the fine
                         work.

Evidence engines are UNCHANGED and imported from stripe_fusion (E1 band matcher,
E2 polarity-aware ridge). Deterministic: no RNG; the DP is exact.

  club_track_v3.py <clip> --anchors a.csv --clubs clubs.json --club "7 IRON"
      [--skeleton skeleton.csv] [--clipmeta m.json] [--fps-override F]
      [--impact-frame N | --impact-us U] [--out-dir out] [--overlay]
      [--truth-out] [--phases-out]
"""
import argparse, csv, json, math, os, sys
import numpy as np, cv2
from scipy.ndimage import median_filter, gaussian_filter1d

from stripe_fusion import (ridge_sweep, frame_band_match, BG_HI, ARM_VETO_DEG)

# ---- grid / DP ---------------------------------------------------------
GRID = 1.0                       # theta grid step (deg)
NS = int(round(360.0 / GRID))
# per-phase max |dtheta| per frame (deg/frame). s01 peaks ~16 deg/f at impact
# (149 fps); generous ceilings so the true sweep is never clipped.
WMAX = {"addr": 3.0, "backswing": 9.0, "top": 5.0, "downswing": 16.0,
        "impact": 24.0, "thru": 16.0, "finish": 11.0}
# expected sign of dtheta relative to swing chirality (0 = either direction)
PHASE_SIGN = {"addr": 0, "backswing": +1, "top": 0, "downswing": -1,
              "impact": -1, "thru": -1, "finish": -1}
MIDSWING = {"backswing", "top", "downswing", "impact", "thru"}   # C2 vetoes here

# ---- cost weights (cost units; DP minimises) ---------------------------
W_E2 = 10.0          # emission: 0 (best evidence) .. W_E2 (none)
W_BAND = 8.0         # extra emission reward at a band-locked theta
W_ARM = 16.0         # C4 into-forearm veto
W_C1 = 10.0          # C1 off-arm reverse-support (scene line through hands)
W_C2 = 13.0          # C2 body-overlap veto
W_CONE = 4.0         # C4 wide-cone soft penalty
K_SMOOTH = 0.03      # transition theta-smoothness (per deg^2)
C_SIGN = 40.0        # wrong-direction transition penalty (soft, not inf)
CONE_HALF = 150.0    # C4 wide half-cone about phi (deg), chirality-centred
C1_TOL = 0.45        # reverse-ridge normalised strength above which C1 bites
RAY_EV_MIN = 0.45    # normalised E2 evidence needed to call a frame 'ray'
BAND_TOL = 6.0       # |theta* - theta_band| (deg) to claim the band tier
STILL_SPEED = 0.8    # grip px/frame below which (sustained) a frame is 'static'
STILL_MIN = 25       # min static-run length (frames)
BAND_NEAR = 5        # a static ray is admissible within this many frames of a band lock

# ---- reused radii for body-mask ray sampling ---------------------------
BODY_MARGIN = 34.0   # px dilation of the body polygon
BODY_R = np.arange(45.0, 470.0, 14.0)   # ray radii for the inside-fraction test


def circ_wrap(a):
    return (a + 180.0) % 360.0 - 180.0


def smooth_phi(phi_deg):
    """Robust unit-vector smoothing of the lead-arm direction (handles wrap)."""
    phr = np.radians(phi_deg)
    cx = gaussian_filter1d(median_filter(np.cos(phr), 9), 3)
    cy = gaussian_filter1d(median_filter(np.sin(phr), 9), 3)
    return np.degrees(np.arctan2(cy, cx))


def segment_phases(gx, gy, nf, fps, impact_frame=None):
    """Hands-only phase model (C3). Returns (phase[nf] str, chir int, landmarks)."""
    spd = np.zeros(nf); spd[1:] = np.hypot(np.diff(gx), np.diff(gy))
    spd_s = gaussian_filter1d(median_filter(spd, 5), 2)
    SW_SPD = 8.0                                   # swing >> waggle (~4-6 px/f)
    mo = spd_s > SW_SPD
    runs, f = [], 0
    while f < nf:
        if mo[f]:
            g = f
            while g + 1 < nf and mo[g + 1]:
                g += 1
            if g - f >= 6:
                runs.append((f, g))
            f = g + 1
        else:
            f += 1
    if len(runs) < 1:
        # no swing detected: whole clip 'addr' (conservative fallback)
        return ["addr"] * nf, 0, 0, nf // 2, nf - 1, spd_s
    runs.sort(key=lambda r: -(r[1] - r[0]))
    big = sorted(runs[:2], key=lambda r: r[0])
    bs0 = big[0][0]                                # takeaway start
    if len(big) >= 2:
        gap_lo, gap_hi = big[0][1], big[1][0]
        top = gap_lo + int(np.argmin(spd_s[gap_lo:gap_hi + 1])) if gap_hi > gap_lo else big[0][1]
        ds_end = big[1][1]
    else:
        top = bs0 + int(np.argmax(gy[bs0:big[0][1] + 1] * -1))   # grip apex proxy
        ds_end = big[0][1]
    addr_gy = float(np.median(gy[:max(bs0, 1)]))
    if impact_frame is None:
        # hands-only: first post-top frame where the grip is back to address
        # height (club has returned to the ball line) inside the downswing run.
        cand = [f for f in range(top, ds_end + 1) if gy[f] >= addr_gy - 20.0]
        impact_frame = cand[0] if cand else (top + ds_end) // 2
    impact_frame = int(np.clip(impact_frame, top + 1, nf - 1))
    fin0 = min(ds_end, nf - 1)                      # finish begins as motion decays
    IMP = 12                                         # impact zone half-width (frames)
    phase = []
    for f in range(nf):
        if f < bs0:
            phase.append("addr")
        elif f < top - 2:
            phase.append("backswing")
        elif f <= top + 2:
            phase.append("top")
        elif abs(f - impact_frame) <= IMP:
            phase.append("impact")
        elif f < impact_frame:
            phase.append("downswing")
        elif f <= fin0:
            phase.append("thru")
        else:
            phase.append("finish")
    # chirality from the unwrapped smoothed phi over the backswing
    return phase, bs0, top, impact_frame, fin0, spd_s


def body_masks(skel_path, nf, W, H):
    """Per-frame filled body polygon (uint8 HxW) from skeleton.csv 8 joints
    (shoulders, hips, knees, ankles), temporally smoothed + dilated."""
    if not skel_path or not os.path.exists(skel_path):
        return None
    J = {}
    for row in csv.reader(open(skel_path)):
        f = int(row[0]); vals = [float(x) for x in row[1:]]
        pts = [(vals[3 * i], vals[3 * i + 1], vals[3 * i + 2]) for i in range(len(vals) // 3)]
        J[f] = pts
    nj = len(next(iter(J.values())))
    xs = np.zeros((nf, nj)); ys = np.zeros((nf, nj))
    for f in range(nf):
        p = J.get(f) or J.get(min(J, key=lambda k: abs(k - f)))
        for i in range(nj):
            xs[f, i], ys[f, i] = p[i][0], p[i][1]
    for i in range(nj):                              # temporal smoothing per joint
        xs[:, i] = gaussian_filter1d(median_filter(xs[:, i], 5), 2)
        ys[:, i] = gaussian_filter1d(median_filter(ys[:, i], 5), 2)
    masks = []
    for f in range(nf):
        pts = np.stack([xs[f], ys[f]], 1).astype(np.float32)
        hull = cv2.convexHull(pts).astype(np.int32)
        m = np.zeros((H, W), np.uint8)
        cv2.fillConvexPoly(m, hull, 255)
        k = int(BODY_MARGIN)
        m = cv2.dilate(m, cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (2 * k + 1, 2 * k + 1)))
        masks.append(m)
    return masks


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("video")
    ap.add_argument("--anchors", required=True)
    ap.add_argument("--clubs", required=True)
    ap.add_argument("--club", required=True)
    ap.add_argument("--skeleton", default=None, help="skeleton.csv (default: next to clip)")
    ap.add_argument("--clipmeta", default=None)
    ap.add_argument("--fps-override", type=float, default=None)
    ap.add_argument("--impact-frame", type=int, default=None)
    ap.add_argument("--impact-us", type=float, default=None)
    ap.add_argument("--out-dir", default=".")
    ap.add_argument("--overlay", action="store_true")
    ap.add_argument("--truth-out", action="store_true")
    ap.add_argument("--phases-out", action="store_true")
    args = ap.parse_args()

    rec = json.load(open(args.clubs))[args.club]
    bands = [float(r) for r in rec["bandCentersMm"]]
    r_len = float(rec["lengthMm"])

    anchors, phi_raw = {}, {}
    for row in csv.reader(open(args.anchors)):
        f = int(row[0]); anchors[f] = (float(row[1]), float(row[2]))
        phi_raw[f] = float(row[3]) if (len(row) >= 5 and int(row[4])) else np.nan

    cap = cv2.VideoCapture(args.video)
    if not cap.isOpened():
        sys.exit(f"cannot open {args.video}")
    W = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH)); H = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
    fps = args.fps_override or cap.get(cv2.CAP_PROP_FPS) or 30.0
    frames = []                                      # uint8 gray (memory: 16 GB box)
    while True:
        ok, fr = cap.read()
        if not ok:
            break
        frames.append(cv2.cvtColor(fr, cv2.COLOR_BGR2GRAY))
    cap.release()
    nf = len(frames)
    rmax = 0.62 * H
    scene_med = np.median(np.stack(frames[::8]).astype(np.float32), axis=0)

    gx = np.array([anchors.get(f, (np.nan, np.nan))[0] for f in range(nf)])
    gy = np.array([anchors.get(f, (np.nan, np.nan))[1] for f in range(nf)])
    phv = np.array([phi_raw.get(f, np.nan) for f in range(nf)])
    # fill phi gaps then smooth
    idx = np.arange(nf); good = ~np.isnan(phv)
    if good.sum():
        phv = np.interp(idx, idx[good], phv[good])
    phi_s = smooth_phi(phv)

    impf = args.impact_frame
    if impf is None and args.impact_us is not None and args.clipmeta:
        tt = np.array(json.load(open(args.clipmeta))["t_us"], float)
        impf = int(np.argmin(np.abs(tt - args.impact_us)))
    phase, bs0, top, impf, fin0, spd_s = segment_phases(gx, gy, nf, fps, impf)
    phu = np.unwrap(np.radians(phi_s))
    chir = 1 if (phu[min(top, nf - 1)] - phu[bs0]) >= 0 else -1

    skel = args.skeleton or os.path.join(os.path.dirname(args.video), "skeleton.csv")
    masks = body_masks(skel, nf, W, H)

    # static-run mask: a sustained low grip-speed span. A ray lock inside a
    # static run cannot be motion-verified (v2-adjudicated: static-period
    # counterfeits pass every appearance gate), so such rays are demoted to
    # 'pred' unless a real band lock sits within BAND_NEAR frames.
    still = spd_s < STILL_SPEED
    static = np.zeros(nf, bool)
    f = 0
    while f < nf:
        if still[f]:
            g = f
            while g + 1 < nf and still[g + 1]:
                g += 1
            if g - f + 1 >= STILL_MIN:
                static[f:g + 1] = True
            f = g + 1
        else:
            f += 1

    TR = np.radians(np.arange(0.0, 360.0, GRID))     # grid theta (rad)
    TDEG = np.degrees(TR)

    # ---- per-frame evidence + constraint penalties --------------------
    emis = np.full((nf, NS), W_E2)                   # emission cost
    EV = np.zeros((nf, NS))                          # normalised E2 evidence (pre-gate)
    band = [None] * nf                               # band-lock dict per frame
    inside = np.zeros((nf, NS))                      # C2 inside-fraction
    for f in range(nf):
        if f not in anchors or np.isnan(gx[f]):
            continue
        g = (gx[f], gy[f]); gray = frames[f].astype(np.float32)
        s_raw, _, sup_raw = ridge_sweep(gray, g[0], g[1], TR)
        diff = np.abs(gray - scene_med)
        s_dif, _, sup_dif = ridge_sweep(diff, g[0], g[1], TR, bright_only=True)

        def norm(s):
            lo = np.percentile(s, 50); hi = np.percentile(s, 97)
            return np.clip((s - lo) / (hi - lo + 1e-6), 0.0, 1.0)
        ev = np.maximum(norm(s_raw), norm(s_dif))    # E2 evidence in [0,1]
        EV[f] = ev

        # band lock (E1) for discrete-bloom frames (needs uint8)
        bm = frame_band_match(frames[f], g[0], g[1], rmax, bands)
        if bm is not None and 0.0 < bm["r0"] <= 260.0:      # C1 (full form)
            band[f] = bm
            bi = int(round(bm["theta"] / GRID)) % NS
            ev[bi] = max(ev[bi], 1.0)

        em = W_E2 * (1.0 - ev)

        # C4 arm-veto: shaft never points INTO the lead forearm (phi+180)
        arm = (phi_s[f] + 180.0) % 360.0
        d_arm = np.abs(circ_wrap(TDEG - arm))
        em += np.where(d_arm < ARM_VETO_DEG, W_ARM, 0.0)

        # C4 wide reachable cone (chirality-centred), active off addr/finish
        if phase[f] not in ("addr", "finish", "top"):
            cen = phi_s[f] + chir * (CONE_HALF - 40.0)      # bias toward shaft side
            d_cone = np.abs(circ_wrap(TDEG - cen))
            em += np.where(d_cone > CONE_HALF, W_CONE, 0.0)

        # C1 (weak form for rays): reverse ridge OFF the forearm = scene line
        rev = norm(s_raw)[(np.arange(NS) + NS // 2) % NS]   # ridge along theta+180
        rev_arm = np.abs(circ_wrap(TDEG - ((phi_s[f]) % 360.0)))  # reverse ~ phi
        em += np.where((rev > C1_TOL) & (rev_arm > ARM_VETO_DEG), W_C1, 0.0)

        # C2 body-overlap veto (mid-swing only)
        if masks is not None and phase[f] in MIDSWING:
            ux, uy = np.cos(TR), np.sin(TR)
            X = np.clip((g[0] + np.outer(ux, BODY_R)).astype(np.int32), 0, W - 1)
            Y = np.clip((g[1] + np.outer(uy, BODY_R)).astype(np.int32), 0, H - 1)
            frac = (masks[f][Y, X] > 0).mean(axis=1)
            inside[f] = frac
            em += np.where(frac > 0.5, W_C2, 0.0)

        # a ratio-verified, butt-terminated band lock is the strongest anchor:
        # a negative-emission well that dominates the gates and forces the DP
        # onto the true (wrapping) global path (else it takes the smoother
        # wrong branch through the evidence-free impact gap -- s01-adjudicated).
        if band[f] is not None:
            bi = int(round(band[f]["theta"] / GRID)) % NS
            em[bi] = -W_BAND

        emis[f] = em

    # ---- global Viterbi DP over the theta grid ------------------------
    INF = 1e9
    cost = emis[0].copy()
    back = np.zeros((nf, NS), np.int32)
    for f in range(1, nf):
        ph = phase[f]
        wmax = int(math.ceil(WMAX[ph] / GRID))
        sgn = PHASE_SIGN[ph]
        if sgn > 0:
            shifts = range(0, wmax + 1)
        elif sgn < 0:
            shifts = range(-wmax, 1)
        else:
            shifts = range(-wmax, wmax + 1)
        best = np.full(NS, INF); barg = np.zeros(NS, np.int32)
        for d in shifts:
            t = K_SMOOTH * (d * GRID) ** 2
            cand = np.roll(cost, d) + t
            src = (np.arange(NS) - d) % NS
            upd = cand < best
            best[upd] = cand[upd]; barg[upd] = src[upd]
        cost = best + emis[f]
        back[f] = barg
    thstar = np.zeros(nf, np.int32)
    thstar[-1] = int(np.argmin(cost))
    for f in range(nf - 1, 0, -1):
        thstar[f - 1] = back[f, thstar[f]]
    theta = TDEG[thstar]                              # deg per frame

    # ---- tiering + rows ----------------------------------------------
    rows = []
    for f in range(nf):
        if f not in anchors or np.isnan(gx[f]):
            continue
        thi = int(thstar[f]); th = theta[f]
        g = (gx[f], gy[f]); bm = band[f]
        tier = "pred"; s = r0 = None; conf = 0.30; hx = hy = ""
        if bm is not None and abs(circ_wrap(th - bm["theta"])) <= BAND_TOL:
            tier = "band"; s = bm["s"]; r0 = bm["r0"]
            ux, uy = math.cos(math.radians(th)), math.sin(math.radians(th))
            bx, by = g[0] - s * r0 * ux, g[1] - s * r0 * uy
            hx, hy = bx + s * r_len * ux, by + s * r_len * uy
            conf = min(0.9, 0.75 + 0.05 * (bm["n"] - 4))
        else:
            # ray tier (honesty): real E2 ridge support at theta* that clearly
            # beats its own reverse direction (dir-safety, as in stripe_fusion).
            # Purely DP-bridged frames (low evidence at theta*) stay 'pred'.
            evs = EV[f, thi]; evrev = EV[f, (thi + NS // 2) % NS]
            band_near = any(band[j] is not None
                            for j in range(max(0, f - BAND_NEAR), min(nf, f + BAND_NEAR + 1)))
            # design C2 policy: at finish the club overlaps/holds and a lone
            # ridge is a static counterfeit risk -> a finish ray is admissible
            # only if band-corroborated. Free-space phases need motion-
            # verifiability instead (a static ridge can't be club there either).
            if phase[f] == "finish":
                verifiable = band_near
            else:
                verifiable = (not static[f]) or band_near
            if phase[f] != "addr" and evs >= RAY_EV_MIN and evs > 1.15 * evrev and verifiable:
                tier = "ray"; conf = 0.55
        rows.append(dict(frame=f, t_s=round(f / fps, 6), phase=phase[f], tier=tier,
                         theta_deg=round(th % 360.0, 2),
                         s_px_mm=round(s, 5) if s else "", r0_mm=round(r0, 1) if r0 else "",
                         head_x=round(hx, 1) if hx != "" else "",
                         head_y=round(hy, 1) if hy != "" else "",
                         inside=round(float(inside[f, thi]), 2), conf=conf))

    stem = os.path.splitext(os.path.basename(args.video))[0]
    os.makedirs(args.out_dir, exist_ok=True)
    # fusion-style CSV (adjudication / scoring)
    fcsv = os.path.join(args.out_dir, f"{stem}_v3.csv")
    cols = ["frame", "t_s", "phase", "tier", "theta_deg", "s_px_mm", "r0_mm",
            "head_x", "head_y", "inside", "conf"]
    with open(fcsv, "w", newline="") as fo:
        w = csv.DictWriter(fo, fieldnames=cols); w.writeheader(); w.writerows(rows)
    # shaft-track contract CSV (frame,grip_x,grip_y,theta_out,kind,conf)
    tcsv = os.path.join(args.out_dir, f"{stem}_v3_track.csv")
    with open(tcsv, "w", newline="") as fo:
        w = csv.writer(fo); w.writerow(["frame", "grip_x", "grip_y", "theta_out", "kind", "conf"])
        for r in rows:
            kind = "pred" if r["tier"] == "pred" else "meas"
            w.writerow([r["frame"], f"{gx[r['frame']]:.2f}", f"{gy[r['frame']]:.2f}",
                        f"{r['theta_deg']:.3f}", kind, r["conf"]])

    tc = {}
    for r in rows:
        tc[r["tier"]] = tc.get(r["tier"], 0) + 1
    lm = f"bs0={bs0} top={top} impact={impf} fin0={fin0} chir={chir:+d}"
    print(f"frames={nf}  {lm}")
    print(f"v3 rows={len(rows)}  tiers: " + " ".join(f"{k}={v}" for k, v in sorted(tc.items())))
    print(f"[out] {fcsv}")

    if args.phases_out:
        pcsv = os.path.join(args.out_dir, f"{stem}_v3_phases.csv")
        with open(pcsv, "w", newline="") as fo:
            w = csv.writer(fo); w.writerow(["frame", "phase", "spd", "phi_s"])
            for f in range(nf):
                w.writerow([f, phase[f], round(float(spd_s[f]), 2), round(float(phi_s[f]), 1)])
        print(f"[phases] {pcsv}")

    if args.overlay:
        cap = cv2.VideoCapture(args.video)
        vw_fps = float(round(cap.get(cv2.CAP_PROP_FPS) or 30.0)) or 30.0
        byf = {r["frame"]: r for r in rows}
        col = {"band": (0, 0, 255), "ray": (0, 200, 255), "pred": (120, 120, 120)}
        vw = cv2.VideoWriter(os.path.join(args.out_dir, f"{stem}_v3.mp4"),
                             cv2.VideoWriter_fourcc(*"mp4v"), vw_fps, (W, H))
        f = 0
        while True:
            ok, frame = cap.read()
            if not ok:
                break
            r = byf.get(f)
            if r is not None:
                g = anchors[f]; th = math.radians(r["theta_deg"])
                ux, uy = math.cos(th), math.sin(th)
                c = col[r["tier"]]
                if r["head_x"] != "":
                    cv2.line(frame, (int(g[0]), int(g[1])), (int(r["head_x"]), int(r["head_y"])), c, 2)
                    cv2.circle(frame, (int(r["head_x"]), int(r["head_y"])), 10, (255, 0, 255), 2)
                else:
                    L = 380
                    cv2.line(frame, (int(g[0]), int(g[1])), (int(g[0] + L * ux), int(g[1] + L * uy)), c, 2)
                cv2.putText(frame, f"f{f} {r['phase']} {r['tier']} th={r['theta_deg']:.0f} in={r['inside']}",
                            (10, 26), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (255, 255, 255), 2)
            vw.write(frame)
            f += 1
        cap.release(); vw.release()
        print(f"[overlay] {os.path.join(args.out_dir, stem + '_v3.mp4')}")

    if args.truth_out:
        if not args.clipmeta:
            sys.exit("--truth-out requires --clipmeta")
        meta = json.load(open(args.clipmeta)); tt = meta["t_us"]
        entries = []
        for r in rows:
            if r["tier"] == "pred":
                continue                             # honesty: pred kept out of truth
            g = anchors[r["frame"]]
            e = dict(t_us=int(tt[r["frame"]]), theta=round(math.radians(r["theta_deg"]), 6),
                     grip=[round(g[0], 1), round(g[1], 1)], tier=r["tier"], conf=r["conf"])
            if r["head_x"] != "":
                e["head"] = [r["head_x"], r["head_y"]]
                e["len"] = round(math.hypot(r["head_x"] - g[0], r["head_y"] - g[1]), 1)
            entries.append(e)
        tj = dict(meta=dict(club=args.club, source="instrumented", tool="club_track_v3",
                            n=len(entries)), shaft=entries)
        tpath = os.path.join(meta["swingDir"], "truth.json")
        if os.path.exists(tpath):
            old = json.load(open(tpath))
            if old.get("meta", {}).get("source") != "instrumented":
                sys.exit(f"refusing to overwrite non-instrumented {tpath}")
        json.dump(tj, open(tpath, "w"), indent=1)
        print(f"[truth] {tpath} ({len(entries)} entries)")


if __name__ == "__main__":
    main()
