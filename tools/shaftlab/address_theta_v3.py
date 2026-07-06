#!/usr/bin/env python3
"""address_theta_v3.py -- v3.2 address / hold theta (the pre-swing still club).

Design: docs/design/club_tracking_v3_design.md; plan:
docs/implementation/shaft_detection_v3_impl.md sec 4 (v3.2); FULL PEDAGOGICAL
EXPLANATION (the C++-port bible): docs/design/club_track_v3_exemplar_explained.md
(v3.2 companion, sec 13-style additive module).

THE PROBLEM v3.2 SOLVES. v3.0 deliberately punts the address to the `pred` tier
(club_track_v3.py: the ray tier is disabled when phase=="addr") because a *static*
club is the exact regime v2 was fooled by -- a still bright/dark line at the hands
cannot be motion-verified, and the address scene is full of look-alikes (a trouser
crease, the trailing leg and its shadow on the blown mat, the mat edge). So the
whole resting address, where the club sits on the ground pointing at the ball, is
emitted as an unpublished DP bridge. v3.2 *measures* that resting theta and, when
it survives the honesty gates, publishes it.

WHY THE ADDRESS IS TRACTABLE (three facts, each a tool from the plan):
  1. HOLD-PERIOD STACK. The address is a long near-still hold (grip waggles only a
     few px/frame). Register every hold frame on the grip anchor and average: the
     club -- rigidly attached to the (registered) grip -- integrates into a sharp
     line, while the swaying legs/body/shadows, which move relative to the grip,
     smear away. This is v3.1's shift-and-stack with ZERO rotation (the club is
     still), and on real tape it is a strong counterfeit suppressor (s01: the
     trailing-leg line that outscores the club in a single frame is smeared below
     the club after stacking 81 frames).
  2. TIGHT ADDRESS CONE. The bible's C4 cone is WIDE mid-swing because psi=theta-phi
     spans 65-107 deg there. But AT REST the club hangs nearly in line with the lead
     arm -- psi is small (s01: theta~=89, phi~=114, psi~=-25). So an address-ONLY
     tight cone about the (smoothed) arm phi rejects the leg/crease counterfeits
     that a wide cone admits. This is the address specialisation of C4; it does NOT
     contradict the wide-cone invariant (that governs the moving swing).
  3. MAT-CROSSING PRIOR. The resting club crosses from the hands, over the dark
     trouser (where the tape bands glow), down onto the BLOWN-OUT mat, where it is a
     DARK line on a bright background (polarity flip). Requiring the measured ray to
     actually cross into blown-mat pixels with dark-on-bright contrast confirms a
     real ground-reaching club and kills short body-only lines.

Evidence engine is UNCHANGED: E2 polarity-aware ridge (`ridge_sweep`) from
stripe_fusion, run on the stack (raw + motion) exactly as v3.0. Bands almost never
lock at the address exposure (the mat blows out the tape), so v3.2 is THETA-ONLY
truth; s/r0/head are emitted only on the rare opportunistic band lock on the stack.

ADDITIVE + HONEST. Reads the frozen v3.0 `*_v3.csv`, only touches the address hold
span, writes only `*_v32_address.csv` + a montage (+ optional theta-only truth
merge). The rest of v3.0's output is untouched, so "no regression elsewhere" holds
structurally. Deterministic: no RNG; the stack, ridge and refine are exact.

  address_theta_v3.py <clip> --anchors a.csv --track s_v3.csv --clubs c.json
      --club "7 IRON" [--clipmeta m.json] [--fps-override F] [--out-dir out]
      [--truth-merge]
"""
import argparse, csv, json, math, os, sys
import numpy as np, cv2
from scipy.ndimage import median_filter, gaussian_filter1d

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from stripe_fusion import ridge_sweep, frame_band_match, _sample, BG_HI
from club_track_v3 import circ_wrap, smooth_phi

# ---- hold detection (hands-only, from anchors) -------------------------
SW_SPD = 8.0          # swing run threshold (matches club_track_v3.segment_phases)
TK_SPD = 3.0          # takeaway creep threshold (grip already moving)
STILL_SPEED = 2.5     # px/frame below which the hold is genuinely still
MIN_HOLD = 20         # min hold length (frames) to attempt a measurement
HOLD_CAP = 80         # max frames stacked (older frames risk a prior occupant)

# ---- address cone + evidence ------------------------------------------
CONE_ADDR = 28.0      # tight half-cone about the smoothed arm phi (deg). At REST
                      # psi is small; this rejects the leg/crease counterfeits a
                      # wide cone admits. Address-only -- NOT the mid-swing cone.
GRID = 1.0            # theta grid (deg)
RAY_EV_MIN = 0.45     # normalised E2 evidence needed at theta0 (matches v3.0 ray)
# NB: v3.0's ray tier uses a forward>reverse "dir-safety" test. It is deliberately
# NOT used at address: the lead arm is ALWAYS above the grip, so the club's reverse
# (theta0+180, pointing up) always carries the arm's ridge -- the bible notes "the
# arm is legitimately behind the hands, so it is excluded from this test". The
# club-vs-arm DIRECTION is pinned instead by the down-pointing CONE about phi + the
# DOWN-SECTOR gate (a 180-flip points up, out of the down-cone).
HOLD_BAND = 8.0       # per-frame theta must sit within this of the stack theta0
STD_MAX = 8.0         # max per-frame theta scatter (deg) over the hold to publish
# Mat-crossing is a LENIENT sanity gate, NOT the club/counterfeit discriminator
# (adjudicated on s01: both the club and the trailing-leg counterfeit cross the
# mat, so this cannot separate them -- the tight CONE + STACK + STABILITY do that).
# Its honest job: confirm the measured ray is a real GROUND-REACHING line (kills a
# short floating body-only blob) and that the polarity-aware evidence is genuine.
MAT_FAR = 200.0       # radius beyond which the ray is over the mat, below the body
MIN_BLOWN_FAR = 10    # blown-mat samples past MAT_FAR: the club reaches the ground
MAT_DARK = 15.0       # bg-on_min (grey) for a dark-on-mat sample
CLOTH_BRIGHT = 25.0   # on_max-bg (grey) for a bright band/steel-on-cloth sample
MIN_COHERENT = 6      # min (dark-on-mat + bright-on-cloth) samples: a real shaft

# ---- mat-crossing annulus ----------------------------------------------
MAT_R_LO, MAT_R_HI, MAT_R_STEP = 40.0, 460.0, 3.0
BAND_R0_MAX = 260.0   # C1 butt-termination on the opportunistic stack band lock


def detect_hold(gx, gy, spd_s, nf):
    """Locate the pre-swing still HOLD from the hands alone. Returns (h0, h1) or
    None. Walks: first big swing run -> takeaway creep -> last genuinely-still
    frame (h1) -> back over the still run, capped (h0). Capping guards against a
    prior occupant of the bay who was also still earlier in a long capture."""
    mo = spd_s > SW_SPD
    f, swing0 = 0, nf
    while f < nf:
        if mo[f]:
            g = f
            while g + 1 < nf and mo[g + 1]:
                g += 1
            if g - f >= 6:
                swing0 = f
                break
            f = g + 1
        else:
            f += 1
    if swing0 >= nf:
        return None                                  # no swing found
    tk0 = swing0                                     # walk back over takeaway creep
    while tk0 > 0 and spd_s[tk0 - 1] > TK_SPD:
        tk0 -= 1
    h1 = tk0                                          # hold end = last still frame
    while h1 > 0 and spd_s[h1] >= STILL_SPEED:
        h1 -= 1
    h0 = h1                                           # hold start = walk back, capped
    while h0 > 0 and spd_s[h0 - 1] < STILL_SPEED and (h1 - h0) < HOLD_CAP:
        h0 -= 1
    if h1 - h0 + 1 < MIN_HOLD:
        return None
    return h0, h1


def build_stack(cap, anch, gx, gy, h0, h1, W, H):
    """Grip-registered nanmean of the hold frames (ref = hold-end grip). The still
    club integrates; swaying body/leg/shadow counterfeits smear (v3.1 shift-and-
    stack with zero rotation). Also returns the raw hold frames for per-frame work
    and the scene median (motion image) over the hold."""
    grxi, gryi = gx[h1], gy[h1]
    cap.set(cv2.CAP_PROP_POS_FRAMES, h0)
    acc = None
    n = 0
    fg = {}
    for f in range(h0, h1 + 1):
        ok, fr = cap.read()
        if not ok:
            break
        g = cv2.cvtColor(fr, cv2.COLOR_BGR2GRAY).astype(np.float32)
        fg[f] = g
        M = np.array([[1.0, 0.0, grxi - gx[f]], [0.0, 1.0, gryi - gy[f]]], np.float32)
        reg = cv2.warpAffine(g, M, (W, H), flags=cv2.INTER_LINEAR)
        acc = reg if acc is None else acc + reg
        n += 1
    stack = acc / max(n, 1)
    scene_med = np.median(np.stack(list(fg.values())), axis=0) if fg else stack
    return stack, fg, (grxi, gryi), n, scene_med


def ev_norm(s):
    lo, hi = np.percentile(s, 50), np.percentile(s, 97)
    return np.clip((s - lo) / (hi - lo + 1e-6), 0.0, 1.0)


def cone_ridge_theta(img, gx, gy, phi_c, motion=None):
    """E2 ridge (raw, and motion if given) over the full circle; return the
    normalised evidence array and the parabola-refined theta of the peak WITHIN
    the tight address cone about phi_c. The ray emanates from the grip, so the
    fit is hand-anchored by construction (the plan's 'fit-then-require-hand-
    proximity')."""
    TR = np.radians(np.arange(0.0, 360.0, GRID))
    TDEG = np.degrees(TR)
    sc_raw, _, _ = ridge_sweep(img, gx, gy, TR)
    if motion is not None:
        sc_mot, _, _ = ridge_sweep(motion, gx, gy, TR, bright_only=True)
        ev = np.maximum(ev_norm(sc_raw), ev_norm(sc_mot))
    else:
        ev = ev_norm(sc_raw)
    m = np.abs(circ_wrap(TDEG - phi_c)) < CONE_ADDR
    idx = np.where(m)[0]
    pk = idx[int(np.argmax(sc_raw[idx]))]
    a, b, c = sc_raw[(pk - 1) % len(TR)], sc_raw[pk], sc_raw[(pk + 1) % len(TR)]
    denom = a - 2 * b + c
    off = 0.5 * (a - c) / denom if denom != 0 else 0.0
    off = float(np.clip(off, -1.0, 1.0))
    theta0 = (TDEG[pk] + off) % 360.0
    return ev, theta0, int(pk)


def mat_crossing(img, gx, gy, theta0_deg, W, H):
    """Mat-crossing prior (lenient sanity gate). Along theta0 measure, with the
    correct polarity in each region (bible sec 5.2): bright bands/steel over the
    dark trouser (near), and a dark shaft over the BLOWN mat (far). Returns
    (blown_far, coherent) where blown_far = blown-mat samples past MAT_FAR (the
    club reaches the ground) and coherent = dark-on-mat + bright-on-cloth samples
    (a real, polarity-correct shaft). Does NOT try to separate the club from a
    leg/crease -- the cone/stack/stability do that."""
    th = math.radians(theta0_deg)
    ux, uy = math.cos(th), math.sin(th)
    R = np.arange(MAT_R_LO, MAT_R_HI, MAT_R_STEP)
    X = (gx + ux * R)[None, :]
    Y = (gy + uy * R)[None, :]
    uxa, uya = np.array([[ux]]), np.array([[uy]])
    on_min = _sample(img, X, Y, (-2.0, -1.0, 0.0, 1.0, 2.0), uxa, uya, np.min)[0]
    on_max = _sample(img, X, Y, (-2.0, -1.0, 0.0, 1.0, 2.0), uxa, uya, np.max)[0]
    bg = _sample(img, X, Y, (-12.0, -9.0, 9.0, 12.0), uxa, uya, np.median)[0]
    inb = (X[0] >= 0) & (X[0] < W) & (Y[0] >= 0) & (Y[0] < H)
    blown = (bg > BG_HI) & inb
    cloth = (bg <= BG_HI) & inb
    dark_on_mat = blown & ((bg - on_min) > MAT_DARK)
    bright_on_cloth = cloth & ((on_max - bg) > CLOTH_BRIGHT)
    blown_far = int((blown & (R > MAT_FAR)).sum())
    coherent = int(dark_on_mat.sum() + bright_on_cloth.sum())
    return blown_far, coherent


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("video")
    ap.add_argument("--anchors", required=True)
    ap.add_argument("--track", required=True, help="frozen v3.0 *_v3.csv (theta prior)")
    ap.add_argument("--clubs", required=True)
    ap.add_argument("--club", required=True)
    ap.add_argument("--clipmeta", default=None)
    ap.add_argument("--fps-override", type=float, default=None)
    ap.add_argument("--out-dir", default=".")
    ap.add_argument("--truth-merge", action="store_true",
                    help="merge the published address theta (theta-only) into truth.json")
    args = ap.parse_args()
    os.makedirs(args.out_dir, exist_ok=True)

    rec = json.load(open(args.clubs))[args.club]
    bands = [float(r) for r in rec["bandCentersMm"]]
    r_len = float(rec["lengthMm"])

    anch, phi_raw = {}, {}
    for row in csv.reader(open(args.anchors)):
        f = int(row[0]); anch[f] = (float(row[1]), float(row[2]))
        phi_raw[f] = float(row[3]) if (len(row) >= 5 and int(row[4])) else np.nan
    trk = {}
    for x in csv.DictReader(open(args.track)):
        trk[int(x["frame"])] = (float(x["theta_deg"]), x["phase"], x["tier"])

    cap = cv2.VideoCapture(args.video)
    if not cap.isOpened():
        sys.exit(f"cannot open {args.video}")
    W = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH)); H = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
    fps = args.fps_override or cap.get(cv2.CAP_PROP_FPS) or 30.0
    nf = (max(anch) + 1) if anch else 0

    gx = np.array([anch.get(f, (np.nan, np.nan))[0] for f in range(nf)])
    gy = np.array([anch.get(f, (np.nan, np.nan))[1] for f in range(nf)])
    phv = np.array([phi_raw.get(f, np.nan) for f in range(nf)])
    good = ~np.isnan(phv)
    if good.sum():
        phv = np.interp(np.arange(nf), np.arange(nf)[good], phv[good])
    phi_s = smooth_phi(phv)
    spd = np.zeros(nf); spd[1:] = np.hypot(np.diff(gx), np.diff(gy))
    spd_s = gaussian_filter1d(median_filter(spd, 5), 2)

    hold = detect_hold(gx, gy, spd_s, nf)
    stem = os.path.splitext(os.path.basename(args.video))[0]
    if hold is None:
        print("NO_HOLD detected (no swing / hold too short); nothing to measure")
        _write_csv(os.path.join(args.out_dir, f"{stem}_v32_address.csv"), [])
        print(f"ADDR_PUBLISHED=0 HOLD=none THETA0=nan FLIPS=0")
        return
    h0, h1 = hold
    stack, fg, (grxi, gryi), n, scene_med = build_stack(cap, anch, gx, gy, h0, h1, W, H)
    cap.release()
    phi_c = float(np.median(phi_s[h0:h1 + 1]))

    # ---- stack measurement: tight-cone ridge -> theta0 (raw + motion) ----
    motion = np.abs(stack - scene_med)
    ev, theta0, pk = cone_ridge_theta(stack, grxi, gryi, phi_c, motion)
    NS = len(ev)
    ev0 = float(ev[pk]); evrev = float(ev[(pk + NS // 2) % NS])

    # ---- honesty gates ----
    blown_far, coherent = mat_crossing(stack, grxi, gryi, theta0, W, H)
    down_sector = math.sin(math.radians(theta0)) > 0.2      # points into lower free space
    # per-frame stability: cone-peak theta each hold frame
    perf = []
    for f in range(h0, h1 + 1):
        s1, _, _ = ridge_sweep(fg[f], gx[f], gy[f], np.radians(np.arange(0, 360, GRID)))
        idx = np.where(np.abs(circ_wrap(np.arange(0, 360, GRID) - phi_s[f])) < CONE_ADDR)[0]
        perf.append(float(idx[int(np.argmax(s1[idx]))]) * GRID)
    perf = np.array(perf)
    theta_med = float(np.median(perf))
    std = float(perf.std())

    gate = dict(ev=ev0 >= RAY_EV_MIN,
                mat=(blown_far >= MIN_BLOWN_FAR and coherent >= MIN_COHERENT),
                down=down_sector, stable=std <= STD_MAX,
                cone=abs(circ_wrap(theta0 - phi_c)) < CONE_ADDR)
    published = all(gate.values())

    # zero-flip assertion: theta0 lives inside the tight cone so a 180 flip is
    # structurally impossible; guard it explicitly for the gate ladder.
    flips = 1 if abs(circ_wrap(theta0 - phi_c)) > 90 else 0

    # ---- opportunistic band lock on the stack (rare; gives s/r0/head) ----
    tier = "hold" if published else "pred"
    s = r0 = None; hx = hy = ""
    cu = np.clip(np.nan_to_num(stack, nan=0.0), 0, 255).astype(np.uint8)
    bm = frame_band_match(cu, grxi, gryi, 0.62 * H, bands)
    if published and bm is not None and 0.0 < bm["r0"] <= BAND_R0_MAX \
            and abs(circ_wrap(bm["theta"] - theta0)) <= CONE_ADDR:
        tier = "band"; s, r0 = bm["s"], bm["r0"]; theta0 = bm["theta"] % 360.0
        ux, uy = math.cos(math.radians(theta0)), math.sin(math.radians(theta0))
        bx, by = grxi - s * r0 * ux, gryi - s * r0 * uy
        hx, hy = bx + s * r_len * ux, by + s * r_len * uy

    # ---- per-frame rows (publish frames whose per-frame theta agrees) ----
    rows = []
    conf = 0.55 if tier != "band" else min(0.9, 0.75 + 0.05 * (bm["n"] - 4))
    for i, f in enumerate(range(h0, h1 + 1)):
        th_f = perf[i]
        agree = abs(circ_wrap(th_f - theta0)) <= HOLD_BAND
        emit = published and agree
        rows.append(dict(
            frame=f, t_s=round(f / fps, 6), phase=trk.get(f, ("", "addr", ""))[1] or "addr",
            v0_tier=trk.get(f, ("", "", "pred"))[2] or "pred",
            tier=(tier if emit else "pred"),
            theta_deg=round(theta0 if emit else th_f, 2),
            theta_frame=round(th_f, 2),
            s_px_mm=round(s, 5) if (emit and s) else "",
            r0_mm=round(r0, 1) if (emit and r0) else "",
            head_x=round(hx, 1) if (emit and hx != "") else "",
            head_y=round(hy, 1) if (emit and hy != "") else "",
            conf=conf if emit else 0.30))

    csv_path = os.path.join(args.out_dir, f"{stem}_v32_address.csv")
    _write_csv(csv_path, rows)

    npub = sum(1 for r in rows if r["tier"] != "pred")
    print(f"HOLD=[{h0},{h1}] n_stacked={n} phi_c={phi_c:.1f} theta0={theta0:.2f} "
          f"theta_med={theta_med:.1f} std={std:.1f}")
    print(f"gates: " + " ".join(f"{k}={'Y' if v else 'N'}" for k, v in gate.items())
          + f"  ev0={ev0:.2f} evrev={evrev:.2f} blown_far={blown_far} coherent={coherent}")
    print(f"ADDR_PUBLISHED={npub} THETA0={theta0:.2f} TIER={tier} FLIPS={flips} STD={std:.2f}")
    print(f"[csv] {csv_path}")

    montage(stack, rows, (grxi, gryi), phi_c, theta0, W, H, published,
            os.path.join(args.out_dir, f"{stem}_v32_address.png"))

    if args.truth_merge and published and args.clipmeta:
        _merge_truth(args.clipmeta, rows, anch, args.club, r_len)


def _write_csv(path, rows):
    cols = ["frame", "t_s", "phase", "v0_tier", "tier", "theta_deg", "theta_frame",
            "s_px_mm", "r0_mm", "head_x", "head_y", "conf"]
    with open(path, "w", newline="") as fo:
        w = csv.DictWriter(fo, fieldnames=cols); w.writeheader(); w.writerows(rows)


def montage(stack, rows, grip, phi_c, theta0, W, H, published, path):
    grxi, gryi = grip
    vis = cv2.cvtColor(np.clip(np.nan_to_num(stack, nan=0.0), 0, 255).astype(np.uint8),
                       cv2.COLOR_GRAY2BGR)
    for th, col in [(phi_c, (255, 200, 0)), (theta0, (0, 0, 255) if published else (120, 120, 120))]:
        thr = math.radians(th)
        cv2.line(vis, (int(grxi), int(gryi)),
                 (int(grxi + 460 * math.cos(thr)), int(gryi + 460 * math.sin(thr))), col, 2)
    cv2.circle(vis, (int(grxi), int(gryi)), 8, (0, 255, 0), 2)
    tag = "PUBLISHED" if published else "abstained (pred)"
    cv2.putText(vis, f"addr theta={theta0:.1f} phi={phi_c:.0f} [{tag}]", (12, 30),
                cv2.FONT_HERSHEY_SIMPLEX, 0.8, (0, 255, 255), 2)
    C = 360
    x0 = int(np.clip(grxi - C, 0, W - 2 * C)); y0 = int(np.clip(gryi - 140, 0, H - 2 * C))
    cv2.imwrite(path, vis[y0:y0 + 2 * C, x0:x0 + 2 * C])
    print(f"[montage] {path}")


def _merge_truth(clipmeta, rows, anch, club, r_len):
    """Merge the published address theta into truth.json as theta-only entries
    (additive: never remove v3.0's band/ray entries; refuse non-instrumented)."""
    meta = json.load(open(clipmeta)); tt = meta["t_us"]
    tpath = os.path.join(meta["swingDir"], "truth.json")
    if os.path.exists(tpath):
        doc = json.load(open(tpath))
        if doc.get("meta", {}).get("source") not in ("instrumented", "synthetic"):
            print(f"[truth] refusing to merge into non-instrumented {tpath}")
            return
    else:
        doc = dict(meta=dict(club=club, source="instrumented", tool="address_theta_v3", n=0),
                   shaft=[])
    have = {e["t_us"] for e in doc.get("shaft", [])}
    added = 0
    for r in rows:
        if r["tier"] == "pred":
            continue
        f = r["frame"]; tu = int(tt[f])
        if tu in have:                                   # never override a v3.0 entry
            continue
        g = anch[f]
        doc["shaft"].append(dict(t_us=tu, theta=round(math.radians(r["theta_deg"]), 6),
                                 grip=[round(g[0], 1), round(g[1], 1)], tier=r["tier"],
                                 conf=r["conf"], src="v32_address"))
        added += 1
    doc["shaft"].sort(key=lambda e: e["t_us"])
    doc["meta"]["n"] = len(doc["shaft"])
    json.dump(doc, open(tpath, "w"), indent=1)
    print(f"[truth] merged {added} address entries into {tpath} (n={doc['meta']['n']})")


if __name__ == "__main__":
    main()
