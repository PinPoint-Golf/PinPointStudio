"""Plane stack D6 (13 Nov 2026, evening): bent_lead_arm's 3-D reading as a CHANGE FROM ADDRESS.

Reads the rows d6_hold_probe.py (or d2_upper_arm.py) writes (argv[1]; one JSON line per swing, read-only on
/mnt/swingdata) and summarises m_leadElbowBendTop3d: bend3d_t3 (the measured lead elbow at the Top +/- 15 ms) less
the address reference named by argv[2]: bend3d_t3_hold (the default, as built since D6 round 2: the median over
the address hold, P1 - 150 ms .. P1) or bend3d_t3_p1 (D6 round 1: Address +/- 15 ms). A swing keeps a reading
only where both ends were measured (leadElbowBendChange3d is not built otherwise).
Prints, per golfer: the swings with a top reading, with an address reading, and with both; the change's median,
middle half and spread; and the firing and amber counts for candidate ceiling norms (mu, tolerance), amber past
mu + tol and firing past mu + 2 tol (the standard grading policy). Nothing is written.

    python3 d6_elbow_change.py rows.jsonl [bend3d_t3_hold | bend3d_t3_p1]
"""
import collections
import json
import sys

import numpy as np

G = ['Mark', 'Nick', 'Jess', 'Paddy', 'Chris']
R = [json.loads(line) for line in open(sys.argv[1])]
REF = sys.argv[2] if len(sys.argv) > 2 else 'bend3d_t3_hold'
both = [r for r in R if 'bend3d_t3' in r and REF in r]
change = lambda r: r['bend3d_t3'] - r[REF]
print('address reference:', REF)

print('golfer  swings  top  address  both   change median [p25, p75]  (top / address medians on both)')
for g in G:
    rr = [r for r in R if r['golfer'] == g]
    top = sum('bend3d_t3' in r for r in rr)
    ad = sum(REF in r for r in rr)
    b = [r for r in both if r['golfer'] == g]
    line = f'{g:6s}  {len(rr):6d}  {top:3d}  {ad:7d}  {len(b):4d}'
    if b:
        d = np.array([change(r) for r in b])
        line += (f'   {np.median(d):+6.1f} [{np.percentile(d, 25):+.1f}, {np.percentile(d, 75):+.1f}]'
                 f'  ({np.median([r["bend3d_t3"] for r in b]):.1f} / {np.median([r[REF] for r in b]):.1f})')
    print(line)
print(f'swings keeping a reading: {len(both)} (with a top reading: {sum("bend3d_t3" in r for r in R)})')

m = [r for r in both if r['golfer'] == 'Mark']
if len(m) > 2:
    a = np.array([r[REF] for r in m]); t = np.array([r['bend3d_t3'] for r in m])
    print(f'Mark: SD address {a.std():.1f}, top {t.std():.1f}, change {(t - a).std():.1f}; '
          f'r(address, top) {np.corrcoef(a, t)[0, 1]:.2f}')
    by = collections.defaultdict(list)
    for r in m: by[r['session']].append(change(r))
    for s, v in sorted(by.items()):
        print(f'   {s}: {len(v)} swings, median {np.median(v):+.1f}, SD {np.std(v):.1f}')

print('candidate ceilings (mu, tol): fires > mu + 2 tol, amber > mu + tol')
for mu, tol in [(0, 8), (0, 7), (0, 6), (0, 10), (3, 7), (5, 6)]:
    f, a = collections.Counter(), collections.Counter()
    for r in both:
        d = change(r)
        if d > mu + 2 * tol: f[r['golfer']] += 1
        elif d > mu + tol: a[r['golfer']] += 1
    print(f'   {mu:2d} {tol:2d}: fires {dict(f)}  amber {dict(a)}')
