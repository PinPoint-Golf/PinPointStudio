#!/usr/bin/env python3
"""Scan swinglab run trees for the tracker's self-check signature (tracker_robustness, Oct 2026).

For every ``<run>/result.json`` under the given roots, print one row with:

  clean      P1 in [80,120] and P3 in [235,310] (the 29 Sept sanity criterion)
  refused    analysis.club.refused (the tracker's own verdict; absent on pre-v5 runs)
  rule       analysis.club.diag.onsetRule (0 walk-back, 1 reseed, 2 A3 near pin, 3 A3 far pin)
  retry      analysis.club.diag.phaseRetries
  suspect    analysis.club.diag.phaseSuspect
  pin        Takeaway == impact - 0.55 s (+- 8 ms) recomputed from the phases (works on old runs)
  noP23      P2 or P3 missing
  P1/P3      degrees
  dP1        |P1 - theta_ball|, theta_ball from the diag when present, else the found-ball median
  lenRatio   A1 length / grip->ball at P1 (diag when present, else recomputed)
  cov        coverage

Usage:
  robust_scan.py ROOT [ROOT ...] [--csv out.csv] [--all]

Rows are printed for every run with --all, else only the runs that fail any check.
"""
import argparse, csv, glob, json, math, os, sys

W_DEFAULT, H_DEFAULT = 1280, 1024


def analyse(path):
    r = json.load(open(path))
    a = r.get('analysis', r)
    c = a.get('club', {})
    W = c.get('frameWidth') or W_DEFAULT
    H = c.get('frameHeight') or H_DEFAULT
    pos = {q['p']: q for q in c.get('positions', [])}
    ph = {}
    imp = None
    for e in a.get('phases', []):
        ph.setdefault(e['phase'], e['t_us'])
        if e['phase'] == 5:
            imp = e['t_us']
    take = ph.get(1)
    pin = imp is not None and take is not None and abs((imp - take) - 550000) < 8000
    p1, p3 = pos.get(1), pos.get(3)
    th1 = math.degrees(p1['theta']) if p1 else None
    th3 = math.degrees(p3['theta']) if p3 else None
    clean = th1 is not None and 80 <= th1 <= 120 and th3 is not None and 235 <= th3 <= 310
    diag = c.get('diag', {})
    dP1 = diag.get('p1BallDeltaDeg', -1)
    ratio = diag.get('lenBallRatio', -1)
    if (dP1 is None or dP1 < 0) and p1:
        fs = [s for s in a.get('ball', {}).get('samples', []) if s.get('found') and 0.5e6 < s['t_us'] <= p1['t_us']]
        if len(fs) >= 5:
            xs = sorted(s['x'] for s in fs); ys = sorted(s['y'] for s in fs)
            bx, by = xs[len(xs) // 2] * W, ys[len(ys) // 2] * H
            gx, gy = p1['grip'][0] * W, p1['grip'][1] * H
            tb = math.degrees(math.atan2(by - gy, bx - gx))
            d = ((th1 - tb + 180) % 360) - 180
            dP1 = abs(d)
            dist = math.hypot(bx - gx, by - gy)
            if c.get('measuredClubLenPx', -1) > 0 and dist > 1:
                ratio = c['measuredClubLenPx'] / dist
    return dict(clean=clean, refused=c.get('refused', ''), rule=diag.get('onsetRule', ''),
                retry=diag.get('phaseRetries', ''), suspect=diag.get('phaseSuspect', ''),
                pin=pin, noP23=(2 not in pos or 3 not in pos), P1=th1, P3=th3,
                dP1=dP1 if dP1 is not None and dP1 >= 0 else None,
                lenRatio=ratio if ratio is not None and ratio > 0 else None,
                cov=c.get('coverage'), valid=c.get('valid'), metrics=len(a.get('metrics', [])))


def flagged(r):
    return (not r['clean']) or r['refused'] or r['pin'] or r['noP23'] \
        or (r['dP1'] is not None and r['dP1'] > 25) \
        or (r['lenRatio'] is not None and abs(r['lenRatio'] - 1) > 0.3)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('roots', nargs='+')
    ap.add_argument('--csv')
    ap.add_argument('--all', action='store_true')
    args = ap.parse_args()
    rows = []
    for root in args.roots:
        for p in sorted(glob.glob(os.path.join(root, '**', 'result.json'), recursive=True)):
            try:
                r = analyse(p)
            except Exception as e:  # noqa: BLE001
                print('ERR', p, e, file=sys.stderr)
                continue
            r['run'] = os.path.relpath(os.path.dirname(p), root)
            r['root'] = root
            rows.append(r)
    fmt = lambda v, f: ('-' if v is None or v == '' else (f % v))
    print('%-52s clean refused        rule retry susp pin   noP23  P1   P3   dP1  lenR  cov  metrics' % 'run')
    for r in rows:
        if not args.all and not flagged(r):
            continue
        print('%-52s %-5s %-14s %-4s %-5s %-4s %-5s %-5s %4s %4s %5s %5s %4s %s' % (
            r['run'][:52], r['clean'], r['refused'] or '-', fmt(r['rule'], '%d'), fmt(r['retry'], '%d'),
            fmt(r['suspect'], '%d'), r['pin'], r['noP23'], fmt(r['P1'], '%.0f'), fmt(r['P3'], '%.0f'),
            fmt(r['dP1'], '%.0f'), fmt(r['lenRatio'], '%.2f'), fmt(r['cov'], '%.2f'), r['metrics']))
    n = len(rows)
    nb = [r for r in rows if not r['clean']]
    print('runs %d  broken %d  refused %d  broken&refused %d  clean&refused %d  pin %d  noP23 %d' % (
        n, len(nb), sum(1 for r in rows if r['refused']), sum(1 for r in nb if r['refused']),
        sum(1 for r in rows if r['clean'] and r['refused']), sum(1 for r in rows if r['pin']),
        sum(1 for r in rows if r['noP23'])))
    if args.csv:
        with open(args.csv, 'w', newline='') as f:
            w = csv.DictWriter(f, fieldnames=list(rows[0].keys()) if rows else ['run'])
            w.writeheader()
            for r in rows:
                w.writerow(r)


if __name__ == '__main__':
    main()
