#!/usr/bin/env python3
"""kb18_impact_probe.py — read-only KB-18 probe: does the face-on club rate collapse at impact?

For every swing document under the given roots (library sessions or a re-analysed mirror) it
writes one CSV row:
  collapse      median club angular speed (KS club channel, `clubAngularSpeed`) within ±8 ms of
                impact over its pre-impact peak (−150..−8 ms). Physics says ≥ ~0.8: the club
                cannot lose most of its speed at the ball. KB-18's artefact reads ~0.5.
  maxRateWin    the largest club rate within ±50 ms (°/s) — catches new spikes.
  sweep         how far the measured face-on shaft angle turns from −20 to +20 ms, over what its
                own pre-window rate (median pair rate, −55..−25 ms) predicts. The face-on
                projection should make this ≥ 1; the leg/trouser lock reads ~0.5.
  backwardAfter samples in +3..+35 ms pointing back up the downswing (θ 125–250°, right-hander)
  clubPeakMs, clubSigmaMs, withheld   the KS club node as the tile sees it.
Reads documents only through tools/pp_swingdoc.py; never writes next to them.

  kb18_impact_probe.py --out probe.csv <root> [<root> ...]
"""
import argparse, csv, glob, math, os, statistics as st, sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from pp_swingdoc import load_swing, find_swing_dirs  # noqa: E402


def wrap(x):
    return (x + 180.0) % 360.0 - 180.0


def probe(sd):
    d = load_swing(sd)
    a = d.get('analysis', {})
    ks, c = a.get('kinematicSequence'), a.get('club')
    if not ks or not c or not c.get('samples'):
        return None
    imp = ks['impactUs']
    m = {e['key']: e for e in a.get('metrics', [])}
    cs = m.get('clubAngularSpeed')
    if not cs:
        return None
    ser = [((t - imp) / 1000.0, v) for t, v in zip(cs['t_us'], cs['value'])]
    pre = max([v for t, v in ser if -150 < t < -8] or [0])
    at_i = [v for t, v in ser if abs(t) <= 8]
    if not pre or not at_i:
        return None
    S = [((x['t_us'] - imp) / 1000.0, math.degrees(x['theta'])) for x in c['samples']]
    rp = [abs(wrap(b[1] - a_[1])) / ((b[0] - a_[0]) / 1000.0)
          for a_, b in zip(S, S[1:]) if a_[0] >= -55 and b[0] <= -25 and b[0] > a_[0]]
    sweep = float('nan')
    if len(rp) >= 2:
        s0 = min(S, key=lambda s: abs(s[0] + 20))
        s1 = min(S, key=lambda s: abs(s[0] - 20))
        r0 = st.median(rp)
        if r0 > 0 and s1[0] > s0[0]:
            sweep = abs(wrap(s1[1] - s0[1])) / (r0 * (s1[0] - s0[0]) / 1000.0)
    back = sum(1 for t, th in S if 3 <= t <= 35 and 125 < th % 360 < 250)
    node = next((n for n in ks.get('nodes', []) if n.get('segment') == 'club'), {})
    peak_t = m.get('clubPeakTime', {})
    return {
        'swing': os.path.relpath(sd, os.path.dirname(os.path.dirname(sd))),
        'route': cs.get('routeId', ''),
        'collapse': round(st.median(at_i) / pre, 3),
        'maxRateWin': round(max([v for t, v in ser if abs(t) <= 50] or [0])),
        'sweep': round(sweep, 3) if sweep == sweep else '',
        'backwardAfter': back,
        'clubPeakMs': round(-node['beforeImpactMs'], 1) if 'beforeImpactMs' in node else '',
        'clubSigmaMs': round(node.get('tSigmaMs', float('nan')), 1) if node else '',
        'placed': node.get('placed', ''),
        'withheld': peak_t.get('withheld', ''),
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('roots', nargs='+')
    ap.add_argument('--out', required=True)
    args = ap.parse_args()
    dirs = []
    for r in args.roots:
        dirs += find_swing_dirs(r) if os.path.isdir(r) else []
    rows = [r for r in (probe(sd) for sd in sorted(set(dirs))) if r]
    with open(args.out, 'w', newline='') as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        w.writeheader()
        w.writerows(rows)
    n = len(rows)
    ok = sum(1 for r in rows if r['collapse'] >= 0.8)
    sw = [r['sweep'] for r in rows if r['sweep'] != '']
    print(f'{n} swings; collapse ≥ 0.8 on {ok} ({100 * ok / n:.0f} %); '
          f'median sweep {st.median(sw):.2f}; max rate ≤ 3500 on '
          f'{sum(1 for r in rows if r["maxRateWin"] <= 3500)}; backward-after on '
          f'{sum(1 for r in rows if r["backwardAfter"] >= 2)}')


if __name__ == '__main__':
    main()
