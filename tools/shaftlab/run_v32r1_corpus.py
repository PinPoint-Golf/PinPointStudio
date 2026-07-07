#!/usr/bin/env python3
"""v3.0-r1 (psi isotonic reconciliation) corpus A/B runner  (STUDIO PC).

For each swing sNN: run club_track_v3 TWICE -- rail ON (isotonic reconciliation,
default) and rail OFF (--no-psi-rail = exact v3.0) -- and compare, on the SAME
host (studio cv2 5.0; never cross-host diff). Reports per-phase coverage,
accuracy vs the swing's v2.0 fusion truth, RELEASE psi-monotonicity violations on
the output track (the physics the reconciliation restores), and flips. The gate:
ON must not regress coverage/accuracy and must reduce release psi-violations.

  run_v32r1_corpus.py --lab-dir <tape_20260705> --clubs clubs.json --club "7 IRON"
      [--only s02,s05] [--swingdir-remap FROM=TO] [--out-dir OUT] [--determinism]
"""
import argparse, csv, json, math, os, subprocess, sys
import numpy as np

PY = sys.executable
HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(HERE, "club_track_v3.py")


def remap(path, rules):
    for a, b in rules:
        if path.startswith(a):
            return b + path[len(a):]
    return path


def wd(a, b):
    return abs((a - b + 180.0) % 360.0 - 180.0)


def phase_of(f, impact, fps, nf):
    d = int(0.45 * fps); t = int(0.55 * fps)
    if f < impact - d:
        return "addr_back"
    if f < impact:
        return "down"
    if f < impact + t:
        return "thru"
    return "finish"


def run(clip, sd, clubs, club, cm_path, fps, impf, outdir, extra):
    cmd = [PY, TOOL, clip, "--anchors", os.path.join(sd, "anchors.csv"),
           "--skeleton", os.path.join(sd, "skeleton.csv"),
           "--clubs", clubs, "--club", club, "--clipmeta", cm_path,
           "--fps-override", str(fps), "--phases-out", "--out-dir", outdir] + extra
    if impf is not None:
        cmd += ["--impact-frame", str(impf)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    return cmd, r


def load(outdir):
    v3 = {int(x["frame"]): x for x in csv.DictReader(open(os.path.join(outdir, "faceon_swing_v3.csv")))}
    phi = {int(x["frame"]): float(x["phi_s"])
           for x in csv.DictReader(open(os.path.join(outdir, "faceon_swing_v3_phases.csv")))}
    return v3, phi


def release_psi_viol(v3, phi, free, tol=2.0):
    """re-hinge count on the OUTPUT track through downswing/impact/thru, outside
    the free top window (the tool prints free=[lo,hi])."""
    lo, hi = free
    fs = sorted(f for f in v3 if v3[f]["phase"] in ("downswing", "impact", "thru")
                and not (lo <= f <= hi))
    v = 0
    for a, b in zip(fs, fs[1:]):
        if b != a + 1:
            continue
        d = ((float(v3[b]["theta_deg"]) - phi[b]) - (float(v3[a]["theta_deg"]) - phi[a]) + 180) % 360 - 180
        if d > tol:
            v += 1
    return v


def parse_free(stdout):
    for tok in stdout.split():
        if tok.startswith("free=["):
            lo, hi = tok[len("free=["):-1].split(",")
            return int(lo), int(hi)
    return (0, 0)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--lab-dir", required=True)
    ap.add_argument("--clubs", required=True)
    ap.add_argument("--club", required=True)
    ap.add_argument("--only", default=None)
    ap.add_argument("--swingdir-remap", default=None)
    ap.add_argument("--out-dir", default="/tmp/v32r1")
    ap.add_argument("--determinism", action="store_true")
    args = ap.parse_args()

    rules = []
    if args.swingdir_remap:
        a, b = args.swingdir_remap.split("=", 1)
        rules = [(a, b)]

    swings = sorted(d for d in os.listdir(args.lab_dir)
                    if d.startswith("s") and d[1:].isdigit()
                    and os.path.isdir(os.path.join(args.lab_dir, d)))
    if args.only:
        swings = [s for s in swings if s in set(args.only.split(","))]

    # aggregates: mode -> phase -> ...
    agg_cov = {"on": {}, "off": {}}       # phase -> [meas, total]
    agg_err = {"on": {}, "off": {}}       # phase -> [errs]
    agg_viol = {"on": 0, "off": 0}
    flips = {"on": 0, "off": 0}
    per_swing = []
    det_cmd = None

    for s in swings:
        sd = os.path.join(args.lab_dir, s)
        clip = os.path.join(sd, "faceon_swing.avi")
        cm_path = os.path.join(sd, "clipmeta.json")
        cm = json.load(open(cm_path))
        fps = cm["fps"]; nf = len(cm["t_us"]); tt = np.array(cm["t_us"], float)
        swingdir = remap(cm["swingDir"], rules)
        sj = os.path.join(swingdir, "swing.json")
        impactUs = json.load(open(sj)).get("capture", {}).get("impactUs") if os.path.exists(sj) else None
        impf = int(np.argmin(np.abs(tt - impactUs))) if impactUs else None

        # v2 truth for accuracy
        tj = os.path.join(swingdir, "truth.json")
        truth = {}
        if os.path.exists(tj):
            for e in json.load(open(tj))["shaft"]:
                truth[int(np.argmin(np.abs(tt - e["t_us"])))] = math.degrees(e["theta"]) % 360.0

        rowvals = {}
        for mode, extra in (("off", ["--no-psi-rail"]), ("on", [])):
            outdir = os.path.join(args.out_dir, f"{s}_{mode}")
            cmd, r = run(clip, sd, args.clubs, args.club, cm_path, fps, impf, outdir, extra)
            if mode == "on" and det_cmd is None:
                det_cmd = list(cmd)
            if r.returncode:
                sys.stderr.write(f"[{s} {mode}] FAIL\n{r.stderr[-800:]}\n"); rowvals = None; break
            v3, phi = load(outdir)
            free = parse_free(r.stdout)
            # coverage + accuracy per phase
            for f in range(nf):
                ph = phase_of(f, impf, fps, nf)
                agg_cov[mode].setdefault(ph, [0, 0])[1] += 1
                if f in v3 and v3[f]["tier"] in ("band", "ray"):
                    agg_cov[mode][ph][0] += 1
            for f, thT in truth.items():
                if f not in v3:
                    continue
                e = wd(float(v3[f]["theta_deg"]), thT)
                agg_err[mode].setdefault(phase_of(f, impf, fps, nf), []).append(e)
                if e > 90:
                    flips[mode] += 1
            viol = release_psi_viol(v3, phi, free)
            agg_viol[mode] += viol
            # per-swing thru-impact summary
            imp = ("down", "thru")
            cov = sum(1 for f in range(nf) if phase_of(f, impf, fps, nf) in imp
                      and f in v3 and v3[f]["tier"] in ("band", "ray"))
            errs = sorted(wd(float(v3[f]["theta_deg"]), truth[f]) for f in truth
                          if f in v3 and phase_of(f, impf, fps, nf) in imp)
            med = errs[len(errs) // 2] if errs else float("nan")
            rowvals[mode] = dict(cov=cov, med=med, viol=viol,
                                 recon=sum(1 for f in v3 if v3[f]["tier"] == "recon"))
        if rowvals:
            per_swing.append((s, impf, rowvals))
            o, n = rowvals["off"], rowvals["on"]
            print(f"[{s}] impact=f{impf}  cov(thru-imp) {o['cov']}->{n['cov']}  "
                  f"med {o['med']:.1f}->{n['med']:.1f}  rel-psi-viol {o['viol']}->{n['viol']}  "
                  f"recon={n['recon']}")

    print("\n==================== v3.0-r1 CORPUS A/B (OFF=v3.0 / ON=isotonic) ====================")
    print("\ncoverage (measured band+ray / frames) by phase:")
    for ph in ("addr_back", "down", "thru", "finish"):
        co = agg_cov["off"].get(ph, [0, 1]); cn = agg_cov["on"].get(ph, [0, 1])
        print(f"  {ph:10s} OFF {co[0]:4d}/{co[1]:<4d} ({100*co[0]/max(co[1],1):3.0f}%)   "
              f"ON {cn[0]:4d}/{cn[1]:<4d} ({100*cn[0]/max(cn[1],1):3.0f}%)")
    print("\naccuracy vs v2 truth by phase (deg)  [median / p90 / bad>15]:")
    for ph in ("addr_back", "down", "thru", "finish"):
        def st(v):
            v = sorted(v); n = len(v)
            if not n:
                return "   -"
            return f"n={n:3d} med={v[n//2]:4.1f} p90={v[int(n*0.9)]:4.1f} bad={sum(e>15 for e in v)}"
        print(f"  {ph:10s} OFF {st(agg_err['off'].get(ph, []))}   ON {st(agg_err['on'].get(ph, []))}")
    print(f"\nRELEASE psi-violations (re-hinge on output track):  OFF={agg_viol['off']}  ON={agg_viol['on']}")
    print(f"FLIPS (err>90 at truth frames):  OFF={flips['off']}  ON={flips['on']}")

    if args.determinism and det_cmd:
        det = list(det_cmd); oi = det.index("--out-dir")
        det[oi + 1] = det[oi + 1] + "_det"
        subprocess.run(det, capture_output=True, text=True)
        a = os.path.join(det_cmd[oi + 1], "faceon_swing_v3.csv")
        b = os.path.join(det[oi + 1], "faceon_swing_v3.csv")
        same = open(a, "rb").read() == open(b, "rb").read()
        print(f"determinism (rail ON, identical args): {'BYTE-IDENTICAL' if same else 'DIFFERS'}")


if __name__ == "__main__":
    main()
