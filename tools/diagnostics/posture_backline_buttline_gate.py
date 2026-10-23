#!/usr/bin/env python3
"""Stage 4 gate: the back line and the butt line against the joint readings, every re-analysed swing.

usage: posture_backline_buttline_gate.py <s4lib root> <out.csv>
Writes one row per swing x position (change from address), then prints the per-golfer gate tables.
"""
import csv, glob, os, statistics as st, sys
sys.path.insert(0, "/Users/markliversedge/Projects/PinPointStudio/tools")
from pp_swingdoc import load_swing

ORDER = [0, 12, 8, 2, 13, 9, 5]          # P1..P7 phase ids
NAMES = ['P1', 'P2', 'P3', 'P4', 'P5', 'P6', 'P7']


def interp(m, t):
    ts, vs = m.get('t_us', []), m.get('value', [])
    valid = m.get('valid') or [1] * len(ts)
    pts = [(a, b) for a, b, ok in zip(ts, vs, valid) if ok and b is not None]
    if not pts: return None
    for (t0, v0), (t1, v1) in zip(pts, pts[1:]):
        if t0 <= t <= t1:
            return v0 if t1 == t0 else v0 + (v1 - v0) * (t - t0) / (t1 - t0)
    return None


def sample(m, ph, t):
    if m is None: return None
    for s in m.get('phaseSamples', []):
        if s.get('phase') == ph and s.get('value') is not None: return s['value']
    return interp(m, t) if t is not None else None


def main(root, out):
    rows = []
    for d in sorted(glob.glob(os.path.join(root, '*', '*', 'swing_*'))):
        golfer, session, swing = d.split(os.sep)[-3:]
        try:
            an = load_swing(d)['analysis']
        except Exception as e:
            continue
        M = {m['key']: m for m in an.get('metrics', [])}
        if 'spineForwardBend' not in M and 'spineBackLine' not in M: continue
        lad = {}
        for p in an.get('phases', []): lad.setdefault(p['phase'], p['t_us'])
        ver = (an.get('addressMarks') or {}).get('stageVersion')
        for key in ('spineForwardBend', 'spineBackLine', 'pelvisThrust', 'hipDepthButt'):
            m = M.get(key)
            vals = [sample(m, ph, lad.get(ph)) for ph in ORDER]
            base = vals[0]
            for n, v in zip(NAMES, vals):
                ch = (v - base) if (v is not None and base is not None and key != 'hipDepthButt') else v
                if key == 'hipDepthButt' and n == 'P1' and v is not None: ch = 0.0
                rows.append((golfer, session, swing, ver, key, n, '' if v is None else round(v, 3), '' if ch is None else round(ch, 3)))
    with open(out, 'w', newline='') as fh:
        w = csv.writer(fh); w.writerow(['golfer', 'session', 'swing', 'addressMarksVersion', 'reading', 'position', 'value', 'change_from_address']); w.writerows(rows)
    print(f'{len(rows)} rows -> {out}')

    from collections import defaultdict
    D = defaultdict(lambda: defaultdict(dict))
    for g, s, sw, ver, k, n, v, ch in rows:
        if ch != '': D[(g, s, sw)][k][n] = ch
    for g in ('Nick-Henn', 'Mark-Liversedge', 'Jess-Ratcliffe'):
        sws = [key for key in D if key[0] == g]
        print(f'\n=== {g}: {len(sws)} DTL swings')
        print(f"{'':3} {'bend joint':>16} {'back line':>16} {'read':>5} | {'hips joint':>16} {'butt line':>16} {'read':>5} {'|butt-joint|':>12}")
        for n in NAMES[1:]:
            def med(k):
                x = [D[sw][k][n] for sw in sws if n in D[sw][k]]
                if not x: return '-', 0
                q = st.quantiles(x, n=4) if len(x) >= 4 else [min(x), st.median(x), max(x)]
                return f'{st.median(x):+5.1f} [{q[0]:+.1f},{q[2]:+.1f}]', len(x)
            bj, nbj = med('spineForwardBend'); bb, nbb = med('spineBackLine')
            hj, nhj = med('pelvisThrust'); hb, nhb = med('hipDepthButt')
            dif = [abs(D[sw]['hipDepthButt'][n] - D[sw]['pelvisThrust'][n]) for sw in sws
                   if n in D[sw]['hipDepthButt'] and n in D[sw]['pelvisThrust']]
            dd = f'{st.median(dif):.1f}' if dif else '-'
            print(f'{n:3} {bj:>16} {bb:>16} {nbb:>2}/{nbj:<2} | {hj:>16} {hb:>16} {nhb:>2}/{nhj:<2} {dd:>12}')


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
