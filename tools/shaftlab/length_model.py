#!/usr/bin/env python3
"""Projected club-length model — stage-2 clubhead (H0 2026-07-03).

Predicts the projected (image-plane) grip->head length L as a function of the
stage-1 shaft angle + swing phase, indexed from the length at address
(rho = L / L0). Two consumers: the search prior + CI for the head measurement
(clubhead_measure), and crop/off-frame detection (clubhead_annotate).

GEOMETRY PRIMER (for the C++ port)
  The projected length of a fixed-length 3D club depends only on the shaft's
  3D DIRECTION relative to the optical axis: rho = |d_xy| = sqrt(1 - d_z^2).
  If shaft directions lay in one fixed plane inclined at iota to the image
  plane, with node line (plane ∩ image plane) at theta_node:
      rho(theta_rel) = cos(iota) / sqrt(cos^2(iota) cos^2(theta_rel)
                                        + sin^2(theta_rel))
  KEY PROPERTY: this is 180-deg periodic (antiparallel directions foreshorten
  identically). The 0008 hand labels VIOLATE it ~2x (len 311 px at theta=102
  vs ~175 px at theta=-78) — so no single fixed plane can be the model; the
  shaft's direction plane shifts through the swing. Hence the candidates:

  M0  constant L                              (null baseline)
  M1  single inclined plane                   (documented failure — evidence)
  M2  per-phase inclined plane, shared scale  (iota = the club-dependent
                                               parameter; back/down fit
                                               near-identical planes on both
                                               labeled swings, thru distinct)
  M3  tilted cone of shaft directions         (global; breaks the symmetry)
  M4  robust kernel regression per phase      (empirical flexibility ceiling)

  MODEL-FORM SELECTION IS CORPUS-GATED (held-out swings AND clubs — plane
  varies with club length; a single labeled swing may itself be off-plane).
  Single-swing residuals are development signals only. See
  docs/validation/pipeline_validation_and_tuning.md §5.5/§3.4.

FITTING PATHS
  labeled  — robust (soft-L1) relative-error fit on hand labels (truth.json).
  self-fit — THE PRODUCTION PATH (no labels): censored quantile fit on the
             swing's own radial length observations. q=0.5 by default: the
             zeroth-order observation error is TWO-sided (attached-shadow /
             body over-runs vs dropout / dark-on-dark under-runs), so a
             central quantile beats the design's upper envelope; revisit once
             measurements are one-sided. Frame-edge-censored points contribute
             only a hinge ("true length >= visible").
             Because the model is re-fit per swing, cross-swing
             generalisation is only needed for the FORM and CI calibration.

C++ PORT NOTES (module -> ClubLengthModel)
  * theta is DEGREES, and stage 2 consumes `theta_u` — theta_out re-unwrapped
    LOCALLY (unwrap_theta): stage-1 re-inits can rebase the winding by +/-360
    (observed on 0009: made top and impact one frame apart). Never trust
    theta_out's winding across frames.
  * Phase splitting (phases_from_track) is anchored on angular RATE from
    MEASURED frames only, clamped at 25 deg/frame: theta extrema are fragile
    on wraparound swings and pred-tier bridges fake |omega| peaks.
  * M4Kernel is Nadaraya-Watson with bisquare IRLS (h=15 deg); the self-fit
    variant is a kernel-weighted QUANTILE (direct, no optimizer). Prediction
    outside data support falls back to nearest — a port should keep that
    (extrapolation once produced L_pred=95 px and poisoned the prior).
  * scipy least_squares(soft_l1) fits M1/M2/M3 with multi-start; Nelder-Mead
    handles the non-smooth pinball loss. A C++ port can use ceres or a small
    hand-rolled NM; determinism matters more than speed here.

Usage:
  length_model.py labeled <lab_dir> [--track <csv>] [--out-dir <dir>]
  length_model.py selffit <lab_dir> --lengths <csv> [--track <csv>] [--out-dir <dir>]
      lengths csv columns: frame,L_vis,censored   (from clubhead_scan.py)
"""
import argparse
import csv
import json
import math
import os
import sys

import numpy as np
from scipy.optimize import least_squares, minimize

PHASES = ("back", "down", "thru")


def wrap180(x):
    return (np.asarray(x, float) + 180.0) % 360.0 - 180.0


# ---------------------------------------------------------------- loaders

def load_clipmeta(path):
    with open(path) as f:
        return json.load(f)


def load_track(path):
    """Contract v1 columns only."""
    frames, gx, gy, th, kind, conf = [], [], [], [], [], []
    with open(path) as f:
        for row in csv.DictReader(f):
            frames.append(int(row["frame"]))
            gx.append(float(row["grip_x"]))
            gy.append(float(row["grip_y"]))
            th.append(float(row["theta_out"]) if row["theta_out"] not in ("", "nan") else np.nan)
            kind.append(row["kind"])
            conf.append(float(row["conf"]) if row.get("conf") not in (None, "", "nan") else 0.0)
    t = {
        "frame": np.array(frames), "grip": np.stack([gx, gy], 1),
        "theta_out": np.array(th), "kind": np.array(kind), "conf": np.array(conf),
    }
    t["theta_u"] = unwrap_theta(t)  # re-unwrapped: the theta stage 2 consumes
    return t


def load_truth(swing_dir):
    with open(os.path.join(swing_dir, "truth.json")) as f:
        return json.load(f)


def phase_of_time(t_us, events):
    p4 = events.get("p4_s", None)
    p7 = events.get("p7_s", None)
    if p4 is None or p7 is None:
        return None
    if t_us <= p4 * 1e6:
        return "back"
    if t_us <= p7 * 1e6:
        return "down"
    return "thru"


def assemble_labels(lab_dir, track_path):
    """truth labels -> (theta_unwrapped_deg, phase, L_px, clip_frame, track_kind).

    theta comes from the LABEL (ground truth), unwrapped using the track's
    winding at the matched clip frame (the track is only trusted for the
    integer number of turns, not the angle itself).
    """
    meta = load_clipmeta(os.path.join(lab_dir, "clipmeta.json"))
    tt = np.asarray(meta["t_us"], float)
    truth = load_truth(meta["swingDir"])
    track = load_track(track_path)
    out = []
    for h in truth["shaft"]:
        i = int(np.argmin(np.abs(tt - h["t_us"])))
        if abs(tt[i] - h["t_us"]) > 30000:
            continue
        j = np.searchsorted(track["frame"], i)
        if j >= len(track["frame"]) or track["frame"][j] != i:
            continue
        th_tr = track["theta_u"][j]
        if not np.isfinite(th_tr):
            continue
        lab_th = math.degrees(h["theta"])
        th_u = th_tr + float(wrap180(lab_th - th_tr))
        L = h.get("len")
        if L is None:
            L = float(np.hypot(h["head"][0] - h["grip"][0], h["head"][1] - h["grip"][1]))
        ph = phase_of_time(h["t_us"], truth.get("events", {}))
        if ph is None:
            continue
        out.append((th_u, ph, float(L), i, track["kind"][j]))
    th = np.array([o[0] for o in out])
    ph = np.array([o[1] for o in out])
    L = np.array([o[2] for o in out])
    fr = np.array([o[3] for o in out])
    kd = np.array([o[4] for o in out])
    return th, ph, L, fr, kd, meta, truth


def unwrap_theta(track):
    """Continuous theta per track frame. theta_out's unwrap continuity is NOT
    trusted across stage-1 re-inits (they can rebase the winding by +/-360 —
    observed on swing_0009, where it made top and impact one frame apart).
    Re-unwrap locally: successive diffs can't exceed 180 deg at swing rates."""
    th = track["theta_out"].copy()
    ok = np.isfinite(th)
    th[~ok] = np.interp(np.flatnonzero(~ok), np.flatnonzero(ok), th[ok]) if ok.any() else 0.0
    d = wrap180(np.diff(th))
    return np.concatenate([[th[0]], th[0] + np.cumsum(d)])


def phases_from_track(track):
    """Phase per track frame, from the (re-unwrapped) theta trajectory alone.

    Anchored on angular RATE, not theta extrema: theta extrema are fragile on
    full-wraparound swings (0009's finish-hold pred-tier noise random-walks
    the unwrapped angle past the real top). |omega| peaks at impact reliably;
    the top is the last omega sign change before it."""
    th = unwrap_theta(track)
    d = np.gradient(th)
    # only measured frames carry rate evidence; re-init jumps and pred-tier
    # bridges fake huge rates (s2: fake |omega| peak in the finish at f692).
    # 2500 deg/s at 150 fps is ~17 deg/frame — clamp anything beyond physical.
    d[(track["kind"] != "meas") | (np.abs(d) > 25.0)] = 0.0
    w = np.convolve(d, np.ones(9) / 9.0, mode="same")
    impact = int(np.argmax(np.abs(w)))
    s_down = 1.0 if w[impact] >= 0 else -1.0
    opp = np.flatnonzero(np.sign(w[:impact]) == -s_down)
    top = int(opp[-1]) if len(opp) else max(0, impact - 1)
    meas = track["kind"] == "meas"
    idx0 = np.flatnonzero(meas)[:15]
    addr = np.median(th[idx0]) if len(idx0) else th[0]
    ph = np.full(len(th), "thru", dtype=object)
    ph[: top + 1] = "back"
    ph[top + 1: impact + 1] = "down"
    return np.array(ph, dtype=str), addr, top, impact


# ---------------------------------------------------------------- models

def rho_plane(th_rel_deg, iota_deg):
    """Projected-length ratio for shaft directions confined to a plane
    inclined at iota to the image plane; th_rel measured from the node line.
    Note the 180-deg periodicity — the property the labels are seen to violate
    for a single global plane."""
    tr = np.radians(th_rel_deg)
    ci = math.cos(math.radians(iota_deg))
    return ci / np.sqrt(ci * ci * np.cos(tr) ** 2 + np.sin(tr) ** 2)


class ModelBase:
    name = "?"

    def fit(self, th, ph, L):
        raise NotImplementedError

    def predict(self, th, ph):
        raise NotImplementedError

    def params_str(self):
        return ""

    def n_params(self):
        return 0


class M0Const(ModelBase):
    name = "M0-const"

    def fit(self, th, ph, L):
        self.c = float(np.median(L))
        r = least_squares(lambda p: (p[0] - L) / L, [self.c], loss="soft_l1", f_scale=0.1)
        self.c = float(r.x[0])
        return self

    def predict(self, th, ph):
        return np.full(np.shape(th), self.c)

    def params_str(self):
        return f"L={self.c:.1f}px"

    def n_params(self):
        return 1


class M1Plane(ModelBase):
    name = "M1-plane"

    def fit(self, th, ph, L):
        best = None
        for thn0 in range(0, 180, 30):
            for i0 in (20.0, 45.0, 65.0):
                try:
                    r = least_squares(
                        lambda p: (p[0] * rho_plane(th - p[2], p[1]) - L) / L,
                        [np.percentile(L, 90), i0, float(thn0)],
                        bounds=([10.0, 1.0, -360.0], [5000.0, 84.0, 720.0]),
                        loss="soft_l1", f_scale=0.1)
                except Exception:
                    continue
                if best is None or r.cost < best.cost:
                    best = r
        self.A, self.iota, self.thn = [float(v) for v in best.x]
        return self

    def predict(self, th, ph):
        return self.A * rho_plane(np.asarray(th) - self.thn, self.iota)

    def params_str(self):
        return f"A={self.A:.1f}px iota={self.iota:.1f}deg node={self.thn % 180:.1f}deg"

    def n_params(self):
        return 3


class M2PlanePerPhase(ModelBase):
    name = "M2-plane/phase"

    def fit(self, th, ph, L):
        # params: [A, (iota, thn) x phase present]
        self.phases = [p for p in PHASES if (ph == p).any()]

        def resid(p):
            out = np.zeros(len(L))
            for k, phase in enumerate(self.phases):
                m = ph == phase
                out[m] = (p[0] * rho_plane(th[m] - p[2 + 2 * k], p[1 + 2 * k]) - L[m]) / L[m]
            return out

        best = None
        for i0 in (20.0, 45.0, 65.0):
            for dn in (0, 45, 90, 135):
                x0 = [np.percentile(L, 90)]
                lb, ub = [10.0], [5000.0]
                for phase in self.phases:
                    m = ph == phase
                    x0 += [i0, float(np.median(th[m]) + dn)]
                    lb += [1.0, -1080.0]
                    ub += [84.0, 1080.0]
                try:
                    r = least_squares(resid, x0, bounds=(lb, ub), loss="soft_l1", f_scale=0.1)
                except Exception:
                    continue
                if best is None or r.cost < best.cost:
                    best = r
        self.A = float(best.x[0])
        self.pp = {phase: (float(best.x[1 + 2 * k]), float(best.x[2 + 2 * k]))
                   for k, phase in enumerate(self.phases)}
        return self

    def predict(self, th, ph):
        th = np.asarray(th, float)
        ph = np.asarray(ph)
        out = np.full(th.shape, np.nan)
        for phase, (iota, thn) in self.pp.items():
            m = ph == phase
            out[m] = self.A * rho_plane(th[m] - thn, iota)
        # unseen phase: fall back to nearest fitted phase parameters
        bad = ~np.isfinite(out)
        if bad.any() and self.pp:
            iota, thn = next(iter(self.pp.values()))
            out[bad] = self.A * rho_plane(th[bad] - thn, iota)
        return out

    def params_str(self):
        s = " ".join(f"{p}:iota={i:.1f} node={t % 180:.1f}" for p, (i, t) in self.pp.items())
        return f"A={self.A:.1f}px {s}"

    def n_params(self):
        return 1 + 2 * len(self.pp)


class M3Cone(ModelBase):
    """Shaft directions on a cone: d(psi) = cos(a)*axis + sin(a)*(cos psi u + sin psi v).
    Global (phase-blind); prediction picks the psi branch whose image angle is
    nearest the queried theta. A theta-mismatch penalty keeps the cone honest."""
    name = "M3-cone"
    PSI = np.arange(0.0, 360.0, 0.5)

    @staticmethod
    def _geometry(az_deg, el_deg, alpha_deg):
        az, el, al = (math.radians(v) for v in (az_deg, el_deg, alpha_deg))
        a = np.array([math.cos(el) * math.cos(az), math.cos(el) * math.sin(az), math.sin(el)])
        ref = np.array([0.0, 0.0, 1.0]) if abs(a[2]) < 0.95 else np.array([1.0, 0.0, 0.0])
        u = np.cross(a, ref)
        u /= np.linalg.norm(u)
        v = np.cross(a, u)
        psi = np.radians(M3Cone.PSI)
        d = (math.cos(al) * a[None, :]
             + math.sin(al) * (np.cos(psi)[:, None] * u[None, :] + np.sin(psi)[:, None] * v[None, :]))
        rho = np.hypot(d[:, 0], d[:, 1])
        thg = np.degrees(np.arctan2(d[:, 1], d[:, 0]))
        return rho, thg

    def _eval(self, p, th):
        A, az, el, al = p
        rho, thg = self._geometry(az, el, al)
        d = np.abs(wrap180(thg[None, :] - np.asarray(th, float).reshape(-1, 1)))
        j = np.argmin(d, axis=1)
        return A * rho[j], d[np.arange(len(j)), j]

    def fit(self, th, ph, L):
        best, bx = None, None
        for az0 in (0.0, 90.0, 180.0, 270.0):
            for el0 in (-60.0, -30.0, 0.0, 30.0, 60.0):
                for al0 in (30.0, 60.0, 80.0):
                    def resid(p):
                        Lp, dmis = self._eval(p, th)
                        return np.concatenate([(Lp - L) / L, dmis / 20.0])
                    A_max = 1.6 * float(np.max(L))  # physical: true length can't hugely exceed max observed projection
                    try:
                        r = least_squares(resid, [np.percentile(L, 90), az0, el0, al0],
                                          bounds=([10.0, -360.0, -89.0, 5.0],
                                                  [A_max, 720.0, 89.0, 89.0]),
                                          loss="soft_l1", f_scale=0.1)
                    except Exception:
                        continue
                    if best is None or r.cost < best:
                        best, bx = r.cost, r.x
        self.p = [float(v) for v in bx]
        return self

    def predict(self, th, ph):
        return self._eval(self.p, th)[0]

    def params_str(self):
        A, az, el, al = self.p
        return f"A={A:.1f}px axis=(az {az % 360:.0f},el {el:.0f}) alpha={al:.1f}deg"

    def n_params(self):
        return 4


class M4Kernel(ModelBase):
    """Robust Nadaraya-Watson kernel regression per phase on unwrapped theta
    (bisquare IRLS). The empirical ceiling any physical model should approach."""
    name = "M4-kernel"

    def __init__(self, h=15.0):
        self.h = h

    def fit(self, th, ph, L):
        self.data = {}
        for phase in PHASES:
            m = ph == phase
            if not m.any():
                continue
            x, y = th[m], L[m]
            r = np.ones(len(y))
            for _ in range(3):
                yh = self._nw(x, x, y, r)
                res = y - yh
                s = 6.0 * max(np.median(np.abs(res - np.median(res))), 1e-6)
                r = np.clip(1 - (res / s) ** 2, 0.0, None) ** 2
            self.data[phase] = (x, y, r)
        return self

    def _nw(self, xq, x, y, r):
        w = np.exp(-0.5 * ((np.asarray(xq, float).reshape(-1, 1) - x[None, :]) / self.h) ** 2) * r[None, :]
        sw = w.sum(1)
        out = np.where(sw > 1e-9, (w * y[None, :]).sum(1) / np.maximum(sw, 1e-9), np.nan)
        bad = ~np.isfinite(out)
        if bad.any():
            j = np.argmin(np.abs(np.asarray(xq, float).reshape(-1, 1)[bad] - x[None, :]), axis=1)
            out[bad] = y[j]
        return out

    def predict(self, th, ph):
        th = np.asarray(th, float)
        ph = np.asarray(ph)
        out = np.full(th.shape, np.nan)
        for phase, (x, y, r) in self.data.items():
            m = ph == phase
            if m.any():
                out[m] = self._nw(th[m], x, y, r)
        bad = ~np.isfinite(out)
        if bad.any():
            allx = np.concatenate([d[0] for d in self.data.values()])
            ally = np.concatenate([d[1] for d in self.data.values()])
            j = np.argmin(np.abs(th[bad].reshape(-1, 1) - allx[None, :]), axis=1)
            out[bad] = ally[j]
        return out

    def params_str(self):
        return f"h={self.h:.0f}deg npts={sum(len(d[0]) for d in self.data.values())}"

    def n_params(self):
        return sum(len(d[0]) for d in self.data.values())  # effective, for honesty


def make_models():
    return [M0Const(), M1Plane(), M2PlanePerPhase(), M3Cone(), M4Kernel()]


# ---------------------------------------------------------------- self-fit (censored)

def self_fit_model(model, th, ph, L_vis, censored, q=0.5):
    """Censored quantile fit on a swing's own run-end lengths (production
    path — no labels needed). Frame-edge-censored points only say 'true length
    >= visible' (hinge).

    q note (H0 finding, swing_0008): zeroth-order run-end error is TWO-sided —
    under-runs from dropout/dark-on-dark presence loss, over-runs from attached
    shadow/body clutter — so a central quantile (q=0.5) beats the design's
    upper envelope (q=0.9) against hand labels (20.2% vs 25.2% median).
    Revisit at H1 when real head measurements make the error one-sided."""
    th, ph = np.asarray(th, float), np.asarray(ph)
    L_vis, censored = np.asarray(L_vis, float), np.asarray(censored, bool)

    # gross-overrun prefilter: run-ends that sail past any plausible projection
    # (ray continuing through body/mat clutter) would locally poison the
    # envelope quantile. True projections can't much exceed the global q90.
    cap = 1.3 * np.quantile(L_vis[~censored], 0.90) if (~censored).sum() else np.inf
    keep = L_vis <= cap
    th, ph, L_vis, censored = th[keep], ph[keep], L_vis[keep], censored[keep]

    if isinstance(model, M4Kernel):
        # direct kernel-weighted quantile per phase (no optimizer)
        model.data = {}
        for phase in PHASES:
            m = (ph == phase) & ~censored
            if m.sum() < 4:
                continue
            x, y = th[m], L_vis[m]
            grid = np.arange(x.min(), x.max() + 1.0, 5.0)
            yq = np.empty(len(grid))
            for i, g in enumerate(grid):
                w = np.exp(-0.5 * ((x - g) / model.h) ** 2)
                order = np.argsort(y)
                cw = np.cumsum(w[order])
                yq[i] = y[order][np.searchsorted(cw, q * cw[-1])] if cw[-1] > 1e-9 else np.nan
            ok = np.isfinite(yq)
            model.data[phase] = (grid[ok], yq[ok], np.ones(ok.sum()))
        return model

    def loss(pvec):
        _apply_params(model, pvec)
        Lp = model.predict(th, ph)
        r = L_vis - Lp
        pin = np.where(~censored, np.where(r >= 0, q * r, (q - 1.0) * r), 0.0)
        hinge = np.where(censored, np.maximum(0.0, L_vis - Lp), 0.0)
        return float(np.sum(pin / np.maximum(L_vis, 1.0)) + np.sum(hinge / np.maximum(L_vis, 1.0)))

    # seed from a plain labeled-style fit on the uncensored points
    m = ~censored
    model.fit(th[m], ph[m], L_vis[m])
    p0 = _extract_params(model)
    r = minimize(loss, p0, method="Nelder-Mead",
                 options={"maxiter": 4000, "xatol": 1e-3, "fatol": 1e-5})
    _apply_params(model, r.x)
    return model


def _extract_params(model):
    if isinstance(model, M0Const):
        return [model.c]
    if isinstance(model, M1Plane):
        return [model.A, model.iota, model.thn]
    if isinstance(model, M2PlanePerPhase):
        p = [model.A]
        for phase in model.phases:
            p += list(model.pp[phase])
        return p
    if isinstance(model, M3Cone):
        return list(model.p)
    raise ValueError(model.name)


def _apply_params(model, p):
    p = list(map(float, p))
    if isinstance(model, M0Const):
        model.c = p[0]
    elif isinstance(model, M1Plane):
        model.A, model.iota, model.thn = p
    elif isinstance(model, M2PlanePerPhase):
        model.A = p[0]
        model.pp = {phase: (p[1 + 2 * k], p[2 + 2 * k]) for k, phase in enumerate(model.phases)}
    elif isinstance(model, M3Cone):
        model.p = p


# ---------------------------------------------------------------- evaluation

def rel_errors(model, th, ph, L):
    return (model.predict(th, ph) - L) / L


def stats(e):
    a = np.abs(e) * 100.0
    if len(a) == 0:
        return "n=0"
    return (f"n={len(a):3d} median={np.median(a):5.1f}% mean={np.mean(a):5.1f}% "
            f"p90={np.percentile(a, 90):5.1f}% >25%={(a > 25).mean() * 100:4.0f}%")


def kfold_cv(model_ctor, th, ph, L, k=5, seed=7):
    rng = np.random.default_rng(seed)
    idx = rng.permutation(len(L))
    errs = np.empty(len(L))
    for f in range(k):
        test = idx[f::k]
        train = np.setdiff1d(idx, test)
        m = model_ctor().fit(th[train], ph[train], L[train])
        errs[test] = rel_errors(m, th[test], ph[test], L[test])
    return errs


def lopo_cv(model_ctor, th, ph, L):
    """Leave-one-phase-out — only meaningful for global (phase-blind) models."""
    out = {}
    for phase in PHASES:
        te = ph == phase
        if not te.any() or te.all():
            continue
        m = model_ctor().fit(th[~te], ph[~te], L[~te])
        out[phase] = rel_errors(m, th[te], ph[te], L[te])
    return out


# ---------------------------------------------------------------- reporting

def run_labeled(lab_dir, track_path, out_dir):
    th, ph, L, fr, kd, meta, truth = assemble_labels(lab_dir, track_path)
    lines = []
    P = lines.append
    P(f"# length_model labeled fit — {lab_dir}")
    P(f"labels n={len(L)}  phases: " + " ".join(f"{p}={np.sum(ph == p)}" for p in PHASES))
    P(f"track kind at labels: meas={np.sum(kd == 'meas')} pred={np.sum(kd == 'pred')}")
    addr_m = np.abs(th - th[0]) < 15.0
    L0 = float(np.median(L[addr_m])) if addr_m.any() else float(L[0])
    P(f"L0 (address) = {L0:.1f}px   (rho = L/L0; min observed rho = {L.min() / L0:.2f})")
    P("NOTE: single-swing residuals are DEVELOPMENT signals only; model-form")
    P("selection is corpus-gated (swing may be off-plane; one club only).")
    P("")

    ctors = {"M0-const": M0Const, "M1-plane": M1Plane, "M2-plane/phase": M2PlanePerPhase,
             "M3-cone": M3Cone, "M4-kernel": M4Kernel}
    fitted = {}
    for name, ctor in ctors.items():
        m = ctor().fit(th, ph, L)
        fitted[name] = m
        e = rel_errors(m, th, ph, L)
        cv = kfold_cv(ctor, th, ph, L)
        P(f"== {name}  [{m.params_str()}]  (params={m.n_params()})")
        P(f"   in-sample  {stats(e)}")
        P(f"   5-fold CV  {stats(cv)}")
        for phase in PHASES:
            msk = ph == phase
            if msk.any():
                P(f"   {phase:5s}      {stats(e[msk])}")
        if name in ("M0-const", "M1-plane", "M3-cone"):
            for phase, ee in lopo_cv(ctor, th, ph, L).items():
                P(f"   LOPO->{phase:5s} {stats(ee)}")
        P("")

    os.makedirs(out_dir, exist_ok=True)
    rp = os.path.join(out_dir, "length_model_report.txt")
    with open(rp, "w") as f:
        f.write("\n".join(lines) + "\n")
    _dump_curves(fitted, th, ph, L, out_dir)
    _plot(fitted, th, ph, L, out_dir)
    print("\n".join(lines))
    print(f"[report] {rp}")
    return fitted


def _dump_curves(fitted, th, ph, L, out_dir):
    grid = np.arange(math.floor(th.min() / 5) * 5, th.max() + 5, 2.0)
    with open(os.path.join(out_dir, "length_model_curves.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["theta_unwrapped", "phase"] + list(fitted.keys()))
        for phase in PHASES:
            m = ph == phase
            if not m.any():
                continue
            g = grid[(grid >= th[m].min() - 10) & (grid <= th[m].max() + 10)]
            pv = {n: fitted[n].predict(g, np.full(g.shape, phase, dtype=object)) for n in fitted}
            for i, t in enumerate(g):
                w.writerow([f"{t:.1f}", phase] + [f"{pv[n][i]:.1f}" for n in fitted])
    with open(os.path.join(out_dir, "length_model_labels.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["theta_unwrapped", "phase", "L_px"])
        for i in range(len(L)):
            w.writerow([f"{th[i]:.1f}", ph[i], f"{L[i]:.1f}"])


def _plot(fitted, th, ph, L, out_dir):
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except Exception:
        return
    cols = {"back": "tab:blue", "down": "tab:red", "thru": "tab:green"}
    fig, axes = plt.subplots(1, len(fitted), figsize=(4.2 * len(fitted), 4.0), sharey=True)
    for ax, (name, m) in zip(np.atleast_1d(axes), fitted.items()):
        for phase in PHASES:
            msk = ph == phase
            if not msk.any():
                continue
            ax.scatter(th[msk], L[msk], s=14, c=cols[phase], label=phase, alpha=0.7)
            g = np.linspace(th[msk].min(), th[msk].max(), 200)
            ax.plot(g, m.predict(g, np.full(g.shape, phase, dtype=object)), c=cols[phase], lw=1.2)
        ax.set_title(name, fontsize=10)
        ax.set_xlabel("theta unwrapped (deg)")
    np.atleast_1d(axes)[0].set_ylabel("projected length (px)")
    np.atleast_1d(axes)[0].legend(fontsize=8)
    fig.tight_layout()
    fig.savefig(os.path.join(out_dir, "length_model_fits.png"), dpi=110)
    plt.close(fig)


def run_selffit(lab_dir, track_path, lengths_csv, out_dir, q=0.5):
    track = load_track(track_path)
    ph_all, addr, top, impact = phases_from_track(track)
    fr2i = {f: i for i, f in enumerate(track["frame"])}
    th, ph, Lv, cen = [], [], [], []
    with open(lengths_csv) as f:
        for row in csv.DictReader(f):
            i = fr2i.get(int(row["frame"]))
            if i is None or not np.isfinite(track["theta_out"][i]):
                continue
            if track["kind"][i] != "meas":
                continue
            th.append(track["theta_u"][i])
            ph.append(ph_all[i])
            Lv.append(float(row["L_vis"]))
            cen.append(int(row.get("censored", 0)) == 1)
    th, ph = np.array(th), np.array(ph)
    Lv, cen = np.array(Lv), np.array(cen)
    lines = [f"# length_model self-fit — {lab_dir}",
             f"run-end obs n={len(Lv)} (censored={cen.sum()})  "
             f"phases back/down/thru = {np.sum(ph == 'back')}/{np.sum(ph == 'down')}/{np.sum(ph == 'thru')}",
             f"track phase split: addr_theta={addr:.1f} top@f{track['frame'][top]} impact@f{track['frame'][impact]}",
             ""]
    # if the swing has hand labels, evaluate the label-free self-fit against
    # them — the production-path length-prediction error
    lab = None
    try:
        lab_th, lab_ph, lab_L, _, _, _, _ = assemble_labels(lab_dir, track_path)
        lab = (lab_th, lab_ph, lab_L)
        lines.append(f"labels available: n={len(lab_L)} — self-fit evaluated against them")
        lines.append("")
    except FileNotFoundError:
        pass

    fitted = {}
    for ctor in (M0Const, M2PlanePerPhase, M4Kernel):
        m = self_fit_model(ctor(), th, ph, Lv, cen, q=q)
        fitted[m.name] = m
        Lp = m.predict(th, ph)
        under = (Lv > Lp) & ~cen
        lines.append(f"== {m.name}  [{m.params_str()}]")
        lines.append(f"   envelope violation (obs > pred, uncensored): {under.mean() * 100:.0f}% "
                     f"(target ~{100 * (1 - q):.0f}% for q={q})")
        if lab is not None:
            e = rel_errors(m, *lab)
            lines.append(f"   vs labels  {stats(e)}")
            for phase in PHASES:
                msk = lab[1] == phase
                if msk.any():
                    lines.append(f"   {phase:5s}      {stats(e[msk])}")
        lines.append("")
    os.makedirs(out_dir, exist_ok=True)
    rp = os.path.join(out_dir, "length_selffit_report.txt")
    with open(rp, "w") as f:
        f.write("\n".join(lines) + "\n")
    print("\n".join(lines))
    print(f"[report] {rp}")
    return fitted


# ---------------------------------------------------------------- CLI

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("mode", choices=["labeled", "selffit"])
    ap.add_argument("lab_dir")
    ap.add_argument("--track", default=None, help="stage-1 track CSV (default <lab_dir>/v6/faceon_swing_track.csv)")
    ap.add_argument("--lengths", default=None, help="selffit: run-end lengths CSV (frame,L_vis,censored)")
    ap.add_argument("--q", type=float, default=0.5, help="selffit quantile (0.5 central for H0 two-sided noise)")
    ap.add_argument("--out-dir", default=None)
    a = ap.parse_args()
    track = a.track or os.path.join(a.lab_dir, "v6", "faceon_swing_track.csv")
    out = a.out_dir or os.path.join(a.lab_dir, "h0")
    if a.mode == "labeled":
        run_labeled(a.lab_dir, track, out)
    else:
        if not a.lengths:
            ap.error("selffit requires --lengths")
        run_selffit(a.lab_dir, track, a.lengths, out, q=a.q)


if __name__ == "__main__":
    main()
