"""Plane stack D2, read-only on /mnt/swingdata (13 Nov 2026): the arm lift at the top measured to the ELBOW,
and the 3-D lead elbow where the fit measured it. extract.py's arm-lift definitions with the wrist replaced by
the elbow (Mark, 13 Nov: a bent lead arm makes the arms look lifted and is its own fault). Writes one JSON line
per swing to argv[1]; reads the swing list from per_swing.csv beside it. Nothing is written to the share.

Per swing, at the Top +/- 15 ms:
  b_el / b_wr   down-the-line picture: the lead upper arm (shoulder -> ELBOW) / shoulder -> wrist against the
                shoulder line, + elbow above the line; positions from the smoothed track, confidence from the RAW
                frames (dtl_posture.h View), conf >= 0.30 on all three; b_el_sigma: keypoint sigma x
                max(1, 0.8 / conf) through the angle (the producer's sigma)
  a2_el / a2_wr 3-D fit: asin(a.n), a = lead shoulder -> elbow (wrist), n = the spine with its shoulder-line
                component removed (RESULTS.md a2); joints tier >= 2
  bend3d        3-D lead elbow bend (180 - the angle at the elbow, shoulder-elbow-wrist), tier >= 2 frames
  bend3d_t3     the same, only frames whose lead elbow is HONESTLY measured: stored tier 3 AND the fit's elbow
                within max(3 sigma, 15 px) of the elbow keypoint in BOTH views (FB's rule replayed on the stored
                v17 fits; the v18 fit applies it itself)
  bend3d_t3_p1  the same at Address
  svd           FB's shoulder-views check at the top (median line angle; > 7 deg fires)
Summarise with d2_summary.py.
"""
import sys, os, csv, math, json
import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "..", "tools"))
from pp_swingdoc import load_swing

D = math.pi / 180


def cams(sk):
    c = sk['camera']
    return dict(fF=c['fF'], pF=c['pFDeg'] * D, rF=c['rFDeg'] * D, cD=np.array(c['cD']), psiD=c['psiDDeg'] * D,
                pD=c['pDDeg'] * D, fD=c['fD'], rD=c['rDDeg'] * D, zG=c['zG'])


def camframe(cm, view, W, H):
    """The stored fit's camera as a pinhole frame (skeleton3d_fit.cpp camFrame)."""
    if view == 0:
        p = cm['pF']; c = np.zeros(3); fwd = np.array([0, math.cos(p), -math.sin(p)]); up = np.array([0, math.sin(p), math.cos(p)])
        right = np.array([1., 0, 0]); down = -up; f = cm['fF']; r = cm['rF']
    else:
        f0 = np.array([math.cos(cm['psiD']), math.sin(cm['psiD']), 0]); Z = np.array([0, 0, 1.])
        c = cm['cD']; fwd = f0 * math.cos(cm['pD']) - Z * math.sin(cm['pD']); up = f0 * math.sin(cm['pD']) + Z * math.cos(cm['pD'])
        right = np.cross(f0, Z); down = -up; f = cm['fD']; r = cm['rD']
    if r:
        R0, D0 = right.copy(), down.copy(); right = R0 * math.cos(r) + D0 * math.sin(r); down = D0 * math.cos(r) - R0 * math.sin(r)
    return dict(c=c, right=right, down=down, fwd=fwd, f=f, cx=W / 2, cy=H / 2)


def project(cf, p):
    rel = p - cf['c']; x = rel @ cf['right']; y = rel @ cf['down']; z = rel @ cf['fwd']
    return np.array([cf['cx'] + cf['f'] * x / z, cf['cy'] + cf['f'] * y / z])


def joints(fr):
    return np.array(fr['p'], float).reshape(27, 3) / 1000.0

W = 15000
CONF = 0.30
def unit(v):
    n = np.linalg.norm(v); return v / n if n > 1e-12 else None

def sigma_of(sig, tier, conf):
    if tier == 2: s = max(1.5, sig if sig > 0 else 2.0)
    elif tier == 1: s = 1.5 * max(3.0, sig if sig > 0 else 4.0)
    else: s = 6.0 if conf >= 0.5 else 0.0
    return s

def a2(Ls, E, Ts, Lh, Th):
    s = Ts - Ls; sh = unit(s)
    spine = 0.5 * (Ls + Ts) - 0.5 * (Lh + Th)
    if sh is None: return None
    n = unit(spine - np.dot(spine, sh) * sh); a = unit(E - Ls)
    if n is None or a is None: return None
    return math.degrees(math.asin(max(-1, min(1, float(a @ n)))))

def bend(S, E, Wr):
    u = unit(S - E); v = unit(Wr - E)
    if u is None or v is None: return None
    return 180.0 - math.degrees(math.acos(max(-1, min(1, float(u @ v)))))

def img_angle(L, T, X):
    s = np.array([T[0] - L[0], -(T[1] - L[1])]); a = np.array([X[0] - L[0], -(X[1] - L[1])])
    sh, ah = unit(s), unit(a)
    if sh is None or ah is None: return None, None, None
    c = max(-1, min(1, float(sh @ ah)))
    mag = math.degrees(math.acos(c))
    nup = np.array([-sh[1], sh[0]])
    if nup[1] < 0: nup = -nup
    return (mag if float(a @ nup) >= 0 else -mag), np.linalg.norm(s), np.linalg.norm(a)

R = list(csv.DictReader(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'per_swing.csv'))))
out = open(sys.argv[1], 'w')
for r in R:
    try: d = load_swing(r['path'])
    except Exception as e:
        print('skip', r['path'], e, file=sys.stderr); continue
    a = d['analysis']; rec = {k: r[k] for k in ('golfer', 'session', 'swing', 'club', 'path')}
    rh = r.get('handedness', 'Right') == 'Right'
    lad = {}
    for e in a.get('phases', []):
        if 'phase' in e and int(e['phase']) not in lad: lad[int(e['phase'])] = int(e['t_us'])
    top = lad.get(2); addr = lad.get(0)
    if top is None:
        out.write(json.dumps(rec) + '\n'); continue
    # ---- DTL image
    pd = a.get('poseDtl')
    dW = dH = None
    for s in d.get('streams', []):
        if 'DTL' in str(s.get('alias', '')).upper() and (s.get('encoded') or {}).get('width'):
            dW, dH = float(s['encoded']['width']), float(s['encoded']['height'])
    if pd and dW:
        raw = pd['frames']; sm = pd.get('smoothed') or []
        par = len(sm) == len(raw)
        F = sm if par else raw
        kls, kts, kle, klw = (5, 6, 7, 9) if rh else (6, 5, 8, 10)
        bel, bwr, sig, ctr = [], [], [], []
        for i, f in enumerate(F):
            if abs(int(f['t_us']) - top) > W: continue
            rc = raw[i]['kp']
            conf = lambda k: rc[3 * k + 2]
            kp = f['kp']
            P_ = lambda k: (kp[3 * k] * dW, kp[3 * k + 1] * dH)
            if min(conf(kls), conf(kts), conf(kle)) >= CONF:
                v, ls_, la = img_angle(P_(kls), P_(kts), P_(kle))
                if v is not None:
                    bel.append(v); ctr.append(conf(kts))
                    if par and 'sigma' in f:
                        sg = lambda k: f['sigma'][k] * max(1.0, 0.8 / max(conf(k), 1e-6)) if f['sigma'][k] > 0 else 0
                        sL, sT, sE = sg(kls), sg(kts), sg(kle)
                        if sL > 0 and sT > 0 and sE > 0:
                            cth = math.cos(math.radians(abs(v)))
                            var = sT**2 / ls_**2 + sE**2 / la**2 + sL**2 * (1 / ls_**2 + 1 / la**2 - 2 * cth / (ls_ * la))
                            sig.append(math.degrees(math.sqrt(max(var, 0))))
            if min(conf(kls), conf(kts), conf(klw)) >= CONF:
                v, _, _ = img_angle(P_(kls), P_(kts), P_(klw))
                if v is not None: bwr.append(v)
        if bel: rec['b_el'] = float(np.median(bel)); rec['b_el_n'] = len(bel); rec['b_trailConf'] = float(np.median(ctr))
        if sig: rec['b_el_sigma'] = float(np.median(sig))
        if bwr: rec['b_wr'] = float(np.median(bwr))
    # ---- 3-D fit
    sk = a.get('skeleton3d')
    foW = foH = None
    for s in d.get('streams', []):
        if str(s.get('alias', '')) == 'Face-On' and (s.get('encoded') or {}).get('width'):
            foW, foH = float(s['encoded']['width']), float(s['encoded']['height'])
    if 'club' in a and a['club'].get('frameWidth'): foW, foH = a['club']['frameWidth'], a['club']['frameHeight']
    if sk and sk.get('valid') and sk.get('dtl') and foW and dW:
        cW, cH = dW, dH
        if 'clubDtl' in a and a['clubDtl'].get('frameWidth'): cW, cH = a['clubDtl']['frameWidth'], a['clubDtl']['frameHeight']
        cm = cams(sk); cf0 = camframe(cm, 0, foW, foH); cf1 = camframe(cm, 1, cW, cH)
        mir = bool(sk.get('foMirrored'))
        def track(trk, Wd, Hd, mirror):
            F = trk.get('smoothed') or trk['frames']
            return np.array([f['t_us'] for f in F]), F, Wd, Hd, mirror
        FO = track(a['pose2d'], foW, foH, mir); DT = track(a['poseDtl'], cW, cH, False)
        def obs(T, t, m):
            ts, F, Wd, Hd, mirror = T
            i = int(np.argmin(abs(ts - t)))
            if abs(ts[i] - t) > 6000: return None
            f = F[i]; kp = f['kp']
            u, v, c = kp[3 * m] * Wd, kp[3 * m + 1] * Hd, kp[3 * m + 2]
            if mirror: u = Wd - u
            sg = f['sigma'][m] if 'sigma' in f else 0
            tr = f['tier'][m] if 'tier' in f else 2
            return u, v, c, sigma_of(sg, tr, c)
        LS, LE, LW, TS = (8, 9, 10, 13) if rh else (13, 14, 15, 8)
        kEl = 7 if rh else 8
        A2e, A2w, B3, B3t, B3t1, svd, ntop, nt3 = [], [], [], [], [], [], 0, 0
        for fr in sk['frames']:
            t = int(fr['t']); tier = [int(x) for x in fr['tier']]; J = joints(fr)
            attop = abs(t - top) <= W; atad = addr is not None and abs(t - addr) <= W
            if not (attop or atad): continue
            ok2 = lambda *js: min(tier[j] for j in js) >= 2
            if attop and ok2(LS, LE, TS, 17, 22):
                v = a2(J[LS], J[LE], J[TS], J[17], J[22]);  v is not None and A2e.append(v)
            if attop and ok2(LS, LW, TS, 17, 22):
                v = a2(J[LS], J[LW], J[TS], J[17], J[22]);  v is not None and A2w.append(v)
            if ok2(LS, LE, LW):
                b = bend(J[LS], J[LE], J[LW])
                if attop: B3.append(b); ntop += 1
                honest = tier[LE] == 3
                if honest:
                    for T, cf in ((FO, cf0), (DT, cf1)):
                        o = obs(T, t, kEl)
                        if not o or o[3] <= 0: honest = False; break
                        uv = project(cf, J[LE])
                        if math.hypot(uv[0] - o[0], uv[1] - o[1]) > max(3.0 * o[3], 15.0): honest = False; break
                if honest:
                    if attop: B3t.append(b); nt3 += 1
                    if atad: B3t1.append(b)
            if attop:
                o5, o6 = obs(DT, t, 5), obs(DT, t, 6)
                if o5 and o6 and o5[2] >= 0.7 and o6[2] >= 0.7 and math.hypot(o6[0] - o5[0], o6[1] - o5[1]) >= 20:
                    p, q = project(cf1, J[8]), project(cf1, J[13])
                    if math.hypot(q[0] - p[0], q[1] - p[1]) >= 1:
                        x = math.atan2(o6[1] - o5[1], o6[0] - o5[0]) - math.atan2(q[1] - p[1], q[0] - p[0])
                        svd.append(abs(math.degrees(math.remainder(x, math.pi))))
        if A2e: rec['a2_el'] = float(np.median(A2e))
        if A2w: rec['a2_wr'] = float(np.median(A2w))
        if B3: rec['bend3d'] = float(np.median(B3))
        if B3t: rec['bend3d_t3'] = float(np.median(B3t))
        if B3t1: rec['bend3d_t3_p1'] = float(np.median(B3t1))
        rec['bend_top_frames'] = ntop; rec['bend_top_t3'] = nt3
        if svd: rec['svd'] = float(np.median(svd))
    out.write(json.dumps(rec) + '\n'); out.flush()
    del d, a
out.close()
