#!/usr/bin/env python3
"""shift_stack_v3.py -- v3.1 shift-and-stack + exposure-arc (impact-zone omega).

Design: docs/design/club_tracking_v3_design.md sec 3; plan:
docs/implementation/shaft_detection_v3_impl.md sec 4 (v3.1); FULL PEDAGOGICAL
EXPLANATION (the C++-port bible): docs/design/club_track_v3_exemplar_explained.md
sec 13. The v3.1 target is
the fast impact segment (impact +-K frames) where a single 6.57 ms frame smears
the taped bands into a rotating streak. It emits an INDEPENDENT physical
angular-velocity profile omega(t) and a deblurred composite for adjudication.

omega(t), triangulated:
  * track    = |central diff of the frozen v3.0 DP theta| -- reliable, and what
               the swing actually did; the EMITTED omega is this, lightly smoothed.
  * exposure-arc = an INDEPENDENT intra-frame measure: the shaft sweeps an arc
               during the exposure tau. Because tau (6.57 ms) ~= the frame period
               T (6.70 ms), one frame's streak angular extent about the grip ~=
               the inter-frame Dtheta. Measured robustly as the theta0-anchored
               equivalent width of the annulus ridge (thickness-corrected), it
               confirms the track omega magnitude + peak without using the DP.
               Sub-frame theta = the sector edges (theta_lead, theta_trail).

Shift-and-stack composite (adjudication): register a short sub-window on the grip
anchor, rotate frame f to the reference by (theta_track[f]-theta_track[f0]) about
the pivot, nanmean-stack. Grip-registration + rotation-about-grip maps the RIGID
club onto itself (the butt lands at grip - s*r0*u(theta0) for every frame), so the
club integrates while the body/background smear into arcs -- a deblurred image
that shows the club is where theta(t) says.

AS-BUILT HONESTY (tape_20260705, retro-reflective bands, near-full-frame
exposure): per-frame SNR is already high, so sqrt(N) stacking does NOT beat the
crisp single frame and E1 cannot re-lock discrete bands in the deep impact zone
(too smeared) -- no tier upgrade here. Shift-and-stack's integration payoff is for
the LOW-SNR passive/untaped regime (v3.3+); on taped data omega(t) is the product.

Additive: reads the frozen v3.0 track, only touches impact +-K; the rest of the
swing's v3.0 output is untouched (gate: no regression elsewhere). Deterministic.

  shift_stack_v3.py <clip> --anchors a.csv --track s_v3.csv --clubs c.json
      --club "7 IRON" --clipmeta m.json --impact-frame N [--skeleton s.csv]
      [--exposure-us U] [--fps-override F] [--k 10] [--wsub 3] [--out-dir out]
"""
import argparse, csv, json, math, os, sys
import numpy as np, cv2
from scipy.ndimage import gaussian_filter1d

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from stripe_fusion import _sample, frame_band_match, BG_HI
from club_track_v3 import circ_wrap

# ---- exposure-arc annulus + sector detection ---------------------------
ARC_R_LO, ARC_R_HI, ARC_R_STEP = 95.0, 335.0, 3.0    # club-covering radii (px)
ARC_WIN = 30.0        # +-deg about track theta (tight: excludes far distractors)
ARC_GRID = 0.5        # deg
ARC_ANCHOR = 8.0      # +-deg about theta0 to seat the local streak peak
ARC_MIN_PEAK = 6.0    # elevated peak below which the streak is too weak
ARC_RUN_FRAC = 0.15   # contiguous run through the peak above this fraction of it
ARC_TH_BIAS = 3.0     # shaft's own angular thickness (deg), subtracted from width
ARC_MAX_EXTENT = 40.0

# ---- shift-and-stack composite -----------------------------------------
COH_SPAN = 30.0       # +-deg about theta0 for the band-relock gate
BAND_R0_MAX = 260.0   # C1 butt-termination on the composite (mm)

# ---- omega smoothing ---------------------------------------------------
OM_SMOOTH = 1.2       # gaussian sigma (frames) for the emitted/plotted omega


def annulus_ridge(gray, gx, gy, thetas_rad):
    """Per-theta mean bright-over-local-bg contrast over a club-covering annulus
    about the grip (polarity-aware like ridge_sweep: dark shaft over blown mat)."""
    R = np.arange(ARC_R_LO, ARC_R_HI, ARC_R_STEP)
    ux, uy = np.cos(thetas_rad)[:, None], np.sin(thetas_rad)[:, None]
    X, Y = gx + ux * R[None, :], gy + uy * R[None, :]
    H, W = gray.shape
    on_max = _sample(gray, X, Y, (-2.0, -1.0, 0.0, 1.0, 2.0), ux, uy, np.max)
    on_min = _sample(gray, X, Y, (-2.0, -1.0, 0.0, 1.0, 2.0), ux, uy, np.min)
    bg = _sample(gray, X, Y, (-12.0, -9.0, 9.0, 12.0), ux, uy, np.median)
    e = np.where(bg > BG_HI, bg - on_min, on_max - bg)
    inb = (X >= 0) & (X < W) & (Y >= 0) & (Y < H)
    return np.where(inb, np.clip(e, 0.0, 120.0), 0.0).mean(axis=1)


def exposure_arc(gray, gx, gy, theta0_deg):
    """Angular sector swept during the exposure, seated at the known track theta0.
    Returns (extent_deg, lead_deg, trail_deg, peak) or None. Robust to threshold
    (equivalent width) and to far distractors (tight window + theta0 anchor)."""
    grid = np.arange(theta0_deg - ARC_WIN, theta0_deg + ARC_WIN + 1e-6, ARC_GRID)
    prof = annulus_ridge(gray, gx, gy, np.radians(grid % 360.0))
    base = float(np.percentile(prof, 20))
    el = np.clip(prof - base, 0.0, None)
    c = len(grid) // 2
    aw = int(ARC_ANCHOR / ARC_GRID)
    a0 = c - aw + int(np.argmax(el[c - aw:c + aw + 1]))     # streak peak near theta0
    peak = float(el[a0])
    if peak < ARC_MIN_PEAK:
        return None
    thr = ARC_RUN_FRAC * peak
    a = a0
    while a > 0 and el[a - 1] > thr:
        a -= 1
    b = a0
    while b < len(el) - 1 and el[b + 1] > thr:
        b += 1
    if a == 0 or b == len(el) - 1:            # streak spills past the window: unreliable
        return None
    equiv = float(el[a:b + 1].sum()) * ARC_GRID / peak      # equivalent width (deg)
    extent = min(max(equiv - ARC_TH_BIAS, 0.0), ARC_MAX_EXTENT)
    return extent, float(grid[a]), float(grid[b]), peak


def warp_to_ref(img, gx, gy, gxr, gyr, rot_deg, W, H):
    """Translate grip[f]->grip[ref], then rotate the club by rot_deg about the
    registered pivot. Rigid club maps onto itself (butt -> grip - s*r0*u(theta0))."""
    Mt = np.array([[1.0, 0.0, gxr - gx], [0.0, 1.0, gyr - gy], [0.0, 0.0, 1.0]])
    Mr = np.vstack([cv2.getRotationMatrix2D((gxr, gyr), rot_deg, 1.0), [0, 0, 1]])
    A = (Mr @ Mt)[:2]
    return cv2.warpAffine(img.astype(np.float32), A, (W, H),
                          flags=cv2.INTER_LINEAR, borderValue=float("nan"))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("video")
    ap.add_argument("--anchors", required=True)
    ap.add_argument("--track", required=True, help="v3.0 *_v3.csv (theta prior)")
    ap.add_argument("--clubs", required=True)
    ap.add_argument("--club", required=True)
    ap.add_argument("--clipmeta", required=True)
    ap.add_argument("--skeleton", default=None)
    ap.add_argument("--exposure-us", type=float, default=None)
    ap.add_argument("--fps-override", type=float, default=None)
    ap.add_argument("--impact-frame", type=int, required=True)
    ap.add_argument("--k", type=int, default=10)
    ap.add_argument("--wsub", type=int, default=3, help="stack sub-window half-width")
    ap.add_argument("--out-dir", default=".")
    args = ap.parse_args()

    rec = json.load(open(args.clubs))[args.club]
    bands = [float(r) for r in rec["bandCentersMm"]]
    r_len = float(rec["lengthMm"])
    meta = json.load(open(args.clipmeta))
    fps = args.fps_override or meta.get("fps") or 149.0
    T_us = 1e6 / fps
    tau_us = args.exposure_us or (meta.get("exposure_s", 0) * 1e6) or (0.98 * T_us)
    expo_frac = tau_us / T_us
    os.makedirs(args.out_dir, exist_ok=True)

    anch = {}
    for row in csv.reader(open(args.anchors)):
        anch[int(row[0])] = (float(row[1]), float(row[2]))
    trk = {}
    for x in csv.DictReader(open(args.track)):
        trk[int(x["frame"])] = (float(x["theta_deg"]), x["phase"], x["tier"])

    impf, K, WSUB = args.impact_frame, args.k, args.wsub
    lo, hi = impf - K, impf + K
    rd_lo, rd_hi = lo - WSUB - 1, hi + WSUB + 1        # pad for central diff + stacks

    cap = cv2.VideoCapture(args.video)
    if not cap.isOpened():
        sys.exit(f"cannot open {args.video}")
    W = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH)); H = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
    cap.set(cv2.CAP_PROP_POS_FRAMES, max(0, rd_lo))
    frames = {}
    for f in range(max(0, rd_lo), rd_hi + 1):
        ok, fr = cap.read()
        if not ok:
            break
        frames[f] = cv2.cvtColor(fr, cv2.COLOR_BGR2GRAY)
    cap.release()

    def th_of(f):
        return trk[f][0] if f in trk else np.nan

    rows, comps = [], {}
    for f in range(lo, hi + 1):
        if f not in frames or f not in anch or f not in trk:
            continue
        gx, gy = anch[f]
        th0, phase, tier0 = trk[f]
        gray = frames[f].astype(np.float32)

        # omega from the DP track (signed central difference)
        fa, fb = f - 1, f + 1
        om_track = (circ_wrap(th_of(fb) - th_of(fa)) / (fb - fa)
                    if fa in trk and fb in trk else np.nan)

        # exposure-arc: independent per-frame |omega|
        arc = exposure_arc(gray, gx, gy, th0)
        if arc is not None:
            extent, lead, trail, _pk = arc
            om_exparc = extent / expo_frac
        else:
            extent = lead = trail = om_exparc = np.nan

        # shift-and-stack composite (nanmean sub-window; adjudication + relock try)
        acc = []
        for j in range(f - WSUB, f + WSUB + 1):
            if j in frames and j in trk:
                gxj, gyj = anch[j]
                rot = circ_wrap(th_of(j) - th0)
                acc.append(warp_to_ref(frames[j], gxj, gyj, gx, gy, rot, W, H))
        comp = np.nanmean(np.stack(acc), axis=0) if acc else gray.copy()
        comps[f] = comp

        # opportunistic E1 band re-lock on the deblurred composite (tier upgrade)
        tier31, s, r0, hx, hy, conf = tier0, "", "", "", "", 0.55
        cu = np.clip(np.nan_to_num(comp, nan=0.0), 0, 255).astype(np.uint8)
        bm = frame_band_match(cu, gx, gy, 0.62 * H, bands)
        if bm is not None and 0.0 < bm["r0"] <= BAND_R0_MAX \
                and abs(circ_wrap(bm["theta"] - th0)) <= COH_SPAN:
            tier31 = "band"; s, r0 = bm["s"], bm["r0"]
            ux, uy = math.cos(math.radians(bm["theta"])), math.sin(math.radians(bm["theta"]))
            bx, by = gx - s * r0 * ux, gy - s * r0 * uy
            hx, hy = bx + s * r_len * ux, by + s * r_len * uy
            conf = min(0.9, 0.75 + 0.05 * (bm["n"] - 4))

        rows.append(dict(
            frame=f, t_s=round(f / fps, 6), phase=phase, tier0=tier0, tier31=tier31,
            theta_track=round(th0 % 360.0, 2),
            omega_track=round(float(om_track), 3) if not np.isnan(om_track) else "",
            omega_exparc=round(float(om_exparc), 3) if not np.isnan(om_exparc) else "",
            arc_extent=round(float(extent), 2) if not np.isnan(extent) else "",
            theta_lead=round(float(lead) % 360.0, 2) if not np.isnan(lead) else "",
            theta_trail=round(float(trail) % 360.0, 2) if not np.isnan(trail) else "",
            s_px_mm=round(s, 5) if s else "", r0_mm=round(r0, 1) if r0 else "",
            head_x=round(hx, 1) if hx != "" else "",
            head_y=round(hy, 1) if hy != "" else "", conf=conf))

    # ---- emitted omega(t): lightly-smoothed |track|; exparc corroborates ----
    om_trk = np.array([abs(float(r["omega_track"])) if r["omega_track"] != "" else np.nan for r in rows])
    om_arc = np.array([float(r["omega_exparc"]) if r["omega_exparc"] != "" else np.nan for r in rows])

    def smooth_nan(v, sigma):
        v = v.copy(); m = np.isfinite(v)
        if m.sum() < 2:
            return v
        if (~m).any():
            v[~m] = np.interp(np.flatnonzero(~m), np.flatnonzero(m), v[m])
        return gaussian_filter1d(v, sigma)

    om_emit = smooth_nan(om_trk, OM_SMOOTH)
    om_arc_s = smooth_nan(om_arc, 1.5)
    for r, v in zip(rows, om_emit):
        r["omega_emit"] = round(float(v), 3) if np.isfinite(v) else ""

    # ---- write CSV ----
    stem = os.path.splitext(os.path.basename(args.video))[0]
    cols = ["frame", "t_s", "phase", "tier0", "tier31", "theta_track",
            "omega_track", "omega_exparc", "omega_emit", "arc_extent",
            "theta_lead", "theta_trail", "s_px_mm", "r0_mm", "head_x", "head_y", "conf"]
    csv_path = os.path.join(args.out_dir, f"{stem}_v31_impact.csv")
    with open(csv_path, "w", newline="") as fo:
        w = csv.DictWriter(fo, fieldnames=cols); w.writeheader(); w.writerows(rows)

    # ---- summary (machine-readable keys for the corpus gate) ----
    pk_trk = float(np.nanmax(om_emit)) if np.isfinite(om_emit).any() else float("nan")
    pk_arc = float(np.nanmax(om_arc_s)) if np.isfinite(om_arc_s).any() else float("nan")
    agree = np.abs(om_trk - om_arc)
    agree_med = float(np.nanmedian(agree)) if np.isfinite(agree).any() else float("nan")
    d2 = np.diff(om_emit[np.isfinite(om_emit)], 2)
    rough = float(np.sqrt(np.mean(d2 ** 2))) if len(d2) else float("nan")
    mph = math.radians(pk_trk * fps) * (r_len / 1000.0) * 2.237
    mph_arc = math.radians(pk_arc * fps) * (r_len / 1000.0) * 2.237
    upg = sum(1 for r in rows if r["tier0"] != "band" and r["tier31"] == "band")
    n_arc = int(np.isfinite(om_arc).sum())
    print(f"impact={impf} window=[{lo},{hi}] fps={fps:.2f} tau={tau_us:.0f}us T={T_us:.0f}us frac={expo_frac:.3f}")
    print(f"OMEGA_PEAK_TRACK={pk_trk:.2f} deg/f  ({mph:.0f} mph)   "
          f"OMEGA_PEAK_EXPARC={pk_arc:.2f} deg/f  ({mph_arc:.0f} mph)   [n_arc={n_arc}]")
    print(f"AGREE_MED={agree_med:.2f} deg/f   ROUGHNESS={rough:.2f}   BAND_UPGRADES={upg}")
    print(f"[csv] {csv_path}")

    # ---- omega(t) plot + composite montage for adjudication ----
    plot_omega(rows, om_arc_s, os.path.join(args.out_dir, f"{stem}_v31_omega.png"), impf)
    montage(rows, comps, anch, W, H, os.path.join(args.out_dir, f"{stem}_v31_composites.png"))


def montage(rows, comps, anch, W, H, path):
    C = 300
    tiles = []
    for r in rows:
        f = r["frame"]; comp = comps.get(f)
        if comp is None:
            continue
        gx, gy = anch[f]
        x0 = int(np.clip(gx - C, 0, W - 2 * C)); y0 = int(np.clip(gy - C, 0, H - 2 * C))
        sub = np.clip(np.nan_to_num(comp[y0:y0 + 2 * C, x0:x0 + 2 * C], nan=0.0), 0, 255).astype(np.uint8)
        sub = cv2.cvtColor(sub, cv2.COLOR_GRAY2BGR)
        th = math.radians(r["theta_track"])
        col = (0, 0, 255) if r["tier31"] == "band" else (0, 200, 255)
        cv2.line(sub, (C, C), (int(C + 380 * math.cos(th)), int(C + 380 * math.sin(th))), col, 2)
        if r["head_x"] != "":
            cv2.circle(sub, (int(r["head_x"] - x0), int(r["head_y"] - y0)), 9, (255, 0, 255), 2)
        cv2.putText(sub, f"f{f} {r['phase']} {r['tier31']}", (6, 22),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.55, (0, 255, 255), 2)
        cv2.putText(sub, f"w={r['omega_emit']} arc={r['arc_extent']}", (6, 290),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.5, (180, 255, 180), 1)
        tiles.append(cv2.resize(sub, (300, 300)))
    if not tiles:
        return
    ncol = 7
    nrow = (len(tiles) + ncol - 1) // ncol
    canvas = np.zeros((nrow * 300, ncol * 300, 3), np.uint8)
    for i, im in enumerate(tiles):
        rr, cc = divmod(i, ncol)
        canvas[rr * 300:(rr + 1) * 300, cc * 300:(cc + 1) * 300] = im
    cv2.imwrite(path, canvas)
    print(f"[montage] {path}")


def plot_omega(rows, om_arc_s, path, impf):
    Wp, Hp, pad = 1100, 460, 60
    canvas = np.full((Hp, Wp, 3), 20, np.uint8)
    fs = [r["frame"] for r in rows]
    if not fs:
        return
    tr = [(r["frame"], abs(float(r["omega_track"]))) for r in rows if r["omega_track"] != ""]
    ar = [(r["frame"], float(r["omega_exparc"])) for r in rows if r["omega_exparc"] != ""]
    em = [(r["frame"], float(r["omega_emit"])) for r in rows if r["omega_emit"] != ""]
    ars = list(zip(fs, om_arc_s))
    allv = [v for _, v in tr + ar + em] or [1.0]
    vmax = max(allv) * 1.15
    f0, f1 = min(fs), max(fs)

    def X(f): return int(pad + (f - f0) / max(f1 - f0, 1) * (Wp - 2 * pad))
    def Y(v): return int(Hp - pad - abs(v) / vmax * (Hp - 2 * pad))
    cv2.line(canvas, (pad, Hp - pad), (Wp - pad, Hp - pad), (90, 90, 90), 1)
    cv2.line(canvas, (pad, pad), (pad, Hp - pad), (90, 90, 90), 1)
    cv2.line(canvas, (X(impf), pad), (X(impf), Hp - pad), (60, 60, 120), 1)
    cv2.putText(canvas, "impact", (X(impf) + 3, pad + 14), cv2.FONT_HERSHEY_SIMPLEX, 0.45, (120, 120, 200), 1)
    for gv in range(0, int(vmax) + 1, 5):
        cv2.line(canvas, (pad, Y(gv)), (Wp - pad, Y(gv)), (45, 45, 45), 1)
        cv2.putText(canvas, str(gv), (12, Y(gv) + 4), cv2.FONT_HERSHEY_SIMPLEX, 0.4, (140, 140, 140), 1)
    series = [("track (raw)", tr, (110, 110, 110), 1), ("exparc (raw)", ar, (80, 200, 255), 1),
              ("exparc (smooth)", ars, (40, 150, 210), 2), ("EMIT omega", em, (90, 130, 255), 2)]
    yy = 22
    for name, pts, c, thick in series:
        pp = [(X(f), Y(v)) for f, v in pts]
        for j in range(1, len(pp)):
            cv2.line(canvas, pp[j - 1], pp[j], c, thick)
        for p in pp:
            cv2.circle(canvas, p, 2, c, -1)
        cv2.putText(canvas, name, (Wp - 210, yy), cv2.FONT_HERSHEY_SIMPLEX, 0.48, c, 2); yy += 20
    cv2.putText(canvas, "impact-zone omega |deg/frame|", (pad, 32), cv2.FONT_HERSHEY_SIMPLEX, 0.6, (220, 220, 220), 1)
    cv2.imwrite(path, canvas)
    print(f"[omega-plot] {path}")


if __name__ == "__main__":
    main()
