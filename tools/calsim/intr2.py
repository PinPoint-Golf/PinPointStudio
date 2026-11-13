import os, sys, json, numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sim import *
N=int(sys.argv[1])
rows={}
for label, kw in [('A4 x12, no stick', dict(use_stick=False)),
                  ('A4 x12 + stick x8', dict()),
                  ('A4 x12 + stick x8, centre fixed', dict(fix_c=True)),
                  ('A4 x20 + stick x8', dict(n_views=20)),
                  ('scorecard 200x120 unknown size x12 + stick x8, centre fixed', dict(obj='card', fix_c=True))]:
    fe, ce, dmax, dmean = [], [], [], []
    for _ in range(N):
        K = intrinsics_trial(**kw)
        fe.append(100*(K[0]/TRUE_K[0]-1)); ce.append(np.hypot(*(K[1:3]-TRUE_K[1:3])))
        a,b = distortion_err_px(K); dmax.append(a); dmean.append(b)
    rows[label]=dict(f_pct_rms=round(float(np.sqrt(np.mean(np.square(fe)))),2), c_px_rms=round(float(np.sqrt(np.mean(np.square(ce)))),1),
                     ray_deg_max=round(float(np.median(dmax)),3), ray_deg_mean=round(float(np.median(dmean)),3))
    print(label, rows[label], flush=True)
tk=[typed_K() for _ in range(200)]
rows['typed f, no distortion']=dict(f_pct_rms=3.0, c_px_rms=round(float(np.hypot(*(TRUE_K[1:3]-[W/2,H/2]))),1),
   ray_deg_max=round(float(np.median([distortion_err_px(k)[0] for k in tk])),3), ray_deg_mean=round(float(np.median([distortion_err_px(k)[1] for k in tk])),3))
print('typed', rows['typed f, no distortion'])
json.dump(rows, open('results_intr.json','w'), indent=1)
