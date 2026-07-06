#!/usr/bin/env python3
"""Set S (v3.1) — synthetic machinery gate for shift_stack_v3 (exposure-arc +
shift-and-stack). Renders a full double-pendulum swing with a FAST impact zone
and a near-full-frame exposure (tau ~= T, like the real WT camera), so:

  * the single-frame shaft is a motion-blurred banded STREAK whose angular extent
    about the grip == the sub-frame sweep (exposure-arc ground truth), and
  * consecutive frames de-rotate coherently under the true omega (shift-and-stack).

The true theta(t) is known exactly, so omega_true(f) = |central diff of theta| is
the ground truth for the exposure-arc and the stack. Emits the v3 input set
(clip + anchors + skeleton + clipmeta WITH exposure_s + truth + clubs.json).

  make_synth_v31.py --out-dir DIR     # generate
  make_synth_v31.py --selftest        # generate + club_track_v3 + shift_stack_v3 + assert
"""
import argparse, csv, json, math, os, subprocess, sys, tempfile
import numpy as np, cv2

# Geometry MIRRORS make_synth_v3 exactly (its proven flip-free swing) so the v3.0
# track prior is clean; v3.1 ONLY changes the exposure (tau ~= T for a real motion-
# blur arc) + the sub-frame blur count, and adds exposure_s to clipmeta.
W, H, FPS = 900, 720, 120.0
HUB = np.array([450.0, 235.0])
ARM = 178.0
DT = 1.0 / FPS
EXPO_FRAC = 0.90                     # tau / T  (near full-frame, like the real cam)
T_EXP = DT * EXPO_FRAC
SUBS = 14                            # smooth sub-frame blur
CLUB = "7 IRON"
BANDS = [308.0, 362.0, 560.0, 758.0, 808.0, 854.0]
LEN_MM = 940.0
S_PXMM = 0.35
R0_MM = 150.0

BETA_KF = [(0, 90), (40, 90), (66, 170), (88, 90), (110, 10), (142, 10)]
PSI_KF = [(0, 0), (40, 0), (66, 88), (88, 2), (110, -88), (142, -88)]
NF = 142
TRUE_IMPACT = 88


def _interp_eased(kf, n):
    fs = np.array([k[0] for k in kf], float)
    vs = np.array([k[1] for k in kf], float)
    out = np.zeros(n)
    for i in range(n):
        j = int(np.clip(np.searchsorted(fs, i) - 1, 0, len(fs) - 2))
        u = (i - fs[j]) / max(fs[j + 1] - fs[j], 1e-9)
        u = 0.5 - 0.5 * math.cos(math.pi * min(max(u, 0), 1))
        out[i] = vs[j] + (vs[j + 1] - vs[j]) * u
    return out


def joints(f):
    sway = 3.0 * math.sin(f / 40.0)
    cx = 450.0 + sway
    return [(cx - 62, 236), (cx + 62, 236), (cx - 46, 402), (cx + 46, 402),
            (cx - 42, 542), (cx + 42, 542), (cx - 40, 664), (cx + 40, 664)]


def draw_banded_shaft(img, grip, theta_deg, thin):
    th = math.radians(theta_deg)
    d = np.array([math.cos(th), math.sin(th)])
    butt = grip - S_PXMM * R0_MM * d
    head = butt + S_PXMM * LEN_MM * d
    cv2.line(img, tuple(butt.astype(int)), tuple(head.astype(int)), (85, 85, 85), 5, cv2.LINE_AA)
    for b in BANDS:
        p = butt + S_PXMM * b * d
        if 0 <= p[0] < W and 0 <= p[1] < H:
            cv2.circle(img, tuple(p.astype(int)), 4 if thin else 5, (255, 255, 255), -1, cv2.LINE_AA)
    return head


def render(out_dir):
    os.makedirs(out_dir, exist_ok=True)
    beta = _interp_eased(BETA_KF, NF)
    psi = _interp_eased(PSI_KF, NF)
    grips = np.stack([HUB[0] + ARM * np.cos(np.radians(beta)),
                      HUB[1] + ARM * np.sin(np.radians(beta))], 1)
    theta = beta + psi
    # dense theta for sub-frame blur (linear within a frame is fine at these rates)
    rng = np.random.default_rng(7)
    speck = [(int(rng.uniform(120, 780)), int(rng.uniform(600, 705))) for _ in range(12)]

    clip = os.path.join(out_dir, "faceon_swing.mp4")
    vw = cv2.VideoWriter(clip, cv2.VideoWriter_fourcc(*"mp4v"), int(FPS), (W, H))
    anch, skel, tus, shaft_json = [], [], [], []
    for f in range(NF):
        acc = np.zeros((H, W, 3), np.float32)
        th_next = theta[min(f + 1, NF - 1)]
        for s in range(SUBS):
            u = (s / (SUBS - 1)) * EXPO_FRAC          # sample only across the exposure
            sub = np.full((H, W, 3), 28, np.uint8)
            sub[600:, :] = (205, 205, 205)            # blown mat
            J = joints(f)
            hull = cv2.convexHull(np.array(J, np.float32)).astype(np.int32)
            cv2.fillConvexPoly(sub, hull, (70, 70, 70))
            cv2.line(sub, (int(J[5][0]), 410), (int(J[7][0]), 660), (200, 200, 200), 3, cv2.LINE_AA)
            for sx, sy in speck:
                cv2.circle(sub, (sx, sy), 2, (255, 255, 255), -1)
            th_s = theta[f] + (th_next - theta[f]) * u
            g = grips[f]
            cv2.line(sub, tuple(HUB.astype(int)), tuple(g.astype(int)), (150, 150, 150), 4, cv2.LINE_AA)
            fast = abs(th_next - theta[max(f - 1, 0)]) > 6
            draw_banded_shaft(sub, g, th_s, fast)
            cv2.circle(sub, tuple(g.astype(int)), 11, (95, 90, 85), -1)
            acc += sub.astype(np.float32)
        frame = (acc / SUBS).astype(np.uint8)
        frame = np.clip(frame.astype(np.int16) + rng.normal(0, 3, frame.shape).astype(np.int16), 0, 255).astype(np.uint8)
        vw.write(frame)

        g = grips[f]
        phi = math.degrees(math.atan2(g[1] - HUB[1], g[0] - HUB[0]))
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
               "W": W, "H": H, "src": "synth", "exposure_s": T_EXP, "t_us": tus},
              open(os.path.join(out_dir, "clipmeta.json"), "w"))
    json.dump({"meta": dict(club=CLUB, source="synthetic", tool="make_synth_v31", n=NF),
               "shaft": shaft_json}, open(os.path.join(out_dir, "truth.json"), "w"))
    json.dump({CLUB: {"shaftType": "steel", "lengthMm": LEN_MM, "bandWidthMm": 25,
                      "bandCentersMm": BANDS, "hoselFromButtMm": 882}},
              open(os.path.join(out_dir, "clubs.json"), "w"))
    return clip, TRUE_IMPACT, theta


def selftest():
    tmp = tempfile.mkdtemp(prefix="synthv31_")
    clip, impact, theta = render(tmp)
    here = os.path.dirname(os.path.abspath(__file__))
    # 1) v3.0 track (impact given, so the phase model is exercised but not under test here)
    r0 = subprocess.run([sys.executable, os.path.join(here, "club_track_v3.py"), clip,
                         "--anchors", f"{tmp}/anchors.csv", "--skeleton", f"{tmp}/skeleton.csv",
                         "--clubs", f"{tmp}/clubs.json", "--club", CLUB,
                         "--clipmeta", f"{tmp}/clipmeta.json", "--fps-override", str(FPS),
                         "--impact-frame", str(impact), "--out-dir", tmp],
                        capture_output=True, text=True)
    if r0.returncode:
        print(r0.stdout[-500:]); print(r0.stderr[-1500:]); return 1
    stem = os.path.splitext(os.path.basename(clip))[0]
    # 2) shift-and-stack over the impact zone
    r1 = subprocess.run([sys.executable, os.path.join(here, "shift_stack_v3.py"), clip,
                         "--anchors", f"{tmp}/anchors.csv", "--track", f"{tmp}/{stem}_v3.csv",
                         "--clubs", f"{tmp}/clubs.json", "--club", CLUB,
                         "--clipmeta", f"{tmp}/clipmeta.json", "--skeleton", f"{tmp}/skeleton.csv",
                         "--fps-override", str(FPS), "--impact-frame", str(impact),
                         "--k", "10", "--out-dir", tmp],
                        capture_output=True, text=True)
    print(r1.stdout.strip())
    if r1.returncode:
        print(r1.stderr[-1800:]); return 1

    # ground-truth omega per frame (deg/frame); peak over the impact window
    om_true = np.abs(np.gradient(theta))
    v31 = {int(x["frame"]): x for x in csv.DictReader(open(f"{tmp}/{stem}_v31_impact.csv"))}
    win = sorted(v31)
    peak_true = float(np.max([om_true[f] for f in win]))
    arc_err, flips, upg = [], 0, 0
    peak_emit, peak_arc = 0.0, 0.0
    for f, x in v31.items():
        if x["omega_exparc"] != "":
            arc_err.append(abs(float(x["omega_exparc"]) - om_true[f]))
            peak_arc = max(peak_arc, float(x["omega_exparc"]))
        if x["omega_emit"] != "":
            peak_emit = max(peak_emit, float(x["omega_emit"]))
        # theta[] is already in DEGREES (beta+psi); compare directly (no radians conv)
        if abs((float(x["theta_track"]) - theta[f] + 180) % 360 - 180) > 90:
            flips += 1
        upg += (x["tier0"] != "band" and x["tier31"] == "band")
    arc_err.sort()
    n = len(arc_err)
    arc_med = arc_err[n // 2] if n else 99
    emit_rel = abs(peak_emit - peak_true) / max(peak_true, 1e-6)
    arc_rel = abs(peak_arc - peak_true) / max(peak_true, 1e-6)
    print(f"omega_true peak={peak_true:.1f} deg/f   emit peak={peak_emit:.1f} (rel {emit_rel:.2f})  "
          f"exparc peak={peak_arc:.1f} (rel {arc_rel:.2f})")
    print(f"exposure-arc: n={n} median|err|={arc_med:.2f} deg/f   band-upgrades={upg}  flips={flips}")
    # machinery gate. PRIMARY: the omega PROFILE peak is recovered (emit + the
    # independent exposure-arc), with no flips -- that is the v3.1 deliverable.
    # SECONDARY: per-frame exposure-arc error stays near its ~3 deg/f single-frame
    # noise floor (thickness/threshold); the smoothed profile is what is reported.
    ok = (n >= 12 and emit_rel <= 0.18 and arc_rel <= 0.30 and flips == 0
          and arc_med <= 3.0)
    print("SELFTEST-V31", "PASS" if ok else "FAIL", f"({tmp})")
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out-dir", default="synthv31")
    a = ap.parse_args()
    clip, impact, _ = render(a.out_dir)
    print(f"wrote {clip} ({NF} frames, expo_frac={EXPO_FRAC}) impact~f{impact}")


if __name__ == "__main__":
    if "--selftest" in sys.argv:
        raise SystemExit(selftest())
    main()
