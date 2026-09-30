#!/usr/bin/env python3
"""Compare two swinglab run roots swing by swing (tracker_robustness gate, Oct 2026).

  robust_compare.py BASE_ROOT NEW_ROOT [--md out.md]

For every run present in both roots (same relative dir with result.json):
  theta     per-frame |Δθ| between the two tracks over the matched samples: p50 / p90 / max (deg),
            and the count of frames that differ at all (0 = byte-identical θ)
  P1..P8    Δt (ms) and Δθ (deg) per position present in both; '-' when only one has it
  band      the BAND-lock yardstick on taped frames: |theta_out − band_theta| p50/p90 over the
            frames the trace marks band-locked (needs trace.jsonl in both roots)
  cov       coverage base → new, valid base → new
  metrics   metric count base → new
  diag      new run's onsetRule / phaseRetries / phaseSuspect / refused / hands cleaned

The summary at the end counts identical-θ swings, positions moved > 30 ms, coverage drops > 0.05,
metric losses, refusals.
"""
import argparse, glob, json, math, os, sys


def load(run):
    res = json.load(open(os.path.join(run, 'result.json')))
    a = res.get('analysis', res)
    tr = []
    tp = os.path.join(run, 'trace.jsonl')
    if os.path.exists(tp):
        for line in open(tp):
            d = json.loads(line)
            if 'summary' not in d:
                tr.append(d)
    return a, tr


def wrap(d):
    return (d + 180.0) % 360.0 - 180.0


def pct(v, p):
    if not v:
        return float('nan')
    s = sorted(v)
    k = (len(s) - 1) * p / 100.0
    f = int(math.floor(k)); c = min(f + 1, len(s) - 1)
    return s[f] + (s[c] - s[f]) * (k - f)


def band_err(tr):
    e = []
    for d in tr:
        if d.get('tier') == 'band' and 'band_theta' in d:
            e.append(abs(wrap(d['theta_out'] - d['band_theta'])))
    return e


def compare(base, new):
    a, ta = load(base)
    b, tb = load(new)
    ca, cb = a.get('club', {}), b.get('club', {})
    out = {}
    sa = {s['t_us']: s for s in ca.get('samples', [])}
    sb = {s['t_us']: s for s in cb.get('samples', [])}
    diffs = [abs(wrap(math.degrees(sb[t]['theta'] - sa[t]['theta']))) for t in sa if t in sb]
    out['nMatched'] = len(diffs)
    out['nDiff'] = sum(1 for d in diffs if d > 1e-6)
    out['th50'], out['th90'], out['thMax'] = pct(diffs, 50), pct(diffs, 90), (max(diffs) if diffs else float('nan'))
    pa = {q['p']: q for q in ca.get('positions', [])}
    pb = {q['p']: q for q in cb.get('positions', [])}
    pos = {}
    for p in range(1, 9):
        if p in pa and p in pb:
            pos[p] = ((pb[p]['t_us'] - pa[p]['t_us']) / 1000.0, wrap(math.degrees(pb[p]['theta'] - pa[p]['theta'])))
        elif p in pa or p in pb:
            pos[p] = ('only ' + ('base' if p in pa else 'new'), None)
    out['pos'] = pos
    ea, eb = band_err(ta), band_err(tb)
    out['band'] = (pct(ea, 50), pct(ea, 90), len(ea), pct(eb, 50), pct(eb, 90), len(eb))
    out['cov'] = (ca.get('coverage'), cb.get('coverage'), ca.get('valid'), cb.get('valid'))
    out['metrics'] = (len(a.get('metrics', [])), len(b.get('metrics', [])))
    d = cb.get('diag', {})
    out['diag'] = (d.get('onsetRule'), d.get('phaseRetries'), d.get('phaseSuspect'), cb.get('refused', ''),
                   d.get('handPairFixed'), d.get('handGlitchFixed'))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('base'); ap.add_argument('new'); ap.add_argument('--md')
    args = ap.parse_args()
    runs = sorted(os.path.relpath(os.path.dirname(p), args.base)
                  for p in glob.glob(os.path.join(args.base, '**', 'result.json'), recursive=True))
    rows = []
    for r in runs:
        nb = os.path.join(args.new, r)
        if not os.path.exists(os.path.join(nb, 'result.json')):
            print('missing in new:', r, file=sys.stderr); continue
        try:
            c = compare(os.path.join(args.base, r), nb)
        except Exception as e:  # noqa: BLE001
            print('ERR', r, e, file=sys.stderr); continue
        c['run'] = r
        rows.append(c)
    lines = []
    hdr = '| run | θ frames≠ | θ p50/p90/max | P1 Δt/Δθ | P3 | P4 | P7 | band p50/p90 base→new (n) | cov | metrics | rule/retry/susp/refused/hands |'
    lines.append(hdr); lines.append('|' + '---|' * 11)
    fp = lambda p, c: ('-' if p not in c['pos'] else (c['pos'][p][0] if c['pos'][p][1] is None else '%+.0f/%+.1f' % c['pos'][p]))
    for c in rows:
        b = c['band']
        lines.append('| %s | %d/%d | %.2f/%.2f/%.1f | %s | %s | %s | %s | %.2f/%.2f→%.2f/%.2f (%d→%d) | %.2f→%.2f %s | %d→%d | %s/%s/%s/%s/%s+%s |' % (
            c['run'], c['nDiff'], c['nMatched'], c['th50'], c['th90'], c['thMax'], fp(1, c), fp(3, c), fp(4, c), fp(7, c),
            b[0], b[1], b[3], b[4], b[2], b[5], c['cov'][0] or 0, c['cov'][1] or 0,
            '' if c['cov'][2] == c['cov'][3] else ('VALID→invalid' if c['cov'][2] else 'invalid→VALID'),
            c['metrics'][0], c['metrics'][1], c['diag'][0], c['diag'][1], c['diag'][2], c['diag'][3] or '-', c['diag'][4], c['diag'][5]))
    n = len(rows)
    ident = sum(1 for c in rows if c['nDiff'] == 0)
    moved = sum(1 for c in rows if any(v[1] is not None and abs(v[0]) > 30 for v in c['pos'].values()))
    covdrop = sum(1 for c in rows if (c['cov'][0] or 0) - (c['cov'][1] or 0) > 0.05)
    lost = sum(1 for c in rows if c['metrics'][1] < c['metrics'][0])
    refused = sum(1 for c in rows if c['diag'][3])
    retried = sum(1 for c in rows if (c['diag'][1] or 0) > 0)
    cleaned = sum(1 for c in rows if (c['diag'][4] or 0) + (c['diag'][5] or 0) > 0)
    allb = [c['band'] for c in rows if c['band'][2] > 0]
    summ = ('runs %d | θ byte-identical %d | any P moved >30 ms %d | coverage drop >0.05 %d | metric loss %d | refused %d | phase retried %d | hands cleaned %d | band-locked runs %d'
            % (n, ident, moved, covdrop, lost, refused, retried, cleaned, len(allb)))
    lines.append(''); lines.append(summ)
    text = '\n'.join(lines)
    print(text)
    if args.md:
        open(args.md, 'w').write(text + '\n')


if __name__ == '__main__':
    main()
