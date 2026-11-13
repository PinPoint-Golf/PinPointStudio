"""Per-swing extraction for the plane-stack analysis (read-only on /mnt/swingdata).

Writes one JSON line per swing to the path in argv[1]. Loads one document at a time and drops it.

Phase ids (src/Analysis/swing_analysis.h Phase): 0 P1 Address, 12 P2 ShaftParallelBack,
8 P3 MidBackswing (lead arm parallel), 2 P4 Top, 13 P5, 9 P6, 5 P7 Impact, 14 P8.

Grader sampling (src/Diagnostics/measures/measure_sample.cpp buildPhaseGrid + series_reduce.h):
  - ladder: first occurrence of each phase in analysis.phases;
  - fitted curves (swingPlaneCurve, handPlaneCurve; fitted_curve.h): linear interpolation AT the instant,
    both bracketing samples valid;
  - other series: median of VALID samples with |t - tP| <= 15 ms (inclusive), mean of the two middles on
    an even count; `valid` mask honoured only when it reaches the end of t_us (short-mask rule);
  - falling back to the series' own phaseSamples entry at that phase.
"""
import sys, os, json, math, glob, gc
import numpy as np
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "..", "tools"))
from pp_swingdoc import load_swing

ROOT = "/mnt/swingdata"
ATHLETES = ["Mark-Liversedge", "Nick-Henn", "Jess-Ratcliffe", "Chris-Marriott", "Paddy-Marriott"]
P = {"P1": 0, "P2": 12, "P3": 8, "P4": 2, "P7": 5}
W = 15000
FITTED = {"swingPlaneCurve", "handPlaneCurve"}
CONF_MIN = 0.30   # dtlPosture.confMin (src/Body/dtl_posture.h)

# ybot_rig.h joints
J_LARM, J_LHAND, J_LFORE = 8, 10, 9
J_RARM, J_RHAND, J_RFORE = 13, 15, 14
J_LUPLEG, J_RUPLEG = 17, 22

LEDGER_MEASURES = ["m_shaftPlaneBackswing", "m_backswingPlaneAngle", "m_shoulderTiltVsBend", "m_trailElbowRise",
                   "m_leadUpperArmToChest", "m_handPathLoop", "m_clubPathAtImpact", "m_spinAxis", "m_faceToPath",
                   "m_axisTiltAtTop", "m_spineBendAtAddress"]


def ladder_of(an):
    lad = {}
    for e in an.get("phases", []):
        if "phase" in e and int(e["phase"]) not in lad:
            lad[int(e["phase"])] = int(e["t_us"])
    return lad


def series_map(an):
    ms = an.get("metrics", [])
    out = {}
    for m in ms:
        k = m.get("key")
        if k and k not in out:
            out[k] = m
    return out


def grader_at(m, phase, lad):
    """Value of series m at `phase` the way the grader reads it; None when there is none."""
    if m is None:
        return None
    t = m.get("t_us") or []
    v = m.get("value") or []
    n = min(len(t), len(v))
    val = m.get("valid") or []
    mask = val if len(val) >= n and n > 0 else None
    labelled = {}
    for s in m.get("phaseSamples") or []:
        if "phase" in s and int(s["phase"]) not in labelled:
            labelled[int(s["phase"])] = (int(s.get("t_us", 0)), float(s["value"]))
    tp = lad.get(phase)
    if tp is None and phase in labelled:
        tp = labelled[phase][0]
    if tp is None:
        return None
    r = None
    if n > 0:
        if m.get("key") in FITTED:
            if t[0] <= tp <= t[n - 1]:
                import bisect
                hi = bisect.bisect_left(t, tp, 0, n)
                good = lambda i: (mask is None or mask[i]) and math.isfinite(v[i])
                if t[hi] == tp:
                    r = v[hi] if good(hi) else None
                else:
                    lo = hi - 1
                    if good(lo) and good(hi):
                        f = (tp - t[lo]) / (t[hi] - t[lo])
                        r = v[lo] + f * (v[hi] - v[lo])
        else:
            win = [v[i] for i in range(n) if abs(t[i] - tp) <= W and (mask is None or mask[i])]
            if win:
                r = float(np.median(win))
    if r is None and phase in labelled:
        r = labelled[phase][1]
    return r


def unit(a):
    nrm = np.linalg.norm(a)
    return a / nrm if nrm > 1e-9 else None


def incl_deg(d):
    """Inclination out of horizontal in the line's own vertical plane, + = end higher (world +Z up)."""
    return math.degrees(math.atan2(d[2], math.hypot(d[0], d[1])))


def sk_top(an, lad, right_handed):
    sk = an.get("skeleton3d")
    res = {"sk_present": sk is not None}
    if not sk:
        return res
    res["sk_valid"] = bool(sk.get("valid"))
    res["sk_dtl"] = bool(sk.get("dtl"))
    res["sk_calibrated"] = bool((sk.get("camera") or {}).get("calibrated"))
    tp = lad.get(2)
    if tp is None or not sk.get("valid"):
        return res
    ls, lh, ts = (J_LARM, J_LHAND, J_RARM) if right_handed else (J_RARM, J_RHAND, J_LARM)
    hips = (J_LUPLEG, J_RUPLEG)
    arm, shl, a1j, a2, nwin, nok, nok2, handtiers = [], [], [], [], 0, 0, 0, []
    for f in sk.get("frames", []):
        if abs(int(f["t"]) - tp) > W:
            continue
        nwin += 1
        tier = f["tier"]
        tr = lambda j: int(tier[j])
        handtiers.append(tr(lh))
        if min(tr(ls), tr(lh), tr(ts)) < 2:
            continue
        p = np.asarray(f["p"], dtype=float).reshape(-1, 3) / 1000.0
        dA = p[lh] - p[ls]                       # lead shoulder -> lead wrist
        dS = p[ts] - p[ls]                       # lead shoulder -> trail shoulder
        aA, aS = incl_deg(dA), incl_deg(dS)
        arm.append(aA); shl.append(aS); a1j.append(aA - aS)
        nok += 1
        if min(tr(hips[0]), tr(hips[1])) >= 2:
            spine = 0.5 * (p[J_LARM] + p[J_RARM]) - 0.5 * (p[J_LUPLEG] + p[J_RUPLEG])   # pelvis -> shoulders
            s_hat = unit(dS)
            nvec = unit(spine - np.dot(spine, s_hat) * s_hat)   # spine part square to the shoulder line
            a_hat = unit(dA)
            if nvec is not None and a_hat is not None:
                a2.append(math.degrees(math.asin(max(-1.0, min(1.0, float(np.dot(a_hat, nvec)))))))
                nok2 += 1
    res["sk_top_frames"] = nwin
    res["sk_top_ok"] = nok
    res["sk_top_handtier_min"] = min(handtiers) if handtiers else None
    if arm:
        res["arm_incl3d_P4"] = float(np.median(arm))
        res["sh_incl3d_joint_P4"] = float(np.median(shl))
        res["armlift_a1_joint"] = float(np.median(a1j))
    if a2:
        res["armlift_a2_plane"] = float(np.median(a2))
    return res


def dtl_top(d, an, lad, right_handed):
    pd = an.get("poseDtl")
    res = {"dtl_pose_present": pd is not None}
    if not pd:
        return res
    tp = lad.get(2)
    if tp is None:
        return res
    # DTL frame size: the stream the pose names, else the one aliased DTL
    streams = d.get("streams") or []
    W_, H_ = None, None
    cam = pd.get("camera")
    cand = []
    if isinstance(cam, int) and 0 <= cam < len(streams):
        cand.append(streams[cam])
    cand += [s for s in streams if "DTL" in str(s.get("alias", "")).upper()]
    for s in cand:
        enc = s.get("encoded") or {}
        if enc.get("width") and enc.get("height"):
            W_, H_ = float(enc["width"]), float(enc["height"])
            res["dtl_alias"] = s.get("alias")
            break
    if W_ is None:
        return res
    kls, ktS, klw = (5, 6, 9) if right_handed else (6, 5, 10)
    vals, confs, nwin = [], [], 0
    for f in pd.get("frames", []):
        if abs(int(f["t_us"]) - tp) > W:
            continue
        nwin += 1
        kp = f["kp"]
        g = lambda k: (kp[3 * k] * W_, kp[3 * k + 1] * H_, kp[3 * k + 2])
        L, T, Wr = g(kls), g(ktS), g(klw)
        cmin = min(L[2], T[2], Wr[2])
        confs.append((L[2], T[2], Wr[2]))
        if cmin < CONF_MIN:
            continue
        # y-up image coordinates
        s = np.array([T[0] - L[0], -(T[1] - L[1])])
        a = np.array([Wr[0] - L[0], -(Wr[1] - L[1])])
        sh, ah = unit(s), unit(a)
        if sh is None or ah is None:
            continue
        mag = math.degrees(math.acos(max(-1.0, min(1.0, float(np.dot(sh, ah))))))
        nup = np.array([-sh[1], sh[0]])
        if nup[1] < 0:
            nup = -nup
        sign = 1.0 if float(np.dot(a, nup)) >= 0 else -1.0
        vals.append(sign * mag)
    res["dtl_top_frames"] = nwin
    res["dtl_top_ok"] = len(vals)
    if confs:
        c = np.array(confs)
        res["dtl_conf_leadSh"] = float(np.median(c[:, 0]))
        res["dtl_conf_trailSh"] = float(np.median(c[:, 1]))
        res["dtl_conf_leadWr"] = float(np.median(c[:, 2]))
    if vals:
        res["armlift_b_dtl"] = float(np.median(vals))
    return res


def ledger_rows(sess_dir):
    p = os.path.join(sess_dir, "diagnostics.json")
    if not os.path.exists(p):
        return {}, {}
    d = json.load(open(p))
    dirs = {str(k): os.path.basename(v) for k, v in (d.get("session", {}).get("swingDirs") or {}).items()}
    out = {}
    for sh in d["ledger"]["shots"]:
        sid = str(sh["shotId"])
        m = {}
        for r in sh.get("rows", []):
            if r.get("state") not in ("fired", "clean"):
                continue
            items = [(r.get("drivingMeasureId", ""), r.get("value"))]
            for rd in r.get("readings") or []:
                items.append((rd.get("measureId", ""), rd.get("value")))
            for mid, val in items:
                if mid in LEDGER_MEASURES and mid not in m and val is not None:
                    m[mid] = float(val)
        m["_spinAxisShot"] = sh.get("spinAxis")
        out[sid] = m
    return out, dirs


def main(outpath):
    done = set()
    if os.path.exists(outpath):
        for line in open(outpath):
            try:
                r = json.loads(line); done.add(r["path"])
            except Exception:
                pass
    fo = open(outpath, "a")
    for ath in ATHLETES:
        for sess in sorted(glob.glob(os.path.join(ROOT, ath, "20*"))):
            if ".pinpoint-trash" in sess or not os.path.isdir(sess):
                continue
            led, dirs = ledger_rows(sess)
            dir2shot = {v: k for k, v in dirs.items()}
            for sw in sorted(glob.glob(os.path.join(sess, "swing_*"))):
                if sw in done:
                    continue
                rec = {"path": sw, "athlete": ath, "session": os.path.basename(sess),
                       "date": os.path.basename(sess)[:10], "swing": os.path.basename(sw)}
                try:
                    d = load_swing(sw)
                except Exception as e:
                    rec["error"] = repr(e)[:200]
                    fo.write(json.dumps(rec) + "\n"); fo.flush(); continue
                an = d.get("analysis") or {}
                hand = (d.get("athlete") or {}).get("handedness") or (d.get("summary") or {}).get("handedness") or "Right"
                rh = str(hand).lower().startswith("r")
                summ = d.get("summary") or {}
                rec.update({"handedness": hand, "club": summ.get("club") or (d.get("review") or {}).get("club"),
                            "excluded": bool(summ.get("excluded")),
                            "swingLength": None})
                lad = ladder_of(an)
                rec["phases_present"] = sorted(lad.keys())
                sm = series_map(an)
                spc = sm.get("swingPlaneCurve")
                for nm, ph in P.items():
                    rec[f"spc_{nm}"] = grader_at(spc, ph, lad)
                if rec["spc_P1"] is not None and rec["spc_P2"] is not None:
                    rec["takeaway_P2_P1"] = rec["spc_P2"] - rec["spc_P1"]
                if rec["spc_P1"] is not None and rec["spc_P3"] is not None:
                    rec["climb_P3_P1"] = rec["spc_P3"] - rec["spc_P1"]
                s3 = sm.get("shoulderPlaneAngle3d")
                rec["spa3d_P4"] = grader_at(s3, 2, lad)
                rec["spa3d_route"] = s3.get("routeId") if s3 else None
                stvb = sm.get("shoulderTiltVsBend")
                rec["stvb_doc"] = grader_at(stvb, 2, lad)
                rec["stvb_route"] = stvb.get("routeId") if stvb else None
                rec["spineForwardBend_P1"] = grader_at(sm.get("spineForwardBend"), 0, lad)
                rec["leadArmToTorso_P4"] = grader_at(sm.get("leadArmToTorso"), 2, lad)
                rec["shoulderPlaneAngle_P4"] = grader_at(sm.get("shoulderPlaneAngle"), 2, lad)
                rec.update(sk_top(an, lad, rh))
                if rec.get("arm_incl3d_P4") is not None and rec["spa3d_P4"] is not None:
                    rec["armlift_a1"] = rec["arm_incl3d_P4"] - rec["spa3d_P4"]
                rec.update(dtl_top(d, an, lad, rh))
                lm = d.get("launchMonitor") or {}
                rec["lm_kind"] = lm.get("kind")
                rec["lm_spinAxis_doc"] = lm.get("spinAxis")
                rec["lm_clubPath_doc"] = lm.get("clubPath")
                rec["lm_faceToPath_doc"] = lm.get("faceToPath")
                sid = dir2shot.get(os.path.basename(sw))
                rec["shotId"] = sid
                if sid is not None and sid in led:
                    for k, v in led[sid].items():
                        rec["L_" + k.lstrip("_")] = v
                del d, an, sm
                gc.collect()
                fo.write(json.dumps(rec) + "\n"); fo.flush()
                print(sw, "ok", flush=True)
    fo.close()


if __name__ == "__main__":
    main(sys.argv[1])
