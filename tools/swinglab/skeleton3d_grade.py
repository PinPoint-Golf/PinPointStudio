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


def main():
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
