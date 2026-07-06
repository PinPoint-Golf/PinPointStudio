#!/usr/bin/env python3
"""v3.0 corpus gate runner (STUDIO PC — whole-corpus batch, per plan section 3).

For each swing sNN under a prepped tape dir: run club_track_v3, then score theta
+ coverage against that swing's v2.0 fusion truth (truth.json in the swing dir
resolved from clipmeta.swingDir). Aggregates per-phase coverage AND accuracy
across the corpus and checks zero-flip / determinism.

  run_v3_corpus.py --lab-dir <tape_20260705> --clubs clubs.json --club "7 IRON"
      [--only s02,s05] [--swingdir-remap FROM=TO] [--out-dir /tmp/v3corpus]
      [--determinism]

Each sNN needs: faceon_swing.avi, anchors.csv, skeleton.csv, clipmeta.json.
Impact comes from swing.json capture.impactUs (resolved via clipmeta.swingDir);
v2 truth from <swingDir>/truth.json. On the Linux dev box pass e.g.
  --swingdir-remap C:/PinPointStudio/Mark-Liversedge=/mnt/swingdata/Mark-Liversedge
to reach the NAS copy of the Windows session dirs.
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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--lab-dir", required=True)
    ap.add_argument("--clubs", required=True)
    ap.add_argument("--club", required=True)
    ap.add_argument("--only", default=None, help="comma list e.g. s02,s05")
    ap.add_argument("--swingdir-remap", default=None, help="FROM=TO")
    ap.add_argument("--out-dir", default="/tmp/v3corpus")
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
        want = set(args.only.split(","))
        swings = [s for s in swings if s in want]

    agg_err = {}          # phase -> list of (err, tier)
    agg_cov = {}          # phase -> [v3_meas, total]
    agg_v2 = {}           # phase -> v2_meas
    flips = 0
    per_swing = []
    det_cmd = None
    for s in swings:
        sd = os.path.join(args.lab_dir, s)
        clip = os.path.join(sd, "faceon_swing.avi")
        cm = json.load(open(os.path.join(sd, "clipmeta.json")))
        fps = cm["fps"]; nf = len(cm["t_us"]); tt = np.array(cm["t_us"], float)
        swingdir = remap(cm["swingDir"], rules)
        sj = os.path.join(swingdir, "swing.json")
        impactUs = None
        if os.path.exists(sj):
            impactUs = json.load(open(sj)).get("capture", {}).get("impactUs")
        # impact from swing.json if present, else let club_track_v3 estimate it
        # hands-only (do NOT force nf//2 -- that defeats the phase model)
        impf = int(np.argmin(np.abs(tt - impactUs))) if impactUs else None
        outdir = os.path.join(args.out_dir, s)
        cmd = [PY, TOOL, clip, "--anchors", os.path.join(sd, "anchors.csv"),
               "--skeleton", os.path.join(sd, "skeleton.csv"),
               "--clubs", args.clubs, "--club", args.club,
               "--clipmeta", os.path.join(sd, "clipmeta.json"),
               "--fps-override", str(fps), "--out-dir", outdir]
        if impf is not None:
            cmd += ["--impact-frame", str(impf)]
        if not per_swing:                      # remember the first swing's EXACT cmd
            det_cmd = list(cmd)
        r = subprocess.run(cmd, capture_output=True, text=True)
        sys.stdout.write(f"[{s}] {r.stdout.strip().splitlines()[-2] if r.stdout else ''}\n")
        if r.returncode:
            sys.stderr.write(r.stderr[-800:]); continue

        v3_csv = os.path.join(outdir, "faceon_swing_v3.csv")
        v3 = {int(x["frame"]): x for x in csv.DictReader(open(v3_csv))}
        # coverage per phase
        cov = {}; v2cov = {}
        v2 = {}
        tj = os.path.join(swingdir, "truth.json")
        truth = json.load(open(tj))["shaft"] if os.path.exists(tj) else []
        v2meas = set()
        for e in truth:
            f = int(np.argmin(np.abs(tt - e["t_us"])))
            v2[f] = (math.degrees(e["theta"]) % 360, e["tier"])
            v2meas.add(f)
        # v2 fusion csv (all meas) if present
        fus = os.path.join(sd, "fusion", "faceon_swing_fusion.csv")
        v2all = set()
        if os.path.exists(fus):
            for x in csv.DictReader(open(fus)):
                if x["tier"] in ("band", "ray"):
                    v2all.add(int(x["frame"]))
        for f in range(nf):
            ph = phase_of(f, impf, fps, nf)
            agg_cov.setdefault(ph, [0, 0]); agg_cov[ph][1] += 1
            if f in v3 and v3[f]["tier"] in ("band", "ray"):
                agg_cov[ph][0] += 1
            agg_v2.setdefault(ph, 0)
            if f in v2all:
                agg_v2[ph] += 1
        # accuracy at v2 truth frames
        serrs = {}
        for f, (thT, tierT) in v2.items():
            if f not in v3:
                continue
            err = wd(float(v3[f]["theta_deg"]), thT)
            ph = phase_of(f, impf, fps, nf)
            agg_err.setdefault(ph, []).append((err, v3[f]["tier"]))
            serrs.setdefault(ph, []).append(err)
            if err > 90:
                flips += 1
        med = {p: sorted(v)[len(v) // 2] for p, v in serrs.items() if v}
        per_swing.append((s, impf, med))

    print("\n==================== v3.0 CORPUS GATE ====================")
    print("per-swing median theta err (deg) at v2 truth frames:")
    for s, impf, med in per_swing:
        print(f"  {s} impact=f{impf}: " + "  ".join(f"{p}={e:.1f}" for p, e in sorted(med.items())))
    print("\naggregate coverage (v3 meas vs v2 fusion):")
    for ph in ("addr_back", "down", "thru", "finish"):
        c = agg_cov.get(ph, [0, 1]); v2m = agg_v2.get(ph, 0)
        print(f"  {ph:10s} v3 {c[0]:4d}/{c[1]:4d} ({100*c[0]/max(c[1],1):3.0f}%)   "
              f"v2 {v2m:4d}/{c[1]:4d} ({100*v2m/max(c[1],1):3.0f}%)")
    print("\naggregate accuracy by phase (deg):")
    for ph, v in sorted(agg_err.items()):
        errs = sorted(e for e, _ in v); n = len(errs)
        bad = sum(1 for e in errs if e > 15)
        print(f"  {ph:10s} n={n:4d} median={errs[n//2]:5.1f} p90={errs[int(n*0.9)]:5.1f} "
              f"max={errs[-1]:5.1f} bad>15={bad}")
    # by TIER (the honesty split): band/ray go to truth, pred does not
    print("\naggregate accuracy by tier (deg)  [band/ray = emitted to truth; pred = bridge only]:")
    bytier = {}
    for v in agg_err.values():
        for e, t in v:
            bytier.setdefault(t, []).append(e)
    for t in ("band", "ray", "pred"):
        errs = sorted(bytier.get(t, []))
        if not errs:
            print(f"  {t:5s}: none"); continue
        n = len(errs); bad = sum(1 for e in errs if e > 15)
        print(f"  {t:5s} n={n:4d} median={errs[n//2]:5.1f} p90={errs[int(n*0.9)]:5.1f} "
              f"max={errs[-1]:5.1f} bad>15={bad} ({100*bad/n:.0f}%)")
    print(f"\nFLIPS (err>90deg at truth frames): {flips}")

    if args.determinism and det_cmd:
        s = swings[0]
        det = list(det_cmd)                    # the FIRST swing's EXACT command
        oi = det.index("--out-dir")
        det[oi + 1] = os.path.join(args.out_dir, s + "_det")
        subprocess.run(det, capture_output=True, text=True)
        a = os.path.join(args.out_dir, s, "faceon_swing_v3.csv")
        b = os.path.join(det[oi + 1], "faceon_swing_v3.csv")
        same = open(a, "rb").read() == open(b, "rb").read()
        print(f"determinism ({s}, identical args): {'BYTE-IDENTICAL' if same else 'DIFFERS'}")


if __name__ == "__main__":
    main()
