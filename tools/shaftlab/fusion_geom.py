#!/usr/bin/env python3
"""fusion_geom.py -- the two-view shaft fusion's geometry in numpy, line for line with
src/Analysis/shaft_fusion.h and src/Analysis/dtl_shaft_synth3d.h, so a grader can re-run
what the C++ did from a run root and nothing else (dtl_continuous_track_design_update.md
§3.2a: the leave-one-band-out gate for (A), the anchored-vs-bridged comparison for (C), the
reflected refit for (D)).

Frame: the CAMERAS' -- X face-on image-right, Y the face-on view ray, Z up.
Angles in radians unless the name says Deg. Every function here has a twin in the header
of the same name; the eta fit is the same linear system (see fit_eta).
"""
import math
import numpy as np

PI = math.pi


# ── cameras ──────────────────────────────────────────────────────────────────
def face_on_camera():
    return dict(d=np.array([0.0, 1.0, 0.0]), right=np.array([1.0, 0.0, 0.0]), down=np.array([0.0, 0.0, -1.0]))


def dtl_camera(yaw_deg=0.0, pitch_deg=0.0, roll_deg=0.0):
    p, a, r = math.radians(pitch_deg), math.radians(yaw_deg), math.radians(roll_deg)
    d0, r0, dn0 = np.array([1.0, 0, 0]), np.array([0, -1.0, 0]), np.array([0, 0, -1.0])
    d1 = d0 * math.cos(p) + dn0 * math.sin(p)
    dn1 = dn0 * math.cos(p) - d0 * math.sin(p)
    c, s = math.cos(a), math.sin(a)
    rz = lambda v: np.array([c * v[0] - s * v[1], s * v[0] + c * v[1], v[2]])
    right, down = rz(r0), rz(dn1)
    return dict(d=rz(d1), right=right * math.cos(r) + down * math.sin(r), down=down * math.cos(r) - right * math.sin(r))


def unit(v):
    n = np.linalg.norm(v)
    return v / n if n > 0 else v


def image_dir(cam, theta):
    return cam["right"] * math.cos(theta) + cam["down"] * math.sin(theta)


def project(cam, u):
    px, py = float(np.dot(u, cam["right"])), float(np.dot(u, cam["down"]))
    return math.atan2(py, px), math.hypot(px, py)


def view_plane_normal(cam, theta):
    return unit(np.cross(cam["d"], image_dir(cam, theta)))


# ── one frame: two view planes cross ─────────────────────────────────────────
def fuse_one(cf, cd, theta_f, theta_d):
    """returns (u, cond, agree) or (None, cond, False)"""
    img_f, img_d = image_dir(cf, theta_f), image_dir(cd, theta_d)
    c = np.cross(view_plane_normal(cf, theta_f), view_plane_normal(cd, theta_d))
    cond = float(np.linalg.norm(c))
    if cond < 1e-9:
        return None, cond, False
    u = c / cond
    sf, sd = float(np.dot(u, img_f)), float(np.dot(u, img_d))
    if (sf if abs(sf) >= abs(sd) else sd) < 0:
        u = -u
    return u, cond, bool(np.dot(u, img_f) > 0 and np.dot(u, img_d) > 0)


# ── de-projection: one camera's image angle through a plane ──────────────────
def deproject(cam, theta, n_plane, min_cond=0.15):
    """u ⊥ n_plane and ⊥ the view-plane normal, headed along the image direction.
    returns (u, cond) or (None, cond)."""
    img = image_dir(cam, theta)
    n_v = view_plane_normal(cam, theta)
    c = np.cross(n_plane, n_v)
    cond = float(np.linalg.norm(c))
    if cond < min_cond:
        return None, cond
    u = c / cond
    if np.dot(u, img) < 0:
        u = -u
    return u, cond


def deproject_eta(cam, theta, n_plane, eta_deg, min_cond=0.15):
    """(A): the direction in the camera's view plane that sits eta off the phase plane, on
    the side the sign says. u(φ) = cos φ·u0 + sin φ·w with w = n_v × u0 (in the view plane,
    ⊥ u0); u·n = sin φ·(w·n) and w·n = cond, so sin φ = sin η / cond. |sin η| > cond has no
    solution: the frame is refused (None), as the header does."""
    u0, cond = deproject(cam, theta, n_plane, min_cond)
    if u0 is None:
        return None, cond
    if not eta_deg or eta_deg == 0.0:
        return u0, cond
    n_v = view_plane_normal(cam, theta)
    w = np.cross(n_v, u0)
    wn = float(np.dot(w, n_plane))
    if abs(wn) < 1e-12:
        return u0, cond
    s = math.sin(math.radians(eta_deg)) / wn
    if abs(s) > 1.0:
        return None, cond
    phi = math.asin(s)
    u = u0 * math.cos(phi) + w * math.sin(phi)
    if np.dot(u, image_dir(cam, theta)) < 0:
        u = -u
    return u, cond


# ── plane through the origin ─────────────────────────────────────────────────
def fit_plane(U):
    """returns dict(normal, inclDeg, azimDeg, oopRmsDeg, oopP90Deg, n) or None (n < 1)."""
    U = np.asarray(U, dtype=float)
    if len(U) == 0:
        return None
    A = U.T @ U
    w, v = np.linalg.eigh(A)
    n = v[:, int(np.argmin(w))]
    if n[2] < 0:
        n = -n
    off = np.degrees(np.arcsin(np.clip(U @ n, -1, 1)))
    a = np.sort(np.abs(off))
    return dict(normal=n, n=len(U),
                inclDeg=math.degrees(math.acos(min(1.0, abs(n[2])))),
                azimDeg=math.degrees(math.atan2(n[1], n[0])),
                oopRmsDeg=float(np.sqrt(np.mean(off * off))),
                oopP90Deg=float(a[min(len(a) - 1, int(0.9 * len(a)))]))


def oop_deg(u, n):
    return math.degrees(math.asin(max(-1.0, min(1.0, float(np.dot(u, n))))))


# ── (A) the out-of-plane curve η(t) ──────────────────────────────────────────
# Knots every knot_us from t_lo to t_hi (inclusive of the last, which is ≥ t_hi). Values v_k
# minimise
#     Σ_i w_i (η(t_i) − oop_i)²  +  λ Σ_k (v_{k−1} − 2 v_k + v_{k+1})²  +  Σ_k μ_k v_k²
# with η(t) the piecewise-LINEAR interpolant in the data term (the evaluation is Catmull-Rom
# through the same knot values, C¹, and the two agree at the knots), μ_k = prior_far where no
# data point lies within prior_reach_us of knot k, prior_near otherwise. One dense solve.
def eta_knots(t_lo, t_hi, knot_us):
    n = int(math.ceil((t_hi - t_lo) / float(knot_us))) + 1
    return np.array([t_lo + k * knot_us for k in range(max(n, 2))], dtype=float)


def fit_eta(t, oop, w, knots, lam=4.0, prior_reach_us=150000, prior_far=1.0, prior_near=1e-3):
    t = np.asarray(t, dtype=float); oop = np.asarray(oop, dtype=float); w = np.asarray(w, dtype=float)
    K = len(knots)
    A = np.zeros((K, K)); b = np.zeros(K)
    h = knots[1] - knots[0]
    for ti, yi, wi in zip(t, oop, w):
        if wi <= 0:
            continue
        k = int(math.floor((ti - knots[0]) / h))
        k = max(0, min(K - 2, k))
        a = (ti - knots[k]) / h
        a = max(0.0, min(1.0, a))
        c0, c1 = 1.0 - a, a
        A[k, k] += wi * c0 * c0; A[k, k + 1] += wi * c0 * c1
        A[k + 1, k] += wi * c1 * c0; A[k + 1, k + 1] += wi * c1 * c1
        b[k] += wi * c0 * yi; b[k + 1] += wi * c1 * yi
    for k in range(1, K - 1):
        idx = (k - 1, k, k + 1); co = (1.0, -2.0, 1.0)
        for i, ci in zip(idx, co):
            for j, cj in zip(idx, co):
                A[i, j] += lam * ci * cj
    near = np.zeros(K, dtype=bool)
    if len(t):
        for k in range(K):
            near[k] = bool(np.any((w > 0) & (np.abs(t - knots[k]) <= prior_reach_us)))
    for k in range(K):
        A[k, k] += prior_near if near[k] else prior_far
    v = np.linalg.solve(A, b)
    return v, near


def eval_eta(knots, v, t):
    """Catmull-Rom through the knot values (clamped ends); constant beyond the ends."""
    K = len(knots); h = knots[1] - knots[0]
    if t <= knots[0]:
        return float(v[0])
    if t >= knots[-1]:
        return float(v[-1])
    k = int(math.floor((t - knots[0]) / h)); k = max(0, min(K - 2, k))
    s = (t - knots[k]) / h
    p0 = v[k - 1] if k > 0 else v[k]
    p1, p2 = v[k], v[k + 1]
    p3 = v[k + 2] if k + 2 < K else v[k + 1]
    return float(0.5 * ((2 * p1) + (-p0 + p2) * s + (2 * p0 - 5 * p1 + 4 * p2 - p3) * s * s
                        + (-p0 + 3 * p1 - 3 * p2 + p3) * s * s * s))


def wrap_deg(d):
    return (d + 180.0) % 360.0 - 180.0


def angle_deg(a, b):
    return math.degrees(math.acos(max(-1.0, min(1.0, float(np.dot(unit(a), unit(b)))))))


def slerp(a, b, w):
    a, b = unit(a), unit(b)
    d = max(-1.0, min(1.0, float(np.dot(a, b))))
    om = math.acos(d)
    if om < 1e-9:
        return a
    return unit(a * math.sin((1 - w) * om) / math.sin(om) + b * math.sin(w * om) / math.sin(om))
