#!/usr/bin/env python3
"""Stripe detector — instrumented-club (retro-band) shaft measurement.

Per frame: saturated-blob detection -> RANSAC collinear line near the grip
anchor -> known-ratio pattern match against the per-club band record
(clubs.json) -> theta + per-frame foreshortening scale s (px/mm) + hosel/head
extrapolation. Distance RATIOS along the shaft are projection-invariant, so
the match holds at any club orientation; the asymmetric 2-1-3 band pattern
fixes the butt->tip direction. Only ratio-verified frames are emitted as meas
(>=3 matched bands); there is no tracker and no prediction tier — misses are
absent rows, honesty by construction (this feeds the auto-truth writer).

  stripe_annotate.py <clip> --anchors anchors.csv --clubs clubs.json \
      --club "7 IRON" [--clipmeta clipmeta.json] [--out-dir out] \
      [--overlay] [--truth-out]

--truth-out writes <swingDir>/truth.json (source: "instrumented") from
clipmeta's swingDir + t_us — the auto-truth path (protocol section 5.2).
"""
import argparse, csv, itertools, json, math, os, sys
import numpy as np, cv2

SAT_T = 235          # band pixels saturate under on-axis light
# Address-phase captures ARE ring-lit (bright retro return along the whole
# shaft) but at this exposure the bands only peak marginally over SAT_T:
# they bloom into the glove/shaft or fragment to 1-2 px specks, while the
# speckle field (mat sparkle, sleeve highlights) offers combinatorially
# abundant false affine fits — adjudicated on tape_20260705 s01: admitting
# 60 proximity-sorted 1px+ blobs made junk n=4 matches outnumber real ones
# ~3:1 even with the dark-gap check. Address needs a profile-correlation
# detector (band/gap template along candidate lines), not looser blobs;
# this saturated-blob detector is scoped to the club-up phases.
AREA_MIN, AREA_MAX = 3, 2500
MAX_BLOBS = 20
GRIP_GATE = 80.0     # line must pass within this of the grip anchor (px)
LAT_TOL = 4.0        # blob-to-line lateral inlier tolerance (px)
S_MIN, S_MAX = 0.05, 0.55   # px/mm; >0.55 needs the club far off the athlete plane
R0_MIN, R0_MAX = -50.0, 260.0  # butt -> grip-anchor offset along shaft (mm)
# Anchor tier (standalone-trusted): 5+ bands, or 4 tight. 3-band matches are
# direction-ambiguous when the subset is the near-symmetric tip trio (46/50 mm
# gaps) — adjudicated 36% flips on s01 — so they only pass the temporal
# rescue: consistent with interpolation between surrounding anchor frames.
RMS4, RMS5 = 1.5, 3.0
RMS3 = 1.5
RESCUE_GAP = 45      # max anchor-to-anchor span (frames) for n=3 rescue
RESCUE_DTH = 5.0     # deg agreement with interpolated theta
RESCUE_DS = 0.12     # fractional s agreement
GAP_MM_MAX = 60.0    # "within-group" adjacent band spacing (54/50/46 mm)
GAP_DARK = 222       # bare steel between group bands must dip below this


def gap_dark_ok(gray, mpts_r, s):
    """A real match must show >=1 dark within-group gap: between adjacent
    matched bands of the same group the shaft is bare steel, well below
    saturation, while speckle matches over the blown mat stay bright.
    mpts_r: [(x, y, r_mm)] for matched blobs, sorted by r."""
    H, W = gray.shape
    found_pair = False
    for (x1, y1, r1), (x2, y2, r2) in zip(mpts_r, mpts_r[1:]):
        if r2 - r1 > GAP_MM_MAX:
            continue
        found_pair = True
        mids = [(x1 + t * (x2 - x1), y1 + t * (y2 - y1)) for t in (0.4, 0.5, 0.6)]
        vals = [gray[int(y), int(x)] for x, y in mids
                if 0 <= int(x) < W and 0 <= int(y) < H]
        if vals and max(vals) <= GAP_DARK:
            return "dark"
    return "nopair" if not found_pair else "bright"


def detect_blobs(gray, gx, gy, rmax):
    _, bw = cv2.threshold(gray, SAT_T, 255, cv2.THRESH_BINARY)
    n, lab, stats, cent = cv2.connectedComponentsWithStats(bw, 8)
    out = []
    for i in range(1, n):
        a = stats[i, cv2.CC_STAT_AREA]
        if not AREA_MIN <= a <= AREA_MAX:
            continue
        cx, cy = cent[i]
        d = math.hypot(cx - gx, cy - gy)
        if d > rmax:
            continue
        out.append((cx, cy, a, d))
    out.sort(key=lambda b: -b[2])
    return [(cx, cy, a) for cx, cy, a, _ in out[:MAX_BLOBS]]


def flip_rms(tj, rk):
    """Best affine fit of the same (ascending) projections onto the band
    subset with its gap sequence REVERSED. Small value means a 180-degree
    flipped club fits these blobs too — direction ambiguous."""
    rr = np.cumsum(np.r_[0.0, np.diff(rk)[::-1]])
    A = np.vstack([rr, np.ones(len(rk))]).T
    c, *_ = np.linalg.lstsq(A, tj, rcond=None)
    if c[0] <= 0:
        return float("inf")
    return float(np.sqrt(np.mean(np.square(tj - A @ c))))


def match_pattern(tproj, bands, s_lo=S_MIN, s_hi=S_MAX):
    """Order-preserving affine match t = s*(r - r0). RANSAC over blob/band
    pairs; returns (n, rms, s, r0, pairs) best by (n, -rms)."""
    best = None
    nb = len(tproj)
    for (a, b) in itertools.combinations(range(nb), 2):
        ta, tb = tproj[a], tproj[b]
        if tb <= ta:
            ta, tb = tb, ta
            a, b = b, a
        for (i, l) in itertools.combinations(range(len(bands)), 2):
            s = (tb - ta) / (bands[l] - bands[i])
            if not s_lo <= s <= s_hi:
                continue
            r0 = bands[i] - ta / s
            if not R0_MIN <= r0 <= R0_MAX:
                continue
            tol = max(3.0, 0.2 * 46.0 * s)   # < half the tightest band gap
            used, pairs, errs = set(), [], []
            for k, r in enumerate(bands):
                tp = s * (r - r0)
                j = min(range(nb), key=lambda j: abs(tproj[j] - tp))
                if j in used or abs(tproj[j] - tp) > tol:
                    continue
                used.add(j)
                pairs.append((j, k))
                errs.append(tproj[j] - tp)
            if len(pairs) < 2:
                continue
            rms = float(np.sqrt(np.mean(np.square(errs))))
            key = (len(pairs), -rms)
            if best is None or key > best[0]:
                # refit s, r0 on all matched pairs (least squares)
                order = np.argsort([bands[k] for _, k in pairs])
                tj = np.array([tproj[pairs[o][0]] for o in order])
                rk = np.array([bands[pairs[o][1]] for o in order])
                if np.any(np.diff(tj) <= 0):
                    continue          # not order-preserving: garbage match
                A = np.vstack([rk, np.ones_like(rk)]).T
                coef, *_ = np.linalg.lstsq(A, tj, rcond=None)
                s2 = float(coef[0])
                if not s_lo <= s2 <= s_hi:
                    continue
                r02 = float(-coef[1] / s2)
                errs2 = tj - s2 * (rk - r02)
                rms2 = float(np.sqrt(np.mean(np.square(errs2))))
                best = (key, len(pairs), rms2, s2, r02, pairs,
                        flip_rms(tj, rk))
    if best is None:
        return 0, 0.0, 0.0, 0.0, [], 0.0
    return best[1], best[2], best[3], best[4], best[5], best[6]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("video")
    ap.add_argument("--anchors", required=True)
    ap.add_argument("--clubs", required=True)
    ap.add_argument("--club", required=True)
    ap.add_argument("--clipmeta", default=None,
                    help="clipmeta.json (t_us + swingDir); required for --truth-out")
    ap.add_argument("--fps-override", type=float, default=None)
    ap.add_argument("--out-dir", default=".")
    ap.add_argument("--overlay", action="store_true")
    ap.add_argument("--truth-out", action="store_true",
                    help="write <swingDir>/truth.json (source: instrumented)")
    ap.add_argument("--allow-n3", action="store_true",
                    help="accept flip-unique self-clustered 3-band matches "
                         "(ONLY safe with an on-axis ring light)")
    args = ap.parse_args()

    rec = json.load(open(args.clubs))[args.club]
    bands = [float(r) for r in rec["bandCentersMm"]]
    r_hosel, r_len = float(rec["hoselFromButtMm"]), float(rec["lengthMm"])

    anchors = {}
    for row in csv.reader(open(args.anchors)):
        anchors[int(row[0])] = (float(row[1]), float(row[2]))

    cap = cv2.VideoCapture(args.video)
    if not cap.isOpened():
        sys.exit(f"cannot open {args.video}")
    W = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
    H = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
    fps = args.fps_override or cap.get(cv2.CAP_PROP_FPS) or 30.0
    rmax = 0.62 * H

    stem = os.path.splitext(os.path.basename(args.video))[0]
    os.makedirs(args.out_dir, exist_ok=True)

    # pass 1: per-frame detection + ratio match (no tracking, no drawing)
    cand, f = {}, 0
    while True:
        ok, frame = cap.read()
        if not ok:
            break
        gx, gy = anchors.get(f, (None, None))
        if gx is None:
            f += 1
            continue
        gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
        blobs = detect_blobs(gray, gx, gy, rmax)
        res = None
        if len(blobs) >= 2:
            pts = np.array([(b[0], b[1]) for b in blobs])
            tried = []
            for (a, b) in itertools.combinations(range(len(pts)), 2):
                d = pts[b] - pts[a]
                nd = math.hypot(*d)
                if nd < 8:
                    continue
                u = d / nd
                ang = math.atan2(u[1], u[0]) % math.pi
                if any(abs((ang - t + math.pi / 2) % math.pi - math.pi / 2) < 0.03
                       for t in tried):
                    continue
                tried.append(ang)
                # lateral distances to the line through pts[a] along u
                rel = pts - pts[a]
                lat = np.abs(rel[:, 0] * u[1] - rel[:, 1] * u[0])
                idx = np.where(lat < LAT_TOL)[0]
                if len(idx) < 2:
                    continue
                grel = np.array((gx, gy)) - pts[a]
                if abs(float(grel[0] * u[1] - grel[1] * u[0])) > GRIP_GATE:
                    continue
                # project inliers, origin at the grip anchor's foot on the line
                t0 = float(np.dot(grel, u))
                for sgn in (1.0, -1.0):
                    tp = [sgn * (float(np.dot(p - pts[a], u)) - t0) for p in pts[idx]]
                    n, rms, s, r0, pairs, fl = match_pattern(tp, bands)
                    gate = RMS3 if n == 3 else (RMS4 if n == 4 else RMS5)
                    if n >= 3 and rms <= gate:
                        mpr = sorted(((pts[idx[j]][0], pts[idx[j]][1], bands[k])
                                      for j, k in pairs), key=lambda m: m[2])
                        gd = gap_dark_ok(gray, mpr, s)
                        # n<=4 must PROVE a dark within-group gap; n>=5 has
                        # enough ratio redundancy that only a bright gap
                        # (speckle over the blown mat) disqualifies
                        if gd == "bright" or (n <= 4 and gd != "dark"):
                            continue
                        c = (n, -rms, s, r0, sgn * u,
                             [tuple(pts[idx[j]]) for j, _ in pairs], fl)
                        if res is None or c[:2] > res[:2]:
                            res = c
        if res is not None:
            n, nrms, s, r0, u, mpts, fl = res
            cand[f] = dict(n=n, rms=-nrms, s=s, r0=r0, flip=fl,
                           theta=math.degrees(math.atan2(u[1], u[0])) % 360.0,
                           u=(float(u[0]), float(u[1])), mpts=mpts,
                           n_blobs=len(blobs))
        f += 1
    cap.release()

    # classify: anchors stand alone; n=3 passes only if flip-unique (the
    # reversed gap sequence does NOT fit — kills tip-trio 46/50 ambiguity)
    # and self-clustered in time, or by consistency with nearby anchors
    anchor_f = sorted(k for k, c in cand.items()
                      if c["n"] >= 5 or (c["n"] == 4 and c["rms"] <= RMS4))
    accepted = {k: "anchor" for k in anchor_f}
    af = np.array(anchor_f)

    # Default OFF: with downlight-only capture the bands do not blaze at
    # address (retro return goes back to the source, not the camera), so
    # n=3 candidates there are static-scene junk that also self-clusters —
    # adjudicated on tape_20260705 s01 (f140: trouser/mat highlights, 44deg
    # off). Re-enable per-session only with an on-axis ring light.
    n3u = [] if not args.allow_n3 else sorted(
        k for k, c in cand.items()
        if c["n"] == 3 and c["rms"] <= 1.0 and k not in accepted
        and c["flip"] > max(3.0 * c["rms"], 2.0))
    for k in n3u:
        c = cand[k]
        agree = 0
        for k2 in n3u + anchor_f:
            if k2 == k or abs(k2 - k) > 10:
                continue
            c2 = cand[k2]
            dth = abs((c["theta"] - c2["theta"] + 180.0) % 360.0 - 180.0)
            if dth <= 3.0 + 0.8 * abs(k2 - k) and abs(c["s"] - c2["s"]) <= 0.10 * c2["s"]:
                agree += 1
        if agree >= 2:
            accepted[k] = "n3u"

    def circ_interp(t1, a1, t2, a2, t):
        d = (a2 - a1 + 180.0) % 360.0 - 180.0
        return (a1 + d * (t - t1) / max(t2 - t1, 1e-9)) % 360.0

    for k, c in cand.items():
        if k in accepted or c["n"] != 3 or len(af) == 0:
            continue
        i = int(np.searchsorted(af, k))
        lo = af[i - 1] if i > 0 else None
        hi = af[i] if i < len(af) else None
        ok3 = False
        if lo is not None and hi is not None and hi - lo <= RESCUE_GAP:
            th = circ_interp(lo, cand[lo]["theta"], hi, cand[hi]["theta"], k)
            si = np.interp(k, [lo, hi], [cand[lo]["s"], cand[hi]["s"]])
            dth = abs((c["theta"] - th + 180.0) % 360.0 - 180.0)
            ok3 = dth <= RESCUE_DTH and abs(c["s"] - si) <= RESCUE_DS * si
        elif (lo is not None and k - lo <= 3) or (hi is not None and hi - k <= 3):
            nb = lo if (lo is not None and k - lo <= 3) else hi
            dth = abs((c["theta"] - cand[nb]["theta"] + 180.0) % 360.0 - 180.0)
            ok3 = dth <= RESCUE_DTH and abs(c["s"] - cand[nb]["s"]) <= RESCUE_DS * cand[nb]["s"]
        if ok3:
            accepted[k] = "rescued"

    rows = []
    for k in sorted(accepted):
        c = cand[k]
        gx, gy = anchors[k]
        s, r0, u = c["s"], c["r0"], c["u"]
        bx, by = gx - s * r0 * u[0], gy - s * r0 * u[1]
        conf = 0.9 if c["n"] >= 5 else (0.75 if c["n"] == 4 else 0.6)
        rows.append(dict(frame=k, t_s=round(k / fps, 6), n_blobs=c["n_blobs"],
                         n_match=c["n"], tier=accepted[k],
                         rms_px=round(c["rms"], 2), s_px_mm=round(s, 5),
                         theta_deg=round(c["theta"], 2),
                         butt_x=round(bx, 1), butt_y=round(by, 1),
                         r0_mm=round(r0, 1),
                         hosel_x=round(bx + s * r_hosel * u[0], 1),
                         hosel_y=round(by + s * r_hosel * u[1], 1),
                         head_x=round(bx + s * r_len * u[0], 1),
                         head_y=round(by + s * r_len * u[1], 1),
                         conf=conf))

    if rows:
        smax = np.percentile([r["s_px_mm"] for r in rows], 98)
        for r in rows:
            r["rho"] = round(min(1.0, r["s_px_mm"] / smax), 3)

    # pass 2: overlay from the FINAL accepted rows (adjudication must see
    # exactly what downstream consumers get)
    if args.overlay:
        bydf = {r["frame"]: r for r in rows}
        cap = cv2.VideoCapture(args.video)
        vw_fps = float(round(cap.get(cv2.CAP_PROP_FPS) or 30.0)) or 30.0
        vw = cv2.VideoWriter(os.path.join(args.out_dir, f"{stem}_stripe.mp4"),
                             cv2.VideoWriter_fourcc(*"mp4v"), vw_fps, (W, H))
        f = 0
        while True:
            ok, frame = cap.read()
            if not ok:
                break
            r = bydf.get(f)
            if r is not None:
                cv2.line(frame, (int(r["butt_x"]), int(r["butt_y"])),
                         (int(r["head_x"]), int(r["head_y"])), (0, 0, 255), 2)
                for px, py in cand[f]["mpts"]:
                    cv2.circle(frame, (int(px), int(py)), 8, (0, 255, 255), 2)
                cv2.circle(frame, (int(r["head_x"]), int(r["head_y"])), 10,
                           (255, 0, 255), 2)
                cv2.putText(frame, f"f{f} {r['tier']} n={r['n_match']} "
                            f"rms={r['rms_px']} th={r['theta_deg']:.1f}",
                            (10, 26), cv2.FONT_HERSHEY_SIMPLEX, 0.7,
                            (255, 255, 255), 2)
            else:
                why = "no ratio match" if f not in cand else \
                    f"n={cand[f]['n']} unverified"
                cv2.putText(frame, f"f{f} {why}", (10, 26),
                            cv2.FONT_HERSHEY_SIMPLEX, 0.7, (128, 128, 128), 2)
            vw.write(frame)
            f += 1
        cap.release()
        vw.release()
    out_csv = os.path.join(args.out_dir, f"{stem}_stripe.csv")
    cols = ["frame", "t_s", "n_blobs", "n_match", "tier", "rms_px", "s_px_mm",
            "rho", "theta_deg", "butt_x", "butt_y", "r0_mm", "hosel_x",
            "hosel_y", "head_x", "head_y", "conf"]
    with open(out_csv, "w", newline="") as fo:
        wcsv = csv.DictWriter(fo, fieldnames=cols)
        wcsv.writeheader()
        wcsv.writerows(rows)
    tc = {t: sum(1 for r in rows if r["tier"] == t)
          for t in ("anchor", "n3u", "rescued")}
    n3all = sum(1 for c in cand.values() if c["n"] == 3)
    print(f"frames={len(anchors)}  stripe-meas={len(rows)}  "
          f"anchors={tc['anchor']} n3u={tc['n3u']} rescued={tc['rescued']}  "
          f"n=3 dropped: {n3all - tc['n3u'] - tc['rescued']}")
    print(f"[out] {out_csv}")

    if args.truth_out:
        if not args.clipmeta:
            sys.exit("--truth-out requires --clipmeta")
        meta = json.load(open(args.clipmeta))
        tt = meta["t_us"]
        entries = []
        for r in rows:
            gx, gy = anchors[r["frame"]]
            entries.append(dict(
                t_us=int(tt[r["frame"]]), theta=round(math.radians(r["theta_deg"]), 6),
                grip=[round(gx, 1), round(gy, 1)],
                head=[r["head_x"], r["head_y"]],
                len=round(math.hypot(r["head_x"] - gx, r["head_y"] - gy), 1),
                n_bands=r["n_match"], conf=r["conf"]))
        tj = dict(meta=dict(club=args.club, source="instrumented",
                            tool="stripe_annotate", n=len(entries)),
                  shaft=entries)
        tpath = os.path.join(meta["swingDir"], "truth.json")
        if os.path.exists(tpath):
            old = json.load(open(tpath))
            if old.get("meta", {}).get("source") != "instrumented":
                sys.exit(f"refusing to overwrite non-instrumented {tpath} "
                         "(hand labels?)")
        json.dump(tj, open(tpath, "w"), indent=1)
        print(f"[truth] {tpath} ({len(entries)} shaft entries)")


if __name__ == "__main__":
    main()
