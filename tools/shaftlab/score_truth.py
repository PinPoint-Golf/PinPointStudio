"""Score a shaft track (and optionally a stage-2 head CSV) against hand labels.

  score_truth.py <track.csv> <clipmeta.json>              theta error by kind
  score_truth.py <track.csv> <clipmeta.json> --head <head.csv>
                                                          + head px / length error by kind_h

Truth mapping: nearest clipmeta t_us, 30 ms tolerance (same for both modes).
Head baseline caveat: single-swing numbers are development signals only —
model/detector acceptance is corpus-gated (see docs/validation/).
"""
import argparse, json, csv, math
import numpy as np

ap = argparse.ArgumentParser()
ap.add_argument('track'); ap.add_argument('clipmeta')
ap.add_argument('--head', default=None, help='stage-2 head CSV (from clubhead annotator)')
args = ap.parse_args()

meta = json.load(open(args.clipmeta))
truth = json.load(open(f"{meta['swingDir']}/truth.json"))['shaft']
tt = np.array(meta['t_us'], float)
th, kind = {}, {}
for r in csv.DictReader(open(args.track)):
    f = int(r['frame'])
    if r['theta_out'] not in ('', 'nan'):
        th[f] = math.radians(float(r['theta_out'])); kind[f] = r['kind']
def wd(a,b): return abs(math.atan2(math.sin(a-b), math.cos(a-b)))
res = {'meas': [], 'pred': []}
for h in truth:
    i = int(np.argmin(np.abs(tt - h['t_us'])))
    if abs(tt[i]-h['t_us']) > 30000 or i not in th: continue
    res[kind[i]].append(math.degrees(wd(th[i], h['theta'])))
for k, v in res.items():
    if not v: print(f"{k}: none"); continue
    v = sorted(v); n = len(v)
    bad = sum(1 for e in v if e > 30)
    print(f"{k}: n={n} median={v[n//2]:.1f} mean={sum(v)/n:.1f} p90={v[int(n*0.9)]:.1f} bad>30={bad} ({100*bad/n:.0f}%)")

if args.head:
    hrow = {}
    for r in csv.DictReader(open(args.head)):
        hrow[int(r['frame'])] = r
    pos = {'meas': [], 'pred': []}          # head position error, px
    ln = {'meas': [], 'pred': []}           # |head-grip| length error, px
    honesty = []                            # (pos_err, conf_h) for meas+pred
    n_off = n_off_labeled = 0
    for h in truth:
        i = int(np.argmin(np.abs(tt - h['t_us'])))
        if abs(tt[i]-h['t_us']) > 30000 or i not in hrow: continue
        r = hrow[i]
        if r['kind_h'] == 'off':
            n_off += 1; n_off_labeled += 1 if h.get('head') else 0
            continue
        if not h.get('head') or r['head_x'] == '': continue
        ex, ey = float(r['head_x']) - h['head'][0], float(r['head_y']) - h['head'][1]
        e = math.hypot(ex, ey)
        pos[r['kind_h']].append(e)
        L = h.get('len') or math.hypot(h['head'][0]-h['grip'][0], h['head'][1]-h['grip'][1])
        rh = float(r['r_h']) if r['r_h'] else float('nan')
        ln[r['kind_h']].append(rh - L)
        honesty.append((e, float(r['conf_h'])))
    print('-- head --')
    for k in ('meas', 'pred'):
        v = sorted(pos[k]); n = len(v)
        if not n: print(f"head {k}: none"); continue
        bad = sum(1 for e in v if e > 40)
        dl = ln[k]
        print(f"head {k}: n={n} median={v[n//2]:.1f}px mean={sum(v)/n:.1f} p90={v[int(n*0.9)]:.1f} "
              f"bad>40px={bad} ({100*bad/n:.0f}%)  len_err mean={np.mean(dl):+.1f}px mad={np.median(np.abs(dl)):.1f}")
    if honesty:
        badf = [(e,c) for e,c in honesty if e > 40]
        hi = [(e,c) for e,c in honesty if c >= 0.5]
        hib = sum(1 for e,c in hi if e > 40)
        blo = sum(1 for e,c in badf if c < 0.5)
        print(f"honesty: bad-frames low-conf {blo}/{len(badf) or 1} ({100*blo/max(len(badf),1):.0f}%; want >=67%)  "
              f"high-conf bad {hib}/{len(hi) or 1} ({100*hib/max(len(hi),1):.0f}%; want <=5%)")
    print(f"off frames at labels: {n_off} (of which labeler placed a head: {n_off_labeled})")
