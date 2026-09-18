#!/usr/bin/env python3
"""Face-on trunk turn from span foreshortening, offline over the pose2 corpus.

The 2026-09-18 pass behind kinematic_sequence_design.md §12.4. From the pinned pose keypoints
(/mnt/swingdata/corpus/pose2) and each swing's phase grid it reads the hip, shoulder and elbow
spans and reports, per swing and per segment:

  * the address-window span against the downswing's own maximum (the implied turn at address —
    the reference-width finding), and the turn at the top and at impact with the maximum as
    reference;
  * the shipped pointwise unfold (acos, sign from the square-up instant) with that reference;
  * a monotone model fit  w(t) = w0·cos θ(t),  rate ≥ 0 on 16 ms knots, w0 free — the peak
    instant, peak rate, and a residual-bootstrap σ_t (fit stability, NOT truth: see §12.4 item 2).

Needs numpy + scipy (the system python3, not the HackMotion venv). ~90 s for 61 swings.

    python3 tools/swinglab/span_turn_offline.py docs/research/data/kinematic_sequence/offline_span_turn_20260918.csv
"""
import json, glob, os, sys, csv, math
import numpy as np
from scipy.ndimage import gaussian_filter1d
from scipy.optimize import least_squares

POSE = '/mnt/swingdata/corpus/pose2'
SW   = '/mnt/swingdata/corpus/swings'
SIZE = {'2026-06-11_Mark-Liversedge_Wrist_01': (720, 1024)}
DEF_SIZE = (1280, 1024)
KP = {'hip': (11, 12), 'shoulder': (5, 6), 'elbow': (7, 8)}
CONF = 0.30
DT = 0.004           # resample grid, s
SMOOTH_S = 0.008     # gaussian sigma, s
KNOT_S = 0.016
NBOOT = 24
RNG = np.random.default_rng(1)

def anchors(path):
    d = json.load(open(path)); ph = {}
    for m in d['metrics']:
        for v in m['values']: ph.setdefault(v['phase'], v['t_us'] / 1e6)
    return ph

def load_spans(pose_path, W, H):
    d = json.load(open(pose_path))['frames']
    t = np.array([f['t_us'] for f in d]) / 1e6
    kp = np.array([f['kp'] for f in d]).reshape(len(d), -1, 3)
    out = {}
    for name, (a, b) in KP.items():
        ok = (kp[:, a, 2] >= CONF) & (kp[:, b, 2] >= CONF)
        dx = (kp[:, a, 0] - kp[:, b, 0]) * W; dy = (kp[:, a, 1] - kp[:, b, 1]) * H
        out[name] = (t[ok], np.hypot(dx, dy)[ok])
    return out

def resample(t, w, t0, t1):
    g = np.arange(t0, t1, DT)
    m = (t >= t0 - 0.05) & (t <= t1 + 0.05)
    if m.sum() < 8: return g, None
    wi = np.interp(g, t[m], w[m])
    return g, gaussian_filter1d(wi, SMOOTH_S / DT)

def fit_monotone(tg, wg, sig_w, t_top):
    """theta(t) = theta0 - int r, r>=0 piecewise linear on knots; w = w0 cos theta."""
    knots = np.arange(tg[0], tg[-1] + KNOT_S, KNOT_S)
    nk = len(knots)
    wmax = wg.max()
    def unpack(p):
        w0, th0 = p[0], p[1]; r = p[2:]
        rt = np.interp(tg, knots, r)
        th = th0 - np.concatenate([[0], np.cumsum(0.5 * (rt[1:] + rt[:-1]) * np.diff(tg))])
        return w0, th, rt
    lam = 0.05
    def resid(p):
        w0, th, rt = unpack(p)
        r = p[2:]
        return np.concatenate([(wg - w0 * np.cos(th)) / sig_w, lam * np.diff(r) / (KNOT_S * 100)])
    # init: w0 slightly above max, theta0 from the first sample, constant rate to reach square at wmax
    w0i = wmax * 1.01
    th0i = math.acos(min(1.0, wg[0] / w0i))
    i_sq = int(np.argmax(wg)); t_sq = tg[i_sq]
    ri = th0i / max(t_sq - tg[0], 0.05)
    p0 = np.concatenate([[w0i, th0i], np.full(nk, ri)])
    lb = np.concatenate([[wmax * 0.995, 0.0], np.zeros(nk)])
    ub = np.concatenate([[wmax * 1.35, math.radians(100)], np.full(nk, math.radians(2000))])
    res = least_squares(resid, p0, bounds=(lb, ub), max_nfev=400)
    w0, th, rt = unpack(res.x)
    return res.x, w0, th, rt, knots

def peak_of(tg, rt, t_lo, t_hi):
    m = (tg >= t_lo) & (tg <= t_hi)
    if m.sum() < 3: return None, None
    i = np.argmax(np.where(m, rt, -1))
    return tg[i], rt[i]

def analyse(name):
    session, swing = name.split('__')
    W, H = SIZE.get(session, DEF_SIZE)
    ph = anchors(f'{SW}/{session}/{swing}/swing_phasegrid.json')
    if not all(k in ph for k in (0, 2, 5, 7)): return None
    t_addr, t_top, t_imp, t_fin = ph[0], ph[2], ph[5], ph[7]
    t_trans = min(ph.get(3, t_top), t_top)
    spans = load_spans(f'{POSE}/{name}.json', W, H)
    row = {'swing': name, 'downswing_ms': (t_imp - t_top) * 1e3}
    for seg, key in (('pelvis', 'hip'), ('thorax', 'shoulder'), ('elbows', 'elbow')):
        t, w = spans[key]
        # address reference and noise, as the shipped code forms it
        ma = (t >= t_addr) & (t <= t_addr + 0.25)
        if ma.sum() < 5: continue
        w_addr = np.median(w[ma]); sig_w = max(1.5, np.std(w[ma]))
        tg, wg = resample(t, w, t_trans - 0.08, min(t_fin, t_imp + 0.12))
        if wg is None: continue
        i_max = int(np.argmax(wg)); w_maxdown = wg[i_max]; t_sq = tg[i_max]
        w_imp = np.interp(t_imp, tg, wg); w_top = np.interp(t_top, tg, wg)
        row[f'{seg}_w_addr'] = w_addr; row[f'{seg}_sig_w'] = sig_w
        row[f'{seg}_maxdown_over_addr'] = w_maxdown / w_addr
        row[f'{seg}_addr_bias_deg'] = math.degrees(math.acos(min(1, w_addr / w_maxdown)))
        row[f'{seg}_top_deg_ref_max'] = math.degrees(math.acos(min(1, w_top / w_maxdown)))
        row[f'{seg}_imp_deg_ref_max'] = math.degrees(math.acos(min(1, w_imp / w_maxdown)))
        row[f'{seg}_square_before_impact_ms'] = (t_imp - t_sq) * 1e3
        # -- pointwise (shipped method, reference swapped to the downswing max) ---------------
        r = np.clip(wg / w_maxdown, 0, 1); th = np.arccos(r); th = np.where(tg < t_sq, th, -th)
        rate_pw = -np.gradient(th, DT)
        valid = np.sin(np.abs(th)) > math.sin(math.radians(5))
        rate_pw = np.where(valid, rate_pw, np.nan)
        m = (tg >= t_trans - 0.05) & (tg <= t_imp + 0.05) & valid
        if m.sum() > 3:
            i = np.nanargmax(np.where(m, rate_pw, np.nan))
            row[f'{seg}_pw_before_impact_ms'] = (t_imp - tg[i]) * 1e3
            row[f'{seg}_pw_peak_dps'] = math.degrees(rate_pw[i])
        # -- monotone fit --------------------------------------------------------------
        try:
            x, w0, thf, rt, knots = fit_monotone(tg, wg, sig_w, t_top)
        except Exception as e:
            print('fit failed', name, seg, e, file=sys.stderr); continue
        tp, rp = peak_of(tg, rt, t_trans - 0.05, t_imp + 0.05)
        if tp is None: continue
        row[f'{seg}_fit_w0_over_addr'] = w0 / w_addr
        row[f'{seg}_fit_top_deg'] = math.degrees(np.interp(t_top, tg, thf))
        row[f'{seg}_fit_imp_deg'] = math.degrees(np.interp(t_imp, tg, thf))
        row[f'{seg}_fit_before_impact_ms'] = (t_imp - tp) * 1e3
        row[f'{seg}_fit_peak_dps'] = math.degrees(rp)
        row[f'{seg}_fit_rms_px'] = float(np.sqrt(np.mean((wg - w0 * np.cos(thf)) ** 2)))
        # residual bootstrap for the timing sigma
        resid = wg - w0 * np.cos(thf); tps = []
        for _ in range(NBOOT):
            wb = w0 * np.cos(thf) + RNG.choice(resid, len(resid), replace=True) + RNG.normal(0, sig_w * 0.5, len(resid))
            try:
                _, _, _, rtb, _ = fit_monotone(tg, wb, sig_w, t_top)
                tb, _ = peak_of(tg, rtb, t_trans - 0.05, t_imp + 0.05)
                if tb is not None: tps.append(tb)
            except Exception: pass
        if len(tps) >= 8:
            row[f'{seg}_fit_sigma_t_ms'] = float(np.std(tps)) * 1e3
    return row

def q(xs, k): 
    xs = [x for x in xs if x is not None and not (isinstance(x, float) and math.isnan(x))]
    return np.percentile(xs, k) if xs else float('nan')

def summ(rows, key, fmt='{:.0f}'):
    xs = [r[key] for r in rows if key in r]
    if not xs: return 'n=0'
    return f'n={len(xs)} ' + fmt.format(np.median(xs)) + ' [' + fmt.format(q(xs, 25)) + ', ' + fmt.format(q(xs, 75)) + ']'

if __name__ == '__main__':
    names = sorted(os.path.basename(p)[:-5] for p in glob.glob(f'{POSE}/*.json'))
    rows = []
    for n in names:
        r = analyse(n)
        if r: rows.append(r); print('.', end='', flush=True)
    print(f'\n{len(rows)} swings')
    out = sys.argv[1] if len(sys.argv) > 1 else 'span_turn_experiment.csv'
    keys = sorted({k for r in rows for k in r}, key=lambda k: (k != 'swing', k))
    with open(out, 'w', newline='') as f:
        wr = csv.DictWriter(f, fieldnames=keys); wr.writeheader(); wr.writerows(rows)
    print('downswing top->impact ms', summ(rows, 'downswing_ms'))
    for seg in ('pelvis', 'thorax', 'elbows'):
        print(f'\n== {seg}')
        print('  span noise at address px           ', summ(rows, f'{seg}_sig_w', '{:.1f}'))
        print('  downswing max span / address span  ', summ(rows, f'{seg}_maxdown_over_addr', '{:.3f}'))
        print('  implied address turn deg           ', summ(rows, f'{seg}_addr_bias_deg'))
        print('  turn at top deg (ref=max)          ', summ(rows, f'{seg}_top_deg_ref_max'))
        print('  turn at impact deg (ref=max)       ', summ(rows, f'{seg}_imp_deg_ref_max'))
        print('  square-up before impact ms         ', summ(rows, f'{seg}_square_before_impact_ms'))
        print('  POINTWISE peak before impact ms    ', summ(rows, f'{seg}_pw_before_impact_ms'))
        print('  POINTWISE peak dps                 ', summ(rows, f'{seg}_pw_peak_dps'))
        print('  FIT w0 / address span              ', summ(rows, f'{seg}_fit_w0_over_addr', '{:.3f}'))
        print('  FIT turn at top / impact deg       ', summ(rows, f'{seg}_fit_top_deg'), '/', summ(rows, f'{seg}_fit_imp_deg'))
        print('  FIT peak before impact ms          ', summ(rows, f'{seg}_fit_before_impact_ms'))
        print('  FIT peak dps                       ', summ(rows, f'{seg}_fit_peak_dps'))
        print('  FIT sigma_t ms (bootstrap)         ', summ(rows, f'{seg}_fit_sigma_t_ms'))
        print('  FIT rms residual px                ', summ(rows, f'{seg}_fit_rms_px', '{:.2f}'))
