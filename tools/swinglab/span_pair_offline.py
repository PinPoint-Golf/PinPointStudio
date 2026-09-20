#!/usr/bin/env python3
"""Trunk turn from the FACE-ON and DOWN-THE-LINE spans paired on one clock, offline.

Work package K0 behind kinematic_sequence_design.md §5.2 / §11 item 6 / §12.4 item 6.

WHY. Face-on reads a body line's turn from its foreshortened span, w = w0·cos θ, whose
sensitivity dw/dθ = −w0·sin θ is ZERO at square. On the 61-swing corpus that put the pelvis peak
out of sight on 45 of 61 swings and the thorax on 42 (design §12.4 item 5). A down-the-line camera
on the target-line axis sees the complement, w' = w0'·|sin θ|: maximal sensitivity at square. Two
spans on one clock lie on a centred ellipse, so

    (w/W)² + (w'/W')² = 1       fixes BOTH pixel scales with no calibration,
    θ = atan2(w'/W', w/W)       has uniform sensitivity everywhere,
    w' → 0                      MEASURES the square-up instant face-on could only infer.

THE DOWN-THE-LINE LEG IS SIGNED. w' above is written |sin θ| because a span is a distance, and the
first cut of this tool took it that way and unfolded the sign at the span's minimum, as §5.2's
formula reads. That manufactures a step of 2·w'_min in θ at one sample, and the 25 ms derivative
calls it an 800 °/s peak one grid step after the minimum on all 21 swings. What the pair needs is
the SIGNED horizontal separation (kp_a.x − kp_b.x), which passes through zero at square with no
fold, and whose zero crossing IS the square-up instant. See load_spans_dx().

WHAT THIS IS NOT. It is not a producer and it is not truth. It is the offline measurement that
decides whether a `faceOn+dtl` rung is worth building in C++, and it is written to be adversarial
about its own answer: the ellipse is fitted three ways (axis-aligned without calibration, general
so the cross term reports the effective angle between the views, and anchored on face-on's own
reference), the two views are tested for whether they see ONE rigid line turning at all
(`cons_r2`), the closure residual |(w/W)²+(w'/W')²−1| is reported per phase, and the peak times
are re-measured with W and W' moved ±3 % and ±10 %.

REUSED FROM span_turn_offline.py, deliberately, so the FACE-ON leg is the same arithmetic the
18 Sept pass ran: anchors(), load_spans() — the face-on span IS THE 2-D KEYPOINT DISTANCE in
pixels, hypot(Δx·W_frame, Δy·H_frame), not |Δx| — resample() (4 ms grid, 8 ms gaussian), the
confidence gate (0.30), the address window, and the square-up reference (the span maximum over the
downswing). The face-on leg with |Δx| instead is reported as a variant, and agrees.

PLACEMENT is the shipped C++ path re-implemented sample-for-sample from src/Analysis/angular_rate.h
and src/Analysis/segment_rates.cpp `finishChannel`: a local quadratic over a window fixed in TIME
(sequence.derivWindowMs = 25 ms), reduceExtremum's centred 40 ms windowed mean for the peak, and
σ_t = sqrt(2·σ_r/|r̈|) with the 40 ms placement threshold. The blind-band gates (sighted band,
rising edge, reversal spike) are NOT applied to the pair route — it has no blind band in principle —
but the reversal-spike condition is still evaluated and REPORTED, because §12.1 and §12.4 both
caught this estimator being confident on garbage.

    ~/.swinglab-venv/bin/python tools/swinglab/span_pair_offline.py \
        --csv docs/research/data/kinematic_sequence/pair_span_turn_20260920.csv \
        --figdir docs/research/data/kinematic_sequence/figures
"""
import argparse, csv, json, math, os, sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import span_turn_offline as sto          # noqa: E402  the FO rig; its loaders ARE the FO leg

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SW   = sto.SW
POSE = sto.POSE

S74 = '2026-07-04_Mark-Liversedge_Wrist_01'
S61 = '2026-06-11_Mark-Liversedge_Wrist_01'

# The 21 swings that have a DTL pose cache, and nothing else.
def swing_table():
    rows = []
    for i in range(4, 16):
        sw = f'swing_{i:04d}'
        run = 'fo_b' if i <= 9 else 'heldout'
        rows.append(dict(session=S74, swing=sw,
                         dtl=f'{REPO}/build/dtl/pose/{S74}__{sw}__dtl.json',
                         run=f'{REPO}/build/dtl/{run}/{S74}__{sw}/result.json'))
    for i in range(1, 10):
        sw = f'swing_{i:04d}'
        rows.append(dict(session=S61, swing=sw,
                         dtl=f'{REPO}/build/dtl/transfer/{S61}__{sw}/pose_dtl.json',
                         run=f'{REPO}/build/dtl/transfer/{S61}__{sw}/result.json'))
    return rows

# tuned::sequence:: — the shipped constants, mirrored so the numbers are comparable to the corpus runs.
DERIV_WINDOW_MS   = 25.0
MAX_PLACE_SIGMA_MS = 40.0
EXTREMUM_WINDOW_US = 40000      # tuned::reduce::kExtremumWindowUs, CENTRED (±20 ms)
MIN_AFTER_REVERSAL_MS = 60.0
SIGHTED_TURN_DEG  = 20.0
SIGMA_K           = 1.0
DT = sto.DT

SEGS = (('pelvis', 'hip'), ('thorax', 'shoulder'))
KPIDX = {'hip': (11, 12), 'shoulder': (5, 6)}       # same pairs span_turn_offline uses


# ── loaders ────────────────────────────────────────────────────────────────────────────────────

def frame_dims(session, swing):
    """(FO w,h), (DTL w,h) from swing.json streams[].encoded — never assumed."""
    d = json.load(open(f'{SW}/{session}/{swing}/swing.json'))
    fo = dtl = None
    for s in d.get('streams', []):
        if s.get('kind') != 'video':
            continue
        e = s['encoded']; a = (s.get('alias') or '').lower()
        if a.startswith('face'):
            fo = (e['width'], e['height'])
        elif a in ('dtl', 'down-the-line'):
            dtl = (e['width'], e['height'])
    return fo, dtl


def load_conf(pose_path, a, b):
    """Per-frame confidence of the two span keypoints — for the adversarial read, not the gate."""
    d = json.load(open(pose_path))['frames']
    t = np.array([f['t_us'] for f in d]) / 1e6
    kp = np.array([f['kp'] for f in d]).reshape(len(d), -1, 3)
    return t, kp[:, a, 2], kp[:, b, 2]


def load_spans_dx(pose_path, W, signed):
    """sto.load_spans with the HORIZONTAL separation instead of the 2-D distance.

    `signed=True` keeps the sign of (kp_a.x − kp_b.x). THE DTL LEG NEEDS THAT SIGN. A turn about a
    vertical axis takes the down-the-line separation THROUGH zero at square and out the other side;
    the unsigned span is its absolute value, with a V at square. Unfolding that V by flipping the
    sign at the span's MINIMUM (the obvious reading of design §5.2, and what the first cut of this
    tool did) puts a step of 2·w'_min into θ at one sample — 13° in 4 ms on a real swing — and the
    25 ms derivative reads that step as an 800 °/s peak, one grid step after the minimum, on every
    swing of the corpus. That is the §12.1 / §12.4 "confident on garbage" failure for the third
    time. The signed separation has no fold, its zero crossing IS the square-up, and it also drops
    the vertical offset that floors the unsigned shoulder span at ~14 % of W' (a head-height camera
    looking down the line sees the trail shoulder lower than the lead one, and the 2-D distance
    never goes to zero).

    The FACE-ON leg keeps sto.load_spans' 2-D distance, unchanged, so the face-on half of every
    number here is the same arithmetic the 18 Sept pass ran. `signed=False` gives |Δx| for the
    face-on variant reported as a cross-check."""
    d = json.load(open(pose_path))['frames']
    t = np.array([f['t_us'] for f in d]) / 1e6
    kp = np.array([f['kp'] for f in d]).reshape(len(d), -1, 3)
    out = {}
    for name, (a, b) in sto.KP.items():
        ok = (kp[:, a, 2] >= sto.CONF) & (kp[:, b, 2] >= sto.CONF)
        dx = (kp[:, a, 0] - kp[:, b, 0]) * W
        out[name] = (t[ok], (dx if signed else np.abs(dx))[ok])
    return out


def zero_crossing(tg, v, t_lo, t_hi):
    """The last sign change of v inside [t_lo, t_hi], linearly interpolated. NaN if it never crosses."""
    m = (tg >= t_lo) & (tg <= t_hi)
    if m.sum() < 3:
        return float('nan')
    tt, vv = tg[m], v[m]
    s = np.sign(vv)
    idx = np.where(s[:-1] * s[1:] < 0)[0]
    if len(idx) == 0:
        return float('nan')
    i = idx[-1]
    f = vv[i] / (vv[i] - vv[i + 1])
    return float(tt[i] + f * (tt[i + 1] - tt[i]))


# ── ellipse fits ───────────────────────────────────────────────────────────────────────────────

def _tukey(r):
    s = 1.4826 * np.median(np.abs(r - np.median(r))) + 1e-12
    u = np.clip(r / (4.685 * s), -1, 1)
    return (1 - u * u) ** 2


def fit_axis_aligned(wf, wd, iters=12):
    """Least squares on (w/W)² + (w'/W')² = 1, i.e. a·w² + b·w'² = 1, robust by IRLS (Tukey).

    Linear in (a, b), so it is deterministic and has no starting guess to get wrong."""
    u = wf ** 2; v = wd ** 2
    X = np.column_stack([u, v]); y = np.ones(len(u))
    wts = np.ones(len(u))
    ab = None
    for _ in range(iters):
        Xw = X * wts[:, None]
        try:
            ab, *_ = np.linalg.lstsq(Xw, y * wts, rcond=None)
        except np.linalg.LinAlgError:
            return None, None, float('nan')
        wts = _tukey(X @ ab - y)
    if ab is None or ab[0] <= 0 or ab[1] <= 0:
        return None, None, float('nan')
    W, Wd = 1.0 / math.sqrt(ab[0]), 1.0 / math.sqrt(ab[1])
    rms = float(np.sqrt(np.mean((X @ ab - y) ** 2)))
    return W, Wd, rms


def fit_general(wf, wds, iters=12):
    """A·w² + B·w·w's + C·w'²  = 1 on the SIGNED dtl span (+ before square-up, − after).

    If the two view axes are γ apart rather than 90°, w = w0·cos θ and w's = w0'·cos(θ−γ), and
    eliminating θ gives x² − 2·x·y·cos γ + y² = sin² γ. So the cross term IS the inter-view angle:
        cos γ = −B / (2·sqrt(A·C)),  w0 = 1/(sqrt(A)·sin γ),  w0' = 1/(sqrt(C)·sin γ).
    Tilt of the ellipse's axis in the (w, w') plane is ½·atan2(B, A−C); zero means orthogonal views.
    """
    X = np.column_stack([wf ** 2, wf * wds, wds ** 2]); y = np.ones(len(wf))
    wts = np.ones(len(wf)); p = None
    for _ in range(iters):
        try:
            p, *_ = np.linalg.lstsq(X * wts[:, None], y * wts, rcond=None)
        except np.linalg.LinAlgError:
            return {}
        wts = _tukey(X @ p - y)
    A, B, C = p
    out = dict(A=A, B=B, C=C, tilt_deg=math.degrees(0.5 * math.atan2(B, A - C)))
    if A > 0 and C > 0:
        cg = -B / (2 * math.sqrt(A * C))
        if abs(cg) < 0.999:
            g = math.acos(cg); sg = math.sin(g)
            out.update(gamma_deg=math.degrees(g),
                       W=1.0 / (math.sqrt(A) * sg), Wd=1.0 / (math.sqrt(C) * sg))
    return out


# ── the shipped peak path, re-implemented ──────────────────────────────────────────────────────

def local_fits(t, v, half_s):
    """Local least-squares quadratic per sample over ±half_s SECONDS (angular_rate.h fitAt).

    Returns (c0 smoothed, c1 first derivative, c2·2 = curvature, sumSq the slope lever arm)."""
    n = len(t)
    c0 = np.full(n, np.nan); c1 = np.full(n, np.nan)
    cur = np.full(n, np.nan); ss = np.full(n, np.nan)
    for i in range(n):
        m = np.abs(t - t[i]) <= half_s
        x = t[m] - t[i]; yy = v[m]
        ok = np.isfinite(yy)
        x = x[ok]; yy = yy[ok]
        if len(x) < 3:
            continue
        A = np.column_stack([np.ones_like(x), x, x * x])
        try:
            c, *_ = np.linalg.lstsq(A, yy, rcond=None)
        except np.linalg.LinAlgError:
            continue
        c0[i], c1[i], cur[i] = c[0], c[1], 2 * c[2]
        ss[i] = float(np.sum(x * x) - np.sum(x) ** 2 / len(x))
    return c0, c1, cur, ss


def windowed_mean(t, v, valid, half_s):
    """reduceExtremum's candidate: the centred ±20 ms mean over valid samples."""
    n = len(t); out = np.full(n, np.nan)
    for i in range(n):
        if not valid[i]:
            continue
        m = (np.abs(t - t[i]) <= half_s) & valid & np.isfinite(v)
        if m.sum() >= 1:
            out[i] = v[m].mean()
    return out


def place_peak(tg, rate, sig_rate, valid, t_lo, t_hi):
    """placePeak() of angular_rate.h: windowed-mean extremum, curvature σ_t, domain-width fallback."""
    dom = (tg >= t_lo) & (tg <= t_hi) & valid & np.isfinite(rate)
    if dom.sum() < 3:
        return None
    wm = windowed_mean(tg, rate, valid & np.isfinite(rate), EXTREMUM_WINDOW_US / 2e6)
    cand = np.where(dom & np.isfinite(wm), wm, -np.inf)
    i = int(np.argmax(cand))
    if not np.isfinite(cand[i]):
        return None
    half = DERIV_WINDOW_MS / 1e3          # the C++ passes windowUs as curvatureHalfUs: ±25 ms
    m = np.abs(tg - tg[i]) <= half
    x = tg[m] - tg[i]; yy = rate[m]
    ok = np.isfinite(yy); x, yy = x[ok], yy[ok]
    curv = 0.0
    if len(x) >= 3:
        c, *_ = np.linalg.lstsq(np.column_stack([np.ones_like(x), x, x * x]), yy, rcond=None)
        curv = 2 * c[2]
    sm = m & valid & np.isfinite(sig_rate) & (sig_rate > 0)
    sr = float(np.median(sig_rate[sm])) if sm.sum() else float('nan')
    domain_s = t_hi - t_lo
    if abs(curv) > 1e-12 and np.isfinite(sr) and sr >= 0:
        st = min(math.sqrt(2.0 * sr / abs(curv)), domain_s)
    else:
        st = domain_s
    # atEdge, exactly as PeakPlacement sets it: the extremum sits within one 40 ms extremum
    # window of a domain end, so the curve was still climbing where the domain stopped.
    edge_us = EXTREMUM_WINDOW_US / 1e6
    at_late = (t_hi - tg[i]) < edge_us
    at_early = (tg[i] - t_lo) < edge_us
    return dict(t=tg[i], peak=float(wm[i]), sigma_dps=sr, t_sigma_ms=st * 1e3, curv=curv,
                at_edge=bool(at_late or at_early), at_late=bool(at_late), at_early=bool(at_early))


# ── per-swing analysis ─────────────────────────────────────────────────────────────────────────

PHASE_WINDOWS = ('address', 'backswing', 'top', 'downswing', 'impact50', 'follow')


def analyse(rec, keep_series=False):
    session, swing = rec['session'], rec['swing']
    name = f'{session}__{swing}'
    (FW, FH), (DW, DH) = frame_dims(session, swing)
    ph = sto.anchors(f'{SW}/{session}/{swing}/swing_phasegrid.json')
    if not all(k in ph for k in (0, 2, 5, 7)):
        return None
    t_addr, t_top, t_imp, t_fin = ph[0], ph[2], ph[5], ph[7]
    t_take = ph.get(1, t_addr)
    t_trans = min(ph.get(3, t_top), t_top)

    fo_spans = sto.load_spans(f'{POSE}/{name}.json', FW, FH)      # the rig's own FO leg, untouched
    dt_unsig = sto.load_spans(rec['dtl'], DW, DH)                 # the folded DTL leg, for the record
    dt_spans = load_spans_dx(rec['dtl'], DW, signed=True)         # the DTL leg the pair actually uses
    fo_dx    = load_spans_dx(f'{POSE}/{name}.json', FW, signed=False)
    run = json.load(open(rec['run']))['analysis']
    ks = run.get('kinematicSequence', {})
    fo_nodes = {n['segment']: n for n in ks.get('nodes', [])}

    row = dict(swing=name, session=session, fo_frame=f'{FW}x{FH}', dtl_frame=f'{DW}x{DH}',
               downswing_ms=(t_imp - t_top) * 1e3)
    series = {}

    for seg, key in SEGS:
        tf, wf = fo_spans[key]
        td, wd = dt_spans[key]
        if len(tf) < 20 or len(td) < 20:
            continue
        g0 = max(t_addr - 0.05, tf.min() + 0.01, td.min() + 0.01)
        g1 = min(t_fin + 0.20, tf.max() - 0.01, td.max() - 0.01)
        if g1 - g0 < 0.4 or g0 > t_top or g1 < t_imp:
            row[f'{seg}_pair_state'] = 'no_dtl_coverage'
            continue
        # one clock, one grid, both views INTERPOLATED onto it (never paired by index)
        tg, wfg = sto.resample(tf, wf, g0, g1)
        _,  wdg = sto.resample(td, wd, g0, g1)
        if wfg is None or wdg is None:
            continue
        # sign convention: closed (top of the backswing) is positive, matching the face-on rig
        m_topw0 = (tg >= t_top - 0.10) & (tg <= t_top + 0.05)
        if m_topw0.sum() and np.median(wdg[m_topw0]) < 0:
            wdg = -wdg; wd = -wd

        # address-hold jitter per view — the σ_w that drives σ_θ (rig's window and floor)
        ma_f = (tf >= t_addr) & (tf <= t_addr + 0.25)
        ma_d = (td >= t_addr) & (td <= t_addr + 0.25)
        sig_f = max(1.5, float(np.std(wf[ma_f]))) if ma_f.sum() >= 5 else float('nan')
        sig_d = max(1.5, float(np.std(wd[ma_d]))) if ma_d.sum() >= 5 else float('nan')

        # ── reference widths ───────────────────────────────────────────────────────────────
        # W: the rig's own square-up maximum (FO span max over the downswing window)
        m_down = (tg >= t_trans - 0.08) & (tg <= min(t_fin, t_imp + 0.12))
        i_sq = int(np.argmax(np.where(m_down, wfg, -np.inf)))
        W_peak = float(wfg[i_sq]); t_sq_fo = float(tg[i_sq])
        # W' crude: the DTL span maximum near the top of the backswing
        m_topw = (tg >= t_top - 0.20) & (tg <= t_top + 0.10)
        i_tp = int(np.argmax(np.where(m_topw, wdg, -np.inf)))
        Wd_peak = float(wdg[i_tp])
        # ... and the same instant corrected by the FO turn there, since |sin θ_top| < 1
        sin_top = math.sqrt(max(0.0, 1.0 - min(1.0, wfg[i_tp] / W_peak) ** 2))
        Wd_peak_corr = Wd_peak / sin_top if sin_top > 0.2 else float('nan')
        # square-up MEASURED: where the SIGNED down-the-line separation crosses zero
        t_sq_dtl = zero_crossing(tg, wdg, t_top, min(g1, t_imp + 0.25))
        if not np.isfinite(t_sq_dtl):
            t_sq_dtl = float(tg[int(np.argmin(np.abs(np.where(
                (tg >= t_top) & (tg <= min(g1, t_imp + 0.25)), wdg, 1e9))))])

        # ── ellipse fits over takeaway → impact ────────────────────────────────────────────
        m_fit = (tg >= t_take) & (tg <= t_imp)
        W_fit, Wd_fit, fit_rms = fit_axis_aligned(wfg[m_fit], wdg[m_fit])
        gen = fit_general(wfg[m_fit], wdg[m_fit])

        W  = W_fit if W_fit else W_peak
        Wd = Wd_fit if Wd_fit else (Wd_peak_corr if np.isfinite(Wd_peak_corr) else Wd_peak)

        x = wfg / W; y = wdg / Wd
        # the tilt is only meaningful on the NORMALISED cloud: in raw pixels A ≈ C whenever the two
        # scales are similar, and ½·atan2(B, A−C) then reads ±45° whatever B is.
        gen_n = fit_general(x[m_fit], y[m_fit])
        closure = np.abs(x ** 2 + y ** 2 - 1.0)
        wins = dict(address=(t_addr, t_addr + 0.25), backswing=(t_take, t_top - 0.05),
                    top=(t_top - 0.05, t_top + 0.05), downswing=(t_top + 0.05, t_imp - 0.05),
                    impact50=(t_imp - 0.05, t_imp + 0.05), follow=(t_imp + 0.05, min(g1, t_fin + 0.1)))
        for wn, (a, b) in wins.items():
            m = (tg >= a) & (tg <= b)
            if m.sum() >= 3:
                row[f'{seg}_clos_{wn}_p50'] = float(np.percentile(closure[m], 50))
                row[f'{seg}_clos_{wn}_p90'] = float(np.percentile(closure[m], 90))

        # ── θ(t) ───────────────────────────────────────────────────────────────────────────
        # No unfold and no blind band: the down-the-line leg is signed, so atan2 hands back the
        # signed turn directly, continuous through square, everywhere in the window.
        def theta_of(Wa, Wb):
            xx = wfg / Wa; yy = wdg / Wb
            return np.arctan2(yy, np.clip(xx, 1e-6, None)), xx, yy               # closed +, open −

        th, _, _ = theta_of(W, Wd)
        rho2 = np.maximum(x ** 2 + y ** 2, 1e-6)
        sig_th = np.sqrt((y * sig_f / W) ** 2 + (x * sig_d / Wd) ** 2) / rho2   # rad, atan2 Jacobian

        c0, c1, curv, ss = local_fits(tg, th, DERIV_WINDOW_MS / 2e3)
        rate = -np.degrees(c1)                                                  # opening positive
        sig_rate = np.degrees(sig_th) / np.sqrt(np.maximum(ss, 1e-12))
        valid = np.isfinite(rate)

        t_ext = min(g1, t_imp + 0.15)         # the diagnostic domain: how far past impact is the peak?
        p = place_peak(tg, rate, sig_rate, valid, t_trans, t_imp)
        pe = place_peak(tg, rate, sig_rate, valid, t_trans, t_ext)
        state = 'neither'
        if p:
            row[f'{seg}_pair_before_impact_ms'] = (t_imp - p['t']) * 1e3
            row[f'{seg}_pair_sigma_t_ms'] = p['t_sigma_ms']
            row[f'{seg}_pair_peak_dps'] = p['peak']
            row[f'{seg}_pair_sigma_dps'] = p['sigma_dps']
            row[f'{seg}_pair_curv'] = p['curv']
            row[f'{seg}_pair_at_edge'] = int(p['at_edge'])
            # A PEAK ON THE DOMAIN EDGE IS NOT A PEAK (angular_rate.h PeakPlacement::atEdge,
            # design §6 "or when the peak sits at a domain edge"). The rate was still climbing
            # where the downswing domain stopped, so what the pair measured is a BOUND.
            if p['at_edge']:
                state = 'bounded'
                if p['at_late']:
                    row[f'{seg}_pair_bound_no_earlier_ms'] = (t_imp - p['t']) * 1e3
                if p['at_early']:
                    row[f'{seg}_pair_bound_no_later_ms'] = (t_imp - p['t']) * 1e3
            else:
                state = 'placed' if p['t_sigma_ms'] <= MAX_PLACE_SIGMA_MS else 'neither'
            # ADVERSARIAL: would the reversal-spike gate have refused this peak? (not applied)
            m_pre = (tg >= t_trans) & (tg <= p['t']) & valid
            npv = tg[m_pre][rate[m_pre] <= 0]
            row[f'{seg}_pair_after_reversal_ms'] = ((p['t'] - npv.max()) * 1e3) if len(npv) else float('nan')
            row[f'{seg}_pair_spikeflag'] = int(len(npv) > 0 and (p['t'] - npv.max()) * 1e3 < MIN_AFTER_REVERSAL_MS)
        row[f'{seg}_pair_state'] = state
        if pe:
            row[f'{seg}_ext_before_impact_ms'] = (t_imp - pe['t']) * 1e3
            row[f'{seg}_ext_sigma_t_ms'] = pe['t_sigma_ms']
            row[f'{seg}_ext_peak_dps'] = pe['peak']
            row[f'{seg}_ext_at_edge'] = int(pe['at_edge'])
            m_pre = (tg >= t_trans) & (tg <= pe['t']) & valid
            npv = tg[m_pre][rate[m_pre] <= 0]
            spike_e = len(npv) > 0 and (pe['t'] - npv.max()) * 1e3 < MIN_AFTER_REVERSAL_MS
            row[f'{seg}_ext_spikeflag'] = int(spike_e)
            row[f'{seg}_ext_state'] = ('bounded' if pe['at_edge'] else
                                       ('neither' if (spike_e or pe['t_sigma_ms'] > MAX_PLACE_SIGMA_MS)
                                        else 'placed'))

        # ── θ_pair against θ_faceOn where face-on can see (|θ| ≥ 20°) ─────────────────────
        rfo = np.clip(wfg / W_peak, 0, 1)
        th_fo_mag = np.arccos(rfo)
        th_fo = np.where(tg < t_sq_fo, th_fo_mag, -th_fo_mag)
        sighted = np.degrees(np.abs(th_fo)) >= SIGHTED_TURN_DEG
        m_cmp = (tg >= t_trans) & (tg <= t_imp) & sighted
        if m_cmp.sum() > 5:
            dth = np.degrees(np.abs(th[m_cmp] - th_fo[m_cmp]))
            row[f'{seg}_th_fo_vs_pair_p50_deg'] = float(np.percentile(dth, 50))
            row[f'{seg}_th_fo_vs_pair_p90_deg'] = float(np.percentile(dth, 90))
        for lbl, a2, b2 in (('late100', t_imp - 0.10, t_imp), ('early', t_trans, t_imp - 0.10)):
            m2 = (tg >= a2) & (tg <= b2)
            if m2.sum() > 3:
                row[f'{seg}_th_fo_minus_pair_{lbl}_deg'] = float(np.median(
                    np.degrees(th_fo[m2] - th[m2])))

        # ── sensitivity: W, W' moved ±3 % (and ±10 %, to find where it starts to matter) ───
        for tag, d in (('3', 0.03), ('10', 0.10)):
            dts, dts_e, dps = [], [], []
            for fW in (1 - d, 1 + d):
                for fD in (1 - d, 1 + d):
                    th2, _, _ = theta_of(W * fW, Wd * fD)
                    _, c1b, _, _ = local_fits(tg, th2, DERIV_WINDOW_MS / 2e3)
                    r2 = -np.degrees(c1b); v2 = np.isfinite(c1b)
                    p2 = place_peak(tg, r2, sig_rate, v2, t_trans, t_imp)
                    p2e = place_peak(tg, r2, sig_rate, v2, t_trans, t_ext)
                    if p and p2:
                        dts.append(abs(p2['t'] - p['t']) * 1e3)
                    if pe and p2e:
                        dts_e.append(abs(p2e['t'] - pe['t']) * 1e3)
                        dps.append(abs(p2e['peak'] - pe['peak']) / max(abs(pe['peak']), 1e-6))
            if dts:
                row[f'{seg}_sens{tag}_dt_ms_max'] = float(max(dts))
            if dts_e:
                row[f'{seg}_sens{tag}_ext_dt_ms_max'] = float(max(dts_e))
                row[f'{seg}_sens{tag}_ext_dps_rel_max'] = float(max(dps))

        # ── references and turns ──────────────────────────────────────────────────────────
        row[f'{seg}_W_peak'] = W_peak
        row[f'{seg}_Wd_peak'] = Wd_peak
        row[f'{seg}_Wd_peak_corr'] = Wd_peak_corr
        row[f'{seg}_W_fit'] = W_fit if W_fit else float('nan')
        row[f'{seg}_Wd_fit'] = Wd_fit if Wd_fit else float('nan')
        row[f'{seg}_W_fit_over_peak'] = (W_fit / W_peak) if W_fit else float('nan')
        row[f'{seg}_Wd_fit_over_peakcorr'] = (Wd_fit / Wd_peak_corr) if (W_fit and np.isfinite(Wd_peak_corr)) else float('nan')
        row[f'{seg}_fit_rms'] = fit_rms
        row[f'{seg}_tilt_deg'] = gen_n.get('tilt_deg', float('nan'))    # on the NORMALISED cloud
        row[f'{seg}_gamma_deg'] = gen.get('gamma_deg', float('nan'))
        row[f'{seg}_gen_W'] = gen.get('W', float('nan'))
        row[f'{seg}_gen_Wd'] = gen.get('Wd', float('nan'))
        row[f'{seg}_sig_w_fo_px'] = sig_f
        row[f'{seg}_sig_w_dtl_px'] = sig_d
        row[f'{seg}_t_sq_fo_before_impact_ms'] = (t_imp - t_sq_fo) * 1e3
        row[f'{seg}_t_sq_dtl_before_impact_ms'] = (t_imp - t_sq_dtl) * 1e3
        row[f'{seg}_t_sq_diff_ms'] = (t_sq_dtl - t_sq_fo) * 1e3
        # The SECOND reading of the inter-view angle, independent of the conic: if the axes are γ
        # apart the DTL span bottoms out at θ = γ − 90°, i.e. |Δt_sq| × the turn rate there.
        rate_sq = float(np.interp(0.5 * (t_sq_fo + t_sq_dtl), tg, np.nan_to_num(rate)))
        row[f'{seg}_gamma_from_tsq_deg'] = 90.0 - abs(rate_sq) * (t_sq_dtl - t_sq_fo)

        # ── THE CONSISTENCY TEST: do the two views see ONE rigid line turning? ────────────
        # Take face-on's own angle (its rig reference, W_peak) and ask the down-the-line separation
        # to be w' = W'·cos(θ_fo − γ) for SOME W' and γ. Linear in (A, B) = W'(cos γ, sin γ), so the
        # R² is a clean verdict: high with γ ≈ 90° ⇒ orthogonal views and only the reference widths
        # were ever in doubt; high with γ ≠ 90° ⇒ the second view is off-axis by that much and the
        # pair can correct for it; LOW ⇒ the two views are not measuring the same rotation and no
        # choice of scale or view angle rescues the pairing.
        m_cons = (tg >= t_top) & (tg <= min(g1, t_imp + 0.10))
        if m_cons.sum() > 20:
            thf = th_fo[m_cons]
            M = np.column_stack([np.cos(thf), np.sin(thf)])
            AB, *_ = np.linalg.lstsq(M, wdg[m_cons], rcond=None)
            pred_d = M @ AB
            ssr = float(np.sum((wdg[m_cons] - pred_d) ** 2))
            sst = float(np.sum((wdg[m_cons] - wdg[m_cons].mean()) ** 2))
            row[f'{seg}_cons_r2'] = 1.0 - ssr / sst if sst > 0 else float('nan')
            row[f'{seg}_cons_gamma_deg'] = math.degrees(math.atan2(AB[1], AB[0]))
            row[f'{seg}_cons_Wd'] = float(math.hypot(AB[0], AB[1]))
            row[f'{seg}_cons_rms_px'] = float(math.sqrt(ssr / m_cons.sum()))

        # ── ANCHORED reference: keep face-on's own W_peak, fit only W' ───────────────────
        u = (wfg[m_fit] / W_peak) ** 2; v = wdg[m_fit] ** 2
        den = float(np.sum(v * v))
        if den > 0:
            b = float(np.sum((1 - u) * v)) / den
            if b > 0:
                Wd_anch = 1.0 / math.sqrt(b)
                xa = wfg / W_peak; ya = wdg / Wd_anch
                ca = np.abs(xa ** 2 + ya ** 2 - 1.0)
                row[f'{seg}_anch_Wd'] = Wd_anch
                for wn in ('downswing', 'impact50', 'top'):
                    a2, b2 = wins[wn]
                    m2 = (tg >= a2) & (tg <= b2)
                    if m2.sum() >= 3:
                        row[f'{seg}_anch_clos_{wn}_p50'] = float(np.percentile(ca[m2], 50))
                tha = np.arctan2(ya, np.clip(xa, 1e-6, None))
                _, c1a, _, _ = local_fits(tg, tha, DERIV_WINDOW_MS / 2e3)
                pa = place_peak(tg, -np.degrees(c1a), sig_rate, np.isfinite(c1a), t_trans, t_ext)
                if pa:
                    row[f'{seg}_anch_ext_before_impact_ms'] = (t_imp - pa['t']) * 1e3
                    row[f'{seg}_anch_ext_peak_dps'] = pa['peak']

        # ── VARIANT A: the face-on leg as |Δx| instead of the 2-D distance ────────────────
        # Both legs are then the same projection (horizontal image separation), which is what the
        # ellipse model actually assumes. Reported, not headlined: the headline keeps the rig's leg.
        tfx, wfx = fo_dx[key]
        _, wfxg = sto.resample(tfx, wfx, g0, g1)
        if wfxg is not None:
            Wx, Wdx, _ = fit_axis_aligned(wfxg[m_fit], wdg[m_fit])
            if Wx and Wdx:
                cx = np.abs((wfxg / Wx) ** 2 + (wdg / Wdx) ** 2 - 1.0)
                for wn in ('downswing', 'impact50', 'top'):
                    a2, b2 = wins[wn]
                    m2 = (tg >= a2) & (tg <= b2)
                    if m2.sum() >= 3:
                        row[f'{seg}_fox_clos_{wn}_p50'] = float(np.percentile(cx[m2], 50))
                thx = np.arctan2(wdg / Wdx, np.clip(wfxg / Wx, 1e-6, None))
                _, c1x, _, _ = local_fits(tg, thx, DERIV_WINDOW_MS / 2e3)
                px = place_peak(tg, -np.degrees(c1x), sig_rate, np.isfinite(c1x), t_trans, t_ext)
                if px:
                    row[f'{seg}_fox_ext_before_impact_ms'] = (t_imp - px['t']) * 1e3
                    row[f'{seg}_fox_ext_peak_dps'] = px['peak']
                    row[f'{seg}_fox_ext_at_edge'] = int(px['at_edge'])

        # ── VARIANT B: the UNSIGNED DTL span with the fold, i.e. the bug this tool started with ──
        tdu, wdu = dt_unsig[key]
        _, wdug = sto.resample(tdu, wdu, g0, g1)
        if wdug is not None:
            Wu, Wdu, _ = fit_axis_aligned(wfg[m_fit], wdug[m_fit])
            if Wu and Wdu:
                m_squ = (tg >= t_top) & (tg <= min(g1, t_imp + 0.15))
                t_squ = float(tg[m_squ][int(np.argmin(wdug[m_squ]))])
                row[f'{seg}_fold_span_min_over_W'] = float(np.min(wdug[m_squ]) / Wdu)
                thu_m = np.arctan2(np.clip(wdug / Wdu, 0, None), np.clip(wfg / Wu, 1e-6, None))
                thu = np.where(tg < t_squ, thu_m, -thu_m)
                _, c1u, _, _ = local_fits(tg, thu, DERIV_WINDOW_MS / 2e3)
                pu = place_peak(tg, -np.degrees(c1u), sig_rate, np.isfinite(c1u), t_trans, t_ext)
                if pu:
                    row[f'{seg}_fold_ext_before_impact_ms'] = (t_imp - pu['t']) * 1e3
                    row[f'{seg}_fold_ext_peak_dps'] = pu['peak']
                    row[f'{seg}_fold_minus_tsq_ms'] = (pu['t'] - t_squ) * 1e3
        for lbl, tt in (('addr', t_addr), ('top', t_top), ('imp', t_imp)):
            row[f'{seg}_pair_turn_{lbl}_deg'] = float(np.degrees(np.interp(tt, tg, th)))
            row[f'{seg}_sig_theta_{lbl}_deg'] = float(np.degrees(np.interp(tt, tg, sig_th)))

        # ── DTL keypoint confidence, and the |sin θ| sanity check ─────────────────────────
        a, b = KPIDX[key]
        tc, ca, cb = load_conf(rec['dtl'], a, b)
        mdw = (tc >= t_top) & (tc <= t_imp)
        if mdw.sum() >= 3:
            row[f'{seg}_dtl_conf_a_p10'] = float(np.percentile(ca[mdw], 10))
            row[f'{seg}_dtl_conf_b_p10'] = float(np.percentile(cb[mdw], 10))
            row[f'{seg}_dtl_conf_min_p10'] = float(np.percentile(np.minimum(ca, cb)[mdw], 10))
        mpk = (tc >= t_imp) & (tc <= t_imp + 0.10)      # where the pair puts the trunk peak
        if mpk.sum() >= 3:
            row[f'{seg}_dtl_conf_min_p10_postimp'] = float(np.percentile(np.minimum(ca, cb)[mpk], 10))
        mfo = (tc >= t_top) & (tc <= t_imp)
        tcf, caf, cbf = load_conf(f'{POSE}/{name}.json', a, b)
        mf = (tcf >= t_top) & (tcf <= t_imp + 0.10)
        if mf.sum() >= 3:
            row[f'{seg}_fo_conf_min_p10'] = float(np.percentile(np.minimum(caf, cbf)[mf], 10))
        # does the DTL separation behave like |sin θ|? correlate |w'| with sqrt(1−(w_fo/W_peak)²),
        # which is what face-on alone would predict for it. Near 1 ⇒ the DTL leg is the complement.
        m_all = (tg >= t_take) & (tg <= t_imp)
        pred = np.sqrt(np.clip(1 - np.clip(wfg / W_peak, 0, 1) ** 2, 0, 1))
        if m_all.sum() > 20 and np.std(pred[m_all]) > 1e-6:
            row[f'{seg}_dtl_vs_sin_r'] = float(np.corrcoef(np.abs(wdg[m_all]), pred[m_all])[0, 1])
        # the far-hip question: is the DTL separation near zero where face-on is at its maximum?
        row[f'{seg}_dtl_at_fo_max_over_W'] = float(np.interp(t_sq_fo, tg, np.abs(wdg)) / Wd)
        m_sq2 = (tg >= t_top) & (tg <= min(g1, t_imp + 0.25))
        row[f'{seg}_dtl_span_min_over_W'] = float(np.min(np.abs(wdg[m_sq2])) / Wd) if m_sq2.sum() > 3 else float('nan')

        # ── the FO-only result as the shipped producer emits it ───────────────────────────
        n = fo_nodes.get(seg)
        if n:
            nb, nl = n.get('peakNoEarlierThanMs'), n.get('peakNoLaterThanMs')
            row[f'{seg}_fo_state'] = ('placed' if n.get('placed') else
                                      ('bounded' if (nb is not None or nl is not None) else 'neither'))
            row[f'{seg}_fo_before_impact_ms'] = n.get('beforeImpactMs')
            row[f'{seg}_fo_sigma_t_ms'] = n.get('tSigmaMs')
            row[f'{seg}_fo_peak_dps'] = n.get('peakDps')
            row[f'{seg}_fo_bound_no_earlier_ms'] = nb
            row[f'{seg}_fo_bound_no_later_ms'] = nl
        else:
            row[f'{seg}_fo_state'] = 'absent'

        if keep_series:
            _, c1f, _, _ = local_fits(tg, th_fo, DERIV_WINDOW_MS / 2e3)
            series[seg] = dict(tg=tg, wf=wfg, wd=wdg, x=x, y=y, th=th, th_fo=th_fo,
                               rate=rate, sig_rate=sig_rate, sig_th=sig_th, sighted=sighted,
                               rate_fo=-np.degrees(c1f), W=W, Wd=Wd, closure=closure,
                               t_sq_fo=t_sq_fo, t_sq_dtl=t_sq_dtl, peak=p, peak_ext=pe,
                               t_imp=t_imp, t_trans=t_trans, t_ext=t_ext,
                               conf=(tc, ca, cb), kp=(a, b))

    # arm and club nodes from the run result — unchanged by anything here
    for s2 in ('leadArm', 'club'):
        n = fo_nodes.get(s2)
        if n:
            row[f'{s2}_placed'] = int(bool(n.get('placed')))
            row[f'{s2}_before_impact_ms'] = n.get('beforeImpactMs')
            row[f'{s2}_sigma_t_ms'] = n.get('tSigmaMs')
            row[f'{s2}_peak_dps'] = n.get('peakDps')
    row['fo_verdict'] = ks.get('verdict', '')
    row['fo_order_resolved'] = int(bool(ks.get('orderResolved')))

    # the combined order: pair trunk + the run's arm and club. Twice — once on the sequence's own
    # domain (…, impact], once on the diagnostic domain that runs 150 ms past it, because on this
    # corpus the trunk peaks are outside the first one and the ORDER is the thing being asked about.
    canon = {'pelvis': 0, 'thorax': 1, 'leadArm': 2, 'club': 3}

    def resolve(prefix, trunk_state_key, trunk_t_key, trunk_s_key):
        nodes = []
        for seg, _ in SEGS:
            if row.get(f'{seg}_{trunk_state_key}') == 'placed':
                nodes.append((seg, row[f'{seg}_{trunk_t_key}'], row[f'{seg}_{trunk_s_key}']))
        for s2 in ('leadArm', 'club'):
            if row.get(f'{s2}_placed'):
                nodes.append((s2, row[f'{s2}_before_impact_ms'], row[f'{s2}_sigma_t_ms']))
        nodes.sort(key=lambda z: -z[1])      # ascending in time = descending before-impact
        row[f'{prefix}_n_placed'] = len(nodes)
        row[f'{prefix}_order'] = '>'.join(n[0] for n in nodes)
        resolved = len(nodes) >= 2
        for i in range(len(nodes) - 1):
            if abs(nodes[i][1] - nodes[i + 1][1]) <= SIGMA_K * math.hypot(nodes[i][2], nodes[i + 1][2]):
                resolved = False
        row[f'{prefix}_order_resolved'] = int(resolved)
        ordered = [canon[n[0]] for n in nodes]
        names = [n[0] for n in nodes]
        if len(nodes) < 2 or not resolved:
            v = 'unresolved'
        elif len(nodes) == 4 and ordered == [0, 1, 2, 3]:
            v = 'proximalToDistal'
        elif 'thorax' in names and 'leadArm' in names and names.index('leadArm') < names.index('thorax'):
            v = 'armBeforeThorax'
        elif len(nodes) == 4:
            v = 'other'
        else:
            v = 'partial' if ordered == sorted(ordered) else 'other'
        row[f'{prefix}_verdict'] = v

    resolve('pair', 'pair_state', 'pair_before_impact_ms', 'pair_sigma_t_ms')
    resolve('ext', 'ext_state', 'ext_before_impact_ms', 'ext_sigma_t_ms')
    return (row, series) if keep_series else (row, None)


# ── figures ────────────────────────────────────────────────────────────────────────────────────

def figure(name, row, series, ph, out_paths):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt

    t_addr, t_top, t_imp = ph[0], ph[2], ph[5]
    fig, ax = plt.subplots(2, 2, figsize=(15, 10), dpi=110)
    fig.suptitle(f'{name} — face-on + down-the-line span pair', fontsize=12)
    colour = {'pelvis': 'tab:blue', 'thorax': 'tab:red'}

    # (a) the cloud against the unit quarter circle
    a0 = ax[0][0]
    a0.plot(np.cos(np.linspace(-np.pi / 2, np.pi / 2, 300)), np.sin(np.linspace(-np.pi / 2, np.pi / 2, 300)),
            'k--', lw=1, label='unit circle (x ≥ 0)')
    a0.axhline(0, color='0.8', lw=0.6)
    for seg in series:
        s = series[seg]
        m = (s['tg'] >= t_addr) & (s['tg'] <= t_imp + 0.10)
        sc = a0.scatter(s['x'][m], s['y'][m], c=(s['tg'][m] - t_addr) * 1e3, s=8,
                        cmap='viridis' if seg == 'pelvis' else 'plasma')
        for lbl, tt in (('P1', t_addr), ('P4', t_top), ('P7', t_imp)):
            a0.annotate(f'{lbl} {seg[:3]}', (np.interp(tt, s['tg'], s['x']), np.interp(tt, s['tg'], s['y'])),
                        fontsize=7, color=colour[seg])
            a0.plot(np.interp(tt, s['tg'], s['x']), np.interp(tt, s['tg'], s['y']), 'o',
                    ms=5, mfc='none', mec=colour[seg])
        fig.colorbar(sc, ax=a0, shrink=0.7, label=f'ms after P1 ({seg})')
    a0.set_xlabel("w_faceOn / W"); a0.set_ylabel("w_dtl / W'")
    a0.set_title('(a) the pair cloud — on the circle means the ellipse closes')
    a0.set_aspect('equal'); a0.grid(alpha=0.3); a0.legend(fontsize=7)

    # (b) theta(t), FO-only vs pair
    a1 = ax[0][1]
    for seg in series:
        s = series[seg]
        a1.plot((s['tg'] - t_imp) * 1e3, np.degrees(s['th']), color=colour[seg], label=f'{seg} pair')
        a1.plot((s['tg'] - t_imp) * 1e3, np.degrees(s['th_fo']), color=colour[seg], ls=':',
                alpha=0.7, label=f'{seg} face-on only')
        blind = ~s['sighted']
        a1.fill_between((s['tg'] - t_imp) * 1e3, -90, 90, where=blind, color=colour[seg], alpha=0.06)
    for lbl, k in (('P1', 0), ('P2', 12), ('P3', 8), ('P4', 2), ('P5', 13), ('P6', 9), ('P7', 5), ('P8', 14)):
        if k in ph:
            a1.axvline((ph[k] - t_imp) * 1e3, color='0.7', lw=0.6)
            a1.annotate(lbl, ((ph[k] - t_imp) * 1e3, 84), fontsize=6, color='0.4')
    a1.set_xlim(-700, 200); a1.set_ylim(-90, 90); a1.grid(alpha=0.3)
    a1.set_xlabel('ms relative to impact'); a1.set_ylabel('turn θ, ° (closed +)')
    a1.set_title('(b) turn — shaded = face-on blind band (|θ| < 20°)')
    a1.legend(fontsize=7)

    # (c) rates with the placed peaks and the arm/club nodes
    a2 = ax[1][0]
    for seg in series:
        s = series[seg]
        a2.plot((s['tg'] - t_imp) * 1e3, s['rate'], color=colour[seg], label=f'{seg} pair')
        a2.fill_between((s['tg'] - t_imp) * 1e3, s['rate'] - s['sig_rate'], s['rate'] + s['sig_rate'],
                        color=colour[seg], alpha=0.15)
        p = s['peak']
        if p and row.get(f'{seg}_pair_state') == 'placed':
            a2.errorbar([(p['t'] - t_imp) * 1e3], [p['peak']], xerr=[p['t_sigma_ms']], fmt='o',
                        color=colour[seg], capsize=4, label=f"{seg} node ±{p['t_sigma_ms']:.0f} ms")
        elif p:
            a2.plot([(p['t'] - t_imp) * 1e3], [p['peak']], 'x', color=colour[seg],
                    label=f"{seg} bounded at the domain edge")
        pe = s.get('peak_ext')
        if pe:
            a2.errorbar([(pe['t'] - t_imp) * 1e3], [pe['peak']], xerr=[pe['t_sigma_ms']], fmt='s',
                        mfc='none', color=colour[seg], capsize=4,
                        label=f"{seg} peak past impact ±{pe['t_sigma_ms']:.0f} ms")
    for s2, c in (('leadArm', 'tab:green'), ('club', 'tab:orange')):
        v = row.get(f'{s2}_before_impact_ms')
        if v is not None and row.get(f'{s2}_placed'):
            a2.axvline(-v, color=c, lw=1.4, ls='--', label=f'{s2} node (run result)')
    a2.axvline(0, color='k', lw=0.8)
    a2.axvspan(0, 150, color='0.85', alpha=0.5, zorder=0)
    a2.annotate('outside the sequence domain', (75, a2.get_ylim()[1]), fontsize=7,
                color='0.35', ha='center', va='top')
    a2.set_xlim(-400, 150); a2.grid(alpha=0.3)
    a2.set_xlabel('ms relative to impact'); a2.set_ylabel('angular rate, °/s (opening +)')
    a2.set_title('(c) pair trunk rates, the arm/club nodes, and where the trunk actually peaks')
    a2.legend(fontsize=7)

    # (d) DTL keypoint confidences
    a3 = ax[1][1]
    for seg, style in (('pelvis', '-'), ('thorax', '--')):
        if seg not in series:
            continue
        tc, ca, cb = series[seg]['conf']; a, b = series[seg]['kp']
        a3.plot((tc - t_imp) * 1e3, ca, style, color=colour[seg], lw=1, label=f'{seg} kp{a}')
        a3.plot((tc - t_imp) * 1e3, cb, style, color=colour[seg], lw=1, alpha=0.5, label=f'{seg} kp{b}')
    a3.axhline(0.30, color='k', ls=':', lw=1, label='confidence gate 0.30')
    for lbl, k in (('P4', 2), ('P7', 5)):
        if k in ph:
            a3.axvline((ph[k] - t_imp) * 1e3, color='0.7', lw=0.6)
    a3.set_xlim(-900, 400); a3.set_ylim(0, 1); a3.grid(alpha=0.3)
    a3.set_xlabel('ms relative to impact'); a3.set_ylabel('DTL keypoint confidence')
    a3.set_title('(d) down-the-line hip and shoulder confidences')
    a3.legend(fontsize=7, ncol=2)

    fig.tight_layout(rect=(0, 0, 1, 0.97))
    for p in out_paths:
        os.makedirs(os.path.dirname(p), exist_ok=True)
        fig.savefig(p)
    plt.close(fig)


# ── main ───────────────────────────────────────────────────────────────────────────────────────

def med(xs):
    xs = [x for x in xs if x is not None and isinstance(x, (int, float)) and np.isfinite(x)]
    return float(np.median(xs)) if xs else float('nan')


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('--csv', default='docs/research/data/kinematic_sequence/pair_span_turn_20260920.csv')
    ap.add_argument('--figdir', default='docs/research/data/kinematic_sequence/figures')
    ap.add_argument('--desktop', default=os.path.expanduser('~/Desktop/DTL-shaft-tracker'))
    ap.add_argument('--figures', default=f'{S74}__swing_0004,{S74}__swing_0010,{S61}__swing_0001')
    a = ap.parse_args()

    want_fig = set(a.figures.split(','))
    rows = []
    for rec in swing_table():
        name = f"{rec['session']}__{rec['swing']}"
        res = analyse(rec, keep_series=(name in want_fig))
        if not res:
            print('skip', name, file=sys.stderr); continue
        row, series = res
        rows.append(row)
        if series:
            ph = sto.anchors(f"{SW}/{rec['session']}/{rec['swing']}/swing_phasegrid.json")
            outs = [os.path.join(a.figdir, f'07_ks_pair_{name}.png')]
            if a.desktop:
                outs.append(os.path.join(a.desktop, f'07_ks_pair_{name}.png'))
            figure(name, row, series, ph, outs)
        print('.', end='', flush=True)
    print(f'\n{len(rows)} swings')

    keys = sorted({k for r in rows for k in r}, key=lambda k: (k != 'swing', k != 'session', k))
    os.makedirs(os.path.dirname(a.csv), exist_ok=True)
    with open(a.csv, 'w', newline='') as f:
        w = csv.DictWriter(f, fieldnames=keys); w.writeheader(); w.writerows(rows)
    print('wrote', a.csv)

    for seg, _ in SEGS:
        print(f'\n== {seg}')
        for st in ('placed', 'bounded', 'neither', 'absent'):
            print(f'  FO-only {st:8s}', sum(1 for r in rows if r.get(f'{seg}_fo_state') == st))
        for st in ('placed', 'bounded', 'neither'):
            print(f'  PAIR    {st:8s}', sum(1 for r in rows if r.get(f'{seg}_pair_state') == st))
        pl = [r for r in rows if r.get(f'{seg}_pair_state') == 'placed']
        print('  pair peak before impact ms  ', med([r[f'{seg}_pair_before_impact_ms'] for r in pl]))
        print('  pair sigma_t ms             ', med([r[f'{seg}_pair_sigma_t_ms'] for r in pl]))
        print('  pair peak dps               ', med([r[f'{seg}_pair_peak_dps'] for r in pl]))
        print('  EXT peak before impact ms   ', med([r.get(f'{seg}_ext_before_impact_ms') for r in rows]),
              ' at edge', sum(1 for r in rows if r.get(f'{seg}_ext_at_edge')))
        print('  closure p50 downswing       ', med([r.get(f'{seg}_clos_downswing_p50') for r in rows]))
        print('  closure p50 impact50        ', med([r.get(f'{seg}_clos_impact50_p50') for r in rows]))
        print('  FO|dx| closure p50 ds/imp/top', med([r.get(f'{seg}_fox_clos_downswing_p50') for r in rows]),
              '/', med([r.get(f'{seg}_fox_clos_impact50_p50') for r in rows]),
              '/', med([r.get(f'{seg}_fox_clos_top_p50') for r in rows]))
        print('  theta fo vs pair p50 deg    ', med([r.get(f'{seg}_th_fo_vs_pair_p50_deg') for r in rows]))
        print('  theta fo−pair early / late  ', med([r.get(f'{seg}_th_fo_minus_pair_early_deg') for r in rows]),
              '/', med([r.get(f'{seg}_th_fo_minus_pair_late100_deg') for r in rows]))
        print('  tilt deg (normalised cloud) ', med([r.get(f'{seg}_tilt_deg') for r in rows]))
        print('  gamma deg conic / from tsq  ', med([r.get(f'{seg}_gamma_deg') for r in rows]),
              '/', med([r.get(f'{seg}_gamma_from_tsq_deg') for r in rows]))
        print('  t_sq dtl − fo ms            ', med([r.get(f'{seg}_t_sq_diff_ms') for r in rows]))
        print('  |dtl| min / W\' (signed)     ', med([r.get(f'{seg}_dtl_span_min_over_W') for r in rows]),
              ' (folded 2-D span', med([r.get(f'{seg}_fold_span_min_over_W') for r in rows]), ')')
        print('  |dtl| at the FO max / W\'    ', med([r.get(f'{seg}_dtl_at_fo_max_over_W') for r in rows]))
        print('  dtl conf p10 downswing      ', med([r.get(f'{seg}_dtl_conf_min_p10') for r in rows]))
        print('  dtl vs |sin| r              ', med([r.get(f'{seg}_dtl_vs_sin_r') for r in rows]))
        print('  sens3 |dt| max ms (dom/ext) ', med([r.get(f'{seg}_sens3_dt_ms_max') for r in rows]),
              '/', med([r.get(f'{seg}_sens3_ext_dt_ms_max') for r in rows]),
              ' dps rel', med([r.get(f'{seg}_sens3_ext_dps_rel_max') for r in rows]))
        print('  sens10 |dt| max ms (dom/ext)', med([r.get(f'{seg}_sens10_dt_ms_max') for r in rows]),
              '/', med([r.get(f'{seg}_sens10_ext_dt_ms_max') for r in rows]))
        print('  FOLD variant ext ms / dps   ', med([r.get(f'{seg}_fold_ext_before_impact_ms') for r in rows]),
              '/', med([r.get(f'{seg}_fold_ext_peak_dps') for r in rows]),
              ' peak-minus-tsq ms', med([r.get(f'{seg}_fold_minus_tsq_ms') for r in rows]))
        print('  FO|dx| variant ext ms/dps   ', med([r.get(f'{seg}_fox_ext_before_impact_ms') for r in rows]),
              '/', med([r.get(f'{seg}_fox_ext_peak_dps') for r in rows]))
        print('  CONSISTENCY r2 / gamma / rms', med([r.get(f'{seg}_cons_r2') for r in rows]),
              '/', med([r.get(f'{seg}_cons_gamma_deg') for r in rows]),
              '/', med([r.get(f'{seg}_cons_rms_px') for r in rows]))
        print('  ANCHORED clos ds/imp/top    ', med([r.get(f'{seg}_anch_clos_downswing_p50') for r in rows]),
              '/', med([r.get(f'{seg}_anch_clos_impact50_p50') for r in rows]),
              '/', med([r.get(f'{seg}_anch_clos_top_p50') for r in rows]))
        print('  ANCHORED ext ms / dps       ', med([r.get(f'{seg}_anch_ext_before_impact_ms') for r in rows]),
              '/', med([r.get(f'{seg}_anch_ext_peak_dps') for r in rows]))
        print('  spikeflag                   ', sum(1 for r in rows if r.get(f'{seg}_pair_spikeflag')))
