#!/usr/bin/env python3
"""Where the two-camera skeleton's pelvis leaves its own data — Part C of ks_skeleton3d_route_design.md.

The rotation route reads the hip line's bearing off skeleton3d. Near impact the fit can coast
(skeleton_rate_k0_20261002.md §8): its pelvis stops turning while the hip keypoints it was fitted to
keep going. This measures that per frame, without knowing the cause:

  psi_fit(t)  the fitted hip line's bearing (LeftUpLeg -> RightUpLeg, world XY), unwrapped, deg
  psi_obs(t)  the bearing the two views' hip KEYPOINTS imply on their own:
              atan2(sep_dtl / r, sep_fo), the pair route's formula, r = skeleton3d's rRatio
  map         psi_obs is uncalibrated (the views are not at 90 deg), so it is mapped onto psi_fit by
              least squares over Top -> P6, where nothing is overlapping and the fit follows it
  delta(t)    psi_fit(t) - map(psi_obs(t)), deg: ~0 where the fit follows its data, growing where
              it coasts. Reported per window, with the Address -> Top stretch as an out-of-sample check.

  python3 tools/swinglab/rotation_coast_offline.py --session /mnt/swingdata/Mark-Liversedge/2026-07-04_Mark-Liversedge_Wrist_01
"""
import argparse, json, os, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE); sys.path.insert(0, os.path.join(HERE, '..'))
from pp_swingdoc import load_swing          # noqa: E402
import skeleton_rate_offline as k           # noqa: E402

PH_ADDR, PH_TOP, PH_IMPACT, PH_P6 = 0, 2, 5, 9


def kp_sep(track, W, t, a=11, b=12, conf=0.3):
    fr = track.get('smoothed') or track.get('frames') or []
    ts = np.array([f['t_us'] for f in fr], float)
    kp = np.array([f['kp'] for f in fr], float).reshape(len(fr), -1, 3)
    ok = (kp[:, a, 2] >= conf) & (kp[:, b, 2] >= conf)
    sep = (kp[:, a, 0] - kp[:, b, 0]) * W
    out = np.full(len(t), np.nan)
    if ok.sum() < 2:
        return out
    out = np.interp(t, ts[ok], sep[ok], left=np.nan, right=np.nan)
    # no bridging across a gap wider than 15 ms
    idx = np.searchsorted(ts[ok], t)
    for i in range(len(t)):
        j0, j1 = max(idx[i] - 1, 0), min(idx[i], ok.sum() - 1)
        if ts[ok][j1] - ts[ok][j0] > 15000:
            out[i] = np.nan
    return out


def analyse(swdir):
    d = load_swing(swdir)
    A = d['analysis']
    sk = k.load_skeleton(d)
    if sk is None:
        return None
    dims = {s.get('alias'): (s['encoded']['width'], s['encoded']['height']) for s in d.get('streams', []) if s.get('kind') == 'video'}
    Wf = next(v for kk, v in dims.items() if kk and kk.lower().startswith('face'))[0]
    Wd = next(v for kk, v in dims.items() if kk and not kk.lower().startswith('face'))[0]
    ph = {}
    for p in A['phases']:
        ph.setdefault(p['phase'], p['t_us'])
    t = sk['t']
    psi, ok = k.line_bearing(sk, 'LeftUpLeg', 'RightUpLeg')
    pf = np.degrees(k.unwrap_valid(psi))
    r = A['skeleton3d']['camera'].get('rRatio', 1.0)
    sf = kp_sep(A['pose2d'], Wf, t)
    sd = kp_sep(A['poseDtl'], Wd, t)
    po = np.degrees(np.arctan2(sd / r, sf))
    m = np.isfinite(po)
    po[m] = np.degrees(np.unwrap(np.radians(po[m])))
    top, p6, imp, addr = ph[PH_TOP], ph.get(PH_P6), ph[PH_IMPACT], ph[PH_ADDR]
    if p6 is None or p6 <= top:
        p6 = imp - 70000
    w = (t >= top) & (t <= p6) & np.isfinite(po) & np.isfinite(pf)
    if w.sum() < 5:
        return None
    a, b = np.polyfit(po[w], pf[w], 1)
    delta = pf - (a * po + b)
    win = lambda lo, hi: np.nanmax(np.abs(delta[(t >= lo) & (t <= hi)])) if ((t >= lo) & (t <= hi) & np.isfinite(delta)).any() else np.nan
    rows = {'swing': os.path.basename(swdir), 'gain': a,
            'fit_rms_top_p6': float(np.sqrt(np.nanmean(delta[w] ** 2))),
            'addr_top_max': win(addr + 100000, top),          # out of sample
            'p6_m40': win(p6, imp - 40000),
            'm40_imp': win(imp - 40000, imp),
            'imp_p40': win(imp, imp + 40000),
            'p40_p100': win(imp + 40000, imp + 100000)}
    rows['series'] = [((ti - imp) / 1e3, float(x)) for ti, x in zip(t, delta) if imp - 120000 <= ti <= imp + 80000 and np.isfinite(x)]
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--session', required=True)
    ap.add_argument('--show', type=int, default=0, help='print the delta series of this swing number')
    a = ap.parse_args()
    out = []
    for s in sorted(x for x in os.listdir(a.session) if x.startswith('swing_')):
        r = analyse(os.path.join(a.session, s))
        if r:
            out.append(r)
    keys = ['gain', 'fit_rms_top_p6', 'addr_top_max', 'p6_m40', 'm40_imp', 'imp_p40', 'p40_p100']
    print('swing      ' + ' '.join(f'{x:>14s}' for x in keys))
    for r in out:
        print(f"{r['swing']} " + ' '.join(f'{r[x]:14.2f}' for x in keys))
    print('median     ' + ' '.join(f'{np.nanmedian([r[x] for r in out]):14.2f}' for x in keys))
    if a.show:
        r = next(x for x in out if x['swing'] == f'swing_{a.show:04d}')
        print(' '.join(f'{t:+.0f}:{v:+.1f}' for t, v in r['series']))


if __name__ == '__main__':
    main()
