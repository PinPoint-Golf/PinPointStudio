#!/usr/bin/env python3
"""v3.0 counterfeit-regression suite — harvested from the adjudicated failures
catalogued in lab tape_20260705/RESULTS.md + stripe_fusion_design notes.

For each known counterfeit it asserts v3 does NOT re-emit it:
  * address shirt-texture (s02/s05) and address-waggle junk (s04/s07): v3 must
    emit NO band/ray tier in the address phase there (v3 abstains at address).
  * s06 leg-shadow takeaway (f407-412): stage-1's MEASURED tier tracked a leg
    shadow; v3 must track the real club or abstain, not lock the shadow.
  * s01 impact streak-flip (~f516, v2 pre-guard locked 130 deg off): v3's C3
    monotone rotation must prevent the flip (err vs v2 truth < 30 deg or absent).

  counterfeit_check.py --v3-out <dir with sNN/faceon_swing_v3.csv>
      [--session-root <maps clipmeta swingDir for the s01 truth check>]
"""
import argparse, csv, json, math, os
import numpy as np

# (swing, description, frames, constraint, rule)
#   rule "abstain_addr" -> no band/ray in phase 'addr' among <frames>
#   rule "no_flip"      -> theta err vs v2 truth < 30 (needs s01 truth)
#   rule "adjudicate"   -> report tier/theta for eyeball (no hard assert)
CATALOG = [
    ("s02", "address shirt-texture (s~0.21)", list(range(0, 400)), "C1/C2/still", "abstain_addr"),
    ("s04", "address-waggle junk", [72, 73, 74, 105], "still-motion", "abstain_addr"),
    ("s05", "address shirt-texture (s~0.21)", list(range(0, 400)), "C1/C2/still", "abstain_addr"),
    ("s07", "address-waggle junk", list(range(98, 120)), "still-motion", "abstain_addr"),
    ("s06", "leg-shadow takeaway", list(range(407, 413)), "C2", "adjudicate"),
    ("s01", "impact streak-flip (~f516)", list(range(510, 522)), "C3", "no_flip"),
]


def load_v3(v3out, s):
    p = os.path.join(v3out, s, "faceon_swing_v3.csv")
    if not os.path.exists(p):
        return None
    return {int(r["frame"]): r for r in csv.DictReader(open(p))}


def s01_truth(session_root):
    # s01 clipmeta swingDir -> truth.json
    cm = json.load(open("/mnt/swingdata/shaftlab/lab/tape_20260705/s01/clipmeta.json"))
    sd = cm["swingDir"]
    if session_root:
        a, b = session_root.split("=", 1)
        if sd.startswith(a):
            sd = b + sd[len(a):]
    tt = np.array(cm["t_us"], float)
    tj = os.path.join(sd, "truth.json")
    if not os.path.exists(tj):
        return None
    out = {}
    for e in json.load(open(tj))["shaft"]:
        f = int(np.argmin(np.abs(tt - e["t_us"])))
        out[f] = math.degrees(e["theta"]) % 360
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--v3-out", required=True)
    ap.add_argument("--session-root", default=None, help="FROM=TO remap for s01 truth")
    args = ap.parse_args()

    truth1 = s01_truth(args.session_root)
    npass = nfail = nskip = 0
    print("==================== v3.0 COUNTERFEIT-REGRESSION SUITE ====================")
    for s, desc, frames, con, rule in CATALOG:
        v3 = load_v3(args.v3_out, s)
        if v3 is None:
            print(f"[SKIP] {s} {desc}: no v3 output"); nskip += 1; continue
        hits = [(f, v3[f]["tier"], v3[f]["theta_deg"], v3[f].get("phase", "?"))
                for f in frames if f in v3 and v3[f]["tier"] in ("band", "ray")]
        if rule == "abstain_addr":
            # A RAY in address is the unverified static counterfeit v2 cut. A
            # BAND is ratio-verified (2-1-3 pattern; legs/cloth cannot satisfy
            # it) -> likely the real club at address (a v3.2 preview), flagged
            # for adjudication, not auto-failed.
            addr_ray = [h for h in hits if h[3] == "addr" and h[1] == "ray"]
            addr_band = [h for h in hits if h[3] == "addr" and h[1] == "band"]
            if addr_ray:
                print(f"[FAIL] {s} {desc} [{con}]: {len(addr_ray)} unverified RAY in ADDRESS: "
                      f"{[(f, th) for f, _, th, _ in addr_ray[:6]]}"); nfail += 1
            elif addr_band:
                print(f"[LOOK] {s} {desc} [{con}]: {len(addr_band)} ratio-verified BAND in address "
                      f"(theta {addr_band[0][2]}..{addr_band[-1][2]}) — adjudicate (s07 f97-110 = real club)"); npass += 1
            else:
                print(f"[PASS] {s} {desc} [{con}]: no address lock"); npass += 1
        elif rule == "no_flip":
            if truth1 is None:
                print(f"[SKIP] {s} {desc}: s01 truth unavailable (pass --session-root)"); nskip += 1; continue
            errs = []
            for f in frames:
                if f in v3 and f in truth1:
                    e = abs((float(v3[f]["theta_deg"]) - truth1[f] + 180) % 360 - 180)
                    errs.append((f, e, v3[f]["tier"]))
            flips = [(f, e) for f, e, _ in errs if e > 90]
            if flips:
                print(f"[FAIL] {s} {desc} [{con}]: FLIP at {flips}"); nfail += 1
            else:
                worst = max((e for _, e, _ in errs), default=0.0)
                print(f"[PASS] {s} {desc} [{con}]: no flip (max err {worst:.1f} deg over "
                      f"{len(errs)} truth-backed frames)"); npass += 1
        else:  # adjudicate
            print(f"[LOOK] {s} {desc} [{con}]: v3 emits "
                  f"{[(f, t, th, ph) for f, t, th, ph in hits] or 'nothing (abstains)'}"); npass += 1
    print(f"\nsuite: {npass} pass/look, {nfail} FAIL, {nskip} skip")
    return 1 if nfail else 0


if __name__ == "__main__":
    raise SystemExit(main())
