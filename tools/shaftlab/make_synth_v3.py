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


def render(out_dir):
    os.makedirs(out_dir, exist_ok=True)
    beta = _interp_eased(BETA_KF, NF)
    psi = _interp_eased(PSI_KF, NF)
    grips = np.stack([HUB[0] + ARM * np.cos(np.radians(beta)),
                      HUB[1] + ARM * np.sin(np.radians(beta))], 1)
    theta = beta + psi
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


def selftest():
    tmp = tempfile.mkdtemp(prefix="synthv3_")
    clip, impact, theta = render(tmp)
    here = os.path.dirname(os.path.abspath(__file__))
    # run FULLY hands-only (no --impact-frame) so the phase model (C3) is tested
    r = subprocess.run([sys.executable, os.path.join(here, "club_track_v3.py"), clip,
                        "--anchors", f"{tmp}/anchors.csv", "--skeleton", f"{tmp}/skeleton.csv",
                        "--clubs", f"{tmp}/clubs.json", "--club", CLUB,
                        "--clipmeta", f"{tmp}/clipmeta.json", "--fps-override", str(FPS),
                        "--out-dir", tmp],
                       capture_output=True, text=True)
    print(r.stdout.strip())
    if r.returncode:
        print(r.stderr[-1500:]); return 1
    # parse the hands-only phase landmarks and check they match the true swing
    lm = {}
    for tok in r.stdout.replace("\n", " ").split():
        if "=" in tok:
            k, _, v = tok.partition("=")
            try:
                lm[k] = int(v)
            except ValueError:
                pass
    phase_ok = ("bs0" in lm and "top" in lm and "impact" in lm
                and lm["bs0"] < lm["top"] < lm["impact"]
                and abs(lm["impact"] - impact) <= 12)
    # score vs truth
    truth = {e["t_us"]: math.degrees(e["theta"]) for e in json.load(open(f"{tmp}/truth.json"))["shaft"]}
    tt = json.load(open(f"{tmp}/clipmeta.json"))["t_us"]
    anch = {}
    for row in csv.reader(open(f"{tmp}/anchors.csv")):
        anch[int(row[0])] = float(row[3])            # phi
    v3 = {int(x["frame"]): x for x in csv.DictReader(open(f"{tmp}/faceon_swing_v3.csv"))}
    errs, flips, arm_locks, band_n, ray_n = [], 0, 0, 0, 0
    for f, x in v3.items():
        if x["tier"] == "pred":
            continue
        thv = float(x["theta_deg"]); tht = truth[tt[f]]
        e = abs((thv - tht + 180) % 360 - 180)
        errs.append(e)
        if e > 90:
            flips += 1
        band_n += x["tier"] == "band"; ray_n += x["tier"] == "ray"
        # C4: a lock must never point into the forearm (phi+180)
        arm = (anch[f] + 180) % 360
        if abs((thv - arm + 180) % 360 - 180) < 12:
            arm_locks += 1
    errs.sort(); n = len(errs)
    mean = sum(errs) / n if n else 99
    bad = sum(1 for e in errs if e > 15)
    print(f"emitted band={band_n} ray={ray_n}  theta err: mean={mean:.2f} "
          f"median={errs[n//2]:.2f} max={errs[-1]:.2f} bad>15={bad}  flips={flips}  "
          f"arm-locks={arm_locks}  phase_ok={phase_ok} (impact det f{lm.get('impact')} vs true f{impact})")
    ok = (n >= 40 and mean <= 2.0 and flips == 0 and arm_locks == 0
          and bad <= max(1, n // 20) and phase_ok)
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
