import json, csv, math, sys
import numpy as np
meta = json.load(open(sys.argv[2]))
truth = json.load(open(f"{meta['swingDir']}/truth.json"))['shaft']
tt = np.array(meta['t_us'], float)
th, kind = {}, {}
for r in csv.DictReader(open(sys.argv[1])):
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
