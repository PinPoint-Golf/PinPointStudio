import os, sys, json, numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sim import *
N=int(sys.argv[1]); rows={}
for label, kw in [('A4 x12 edges + stick x8', dict(edges=True)),
                  ('A4 x20 edges + stick x8', dict(edges=True, n_views=20)),
                  ('scorecard x12 edges + stick x8', dict(obj='card', edges=True))]:
    fe, ce, dmax, dmean = [], [], [], []
    for _ in range(N):
        K = intrinsics_trial(**kw)
        fe.append(100*(K[0]/TRUE_K[0]-1)); ce.append(np.hypot(*(K[1:3]-TRUE_K[1:3])))
        a,b = distortion_err_px(K); dmax.append(a); dmean.append(b)
    rows[label]=dict(f_pct_rms=round(float(np.sqrt(np.mean(np.square(fe)))),2), c_px_rms=round(float(np.sqrt(np.mean(np.square(ce)))),1),
                     ray_deg_max=round(float(np.median(dmax)),3), ray_deg_mean=round(float(np.median(dmean)),3))
    print(label, rows[label], flush=True)
json.dump(rows, open('results_intr3.json','w'), indent=1)
