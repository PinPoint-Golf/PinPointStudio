#!/usr/bin/env python3
"""
skeleton3d_grade.py — grade a skeleton3d corpus pass (docs/design/swing_3d_viz_design.md §8.2).

    python3 tools/swinglab/skeleton3d_grade.py <outroot> [--csv out.csv] [--md out.md]

<outroot> is what tools/swinglab/skeleton3d_run.sh wrote: one directory per config
(full, control, visionOnly, faceOnly, the ablations), each with one directory per swing
holding result.json. Every number here is measured against something the app ALREADY
publishes by another route, or is the fit's own diagnostic — judged per swing, by count,
never as one score.

Per swing (config `full`):
  frames, ms                     the solve's size and cost
  reprojFo/Dtl                   median |px| of the joint-centre keypoints the fit reprojects
  worstMarker                    the keypoint with the largest median residual (where the misfit is)
  lenMaxDev                      the fitted length group furthest from its height prior (fraction)
  rawCv*                         the unconstrained triangulation's length CV (what rigidity corrects)
  gamma, r                       the fitted view angle and scale ratio (pair route: 75–84°, 1.12–1.25)
  pelvisRateCorr/RmsDps          the fitted pelvis turn RATE against pelvisAngularSpeed (address → impact)
  thoraxRateCorr/RmsDps          …thorax against thoraxAngularSpeed
  spineBendDiff                  address trunk inclination (sagittal) − spineForwardBend
  leadKneeDiff, trailKneeDiff    address knee flexion − lead/trailKneeFlexion
  planeDiff                      downswing plane of the fitted shaft − club3d's down plane (°)
  swaps, limitHeld, slipP90mm    the fit's flags
  faceOnlyBoneP90                (config faceOnly) body bone direction p90 against the full fit (°)
  ablation columns               body bone direction p90 of each ablation against the full fit (°)
Parity: `full` and `control` result.json must differ ONLY in analysis.skeleton3d,
analysis.versions.skeleton3d and timings.

--branch (skeleton3d_shaft_branch_design.md §6.2): the club's depth branch where the DTL is blind,
from the configs full / v1 / dtlDropThrough / dtlDropThroughV1 / faceOnly / faceOnlyV1. Per swing,
address → P8 and P8 → finish:
  oopBlind / oopSeen    |shaft out of club3d's down plane| median on DTL-blind / DTL-seen frames (°)
  mirrorNearer          blind frames whose face-on mirror is nearer that plane (count / blind)
  foErr, dtlErr         shaft image angle against the face-on / DTL tracker, median (°)
  drop*_vsFull          a dropout config: shaft vs the FULL fit on the frames DTL saw but was hidden (°)
  drop*_vsDtl           …and against the DTL tracker's own angle there (°). dtlDropThrough hides from
                        impact + 60 ms (little DTL data there); dtlDropDown from impact − 250 ms (≈ the top)
  shiftArm / shiftRest  median joint shift full vs v1 on blind frames: lead arm / everything else (cm)
  faceOnly              faceOnly vs full: shaft direction median / p90, address → P8 (°)
  ms, kept              solve time; runs the branch pass kept
  P8oop / P8oopMax      |shaft out of the down plane| at the P8 instant / worst over P8 ± 20 ms (°)
"""

import csv
import json
import math
import os
import statistics
import sys

import numpy as np

# COCO keypoint → ybot joint index (ybot_rig.h order) — the JOINT-CENTRE keypoints only. The
# shoulders (acromion) and hips (trochanters) are surface points with fitted offsets the document
# does not carry; the fit's own reprojFo/Dtl (all markers) is reported beside this.
JOINT_MARKERS = {7: 9, 8: 14, 9: 10, 10: 15, 13: 18, 14: 23, 15: 19, 16: 24}
KP_NAMES = {5: 'lSho', 6: 'rSho', 7: 'lElb', 8: 'rElb', 9: 'lWri', 10: 'rWri', 11: 'lHip', 12: 'rHip',
            13: 'lKnee', 14: 'rKnee', 15: 'lAnk', 16: 'rAnk'}
BONES = [(0, 1), (1, 2), (2, 3), (3, 4), (4, 5), (3, 7), (7, 8), (8, 9), (9, 10), (3, 12), (12, 13),
         (13, 14), (14, 15), (0, 17), (17, 18), (18, 19), (19, 20), (0, 22), (22, 23), (23, 24), (24, 25)]
PHASE_ADDRESS, PHASE_TOP, PHASE_IMPACT = 0, 2, 5


def load(path):
    try:
        with open(path) as f:
            return json.load(f)
    except Exception:
        return None


def _roll(right, down, r):
    return right * math.cos(r) + down * math.sin(r), down * math.cos(r) - right * math.sin(r)


def cam_frame(c, view, W, H):
    if view == 0:
        p = math.radians(c['pFDeg'])
        fwd = np.array([0, math.cos(p), -math.sin(p)])
        up = np.array([0, math.sin(p), math.cos(p)])
        right, down = _roll(np.array([1.0, 0, 0]), -up, math.radians(c.get('rFDeg', 0.0)))
        return np.zeros(3), right, down, fwd, c['fF'], W / 2, H / 2
    psi, p = math.radians(c['psiDDeg']), math.radians(c['pDDeg'])
    f0 = np.array([math.cos(psi), math.sin(psi), 0])
    Z = np.array([0, 0, 1.0])
    fwd = f0 * math.cos(p) - Z * math.sin(p)
    up = f0 * math.sin(p) + Z * math.cos(p)
    right, down = _roll(np.cross(f0, Z), -up, math.radians(c.get('rDDeg', 0.0)))
    return np.array(c['cD']), right, down, fwd, c['fD'], W / 2, H / 2


def project(cf, P):
    c, right, down, fwd, f, cx, cy = cf
    rel = P - c
    z = rel @ fwd
    if z < 0.05:
        return None
    return cx + f * (rel @ right) / z, cy + f * (rel @ down) / z


def kp_at(track, t):
    """Nearest pose frame within 8 ms (smoothed track where present)."""
    frames = track.get('smoothed') or track.get('frames') or []
    best, bd = None, 1e18
    for fr in frames:
        d = abs(fr['t_us'] - t)
        if d < bd:
            best, bd = fr, d
    return best if bd <= 8000 else None


_DIMS = {}


def stream_dims(res):
    """Encoded frame sizes by stream alias — from the SOURCE swing document (result.json
    carries no streams)."""
    src = res.get('source', {}).get('swingDir')
    if src in _DIMS:
        return _DIMS[src]
    dims = {}
    try:
        sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
        import pp_swingdoc
        doc = pp_swingdoc.load_swing(src)
        for s in doc.get('streams', []) or []:
            e = s.get('encoded', {})
            dims[s.get('alias')] = (e.get('width'), e.get('height'))
    except Exception as ex:
        print('warning: no stream sizes for %s (%s)' % (src, ex), file=sys.stderr)
    _DIMS[src] = dims
    return dims


def series(res, key):
    for m in res['analysis'].get('metrics', []):
        if m['key'] == key:
            return m
    return None


def phase_value(m, phase):
    if not m:
        return None
    for ps in m.get('phaseSamples', []):
        if ps.get('phase') == phase:
            return ps.get('value')
    return None


def phases(res):
    return {p['phase']: p['t_us'] for p in res['analysis'].get('phases', [])}


def frames_arrays(sk):
    t = np.array([f['t'] for f in sk['frames']], dtype=float)
    P = np.array([f['p'] for f in sk['frames']], dtype=float).reshape(len(t), -1, 3) / 1000.0
    U = np.array([f['u'] for f in sk['frames']], dtype=float)
    return t, P, U


def yaw_series(t, P, a, b):
    v = P[:, b, :] - P[:, a, :]
    ang = np.unwrap(np.arctan2(v[:, 1], v[:, 0]))
    return np.degrees(ang)


def rate_compare(t, ang, m, t0, t1):
    if not m or len(t) < 5:
        return None, None
    mt = np.array(m['t_us'], dtype=float)
    mv = np.array(m['value'], dtype=float)
    sel = (t >= t0) & (t <= t1)
    if sel.sum() < 5:
        return None, None
    rate = np.gradient(ang, t / 1e6)
    ts = t[sel]
    ours = np.abs(rate[sel])
    theirs = np.interp(ts, mt, mv)
    ok = np.isfinite(theirs)
    if ok.sum() < 5:
        return None, None
    ours, theirs = ours[ok], np.abs(theirs[ok])
    corr = float(np.corrcoef(ours, theirs)[0, 1]) if np.std(ours) > 0 and np.std(theirs) > 0 else None
    rms = float(np.sqrt(np.mean((ours - theirs) ** 2)))
    return corr, rms


def knee_flex(P, i, hip, knee, ank):
    a = P[i, hip] - P[i, knee]
    b = P[i, ank] - P[i, knee]
    c = a @ b / (np.linalg.norm(a) * np.linalg.norm(b))
    return 180.0 - math.degrees(math.acos(max(-1, min(1, c))))


def plane_incl(U):
    if len(U) < 5:
        return None
    S = U.T @ U
    w, v = np.linalg.eigh(S)
    n = v[:, 0]
    return math.degrees(math.acos(min(1.0, abs(n[2]))))


def bone_p90(Pa, Pb):
    errs = []
    n = min(len(Pa), len(Pb))
    for i in range(n):
        for a, b in BONES:
            u = Pa[i, b] - Pa[i, a]
            v = Pb[i, b] - Pb[i, a]
            nu, nv = np.linalg.norm(u), np.linalg.norm(v)
            if nu < 1e-6 or nv < 1e-6:
                continue
            errs.append(math.degrees(math.acos(max(-1, min(1, (u @ v) / (nu * nv))))))
    return float(np.percentile(errs, 90)) if errs else None


def grade_swing(root, sid, configs):
    res = load(os.path.join(root, 'full', sid, 'result.json'))
    row = {'swing': sid}
    if not res or 'skeleton3d' not in res.get('analysis', {}):
        row['status'] = 'no result'
        return row
    sk = res['analysis']['skeleton3d']
    if not sk.get('valid'):
        row['status'] = 'refused: ' + sk.get('reason', '')
        return row
    row['status'] = 'ok'
    d = sk['diagnostics']
    row['frames'] = len(sk['frames'])
    row['ms'] = d.get('ms')
    row['gamma'] = sk['camera'].get('gammaDeg')
    row['r'] = sk['camera'].get('rRatio')
    L = sk['lengths']
    dev = {k: v / sk['scale'] - 1.0 for k, v in L.items()}
    kmax = max(dev, key=lambda k: abs(dev[k]))
    row['lenMaxDev'] = '%s %+.2f' % (kmax, dev[kmax])
    for g in ('upperArm', 'forearm', 'thigh', 'shank'):
        row['rawCv_' + g] = d.get('rawLengthCv', {}).get(g)
    row['swaps'] = '%d/%d' % (d.get('nSwapFo', 0), d.get('nSwapDtl', 0))
    row['limitHeld'] = d.get('nLimitHeld')
    row['slipP90mm'] = round(d.get('footSlipP90Mm') or float('nan'), 1)

    t, P, U = frames_arrays(sk)
    dims = stream_dims(res)
    def pick(pred):
        for k, v in dims.items():
            if k and v and v[0] and pred(k.lower()):
                return v
        return (None, None)
    fo_dims = pick(lambda k: 'face' in k)
    dtl_dims = pick(lambda k: 'dtl' in k or 'down' in k)
    a = res['analysis']
    resid = {0: {}, 1: {}}
    for view, track, (W, H) in ((0, a.get('pose2d'), fo_dims), (1, a.get('poseDtl'), dtl_dims)):
        if not track or not W or (view == 1 and not sk.get('dtl')):
            continue
        cf = cam_frame(sk['camera'], view, W, H)
        for i, ti in enumerate(t):
            fr = kp_at(track, ti)
            if not fr:
                continue
            kp = fr['kp']
            for m, j in JOINT_MARKERS.items():
                if kp[3 * m + 2] < 0.3:
                    continue
                pr = project(cf, P[i, j])
                if pr is None:
                    continue
                resid[view].setdefault(m, []).append(math.hypot(pr[0] - kp[3 * m] * W, pr[1] - kp[3 * m + 1] * H))
    row['fitReprojFo'] = round(d.get('reprojMedPxFo') or float('nan'), 2)
    row['fitReprojDtl'] = round(d.get('reprojMedPxDtl') or float('nan'), 2) if sk.get('dtl') else None
    row['dtlRollDeg'] = sk['camera'].get('rDDeg')
    for view, name in ((0, 'Fo'), (1, 'Dtl')):
        allr = [x for v in resid[view].values() for x in v]
        row['jointReproj' + name] = round(statistics.median(allr), 2) if allr else None
        if resid[view]:
            worst = max(resid[view], key=lambda m: statistics.median(resid[view][m]))
            row['worst' + name] = '%s %.1f' % (KP_NAMES[worst], statistics.median(resid[view][worst]))

    ph = phases(res)
    t_addr, t_top, t_imp = ph.get(PHASE_ADDRESS), ph.get(PHASE_TOP), ph.get(PHASE_IMPACT)
    if t_addr and t_imp:
        yp = yaw_series(t, P, 22, 17)          # trail hip → lead hip
        yt = yaw_series(t, P, 13, 8)           # trail shoulder → lead shoulder
        c, r = rate_compare(t, yp, series(res, 'pelvisAngularSpeed'), t_addr, t_imp)
        row['pelvisRateCorr'], row['pelvisRateRms'] = (round(c, 2) if c is not None else None), (round(r) if r else None)
        c, r = rate_compare(t, yt, series(res, 'thoraxAngularSpeed'), t_addr, t_imp)
        row['thoraxRateCorr'], row['thoraxRateRms'] = (round(c, 2) if c is not None else None), (round(r) if r else None)
        ia = int(np.argmin(np.abs(t - t_addr)))
        # Trunk inclination in the sagittal plane: the plane perpendicular to the stance axis.
        yaw = math.radians(sk['display']['stanceYawDeg'])
        ax = np.array([math.cos(yaw), math.sin(yaw), 0])
        trunk = 0.5 * (P[ia, 8] + P[ia, 13]) - 0.5 * (P[ia, 17] + P[ia, 22])
        trunk_s = trunk - ax * (trunk @ ax)
        incl = math.degrees(math.acos(max(-1, min(1, trunk_s[2] / np.linalg.norm(trunk_s)))))
        sb = phase_value(series(res, 'spineForwardBend'), PHASE_ADDRESS)
        row['spineBendDiff'] = round(incl - sb, 1) if sb is not None else None
        lead_left = True
        lk = knee_flex(P, ia, 17, 18, 19) if lead_left else knee_flex(P, ia, 22, 23, 24)
        tk = knee_flex(P, ia, 22, 23, 24) if lead_left else knee_flex(P, ia, 17, 18, 19)
        v = phase_value(series(res, 'leadKneeFlexion'), PHASE_ADDRESS)
        row['leadKneeDiff'] = round(lk - v, 1) if v is not None else None
        v = phase_value(series(res, 'trailKneeFlexion'), PHASE_ADDRESS)
        row['trailKneeDiff'] = round(tk - v, 1) if v is not None else None
    down = a.get('club3d', {}).get('planes', {}).get('down', {})
    if t_top and t_imp and down.get('fitted'):
        sel = (t >= t_top) & (t <= t_imp + 20000)
        inc = plane_incl(U[sel])
        row['planeDiff'] = round(inc - down['inclDeg'], 1) if inc is not None else None

    for cfg in configs:
        if cfg in ('full', 'control'):
            continue
        other = load(os.path.join(root, cfg, sid, 'result.json'))
        if not other or not other['analysis'].get('skeleton3d', {}).get('valid'):
            row['abl_' + cfg] = None
            continue
        _, Po, _ = frames_arrays(other['analysis']['skeleton3d'])
        p = bone_p90(P, Po)
        row['abl_' + cfg] = round(p, 1) if p is not None else None

    ctrl = load(os.path.join(root, 'control', sid, 'result.json'))
    if ctrl:
        row['parity'] = parity(res, ctrl)
    return row


def strip(doc):
    doc = json.loads(json.dumps(doc))
    a = doc.get('analysis', {})
    a.pop('skeleton3d', None)
    a.get('versions', {}).pop('skeleton3d', None)
    a.pop('timings', None)
    doc.pop('runmeta', None)
    return doc


def parity(a, b):
    sa, sb = strip(a), strip(b)
    if sa == sb:
        return 'identical'
    diffs = []
    for k in set(sa.get('analysis', {})) | set(sb.get('analysis', {})):
        if sa['analysis'].get(k) != sb['analysis'].get(k):
            diffs.append('analysis.' + k)
    for k in set(sa) | set(sb):
        if k != 'analysis' and sa.get(k) != sb.get(k):
            diffs.append(k)
    return 'DIFFERS: ' + ','.join(sorted(diffs))


BRANCH_CONFIGS = ('full', 'v1', 'dtlDropThrough', 'dtlDropThroughV1', 'dtlDropDown', 'dtlDropDownV1', 'faceOnly', 'faceOnlyV1')
DROP_CONFIGS = (('drop', 'dtlDropThrough'), ('dropV1', 'dtlDropThroughV1'), ('dropDown', 'dtlDropDown'), ('dropDownV1', 'dtlDropDownV1'))
PHASE_P8, PHASE_FINISH = 14, 7
LEAD_ARM = (8, 9, 10)            # LeftArm, LeftForeArm, LeftHand (a right-handed golfer)


def _img_angle(cf, a, b, mirror_w=None):
    pa, pb = project(cf, a), project(cf, b)
    if pa is None or pb is None:
        return None
    ax, bx = pa[0], pb[0]
    if mirror_w:
        ax, bx = mirror_w - ax, mirror_w - bx
    return math.degrees(math.atan2(pb[1] - pa[1], bx - ax))


def _wrap(d):
    return (d + 180.0) % 360.0 - 180.0


def _near(ts, arr, t, tol=6000):
    import bisect
    j = bisect.bisect_left(ts, t)
    best = None
    for k in (j - 1, j):
        if 0 <= k < len(ts) and abs(ts[k] - t) <= tol and (best is None or abs(ts[k] - t) < abs(ts[best] - t)):
            best = k
    return None if best is None else arr[best][1]     # arr holds (t, sample) pairs


def _med(v):
    v = [x for x in v if x is not None and not math.isnan(x)]
    return round(statistics.median(v), 1) if v else None


def _p90(v):
    v = sorted(x for x in v if x is not None and not math.isnan(x))
    return round(v[min(len(v) - 1, int(round(0.9 * (len(v) - 1))))], 1) if v else None


def grade_branch_swing(root, sid):
    R = {c: load(os.path.join(root, c, sid, 'result.json')) for c in BRANCH_CONFIGS}
    full = R['full']
    row = {'swing': sid.replace('_Mark-Liversedge_Wrist_01__swing_', ' s')}
    if not full or not full['analysis'].get('skeleton3d', {}).get('valid'):
        row['note'] = 'no full fit'
        return row
    a = full['analysis']
    ph = phases(full)
    t_a, t_imp, t_p8, t_fin = ph.get(0), ph.get(5), ph.get(PHASE_P8), ph.get(PHASE_FINISH)
    if t_p8 is None and t_imp is not None:
        t_p8 = t_imp + 100000
    down = (a.get('club3d', {}).get('planes', {}) or {}).get('down', {}) or {}
    n = np.array(down['normal']) if down.get('offered') and down.get('normal') else None
    dims = stream_dims(full)
    fo_dims = next((v for k, v in dims.items() if k and v and v[0] and 'face' in k.lower()), (None, None))
    dtl_dims = next((v for k, v in dims.items() if k and v and v[0] and ('dtl' in k.lower() or 'down' in k.lower())), (None, None))
    cd = a.get('clubDtl', {}) or {}
    off = cd.get('clockOffsetUs', 0) or 0
    dtl_pub = sorted((f['t_us'] - off, f) for f in cd.get('frames', []) or []
                     if f.get('tier') in ('RAY', 'SEG', 'BAND') and f.get('grip') and f.get('head'))
    dts = [x[0] for x in dtl_pub]
    syn = sorted((s_['t_us'], s_) for s_ in (a.get('club', {}) or {}).get('synth', []) or [] if s_.get('grip') and s_.get('head'))
    sts = [x[0] for x in syn]

    def club(res):
        sk = res['analysis']['skeleton3d']
        L = (sk.get('grip') or {}).get('clubLengthM') or 0.95
        F = sk['frames']
        t = [f['t'] for f in F]
        U = [np.array(f['u']) for f in F]
        G = [np.array(f['g']) for f in F]
        P = [np.array(f['p']).reshape(-1, 3) / 1000.0 for f in F]
        return sk, L, t, U, G, P

    def seg(t):
        if t_a is not None and t < t_a:
            return None
        if t_p8 is not None and t <= t_p8:
            return 'toP8'
        if t_fin is None or t <= t_fin:
            return 'after'
        return None

    def per_config(res, tag):
        if not res or not res['analysis'].get('skeleton3d', {}).get('valid'):
            return
        sk, L, T_, U, G, _ = club(res)
        cam = sk['camera']
        cff = cam_frame(cam, 0, *fo_dims) if fo_dims[0] else None
        cfd = cam_frame(cam, 1, *dtl_dims) if dtl_dims[0] and sk.get('dtl') else None
        mir_w = fo_dims[0] if sk.get('foMirrored') else None
        acc = {}
        for i, t in enumerate(T_):
            sgm = seg(t)
            if not sgm:
                continue
            u, g = U[i], G[i]
            butt, head = g - u * 0.04, g - u * 0.04 + u * L
            seen = _near(dts, dtl_pub, t, 8000) is not None
            if n is not None:
                o = abs(math.degrees(math.asin(max(-1.0, min(1.0, float(u @ n))))))
                acc.setdefault((sgm, 'oopSeen' if seen else 'oopBlind'), []).append(o)
                if not seen:
                    r = (g + u * (L / 2)) / np.linalg.norm(g + u * (L / 2))
                    m = u - 2 * (u @ r) * r
                    om = abs(math.degrees(math.asin(max(-1.0, min(1.0, float(m @ n) / np.linalg.norm(m))))))
                    acc.setdefault((sgm, 'mirrorNearer'), []).append(1.0 if om < o else 0.0)
            s_ = _near(sts, syn, t)
            if s_ and cff:
                af = _img_angle(cff, butt, head, mir_w)
                tf = math.degrees(math.atan2((s_['head'][1] - s_['grip'][1]) * fo_dims[1], (s_['head'][0] - s_['grip'][0]) * fo_dims[0]))
                if af is not None:
                    acc.setdefault((sgm, 'foErr'), []).append(abs(_wrap(af - tf)))
            d_ = _near(dts, dtl_pub, t)
            if d_ and cfd:
                ad = _img_angle(cfd, butt, head)
                td = math.degrees(math.atan2((d_['head'][1] - d_['grip'][1]) * dtl_dims[1], (d_['head'][0] - d_['grip'][0]) * dtl_dims[0]))
                if ad is not None:
                    acc.setdefault((sgm, 'dtlErr'), []).append(abs(_wrap(ad - td)))
        for (sgm, k), v in acc.items():
            if k == 'mirrorNearer':
                row['%s_%s_%s' % (tag, sgm, k)] = '%d/%d' % (int(sum(v)), len(v))
            else:
                row['%s_%s_%s' % (tag, sgm, k)] = _med(v)
        diag = sk.get('diagnostics', {})
        row[tag + '_ms'] = round(diag.get('ms') or float('nan'))
        if tag == 'full':
            pl = diag.get('plane', {}) or {}
            row['kept'] = '%s/%s' % (pl.get('nBranchKept'), pl.get('nBranchRuns'))
            row['planes'] = '%s/%s' % ((pl.get('back') or {}).get('source'), (pl.get('down') or {}).get('source'))

    per_config(full, 'full')
    per_config(R['v1'], 'v1')

    # The P8 instant itself — what the panel shows last (Mark, 28 Sept: "P8 still looks like the club
    # veers wildly off plane"): |out of plane| at P8, and the worst over P8 ± 20 ms.
    if n is not None and t_p8 is not None:
        for tag, res in (('full', full), ('v1', R['v1'])):
            if not res or not res['analysis'].get('skeleton3d', {}).get('valid'):
                continue
            F = res['analysis']['skeleton3d']['frames']

            def oop_at(t):
                f = min(F, key=lambda f: abs(f['t'] - t))
                return abs(math.degrees(math.asin(max(-1.0, min(1.0, float(np.array(f['u']) @ n))))))
            row[tag + '_P8oop'] = round(oop_at(t_p8), 1)
            row[tag + '_P8oopMax'] = round(max(oop_at(t_p8 + dt) for dt in (-20000, -10000, 0, 10000, 20000)), 1)

    # The dropout: frames the DTL saw after impact + 60 ms, hidden from the fit.
    _, Lf, Tf, Uf, _, _ = club(full)
    for tag, cfg in DROP_CONFIGS:
        res = R[cfg]
        if not res or not res['analysis'].get('skeleton3d', {}).get('valid') or t_imp is None:
            continue
        try:
            with open(os.path.join(root, cfg, 'params.json')) as f:
                drop_off = int(json.load(f).get('skeleton3d.debugDropDtlShaftAfterUs', 60000))
        except Exception:
            drop_off = 60000
        sk, L, T_, U, G, _ = club(res)
        cfd = cam_frame(sk['camera'], 1, *dtl_dims) if dtl_dims[0] else None
        acc = {'toP8': ([], []), 'after': ([], [])}
        for i, t in enumerate(T_):
            sgm = seg(t)
            if not sgm or t <= t_imp + drop_off:
                continue
            d_ = _near(dts, dtl_pub, t)
            if not d_:
                continue
            j = min(range(len(Tf)), key=lambda k: abs(Tf[k] - t))
            acc[sgm][0].append(math.degrees(math.acos(max(-1.0, min(1.0, float(U[i] @ Uf[j]))))))
            if cfd:
                butt, head = G[i] - U[i] * 0.04, G[i] - U[i] * 0.04 + U[i] * L
                ad = _img_angle(cfd, butt, head)
                td = math.degrees(math.atan2((d_['head'][1] - d_['grip'][1]) * dtl_dims[1], (d_['head'][0] - d_['grip'][0]) * dtl_dims[0]))
                if ad is not None:
                    acc[sgm][1].append(abs(_wrap(ad - td)))
        for sgm, (vf, vd) in acc.items():
            row['%s_%s_vsFull' % (tag, sgm)] = _med(vf)
            row['%s_%s_vsDtl' % (tag, sgm)] = _med(vd)
            row['%s_%s_n' % (tag, sgm)] = len(vf)

    # The body outside the lead arm: full vs v1, on blind frames.
    if R['v1'] and R['v1']['analysis'].get('skeleton3d', {}).get('valid'):
        _, _, T1, _, _, P1 = club(R['v1'])
        _, _, T0, _, _, P0 = club(full)
        arm, rest = [], []
        for i, t in enumerate(T0):
            if not seg(t) or _near(dts, dtl_pub, t, 8000) is not None:
                continue
            j = min(range(len(T1)), key=lambda k: abs(T1[k] - t))
            dd = np.linalg.norm((P0[i] - P0[i][0]) - (P1[j] - P1[j][0]), axis=1) * 100
            arm += [dd[k] for k in LEAD_ARM]
            rest += [dd[k] for k in range(1, len(dd)) if k not in LEAD_ARM and k not in (11, 16)]
        row['shiftArm'] = _med(arm)
        row['shiftRest'] = _med(rest)

    # Face-on only against the two-view fit, address → P8.
    for tag, cfg in (('faceOnly', 'faceOnly'), ('faceOnlyV1', 'faceOnlyV1')):
        res = R[cfg]
        if not res or not res['analysis'].get('skeleton3d', {}).get('valid'):
            continue
        _, _, T_, U, _, _ = club(res)
        e = []
        for i, t in enumerate(T_):
            if seg(t) != 'toP8':
                continue
            j = min(range(len(Tf)), key=lambda k: abs(Tf[k] - t))
            e.append(math.degrees(math.acos(max(-1.0, min(1.0, float(U[i] @ Uf[j]))))))
        row[tag + '_med'] = _med(e)
        row[tag + '_p90'] = _p90(e)
    return row


def main_branch(root, csv_out, md_out):
    sids = sorted(d for d in os.listdir(os.path.join(root, 'full')) if os.path.isdir(os.path.join(root, 'full', d)))
    rows = [grade_branch_swing(root, s) for s in sids]
    cols = []
    for r in rows:
        for k in r:
            if k not in cols:
                cols.append(k)
    if csv_out:
        with open(csv_out, 'w', newline='') as f:
            w = csv.DictWriter(f, fieldnames=cols)
            w.writeheader()
            w.writerows(rows)

    def colmed(c):
        vals = [r[c] for r in rows if isinstance(r.get(c), (int, float)) and not math.isnan(r[c])]
        if vals:
            return round(statistics.median(vals), 1)
        fr = [r[c] for r in rows if isinstance(r.get(c), str) and '/' in r[c]]
        if fr:
            num = sum(int(x.split('/')[0]) for x in fr if x.split('/')[0].isdigit())
            den = sum(int(x.split('/')[1]) for x in fr if x.split('/')[1].isdigit())
            return '%d/%d' % (num, den)
        return ''
    summary = ['| metric | median over swings (fractions: pooled) |', '|---|---|']
    for c in cols[1:]:
        summary.append('| %s | %s |' % (c, colmed(c)))
    text = '\n'.join(summary)
    if md_out:
        with open(md_out, 'w') as f:
            f.write('# skeleton3d depth branch — corpus grade\n\n'
                    'Generated by `tools/swinglab/skeleton3d_grade.py <root> --branch` '
                    '(skeleton3d_shaft_branch_design.md §6.2); per-swing rows in the CSV.\n\n' + text + '\n')
    print(text)
    return 0


def main():
    if '--branch' in sys.argv:
        csv_out = sys.argv[sys.argv.index('--csv') + 1] if '--csv' in sys.argv else None
        md_out = sys.argv[sys.argv.index('--md') + 1] if '--md' in sys.argv else None
        return main_branch(sys.argv[1], csv_out, md_out)
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    root = sys.argv[1]
    csv_out = md_out = None
    if '--csv' in sys.argv:
        csv_out = sys.argv[sys.argv.index('--csv') + 1]
    if '--md' in sys.argv:
        md_out = sys.argv[sys.argv.index('--md') + 1]
    configs = sorted(d for d in os.listdir(root) if os.path.isdir(os.path.join(root, d)))
    sids = sorted(d for d in os.listdir(os.path.join(root, 'full')) if os.path.isdir(os.path.join(root, 'full', d)))
    rows = [grade_swing(root, s, configs) for s in sids]
    cols = []
    for r in rows:
        for k in r:
            if k not in cols:
                cols.append(k)
    if csv_out:
        with open(csv_out, 'w', newline='') as f:
            w = csv.DictWriter(f, fieldnames=cols)
            w.writeheader()
            for r in rows:
                w.writerow(r)
    lines = ['| ' + ' | '.join(cols) + ' |', '|' + '---|' * len(cols)]
    for r in rows:
        lines.append('| ' + ' | '.join('' if r.get(c) is None else str(r.get(c)) for c in cols) + ' |')
    # Column medians for the numeric columns.
    med = {}
    for c in cols:
        vals = [r[c] for r in rows if isinstance(r.get(c), (int, float)) and not (isinstance(r.get(c), float) and math.isnan(r[c]))]
        if vals:
            med[c] = round(statistics.median(vals), 2)
    lines.append('| **median** | ' + ' | '.join(str(med.get(c, '')) for c in cols[1:]) + ' |')
    text = '\n'.join(lines)
    if md_out:
        with open(md_out, 'w') as f:
            f.write(text + '\n')
    print(text)
    return 0


if __name__ == '__main__':
    sys.exit(main())
