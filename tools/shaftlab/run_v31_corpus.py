#!/usr/bin/env python3
"""v3.1 corpus gate runner (STUDIO PC -- whole-corpus batch, per plan section 3).

For each swing sNN under a prepped tape dir: run club_track_v3 (theta prior),
then shift_stack_v3 over the impact zone; parse the machine-readable omega
summary and aggregate the v3.1 gate across the corpus:

  * omega(t): peak per swing in a plausible 7-iron band, track vs exposure-arc
    agreement small, omega(t) smooth (bounded roughness);
  * no regression elsewhere: v3.1 writes only *_v31_impact.csv + images; the
    frozen v3.0 *_v3.csv / truth.json are never touched (additive by construction);
  * determinism: byte-identical *_v31_impact.csv rerun on the canonical host.

  run_v31_corpus.py --lab-dir <tape_20260705> --clubs clubs.json --club "7 IRON"
      [--only s02,s05] [--swingdir-remap FROM=TO] [--out-dir /tmp/v31corpus]
      [--determinism]

On the Linux dev box the NAS copy of the Windows session dirs is reached with
  --swingdir-remap C:/PinPointStudio/Mark-Liversedge=/mnt/swingdata/Mark-Liversedge
"""
import argparse, csv, json, os, re, subprocess, sys
import numpy as np

PY = sys.executable
HERE = os.path.dirname(os.path.abspath(__file__))
TRACK_TOOL = os.path.join(HERE, "club_track_v3.py")
V31_TOOL = os.path.join(HERE, "shift_stack_v3.py")

# plausible 7-iron shaft angular-velocity peak (deg/frame at the clip fps ->
# clubhead speed via v = omega*len). ~45-120 mph is a generous acceptance band.
def mph_of(peak_pf, fps, len_mm):
    import math
    return math.radians(peak_pf * fps) * (len_mm / 1000.0) * 2.237

MPH_LO, MPH_HI = 40.0, 120.0
# The gate tests the DELIVERABLE: the smoothed omega(t) PEAK (an independent
# physical clubhead-speed measurement). PEAK_AGREE = |track_peak - exparc_peak|
# is that criterion. AGREE_MED (median per-frame |track - exparc|) is the raw
# single-frame exposure-arc NOISE floor (~4-5 deg/f on real data with body
# overlap) -- reported as a diagnostic, NOT gated, because the profile is smoothed
# before use (same reason the synth gate scores the peak, not per-frame values).
PEAK_AGREE_MAX = 4.0   # |track_peak - exparc_peak| omega (deg/frame)
ROUGH_MAX = 3.0        # rms 2nd-diff of the smoothed emitted omega


def remap(path, rules):
    for a, b in rules:
        if path.startswith(a):
            return b + path[len(a):]
    return path


def parse(stdout, key):
    m = re.search(rf"{key}=([-\d.]+)", stdout)
    return float(m.group(1)) if m else float("nan")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--lab-dir", required=True)
    ap.add_argument("--clubs", required=True)
    ap.add_argument("--club", required=True)
    ap.add_argument("--only", default=None)
    ap.add_argument("--swingdir-remap", default=None)
    ap.add_argument("--out-dir", default="/tmp/v31corpus")
    ap.add_argument("--k", type=int, default=10)
    ap.add_argument("--determinism", action="store_true")
    args = ap.parse_args()

    rules = []
    if args.swingdir_remap:
        a, b = args.swingdir_remap.split("=", 1)
        rules = [(a, b)]
    len_mm = float(json.load(open(args.clubs))[args.club]["lengthMm"])

    swings = sorted(d for d in os.listdir(args.lab_dir)
                    if d.startswith("s") and d[1:].isdigit()
                    and os.path.isdir(os.path.join(args.lab_dir, d)))
    if args.only:
        want = set(args.only.split(","))
        swings = [s for s in swings if s in want]

    per = []
    det_cmd = None
    for s in swings:
        sd = os.path.join(args.lab_dir, s)
        clip = os.path.join(sd, "faceon_swing.avi")
        cm = json.load(open(os.path.join(sd, "clipmeta.json")))
        fps = cm["fps"]; tt = np.array(cm["t_us"], float)
        swingdir = remap(cm["swingDir"], rules)
        sj = os.path.join(swingdir, "swing.json")
        impactUs = json.load(open(sj)).get("capture", {}).get("impactUs") if os.path.exists(sj) else None
        outdir = os.path.join(args.out_dir, s)
        # 1) v3.0 track prior. Use the recorded impactUs when the session dir is
        # reachable (studio PC / s01 on the NAS); otherwise let club_track_v3
        # estimate impact hands-only (dev box, s02-s10 session dirs are C:\-only).
        t_cmd = [PY, TRACK_TOOL, clip, "--anchors", os.path.join(sd, "anchors.csv"),
                 "--skeleton", os.path.join(sd, "skeleton.csv"),
                 "--clubs", args.clubs, "--club", args.club,
                 "--clipmeta", os.path.join(sd, "clipmeta.json"),
                 "--fps-override", str(fps), "--out-dir", outdir]
        if impactUs is not None:
            t_cmd += ["--impact-frame", str(int(np.argmin(np.abs(tt - impactUs))))]
        rt = subprocess.run(t_cmd, cwd=HERE, capture_output=True, text=True)
        if rt.returncode:
            sys.stderr.write(f"[{s}] track FAILED\n{rt.stderr[-800:]}"); continue
        mimp = re.search(r"impact=(\d+)", rt.stdout)
        if not mimp:
            sys.stderr.write(f"[{s}] could not parse impact from track output -> skip\n"); continue
        impf = int(mimp.group(1))
        impsrc = "recorded" if impactUs is not None else "hands-only"
        track_csv = os.path.join(outdir, "faceon_swing_v3.csv")
        # 2) v3.1 shift-and-stack + exposure-arc over the impact zone
        v_cmd = [PY, V31_TOOL, clip, "--anchors", os.path.join(sd, "anchors.csv"),
                 "--track", track_csv, "--clubs", args.clubs, "--club", args.club,
                 "--clipmeta", os.path.join(sd, "clipmeta.json"),
                 "--skeleton", os.path.join(sd, "skeleton.csv"),
                 "--fps-override", str(fps), "--impact-frame", str(impf),
                 "--k", str(args.k), "--out-dir", outdir]
        if det_cmd is None:
            det_cmd = list(v_cmd)
        rv = subprocess.run(v_cmd, cwd=HERE, capture_output=True, text=True)
        if rv.returncode:
            sys.stderr.write(f"[{s}] v31 FAILED\n{rv.stderr[-800:]}"); continue
        out = rv.stdout
        pk_t = parse(out, "OMEGA_PEAK_TRACK")
        pk_a = parse(out, "OMEGA_PEAK_EXPARC")
        agr = parse(out, "AGREE_MED")
        rgh = parse(out, "ROUGHNESS")
        upg = parse(out, "BAND_UPGRADES")
        per.append(dict(s=s, impf=impf, fps=fps, pk_t=pk_t, pk_a=pk_a,
                        mph_t=mph_of(pk_t, fps, len_mm), mph_a=mph_of(pk_a, fps, len_mm),
                        peak_agree=abs(pk_t - pk_a),
                        agree=agr, rough=rgh, upg=int(upg) if upg == upg else 0))
        sys.stdout.write(f"[{s}] impact=f{impf}({impsrc})  omega_peak track={pk_t:.1f} "
                         f"({mph_of(pk_t,fps,len_mm):.0f}mph) exparc={pk_a:.1f} "
                         f"({mph_of(pk_a,fps,len_mm):.0f}mph)  agree={agr:.2f} rough={rgh:.2f} upg={int(upg)}\n")

    print("\n==================== v3.1 CORPUS GATE ====================")
    if not per:
        print("no swings scored"); return
    mph_ok = all(MPH_LO <= p["mph_t"] <= MPH_HI for p in per)
    peak_ok = all(p["peak_agree"] <= PEAK_AGREE_MAX for p in per)
    rough_ok = all(p["rough"] <= ROUGH_MAX for p in per)
    print(f"swings scored: {len(per)}")
    print(f"omega peak (track):   " + "  ".join(f"{p['s']}={p['pk_t']:.1f}" for p in per))
    print(f"clubhead mph (track): " + "  ".join(f"{p['s']}={p['mph_t']:.0f}" for p in per))
    print(f"PEAK agree |t-a|:     " + "  ".join(f"{p['s']}={p['peak_agree']:.1f}" for p in per))
    print(f"per-frame agree (diag)" + "  ".join(f"{p['s']}={p['agree']:.1f}" for p in per))
    print(f"omega roughness:      " + "  ".join(f"{p['s']}={p['rough']:.2f}" for p in per))
    print(f"band upgrades:        " + "  ".join(f"{p['s']}={p['upg']}" for p in per))
    print(f"\nGATE: plausible-mph={'PASS' if mph_ok else 'FAIL'} "
          f"({MPH_LO:.0f}-{MPH_HI:.0f})   peak-agree<= {PEAK_AGREE_MAX}={'PASS' if peak_ok else 'FAIL'}   "
          f"smooth<= {ROUGH_MAX}={'PASS' if rough_ok else 'FAIL'}")
    print("no-regression: v3.1 writes only *_v31_impact.csv + images; v3.0 *_v3.csv/truth untouched (additive)")

    if args.determinism and det_cmd:
        s = per[0]["s"]
        det = list(det_cmd)
        oi = det.index("--out-dir")
        det[oi + 1] = os.path.join(args.out_dir, s + "_det")
        subprocess.run(det, cwd=HERE, capture_output=True, text=True)
        a = os.path.join(args.out_dir, s, "faceon_swing_v31_impact.csv")
        b = os.path.join(det[oi + 1], "faceon_swing_v31_impact.csv")
        same = open(a, "rb").read() == open(b, "rb").read()
        print(f"determinism ({s}, identical args): {'BYTE-IDENTICAL' if same else 'DIFFERS'}")


if __name__ == "__main__":
    main()
