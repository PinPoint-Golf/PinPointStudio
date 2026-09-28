#!/usr/bin/env python3
"""Does the 3-D swing panel's trace say what the video's trace says?
(docs/design/swing_3d_annotations_design.md §7.2)

The 3-D panel draws each motion-annotation trace through the fitted skeleton's joint centres and
the fitted club. This projects those same 3-D anchor paths back through the fit's own persisted
cameras (skeleton3d.camera — the camFrame() maths, as skeleton3d_grade.py ports it) and measures
them against the 2-D trace the camera TILE draws for the same element:

  element     3-D anchor (skeleton3d)            tile anchor (2-D track)
  arms        lead Hand joint (the wrist)        lead wrist kp 9/10
  spine       mid LeftArm/RightArm               shoulder midpoint kp 5, 6
  shoulders   lead Arm joint                     lead shoulder kp 5/6
  hips        mid LeftUpLeg/RightUpLeg           hip midpoint kp 11, 12
  legs        lead Foot joint (the ankle)        lead ankle kp 15/16
  head        Head joint (skull base)            ear midpoint kp 3, 4, else nose
  shaft       butt + clubLength·u (clubhead)     face-on club.synth head / DTL clubDtl head (published)
  shaftGrip   grip                               face-on club.synth grip / DTL clubDtl grip

Frames: every fitted frame from Address to Finish. Pose: the synth track, else smoothed (the
tiles' preference), linearly interpolated at the frame time; a keypoint under 0.3 confidence is
skipped. A projected (0x10) face-on head is skipped. DTL club samples are the published ones
(tier RAY/SEG/BAND), retimed by clockOffsetUs.

The 3-D anchors are JOINT CENTRES and the tile anchors are surface keypoints (design §2): the
shoulder and hip rows carry that offset. Pixels are converted to centimetres at the anchor
(px · depth / f) so the two views, with their different crops, compare.

THE DISPLAYED CLUB (design §10): the panel smooths the clubhead path (20 ms Gaussian, local
quadratic, zero-phase; unseen frames left out of every fit) and re-points the club from the fitted
butt to it at the fitted length. The rows 'shaft~' and 'shaftGrip' (unchanged) grade THAT club —
the one on screen — beside the raw 'shaft' rows. --club-smooth-ms sets the width (0 = off).

SELF-CHECK: the joint-centre keypoints (elbows, wrists, knees, ankles) are reprojected the same
way and their median printed beside the fit's own reprojMedPx — if they disagree badly, the port
of the camera model, the image sizes or the time base is wrong and nothing else here means much.

  tools/swinglab/swing3d_trace_reproject.py <run root>/full --out docs/research/data/skeleton3d/annot_reproject_20260927
"""
import argparse
import bisect
import csv
import glob
import math
import os
import statistics
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from skeleton3d_grade import cam_frame, load, project, stream_dims  # noqa: E402

PHASE_ADDRESS, PHASE_TOP, PHASE_IMPACT, PHASE_FINISH = 0, 2, 5, 7
IMPACT_BAND_US = 60000     # the clubhead rows also report |t − impact| ≤ 60 ms on their own
J = {'LeftArm': 8, 'LeftForeArm': 9, 'LeftHand': 10, 'RightArm': 13, 'RightForeArm': 14, 'RightHand': 15,
     'LeftUpLeg': 17, 'LeftLeg': 18, 'LeftFoot': 19, 'RightUpLeg': 22, 'RightLeg': 23, 'RightFoot': 24, 'Head': 5}
ELEMENTS = ['arms', 'spine', 'shoulders', 'hips', 'legs', 'head', 'shaft', 'shaftGrip']
ROWS = ELEMENTS[:6] + ['shaft', 'shaft~', 'shaft@backswing', 'shaft@downswing', 'shaft@impact', 'shaftGrip']
CLUB_SMOOTH_MS = 20.0


def smooth_heads(t_us, X, w, sigma_ms):
    """swing_annotation_mesh.cpp smoothPath(): local quadratic, Gaussian weights, per axis."""
    if sigma_ms <= 0:
        return X
    t = np.asarray(t_us, float) / 1000.0
    out = X.copy()
    for i in range(len(t)):
        d = t - t[i]
        k = (np.abs(d) <= 3 * sigma_ms) & (w > 0)
        if k.sum() < 3:
            continue
        ww = np.exp(-0.5 * (d[k] / sigma_ms) ** 2) * w[k]
        A = np.vstack([np.ones(k.sum()), d[k] / sigma_ms, (d[k] / sigma_ms) ** 2]).T
        s = np.sqrt(ww)
        out[i] = np.linalg.lstsq(A * s[:, None], X[k] * s[:, None], rcond=None)[0][0]
    return out
SELF = {7: 'LeftForeArm', 8: 'RightForeArm', 9: 'LeftHand', 10: 'RightHand', 13: 'LeftLeg', 14: 'RightLeg',
        15: 'LeftFoot', 16: 'RightFoot'}
KP_MIN = 0.3


def phases_of(a):
    out = {}
    for p in a.get('phases', []) or []:
        out[p.get('phase')] = p.get('t_us')
    return out


def pose_track(tr):
    fr = (tr or {}).get('synth') or (tr or {}).get('smoothed') or []
    return [f for f in fr if f.get('kp')], [f['t_us'] for f in fr if f.get('kp')]


def interp_kp(frames, ts, t, idx):
    """(x, y, conf) for kp idx at t — linear between the neighbours within 20 ms."""
    i = bisect.bisect_left(ts, t)
    a = frames[i - 1] if i > 0 else None
    b = frames[i] if i < len(ts) else None
    if a is None and b is None:
        return None
    if a is None or b is None or b['t_us'] == a['t_us']:
        f = a or b
        if abs(f['t_us'] - t) > 20000:
            return None
        k = f['kp']
        return k[3 * idx], k[3 * idx + 1], k[3 * idx + 2]
    if b['t_us'] - a['t_us'] > 40000:
        return None
    w = (t - a['t_us']) / (b['t_us'] - a['t_us'])
    ka, kb = a['kp'], b['kp']
    return (ka[3 * idx] * (1 - w) + kb[3 * idx] * w, ka[3 * idx + 1] * (1 - w) + kb[3 * idx + 1] * w,
            min(ka[3 * idx + 2], kb[3 * idx + 2]))


def tile_anchor(frames, ts, t, el, lead_left):
    def kp(i):
        v = interp_kp(frames, ts, t, i)
        return (v[0], v[1]) if v and v[2] >= KP_MIN else None

    def mid(a, b):
        pa, pb = kp(a), kp(b)
        return ((pa[0] + pb[0]) / 2, (pa[1] + pb[1]) / 2) if pa and pb else None
    if el == 'arms':
        return kp(9 if lead_left else 10)
    if el == 'spine':
        return mid(5, 6)
    if el == 'shoulders':
        return kp(5 if lead_left else 6)
    if el == 'hips':
        return mid(11, 12)
    if el == 'legs':
        return kp(15 if lead_left else 16)
    if el == 'head':
        return mid(3, 4) or kp(0)
    return None


def anchor3d(P, g, u, L, el, lead_left, head_s=None):
    side = 'Left' if lead_left else 'Right'
    if el == 'arms':
        return P[J[side + 'Hand']]
    if el == 'spine':
        return (P[J['LeftArm']] + P[J['RightArm']]) / 2
    if el == 'shoulders':
        return P[J[side + 'Arm']]
    if el == 'hips':
        return (P[J['LeftUpLeg']] + P[J['RightUpLeg']]) / 2
    if el == 'legs':
        return P[J[side + 'Foot']]
    if el == 'head':
        return P[J['Head']]
    if el == 'shaft':
        return g - u * 0.04 + u * L
    if el == 'shaft~':
        butt = g - u * 0.04
        d = head_s - butt
        return butt + d / np.linalg.norm(d) * L
    if el == 'shaftGrip':
        return g
    return None


def nearest(samples, t, tol=6000):
    ts = [s[0] for s in samples]
    i = bisect.bisect_left(ts, t)
    best = None
    for j in (i - 1, i):
        if 0 <= j < len(samples) and abs(samples[j][0] - t) <= tol:
            if best is None or abs(samples[j][0] - t) < abs(best[0] - t):
                best = samples[j]
    return best


def club_tracks(a):
    fo = []
    for s in (a.get('club', {}) or {}).get('synth', []) or []:
        fo.append((s['t_us'], s.get('grip'), None if (s.get('flags', 0) & 0x10) else s.get('head')))
    fo.sort(key=lambda s: s[0])
    cd = a.get('clubDtl', {}) or {}
    off = cd.get('clockOffsetUs', 0) or 0
    dtl = []
    for f in cd.get('frames', []) or []:
        if f.get('tier') not in ('RAY', 'SEG', 'BAND'):
            continue
        dtl.append((f['t_us'] - off, f.get('grip'), f.get('head')))
    dtl.sort(key=lambda s: s[0])
    return fo, dtl


def load_any(path):
    """A swinglab result.json, or a library swing directory (read through the store)."""
    if os.path.isdir(path):
        sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
        import pp_swingdoc
        return pp_swingdoc.load_swing(path)
    return load(path)


def grade(path):
    res = load_any(path)
    if not res:
        return None, 'unreadable'
    a = res.get('analysis', {})
    sk = a.get('skeleton3d') or {}
    if not sk.get('valid'):
        return None, 'no valid skeleton3d'
    dims = stream_dims(res) if res.get('source') else {
        s.get('alias'): (s.get('encoded', {}).get('width'), s.get('encoded', {}).get('height'))
        for s in res.get('streams', []) or []}

    def pick(pred):
        for k, v in dims.items():
            if k and v and v[0] and pred(k.lower()):
                return v
        return (None, None)
    fo_dims = pick(lambda k: 'face' in k)
    dtl_dims = pick(lambda k: 'dtl' in k or 'down' in k)
    ph = phases_of(a)
    t_a, t_f = ph.get(PHASE_ADDRESS), ph.get(PHASE_FINISH)
    lead_left = ((res.get('athlete') or {}).get('handedness', 'Right') != 'Left')
    L = (sk.get('grip') or {}).get('clubLengthM') or 0.95
    mirrored = bool(sk.get('foMirrored'))
    fo_club, dtl_club = club_tracks(a)
    # The displayed club: the smoothed head over the WHOLE fitted track, as the driver does it.
    F = sk.get('frames', [])
    heads = np.array([np.array(f['g']) - np.array(f['u']) * 0.04 + np.array(f['u']) * L for f in F], float)
    hw = np.array([1.0 if f.get('s', 0) > 0 else 0.0 for f in F])
    heads_s = smooth_heads([f['t'] for f in F], heads, hw, CLUB_SMOOTH_MS)
    out = {}
    selfres = {0: [], 1: []}
    for view, track, (W, H), club in ((0, a.get('pose2d'), fo_dims, fo_club), (1, a.get('poseDtl'), dtl_dims, dtl_club)):
        if not track or not W or (view == 1 and not sk.get('dtl')):
            continue
        cf = cam_frame(sk['camera'], view, W, H)
        f = cf[4]
        frames, ts = pose_track(track)
        for fi, fr in enumerate(F):
            t = fr['t']
            if t_a is not None and t < t_a:
                continue
            if t_f is not None and t > t_f:
                continue
            P = np.array(fr['p'], dtype=float).reshape(-1, 3) / 1000.0
            g, u = np.array(fr['g'], dtype=float), np.array(fr['u'], dtype=float)

            def proj(X):
                pr = project(cf, X)
                if pr is None:
                    return None, None
                x, y = pr
                if view == 0 and mirrored:
                    x = W - x
                z = (X - cf[0]) @ cf[3]
                return (x, y), z
            for el in ELEMENTS + ['shaft~']:
                X = anchor3d(P, g, u, L, el, lead_left, heads_s[fi])
                if el in ('shaft', 'shaft~', 'shaftGrip'):
                    s = nearest(club, t)
                    pt = s and (s[1] if el == 'shaftGrip' else s[2])
                else:
                    pt = tile_anchor(frames, ts, t, el, lead_left)
                if not pt:
                    continue
                pr, z = proj(X)
                if pr is None:
                    continue
                d = math.hypot(pr[0] - pt[0] * W, pr[1] - pt[1] * H)
                out.setdefault((el, view), []).append((d, d * z / f * 100.0))
                if el == 'shaft':
                    # Where along the swing the clubhead departs: address → top, top → impact,
                    # and the impact band itself.
                    t_top, t_imp = ph.get(PHASE_TOP), ph.get(PHASE_IMPACT)
                    if t_imp is not None and abs(t - t_imp) <= IMPACT_BAND_US:
                        out.setdefault(('shaft@impact', view), []).append((d, d * z / f * 100.0))
                    if t_top is not None:
                        key = 'shaft@backswing' if t <= t_top else ('shaft@downswing' if t_imp is None or t <= t_imp else None)
                        if key:
                            out.setdefault((key, view), []).append((d, d * z / f * 100.0))
            for k, jn in SELF.items():
                v = interp_kp(frames, ts, t, k)
                if not v or v[2] < KP_MIN:
                    continue
                pr, z = proj(P[J[jn]])
                if pr is not None:
                    selfres[view].append(math.hypot(pr[0] - v[0] * W, pr[1] - v[1] * H))
    diag = sk.get('diagnostics', {})
    win = [i for i, f in enumerate(F) if (t_a is None or f['t'] >= t_a) and (t_f is None or f['t'] <= t_f)]
    tw = [F[i]['t'] for i in win]
    disp = np.array([anchor3d(None, np.array(F[i]['g']), np.array(F[i]['u']), L, 'shaft~', lead_left, heads_s[i]) for i in win])
    jit = (jitter_cm(tw, heads[win]), jitter_cm(tw, disp)) if len(win) > 11 else (float('nan'), float('nan'))
    return {'cells': out, 'jitter': jit,
            'self': {v: (statistics.median(x) if x else float('nan')) for v, x in selfres.items()},
            'fit': {0: diag.get('reprojMedPxFo'), 1: diag.get('reprojMedPxDtl') if sk.get('dtl') else None},
            'dims': {0: fo_dims, 1: dtl_dims}}, None


def jitter_cm(t_us, X):
    """Frame-to-frame jitter: the rms residual of a local cubic over ±5 frames, cm."""
    t = np.asarray(t_us, float) / 1000.0
    r = []
    for i in range(len(t)):
        j = slice(max(0, i - 5), min(len(t), i + 6))
        d = t[j] - t[i]
        A = np.vstack([d ** k for k in range(4)]).T
        co = np.linalg.lstsq(A, X[j], rcond=None)[0]
        r.append(X[i] - co[0])
    return float(np.sqrt(np.mean(np.sum(np.square(r), 1))) * 100)


def pct(xs, q):
    xs = sorted(xs)
    if not xs:
        return float('nan')
    return xs[min(len(xs) - 1, int(round(q * (len(xs) - 1))))]


def main():
    global CLUB_SMOOTH_MS
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('root', nargs='?', default='', help='a skeleton3d_run.sh config directory (<id>/result.json)')
    ap.add_argument('--swings', nargs='*', help='library swing directories instead of a run root')
    ap.add_argument('--out', required=True, help='output prefix (writes .csv and .md)')
    ap.add_argument('--club-smooth-ms', type=float, default=CLUB_SMOOTH_MS, help='the display stabiliser width (0 = off)')
    args = ap.parse_args()
    CLUB_SMOOTH_MS = args.club_smooth_ms
    if args.swings:
        items = [(os.path.basename(s.rstrip('/')), s) for s in args.swings]
    else:
        items = [(os.path.basename(os.path.dirname(p)), p) for p in sorted(glob.glob(os.path.join(args.root, '*', 'result.json')))]
    rows, per = [], {}
    jitrows = []
    selfrows = []
    for sid, path in items:
        r, err = grade(path)
        if err:
            print('%s: %s' % (sid, err), file=sys.stderr)
            continue
        selfrows.append((sid, r['self'], r['fit']))
        jitrows.append((sid,) + r['jitter'])
        for (el, view), vals in sorted(r['cells'].items()):
            px = [v[0] for v in vals]
            cm = [v[1] for v in vals]
            row = {'swing': sid, 'element': el, 'view': 'faceOn' if view == 0 else 'dtl', 'n': len(vals),
                   'medPx': round(statistics.median(px), 2), 'p90Px': round(pct(px, 0.9), 2),
                   'medCm': round(statistics.median(cm), 2), 'p90Cm': round(pct(cm, 0.9), 2)}
            rows.append(row)
            per.setdefault((el, row['view']), []).append(row)
    if not rows:
        print('nothing graded', file=sys.stderr)
        return 1
    with open(args.out + '.csv', 'w', newline='') as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        w.writeheader()
        w.writerows(rows)
    lines = ['# 3-D annotation traces reprojected into the video (%d swings)' % len(selfrows), '',
             'Generated by `tools/swinglab/swing3d_trace_reproject.py` — design '
             '`docs/design/swing_3d_annotations_design.md` §7.2. Per swing: the median and p90 distance, over every '
             'fitted frame from Address to Finish, between the 3-D trace anchor projected through the fit\'s own '
             'camera and the 2-D anchor the camera tile draws. Below: the median over swings of each swing\'s '
             'median (and of its p90), with the p10–p90 spread of the per-swing medians.', '',
             '| element | view | swings | median px (p10–p90) | p90 px | median cm (p10–p90) | p90 cm |',
             '|---|---|---|---|---|---|---|']
    for el in ROWS:
        for view in ('faceOn', 'dtl'):
            rs = per.get((el, view))
            if not rs:
                continue
            mp = [r['medPx'] for r in rs]
            mc = [r['medCm'] for r in rs]
            lines.append('| %s | %s | %d | %.1f (%.1f–%.1f) | %.1f | %.1f (%.1f–%.1f) | %.1f |' % (
                el, view, len(rs), statistics.median(mp), pct(mp, 0.1), pct(mp, 0.9),
                statistics.median([r['p90Px'] for r in rs]),
                statistics.median(mc), pct(mc, 0.1), pct(mc, 0.9), statistics.median([r['p90Cm'] for r in rs])))
    lines += ['', '## The clubhead\'s frame-to-frame jitter, raw and as displayed (%g ms stabiliser)' % CLUB_SMOOTH_MS, '',
              'The rms residual of a local cubic over ±5 frames, Address → Finish, cm. `shaft~` above grades the '
              'displayed club against both trackers.', '',
              '| | raw | displayed |', '|---|---|---|',
              '| median over swings | %.2f | %.2f |' % (statistics.median([j[1] for j in jitrows]), statistics.median([j[2] for j in jitrows])),
              '| p10–p90 | %.2f–%.2f | %.2f–%.2f |' % (pct([j[1] for j in jitrows], 0.1), pct([j[1] for j in jitrows], 0.9),
                                                    pct([j[2] for j in jitrows], 0.1), pct([j[2] for j in jitrows], 0.9))]
    lines += ['', '## Self-check — joint-centre keypoints (elbows, wrists, knees, ankles) reprojected the same way', '',
              '| swing | face-on px (this tool) | face-on px (fit, all markers) | DTL px (this tool) | DTL px (fit) |',
              '|---|---|---|---|---|']
    for sid, s, fit in selfrows:
        lines.append('| %s | %.2f | %s | %.2f | %s |' % (
            sid, s.get(0, float('nan')), '%.2f' % fit[0] if fit[0] is not None else '—',
            s.get(1, float('nan')), '%.2f' % fit[1] if fit[1] is not None else '—'))
    with open(args.out + '.md', 'w') as f:
        f.write('\n'.join(lines) + '\n')
    print('\n'.join(lines))
    return 0


if __name__ == '__main__':
    sys.exit(main())
