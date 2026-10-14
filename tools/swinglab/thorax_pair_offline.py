#!/usr/bin/env python3
"""A camera-only THORAX node from the face-on + down-the-line pair, offline (14 Oct 2026).

WHY. The shipped pair route (segment_rates.cpp pairTurnTrack) places the PELVIS and keeps the
THORAX ring off (tuned::sequence::kPairTrunkThoraxPlacement = false): the face-on shoulder line
genuinely crosses square TO THE FACE-ON CAMERA 160–190 ms before impact, its keypoints jump there,
and the 25 ms derivative turns the jump into a confident thorax "peak". A golfer with two cameras
and no IMU has no thorax node at all. This rig asks whether a variant of the pair can place one
that deserves trust, against gates fixed before any result (the brief, and below).

THE VARIANTS (cumulative; every one run for BOTH pelvis and thorax):
  V0  the shipped pair, re-implemented sample for sample from segment_rates.cpp: signed |Δx|
      separations in both legs, r from the address vertical extent, γ = 90° (ψ = atan2(y/r, x)),
      the collapse relabel guard (pairSwapMinFrac 0.35, ≥3 flips in 100 ms dropped), the
      rigid-body rate limit (2000 °/s), smoothed-where-clean source choice, the 25 ms local
      quadratic, the domain Top/Transition → P8 with the P4 draw cut, and finishChannel's
      placement and bounds (validityBand, bandIsHole, reversal spike, σ_t ≤ 40 ms). VALIDATED on
      the pelvis against the shipped nodes in the swing documents (gate G-a).
  V1  + the face-on 90° crossing (|d_fo| < 0.35·W_fo, padded by one 25 ms derivative window) is an
      interior HOLE: those angle samples are invalid, and a peak within one derivative window of
      ANY hole edge is a bound (the C++ only reads the first hole; V1 reads all of them).
  V2  + the face-on relabel decision is arbitrated by the DOWN-THE-LINE leg: at a face-on sign
      change the crossing is genuine iff |d_dtl| ≥ cos 20°·W'_dtl there, else it is a relabel.
  V3  + the scales are the jointly fitted ellipse (W, W') over Top → Impact (K0 §2.3's robust
      a·x² + b·y² = 1) instead of r from the address vertical extent.
  V4  + γ carried: fitted per swing per segment from K0 §2.1's consistency form
      d_dtl = W'·cos(θ_fo − γ), used in ψ = atan2((y − x·cos γ)/sin γ, x); refused when γ is
      outside 60–90° or R² < 0.85 (K0 G3).
  V5  (columns, not a variant) each variant's σ_t with σ_w from the still pre-takeaway hold (lower
      quartile of 150 ms windows) in place of the keypoint σ (K0 §3.5).

DECISIONS TAKEN ON THE DEVELOPMENT SESSIONS, BEFORE THE HELD-OUT RUN (each one written up in
thorax_pair_20261014.md §3):
  * V1's crossing is read on the RAW face-on keypoints, and a face-on leg that crosses is consumed
    RAW (de-swapped) rather than smoothed: the smoother bridges the crossing's label flutter into a
    ramp that overshoots the line's own width (07-04 s8: +168 px against a 130 px p95).
  * V2 as briefed (20°) inverts the face-on parity at impact on 9 of 24 dev swings (V0's collapse
    rule on 2, 06-11 s5/s7): the DTL shoulder amplitude at the genuine downswing crossing is only
    0.84–1.02 of its own p95. V2s/V3s/V4s carry the same rule at 35° (0/24 inverted); the freeze
    tries that chain first.
  * V4's scales are K0 §2.1's own triple (face-on W_peak, the fitted W', γ): under γ ≠ 90 the
    axis-aligned ellipse of V3 is the wrong model, and with it the pelvis γ read 54–65° on 06-11.
  * G-f is counted in FRAMES (face-on samples between the two peak instants), because "one frame"
    is 6.6–6.9 ms on these clocks and a strict 6.7 ms test fails a one-frame move.

THE GATES (fixed by the brief before any result):
  G-a  V0 pelvis matches the shipped node: |Δt| ≤ 1 ms and |Δpeak| ≤ 3 % on ≥ 90 % of swings
       (a swing whose shipped pelvis is NOT the pair reproduces iff V0 also withdraws/refuses).
  G-b  no placed thorax node within 60 ms of a rate reversal (either side) or adjacent (within one
       25 ms derivative window) to the face-on crossing hole.
  G-c  every placed thorax peak in 400–1100 °/s.
  G-d  within-session IQR of placed thorax peak time ≤ 15 ms.
  G-e  the frozen variant on 2026-07-03 (HELD OUT) meets G-b, G-c, G-d and places on ≥ half.
  G-f  placed thorax peak time moves ≤ 1 frame (6.7 ms) under ±10 % on either scale and γ anywhere
       in 75–90°.
  G-g  (reported, judged) does the thorax peak track its own DTL square crossing more tightly
       than it tracks the pelvis or the arm?

THE FREEZE (select_variant): the frozen variant is the FIRST in VARIANTS order that, on BOTH dev
sessions, passes G-b, G-c, G-d, G-f, places the thorax on ≥ half the swings, and does not break the
pelvis (pelvis placed count ≥ V0's and median |Δt| vs V0 ≤ 1 frame). If none does, it is the one
passing the most of those twelve checks (ties to the earlier). On the dev sessions none passes and
the rule freezes V3s (10 of 12; G-f fails on both sessions). The held-out session is then run ONCE,
on that variant only (plus V0's pelvis, for G-a).

DATA. The 34 two-camera corpus swings, read-only through tools/pp_swingdoc.load_swing:
2026-06-11 (9) and 2026-07-04 (15) are the development set; 2026-07-03 (10) is held out. Frame
sizes from streams[].encoded. The lead-arm and club nodes are the documents' shipped ones.

REUSED: span_pair_offline.fit_axis_aligned (the K0 ellipse, V3) and zero_crossing (the DTL square
crossing); skeleton_rate_offline.load_skeleton / line_bearing (the triangulated route's lines).
NOT reused, deliberately: span_pair_offline's local_fits / place_peak run on a 4 ms RESAMPLED grid
with no window widening and no two-instant linear fallback; G-a asks for 1 ms against the shipped
C++, which runs on the native sample instants. fit_at / local_rates / reduce_extremum /
place_peak / finish_channel below are line-for-line replicas of angular_rate.h, series_reduce.h
and segment_rates.cpp finishChannel instead.

    ~/.swinglab-venv/bin/python tools/swinglab/thorax_pair_offline.py --stage all \\
        --csv docs/research/data/kinematic_sequence/thorax_pair_20261014.csv \\
        --figdir docs/research/data/kinematic_sequence/figures \\
        --summary /mnt/swingdata/scratch/thorax-pair/summary.json \\
        --report /mnt/swingdata/scratch/thorax-pair/report.md     # the write-up's tables

  --stage dev      development sessions only, all variants (what the freeze is read from)
  --stage all      dev, then the freeze, then the held-out session once on the frozen variant
"""
import argparse, csv, json, math, os, sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, '..'))
from pp_swingdoc import load_swing                                      # noqa: E402
from span_pair_offline import fit_axis_aligned, zero_crossing           # noqa: E402  K0's ellipse + crossing
from skeleton_rate_offline import load_skeleton, line_bearing           # noqa: E402  triangulated lines

CORPUS = '/mnt/swingdata/corpus/swings'
DEV = ('2026-06-11_Mark-Liversedge_Wrist_01', '2026-07-04_Mark-Liversedge_Wrist_01')
HELDOUT = ('2026-07-03_Mark-Liversedge_Wrist_01',)
# Each variant is a set of switches. V0…V4 are the brief's cumulative chain; V2s…V4s are the same chain
# with the V2 broadside threshold set on the DEVELOPMENT data (35°, see V2S_GENUINE_DEG) after the
# inspection the brief asked for found the 20° rule wrong on 9 of 24 dev swings. ORDER is the order the
# freeze tries them in (simplest first).
SPECS = {
    'V0':  dict(hole=False, relabel=None, ellipse=False, gamma=False),
    'V1':  dict(hole=True,  relabel=None, ellipse=False, gamma=False),
    'V2':  dict(hole=True,  relabel=20.0, ellipse=False, gamma=False),
    'V2s': dict(hole=True,  relabel=35.0, ellipse=False, gamma=False),
    'V3':  dict(hole=True,  relabel=20.0, ellipse=True,  gamma=False),
    'V3s': dict(hole=True,  relabel=35.0, ellipse=True,  gamma=False),
    'V4':  dict(hole=True,  relabel=20.0, ellipse=True,  gamma=True),
    'V4s': dict(hole=True,  relabel=35.0, ellipse=True,  gamma=True),
}
VARIANTS = ('V0', 'V1', 'V2s', 'V2', 'V3s', 'V3', 'V4s', 'V4')   # the freeze's order (simplest first; the
# 35° chain ahead of the 20° one because the 20° rule inverts the face-on parity on 9 of 24 dev swings)
DTL_SHIFTS_US = (-4000, -12000)   # the sync sensitivity: DTL stamps moved earlier by today's +4 ms and this rig's +12 ms

# ── tuned::sequence:: / tuned::reduce:: / tuned::pose:: — the shipped constants, mirrored ───────
CONF_MIN = 0.30
ADDR_MIN_FRAMES = 5
ADDR_WINDOW_US = 250000
KP_SIGMA_PX = 3.0
DERIV_WINDOW_US = 25000
MAX_PLACE_SIGMA_MS = 40.0
MIN_AFTER_REVERSAL_MS = 60.0
PAIR_MIN_CORR = 0.6
PAIR_MIN_EXTENT_PX = 100.0
PAIR_MAX_GAP_FRAMES = 2.5
PAIR_SWAP_MIN_FRAC = 0.35
PAIR_MAX_TURN_DPS = 2000.0
DTL_LOCAL_GAP = True
EXTREMUM_WINDOW_US = 40000
MIN_EXTREMUM_SAMPLES = 3
SKEL_ANGLE_SIGMA_RAD = math.radians(0.5)        # sigmaM is not persisted (skeleton_rate_offline)

PH_ADDRESS, PH_TAKEAWAY, PH_TOP, PH_TRANSITION, PH_IMPACT, PH_FINISH, PH_P8 = 0, 1, 2, 3, 5, 7, 14
LS, RS, LE, RE, LW, RW, LH, RH, LA, RA = 5, 6, 7, 8, 9, 10, 11, 12, 15, 16
SEGS = (('pelvis', LH, RH, 'LeftUpLeg', 'RightUpLeg'), ('thorax', LS, RS, 'LeftArm', 'RightArm'))

# ── The study's own constants, fixed before any result ────────────────────────────────────────
HOLE_FRAC = 0.35                 # V1: face-on |d| below this share of its p95 is the 90° crossing
HOLE_PAD_US = DERIV_WINDOW_US    # … padded by one derivative window either side
V2_GENUINE_DEG = 20.0            # V2: DTL within 20° of broadside ⇒ the face-on crossing is genuine (the brief)
V2S_GENUINE_DEG = 35.0           # V2s: the same rule at 35°, set on the dev sessions (module docstring)
GAMMA_LO, GAMMA_HI, R2_MIN = 60.0, 90.0, 0.85
GB_REVERSAL_MS, GB_ADJ_MS = 60.0, DERIV_WINDOW_US / 1000.0
GC_LO, GC_HI = 400.0, 1100.0
GD_IQR_MS = 15.0
FRAME_MS = 6.7
SCALE_PERT = 0.10
GAMMAS_PERT = (75.0, 80.0, 85.0, 90.0)
STILL_WIN_US, STILL_MIN_N = 150000, 5     # V5 still-window σ_w


# ══ C++ replicas ═══════════════════════════════════════════════════════════════════════════════

def cmedian(v):
    """medianOf(): nth_element at n/2 — the UPPER median for an even count. 0 when empty."""
    v = sorted(x for x in v if math.isfinite(x))
    return v[len(v) // 2] if v else 0.0


def cpercentile(v, q):
    """percentileOf(): linear between order statistics."""
    v = sorted(x for x in v if math.isfinite(x))
    if not v:
        return 0.0
    pos = min(max(q, 0.0), 1.0) * (len(v) - 1)
    lo = int(math.floor(pos)); hi = min(lo + 1, len(v) - 1)
    return v[lo] + (pos - lo) * (v[hi] - v[lo])


def pearson(a, b):
    n = min(len(a), len(b))
    if n < 3:
        return 0.0
    a = np.asarray(a[:n], float); b = np.asarray(b[:n], float)
    da, db = a - a.mean(), b - b.mean()
    saa, sbb = float((da * da).sum()), float((db * db).sum())
    return float((da * db).sum() / math.sqrt(saa * sbb)) if saa > 0 and sbb > 0 else 0.0


def fit_at(t, v, valid, i0, half):
    """angular_rate.h detail_rate::fitAt. Returns (ok, n, c0, c1, c2, sumSq)."""
    n = len(t); t0 = t[i0]
    lo = i0
    while lo > 0 and t0 - t[lo - 1] <= half:
        lo -= 1
    s0 = s1 = s2 = s3 = s4 = sy = sxy = sx2y = 0.0
    cnt = 0; distinct = 0; last = None
    i = lo
    while i < n and t[i] - t0 <= half:
        if (valid is None or valid[i]) and math.isfinite(v[i]):
            x = (t[i] - t0) * 1e-6; y = v[i]
            s0 += 1; s1 += x; s2 += x * x; s3 += x ** 3; s4 += x ** 4
            sy += y; sxy += x * y; sx2y += x * x * y; cnt += 1
            if t[i] != last:
                distinct += 1; last = t[i]
        i += 1
    if distinct < 2:
        return (False, cnt, 0.0, 0.0, 0.0, 0.0)
    det = s0 * (s2 * s4 - s3 * s3) - s1 * (s1 * s4 - s3 * s2) + s2 * (s1 * s3 - s2 * s2)
    if distinct < 3 or not (abs(det) > 0.0):
        ld = s0 * s2 - s1 * s1
        if not (abs(ld) > 1e-30):
            return (False, cnt, 0.0, 0.0, 0.0, 0.0)
        return (True, cnt, (sy * s2 - s1 * sxy) / ld, (s0 * sxy - s1 * sy) / ld, 0.0, s2 - s1 * s1 / s0)
    d0 = sy * (s2 * s4 - s3 * s3) - s1 * (sxy * s4 - s3 * sx2y) + s2 * (sxy * s3 - s2 * sx2y)
    d1 = s0 * (sxy * s4 - s3 * sx2y) - sy * (s1 * s4 - s3 * s2) + s2 * (s1 * sx2y - s2 * sxy)
    d2 = s0 * (s2 * sx2y - sxy * s3) - s1 * (s1 * sx2y - sxy * s2) + sy * (s1 * s3 - s2 * s2)
    return (True, cnt, d0 / det, d1 / det, d2 / det, s2 - s1 * s1 / s0)


def local_rates(t, v, sigma, valid, window_us):
    """angular_rate.h localRates: (ok, rate/s, rateSigma/s) per sample."""
    n = len(t); half = max(window_us // 2, 1)
    ok = np.zeros(n, bool); rate = np.full(n, np.nan); rs = np.zeros(n)
    tt = np.asarray(t)
    for i in range(n):
        f = fit_at(t, v, valid, i, half)
        if not f[0]:
            continue
        sv = 0.0
        if sigma is not None:
            m = np.abs(tt - t[i]) <= half
            if valid is not None:
                m &= valid
            ws = [x for x in np.asarray(sigma)[m] if math.isfinite(x)]
            if ws:
                sv = sorted(ws)[len(ws) // 2]
        ok[i] = True; rate[i] = f[3]
        rs[i] = sv / math.sqrt(f[5]) if f[5] > 0 else 0.0
    return ok, rate, rs


def differentiate(at, sign):
    """segment_rates.cpp differentiate(): °/s, invalid where the fit failed or the angle is not admitted."""
    ok, rate, rs = local_rates(at['t'], at['ang'], at['sig'], at['valid'], DERIV_WINDOW_US)
    good = ok & at['valid']
    dps = np.where(good, sign * rate * 180.0 / math.pi, 0.0)
    sig = np.where(good, rs * 180.0 / math.pi, 0.0)
    return dict(t=list(at['t']), dps=dps, sig=sig, valid=good)


def window_mean_at(t, v, valid, i):
    """series_reduce.h windowMeanValueAt: ±20 ms, widened symmetrically to ≥ 3 valid samples."""
    vi = np.where(valid)[0]
    d = np.abs(t[vi] - t[i])
    h = EXTREMUM_WINDOW_US // 2
    while True:
        inn = d <= h
        if inn.sum() >= MIN_EXTREMUM_SAMPLES or not (d > h).any():
            break
        h = int(d[d > h].min())
    return float(v[vi][d <= h].mean())


def place_peak(t, v, valid, rsig, lo, hi):
    """angular_rate.h placePeak over reduceExtremum (max). t int64 array."""
    t = np.asarray(t, np.int64)
    best = None; bi = -1
    for i in range(len(t)):
        if t[i] < lo:
            continue
        if t[i] > hi:
            break
        if not valid[i] or not math.isfinite(v[i]):
            continue
        w = window_mean_at(t, v, valid & np.isfinite(v), i)
        if best is not None and not (w > best + 1e-12 * max(1.0, abs(best))):
            continue
        best = w; bi = i
    if bi < 0:
        return None
    tp = int(t[bi])
    i0 = int(np.argmin(np.abs(t - tp)))
    f = fit_at(list(t), list(v), valid, i0, DERIV_WINDOW_US)
    curv = 2.0 * f[4] if f[0] else 0.0
    m = (np.abs(t - tp) <= DERIV_WINDOW_US) & valid & np.isfinite(rsig) & (rsig > 0)
    ws = sorted(rsig[m])
    sr = ws[len(ws) // 2] if ws else float('nan')   # (the reducer's window σ fallback is never needed here)
    dom_s = (hi - lo) * 1e-6
    st = min(math.sqrt(2.0 * sr / abs(curv)), dom_s) if (abs(curv) > 1e-12 and sr >= 0) else dom_s
    return dict(t=tp, peak=best, sr=sr, t_sigma_ms=st * 1000.0, curv=curv)


def validity_band(at, lo, hi):
    """segment_rates.cpp validityBand: the FIRST hole after the first valid domain sample."""
    bf = bt = -1; started = inb = False
    for t, ok in zip(at['t'], at['valid']):
        if t < lo or t > hi:
            continue
        if not started:
            if not ok:
                continue
            started = True
        if not ok and not inb and bf < 0:
            bf = t; inb = True
        elif ok and inb and bt < 0:
            bt = t; inb = False
    return bf, bt


def holes_of(at, lo, hi, crossing):
    """V1+: EVERY run of invalid angle samples in the domain. A leading run counts only when it lies in
    the face-on crossing (otherwise the domain had merely not begun, as validityBand says)."""
    runs = []; cur = None
    t = at['t']; ok = at['valid']
    seen_valid = False
    for i in range(len(t)):
        if t[i] < lo or t[i] > hi:
            continue
        if not ok[i]:
            if cur is None:
                cur = [t[i], None, seen_valid]
        else:
            if cur is not None:
                cur[1] = t[i]; runs.append(cur); cur = None
            seen_valid = True
    if cur is not None:
        runs.append(cur)
    out = []
    for s, e, interior in runs:
        if not interior:
            if not any(cs - HOLE_PAD_US <= s <= ce + HOLE_PAD_US for cs, ce in crossing):
                continue
            s = None                      # the domain opened inside it: only its exit is an edge
        out.append((s, e))
    return out


def finish_channel(rt, dom, gate):
    """segment_rates.cpp finishChannel: trim to P4…P8, place the peak, then the bounds and gates.
    gate: may, spike_guard, mode 'cpp' (blind_from/blind_to) or 'holes' (list of (start, end))."""
    t = np.asarray(rt['t'], np.int64); v = np.asarray(rt['dps'], float)
    s = np.asarray(rt['sig'], float); ok = np.asarray(rt['valid'], bool).copy()
    keep = np.ones(len(t), bool)
    if dom['draw_from'] >= 0 and dom['draw_from'] <= dom['from']:
        keep &= t >= dom['draw_from']
    if dom['to'] > dom['impact']:
        keep &= t <= dom['to']
    t, v, s, ok = t[keep], v[keep], s[keep], ok[keep]
    ok &= (t >= dom['from']) & (t <= dom['to'])
    node = dict(ok=False, placed=False, no_earlier=float('nan'), no_later=float('nan'),
                spike=False, at_entry=False, at_exit=False, series=(t, v, s, ok))
    if len(t) < 2:
        node['state'] = 'neither'; return node
    p = place_peak(t, v, ok, s, dom['from'], dom['to'])
    if p is None:
        node['state'] = 'neither'; return node
    W = DERIV_WINDOW_US
    tp = p['t']
    node.update(ok=True, t_peak=tp, peak=p['peak'], t_sigma_ms=p['t_sigma_ms'], curv=p['curv'])
    lo, hi = dom['from'], dom['to']
    at_entry = at_exit = False
    entry_edge = exit_edge = None
    if gate['mode'] == 'cpp':
        bf, bt = gate['blind_from'], gate['blind_to']
        at_entry = bf >= 0 and bf <= hi and abs(tp - bf) <= W
        at_exit = bt >= 0 and bt >= lo and abs(tp - bt) <= W and not at_entry
        entry_edge, exit_edge = bf, bt
        nxt = bf if (bf >= 0 and bf <= hi and tp < bf - W) else None
    else:
        nxt = None
        for hs, he in gate['holes']:
            if hs is not None and abs(tp - hs) <= W and not at_entry:
                at_entry = True; entry_edge = hs
            if he is not None and abs(tp - he) <= W and not at_entry and not at_exit:
                at_exit = True; exit_edge = he
            if hs is not None and tp < hs - W and (nxt is None or hs < nxt):
                nxt = hs
    # RISING INTO THE BAND (the next hole after the peak)
    if not at_entry and not at_exit and nxt is not None:
        mL = (t >= nxt - W) & (t < nxt) & ok
        mE = (t >= nxt - 2 * W) & (t < nxt - W) & ok
        if mL.sum() and mE.sum() and v[mL].mean() > v[mE].mean():
            at_entry = True; entry_edge = nxt
    spike = False
    if gate['spike_guard'] and not at_entry and not at_exit:
        m = (t >= lo) & (t <= tp) & ok & (v <= 0.0)
        if m.any():
            spike = (tp - int(t[m].max())) < MIN_AFTER_REVERSAL_MS * 1000
    ref = dom['impact']
    if at_entry and entry_edge is not None and not (entry_edge <= lo + W):
        node['no_earlier'] = (ref - entry_edge) * 1e-3
    if at_exit and exit_edge is not None and not (exit_edge >= hi - W):
        node['no_later'] = (ref - exit_edge) * 1e-3
    node.update(spike=spike, at_entry=at_entry, at_exit=at_exit)
    node['placed'] = bool(gate['may'] and not at_entry and not at_exit and not spike
                          and p['t_sigma_ms'] <= MAX_PLACE_SIGMA_MS)
    bounded = math.isfinite(node['no_earlier']) or math.isfinite(node['no_later'])
    node['state'] = 'placed' if node['placed'] else ('bounded' if bounded else 'neither')
    return node


def resolve(nodes, sigma_k=1.0):
    """kinematic_sequence.h resolveKinematicSequence: order and orderResolved over PLACED nodes."""
    pl = sorted([n for n in nodes if n and n.get('placed')], key=lambda n: n['t_peak'])
    order = '>'.join(n['segment'] for n in pl)
    res = len(pl) >= 2
    for a, b in zip(pl, pl[1:]):
        gap = (b['t_peak'] - a['t_peak']) / 1000.0
        if not (gap > sigma_k * math.hypot(a['t_sigma_ms'], b['t_sigma_ms'])):
            res = False
    return order, res


# ══ The documents ══════════════════════════════════════════════════════════════════════════════

def frame_dims(doc):
    fo = dtl = None
    for s in doc.get('streams', []):
        if s.get('kind') != 'video':
            continue
        e = s['encoded']; a = (s.get('alias') or '').lower()
        if a.startswith('face'):
            fo = (e['width'], e['height'])
        elif a in ('dtl', 'down-the-line'):
            dtl = (e['width'], e['height'])
    return fo, dtl


def view(frames, W, H, with_sigma=False):
    t = np.array([int(round(f['t_us'])) for f in frames], np.int64)
    kp = np.array([f['kp'] for f in frames], float).reshape(len(frames), -1, 3)
    out = dict(t=t, x=kp[:, :, 0] * W, y=kp[:, :, 1] * H, c=kp[:, :, 2], W=W, H=H)
    if with_sigma and frames and 'sigma' in frames[0]:
        out['s'] = np.array([f['sigma'] for f in frames], float)
    return out


def phase_time(phases, ph, default=-1):
    for p in phases:
        if int(p['phase']) == ph:
            return int(round(p['t_us']))
    return default


def doc_node(doc, seg):
    for n in (doc['analysis'].get('kinematicSequence') or {}).get('nodes', []):
        if n['segment'] == seg:
            d = dict(n); d['t_peak'] = int(n['tPeakUs']); d['t_sigma_ms'] = n['tSigmaMs']
            d['peak'] = n['peakDps']; return d
    return None


def doc_series(doc, key):
    for m in doc['analysis'].get('metrics', []):
        if m['key'] == key:
            return m
    return None


# ══ The pair geometry ══════════════════════════════════════════════════════════════════════════

def separations(v, lead, trail):
    ok = (v['c'][:, lead] >= CONF_MIN) & (v['c'][:, trail] >= CONF_MIN)
    t = v['t'][ok]; d = (v['x'][ok, lead] - v['x'][ok, trail])
    if 's' in v:
        sl = np.where(v['s'][ok, lead] > 0, v['s'][ok, lead], KP_SIGMA_PX)
        st = np.where(v['s'][ok, trail] > 0, v['s'][ok, trail], KP_SIGMA_PX)
    else:
        sl = st = np.full(len(t), KP_SIGMA_PX)
    return dict(t=t.copy(), d=d.copy(), sigma=np.hypot(sl, st), ok=np.ones(len(t), bool))


def local_gap_us(t, k):
    n = len(t)
    if n < 2:
        return 0
    lo, hi = (k - 3 if k >= 3 else 0), min(n - 2, k + 3)
    g = [int(t[j + 1] - t[j]) for j in range(lo, hi + 1) if t[j + 1] - t[j] > 0]
    return sorted(g)[len(g) // 2] if g else 0


def address_extent(v, a0, a1):
    m = (v['t'] >= a0) & (v['t'] <= a1)
    for k in (LA, RA, LS, RS):
        m &= v['c'][:, k] >= CONF_MIN
    ay = 0.5 * (v['y'][m, LA] + v['y'][m, RA]); sy = 0.5 * (v['y'][m, LS] + v['y'][m, RS])
    e = list(np.abs(ay - sy))
    return (cmedian(e) if e else 0.0), len(e)


def deswap(s, a_from, a_to, w_view, local_gap, arbiter=None, events=None, judge=None):
    """deswapInPlace. With `arbiter(t0, t1)` → True when the crossing is GENUINE (V2), the decision at a
    raw sign change is the arbiter's instead of the collapse rule. `events` collects, per raw sign
    change, (t, collapse rule says relabel, `judge` says relabel) — the judge is the DTL rule, logged
    whichever rule decides, so V0 and V2 can be compared on the same sign changes."""
    n = len(s['t'])
    rep = dict(flips=0, dropped=0)
    if n < 3 or not (w_view > 0):
        return rep
    t = s['t']; d = s['d']
    step_us = cmedian([float(t[i] - t[i - 1]) for i in range(1, n)])
    if not (step_us > 0):
        return rep
    max_gap = int(3.0 * step_us)
    floor = PAIR_SWAP_MIN_FRAC * w_view

    def gap_limit(i_end):
        if not local_gap or i_end == 0:
            return max_gap
        return max(max_gap, int(3.0 * local_gap_us(t, i_end - 1)))

    idx = [i for i in range(n) if a_from <= t[i] <= a_to]
    a0, a1 = (min(idx), max(idx)) if idx else (0, 0)
    flip_t = []

    def decide(raw, prev, dt, i_end, t_a, t_b):
        if not (raw * prev < 0.0 and dt <= gap_limit(i_end)):
            return False
        collapse_relabel = min(abs(raw), abs(prev)) >= floor
        if arbiter is None:
            relabel = collapse_relabel
        else:
            relabel = not arbiter(t_a, t_b)
        if events is not None:
            events.append((0.5 * (t_a + t_b), collapse_relabel,
                           (not judge(t_a, t_b)) if judge is not None else None))
        return relabel

    p = 1.0; prev = d[a1]; prev_t = t[a1]
    for i in range(a1 + 1, n):
        if decide(d[i] * p, prev, t[i] - prev_t, i, prev_t, t[i]):
            p = -p; rep['flips'] += 1; flip_t.append(int(t[i]))
        d[i] *= p; prev = d[i]; prev_t = t[i]
    p = 1.0; prev = d[a0]; prev_t = t[a0]
    for k in range(a0 - 1, -1, -1):
        if decide(d[k] * p, prev, prev_t - t[k], k + 1, t[k], prev_t):
            p = -p; rep['flips'] += 1; flip_t.append(int(t[k]))
        d[k] *= p; prev = d[k]; prev_t = t[k]
    flip_t.sort()
    drop = np.zeros(n, bool)
    for i in range(len(flip_t) - 2):
        if flip_t[i + 2] - flip_t[i] > 100000:
            continue
        lo, hi = flip_t[i] - int(step_us), flip_t[i + 2]
        drop |= (t >= lo) & (t <= hi)
    rep['dropped'] = int((drop & s['ok']).sum())
    s['ok'] &= ~drop
    return rep


def rate_limit(s, w_view):
    n = len(s['t'])
    if n < 2 or not (w_view > 0):
        return 0
    wmax = math.radians(PAIR_MAX_TURN_DPS)
    kill = np.zeros(n, bool)
    for i in range(1, n):
        dt = (s['t'][i] - s['t'][i - 1]) * 1e-6
        if not (dt > 0):
            continue
        lim = w_view * math.sin(min(wmax * dt, math.pi / 2)) + 2.0 * KP_SIGMA_PX
        if abs(s['d'][i] - s['d'][i - 1]) <= lim:
            continue
        kill[i - 1] = kill[i] = True
        if i + 1 < n:
            kill[i + 1] = True
    hit = int((kill & s['ok']).sum())
    s['ok'] &= ~kill
    return hit


def median_over(s, a, b):
    m = s['ok'] & (s['t'] >= a) & (s['t'] <= b)
    return cmedian(list(s['d'][m])) if m.any() else 0.0


def interp_at(t_src, v_src, tq):
    return float(np.interp(tq, t_src, v_src))


def pair_inputs(sw, lead, trail, relabel_deg, raw_on_crossing=False):
    """Everything up to the paired separations: the guards, the source choice, the orientation bits,
    the DTL resampled onto the face-on instants. Variant-dependent only through the face-on relabel
    arbiter (V2+)."""
    pin = sw['pin']
    diag = dict(refusal='')
    sfr = separations(sw['fo_raw'], lead, trail); sdr = separations(sw['dtl_raw'], lead, trail)
    if len(sfr['t']) < 3 or len(sdr['t']) < 3:
        diag['refusal'] = 'too few admitted frames'; return None, diag
    tk = pin['takeaway']

    def p95(s):
        m = (s['t'] >= tk) & (s['t'] <= pin['impact'])
        v = list(np.abs(s['d'][m])) if m.sum() >= 3 else list(np.abs(s['d']))
        return cpercentile(v, 0.95)
    w_fo, w_dtl = p95(sfr), p95(sdr)
    dtl_raw_t, dtl_raw_absd = sdr['t'].copy(), np.abs(sdr['d'])
    thr = math.cos(math.radians(relabel_deg if relabel_deg else V2_GENUINE_DEG)) * w_dtl

    def judge(ta, tb):            # V2's rule: genuine iff the DTL sees the line within N° of broadside
        return abs(interp_at(dtl_raw_t, dtl_raw_absd, 0.5 * (ta + tb))) >= thr
    ev_fo = []
    a0, a1 = pin['address'], pin['address'] + ADDR_WINDOW_US
    rf = deswap(sfr, a0, a1, w_fo, False, arbiter=judge if relabel_deg else None, events=ev_fo, judge=judge)
    rd = deswap(sdr, a0, a1, w_dtl, DTL_LOCAL_GAP)
    dom_ = sw['dom']
    ev_dom = [(t, c, a) for t, c, a in ev_fo if dom_['from'] <= t <= dom_['to']]
    ratios = [abs(interp_at(dtl_raw_t, dtl_raw_absd, t)) / w_dtl for t, _, _ in ev_dom]
    diag.update(dom_signchanges=len(ev_dom), dom_collapse_relabels=sum(1 for _, c, _ in ev_dom if c),
                dom_v0v2_disagree=sum(1 for _, c, a in ev_dom if a is not None and c != a),
                dom_cross_dtl_ratio_min=min(ratios) if ratios else float('nan'))
    diag.update(swaps_fo=rf['flips'], swaps_dtl=rd['flips'], dropped=rf['dropped'] + rd['dropped'],
                fo_signchanges=len(ev_fo),
                fo_v0v2_disagree=sum(1 for _, c, a in ev_fo if a is not None and c != a),
                fo_events=ev_fo, w_fo=w_fo, w_dtl=w_dtl)
    fo_clean = rf['flips'] == 0 and rf['dropped'] == 0 and sw['fo_sm'] is not None
    # V1+: THE CROSSING IS READ ON THE RAW KEYPOINTS, and a face-on leg that crosses is CONSUMED raw.
    # Across the crossing the raw labels flutter (|d| ~ 25–60 px with the sign alternating for up to
    # 150 ms around the top) and the RTS smoother bridges that flutter into a smooth ramp that
    # overshoots the line's own width — 07-04 s8 reads +168 px against a 130 px p95, i.e. "32° from
    # square" while the DTL sees the line broadside. The collapse rule never fires there (every sign
    # change collapses), so the shipped source choice calls the series clean and consumes the ramp.
    dom = sw['dom']
    mcr = ((sfr['t'] >= dom['draw_from'] - HOLE_PAD_US) & (sfr['t'] <= dom['to'] + HOLE_PAD_US)
           & (np.abs(sfr['d']) < HOLE_FRAC * w_fo))
    diag['fo_crossing'] = int(mcr.any())
    if sw['fo_sm'] is not None:          # how far the SMOOTHED face-on track overshoots the raw line width
        ssm = separations(sw['fo_sm'], lead, trail)
        msm = (ssm['t'] >= dom['from']) & (ssm['t'] <= dom['to'])
        diag['fo_sm_absmax_over_p95'] = float(np.abs(ssm['d'][msm]).max() / w_fo) if msm.any() else float('nan')
    if raw_on_crossing and mcr.any():
        fo_clean = False
    dtl_clean = rd['flips'] == 0 and rd['dropped'] == 0 and sw['dtl_sm'] is not None
    sf = separations(sw['fo_sm'], lead, trail) if fo_clean else sfr
    sd = separations(sw['dtl_sm'], lead, trail) if dtl_clean else sdr
    diag['src'] = ('smoothed' if fo_clean else 'rawDeswapped') + '/' + ('smoothed' if dtl_clean else 'rawDeswapped')
    diag['rl_fo'] = rate_limit(sf, w_fo); diag['rl_dtl'] = rate_limit(sd, w_dtl)
    if sf['ok'].sum() < 3 or sd['ok'].sum() < 3:
        diag['refusal'] = 'too few usable separations after the guards'; return None, diag
    mF = median_over(sf, a0, a1); mD = median_over(sd, pin['top'] - 150000, pin['top'] + 50000)
    if not abs(mF) > 0 or not abs(mD) > 0:
        diag['refusal'] = 'no orientation'; return None, diag
    sgnF = 1.0 if mF > 0 else -1.0; sgnD = 1.0 if mD > 0 else -1.0
    gaps = [float(sd['t'][i] - sd['t'][i - 1]) for i in range(1, len(sd['t']))]
    dstep = cmedian(gaps) if gaps else 0.0
    max_br = PAIR_MAX_GAP_FRAMES * dstep if dstep > 0 else 0.0
    if not max_br > 0:
        diag['refusal'] = 'no DTL frame interval'; return None, diag
    tD = sd['t'][sd['ok']]; vD = sd['d'][sd['ok']]; gD = sd['sigma'][sd['ok']]
    if len(tD) < 3:
        diag['refusal'] = 'too few DTL separations'; return None, diag
    n = len(sf['t'])
    xp = np.full(n, np.nan); yp = np.full(n, np.nan); sx = np.zeros(n); sy = np.zeros(n)
    k = 0
    for i in range(n):
        t = sf['t'][i]
        if not sf['ok'][i] or t < tD[0] or t > tD[-1]:
            continue
        while k + 1 < len(tD) and tD[k + 1] < t:
            k += 1
        if tD[k] == t:
            dD, sD = vD[k], gD[k]
        elif k + 1 < len(tD):
            lim = max(max_br, PAIR_MAX_GAP_FRAMES * local_gap_us(tD, k)) if DTL_LOCAL_GAP else max_br
            if tD[k + 1] - tD[k] > lim:
                continue
            f = (t - tD[k]) / (tD[k + 1] - tD[k])
            dD = vD[k] + f * (vD[k + 1] - vD[k]); sD = gD[k] + f * (gD[k + 1] - gD[k])
        else:
            continue
        xp[i] = sgnF * sf['d'][i]; yp[i] = sgnD * dD; sx[i] = sf['sigma'][i]; sy[i] = sD
    diag.update(sgnF=int(sgnF), sgnD=int(sgnD))
    return dict(t=sf['t'], xp=xp, yp=yp, sx=sx, sy=sy, fo_t=sf['t'], fo_d=sgnF * sf['d'], fo_ok=sf['ok'],
                fo_raw_t=sfr['t'], fo_raw_absd=np.abs(sfr['d']),
                dtl_t=tD, dtl_d=sgnD * vD, w_fo=w_fo, w_dtl=w_dtl), diag


def crossing_holes(P, dom):
    """V1: the face-on 90° crossing — RAW face-on samples with |d| < 0.35·W_fo (the view's own p95),
    grouped into episodes (gaps ≤ 3 frames), each padded by one derivative window."""
    t = P['fo_raw_t']; d = P['fo_raw_absd']
    lo, hi = dom['draw_from'] - HOLE_PAD_US, dom['to'] + HOLE_PAD_US
    idx = [i for i in range(len(t)) if lo <= t[i] <= hi and abs(d[i]) < HOLE_FRAC * P['w_fo']]
    if not idx:
        return []
    step = np.median(np.diff(t))
    eps = []; s = e = t[idx[0]]
    for i in idx[1:]:
        if t[i] - e <= 3 * step:
            e = t[i]
        else:
            eps.append((s, e)); s = e = t[i]
    eps.append((s, e))
    return [(int(a - HOLE_PAD_US), int(b + HOLE_PAD_US)) for a, b in eps]


def angle_track(P, W, Wd, gamma_deg, hole_mask):
    """ψ from the paired separations. x = xp/W, y = yp/Wd; γ = 90 is V0's atan2(y, x)."""
    t = P['t']; n = len(t)
    g = math.radians(gamma_deg); c, s = math.cos(g), math.sin(g)
    x = P['xp'] / W; y = P['yp'] / Wd; sxn = P['sx'] / W; syn = P['sy'] / Wd
    u = (y - x * c) / s
    den = x * x + u * u
    valid = np.isfinite(x) & np.isfinite(y) & (den > 0)
    if hole_mask is not None:
        valid &= ~hole_mask
    ang = np.zeros(n); sig = np.zeros(n)
    ang[valid] = np.unwrap(np.arctan2(u[valid], x[valid]))
    num = (x / s) ** 2 * syn ** 2 + (x * c / s + u) ** 2 * sxn ** 2
    sig[valid] = np.sqrt(num[valid]) / den[valid]
    return dict(t=[int(v) for v in t], ang=ang, sig=sig, valid=valid)


def consistency_fit(P, dom, hole_mask):
    """K0 §2.1: d_dtl = W'·cos(θ_fo − γ), θ_fo FACE-ON'S OWN angle — acos(x / W_peak) with W_peak the
    face-on maximum (a ±12.5 ms local median) over the domain, closed before that square-up instant,
    open after. Linear in W'(cos γ, sin γ). Over Top → min(P8, impact + 100 ms)."""
    t = P['t']; xpx = P['xp']
    m_sq = np.isfinite(xpx) & (t >= dom['from']) & (t <= dom['to'])
    if hole_mask is not None:
        m_sq &= ~hole_mask
    if m_sq.sum() < 10:
        return None
    # the face-on square-up: maximum of a ±12.5 ms local median of x over the domain
    ti = t[m_sq]; xi = xpx[m_sq]
    lm = np.array([np.median(xi[np.abs(ti - tt) <= DERIV_WINDOW_US // 2]) for tt in ti])
    t_sq = int(ti[int(np.argmax(lm))]); W_peak = float(lm.max())
    x = xpx / W_peak
    th = np.arccos(np.clip(x, -1, 1)); th = np.where(t < t_sq, th, -th)
    m = m_sq & np.isfinite(P['yp']) & (t <= min(dom['to'], dom['impact'] + 100000))
    if m.sum() < 10:
        return None
    M = np.column_stack([np.cos(th[m]), np.sin(th[m])]); yy = P['yp'][m]
    AB, *_ = np.linalg.lstsq(M, yy, rcond=None)
    pred = M @ AB
    ssr = float(((yy - pred) ** 2).sum()); sst = float(((yy - yy.mean()) ** 2).sum())
    return dict(gamma=math.degrees(math.atan2(AB[1], AB[0])), Wd=float(math.hypot(*AB)),
                r2=1 - ssr / sst if sst > 0 else float('nan'), t_sq_fo=t_sq, W_peak=W_peak,
                rms=math.sqrt(ssr / m.sum()))


def still_sigma(v, lead, trail, t_end):
    """V5: σ of a view's separation over the STILL part of the pre-takeaway recording — the lower
    quartile of the line-detrended residual σ of every 150 ms window (≥ 5 samples) before takeaway.
    Not the minimum: the minimum of many small-sample σ is biased low by construction."""
    s = separations(v, lead, trail)
    m = s['t'] <= t_end
    t = s['t'][m]; d = s['d'][m]
    vals = []
    for i in range(len(t)):
        w = (t >= t[i]) & (t <= t[i] + STILL_WIN_US)
        if w.sum() < STILL_MIN_N:
            continue
        tt = (t[w] - t[i]) * 1e-6; yy = d[w]
        A = np.column_stack([np.ones_like(tt), tt])
        c, *_ = np.linalg.lstsq(A, yy, rcond=None)
        vals.append(float(np.std(yy - A @ c, ddof=2)))
    return max(float(np.percentile(vals, 25)), 0.3) if vals else float('nan')


# ══ One swing ══════════════════════════════════════════════════════════════════════════════════

def load(session, swing):
    path = f'{CORPUS}/{session}/{swing}'
    doc = load_swing(path)
    a = doc['analysis']
    (FW, FH), (DW, DH) = frame_dims(doc)
    ph = a['phases']
    impact = phase_time(ph, PH_IMPACT, int((a.get('kinematicSequence') or {}).get('impactUs', -1)))
    dom_from = phase_time(ph, PH_TRANSITION, phase_time(ph, PH_TOP, -1))
    p8 = phase_time(ph, PH_P8, -1)
    dom = dict(**{'from': dom_from}, to=p8 if p8 > impact else impact, impact=impact,
               draw_from=phase_time(ph, PH_TOP, -1))
    address = phase_time(ph, PH_ADDRESS, -1)
    pin = dict(address=address, takeaway=phase_time(ph, PH_TAKEAWAY, address),
               top=phase_time(ph, PH_TOP, dom_from), impact=impact)
    P2, PD = a['pose2d'], a['poseDtl']
    sw = dict(session=session, swing=swing, doc=doc, dom=dom, pin=pin,
              fo_raw=view(P2['frames'], FW, FH), dtl_raw=view(PD['frames'], DW, DH),
              fo_sm=view(P2['smoothed'], FW, FH, True) if len(P2.get('smoothed', [])) >= 2 else None,
              dtl_sm=view(PD['smoothed'], DW, DH, True) if len(PD.get('smoothed', [])) >= 2 else None,
              dims=(FW, FH, DW, DH))
    ef, nf = address_extent(sw['fo_raw'], address, address + ADDR_WINDOW_US)
    ed, nd = address_extent(sw['dtl_raw'], address, address + ADDR_WINDOW_US)
    sw['scale'] = dict(ok=nf >= ADDR_MIN_FRAMES and nd >= ADDR_MIN_FRAMES and ef >= PAIR_MIN_EXTENT_PX
                       and ed >= PAIR_MIN_EXTENT_PX, r=ed / ef if ef > 0 else float('nan'), ext_fo=ef, ext_dtl=ed)
    return sw


def sync_offset(sw, kp):
    """The hard sync check: the DTL time offset δ that best maps the face-on lead-joint vertical onto
    the DTL one (y_fo(t) ≈ a + b·y_dtl(t + δ)) over Top → Impact + 50 ms. +δ ⇒ the DTL stamps late."""
    f, d = sw['fo_raw'], sw['dtl_raw']
    lo, hi = sw['pin']['top'], sw['pin']['impact'] + 50000
    mf = (f['t'] >= lo) & (f['t'] <= hi) & (f['c'][:, kp] >= CONF_MIN)
    md = d['c'][:, kp] >= CONF_MIN
    tf, yf = f['t'][mf].astype(float), f['y'][mf, kp]
    td, yd = d['t'][md].astype(float), d['y'][md, kp]
    if len(tf) < 10 or len(td) < 10:
        return float('nan')
    deltas = np.arange(-30000, 30001, 250.0)
    rms = []
    for dl in deltas:
        q = tf + dl
        ok = (q >= td[0]) & (q <= td[-1])
        if ok.sum() < 10:
            rms.append(np.inf); continue
        yq = np.interp(q[ok], td, yd)
        A = np.column_stack([np.ones(ok.sum()), yq])
        c, *_ = np.linalg.lstsq(A, yf[ok], rcond=None)
        rms.append(float(np.sqrt(np.mean((yf[ok] - A @ c) ** 2))))
    rms = np.array(rms); i = int(np.argmin(rms))
    if 0 < i < len(rms) - 1 and np.isfinite(rms[i - 1]) and np.isfinite(rms[i + 1]):
        a, b, c = rms[i - 1], rms[i], rms[i + 1]
        den = a - 2 * b + c
        off = 0.5 * (a - c) / den if den > 0 else 0.0
        return float((deltas[i] + off * 250.0) / 1000.0)
    return float(deltas[i] / 1000.0)


def triangulated(sw, ja, jb):
    """The skeleton3d line's bearing, leadSign·dψ/dt, placed exactly as the pair is (may = true)."""
    sk = load_skeleton(sw['doc'])
    if sk is None:
        return None
    psi, ok = line_bearing(sk, ja, jb)
    t = [int(round(x)) for x in sk['t']]
    ang = np.where(ok, psi, 0.0)
    if ok.sum() < 3:
        return None
    ang[ok] = np.unwrap(psi[ok])
    at = dict(t=t, ang=ang, sig=np.where(ok, SKEL_ANGLE_SIGMA_RAD, 0.0), valid=ok)
    dom = sw['dom']
    bf, bt = validity_band(at, dom['from'], dom['to'])
    nd = finish_channel(differentiate(at, +1.0), dom,
                        dict(may=True, spike_guard=True, mode='cpp', blind_from=bf, blind_to=bt))
    return nd


def run_variant(sw, seg, lead, trail, vname, P=None, diag=None, scales=None, gamma_override=None,
                sigma_override=None):
    """One segment, one variant. Returns (node, info). `scales` = (fx, fy) multiplies W and W'
    (the G-f perturbations); `gamma_override` replaces γ; `sigma_override` = (σ_fo, σ_dtl) px (V5)."""
    dom = sw['dom']
    spec = SPECS[vname]
    info = dict(variant=vname)
    if not sw['scale']['ok']:
        info['refusal'] = 'pair scale refused'; return None, info
    if P is None:
        P, diag = pair_inputs(sw, lead, trail, spec['relabel'], raw_on_crossing=spec['hole'])
    info.update({k: v for k, v in diag.items() if k != 'fo_events'})
    if P is None:
        return None, info
    if sigma_override is not None:
        P = dict(P); P['sx'] = np.where(np.isfinite(P['xp']), sigma_override[0], 0.0)
        P['sy'] = np.where(np.isfinite(P['yp']), sigma_override[1], 0.0)
    t = P['t']; r = sw['scale']['r']
    # the face-on PARITY check: at impact the shoulders are near square to the face-on camera, so the
    # consumed, oriented face-on separation must be positive there (labels as at address)
    mi = np.isfinite(P['xp']) & (np.abs(t - dom['impact']) <= 20000)
    info['fo_x_at_impact_px'] = float(np.median(P['xp'][mi])) if mi.any() else float('nan')
    # ── the C++ diagnostics and its one gate (corr) — computed on V0's geometry, as shipped ──
    valid0 = np.isfinite(P['xp']) & np.isfinite(P['yp'])
    m_tk = valid0 & (t >= sw['pin']['takeaway']) & (t <= sw['pin']['impact'])
    absF = list(np.abs(P['xp'][m_tk])); absD = list(np.abs(P['yp'][m_tk]))
    Wd95 = cpercentile(absF, 0.95)
    m_dom = valid0 & (t >= dom['from']) & (t <= dom['to'])
    yAbs = np.abs(P['yp'][m_dom]) / r
    pred = np.sqrt(np.maximum(0.0, Wd95 ** 2 - P['xp'][m_dom] ** 2))
    info['corr_abs'] = abs(pearson(list(yAbs), list(pred)))
    info['n_paired'] = int(m_dom.sum())
    in_dom = (t >= dom['from']) & (t <= dom['to'])
    info['invalid_frac'] = float(1 - m_dom.sum() / max(1, in_dom.sum()))
    info['amp_ratio_v0'] = (cpercentile(absD, 0.95) / r) / Wd95 if Wd95 > 0 else float('nan')
    if info['n_paired'] < ADDR_MIN_FRAMES or info['corr_abs'] < PAIR_MIN_CORR:
        info['refusal'] = 'corr / paired floor'; return None, info
    # ── holes (V1+) ──
    cross = crossing_holes(P, dom)
    info['hole'] = ';'.join(f'{(a - dom["impact"]) / 1000:.0f}..{(b - dom["impact"]) / 1000:.0f}' for a, b in cross)
    hole_mask = None
    if spec['hole'] and cross:
        hole_mask = np.zeros(len(t), bool)
        for a, b in cross:
            hole_mask |= (t >= a) & (t <= b)
    # ── scales (V3+) and γ (V4) ──
    W, Wd, gamma = 1.0, r, 90.0
    m_ds = valid0 & (t >= dom['from']) & (t <= dom['impact'])
    if hole_mask is not None:
        m_ds &= ~hole_mask
    Wf, Wdf, _ = fit_axis_aligned(P['xp'][m_ds], P['yp'][m_ds]) if m_ds.sum() >= 10 else (None, None, None)
    info['W_fit'] = Wf if Wf else float('nan'); info['Wd_fit'] = Wdf if Wdf else float('nan')
    info['ratio_fit_over_r'] = (Wdf / Wf) / r if (Wf and Wdf) else float('nan')
    if spec['ellipse']:
        if not (Wf and Wdf):
            info['refusal'] = 'ellipse fit failed'; return None, info
        W, Wd = Wf, Wdf
    cf = consistency_fit(P, dom, hole_mask)
    if cf:
        info.update(gamma_fit=cf['gamma'], r2=cf['r2'], cons_rms_px=cf['rms'], cons_Wd=cf['Wd'],
                    W_peak=cf['W_peak'])
    if spec['gamma']:
        if cf is None:
            info['refusal'] = 'consistency fit failed'; return None, info
        if gamma_override is None and not (GAMMA_LO <= cf['gamma'] <= GAMMA_HI and cf['r2'] >= R2_MIN):
            info['refusal'] = f"gamma {cf['gamma']:.1f} / R2 {cf['r2']:.3f} refused"; return None, info
        # under γ ≠ 90 the axis-aligned ellipse is the wrong model: K0 §2.1's own triple instead
        W, Wd, gamma = cf['W_peak'], cf['Wd'], cf['gamma']
    if scales is not None:
        W *= scales[0]; Wd *= scales[1]
    if gamma_override is not None:
        gamma = gamma_override
    info['gamma_used'] = gamma
    # closure over the downswing on the variant's own geometry
    if spec['ellipse']:
        xx, yy = P['xp'] / W, P['yp'] / Wd
    else:
        xx, yy = P['xp'] / Wd95, P['yp'] / (r * Wd95)
    gc = math.cos(math.radians(gamma)); gs = math.sin(math.radians(gamma))
    clo = np.abs(xx ** 2 - 2 * xx * yy * gc + yy ** 2 - gs ** 2) / gs ** 2
    mc = m_ds & np.isfinite(clo)
    info['closure_p50'] = float(np.percentile(clo[mc], 50)) if mc.sum() else float('nan')
    info['closure_p90'] = float(np.percentile(clo[mc], 90)) if mc.sum() else float('nan')
    # ── the angle, the rate, the node ──
    at = angle_track(P, W, Wd, gamma, hole_mask)
    if at['valid'].sum() < 3:
        info['refusal'] = 'too few paired samples'; return None, info
    rt = differentiate(at, -1.0)
    may = True                     # the study evaluates the ring as if it were switched on
    if vname == 'V0':
        bf, bt = validity_band(at, dom['from'], dom['to'])
        gate = dict(may=may, spike_guard=True, mode='cpp', blind_from=bf, blind_to=bt)
    else:
        gate = dict(may=may, spike_guard=True, mode='holes', holes=holes_of(at, dom['from'], dom['to'], cross))
    nd = finish_channel(rt, dom, gate)
    nd['segment'] = seg
    info['cross'] = cross
    info['P'] = P; info['at'] = at
    return nd, info


def shift_dtl(sw, dt_us):
    """A copy of the swing with every DTL pose stamp moved by dt_us (the sync sensitivity)."""
    sw2 = dict(sw)
    for k in ('dtl_raw', 'dtl_sm'):
        if sw[k] is not None:
            v = dict(sw[k]); v['t'] = sw[k]['t'] + int(dt_us); sw2[k] = v
    return sw2


def reversal_distance_ms(nd, dom):
    """G-b: distance from the peak to the nearest sign change of the rate in the domain, either side."""
    t, v, _, ok = nd['series']
    ti, vi = t[ok], v[ok]
    best = float('inf')
    for i in range(1, len(ti)):
        if vi[i - 1] * vi[i] < 0 or (vi[i] == 0.0):
            f = vi[i - 1] / (vi[i - 1] - vi[i]) if vi[i - 1] != vi[i] else 0.0
            tz = ti[i - 1] + f * (ti[i] - ti[i - 1])
            best = min(best, abs(nd['t_peak'] - tz) / 1000.0)
    return best


def adjacent_to_crossing(nd, cross):
    """G-b: the peak inside the face-on crossing hole, or within one derivative window of its edges."""
    for a, b in cross:
        if a - DERIV_WINDOW_US <= nd['t_peak'] <= b + DERIV_WINDOW_US:
            return True
    return False


def analyse_swing(sw, variants, want_pert=True):
    rows = []
    dom = sw['dom']; imp = dom['impact']
    arm = doc_node(sw['doc'], 'leadArm'); club = doc_node(sw['doc'], 'club')
    for n in (arm, club):
        if n:
            n['segment'] = n['segment']
    sync_w = sync_offset(sw, LW); sync_e = sync_offset(sw, LE)
    shipped = {s: doc_node(sw['doc'], s) for s in ('pelvis', 'thorax')}
    tri = {seg: triangulated(sw, ja, jb) for seg, _, _, ja, jb in SEGS}
    out_nodes = {}
    for v in variants:
        vi = v
        per_seg = {}
        for seg, a, b, _, _ in SEGS:
            nd, info = run_variant(sw, seg, a, b, vi)
            per_seg[seg] = (nd, info)
        for seg, a, b, _, _ in SEGS:
            nd, info = per_seg[seg]
            row = dict(session=sw['session'][:10], swing=sw['swing'], segment=seg, variant=v,
                       fo_frame=f"{sw['dims'][0]}x{sw['dims'][1]}", dtl_frame=f"{sw['dims'][2]}x{sw['dims'][3]}",
                       r_vertical=sw['scale']['r'], sync_wrist_ms=sync_w, sync_elbow_ms=sync_e)
            for k in ('refusal', 'src', 'swaps_fo', 'swaps_dtl', 'dropped', 'rl_fo', 'rl_dtl', 'fo_signchanges',
                      'fo_v0v2_disagree', 'fo_crossing', 'dom_signchanges', 'dom_collapse_relabels',
                      'dom_v0v2_disagree', 'dom_cross_dtl_ratio_min', 'fo_sm_absmax_over_p95', 'fo_x_at_impact_px', 'corr_abs', 'n_paired', 'invalid_frac', 'amp_ratio_v0', 'W_fit',
                      'Wd_fit', 'ratio_fit_over_r', 'W_peak', 'cons_Wd', 'gamma_fit', 'r2', 'cons_rms_px', 'gamma_used',
                      'closure_p50', 'closure_p90', 'hole'):
                if k in info:
                    row[k] = info[k]
            if nd is None:
                row['state'] = 'refused'
            else:
                row['state'] = nd['state']
                if nd['ok']:
                    row['peak_rel_impact_ms'] = (nd['t_peak'] - imp) / 1000.0
                    row['peak_dps'] = nd['peak']; row['t_sigma_ms'] = nd['t_sigma_ms']
                    row['spike_flag'] = int(nd['spike'])
                    row['reversal_dist_ms'] = reversal_distance_ms(nd, dom)
                    row['adj_hole'] = int(adjacent_to_crossing(nd, info.get('cross', [])))
                    row['bound_no_earlier_ms'] = nd['no_earlier']; row['bound_no_later_ms'] = nd['no_later']
                # the DTL square crossing of this segment (signed DTL separation, closed + → open −)
                P = info['P']
                tz = zero_crossing(P['dtl_t'] / 1e6, P['dtl_d'], dom['from'] / 1e6, dom['to'] / 1e6)
                row['dtl_square_rel_impact_ms'] = (tz * 1e6 - imp) / 1000.0 if math.isfinite(tz) else float('nan')
                if want_pert and nd['ok']:
                    t_ser = nd['series'][0]

                    def frames_between(ta, tb):      # face-on samples strictly after min, up to max
                        lo_, hi_ = min(ta, tb), max(ta, tb)
                        return int(((t_ser > lo_) & (t_ser <= hi_)).sum())
                    shifts = []; fsh = []
                    for sc in ((1 - SCALE_PERT, 1), (1 + SCALE_PERT, 1), (1, 1 - SCALE_PERT), (1, 1 + SCALE_PERT)):
                        n2, _ = run_variant(sw, seg, a, b, vi, P=P, diag=info_diag(info), scales=sc)
                        if n2 is not None and n2['ok']:
                            shifts.append(abs(n2['t_peak'] - nd['t_peak']) / 1000.0)
                            fsh.append(frames_between(n2['t_peak'], nd['t_peak']))
                        else:
                            shifts.append(float('inf')); fsh.append(10 ** 6)
                    row['inv_scale_max_ms'] = max(shifts); row['inv_scale_max_frames'] = max(fsh)
                    gsh = []; gfr = []
                    for g in GAMMAS_PERT:
                        n2, _ = run_variant(sw, seg, a, b, vi, P=P, diag=info_diag(info), gamma_override=g)
                        ok2 = n2 is not None and n2['ok']
                        gsh.append(abs(n2['t_peak'] - nd['t_peak']) / 1000.0 if ok2 else float('inf'))
                        gfr.append(frames_between(n2['t_peak'], nd['t_peak']) if ok2 else 10 ** 6)
                    row['inv_gamma_max_ms'] = max(gsh); row['inv_gamma_max_frames'] = max(gfr)
                    row['inv_max_ms'] = max(row['inv_scale_max_ms'], row['inv_gamma_max_ms'])
                    row['inv_max_frames'] = max(row['inv_scale_max_frames'], row['inv_gamma_max_frames'])
                    # the clock: move the DTL stamps and re-run the whole variant
                    for sh_us in DTL_SHIFTS_US:
                        n3, _ = run_variant(shift_dtl(sw, sh_us), seg, a, b, vi)
                        row[f'dtlshift_{-sh_us // 1000}ms_dt_ms'] = ((n3['t_peak'] - nd['t_peak']) / 1000.0
                                                                   if (n3 is not None and n3['ok']) else float('nan'))
                    # V5: σ from the stillest pre-takeaway window of each view
                    sf = still_sigma(sw['fo_raw'], a, b, sw['pin']['takeaway'])
                    sd_ = still_sigma(sw['dtl_raw'], a, b, sw['pin']['takeaway'])
                    row['still_sigma_fo_px'] = sf; row['still_sigma_dtl_px'] = sd_
                    if math.isfinite(sf) and math.isfinite(sd_):
                        n5, _ = run_variant(sw, seg, a, b, vi, P=P, diag=info_diag(info), sigma_override=(sf, sd_))
                        if n5 is not None and n5['ok']:
                            row['v5_t_sigma_ms'] = n5['t_sigma_ms']; row['v5_state'] = n5['state']
            # shipped node, triangulated node
            sh = shipped[seg]
            if sh:
                row['shipped_route'] = sh['routeId']; row['shipped_placed'] = int(sh['placed'])
                row['shipped_peak_rel_impact_ms'] = (sh['t_peak'] - imp) / 1000.0
                row['shipped_peak_dps'] = sh['peak']
                if nd is not None and nd['ok']:
                    row['d_t_vs_shipped_ms'] = (nd['t_peak'] - sh['t_peak']) / 1000.0
                    row['d_peak_vs_shipped_rel'] = (nd['peak'] - sh['peak']) / abs(sh['peak']) if sh['peak'] else float('nan')
                ser = doc_series(sw['doc'], f'{seg}AngularSpeed')
                if ser and nd is not None and sh['routeId'] == 'faceOn+dtl':
                    st = np.array([int(round(x)) for x in ser['t_us']]); sv = np.array(ser['value'], float)
                    t2, v2, _, ok2 = nd['series']
                    if len(st) == len(t2) and np.all(st == t2):
                        sval = np.array(ser.get('valid') or [1] * len(st), bool)
                        mm = sval & ok2
                        row['series_max_abs_diff_dps'] = float(np.max(np.abs(sv[mm] - v2[mm]))) if mm.any() else float('nan')
                    else:
                        row['series_max_abs_diff_dps'] = float('inf')
            tn = tri[seg]
            if tn is not None and tn['ok']:
                row['tri_state'] = tn['state']; row['tri_peak_rel_impact_ms'] = (tn['t_peak'] - imp) / 1000.0
                row['tri_peak_dps'] = tn['peak']; row['tri_t_sigma_ms'] = tn['t_sigma_ms']
            if arm:
                row['arm_peak_rel_impact_ms'] = (arm['t_peak'] - imp) / 1000.0; row['arm_placed'] = int(arm['placed'])
            if club:
                row['club_peak_rel_impact_ms'] = (club['t_peak'] - imp) / 1000.0; row['club_placed'] = int(club['placed'])
            rows.append(row)
        # order with the shipped arm and club
        ns = [per_seg[s][0] for s in ('pelvis', 'thorax')]
        for n_ in (arm, club):
            if n_:
                n_['segment'] = n_['segment']
        order, resolved = resolve([x for x in ns if x is not None] + [x for x in (arm, club) if x])
        for row in rows[-2:]:
            row['order'] = order; row['order_resolved'] = int(resolved)
        out_nodes[v] = per_seg
    return rows, out_nodes, tri


def info_diag(info):
    return {k: info[k] for k in ('refusal', 'src', 'swaps_fo', 'swaps_dtl', 'dropped', 'rl_fo', 'rl_dtl',
                                 'fo_signchanges', 'fo_v0v2_disagree', 'w_fo', 'w_dtl', 'sgnF', 'sgnD',
                                 'fo_crossing', 'dom_signchanges', 'dom_collapse_relabels', 'dom_v0v2_disagree',
                                 'dom_cross_dtl_ratio_min', 'fo_sm_absmax_over_p95')
            if k in info}


# ══ Gates ══════════════════════════════════════════════════════════════════════════════════════

def iqr(v):
    return float(np.percentile(v, 75) - np.percentile(v, 25)) if len(v) >= 2 else float('nan')


def session_gates(rows, variant, session, seg='thorax'):
    R = [r for r in rows if r['variant'] == variant and r['session'] == session[:10] and r['segment'] == seg]
    pl = [r for r in R if r['state'] == 'placed']
    tpk = [r['peak_rel_impact_ms'] for r in pl]
    g = dict(n=len(R), placed=len(pl), bounded=sum(r['state'] == 'bounded' for r in R),
             neither=sum(r['state'] == 'neither' for r in R), refused=sum(r['state'] == 'refused' for r in R))
    g['Gb'] = all(r['reversal_dist_ms'] >= GB_REVERSAL_MS and not r['adj_hole'] for r in pl)
    g['Gb_fail'] = sum(1 for r in pl if not (r['reversal_dist_ms'] >= GB_REVERSAL_MS and not r['adj_hole']))
    g['Gc'] = all(GC_LO <= r['peak_dps'] <= GC_HI for r in pl)
    g['Gc_fail'] = sum(1 for r in pl if not (GC_LO <= r['peak_dps'] <= GC_HI))
    g['iqr_ms'] = iqr(tpk)
    g['Gd'] = len(tpk) >= 3 and g['iqr_ms'] <= GD_IQR_MS
    g['half'] = len(pl) >= 0.5 * len(R) and len(R) > 0
    inv = [r.get('inv_max_ms', float('inf')) for r in pl]
    invf = [r.get('inv_max_frames', 10 ** 6) for r in pl]
    g['inv_max_ms'] = max(inv) if inv else float('nan')
    g['inv_max_frames'] = max(invf) if invf else float('nan')
    g['Gf'] = all(x <= 1 for x in invf)                 # ≤ 1 frame (6.7 ms at 149 fps)
    g['Gf_fail'] = sum(1 for x in invf if not x <= 1)
    g['Gf_scale_fail'] = sum(1 for r in pl if r.get('inv_scale_max_frames', 10 ** 6) > 1)
    g['Gf_gamma_fail'] = sum(1 for r in pl if r.get('inv_gamma_max_frames', 10 ** 6) > 1)
    g['peak_med_ms'] = float(np.median(tpk)) if tpk else float('nan')
    g['peak_dps_med'] = float(np.median([r['peak_dps'] for r in pl])) if pl else float('nan')
    g['t_sigma_med'] = float(np.median([r['t_sigma_ms'] for r in pl])) if pl else float('nan')
    return g


def pelvis_ok(rows, variant, session):
    def get(v):
        return {r['swing']: r for r in rows if r['variant'] == v and r['session'] == session[:10]
                and r['segment'] == 'pelvis'}
    b, c = get('V0'), get(variant)
    nb = sum(r['state'] == 'placed' for r in b.values()); nc = sum(r['state'] == 'placed' for r in c.values())
    d = [abs(c[s]['peak_rel_impact_ms'] - b[s]['peak_rel_impact_ms']) for s in b
         if s in c and b[s]['state'] == 'placed' and c[s]['state'] == 'placed']
    med = float(np.median(d)) if d else 0.0
    return dict(placed=nc, placed_v0=nb, med_dt_ms=med, ok=nc >= nb and med <= FRAME_MS)


def gate_a(rows, sessions):
    out = []
    for r in rows:
        if r['variant'] != 'V0' or r['segment'] != 'pelvis' or r['session'] not in [s[:10] for s in sessions]:
            continue
        if r.get('shipped_route') == 'faceOn+dtl':
            ok = (r['state'] in ('placed', 'bounded') and abs(r.get('d_t_vs_shipped_ms', 1e9)) <= 1.0
                  and abs(r.get('d_peak_vs_shipped_rel', 1e9)) <= 0.03
                  and int(r['state'] == 'placed') == r['shipped_placed'])
        else:
            ok = r['state'] in ('refused', 'neither')
        out.append((r['session'], r['swing'], ok))
    return out


def gate_a_thorax(rows, sessions):
    """Secondary reproduction: with the ring OFF as shipped, the pair thorax must have stood down
    (neither placed nor bounded) on every swing whose shipped thorax is not the pair."""
    out = []
    for r in rows:
        if r['variant'] != 'V0' or r['segment'] != 'thorax' or r['session'] not in [s[:10] for s in sessions]:
            continue
        if r.get('shipped_route') != 'faceOn+dtl':
            out.append((r['swing'], r['state'] in ('refused', 'neither', 'placed')))  # placed ⇒ ring off ⇒ withdrawn
    return out


def select_variant(rows, sessions):
    """THE FREEZE. Pre-registered (module docstring)."""
    table = {}
    for v in VARIANTS:
        checks = []
        for s in sessions:
            g = session_gates(rows, v, s)
            pv = pelvis_ok(rows, v, s)
            checks += [g['Gb'], g['Gc'], g['Gd'], g['Gf'], g['half'], pv['ok']]
        table[v] = (all(checks), sum(checks), len(checks))
    passing = [v for v in VARIANTS if table[v][0]]
    if passing:
        return passing[0], table, True
    best = max(VARIANTS, key=lambda v: (table[v][1], -VARIANTS.index(v)))
    return best, table, False


# ══ Figures ════════════════════════════════════════════════════════════════════════════════════

def figures(figdir, swings_nodes, rows, frozen):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    INK, MUTED, GRID = '#1f2328', '#6e7781', '#d8dee4'
    C = {'V0': '#8c959f', 'V2': '#eb6834', 'V4s': '#1baf7a'}
    C[frozen] = '#2a78d6'
    SHOW = [v for v in ('V0', 'V2', frozen, 'V4s') if v in C]
    SHOW = list(dict.fromkeys(SHOW))
    plt.rcParams.update({'font.size': 9, 'axes.edgecolor': MUTED, 'axes.labelcolor': INK,
                         'xtick.color': MUTED, 'ytick.color': MUTED, 'axes.spines.top': False,
                         'axes.spines.right': False, 'axes.grid': True, 'grid.color': GRID,
                         'grid.linewidth': 0.6, 'legend.frameon': False})
    paths = []
    # 1. per-swing anatomy, for the two example swings
    for (sess, swing), (sw, nodes) in swings_nodes.items():
        dom = sw['dom']; imp = dom['impact']
        fig, ax = plt.subplots(3, 1, figsize=(8.0, 8.2), sharex=True)
        nd0, i0 = nodes['V0']['thorax']; ndf, iff = nodes[frozen]['thorax']
        if ndf is None:
            plt.close(fig); continue
        P = iff['P']
        ms = lambda t: (np.asarray(t, float) - imp) / 1000.0
        if 'P' in i0:
            P0 = i0['P']; m0 = (P0['fo_t'] >= dom['draw_from'] - 50000) & (P0['fo_t'] <= dom['to'] + 20000)
            ax[0].plot(ms(P0['fo_t'][m0]), P0['fo_d'][m0], color=MUTED, lw=1.2,
                       label=f'face-on shoulder d, V0 source ({i0.get("src", "?").split("/")[0]})')
        m = (P['fo_t'] >= dom['draw_from'] - 50000) & (P['fo_t'] <= dom['to'] + 20000)
        ax[0].plot(ms(P['fo_t'][m]), P['fo_d'][m], '.', color=C[frozen], ms=3.5,
                   label=f'face-on shoulder d, {frozen} source ({iff.get("src", "?").split("/")[0]})')
        bad = m & ~P['fo_ok']
        ax[0].plot(ms(P['fo_t'][bad]), P['fo_d'][bad], 'x', color='#e34948', ms=4, label='guard-dropped')
        md = (P['dtl_t'] >= dom['draw_from'] - 50000) & (P['dtl_t'] <= dom['to'] + 20000)
        ax[0].plot(ms(P['dtl_t'][md]), P['dtl_d'][md] / sw['scale']['r'], color='#eb6834', lw=1.2,
                   label='DTL shoulder d / r (px)')
        for a, b in iff['cross']:
            for k in range(3):
                ax[k].axvspan(ms(a), ms(b), color='#fff8c5', zorder=0)
        ax[0].axhline(0, color=MUTED, lw=0.6)
        ax[0].set_ylabel('signed separation, px'); ax[0].legend(loc='lower left', fontsize=8)
        for lab, nd, inf, col in (('V0', nd0, i0, C['V0']), (frozen, ndf, iff, C[frozen])):
            if nd is None:
                continue
            at = inf['at']; tt = np.array(at['t']); mv = at['valid'] & (tt >= dom['draw_from']) & (tt <= dom['to'])
            ax[1].plot(ms(tt[mv]), np.degrees(at['ang'][mv]), '.', ms=2.5, color=col, label=f'ψ {lab}')
            t2, v2, _, ok2 = nd['series']
            ax[2].plot(ms(t2[ok2]), v2[ok2], '-', color=col, lw=1.3, label=f'thorax rate {lab} ({nd["state"]})')
            if nd['ok']:
                ax[2].plot([ms(nd['t_peak'])], [nd['peak']], 'o', color=col, ms=6, mfc='white' if not nd['placed'] else col)
        tn = None
        for k in range(3):
            ax[k].axvline(0, color=INK, lw=0.8)
        ax[1].set_ylabel('ψ, ° (closed +)'); ax[1].legend(loc='upper right', fontsize=8)
        ax[2].set_ylabel('°/s (opening +)'); ax[2].set_xlabel('ms from impact (yellow: face-on 90° crossing hole)')
        ax[2].legend(loc='upper left', fontsize=8)
        ax[0].set_title(f'{sess[:10]} {swing}: thorax, V0 (shipped, ring off) vs frozen {frozen}'
                        + ('' if nd0 is not None else ' — V0 refused this swing'), loc='left', fontsize=10, color=INK)
        fig.tight_layout()
        p = os.path.join(figdir, f'08_thorax_pair_{sess[:10]}_{swing}.png'); fig.savefig(p, dpi=130); plt.close(fig)
        paths.append(p)
    # 2. thorax peak times per variant, per swing
    fig, ax = plt.subplots(figsize=(8.0, 6.4))
    sessions = sorted(set(r['session'] for r in rows))
    ylab = []
    y = 0
    for s in sessions:
        sws = sorted(set(r['swing'] for r in rows if r['session'] == s))
        for swg in sws:
            for j, v in enumerate(SHOW):
                r = [x for x in rows if x['session'] == s and x['swing'] == swg and x['variant'] == v
                     and x['segment'] == 'thorax']
                if not r or 'peak_rel_impact_ms' not in r[0]:
                    continue
                r = r[0]
                ax.plot(r['peak_rel_impact_ms'], y + (j - 1.5) * 0.15, 'o' if r['state'] == 'placed' else 'x',
                        color=C[v], ms=4.5 if r['state'] == 'placed' else 4)
            rr = [x for x in rows if x['session'] == s and x['swing'] == swg and x['segment'] == 'thorax']
            if rr and 'tri_peak_rel_impact_ms' in rr[0]:
                ax.plot(rr[0]['tri_peak_rel_impact_ms'], y, 's', color=INK, ms=3.5, mfc='none')
            if rr and 'dtl_square_rel_impact_ms' in rr[0]:
                ax.plot(rr[0]['dtl_square_rel_impact_ms'], y, '|', color='#cf222e', ms=9)
            ylab.append(f'{s[5:10]} {swg[-2:]}'); y += 1
    ax.set_yticks(range(len(ylab))); ax.set_yticklabels(ylab, fontsize=6.5)
    ax.axvline(0, color=INK, lw=0.8)
    for v in SHOW:
        ax.plot([], [], 'o', color=C[v], label=v + (' (frozen)' if v == frozen else ''))
    ax.plot([], [], 'x', color=MUTED, label='not placed'); ax.plot([], [], 's', color=INK, mfc='none', label='skeleton3d')
    ax.plot([], [], '|', color='#cf222e', ms=9, label='DTL square crossing')
    ax.set_xlabel('thorax peak, ms from impact (V0 shown with its ring switched on)')
    fig.legend(*ax.get_legend_handles_labels(), ncol=7, fontsize=7.5, loc='lower center', frameon=False)
    ax.set_title('Thorax peak instant per swing and variant (dev sessions; held-out = frozen only)', loc='left', fontsize=10)
    fig.tight_layout(rect=(0, 0.06, 1, 1)); p = os.path.join(figdir, '08_thorax_pair_peaks.png'); fig.savefig(p, dpi=130); plt.close(fig)
    paths.append(p)
    # 3. G-g: thorax peak against its DTL square crossing, the pelvis and the arm (frozen variant)
    R = [r for r in rows if r['variant'] == frozen and r['segment'] == 'thorax' and r['state'] == 'placed']
    pel = {(r['session'], r['swing']): r for r in rows if r['variant'] == frozen and r['segment'] == 'pelvis'}
    fig, ax = plt.subplots(1, 3, figsize=(9.0, 3.2), sharey=True)
    marks = {'2026-06-11': 'o', '2026-07-04': 's', '2026-07-03': '^'}
    for r in R:
        mk = marks.get(r['session'], 'o')
        ax[0].plot(r['dtl_square_rel_impact_ms'], r['peak_rel_impact_ms'], mk, color='#0969da', ms=4.5, mfc='none')
        pr = pel.get((r['session'], r['swing']))
        if pr and pr['state'] == 'placed':
            ax[1].plot(pr['peak_rel_impact_ms'], r['peak_rel_impact_ms'], mk, color='#8250df', ms=4.5, mfc='none')
        if 'arm_peak_rel_impact_ms' in r:
            ax[2].plot(r['arm_peak_rel_impact_ms'], r['peak_rel_impact_ms'], mk, color='#1a7f37', ms=4.5, mfc='none')
    for a_, lab in zip(ax, ('thorax DTL square crossing', 'pelvis peak (same variant)', 'lead-arm peak (shipped)')):
        a_.set_xlabel(f'{lab}, ms from impact')
    ax[0].set_ylabel(f'thorax peak ({frozen}), ms from impact')
    for s_, mk in marks.items():
        ax[2].plot([], [], mk, color=MUTED, mfc='none', label=s_)
    ax[2].legend(fontsize=7)
    fig.suptitle('G-g: what the placed thorax peak moves with, shot by shot', x=0.01, ha='left', fontsize=10)
    fig.tight_layout(); p = os.path.join(figdir, '08_thorax_pair_common_mode.png'); fig.savefig(p, dpi=130); plt.close(fig)
    paths.append(p)
    return paths


# ══ Main ═══════════════════════════════════════════════════════════════════════════════════════

def swings_of(session):
    return sorted(d for d in os.listdir(f'{CORPUS}/{session}') if d.startswith('swing_'))


def run_sessions(sessions, variants, keep=()):
    rows, kept = [], {}
    for s in sessions:
        for swg in swings_of(s):
            sw = load(s, swg)
            r, nodes, _ = analyse_swing(sw, variants)
            rows += r
            if (s, swg) in keep:
                kept[(s, swg)] = (sw, nodes)
            print(f'  {s[:10]} {swg}', file=sys.stderr)
    return rows, kept


def common_mode(rows, variant, sessions):
    R = [r for r in rows if r['variant'] == variant and r['segment'] == 'thorax' and r['state'] == 'placed'
         and r['session'] in [s[:10] for s in sessions]]
    pel = {(r['session'], r['swing']): r for r in rows if r['variant'] == variant and r['segment'] == 'pelvis'}
    out = {}
    for key, f in (('dtl_square', lambda r: r.get('dtl_square_rel_impact_ms', float('nan'))),
                   ('pelvis', lambda r: pel[(r['session'], r['swing'])]['peak_rel_impact_ms']
                    if pel.get((r['session'], r['swing']), {}).get('state') == 'placed' else float('nan')),
                   ('arm', lambda r: r.get('arm_peak_rel_impact_ms', float('nan'))),
                   ('triangulated', lambda r: r.get('tri_peak_rel_impact_ms', float('nan')))):
        xs, ys, ss = [], [], []
        for r in R:
            x = f(r)
            if math.isfinite(x):
                xs.append(x); ys.append(r['peak_rel_impact_ms']); ss.append(r['session'])
        if len(xs) < 3:
            out[key] = dict(n=len(xs)); continue
        xs, ys = np.array(xs), np.array(ys)
        lag = ys - xs
        # within-session: remove each session's mean before correlating, so a session offset is not a "track"
        xw, yw = xs.copy(), ys.copy()
        for s in set(ss):
            m = np.array([q == s for q in ss])
            xw[m] -= xs[m].mean(); yw[m] -= ys[m].mean()
        out[key] = dict(n=len(xs), r=float(np.corrcoef(xs, ys)[0, 1]),
                        r_within=float(np.corrcoef(xw, yw)[0, 1]) if np.std(xw) > 0 and np.std(yw) > 0 else float('nan'),
                        lag_med=float(np.median(lag)), lag_sd=float(np.std(lag, ddof=1)),
                        lag_iqr=iqr(lag), within_1ms=int((np.abs(lag) <= 1.0).sum()),
                        within_1frame=int((np.abs(lag) <= FRAME_MS + 0.3).sum()))
    return out


def _f(x, d=0):
    try:
        x = float(x)
    except (TypeError, ValueError):
        return '—'
    if not math.isfinite(x):
        return '∞' if x > 0 else '—'
    return f'{x:.{d}f}'


def _med_rng(v, d=0):
    v = [float(x) for x in v if x not in (None, '') and math.isfinite(float(x))]
    if not v:
        return '—'
    return f'{_f(np.median(v), d)} [{_f(np.percentile(v, 25), d)}, {_f(np.percentile(v, 75), d)}]'


def report(rows, summ, frozen):
    """Every table the write-up quotes, as markdown, from the rows of this run."""
    L = []
    P = L.append
    sess = sorted(set(r['session'] for r in rows))
    num = lambda r, k: float(r[k]) if (k in r and r[k] not in (None, '')) else float('nan')
    P('## G-a reproduction (V0 pelvis against the shipped node)')
    P('| session | swings | reproduced | shipped pair nodes | max |Δt| ms | max |Δpeak| % | max |Δrate| over the series, °/s |')
    P('|---|---|---|---|---|---|---|')
    for s_ in sess:
        R = [r for r in rows if r['variant'] == 'V0' and r['segment'] == 'pelvis' and r['session'] == s_]
        ga = [x for x in gate_a(rows, [s_]) if x[0] == s_]
        pr = [r for r in R if r.get('shipped_route') == 'faceOn+dtl']
        P(f"| {s_} | {len(R)} | {sum(x[2] for x in ga)} | {len(pr)} | "
          f"{_f(max([abs(num(r, 'd_t_vs_shipped_ms')) for r in pr] or [float('nan')]), 3)} | "
          f"{_f(100 * max([abs(num(r, 'd_peak_vs_shipped_rel')) for r in pr] or [float('nan')]), 4)} | "
          f"{_f(max([num(r, 'series_max_abs_diff_dps') for r in pr] or [float('nan')]), 6)} |")
    gt = summ.get('gate_a_thorax', [])
    P(f"\nThorax, ring off as shipped: the pair thorax stands down on {sum(x[1] for x in gt)} of {len(gt)} "
      "dev swings whose shipped thorax is not the pair.")
    P('\n## Variants × session (thorax; ring evaluated ON)')
    P('| variant | session | n | placed | bounded | neither | refused | placed peak, ms from impact | peak °/s | σt ms | G-b | G-c | G-d IQR ms | ≥ half | G-f fails (scale/γ) | inverted parity at impact |')
    P('|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|')
    for v in VARIANTS:
        for s_ in sess:
            R = [r for r in rows if r['variant'] == v and r['segment'] == 'thorax' and r['session'] == s_]
            if not R:
                continue
            g = session_gates(rows, v, s_)
            pl = [r for r in R if r['state'] == 'placed']
            Rx = [r for r in R if math.isfinite(num(r, 'fo_x_at_impact_px'))]
            inv = sum(1 for r in Rx if num(r, 'fo_x_at_impact_px') < 0)
            P(f"| {v} | {s_[5:]} | {g['n']} | {g['placed']} | {g['bounded']} | {g['neither']} | {g['refused']} | "
              f"{_med_rng([r['peak_rel_impact_ms'] for r in pl])} | {_med_rng([r['peak_dps'] for r in pl])} | "
              f"{_f(g['t_sigma_med'])} | {'pass' if g['Gb'] else 'FAIL'} | {'pass' if g['Gc'] else 'FAIL'} ({g['Gc_fail']}) | "
              f"{_f(g['iqr_ms'], 1)} {'pass' if g['Gd'] else 'FAIL'} | {'yes' if g['half'] else 'no'} | "
              f"{g['Gf_fail']} ({g['Gf_scale_fail']}/{g['Gf_gamma_fail']}) | {inv} of {len(Rx)} |")
    P('\n## Variants × session (pelvis)')
    P('| variant | session | placed | refused | placed peak, ms | peak °/s | IQR ms | G-f fails | vs V0: median |Δt| ms |')
    P('|---|---|---|---|---|---|---|---|---|')
    for v in VARIANTS:
        for s_ in sess:
            R = [r for r in rows if r['variant'] == v and r['segment'] == 'pelvis' and r['session'] == s_]
            if not R:
                continue
            g = session_gates(rows, v, s_, 'pelvis'); pv = pelvis_ok(rows, v, s_)
            pl = [r for r in R if r['state'] == 'placed']
            P(f"| {v} | {s_[5:]} | {g['placed']} | {g['refused']} | {_med_rng([r['peak_rel_impact_ms'] for r in pl])} | "
              f"{_med_rng([r['peak_dps'] for r in pl])} | {_f(g['iqr_ms'], 1)} | {g['Gf_fail']} | {_f(pv['med_dt_ms'], 1)} |")
    P('\n## Face-on shoulder sign changes inside the domain (V0 rows; the 20° judge logged beside the collapse rule)')
    P('| session | swings | sign changes | collapse rule says relabel | 20° rule disagrees | DTL |d|/p95 at the change, min per swing: median [IQR] (range) | smoothed face-on max |d| / raw p95, thorax: median (max; swings > 1) |')
    P('|---|---|---|---|---|---|---|')
    for s_ in sess:
        R = [r for r in rows if r['variant'] in ('V0', frozen) and r['segment'] == 'thorax' and r['session'] == s_]
        R = [r for r in R if r['variant'] == ('V0' if any(x['variant'] == 'V0' for x in R) else frozen)]
        P(f"| {s_} | {len(R)} | {int(np.nansum([num(r, 'dom_signchanges') for r in R]))} | "
          f"{int(np.nansum([num(r, 'dom_collapse_relabels') for r in R]))} | "
          f"{int(np.nansum([num(r, 'dom_v0v2_disagree') for r in R]))} | {_med_rng([r.get('dom_cross_dtl_ratio_min') for r in R], 2)} "
          f"({_f(np.nanmin([num(r, 'dom_cross_dtl_ratio_min') for r in R]), 2)}–{_f(np.nanmax([num(r, 'dom_cross_dtl_ratio_min') for r in R]), 2)}) | "
          f"{_f(np.nanmedian([num(r, 'fo_sm_absmax_over_p95') for r in R]), 2)} ({_f(np.nanmax([num(r, 'fo_sm_absmax_over_p95') for r in R]), 2)}; "
          f"{sum(1 for r in R if num(r, 'fo_sm_absmax_over_p95') > 1.0)}) |")
    P(f'\n## Geometry on the frozen variant ({frozen}): median [IQR] over the swings it produced')
    P('| session | segment | γ fit ° | R² | closure p50 | closure p90 | DTL amplitude / (r·W_fo) | (W\'/W)_fit / r |')
    P('|---|---|---|---|---|---|---|---|')
    for s_ in sess:
        for seg in ('pelvis', 'thorax'):
            R = [r for r in rows if r['variant'] == frozen and r['segment'] == seg and r['session'] == s_ and r['state'] != 'refused']
            P(f"| {s_[5:]} | {seg} | {_med_rng([r.get('gamma_fit') for r in R], 1)} | {_med_rng([r.get('r2') for r in R], 3)} | "
              f"{_med_rng([r.get('closure_p50') for r in R], 3)} | {_med_rng([r.get('closure_p90') for r in R], 3)} | "
              f"{_med_rng([r.get('amp_ratio_v0') for r in R], 2)} | {_med_rng([r.get('ratio_fit_over_r') for r in R], 2)} |")
    P(f'\n## Frozen thorax ({frozen}) against the triangulated (skeleton3d) thorax and the DTL square crossing')
    P('| session | pair placed | pair peak ms | skeleton peak ms (all swings) | skeleton peaks before impact | skeleton within 1 frame of the pair | DTL crossing ms | pair − crossing ms |')
    P('|---|---|---|---|---|---|---|---|')
    for s_ in sess:
        R = [r for r in rows if r['variant'] == frozen and r['segment'] == 'thorax' and r['session'] == s_]
        pl = [r for r in R if r['state'] == 'placed']
        tri = [num(r, 'tri_peak_rel_impact_ms') for r in R if math.isfinite(num(r, 'tri_peak_rel_impact_ms'))]
        same = sum(1 for r in pl if abs(num(r, 'tri_peak_rel_impact_ms') - num(r, 'peak_rel_impact_ms')) <= 7.0)
        lag = [num(r, 'peak_rel_impact_ms') - num(r, 'dtl_square_rel_impact_ms') for r in pl]
        P(f"| {s_} | {len(pl)}/{len(R)} | {_med_rng([r['peak_rel_impact_ms'] for r in pl])} | {_med_rng(tri)} | "
          f"{sum(1 for x in tri if x < 0)}/{len(tri)} | {same}/{len(pl)} | {_med_rng([r.get('dtl_square_rel_impact_ms') for r in pl])} | {_med_rng(lag, 1)} |")
    P('\n## G-g: what the placed thorax peak tracks, shot by shot')
    for key, cm in (('dev', summ.get('common_mode_dev', {}).get(frozen, {})), ('all three', summ.get('common_mode_all', {}))):
        if not cm:
            continue
        P(f'\n{key} sessions:\n\n| against | n | r | r within session | lag median ms | lag SD ms | lag IQR ms | same sample | within 1 frame |\n|---|---|---|---|---|---|---|---|---|')
        for k, d in cm.items():
            P(f"| {k} | {d.get('n')} | {_f(d.get('r'), 2)} | {_f(d.get('r_within'), 2)} | {_f(d.get('lag_med'), 1)} | {_f(d.get('lag_sd'), 1)} | {_f(d.get('lag_iqr'), 1)} | {d.get('within_1ms', '—')} | {d.get('within_1frame', '—')} |")
    P(f'\n## The clock: DTL stamps moved earlier, frozen variant ({frozen}), peak shift in ms (median [IQR])')
    P('| session | segment | −4 ms | −12 ms | hard sync check: wrist δ ms | elbow δ ms |')
    P('|---|---|---|---|---|---|')
    for s_ in sess:
        for seg in ('pelvis', 'thorax'):
            R = [r for r in rows if r['variant'] == frozen and r['segment'] == seg and r['session'] == s_ and r['state'] == 'placed']
            A = [r for r in rows if r['variant'] == frozen and r['segment'] == seg and r['session'] == s_]
            P(f"| {s_[5:]} | {seg} | {_med_rng([r.get('dtlshift_4ms_dt_ms') for r in R], 1)} | {_med_rng([r.get('dtlshift_12ms_dt_ms') for r in R], 1)} | "
              f"{_med_rng([r.get('sync_wrist_ms') for r in A], 1)} | {_med_rng([r.get('sync_elbow_ms') for r in A], 1)} |")
    P(f'\n## V5: σt with the still-hold σ_w (frozen variant {frozen}, thorax)')
    P('| session | σt median, keypoint σ | σt median, still σ_w | σ_w face-on px | σ_w DTL px | placements changed |')
    P('|---|---|---|---|---|---|')
    for s_ in sess:
        R = [r for r in rows if r['variant'] == frozen and r['segment'] == 'thorax' and r['session'] == s_ and r['state'] != 'refused']
        ch = sum(1 for r in R if r.get('v5_state') not in (None, '') and (r['v5_state'] == 'placed') != (r['state'] == 'placed'))
        P(f"| {s_} | {_med_rng([r.get('t_sigma_ms') for r in R], 1)} | {_med_rng([r.get('v5_t_sigma_ms') for r in R], 1)} | "
          f"{_med_rng([r.get('still_sigma_fo_px') for r in R], 2)} | {_med_rng([r.get('still_sigma_dtl_px') for r in R], 2)} | {ch} |")
    P(f'\n## Orders with the shipped lead-arm and club nodes (frozen variant {frozen})')
    P('| session | order | n | resolved |')
    P('|---|---|---|---|')
    for s_ in sess:
        R = [r for r in rows if r['variant'] == frozen and r['segment'] == 'thorax' and r['session'] == s_]
        od = {}
        for r in R:
            od.setdefault(r['order'], [0, 0]); od[r['order']][0] += 1; od[r['order']][1] += int(r['order_resolved'])
        for o, (n, k) in sorted(od.items(), key=lambda x: -x[1][0]):
            P(f'| {s_[5:]} | {o or "—"} | {n} | {k} |')
    P(f'\n## Held-out, swing by swing ({frozen})')
    P('| swing | thorax state | peak ms | °/s | σt ms | reversal ms | G-f frames (scale/γ) | γ ° | R² | DTL crossing ms | skeleton ms | pelvis state | pelvis ms |')
    P('|---|---|---|---|---|---|---|---|---|---|---|---|---|')
    for r in rows:
        if r['session'] != HELDOUT[0][:10] or r['variant'] != frozen or r['segment'] != 'thorax':
            continue
        pr = [x for x in rows if x['session'] == r['session'] and x['swing'] == r['swing'] and x['variant'] == frozen and x['segment'] == 'pelvis']
        pr = pr[0] if pr else {}
        P(f"| {r['swing'][-2:]} | {r['state']}{(' (' + r['refusal'] + ')') if r.get('refusal') else ''} | {_f(r.get('peak_rel_impact_ms'), 1)} | {_f(r.get('peak_dps'))} | "
          f"{_f(r.get('t_sigma_ms'))} | {_f(r.get('reversal_dist_ms'))} | {r.get('inv_scale_max_frames', '—')}/{r.get('inv_gamma_max_frames', '—')} | "
          f"{_f(r.get('gamma_fit'), 1)} | {_f(r.get('r2'), 3)} | {_f(r.get('dtl_square_rel_impact_ms'), 1)} | {_f(r.get('tri_peak_rel_impact_ms'), 1)} | "
          f"{pr.get('state', '—')} | {_f(pr.get('peak_rel_impact_ms'), 1)} |")
    return '\n'.join(L)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--stage', choices=('dev', 'all'), default='dev')
    ap.add_argument('--csv'); ap.add_argument('--figdir'); ap.add_argument('--summary')
    ap.add_argument('--report', help='write the write-up\'s tables (markdown) here')
    a = ap.parse_args()
    examples = {(DEV[1], 'swing_0008'), (DEV[1], 'swing_0003')}
    print('dev sessions …', file=sys.stderr)
    rows, kept = run_sessions(DEV, VARIANTS, keep=examples)
    frozen, table, clean = select_variant(rows, DEV)
    summ = dict(frozen=frozen, frozen_clean_pass=clean,
                freeze_table={v: list(t) for v, t in table.items()},
                gate_a=[list(x) for x in gate_a(rows, DEV)],
                gate_a_thorax=[list(x) for x in gate_a_thorax(rows, DEV)],
                dev={v: {s[:10]: dict(thorax=session_gates(rows, v, s), pelvis=session_gates(rows, v, s, 'pelvis'),
                                      pelvis_vs_v0=pelvis_ok(rows, v, s)) for s in DEV} for v in VARIANTS},
                common_mode_dev={v: common_mode(rows, v, DEV) for v in VARIANTS})
    print(f'FROZEN on dev: {frozen} (clean pass: {clean}) table {table}', file=sys.stderr)
    if a.stage == 'all':
        print(f'held-out session, ONCE, on {frozen} …', file=sys.stderr)
        vset = ('V0', frozen) if frozen != 'V0' else ('V0',)
        hrows, _ = run_sessions(HELDOUT, vset)
        hrows = [r for r in hrows if r['variant'] == frozen or (r['variant'] == 'V0' and r['segment'] == 'pelvis')]
        rows += hrows
        g = session_gates(rows, frozen, HELDOUT[0])
        summ['heldout'] = dict(thorax=g, pelvis=session_gates(rows, frozen, HELDOUT[0], 'pelvis'),
                               Ge=g['Gb'] and g['Gc'] and g['Gd'] and g['half'],
                               gate_a=[list(x) for x in gate_a(rows, HELDOUT)])
        summ['common_mode_all'] = common_mode(rows, frozen, DEV + HELDOUT)
    if a.csv:
        keys = []
        for r in rows:
            for k in r:
                if k not in keys:
                    keys.append(k)
        with open(a.csv, 'w', newline='') as fh:
            w = csv.DictWriter(fh, fieldnames=keys)
            w.writeheader()
            for r in rows:
                w.writerow({k: (f'{v:.4g}' if isinstance(v, float) else v) for k, v in r.items()})
    if a.figdir:
        summ['figures'] = figures(a.figdir, kept, rows, frozen)
    if a.report:
        with open(a.report, 'w') as fh:
            fh.write(report(rows, summ, frozen) + '\n')
    if a.summary:
        os.makedirs(os.path.dirname(a.summary), exist_ok=True)
        with open(a.summary, 'w') as fh:
            json.dump(summ, fh, indent=1, default=lambda o: o if not isinstance(o, float) or math.isfinite(o) else str(o))
    print(json.dumps({k: summ[k] for k in ('frozen', 'frozen_clean_pass', 'freeze_table')}, indent=1))


if __name__ == '__main__':
    main()
