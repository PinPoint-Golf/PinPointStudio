#!/usr/bin/env python3
"""Set S (v3.2) — synthetic machinery gate for address_theta_v3 (the resting
address / hold theta). Mirrors make_synth_v3's proven flip-free double-pendulum
swing (so the frozen v3.0 track is clean) and adds a LONG still address hold with
two address-specific counterfeits positioned to reproduce the exact real-data
failure mode v3.2 must survive:

  * counterfeit A — "trailing leg / shadow": a strong bright line from near the
    grip at an angle OUTSIDE the tight address cone, BODY-attached (it shifts with
    the body sway while the grip is fixed). In a single frame it OUTSCORES the
    club's ridge (longer, continuous) — a naive wide-cone ridge locks onto it —
    but under grip-registered STACKING it smears away. Tests: stack suppression.
  * counterfeit B — "rigid crease at the grip": a bright line from the grip at a
    different out-of-cone angle, GRIP-rigid (it survives the stack). Tests: the
    tight address CONE rejects a stack-surviving counterfeit.

The resting club sits at a known theta over the blown mat (dark-on-mat) with bright
bands over the dark body (bright-on-cloth) — the mat-crossing polarity structure.
address_theta_v3 must recover the club theta, publish it, reject BOTH counterfeits,
and never flip.

  make_synth_v32.py --out-dir DIR     # generate
  make_synth_v32.py --selftest        # generate + club_track_v3 + address_theta_v3 + assert
"""
import argparse, csv, json, math, os, subprocess, sys, tempfile
import numpy as np, cv2

# Geometry MIRRORS make_synth_v3 exactly (proven flip-free swing) so the v3.0 track
# prior is clean. v3.2 only enriches the ADDRESS hold (frames < HOLD_END) with the
# two counterfeits + a larger body sway so the body-attached counterfeit smears.
W, H, FPS = 900, 720, 120.0
HUB = np.array([450.0, 235.0])
ARM = 178.0
DT = 1.0 / FPS
T_EXP = DT * 0.5
SUBS = 6
CLUB = "7 IRON"
BANDS = [308.0, 362.0, 560.0, 758.0, 808.0, 854.0]
LEN_MM = 940.0
S_PXMM = 0.35
R0_MM = 150.0

BETA_KF = [(0, 90), (40, 90), (66, 170), (88, 90), (110, 10), (142, 10)]
PSI_KF = [(0, 0), (40, 0), (66, 88), (88, 2), (110, -88), (142, -88)]
NF = 142
TRUE_IMPACT = 88
HOLD_END = 38                        # counterfeits drawn on the still address only
SWAY_ADDR = 16.0                     # body sway amplitude px during address
# CF_A: IN the tight cone (|75-90|=15 < 28) but body-attached and OSCILLATING, so a
# single frame's ridge outscores the club yet the grip-registered STACK smears it
# away (tests stack suppression -- the exact s01 leg-at-75 vs club-at-98 regime).
# CF_B: OUT of the cone (|130-90|=40 > 28) and grip-RIGID, so it survives the stack
# and must be rejected by the CONE alone (tests the tight address cone).
CF_A_DEG = 75.0
CF_B_DEG = 130.0


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


def addr_sway(f):
    """Symmetric, multi-cycle body sway during the hold so the body-attached
    counterfeit A smears over its full +-SWAY range under grip-registration (a
    single positive hump would just relocate it, not smear it)."""
    return SWAY_ADDR * math.sin(f / 4.0) if f < HOLD_END else 3.0 * math.sin(f / 40.0)


def joints(f):
    cx = 450.0 + addr_sway(f)
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
    rng = np.random.default_rng(5)
    speck = [(int(rng.uniform(120, 780)), int(rng.uniform(600, 705))) for _ in range(12)]

    clip = os.path.join(out_dir, "faceon_swing.mp4")
    vw = cv2.VideoWriter(clip, cv2.VideoWriter_fourcc(*"mp4v"), int(FPS), (W, H))
    anch, skel, tus, shaft_json = [], [], [], []
    for f in range(NF):
        acc = np.zeros((H, W, 3), np.float32)
        th_next = theta[min(f + 1, NF - 1)]
        for s in range(SUBS):
            u = s / (SUBS - 1)
            sub = np.full((H, W, 3), 28, np.uint8)
            sub[600:, :] = (205, 205, 205)                     # blown mat
            J = joints(f)
            hull = cv2.convexHull(np.array(J, np.float32)).astype(np.int32)
            cv2.fillConvexPoly(sub, hull, (70, 70, 70))        # body
            g = grips[f]
            if f < HOLD_END:
                # counterfeit A (leg/shadow): body-attached (its base rides the
                # sway), from near the grip at an in-cone angle. A single frame's
                # continuous bright line outscores the dotted club, but the grip-
                # registered stack smears it over the +-SWAY band.
                ax0 = int(g[0] + addr_sway(f) - 4); ay0 = 422
                da = (math.cos(math.radians(CF_A_DEG)), math.sin(math.radians(CF_A_DEG)))
                cv2.line(sub, (ax0, ay0), (int(ax0 + 430 * da[0]), int(ay0 + 430 * da[1])),
                         (190, 190, 190), 3, cv2.LINE_AA)
                # counterfeit B (rigid crease): from the FIXED grip, out-of-cone,
                # survives the stack -> must be rejected by the cone alone.
                db = (math.cos(math.radians(CF_B_DEG)), math.sin(math.radians(CF_B_DEG)))
                cv2.line(sub, tuple(g.astype(int)),
                         (int(g[0] + 300 * db[0]), int(g[1] + 300 * db[1])),
                         (205, 205, 205), 4, cv2.LINE_AA)
            else:
                # mid-swing counterfeits from make_synth_v3 (keep the track clean)
                cv2.line(sub, (int(J[5][0]), 410), (int(J[7][0]), 660), (200, 200, 200), 3, cv2.LINE_AA)
            for sx, sy in speck:
                cv2.circle(sub, (sx, sy), 2, (255, 255, 255), -1)
            th_s = theta[f] + (th_next - theta[f]) * u
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
               "W": W, "H": H, "src": "synth", "t_us": tus},
              open(os.path.join(out_dir, "clipmeta.json"), "w"))
    json.dump({"meta": dict(club=CLUB, source="synthetic", tool="make_synth_v32", n=NF),
               "shaft": shaft_json}, open(os.path.join(out_dir, "truth.json"), "w"))
    json.dump({CLUB: {"shaftType": "steel", "lengthMm": LEN_MM, "bandWidthMm": 25,
                      "bandCentersMm": BANDS, "hoselFromButtMm": 882}},
              open(os.path.join(out_dir, "clubs.json"), "w"))
    addr_theta = float(theta[HOLD_END // 2])             # known resting club theta
    return clip, TRUE_IMPACT, addr_theta


def _parse_kv(text):
    kv = {}
    for tok in text.replace("\n", " ").split():
        if "=" in tok:
            k, _, v = tok.partition("=")
            try:
                kv[k] = float(v)
            except ValueError:
                pass
    return kv


def selftest():
    tmp = tempfile.mkdtemp(prefix="synthv32_")
    clip, impact, addr_theta = render(tmp)
    here = os.path.dirname(os.path.abspath(__file__))
    # 1) frozen v3.0 track (impact given; the address is pred territory in v3.0)
    r0 = subprocess.run([sys.executable, os.path.join(here, "club_track_v3.py"), clip,
                         "--anchors", f"{tmp}/anchors.csv", "--skeleton", f"{tmp}/skeleton.csv",
                         "--clubs", f"{tmp}/clubs.json", "--club", CLUB,
                         "--clipmeta", f"{tmp}/clipmeta.json", "--fps-override", str(FPS),
                         "--impact-frame", str(impact), "--out-dir", tmp],
                        capture_output=True, text=True)
    if r0.returncode:
        print(r0.stdout[-500:]); print(r0.stderr[-1500:]); return 1
    stem = os.path.splitext(os.path.basename(clip))[0]
    # 2) v3.2 address theta
    r1 = subprocess.run([sys.executable, os.path.join(here, "address_theta_v3.py"), clip,
                         "--anchors", f"{tmp}/anchors.csv", "--track", f"{tmp}/{stem}_v3.csv",
                         "--clubs", f"{tmp}/clubs.json", "--club", CLUB,
                         "--clipmeta", f"{tmp}/clipmeta.json", "--fps-override", str(FPS),
                         "--out-dir", tmp],
                        capture_output=True, text=True)
    print(r1.stdout.strip())
    if r1.returncode:
        print(r1.stderr[-1800:]); return 1
    kv = _parse_kv(r1.stdout)
    theta0 = kv.get("THETA0", float("nan"))
    published = int(kv.get("ADDR_PUBLISHED", 0))
    flips = int(kv.get("FLIPS", 9))
    # error vs the known resting club theta (wrapped)
    err = abs((theta0 - addr_theta + 180) % 360 - 180)
    # counterfeit rejection: theta0 must be the club, NOT A or B
    err_A = abs((theta0 - CF_A_DEG + 180) % 360 - 180)
    err_B = abs((theta0 - CF_B_DEG + 180) % 360 - 180)
    print(f"true addr theta={addr_theta:.1f}  measured theta0={theta0:.1f}  err={err:.2f}  "
          f"(dist to cf_A={err_A:.0f}, cf_B={err_B:.0f})  published={published} flips={flips}")
    # theta0 recovers the club (err small) and is well clear of BOTH counterfeits
    # (nearer the club than either): stack defeats in-cone A, cone rejects B.
    ok = (published > 0 and err <= 5.0 and flips == 0 and err_A > 8 and err_B > 8)
    print("SELFTEST-V32", "PASS" if ok else "FAIL", f"({tmp})")
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out-dir", default="synthv32")
    a = ap.parse_args()
    clip, impact, addr_theta = render(a.out_dir)
    print(f"wrote {clip} ({NF} frames) addr_theta={addr_theta:.1f} impact~f{impact}")


if __name__ == "__main__":
    if "--selftest" in sys.argv:
        raise SystemExit(selftest())
    main()
