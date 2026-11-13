import os, sys, json, numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sim
from sim import *

N = int(sys.argv[1]) if len(sys.argv) > 1 else 60
JOINT_N = 6
import os
ONLY = os.environ.get('ONLY')

def intr_stats():
    rows = {}
    for label, kw in [('A4 wave only', dict(obj='A4', use_stick=False)),
                      ('A4 wave + stick plumb-line', dict(obj='A4', use_stick=True)),
                      ('scorecard (unknown size) + stick', dict(obj='card', use_stick=True))]:
        fe, ce, dmax, dmean = [], [], [], []
        for _ in range(N):
            K = intrinsics_trial(**kw)
            fe.append(100 * (K[0] / TRUE_K[0] - 1)); ce.append(np.hypot(*(K[1:3] - TRUE_K[1:3])))
            a, b = distortion_err_px(K); dmax.append(a); dmean.append(b)
        rows[label] = dict(f_err_pct_rms=float(np.sqrt(np.mean(np.square(fe)))),
                           c_err_px_rms=float(np.sqrt(np.mean(np.square(ce)))),
                           ray_err_deg_max_med=float(np.median(dmax)),
                           ray_err_deg_mean_med=float(np.median(dmean)))
    tk = [typed_K() for _ in range(N)]
    rows['typed focal length (I1), no distortion'] = dict(
        f_err_pct_rms=3.0, c_err_px_rms=float(np.hypot(*(TRUE_K[1:3] - [W/2, H/2]))),
        ray_err_deg_max_med=float(np.median([distortion_err_px(k)[0] for k in tk])),
        ray_err_deg_mean_med=float(np.median([distortion_err_px(k)[1] for k in tk])))
    return rows

METHODS = [
    ('Design G2: 3 floor sticks, typed f', 'design_G2', 'typed'),
    ('Design G3: + plumbed 4th stick, typed f', 'design_G3', 'typed'),
    ('Design G3, perfect lens', 'design_G3', 'true'),
    ('E1 one stick + sheet + ball, typed f', 'everyday', 'typed'),
    ('E1 one stick + sheet + ball, perfect lens', 'everyday', 'true'),
    ('E1 + upright eyeballed, perfect lens', 'everyday_up', 'true'),
    ('E1 + upright phone-level, perfect lens', 'everyday_up_level', 'true'),
    ('E3 stick moved x3 on sheet jig, perfect lens', 'everyday_3pos', 'true'),
    ('E3 + upright phone-level, perfect lens', 'everyday_3pos_level', 'true'),
    ('E1 one stick + sheet + ball, waved lens', 'everyday', 'wave'),
    ('E1 + upright phone-level, waved lens', 'everyday_up_level', 'wave'),
]

def rel_err(X, P):
    """RMS mm after the best rigid alignment: shape error, independent of where the world frame was put."""
    mx, mp = X.mean(0), P.mean(0)
    U, _, Vt = np.linalg.svd((X - mx).T @ (P - mp))
    D = np.diag([1, 1, np.sign(np.linalg.det(U @ Vt))])
    Rr = (U @ D @ Vt).T
    E = (X - mx) @ Rr.T + mp - P
    return float(np.sqrt(np.mean((E ** 2).sum(1))) * 1000)

def getK(src):
    if src == 'typed': return typed_K()
    if src == 'true': return TRUE_K.copy()
    return intrinsics_trial(obj='A4', use_stick=True, edges=True)

def pose_stats():
    Pv = volume_points()
    out = {}
    for label, layout, src in METHODS:
        if ONLY and ONLY not in src: continue
        rec = {k: [] for k in ['FO_rot', 'FO_pos', 'DTL_rot', 'DTL_pos', 'FO_ground_mm', 'tri_rms', 'tri_p95',
                               'joint_tri_rms', 'joint_tri_p95', 'jointK_tri_rms', 'jointK_tri_p95', 'tri_rel', 'jointK_tri_rel']}
        for trial in range(N):
            S = scene(layout)
            Ks = {n: getK(src) for n in CAMS}
            Os = {n: observe(S, CAMS[n]) for n in CAMS}
            poses = {}
            for n, cam in CAMS.items():
                R, t = solve_single(Ks[n], S, Os[n], cam)
                poses[n] = (R, t)
                rec[n + '_rot'].append(rot_err_deg(R, cam['R']))
                rec[n + '_pos'].append(1000 * np.linalg.norm(-R.T @ t - cam['C']))
            rec['FO_ground_mm'].append(ground_map_err_mm(Ks['FO'], *poses['FO']))
            obs_clean = {n: project(TRUE_K, CAMS[n]['R'], CAMS[n]['t'], Pv)[0] for n in CAMS}
            e = np.linalg.norm(triangulate(Ks, poses, obs_clean) - Pv, axis=1) * 1000
            rec['tri_rms'].append(np.sqrt(np.mean(e ** 2))); rec['tri_p95'].append(np.percentile(e, 95))
            rec['tri_rel'].append(rel_err(triangulate(Ks, poses, obs_clean), Pv))
            if src == 'wave' and trial < JOINT_N:
                Pw = volume_points(150, grid=False)
                wobs = {}
                for n in CAMS:
                    jitter = RNG.normal(0, 0.001, Pw.shape)          # sync + finger bias, 1 mm
                    wobs[n] = project(TRUE_K, CAMS[n]['R'], CAMS[n]['t'], Pw + jitter)[0] + RNG.normal(0, SIG['wave'], (len(Pw), 2))
                jp, _ = solve_joint(Ks, S, Os, wobs, poses)
                e = np.linalg.norm(triangulate(Ks, jp, obs_clean) - Pv, axis=1) * 1000
                rec['joint_tri_rms'].append(np.sqrt(np.mean(e ** 2))); rec['joint_tri_p95'].append(np.percentile(e, 95))
                jp, jk = solve_joint(Ks, S, Os, wobs, poses, refine_K=True)
                e = np.linalg.norm(triangulate(jk, jp, obs_clean) - Pv, axis=1) * 1000
                rec['jointK_tri_rms'].append(np.sqrt(np.mean(e ** 2))); rec['jointK_tri_p95'].append(np.percentile(e, 95))
                rec['jointK_tri_rel'].append(rel_err(triangulate(jk, jp, obs_clean), Pv))
        out[label] = {k: (float(np.median(v)) if v else None) for k, v in rec.items()}
        print(label, json.dumps({k: (round(v, 2) if v is not None else None) for k, v in out[label].items()}), flush=True)
    return out

if __name__ == '__main__':
    what = sys.argv[2] if len(sys.argv) > 2 else 'all'
    res = {}
    if what in ('all', 'intr'):
        res['intrinsics'] = intr_stats(); print(json.dumps(res['intrinsics'], indent=1), flush=True)
    if what in ('all', 'pose'):
        res['pose'] = pose_stats()
    json.dump(res, open(f'results_{what}.json', 'w'), indent=1)
