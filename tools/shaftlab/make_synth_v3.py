#!/usr/bin/env python3
"""Set S — synthetic machinery gate for club_track_v3 (v3.0 constraint system).

Unlike make_synth.py (a near-static-grip clubhead-stage fixture) this renders a
full double-pendulum swing so the HANDS-ONLY phase model, C3 chirality and the
DP all exercise: a moving grip on an arm arc, a banded shaft hinging at the
wrist, a body polygon, and a counterfeit population positioned so each is
rejected by a specific constraint:

  * trouser crease  -> inside the body polygon during mid-swing (C2) AND its
                       support continues behind the grip (C1)
  * lead-arm line   -> points from the grip back INTO the forearm (C4 arm-veto)
  * mat speckle     -> bright static blobs that do not ride the club (motion)

Emits the exact v3 input set (clip + anchors.csv + skeleton.csv + clipmeta.json
+ truth.json) so club_track_v3 runs on it unmodified.

  make_synth_v3.py --out-dir DIR            # generate
  make_synth_v3.py --selftest               # generate + run club_track_v3 + assert
"""
import argparse, csv, json, math, os, subprocess, sys, tempfile
import numpy as np, cv2

W, H, FPS = 900, 720, 120.0
HUB = np.array([450.0, 235.0])       # shoulder pivot
ARM = 178.0                          # hub -> grip (px)
DT = 1.0 / FPS
T_EXP = DT * 0.5
SUBS = 6
CLUB = "7 IRON"
BANDS = [308.0, 362.0, 560.0, 758.0, 808.0, 854.0]
LEN_MM = 940.0
S_PXMM = 0.35                        # foreshortening scale (px/mm)
R0_MM = 150.0                        # butt sits this far behind the grip anchor

# swing keyframes (frame, degrees). beta = arm angle about hub; psi = wrist.
# Tuned for realistic grip velocity (>8 px/f through the swing, ~0 in the holds)
# so the hands-only phase model segments takeaway/top/impact correctly.
# NB no keyframe AT impact: an eased keyframe there would zero the grip velocity
# and break the downswing motion run, so the phase model would mislabel the
# through as 'finish' (rail-off) and the gap test would be inert. beta/psi
# interpolate smoothly through impact (peak grip speed at the ball), giving clean
# downswing/impact/thru phases. beta=90 (=> grip back to address height) recurs
# at f88, so impact still detects there.
BETA_KF = [(0, 90), (40, 90), (66, 170), (110, 10), (142, 10)]
PSI_KF = [(0, 0), (40, 0), (66, 88), (110, -88), (142, -88)]
NF = 142
TRUE_IMPACT = 88

# v3.0-r1 psi-rail gate: an evidence gap + a psi-non-monotone COUNTERFEIT.
# Through [GAP_LO,GAP_HI] the real club is made INVISIBLE (impact-blur analogue,
# no bands, no shaft) so the only evidence is a bright DECOY line held at a FIXED
# theta. A fixed theta satisfies C3 (d_theta = 0, monotone-boundary) but, because
# the arm phi keeps releasing under it, implies psi = theta - phi that RE-HINGES
# (rises) through the downswing -- anatomically impossible. Without the rail the
# DP locks the bright decoy (its only evidence) and emits a wrong 'ray'; with the
# rail the re-hinge is penalised and the DP bridges via the arm witness to 'pred'.
# The gap sits FULLY INSIDE the impact phase (detected impact~f84 -> impact span
# [72,96]) -- required since the v3.0-r1 refinement narrowed RECON_PHASES to
# ("impact",): theta is reconstructed (arm-witness) only in the impact blur, so the
# gap must lie there for the reconstruction/decoy-rejection to exercise. It is also
# clear of the psi_free top window [64,79] (top~f67). [85,95] is late enough that
# the fixed decoy diverges from the monotone truth (a clear re-hinge / large
# residual at f88-89) yet leaves a 1-2 frame margin to the impact boundary at f96
# so a small shift in the detected impact frame across hosts cannot spill the gap
# into the (measured, non-reconstructed) follow-through.
GAP_LO, GAP_HI = 85, 95
DECOY_ON = True


def _interp_eased(kf, n):
    fs = np.array([k[0] for k in kf], float)
    vs = np.array([k[1] for k in kf], float)
    out = np.zeros(n)
    for i in range(n):
        j = int(np.clip(np.searchsorted(fs, i) - 1, 0, len(fs) - 2))
        u = (i - fs[j]) / max(fs[j + 1] - fs[j], 1e-9)
        u = 0.5 - 0.5 * math.cos(math.pi * min(max(u, 0), 1))    # ease
        out[i] = vs[j] + (vs[j + 1] - vs[j]) * u
    return out


def joints(f):
    """8 body joints (Lsh,Rsh,Lhip,Rhip,Lknee,Rknee,Lank,Rank) with tiny sway."""
    sway = 3.0 * math.sin(f / 40.0)
    cx = 450.0 + sway
    return [(cx - 62, 236), (cx + 62, 236), (cx - 46, 402), (cx + 46, 402),
            (cx - 42, 542), (cx + 42, 542), (cx - 40, 664), (cx + 40, 664)]


def draw_banded_shaft(img, grip, theta_deg, sub_blur):
    """Dark-steel shaft grip->head with saturated retro bands at BANDS."""
    th = math.radians(theta_deg)
    d = np.array([math.cos(th), math.sin(th)])
    butt = grip - S_PXMM * R0_MM * d
    head = butt + S_PXMM * LEN_MM * d
    cv2.line(img, tuple(butt.astype(int)), tuple(head.astype(int)), (85, 85, 85), 5, cv2.LINE_AA)
    for b in BANDS:
        p = butt + S_PXMM * b * d
        if 0 <= p[0] < W and 0 <= p[1] < H:
            r = 5 if not sub_blur else 4
            cv2.circle(img, tuple(p.astype(int)), r, (255, 255, 255), -1, cv2.LINE_AA)
    return head


def draw_decoy(img, grip, theta_deg):
    """A bright straight scene line from the grip at a FIXED theta -- the psi-
    non-monotone counterfeit. No retro bands (so E1 can never lock it -- it can
    at most be a 'ray'), but bright enough to dominate E2 in the evidence gap."""
    th = math.radians(theta_deg)
    d = np.array([math.cos(th), math.sin(th)])
    a = grip + S_PXMM * 40.0 * d
    b = grip + S_PXMM * LEN_MM * d
    cv2.line(img, tuple(a.astype(int)), tuple(b.astype(int)), (225, 225, 225), 4, cv2.LINE_AA)
    return b


def render(out_dir):
    os.makedirs(out_dir, exist_ok=True)
    beta = _interp_eased(BETA_KF, NF)
    psi = _interp_eased(PSI_KF, NF)
    grips = np.stack([HUB[0] + ARM * np.cos(np.radians(beta)),
                      HUB[1] + ARM * np.sin(np.radians(beta))], 1)
    theta = beta + psi
    theta_decoy = float(theta[GAP_LO - 1])       # held fixed through the gap
    # static mat speckle positions (do not move with the club)
    rng = np.random.default_rng(3)
    speck = [(int(rng.uniform(120, 780)), int(rng.uniform(600, 705))) for _ in range(12)]

    clip = os.path.join(out_dir, "faceon_swing.mp4")
    vw = cv2.VideoWriter(clip, cv2.VideoWriter_fourcc(*"mp4v"), int(FPS), (W, H))
    anch, skel, tus, shaft_json = [], [], [], []
    for f in range(NF):
        acc = np.zeros((H, W, 3), np.float32)
        for s in range(SUBS):
            u = s / (SUBS - 1)
            sub = np.full((H, W, 3), 28, np.uint8)
            sub[600:, :] = (205, 205, 205)                     # blown mat
            J = joints(f)
            hull = cv2.convexHull(np.array(J, np.float32)).astype(np.int32)
            cv2.fillConvexPoly(sub, hull, (70, 70, 70))        # body
            # counterfeit 1: trouser crease (bright line on the trail leg, inside body)
            cv2.line(sub, (int(J[5][0]), 410), (int(J[7][0]), 660), (200, 200, 200), 3, cv2.LINE_AA)
            # counterfeit 3: static mat speckle
            for sx, sy in speck:
                cv2.circle(sub, (sx, sy), 2, (255, 255, 255), -1)
            bt = beta[f] + (psi[0] * 0)  # arm angle at sub time (approx const within frame)
            th_s = theta[f] + (theta[min(f + 1, NF - 1)] - theta[f]) * u
            g = grips[f]
            # counterfeit 2: lead-arm line hub->grip (bright; points into forearm)
            cv2.line(sub, tuple(HUB.astype(int)), tuple(g.astype(int)), (150, 150, 150), 4, cv2.LINE_AA)
            in_gap = DECOY_ON and GAP_LO <= f <= GAP_HI
            if in_gap:
                # real club INVISIBLE (impact-blur analogue) -> only the psi-
                # non-monotone decoy provides evidence in this span.
                draw_decoy(sub, g, theta_decoy)
            else:
                fast = abs(theta[min(f + 1, NF - 1)] - theta[max(f - 1, 0)]) > 6
                draw_banded_shaft(sub, g, th_s, fast)
            cv2.circle(sub, tuple(g.astype(int)), 11, (95, 90, 85), -1)   # hands
            acc += sub.astype(np.float32)
        frame = (acc / SUBS).astype(np.uint8)
        frame = np.clip(frame.astype(np.int16) + rng.normal(0, 3, frame.shape).astype(np.int16), 0, 255).astype(np.uint8)
        vw.write(frame)

        g = grips[f]
        phi = math.degrees(math.atan2(g[1] - HUB[1], g[0] - HUB[0]))   # elbow(hub)->grip
        anch.append([f, round(float(g[0]), 3), round(float(g[1]), 3), round(phi, 2), 1])
        row = [f]
        for (jx, jy) in joints(f):
            row += [round(jx, 1), round(jy, 1), 0.95]
        skel.append(row)
        t_us = int(round(f * DT * 1e6)); tus.append(t_us)
        th = math.radians(theta[f]); d = np.array([math.cos(th), math.sin(th)])
        butt = g - S_PXMM * R0_MM * d; head = butt + S_PXMM * LEN_MM * d
        shaft_json.append(dict(t_us=t_us, theta=round(math.radians(theta[f] % 360), 6),
                               grip=[round(float(g[0]), 1), round(float(g[1]), 1)],
                               head=[round(float(head[0]), 1), round(float(head[1]), 1)],
                               len=round(float(np.hypot(*(head - g))), 1)))
    vw.release()
    with open(os.path.join(out_dir, "anchors.csv"), "w", newline="") as fo:
        csv.writer(fo).writerows(anch)
    with open(os.path.join(out_dir, "skeleton.csv"), "w", newline="") as fo:
        csv.writer(fo).writerows(skel)
    json.dump({"swingDir": os.path.abspath(out_dir), "frame0": 0, "fps": FPS,
               "W": W, "H": H, "src": "synth", "t_us": tus},
              open(os.path.join(out_dir, "clipmeta.json"), "w"))
    json.dump({"meta": dict(club=CLUB, source="synthetic", tool="make_synth_v3", n=NF),
               "shaft": shaft_json}, open(os.path.join(out_dir, "truth.json"), "w"))
    # a per-swing clubs.json so the tool has band geometry
    json.dump({CLUB: {"shaftType": "steel", "lengthMm": LEN_MM, "bandWidthMm": 25,
                      "bandCentersMm": BANDS, "hoselFromButtMm": 882}},
              open(os.path.join(out_dir, "clubs.json"), "w"))
    return clip, TRUE_IMPACT, theta


def _run_tool(tmp, clip, extra):
    """Run club_track_v3 fully hands-only (no --impact-frame so C3 is tested).
    Returns (returncode, stdout)."""
    here = os.path.dirname(os.path.abspath(__file__))
    r = subprocess.run([sys.executable, os.path.join(here, "club_track_v3.py"), clip,
                        "--anchors", f"{tmp}/anchors.csv", "--skeleton", f"{tmp}/skeleton.csv",
                        "--clubs", f"{tmp}/clubs.json", "--club", CLUB,
                        "--clipmeta", f"{tmp}/clipmeta.json", "--fps-override", str(FPS),
                        "--out-dir", tmp] + list(extra),
                       capture_output=True, text=True)
    return r.returncode, r.stdout, r.stderr


def _score(v3path, truth, tt, anch):
    """Score an emitted v3.csv vs truth. DIRECT measurements = band/ray only
    (pred = bridge, recon = arm-witness reconstruction -- both excluded from the
    error stats, like Mark's honest markup gaps). Returns the tier error stats,
    the full output track (theta per frame incl. reconstruction), and any band/ray
    emitted inside the gap (a decoy locked AS A MEASUREMENT -- the counterfeit A/B)."""
    v3 = {int(x["frame"]): x for x in csv.DictReader(open(v3path))}
    errs, flips, arm_locks, band_n, ray_n, recon_n = [], 0, 0, 0, 0, 0
    track, gap_locked = {}, []
    for f, x in v3.items():
        thv = float(x["theta_deg"]); track[f] = thv
        recon_n += x["tier"] == "recon"
        if x["tier"] in ("pred", "recon"):
            continue
        if GAP_LO <= f <= GAP_HI:            # blackout: a band/ray here = decoy MEASURED
            gap_locked.append((f, thv))      # (rejection metric only -- truth is
            continue                         #  physically unrecoverable through the gap)
        e = abs((thv - truth[tt[f]] + 180) % 360 - 180)
        errs.append(e)
        flips += e > 90
        band_n += x["tier"] == "band"; ray_n += x["tier"] == "ray"
        arm = (anch[f] + 180) % 360                          # C4 into-forearm veto
        if abs((thv - arm + 180) % 360 - 180) < 12:
            arm_locks += 1
    errs.sort()
    return dict(errs=errs, flips=flips, arm_locks=arm_locks, band_n=band_n,
                ray_n=ray_n, recon_n=recon_n, track=track, gap_locked=gap_locked)


def _psi_violations(track, phase, phi_s, psi_lo, psi_hi, tol=2.0):
    """Count psi=theta-phi RELEASE monotonicity violations on the OUTPUT track:
    through downswing/impact/thru psi must not INCREASE (re-hinge), outside the
    free top window. This is the physics the reconciliation restores (the release
    is where it reconstructs; the backswing keeps its measured theta)."""
    v = 0
    for f in range(1, len(phase)):
        if f - 1 not in track or f not in track or psi_lo <= f <= psi_hi:
            continue
        dpsi = ((track[f] - phi_s[f]) - (track[f - 1] - phi_s[f - 1]) + 180) % 360 - 180
        if phase[f] in ("downswing", "impact", "thru") and dpsi > tol:
            v += 1
    return v


def selftest():
    import club_track_v3 as ct
    tmp = tempfile.mkdtemp(prefix="synthv3_")
    clip, impact, theta = render(tmp)
    theta_decoy = float(theta[GAP_LO - 1])
    truth = {e["t_us"]: math.degrees(e["theta"]) for e in json.load(open(f"{tmp}/truth.json"))["shaft"]}
    tt = json.load(open(f"{tmp}/clipmeta.json"))["t_us"]
    anch, phi_raw = {}, []
    for row in sorted(csv.reader(open(f"{tmp}/anchors.csv")), key=lambda r: int(r[0])):
        anch[int(row[0])] = float(row[3]); phi_raw.append(float(row[3]))
    phi_s = ct.smooth_phi(np.array(phi_raw))                 # same de-spike the rail uses

    # ---- rail ON (default) --------------------------------------------
    rc, out, err = _run_tool(tmp, clip, [])
    print(out.strip())
    if rc:
        print(err[-1500:]); return 1
    lm = {}
    for tok in out.replace("\n", " ").split():
        k, _, v = tok.partition("=")
        try:
            lm[k] = int(v)
        except ValueError:
            pass
    top = lm.get("top", 66)
    psi_lo, psi_hi = 0, 0
    for tok in out.split():
        if tok.startswith("free=["):
            psi_lo, psi_hi = (int(z) for z in tok[len("free=["):-1].split(","))
    phase = [""] * NF
    for x in csv.DictReader(open(f"{tmp}/faceon_swing_v3.csv")):
        phase[int(x["frame"])] = x["phase"]
    phase_ok = ("bs0" in lm and "top" in lm and "impact" in lm
                and lm["bs0"] < lm["top"] < lm["impact"]
                and abs(lm["impact"] - impact) <= 12)

    on = _score(f"{tmp}/faceon_swing_v3.csv", truth, tt, anch)
    viol_on = _psi_violations(on["track"], phase, phi_s, psi_lo, psi_hi)
    _rows = list(csv.DictReader(open(f"{tmp}/faceon_swing_v3.csv")))
    _ingap = lambda x: GAP_LO <= int(x["frame"]) <= GAP_HI
    gap_resid = [float(x["psi_err"]) for x in _rows if _ingap(x) and x["psi_err"]]
    # residual-localisation baseline = the WELL-MEASURED swing (backswing/downswing/
    # follow-through), where the real club is directly tracked -> psi_err ~0. NOT
    # impact+thru: since the v3.0-r1 refinement narrowed reconstruction to the impact
    # blur, the clean IMPACT frames ARE the reconstruction zone (legitimately
    # uncertain -> nonzero residual), so they cannot serve as the "clean" baseline;
    # and thru is now measured & anchors the fit (residual ~0). This mirrors real s01
    # ("gap 4.3 vs clean 1.3"), where clean was the well-measured frames.
    clean_resid = [float(x["psi_err"]) for x in _rows
                   if x["phase"] in ("backswing", "downswing", "thru") and x["psi_err"]
                   and not _ingap(x)]
    blur_resid = [float(x["psi_err"]) for x in _rows           # clean impact (blur, recon)
                  if x["phase"] == "impact" and x["psi_err"] and not _ingap(x)]
    n = len(on["errs"]); mean = sum(on["errs"]) / n if n else 99
    bad = sum(1 for e in on["errs"] if e > 15)
    print(f"[recon ON ] band={on['band_n']} ray={on['ray_n']} recon={on['recon_n']}  "
          f"err(non-gap) mean={mean:.2f} median={on['errs'][n // 2]:.2f} max={on['errs'][-1]:.2f} "
          f"bad>15={bad}  flips={on['flips']}  arm-locks={on['arm_locks']}  "
          f"release-psi-viol={viol_on}  gap-measured={len(on['gap_locked'])}  phase_ok={phase_ok}")

    # ---- recon OFF (= v3.0) : the re-hinge / counterfeit must bite ---------
    rc2, out2, err2 = _run_tool(tmp, clip, ["--no-psi-rail"])
    if rc2:
        print(err2[-1500:]); return 1
    off = _score(f"{tmp}/faceon_swing_v3.csv", truth, tt, anch)
    viol_off = _psi_violations(off["track"], phase, phi_s, psi_lo, psi_hi)
    g_res = sum(gap_resid) / len(gap_resid) if gap_resid else 0.0
    c_res = sum(clean_resid) / len(clean_resid) if clean_resid else 0.0
    b_res = sum(blur_resid) / len(blur_resid) if blur_resid else 0.0
    print(f"[recon OFF] release-psi-viol={viol_off}  gap-measured={len(off['gap_locked'])}  "
          f"==>  isotonic removes re-hinges (viol {viol_off}->{viol_on}), rejects decoy "
          f"(gap-measured {len(off['gap_locked'])}->{len(on['gap_locked'])}); residual "
          f"localises error: gap {g_res:.1f} vs well-measured {c_res:.1f} "
          f"(clean-impact blur {b_res:.1f}) deg")

    # ---- verdict (fair: gap is a deliberate blackout, truth unrecoverable) -
    known_ok = (n >= 40 and mean <= 2.0 and on["flips"] == 0 and on["arm_locks"] == 0
                and bad <= max(1, n // 20) and phase_ok)     # known-theta recovery (Set S)
    monotone_ok = viol_on == 0 and viol_off > viol_on        # isotonic restores monotone psi
    reject_ok = len(on["gap_locked"]) < len(off["gap_locked"])  # decoy less measured w/ recon
    resid_ok = g_res > c_res + 2.0                           # residual (psi_err) is elevated
                                                             # in the counterfeit span vs the
                                                             # well-measured swing (~0)
    ok = known_ok and monotone_ok and reject_ok and resid_ok
    print(f"  checks: known-theta={known_ok} monotone-restored={monotone_ok} "
          f"decoy-rejected={reject_ok} residual-localises={resid_ok}")
    print("SELFTEST", "PASS" if ok else "FAIL", f"({tmp})")
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out-dir", default="synthv3")
    a = ap.parse_args()
    clip, impact, _ = render(a.out_dir)
    print(f"wrote {clip} ({NF} frames) + anchors/skeleton/clipmeta/truth/clubs.json  impact~f{impact}")


if __name__ == "__main__":
    if "--selftest" in sys.argv:
        raise SystemExit(selftest())
    main()
