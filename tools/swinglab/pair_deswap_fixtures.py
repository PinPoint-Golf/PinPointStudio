#!/usr/bin/env python3
"""The pair route's two face-on defects (thorax_pair_20261014.md §3.1, §3.2, §12): fixtures and the
corpus check behind the 14 Oct 2026 fix in segment_rates.cpp (deswapInPlace, pairTurnTrack).

  (i)  SOURCE CHOICE. The face-on leg was consumed SMOOTHED whenever deswapInPlace reported no flip
       and no drop. Through the face-on shoulder crossing the raw labels flutter, the collapse rule
       calls every one of those sign changes genuine, and the RTS smoother bridges the flutter into
       a ramp past the line's own width (07-04 s8: 1.30× the raw p95). Fixed: a face-on leg whose
       de-swapped, rate-limited RAW track changes sign inside the domain is consumed raw.
  (ii) THE COLLAPSE RULE ON A KEYPOINT JUMP ACROSS ZERO. On 06-11 s5 and s7 the face-on shoulders
       jump across zero in the downswing with |d| ≥ 0.35 W on both sides; the collapse rule reads
       that GENUINE crossing as a relabel and the oriented face-on separation comes out negative
       at impact — the trunk 180° from where it was at address. Fixed: impact is a second parity
       anchor for the face-on view (near square to that camera, as at address); a walk that
       contradicts it with exactly one ambiguous unit between the anchors — one relabel call, or
       one ≥ 3-in-100 ms flutter cluster — has that unit revoked, and with any other count the
       leg is refused.

    ~/.swinglab-venv/bin/python tools/swinglab/pair_deswap_fixtures.py extract   # writes the fixtures
    ~/.swinglab-venv/bin/python tools/swinglab/pair_deswap_fixtures.py check     # 34 swings, before/after

`extract` writes src/Analysis/tests/data/segment_rates/<session>_<swing>.txt — the MINIMUM the pair
route reads, with x, y and σ at the documents' own precision so the shipped pelvis node reproduces
(confidence to three digits: the route only compares it with confMin, and a value within 0.002 of
that gate is written in full):
every pose frame of both views and both tiers (the de-swap and bracket gap rules read the tracks'
MEDIAN spacing, so no frame may be dropped), the hip and shoulder x + confidence on every frame,
the smoothed tier's σ on those four, and the shoulder/ankle y + confidence only inside the address
window (the vertical-extent scale is the only reader of y). Plus the frame sizes, the phase ladder,
impact, and the SHIPPED pelvis / thorax nodes the test pins.

`check` re-runs the offline rig's V0 (thorax_pair_offline.py, bit-exact with the shipped C++) and a
replica of the FIXED C++ on all 34 two-camera swings and prints, per swing and segment: the face-on
source, flips, revokes, the consumed face-on max |d| / raw p95, the oriented face-on separation at
impact, and the node before → after (thorax ring ON, as the rig evaluates it: with the ring off as
shipped, "placed" stands down to the face-on span and "bounded" keeps its bound). Read-only on the
corpus. Run on 14 Oct 2026: pelvis 34/34 identical; the face-on shoulders consumed max ≤ 1.08× the
raw p95 on every swing (was 1.30 on 07-04 s8, 1.32 on 06-11 s3); the oriented face-on shoulder
separation at impact positive on 34/34 (was negative on 06-11 s5, s7 and 07-03 s4, s5, s6).
"""
import argparse, math, os, sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, '..'))
import thorax_pair_offline as R                                          # noqa: E402

REPO = os.path.abspath(os.path.join(HERE, '..', '..'))
OUT_DIR = os.path.join(REPO, 'src', 'Analysis', 'tests', 'data', 'segment_rates')
FIXTURES = (('2026-07-04_Mark-Liversedge_Wrist_01', 'swing_0008'),
            ('2026-06-11_Mark-Liversedge_Wrist_01', 'swing_0005'),
            ('2026-06-11_Mark-Liversedge_Wrist_01', 'swing_0007'))
KPS = (R.LS, R.RS, R.LH, R.RH)          # x + conf on every frame (+ σ on the smoothed tier)
KPS_Y = (R.LS, R.RS, R.LA, R.RA)        # y + conf inside the address window (pairScale)
ANCHOR_HALF_US = R.DERIV_WINDOW_US      # cfg.derivWindowMs: the impact anchor is impact ± one window


# ══ The FIXED C++, replicated (segment_rates.cpp, 14 Oct 2026) ══════════════════════════════════

def deswap_fixed(s, a_from, a_to, w_view, local_gap, anchor=None):
    """deswapInPlace after the fix. `anchor` = (from_us, to_us) of the impact window, face-on only."""
    n = len(s['t'])
    rep = dict(flips=0, dropped=0, revoked=0, anchor_broken=False)
    if n < 3 or not (w_view > 0):
        return rep
    t = s['t']; d = s['d']
    step_us = R.cmedian([float(t[i] - t[i - 1]) for i in range(1, n)])
    if not (step_us > 0):
        return rep
    max_gap = int(3.0 * step_us)
    floor = R.PAIR_SWAP_MIN_FRAC * w_view

    def gap_limit(i_end):
        if not local_gap or i_end == 0:
            return max_gap
        return max(max_gap, int(3.0 * R.local_gap_us(t, i_end - 1)))

    idx = [i for i in range(n) if a_from <= t[i] <= a_to]
    a0, a1 = (min(idx), max(idx)) if idx else (0, 0)

    def step(raw, prev, dt, i_end):
        return raw * prev < 0.0 and min(abs(raw), abs(prev)) >= floor and dt <= gap_limit(i_end)

    flip_t, fwd = [], []
    p = 1.0; prev = d[a1]; prev_t = t[a1]
    for i in range(a1 + 1, n):
        if step(d[i] * p, prev, t[i] - prev_t, i):
            p = -p; rep['flips'] += 1; flip_t.append(int(t[i])); fwd.append(i)
        d[i] *= p; prev = d[i]; prev_t = t[i]
    if anchor is not None and idx:
        ma = R.cmedian([d[i] for i in range(a0, a1 + 1)])
        wi = [d[i] for i in range(n) if anchor[0] <= t[i] <= anchor[1]]
        mi = R.cmedian(wi) if wi else 0.0
        if abs(ma) >= floor and abs(mi) >= floor and ma * mi < 0.0:
            # the ambiguous parity UNITS between the anchors: a lone relabel call is one, and a
            # flutter cluster (≥ 3 flips inside 100 ms, the drop rule's own test) is one
            cand = [i for i in fwd if t[i] <= anchor[1]]
            ct = [int(t[i]) for i in cand]
            unit = list(range(len(cand)))
            for q in range(len(cand) - 2):
                if ct[q + 2] - ct[q] <= 100000:
                    for r_ in (q + 1, q + 2):
                        old = unit[r_]
                        unit = [unit[q] if u == old else u for u in unit]
            units = sorted(set(unit))
            if len(units) == 1:
                members = [q for q in range(len(cand)) if unit[q] == units[0]]
                j = cand[members[-1]]
                d[j:] *= -1.0
                rep['revoked'] += 1
                if len(members) == 1:
                    rep['flips'] -= 1
                    flip_t.remove(int(t[j]))
            else:
                rep['anchor_broken'] = True
    p = 1.0; prev = d[a0]; prev_t = t[a0]
    for k in range(a0 - 1, -1, -1):
        if step(d[k] * p, prev, prev_t - t[k], k + 1):
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


def crossing_count(s, lo, hi):
    """Sign changes between consecutive USABLE samples whose later sample lies in [lo, hi]."""
    ti = s['t'][s['ok']]; di = s['d'][s['ok']]
    return int(sum(1 for i in range(1, len(ti)) if lo <= ti[i] <= hi and di[i] * di[i - 1] < 0.0))


def pair_inputs_fixed(sw, lead, trail):
    """pairTurnTrack's front half after the fix, returning the rig's (P, diag) for run_variant('V0')."""
    pin = sw['pin']; dom = sw['dom']
    diag = dict(refusal='')
    sfr = R.separations(sw['fo_raw'], lead, trail); sdr = R.separations(sw['dtl_raw'], lead, trail)
    if len(sfr['t']) < 3 or len(sdr['t']) < 3:
        diag['refusal'] = 'too few admitted frames'; return None, diag
    tk = pin['takeaway']

    def p95(s):
        m = (s['t'] >= tk) & (s['t'] <= pin['impact'])
        v = list(np.abs(s['d'][m])) if m.sum() >= 3 else list(np.abs(s['d']))
        return R.cpercentile(v, 0.95)
    w_fo, w_dtl = p95(sfr), p95(sdr)
    a0, a1 = pin['address'], pin['address'] + R.ADDR_WINDOW_US
    rf = deswap_fixed(sfr, a0, a1, w_fo, False,
                      anchor=(pin['impact'] - ANCHOR_HALF_US, pin['impact'] + ANCHOR_HALF_US))
    rd = deswap_fixed(sdr, a0, a1, w_dtl, R.DTL_LOCAL_GAP)
    diag.update(swaps_fo=rf['flips'], swaps_dtl=rd['flips'], dropped=rf['dropped'] + rd['dropped'],
                revoked_fo=rf['revoked'], w_fo=w_fo, w_dtl=w_dtl)
    if rf['anchor_broken']:
        diag['refusal'] = 'face-on labels contradict the impact anchor'; return None, diag
    # (i) the crossing test on the de-swapped, rate-limited RAW face-on track
    sfl = dict(t=sfr['t'], d=sfr['d'].copy(), sigma=sfr['sigma'], ok=sfr['ok'].copy())
    rl_raw = R.rate_limit(sfl, w_fo)
    lo = dom['from'] if dom['draw_from'] < 0 else min(dom['from'], dom['draw_from'])
    ncr = crossing_count(sfl, lo, dom['to'])
    diag['crossings_fo'] = ncr
    fo_clean = rf['flips'] == 0 and rf['dropped'] == 0 and ncr == 0 and sw['fo_sm'] is not None
    dtl_clean = rd['flips'] == 0 and rd['dropped'] == 0 and sw['dtl_sm'] is not None
    if fo_clean:
        sf = R.separations(sw['fo_sm'], lead, trail)
        diag['rl_fo'] = R.rate_limit(sf, w_fo)
    else:
        sf = sfl
        diag['rl_fo'] = rl_raw
    sd = R.separations(sw['dtl_sm'], lead, trail) if dtl_clean else sdr
    diag['src'] = ('smoothed' if fo_clean else 'rawDeswapped') + '/' + ('smoothed' if dtl_clean else 'rawDeswapped')
    diag['rl_dtl'] = R.rate_limit(sd, w_dtl)
    if sf['ok'].sum() < 3 or sd['ok'].sum() < 3:
        diag['refusal'] = 'too few usable separations after the guards'; return None, diag
    mF = R.median_over(sf, a0, a1); mD = R.median_over(sd, pin['top'] - 150000, pin['top'] + 50000)
    if not abs(mF) > 0 or not abs(mD) > 0:
        diag['refusal'] = 'no orientation'; return None, diag
    sgnF = 1.0 if mF > 0 else -1.0; sgnD = 1.0 if mD > 0 else -1.0
    gaps = [float(sd['t'][i] - sd['t'][i - 1]) for i in range(1, len(sd['t']))]
    dstep = R.cmedian(gaps) if gaps else 0.0
    max_br = R.PAIR_MAX_GAP_FRAMES * dstep if dstep > 0 else 0.0
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
            lim = max(max_br, R.PAIR_MAX_GAP_FRAMES * R.local_gap_us(tD, k)) if R.DTL_LOCAL_GAP else max_br
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


def consumed_fo_stats(P, dom):
    """The two defect read-outs: max |consumed face-on d| over the domain / raw p95, and the oriented
    consumed face-on separation at impact (median over impact ± one derivative window)."""
    t = P['fo_t']; d = P['fo_d']; ok = P['fo_ok']
    lo = dom['from'] if dom['draw_from'] < 0 else min(dom['from'], dom['draw_from'])
    m = ok & (t >= lo) & (t <= dom['to'])
    mx = float(np.abs(d[m]).max()) / P['w_fo'] if m.any() else float('nan')
    mi = ok & (np.abs(t - dom['impact']) <= ANCHOR_HALF_US)
    return mx, (R.cmedian(list(d[mi])) if mi.any() else float('nan'))


def shipped_inputs(sw, lead, trail):
    return R.pair_inputs(sw, lead, trail, None, raw_on_crossing=False)


def node_str(nd):
    if nd is None:
        return 'refused'
    if not nd['ok']:
        return nd['state']
    return f"{nd['state']} {nd['t_peak']} {nd['peak']:.6f}"


def check(args):
    sessions = R.DEV + R.HELDOUT
    print('| swing | seg | src before → after | flips → after (revoked) | crossings | consumed max/p95 before → after | '
          'oriented fo at impact before → after px | node before | node after | same |')
    print('|---|---|---|---|---|---|---|---|---|---|')
    pel_same = pel_n = 0
    for s in sessions:
        for swg in R.swings_of(s):
            sw = R.load(s, swg)
            for seg, a, b, _, _ in R.SEGS:
                P0, d0 = shipped_inputs(sw, a, b)
                P1, d1 = pair_inputs_fixed(sw, a, b)
                n0 = R.run_variant(sw, seg, a, b, 'V0', P=P0, diag=d0)[0] if P0 is not None and sw['scale']['ok'] else None
                n1 = R.run_variant(sw, seg, a, b, 'V0', P=P1, diag=d1)[0] if P1 is not None and sw['scale']['ok'] else None
                s0 = consumed_fo_stats(P0, sw['dom']) if P0 is not None else (float('nan'),) * 2
                s1 = consumed_fo_stats(P1, sw['dom']) if P1 is not None else (float('nan'),) * 2
                same = node_str(n0) == node_str(n1)
                if seg == 'pelvis':
                    pel_n += 1; pel_same += int(same)
                print(f"| {s[:10]} {swg[-2:]} | {seg} | {d0.get('src', '-')} → {d1.get('src', d1.get('refusal', '-'))} | "
                      f"{d0.get('swaps_fo', '-')} → {d1.get('swaps_fo', '-')} ({d1.get('revoked_fo', 0)}) | "
                      f"{d1.get('crossings_fo', '-')} | {s0[0]:.3f} → {s1[0]:.3f} | {s0[1]:.1f} → {s1[1]:.1f} | "
                      f"{node_str(n0)} | {node_str(n1)} | {'=' if same else 'CHANGED'} |")
    print(f'\npelvis node identical before/after (rig, ring on): {pel_same}/{pel_n}')


# ══ The fixtures ═══════════════════════════════════════════════════════════════════════════════

def fmt_d(x):
    return repr(float(x))


def fmt_f(x):
    return '%.9g' % float(x)       # float32 σ: nine significant digits round-trip exactly


def fmt_c(x):
    """A keypoint confidence. The pair route only ever compares it with confMin (0.30), so three
    significant digits carry it — except within 0.002 of that gate, where it is written in full."""
    x = float(x)
    return fmt_f(x) if abs(x - R.CONF_MIN) < 0.002 else '%.3g' % x


def write_tier(out, name, frames, addr_lo, addr_hi, raw_twin=None):
    """One tier. A smoothed tier (raw_twin = its raw frames) shares the raw tier's instants, so its
    rows carry no t, and a confidence equal to the raw one is written '='."""
    out.append(f'tier {name} {len(frames)}')
    if raw_twin is not None:
        assert len(raw_twin) == len(frames) and all(int(round(a['t_us'])) == int(round(b['t_us']))
                                                    for a, b in zip(raw_twin, frames))
    for j, f in enumerate(frames):
        kp = f['kp']
        t = int(round(f['t_us']))
        row = [] if raw_twin is not None else [str(t)]
        for k in KPS:
            c = kp[3 * k + 2]
            same = raw_twin is not None and raw_twin[j]['kp'][3 * k + 2] == c
            row += [fmt_d(kp[3 * k]), '=' if same else fmt_c(c)]
        if raw_twin is not None:
            sg = f['sigma']
            row += [fmt_f(sg[k]) for k in KPS]
        elif addr_lo <= t <= addr_hi:
            for k in KPS_Y:
                row += [fmt_d(kp[3 * k + 1]), fmt_c(kp[3 * k + 2])]
        out.append(' '.join(row))


def extract(args):
    os.makedirs(OUT_DIR, exist_ok=True)
    for sess, swg in FIXTURES:
        doc = R.load_swing(f'{R.CORPUS}/{sess}/{swg}')
        a = doc['analysis']
        (FW, FH), (DW, DH) = R.frame_dims(doc)
        ph = a['phases']
        addr = R.phase_time(ph, R.PH_ADDRESS, -1)
        impact = R.phase_time(ph, R.PH_IMPACT, int((a.get('kinematicSequence') or {}).get('impactUs', -1)))
        lo, hi = addr, addr + R.ADDR_WINDOW_US
        out = [f'# {sess}/{swg} — written by tools/swinglab/pair_deswap_fixtures.py extract. Read-only copy of',
               '# the corpus document: pose2d + poseDtl, raw + smoothed. Raw rows: t, then x conf for keypoints',
               '# 5 6 11 12, then (address-window rows only) y conf for 5 6 15 16. Smoothed rows: the raw row\'s t,',
               '# x conf for 5 6 11 12 (conf "=" when equal to the raw one), then σ px for 5 6 11 12. x, y are',
               '# normalised to the frame and exact; conf is to 3 significant digits (only ever compared with 0.30).',
               f'frame fo {FW} {FH}', f'frame dtl {DW} {DH}', f'impact {impact}']
        for p in ph:
            out.append(f"phase {int(p['phase'])} {int(round(p['t_us']))}")
        for seg in ('pelvis', 'thorax'):
            n = R.doc_node(doc, seg)
            out.append(f"shipped {seg} {n['routeId']} {int(n['placed'])} {int(n['tPeakUs'])} {fmt_d(n['peakDps'])}")
        for v, tag in (('pose2d', 'fo'), ('poseDtl', 'dtl')):
            write_tier(out, f'{tag}_raw', a[v]['frames'], lo, hi)
            write_tier(out, f'{tag}_sm', a[v]['smoothed'], lo, hi, raw_twin=a[v]['frames'])
        path = os.path.join(OUT_DIR, f'{sess[:10]}_{swg}.txt')
        with open(path, 'w') as fh:
            fh.write('\n'.join(out) + '\n')
        print(f'{path}: {os.path.getsize(path) / 1024:.0f} KB')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('mode', choices=('extract', 'check'))
    args = ap.parse_args()
    (extract if args.mode == 'extract' else check)(args)


if __name__ == '__main__':
    main()
