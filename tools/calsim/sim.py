"""Monte Carlo: everyday-kit camera calibration (docs/design/camera_calibration_design.md §3b.7) for a PinPoint bay (face-on + optional DTL).

Bay frame: origin ball spot, X target, Z up, golfer on +Y.
Cameras: FLIR-class 1280x1024, f~900 px (4.3 mm on 4.8 um), wide-lens barrel distortion.
All solves are least squares on the stated observations; noise levels are stated inline.
"""
import numpy as np
from scipy.optimize import least_squares
from scipy.spatial.transform import Rotation as Rot

RNG = np.random.default_rng(1)
W, H = 1280, 1024
STICK = 1.219
BALL_R = 0.02134
STICK_R = 0.006

# ---------------- camera model ----------------
def look_R(C, target, roll_deg=0.0):
    z = target - C; z /= np.linalg.norm(z)
    x = np.cross(z, [0, 0, 1.0]); x /= np.linalg.norm(x)
    y = np.cross(z, x)
    R = np.vstack([x, y, z])            # world -> cam
    if roll_deg:
        R = Rot.from_euler('z', roll_deg, degrees=True).as_matrix() @ R
    return R

def distort(xn, k1, k2):
    r2 = (xn ** 2).sum(-1, keepdims=True)
    return xn * (1 + k1 * r2 + k2 * r2 * r2)

def undistort(xd, k1, k2, it=12):
    xn = xd.copy()
    for _ in range(it):
        r2 = (xn ** 2).sum(-1, keepdims=True)
        xn = xd / (1 + k1 * r2 + k2 * r2 * r2)
    return xn

def project(K, R, t, X):
    f, cx, cy, k1, k2 = K
    Xc = X @ R.T + t
    xn = Xc[:, :2] / Xc[:, 2:3]
    xd = distort(xn, k1, k2)
    return xd * f + [cx, cy], Xc[:, 2]

def to_ideal(K, uv):
    """Observed (distorted) px -> ideal pinhole px under intrinsics K."""
    f, cx, cy, k1, k2 = K
    xd = (uv - [cx, cy]) / f
    return undistort(xd, k1, k2) * f + [cx, cy]

def in_img(uv, z, m=5):
    return (z > 0.1) & (uv[:, 0] > m) & (uv[:, 0] < W - m) & (uv[:, 1] > m) & (uv[:, 1] < H - m)

import os
TRUE_K = np.array([float(x) for x in os.environ.get('TRUE_K', '900,648,506,-0.28,0.09').split(',')])

CAMS = {
    'FO':  dict(C=np.array([0.0, -1.40, 0.90]), aim=np.array([0.0, 0.5, float(os.environ.get('FO_AIM_Z', '0.62'))]), roll=1.0),
    'DTL': dict(C=np.array([-2.00, 0.35, 1.10]), aim=np.array([0.5, 0.35, 0.68]), roll=-1.5),
}
for c in CAMS.values():
    c['R'] = look_R(c['C'], c['aim'], c['roll'])
    c['t'] = -c['R'] @ c['C']

# ---------------- intrinsics from a waved flat object (+ stick plumb-line) ----------------
def rand_view_pose(dist_lo, dist_hi, obj_half):
    """Random pose of a planar object: centre somewhere in the frame, tilted 15–45°."""
    for _ in range(200):
        d = RNG.uniform(dist_lo, dist_hi)
        u, v = RNG.uniform(0.12 * W, 0.88 * W), RNG.uniform(0.12 * H, 0.88 * H)
        xn = undistort(((np.array([[u, v]]) - TRUE_K[1:3]) / TRUE_K[0]), *TRUE_K[3:])[0]
        centre = np.array([xn[0], xn[1], 1.0]) * d
        tilt = np.deg2rad(RNG.uniform(15, 45)); ax = RNG.normal(size=2); ax /= np.linalg.norm(ax)
        Rv = Rot.from_rotvec(np.array([ax[0], ax[1], 0]) * tilt).as_matrix() @ \
             Rot.from_euler('z', RNG.uniform(-40, 40), degrees=True).as_matrix()
        yield_ok = True
        corners = obj_half  # (N,3) object points
        Xc = corners @ Rv.T + centre
        uv, z = project(TRUE_K, np.eye(3), np.zeros(3), Xc)
        if in_img(uv, z, 15).all():
            return Rv, centre
    raise RuntimeError('no view')

def intrinsics_trial(obj='A4', n_views=12, n_stick=8, use_stick=True, corner_sigma=0.4,
                     flat_sigma_mm=0.5, stick_sigma=0.5, bow_mm=1.0, fix_c=False, edges=False):
    if obj == 'A4':
        w, h = 0.297, 0.210; known_aspect = True
    else:  # scorecard: stiff card of unknown aspect
        w, h = 0.200, 0.120; known_aspect = False
    rect = np.array([[-w/2, -h/2, 0], [w/2, -h/2, 0], [w/2, h/2, 0], [-w/2, h/2, 0]])
    views = []
    for _ in range(n_views):
        Rv, c = rand_view_pose(0.9, 1.5, rect)
        Xo = rect + np.c_[np.zeros((4, 2)), RNG.normal(0, flat_sigma_mm / 1000, 4)]
        uv, _ = project(TRUE_K, Rv, c, Xo)
        E = []
        if edges:
            for i in range(4):
                a, b = Xo[i], Xo[(i + 1) % 4]
                pts = seg(a, b, 40)[3:-3]
                pe, _ = project(TRUE_K, Rv, c, pts)
                E.append(pe + RNG.normal(0, stick_sigma, pe.shape))
        views.append((Rv, c, uv + RNG.normal(0, corner_sigma, uv.shape), E))
    sticks = []
    if use_stick:
        for _ in range(n_stick):
            d = RNG.uniform(1.1, 1.8)
            ang = RNG.uniform(0, np.pi)
            off = RNG.uniform(-0.35, 0.35, 2) * d
            s = np.linspace(-STICK / 2, STICK / 2, 120)
            dirv = np.array([np.cos(ang), np.sin(ang), RNG.uniform(-0.3, 0.3)])
            dirv /= np.linalg.norm(dirv)
            bow = RNG.normal(0, bow_mm / 1000) * (1 - (2 * s / STICK) ** 2)
            perp = np.cross(dirv, [0, 0, 1.0]); perp /= np.linalg.norm(perp)
            X = np.array([off[0], off[1], d]) + s[:, None] * dirv + bow[:, None] * perp
            uv, z = project(TRUE_K, np.eye(3), np.zeros(3), X)
            m = in_img(uv, z, 10)
            if m.sum() > 30:
                idx = np.where(m)[0][::3]
                sticks.append(uv[idx] + RNG.normal(0, stick_sigma, (len(idx), 2)))

    # unknowns: f, cx, cy, k1, k2, [aspect], per-view rvec(3) t(3)
    def unpack(p):
        if fix_c:
            K = np.array([p[0], W / 2, H / 2, p[1], p[2]]); p = np.r_[p[0], 0, 0, p[1:]]
        else:
            K = p[:5]
        i = 5
        a = h / w
        if not known_aspect:
            a = p[5]; i = 6
        return K, a, p[i:].reshape(-1, 6)

    def resid(p):
        K, a, poses = unpack(p)
        rect_m = np.array([[-0.5, -a/2, 0], [0.5, -a/2, 0], [0.5, a/2, 0], [-0.5, a/2, 0]]) * (w if known_aspect else 1.0)
        out = []
        for (Rv, c, uv, E), q in zip(views, poses):
            R = Rot.from_rotvec(q[:3]).as_matrix()
            pr, _ = project(K, R, q[3:], rect_m)
            out.append(((pr - uv) / corner_sigma).ravel())
            for i, pe in enumerate(E):
                out.append(line_resid(K, R, q[3:], rect_m[i], rect_m[(i + 1) % 4], to_ideal(K, pe), stick_sigma))
        for uv in sticks:
            ideal = to_ideal(K, uv)
            mu = ideal.mean(0); _, _, vt = np.linalg.svd(ideal - mu)
            out.append(((ideal - mu) @ vt[1]) / stick_sigma)
        return np.concatenate(out)

    p0 = list(TRUE_K * [1 + RNG.normal(0, 0.08), 1, 1, 0, 0]
              + np.r_[0, RNG.normal(0, 15), RNG.normal(0, 15), 0, 0])
    p0[1] = W / 2; p0[2] = H / 2; p0[3] = 0.0; p0[4] = 0.0
    if not known_aspect:
        p0.append(0.5)
    scale0 = 1.0 if known_aspect else 1 / w
    for Rv, c, _, _ in views:
        p0 += list(Rot.from_matrix(Rv).as_rotvec() + RNG.normal(0, 0.05, 3)) + list(c * scale0 * (1 + RNG.normal(0, 0.05)))
    p0 = np.array(p0)
    if fix_c: p0 = np.r_[p0[0], p0[3:]]
    sol = least_squares(resid, p0, method='lm', x_scale='jac', max_nfev=1500)
    if fix_c: return np.array([sol.x[0], W / 2, H / 2, sol.x[1], sol.x[2]])
    return sol.x[:5]

def distortion_err_px(K):
    """Worst ideal-image error across the frame from using K instead of the truth."""
    g = np.stack(np.meshgrid(np.linspace(0, W, 17), np.linspace(0, H, 14)), -1).reshape(-1, 2)
    xn = undistort((g - TRUE_K[1:3]) / TRUE_K[0], *TRUE_K[3:])
    ideal_true = xn * TRUE_K[0] + TRUE_K[1:3]
    ideal_est = to_ideal(K, g)
    # compare in angle: back to rays
    r_t = np.c_[(ideal_true - TRUE_K[1:3]) / TRUE_K[0], np.ones(len(g))]
    r_e = np.c_[(ideal_est - K[1:3]) / K[0], np.ones(len(g))]
    r_t /= np.linalg.norm(r_t, axis=1, keepdims=True); r_e /= np.linalg.norm(r_e, axis=1, keepdims=True)
    ang = np.arccos(np.clip((r_t * r_e).sum(1), -1, 1))
    return np.degrees(ang).max(), np.degrees(ang).mean()

def typed_K():
    """I1 as the current design has it: a typed nominal focal length (±3 %), centre assumed, no distortion."""
    return np.array([TRUE_K[0] * (1 + RNG.normal(0, 0.03)), W / 2, H / 2, 0.0, 0.0])

# ---------------- ground scene ----------------
def seg(a, b, n=200):
    s = np.linspace(0, 1, n)[:, None]
    return a + s * (b - a)

TGT_Y = -(BALL_R + STICK_R)          # stick axis just in front of the ball (camera side)

def scene(kind):
    """Truth for one static layout. Returns dict of named 3D features (with truth perturbations)."""
    S = {}
    z = STICK_R
    if kind in ('design_G2', 'design_G3', 'design_G3_I2'):
        # camera_pose_sticks.h / two_camera_capture_protocol §2: target, hands-line (parallel), cross.
        # Placement as the protocol achieves it: parallel offsets matched ±5 mm at both ends over
        # 1.2 m (±0.25°), cross squared with 3-4-5 (±0.3°). The solver assumes them perfect.
        S['line_target'] = (np.array([-0.61, 0, z]), np.array([0.61, 0, z]))
        PL = float(os.environ.get('DESIGN_PLACEMENT', '1'))   # 0 = perfectly placed sticks
        a = np.deg2rad(RNG.normal(0, 0.25 * PL + 1e-12)); dy = 0.61 * np.tan(a)
        S['line_hands'] = (np.array([-0.61, 0.40 - dy, z]), np.array([0.61, 0.40 + dy, z]))
        b = np.deg2rad(RNG.normal(0, 0.3 * PL + 1e-12)); dx = 0.61 * np.tan(b)
        S['line_cross'] = (np.array([0 + dx, -0.61, z + 0.012]), np.array([0 - dx, 0.61, z + 0.012]))
        S['model_overrides'] = {'line_hands': (np.array([-0.61, 0.40, z]), np.array([0.61, 0.40, z])),
                                'line_cross': (np.array([0, -0.61, z + 0.012]), np.array([0, 0.61, z + 0.012]))}
        S['pt_target_end'] = np.array([0.61, 0, z])
        S['ball'] = np.array([0.0, 0.0, BALL_R])
        if kind != 'design_G2':
            tilt = Rot.from_rotvec(np.r_[RNG.normal(0, np.deg2rad(0.3), 2), 0]).as_matrix()
            top = tilt @ np.array([0, 0, STICK])
            S['line_vert'] = (np.array([0.10, 0.0, 0]), np.array([0.10, 0.0, 0]) + top)
            S['pt_vert_top'] = S['line_vert'][1]
            S['vert_prior_deg'] = 0.3
        return S
    else:
        # everyday: ONE stick on the target line, ball against it, A4 flat on the far side of the
        # stick, long edge butted to it, crease (fold in half) at the ball.
        S['line_target'] = (np.array([-0.61, TGT_Y, z]), np.array([0.61, TGT_Y, z]))
        S['pt_target_end'] = np.array([0.61, TGT_Y, z])
        S['ball'] = np.array([0.0, 0.0, BALL_R])
        # paper placement error: along-line ±5 mm, butted so angle ±0.4°, curl 0.5 mm
        dx, dth = RNG.normal(0, 0.005), np.deg2rad(RNG.normal(0, 0.4))
        # paper on the GOLFER side of the stick, long edge butted to it; the ball sits on it at the crease
        y0 = TGT_Y + STICK_R
        loc = np.array([[-0.1485, 0], [0.1485, 0], [0.1485, 0.210], [-0.1485, 0.210]])
        Rz = np.array([[np.cos(dth), -np.sin(dth)], [np.sin(dth), np.cos(dth)]])
        P = loc @ Rz.T + [dx, y0]
        S['paper'] = np.c_[P, RNG.normal(0, 0.0005, 4)]
        S['paper_model'] = np.c_[loc + [0, y0], np.zeros(4)]
        if kind in ('everyday_3pos', 'everyday_3pos_level'):
            # the SAME stick moved between captures, the sheet used as a jig: (2) butted against the
            # sheet's far long edge — parallel, 210 mm + one stick diameter away; (3) butted against
            # the sheet's target-side short edge — square. Butting error ±0.25° on the long edge,
            # ±0.35° on the short edge, ±1 mm gap. The solver assumes them perfect.
            yH = y0 + 0.210 + STICK_R
            a = np.deg2rad(RNG.normal(0, 0.25)); dy = 0.61 * np.tan(a); g = RNG.normal(0, 0.001)
            S['line_hands'] = (np.array([-0.61, yH + g - dy, z]), np.array([0.61, yH + g + dy, z]))
            xC = 0.1485 + STICK_R
            b = np.deg2rad(RNG.normal(0, 0.35)); dx = 0.61 * np.tan(b); g2 = RNG.normal(0, 0.001)
            S['line_cross'] = (np.array([xC + g2 + dx, -0.40, z]), np.array([xC + g2 - dx, 0.82, z]))
            S['model_overrides'] = {'line_hands': (np.array([-0.61, yH, z]), np.array([0.61, yH, z])),
                                    'line_cross': (np.array([xC, -0.40, z]), np.array([xC, 0.82, z]))}
        if kind in ('everyday_up_level', 'everyday_3pos_level'):
            # upright at the target end, plumbed with a phone level app (±0.3°)
            tilt = Rot.from_rotvec(np.r_[RNG.normal(0, np.deg2rad(0.3), 2), 0]).as_matrix()
            base = np.array([0.55, TGT_Y, 0])
            S['line_vert'] = (base, base + tilt @ np.array([0, 0, STICK]))
            S['pt_vert_top'] = S['line_vert'][1]
            S['vert_prior_deg'] = 0.3
        if kind == 'everyday_up':
            # second capture: the same stick held upright by the golfer at the target end, eyeballed.
            tilt = Rot.from_rotvec(np.r_[RNG.normal(0, np.deg2rad(1.5), 2), 0]).as_matrix()
            base = np.array([0.55, TGT_Y, 0])
            S['line_vert'] = (base, base + tilt @ np.array([0, 0, STICK]))
            S['pt_vert_top'] = S['line_vert'][1]
            S['vert_prior_deg'] = 1.5
    return S

SIG = dict(line=0.5, end=5.0, ball=0.7, paper=0.4, wave=1.0)

def observe(S, cam):
    """Image observations (distorted px, noisy) for one camera."""
    R, t = cam['R'], cam['t']
    O = {}
    for k, v in S.items():
        if k == 'model_overrides': continue
        if k.startswith('line_'):
            uv, z = project(TRUE_K, R, t, seg(*v, 300))
            m = in_img(uv, z)
            idx = np.where(m)[0][::6]
            if len(idx) > 8:
                O[k] = uv[idx] + RNG.normal(0, SIG['line'], (len(idx), 2))
        elif k.startswith('pt_'):
            uv, z = project(TRUE_K, R, t, v[None])
            if in_img(uv, z).all():
                O[k] = uv[0] + RNG.normal(0, SIG['end'], 2)
        elif k == 'ball':
            uv, z = project(TRUE_K, R, t, v[None])
            O[k] = uv[0] + RNG.normal(0, SIG['ball'], 2)
        elif k == 'paper':
            uv, z = project(TRUE_K, R, t, v)
            if in_img(uv, z).all():
                O[k] = uv + RNG.normal(0, SIG['paper'], uv.shape)
    return O

def pinhole(K, R, t, X):
    Xc = X @ R.T + t
    return Xc[:, :2] / Xc[:, 2:3] * K[0] + K[1:3]

def line_resid(K, R, t, a, b, obs_ideal, sigma):
    pa, pb = pinhole(K, R, t, np.array([a, b]))
    d = pb - pa; n = np.array([-d[1], d[0]]) / np.linalg.norm(d)
    return ((obs_ideal - pa) @ n) / sigma

def ground_resid_cam(K, R, t, S_model, O, nuis):
    """Residuals for one camera given the MODEL layout (what the solver believes)."""
    r = []
    for k, ob in O.items():
        if k.startswith('line_'):
            a, b = S_model[k]
            if k == 'line_vert':
                a = np.array(a); b = a + Rot.from_rotvec(np.r_[nuis['vt'], 0]).as_matrix() @ np.array([0, 0, STICK])
            r.append(line_resid(K, R, t, a, b, ob, SIG['line']))
        elif k.startswith('pt_'):
            X = np.array(S_model[k])
            if k == 'pt_vert_top':
                a = np.array(S_model['line_vert'][0])
                X = a + Rot.from_rotvec(np.r_[nuis['vt'], 0]).as_matrix() @ np.array([0, 0, STICK])
            r.append((pinhole(K, R, t, X[None])[0] - ob) / SIG['end'])
        elif k == 'ball':
            r.append((pinhole(K, R, t, S_model['ball'][None])[0] - ob) / SIG['ball'])
        elif k == 'paper':
            dx, dth = nuis['pp']
            M = S_model['paper_model']
            c, s = np.cos(dth), np.sin(dth)
            P = np.c_[M[:, 0] * c - M[:, 1] * s + dx, M[:, 0] * s + M[:, 1] * c, M[:, 2]]
            # rotation about the paper's butted edge mid-point is close enough to the truth model
            r.append(((pinhole(K, R, t, P) - ob) / SIG['paper']).ravel())
    return np.concatenate(r)

def model_layout(S):
    M = {k: v for k, v in S.items() if k != 'model_overrides'}
    M.update(S.get('model_overrides', {}))
    if 'line_vert' in S:   # the solver only knows the base and that it is ~vertical
        M['line_vert'] = (S['line_vert'][0], None)
    return M

def priors(S, nuis):
    r = []
    if 'paper' in S:
        r += [nuis['pp'][0] / 0.005, nuis['pp'][1] / np.deg2rad(0.4)]
    if 'line_vert' in S:
        r += list(nuis['vt'] / np.deg2rad(S['vert_prior_deg']))
    return np.array(r)

def n_nuis(S):
    return (2 if 'paper' in S else 0) + (2 if 'line_vert' in S else 0)

def split_nuis(S, q):
    nu = {}; i = 0
    if 'paper' in S: nu['pp'] = q[i:i+2]; i += 2
    if 'line_vert' in S: nu['vt'] = q[i:i+2]; i += 2
    return nu

def solve_single(K, S, O, cam):
    Mdl = model_layout(S)
    O_ideal = {k: (to_ideal(K, v) if v.ndim == 2 else to_ideal(K, v[None])[0]) for k, v in O.items()}
    nn = n_nuis(S)
    def res(p):
        R = Rot.from_rotvec(p[:3]).as_matrix(); t = p[3:6]
        nu = split_nuis(S, p[6:])
        return np.r_[ground_resid_cam(K, R, t, Mdl, O_ideal, nu), priors(S, nu)]
    p0 = np.r_[(Rot.from_rotvec(RNG.normal(0, 0.05, 3)) * Rot.from_matrix(cam['R'])).as_rotvec(),
               cam['t'] + RNG.normal(0, 0.08, 3), np.zeros(nn)]
    sol = least_squares(res, p0, method='lm', x_scale='jac', max_nfev=3000)
    return Rot.from_rotvec(sol.x[:3]).as_matrix(), sol.x[3:6]

# ---------------- two-camera: the ball wave and a joint solve ----------------
def volume_points(n=None, grid=True):
    if grid:
        xs, ys, zs = np.linspace(-0.9, 0.9, 7), np.linspace(-0.2, 0.8, 5), np.linspace(0.0, 2.0, 9)
        P = np.stack(np.meshgrid(xs, ys, zs), -1).reshape(-1, 3)
    else:
        P = np.c_[RNG.uniform(-0.8, 0.8, n), RNG.uniform(-0.1, 0.7, n), RNG.uniform(0.2, 1.9, n)]
    ok = np.ones(len(P), bool)
    for c in CAMS.values():
        uv, z = project(TRUE_K, c['R'], c['t'], P)
        ok &= in_img(uv, z, 20)
    return P[ok]

def solve_joint(Ks, S, Os, wave_obs, init, refine_K=False):
    """Both cameras + wave points in one least squares; optionally refine each camera's lens."""
    names = ['FO', 'DTL']
    Mdl = model_layout(S)
    nw = len(wave_obs['FO']); nn = n_nuis(S); nk = 5 if refine_K else 0
    base = 12 + nn + 2 * nk
    def Kof(p, j):
        if not refine_K: return Ks[names[j]]
        return p[12 + nn + 5 * j: 12 + nn + 5 * j + 5]
    def res(p):
        out = []
        nu = split_nuis(S, p[12:12+nn])
        Xw = p[base:].reshape(nw, 3)
        for j, n in enumerate(names):
            K = Kof(p, j)
            R = Rot.from_rotvec(p[6*j:6*j+3]).as_matrix(); t = p[6*j+3:6*j+6]
            Oi = {k: (to_ideal(K, v) if v.ndim == 2 else to_ideal(K, v[None])[0]) for k, v in Os[n].items()}
            out.append(ground_resid_cam(K, R, t, Mdl, Oi, nu))
            pr, _ = project(K, R, t, Xw)
            out.append(((pr - wave_obs[n]) / SIG['wave']).ravel())
        out.append(priors(S, nu))
        return np.concatenate(out)
    X0 = triangulate(Ks, init, wave_obs)
    p0 = np.r_[Rot.from_matrix(init['FO'][0]).as_rotvec(), init['FO'][1],
               Rot.from_matrix(init['DTL'][0]).as_rotvec(), init['DTL'][1], np.zeros(nn)]
    if refine_K: p0 = np.r_[p0, Ks['FO'], Ks['DTL']]
    p0 = np.r_[p0, X0.ravel()]
    from scipy.sparse import lil_matrix
    r0 = res(p0)
    A = lil_matrix((len(r0), len(p0)), dtype=int)
    row = 0
    for j, n in enumerate(names):
        Oi = {k: (to_ideal(Ks[n], v) if v.ndim == 2 else to_ideal(Ks[n], v[None])[0]) for k, v in Os[n].items()}
        ng = len(ground_resid_cam(Ks[n], init[n][0], init[n][1], Mdl, Oi, split_nuis(S, np.zeros(nn))))
        cols = list(range(6*j, 6*j+6)) + list(range(12, 12+nn))
        if refine_K: cols += list(range(12+nn+5*j, 12+nn+5*j+5))
        for c in cols: A[row:row+ng+2*nw, c] = 1
        A[row:row+ng, 12:12+nn] = 1
        row += ng
        for i in range(nw):
            A[row:row+2, base+3*i:base+3*i+3] = 1; row += 2
    A[row:, 12:12+nn] = 1
    sol = least_squares(res, p0, method='trf', x_scale='jac', max_nfev=300, jac_sparsity=A)
    out = {}
    for j, n in enumerate(names):
        out[n] = (Rot.from_rotvec(sol.x[6*j:6*j+3]).as_matrix(), sol.x[6*j+3:6*j+6])
    Kout = {n: np.array(Kof(sol.x, j)) for j, n in enumerate(names)}
    return out, Kout

def _sparsity(nr, Oi, Mdl, S, nw, nn, Ks, p0, res):
    from scipy.sparse import lil_matrix
    names = ['FO', 'DTL']
    A = lil_matrix((nr, len(p0)), dtype=int)
    row = 0
    for j, n in enumerate(names):
        nu = split_nuis(S, np.zeros(nn))
        R = np.eye(3)
        ng = len(ground_resid_cam(Ks[n], Rot.from_rotvec(p0[6*j:6*j+3]).as_matrix(), p0[6*j+3:6*j+6], Mdl, Oi[n], nu))
        A[row:row+ng, 6*j:6*j+6] = 1; A[row:row+ng, 12:12+nn] = 1; row += ng
        for i in range(nw):
            A[row:row+2, 6*j:6*j+6] = 1
            A[row:row+2, 12+nn+3*i:12+nn+3*i+3] = 1
            row += 2
    A[row:, 12:12+nn] = 1
    return A

def triangulate(Ks, poses, obs):
    """Midpoint-free linear triangulation in ideal pinhole coordinates."""
    Ps, xs = [], []
    for n in ['FO', 'DTL']:
        K = Ks[n]; R, t = poses[n]
        Kp = np.array([[K[0], 0, K[1]], [0, K[0], K[2]], [0, 0, 1]])
        Ps.append(Kp @ np.c_[R, t]); xs.append(to_ideal(K, obs[n]))
    X = []
    for i in range(len(xs[0])):
        A = []
        for P, x in zip(Ps, xs):
            A.append(x[i, 0] * P[2] - P[0]); A.append(x[i, 1] * P[2] - P[1])
        _, _, vt = np.linalg.svd(np.array(A))
        X.append(vt[-1, :3] / vt[-1, 3])
    return np.array(X)

def rot_err_deg(Ra, Rb):
    return np.degrees(Rot.from_matrix(Ra @ Rb.T).magnitude())

def ground_map_err_mm(K, R, t):
    """FO single-camera: image -> mat plane error over the hitting area (±0.5 m × ±0.3 m)."""
    cam = CAMS['FO']
    g = np.stack(np.meshgrid(np.linspace(-0.5, 0.5, 11), np.linspace(-0.3, 0.3, 7)), -1).reshape(-1, 2)
    X = np.c_[g, np.zeros(len(g))]
    uv, _ = project(TRUE_K, cam['R'], cam['t'], X)
    xi = to_ideal(K, uv)
    rays = np.c_[(xi - K[1:3]) / K[0], np.ones(len(g))] @ R      # cam->world dirs
    C = -R.T @ t
    s = -C[2] / rays[:, 2]
    Xe = C + s[:, None] * rays
    return np.linalg.norm(Xe[:, :2] - g, axis=1).mean() * 1000
