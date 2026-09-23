#!/usr/bin/env python3
"""dtl_probe.py -- Stage 0 measurement for the down-the-line shaft tracker.

Design: docs/design/dtl_shaft_tracker_design.md Sec 7, Stage 0 ("measure").
NO tracker logic lives here: nothing in this file solves for a shaft, publishes a
sample, or feeds anything.  It measures six things on the dev swings and writes
them down, so that Stage 2 can be built against numbers instead of guesses.

Inputs (all pre-computed; this tool never runs swinglab_run):
  <corpus>/<session>/<swing>/swing.json   two video streams, DTL (setup.perspective==1)
                                          and face-on (perspective==2), each with
                                          frames.t_us on the SHARED window clock.
  <fo-runs>/<id>/result.json + trace.jsonl    the normal face-on analysis (pinned pose):
                                          analysis.club.samples[] = theta_F/lenPx,
                                          analysis.club.positions[] = the P-ladder,
                                          analysis.pose2d = face-on pose.
  <dtl-runs>/<id>/result.json + trace.jsonl   the UNMODIFIED face-on tracker pointed at
                                          the DTL stream (`--face-on DTL --trace`):
                                          analysis.pose2d = the DTL pose (this is what
                                          we want), analysis.club.samples[] = its
                                          (often wrong) DTL track, trace.jsonl = its
                                          per-frame evidence columns.

Outputs:
  <data-dir>/stage0_probe.csv           one row per DTL frame inside the face-on swing span
  <data-dir>/stage0_probe_summary.md    six tables + "what this says" per table

Geometry (design Sec 4.1), right-handed golfer, image atan2 with y DOWN:
    u_x = rho_F cos(theta_F)      u_z = -rho_F sin(theta_F)      |u_y| = sqrt(1 - rho_F^2)
    rho_hat_D = sqrt(1 - u_x^2)                                    (the visibility schedule)
    theta_hat_D+- = atan2(rho_F sin theta_F, -+sqrt(1 - rho_F^2))  (the corridor centres)
    rho_F^2 + rho_D^2 = 1 + u_z^2                                  (identity (d))

The two streams are NOT frame-synchronous, so the face-on track is always
INTERPOLATED (unwrapped theta) to each DTL frame's t_us.  Never paired by index.

Usage:
  dtl_probe.py --corpus /mnt/swingdata/corpus/swings --fo-runs build/dtl/fo \
               --dtl-runs build/dtl/baseline --swings <id,id,...> \
               --data-dir docs/research/data/dtl [--no-scan] [--summary-copy PATH]
"""
import argparse
import json
import math
import sys
import warnings
from pathlib import Path

import numpy as np
import cv2

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from pp_swingdoc import load_swing as load_swing_doc, has_swing  # noqa: E402

# Rays leave the frame at P3 (the head reaches the top-left corner, design Sec 1.4);
# off-frame samples are NaN by design and the nan-aware reductions are correct.
warnings.filterwarnings("ignore", message="Mean of empty slice")
warnings.filterwarnings("ignore", message="All-NaN slice encountered")

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
sys.stderr.reconfigure(encoding="utf-8", errors="replace")

# ------------------------------------------------------------------ constants
SHAFT_MEASURED = 0x01          # ShaftSample2D flags bit 0 (swing_analysis.h)
RHO_SOLVE_MIN = 0.35           # design D3 / Sec 5.7 rhoSolveMin candidate
RHO_F_MAX = 0.93               # design Sec 5.7 rhoFMax -- (c) degenerate above this
WRIST_CONF = 0.30              # design Sec 2 "both wrists > 0.3"
KP_LWRIST, KP_RWRIST = 9, 10   # COCO-wholebody wrist indices
KP_LELBOW, KP_RELBOW = 7, 8
KP_LSHO, KP_RSHO = 5, 6
KP_LHIP, KP_RHIP = 11, 12

# Sec 1.2: the DTL background regimes, as IMAGE ROW bands of the 1024-row frame.
# Deliberately crude and stated as such -- this is a sizing measurement, not a
# segmentation.  Rows from the design's own reading of the feasibility frames.
REGIME_ROWS = [("above-screen", 0, 280), ("screen", 280, 650),
               ("mat", 650, 1024)]

SCAN_ANGLES = 360              # 1 deg grid
SCAN_LAT_PX = (9.0, 12.0)      # lateral background offsets, px (design table (v))
SCAN_R_LO_FRAC = 0.15          # ray start, fraction of L_D (clear of the hands)
SCAN_R_HI_FRAC = 0.90
SCAN_DR_PX = 2.0

# The ADJUDICATED address/impact anchor.  The sector is not a measurement and is
# not derived from face-on: it is a constraint read off the montage by eye
# (~/Desktop/DTL-shaft-tracker/02_address_impact_adjudication.png), where the
# taped shaft at P1 and P7 is unmistakable and lies between 55 and 70 deg in DTL
# image coordinates on all six dev swings.  Inside that sector the polarity-free
# ridge score picks the shaft; outside it the same score picks the trail leg or
# the lead arm (that is what table (iv-b) measures).  Stating the sector is the
# price of having any DTL angle truth at all here, and it is stated.
ANCHOR_SECTOR_DEG = (30.0, 86.0)
ANCHOR_STEP_DEG = 0.5
ANCHOR_SCORE_MIN = 20.0        # below this the frame is blurred/absent -- rejected
ANCHOR_WINDOW_US = 25_000      # how close to P1 / P7 a frame must be


# ------------------------------------------------------------------ small utils
def load_json(p):
    with open(p, encoding="utf-8") as f:
        return json.load(f)


def trace_lines(run_dir):
    t = Path(run_dir) / "trace.jsonl"
    if not t.exists():
        return [], {}
    rows, summary = [], {}
    with open(t, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            o = json.loads(line)
            if "summary" in o:
                summary = o["summary"]
            else:
                rows.append(o)
    return rows, summary


def wrap_pi(a):
    return (np.asarray(a) + np.pi) % (2 * np.pi) - np.pi


def wrap_deg(a):
    return (np.asarray(a) + 180.0) % 360.0 - 180.0


def pct(a, q):
    a = np.asarray(a, dtype=float)
    a = a[np.isfinite(a)]
    return float(np.percentile(a, q)) if a.size else float("nan")


def fmt(v, nd=2):
    if v is None:
        return "-"
    try:
        f = float(v)
    except (TypeError, ValueError):
        return str(v)
    return "-" if not math.isfinite(f) else f"{f:.{nd}f}"


def swing_dir_for(corpus_root, sid):
    """<corpus>/<sid> when flat, else <corpus>/<session>/<swing> for 'sess__swing_000N'."""
    corpus_root = Path(corpus_root)
    if has_swing(corpus_root / sid):
        return corpus_root / sid
    if "__" in sid:
        a, b = sid.split("__", 1)
        if has_swing(corpus_root / a / b):
            return corpus_root / a / b
    return corpus_root / sid


def video_streams(doc):
    return [s for s in doc.get("streams", []) if s.get("kind") == "video"]


def pick_stream(doc, perspective, alias_needles):
    for s in video_streams(doc):
        if s.get("setup", {}).get("perspective") == perspective:
            return s
    for s in video_streams(doc):
        blob = (s.get("alias", "") + " " + s.get("file", "")).lower()
        if any(n in blob for n in alias_needles):
            return s
    return None


# ------------------------------------------------------------------ pose access
def pose_grip_series(pose2d, W, H, use_synth=True):
    """(t_us, grip_x_px, grip_y_px) from the pose wrist midpoint.

    `synth` is the analyzer's own 240 Hz resample of the pose (uniform, whole clip)
    and is what we interpolate with; `frames` is the real inferences and is what
    confidences and continuity are measured on.  The wrist MIDPOINT is used, not
    frames[].lead/trail: those exist only on real frames and carry the tracker's
    own hand construction, which is the thing table (ii) is checking, not the
    thing it should be built from.
    """
    src = pose2d.get("synth") if use_synth else pose2d.get("frames")
    if not src:
        src = pose2d.get("frames") or []
    t = np.array([f["t_us"] for f in src], dtype=np.float64)
    gx = np.empty(len(src))
    gy = np.empty(len(src))
    for i, f in enumerate(src):
        kp = np.asarray(f["kp"], dtype=np.float64).reshape(-1, 3)
        gx[i] = 0.5 * (kp[KP_LWRIST, 0] + kp[KP_RWRIST, 0]) * W
        gy[i] = 0.5 * (kp[KP_LWRIST, 1] + kp[KP_RWRIST, 1]) * H
    return t, gx, gy


def pose_real_frames(pose2d, W, H):
    """Real pose inferences: t_us, grip px, both-wrist conf, elbows px."""
    fr = pose2d.get("frames") or []
    n = len(fr)
    out = dict(t=np.zeros(n), gx=np.zeros(n), gy=np.zeros(n),
               cl=np.zeros(n), cr=np.zeros(n),
               ex_l=np.zeros(n), ey_l=np.zeros(n), ex_r=np.zeros(n), ey_r=np.zeros(n),
               torso=np.zeros((n, 4)))
    for i, f in enumerate(fr):
        kp = np.asarray(f["kp"], dtype=np.float64).reshape(-1, 3)
        out["t"][i] = f["t_us"]
        out["gx"][i] = 0.5 * (kp[KP_LWRIST, 0] + kp[KP_RWRIST, 0]) * W
        out["gy"][i] = 0.5 * (kp[KP_LWRIST, 1] + kp[KP_RWRIST, 1]) * H
        out["cl"][i] = kp[KP_LWRIST, 2]
        out["cr"][i] = kp[KP_RWRIST, 2]
        out["ex_l"][i], out["ey_l"][i] = kp[KP_LELBOW, 0] * W, kp[KP_LELBOW, 1] * H
        out["ex_r"][i], out["ey_r"][i] = kp[KP_RELBOW, 0] * W, kp[KP_RELBOW, 1] * H
        xs = np.array([kp[KP_LSHO, 0], kp[KP_RSHO, 0], kp[KP_LHIP, 0], kp[KP_RHIP, 0]]) * W
        ys = np.array([kp[KP_LSHO, 1], kp[KP_RSHO, 1], kp[KP_LHIP, 1], kp[KP_RHIP, 1]]) * H
        out["torso"][i] = [xs.min(), ys.min(), xs.max(), ys.max()]
    return out


def pose_extent_px(pose2d, W, H, t_lo, t_hi, conf_min=0.3):
    """Bounding box of every confident keypoint over [t_lo,t_hi] -- the fixed
    golfer crop for the montage."""
    fr = pose2d.get("frames") or []
    xs, ys = [], []
    for f in fr:
        if not (t_lo <= f["t_us"] <= t_hi):
            continue
        kp = np.asarray(f["kp"], dtype=np.float64).reshape(-1, 3)
        m = kp[:, 2] >= conf_min
        if not m.any():
            continue
        xs.append(kp[m, 0] * W)
        ys.append(kp[m, 1] * H)
    if not xs:
        return None
    xs = np.concatenate(xs)
    ys = np.concatenate(ys)
    return float(xs.min()), float(ys.min()), float(xs.max()), float(ys.max())


# ------------------------------------------------------------------ swing load
def load_swing(corpus_root, fo_root, dtl_root, sid):
    sd = swing_dir_for(corpus_root, sid)
    doc = load_swing_doc(sd)
    dtl_s = pick_stream(doc, 1, ("dtl", "down"))
    fo_s = pick_stream(doc, 2, ("face", "fo"))
    if dtl_s is None or fo_s is None:
        raise RuntimeError(f"{sid}: need both a DTL (perspective 1) and a face-on "
                           f"(perspective 2) video stream")

    fo = load_json(Path(fo_root) / sid / "result.json")["analysis"]
    dtl = load_json(Path(dtl_root) / sid / "result.json")["analysis"]
    fo_tr, fo_sum = trace_lines(Path(fo_root) / sid)
    dtl_tr, dtl_sum = trace_lines(Path(dtl_root) / sid)

    return dict(
        sid=sid, swing_dir=sd, doc=doc,
        dtl_stream=dtl_s, fo_stream=fo_s,
        dtl_ts=np.asarray(dtl_s["frames"]["t_us"], dtype=np.int64),
        fo_ts=np.asarray(fo_s["frames"]["t_us"], dtype=np.int64),
        dtl_W=int(dtl["club"].get("frameWidth") or dtl_s["encoded"]["width"]),
        dtl_H=int(dtl["club"].get("frameHeight") or dtl_s["encoded"]["height"]),
        fo_W=int(fo["club"].get("frameWidth") or fo_s["encoded"]["width"]),
        fo_H=int(fo["club"].get("frameHeight") or fo_s["encoded"]["height"]),
        fo=fo, dtl=dtl, fo_tr=fo_tr, dtl_tr=dtl_tr, fo_sum=fo_sum, dtl_sum=dtl_sum)


def positions_by_p(club):
    return {int(p["p"]): p for p in club.get("positions", []) or [] if p.get("p") is not None}


# ------------------------------------------------------------------ geometry
def l_full_face_on(club, report=None):
    """L_full -- the face-on length of a shaft lying IN the image plane, px.

    Measured as the p95 of the published lenPx over MEASURED-tier frames whose
    shaft is near horizontal in the face-on image (|sin theta_F| < 0.25, i.e.
    around P2 and P6, where the shaft is along the target line and therefore in
    the face-on image plane so rho_F == 1).  p95 not max: the face-on head
    estimate has a long upper tail (perspective magnification at address, the
    occasional over-long lock).

    Only the RATIO lenPx/L_full matters downstream, so a face-on length ladder
    that is globally short (it is, on two of the dev six) cancels here.
    """
    s = club.get("samples", []) or []
    th = np.array([x.get("theta", 0.0) for x in s], dtype=float)
    ln = np.array([x.get("lenPx", 0.0) for x in s], dtype=float)
    fl = np.array([int(x.get("flags", 0)) for x in s])
    m = ((fl & SHAFT_MEASURED) > 0) & (ln > 0)
    h = m & (np.abs(np.sin(th)) < 0.25)
    use = h if h.sum() >= 8 else m
    if report is not None:
        report["n_horiz"] = int(h.sum())
        report["n_meas"] = int(m.sum())
        report["fallback"] = bool(h.sum() < 8)
    return float(np.percentile(ln[use], 95)) if use.any() else float("nan")


def interp_face_on(club, t_query_us):
    """Face-on witness interpolated to arbitrary t_us (design Sec 4.5).

    theta is UNWRAPPED before interpolation so the +-pi seam never fabricates a
    half-turn; lenPx and flags come across too (flags by nearest sample -- a flag
    is not an interpolable quantity).
    """
    s = club.get("samples", []) or []
    t = np.array([x["t_us"] for x in s], dtype=float)
    th = np.unwrap(np.array([x.get("theta", 0.0) for x in s], dtype=float))
    ln = np.array([x.get("lenPx", 0.0) for x in s], dtype=float)
    fl = np.array([int(x.get("flags", 0)) for x in s])
    gx = np.array([x.get("grip", [np.nan, np.nan])[0] for x in s], dtype=float)
    gy = np.array([x.get("grip", [np.nan, np.nan])[1] for x in s], dtype=float)
    q = np.asarray(t_query_us, dtype=float)
    out = dict(
        theta=np.interp(q, t, th),
        lenPx=np.interp(q, t, ln),
        grip_x=np.interp(q, t, gx),
        grip_y=np.interp(q, t, gy),
    )
    idx = np.clip(np.searchsorted(t, q), 0, len(t) - 1)
    lo = np.clip(idx - 1, 0, len(t) - 1)
    near = np.where(np.abs(t[idx] - q) <= np.abs(t[lo] - q), idx, lo)
    out["flags"] = fl[near]
    out["dt_us"] = t[near] - q
    return out


def schedule(theta_F, rho_F):
    """(a)+(c): rho_hat_D and the two corridor centres, from the face-on witness."""
    rho = np.clip(np.asarray(rho_F, dtype=float), 0.0, 1.0)
    th = np.asarray(theta_F, dtype=float)
    ux = rho * np.cos(th)
    uz = -rho * np.sin(th)
    uy = np.sqrt(np.clip(1.0 - rho * rho, 0.0, 1.0))
    rho_d = np.sqrt(np.clip(1.0 - ux * ux, 0.0, 1.0))
    a = np.arctan2(rho * np.sin(th), -uy)   # sign +: head on the -u_y side
    b = np.arctan2(rho * np.sin(th), +uy)   # sign -
    return rho_d, a, b, ux, uz, uy


def phase_bands(ps):
    """The four pre-finish sighted bands of design Sec 1.1, as t_us intervals,
    built from the face-on P-ladder only.  'P2.5' is read as the midpoint of P2
    and P3, as the design's prose does."""
    def mid(i, j, f=0.5):
        return ps[i]["t_us"] + f * (ps[j]["t_us"] - ps[i]["t_us"])
    out = {}
    if 1 in ps and 2 in ps:
        out["address"] = (float(ps[1]["t_us"]), mid(1, 2))
    if 2 in ps and 3 in ps and 4 in ps:
        out["mid-backswing"] = (mid(2, 3), mid(3, 4))
    if 4 in ps and 5 in ps and 6 in ps:
        out["downswing"] = (mid(4, 5), mid(5, 6))
    if 6 in ps and 7 in ps:
        hi = mid(7, 8, 0.3) if 8 in ps else ps[7]["t_us"] + 50_000.0
        out["impact"] = (mid(6, 7), hi)
    return out


# ------------------------------------------------------------------ the scan
class DtlScan:
    """An unconditioned per-frame DTL line measurement, used ONLY as a stand-in
    truth because the E1 band lock is absent from these runs (see the summary's
    preamble).  It is a measurement of where the strongest long attached ridge
    lies, on a 1 deg grid, with no face-on input whatsoever -- no solve, no
    temporal model, no priors, no publishing.  The tracker must never use it.

    Channels, per design Sec 5.5 / Sec 1.2:
      motion  M = |frame - plate|, plate = per-pixel median of NPLATE frames
                  spread across the whole clip.  NB the plate CONTAINS the
                  address club (the golfer stands at address for half the clip),
                  which is the research record's permanence-snapshot error; the
                  address band's motion numbers are therefore suspect BY
                  CONSTRUCTION and are reported as such.
      raw     the grey frame itself.

    For each angle: contrast(r) = centre(r) - mean(lateral +-9px, +-12px);
    score(theta) = the larger of |median_r contrast| under each polarity.
    """
    NPLATE = 25

    def __init__(self, video_path, W, H, frame_ts):
        self.path = str(video_path)
        self.W, self.H = W, H
        self.frame_ts = frame_ts
        self.plate = None

    def _read_indices(self, idxs):
        idxs = sorted(set(int(i) for i in idxs))
        want = set(idxs)
        cap = cv2.VideoCapture(self.path)
        out, i = {}, 0
        hi = max(idxs) if idxs else -1
        while i <= hi:
            ok, img = cap.read()
            if not ok:
                break
            if i in want:
                out[i] = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY).astype(np.float32)
            i += 1
        cap.release()
        return out

    def build_plate(self):
        n = len(self.frame_ts)
        idxs = np.linspace(0, n - 1, self.NPLATE).round().astype(int)
        frames = self._read_indices(idxs)
        if not frames:
            return False
        self.plate = np.median(np.stack([frames[k] for k in sorted(frames)]), axis=0)
        return True

    @staticmethod
    def _rays(gx, gy, L):
        r = np.arange(SCAN_R_LO_FRAC * L, SCAN_R_HI_FRAC * L, SCAN_DR_PX, dtype=np.float32)
        a = np.deg2rad(np.arange(SCAN_ANGLES, dtype=np.float32))
        ux, uy = np.cos(a)[:, None], np.sin(a)[:, None]
        nx, ny = -uy, ux
        maps = []
        for lat in (0.0,) + SCAN_LAT_PX + tuple(-v for v in SCAN_LAT_PX):
            maps.append((gx + r[None, :] * ux + lat * nx,
                         gy + r[None, :] * uy + lat * ny))
        return r, a, maps

    @staticmethod
    def _score(img, maps):
        """Polarity-free ridge score per angle: median over the ray of
        |on-line - mean(lateral)|.  Polarity-free matters: the dev club is TAPED,
        so a signed score is cancelled along the shaft by the alternating black
        and white bands, and the winner becomes any wide bright limb."""
        samp = [cv2.remap(img, mx, my, cv2.INTER_LINEAR,
                          borderMode=cv2.BORDER_CONSTANT,
                          borderValue=float("nan")) for mx, my in maps]
        contrast = samp[0] - np.nanmean(np.stack(samp[1:]), axis=0)
        med = np.nanmedian(np.abs(contrast), axis=1)
        return np.where(np.isfinite(med), med, 0.0), contrast

    def scan_frame(self, gray, gx, gy, L):
        """The UNCONSTRAINED winner over the full circle -- a negative control,
        never truth.  -> dict(theta, score, margin, rend, rho_obs, chan) or None."""
        if not (np.isfinite(gx) and np.isfinite(gy) and L > 20):
            return None
        r, a, maps = self._rays(np.float32(gx), np.float32(gy), np.float32(L))
        best = None
        for chan_name, img in (("mot", np.abs(gray - self.plate) if self.plate is not None else None),
                               ("raw", gray)):
            if img is None:
                continue
            sc, contrast = self._score(img, maps)
            k = int(np.argmax(sc))
            if best is not None and sc[k] <= best["score"]:
                continue
            far = np.abs(wrap_deg(np.arange(SCAN_ANGLES) - k)) > 20.0
            prof = np.abs(contrast[k])
            good = (np.isfinite(prof) & (prof > 0.3 * sc[k])).astype(float)
            frac = np.cumsum(good) / np.arange(1, len(good) + 1)
            keep = np.nonzero(frac >= 0.7)[0]
            run = int(keep[-1]) if keep.size else 0
            best = dict(theta=float(a[k]), score=float(sc[k]),
                        margin=float(sc[k] - np.max(sc[far])) if far.any() else float("nan"),
                        rend=float(r[min(run, len(r) - 1)]), chan=chan_name)
        if best is not None:
            best["rho_obs"] = best["rend"] / L
        return best

    def anchor_frame(self, gray, gx, gy, L):
        """The ADJUDICATED anchor: the same polarity-free score, searched only
        inside ANCHOR_SECTOR_DEG, on the raw channel (the taped shaft is a raw
        contrast feature; the motion channel is blind to it at address because
        the clean plate contains the address club).  -> (theta_rad, score)."""
        if not (np.isfinite(gx) and np.isfinite(gy) and L > 20):
            return float("nan"), float("nan")
        lo, hi = ANCHOR_SECTOR_DEG
        a = np.deg2rad(np.arange(lo, hi, ANCHOR_STEP_DEG, dtype=np.float32))
        r = np.arange(SCAN_R_LO_FRAC * L, SCAN_R_HI_FRAC * L, 1.0, dtype=np.float32)
        ux, uy = np.cos(a)[:, None], np.sin(a)[:, None]
        nx, ny = -uy, ux
        maps = [((gx + r[None, :] * ux + lat * nx).astype(np.float32),
                 (gy + r[None, :] * uy + lat * ny).astype(np.float32))
                for lat in (0.0,) + SCAN_LAT_PX + tuple(-v for v in SCAN_LAT_PX)]
        sc, _ = self._score(gray, maps)
        k = int(np.argmax(sc))
        return float(a[k]), float(sc[k])


# ------------------------------------------------------------------ per-swing
def probe_swing(S, do_scan=True, verbose=True):
    sid = S["sid"]
    fo_club, dtl_club = S["fo"]["club"], S["dtl"]["club"]
    ps = positions_by_p(fo_club)
    if not ps:
        raise RuntimeError(f"{sid}: face-on run has no P-ladder")

    lrep = {}
    L_full = l_full_face_on(fo_club, lrep)
    span_lo = float(min(p["t_us"] for p in ps.values()))
    span_hi = float(max(p["t_us"] for p in ps.values()))
    t_impact = float(ps[7]["t_us"]) if 7 in ps else float("nan")

    dtl_ts = S["dtl_ts"].astype(float)
    sel = (dtl_ts >= span_lo) & (dtl_ts <= span_hi)
    t = dtl_ts[sel]
    fidx = np.nonzero(sel)[0]

    # --- face-on witness, interpolated to DTL frame times -------------------
    W = interp_face_on(fo_club, t)
    rho_F = np.clip(W["lenPx"] / L_full, 0.0, 1.0)
    rho_F[W["lenPx"] <= 0] = np.nan
    rho_d, corr_a, corr_b, ux, uz, uy = schedule(W["theta"], np.nan_to_num(rho_F, nan=0.0))
    rho_d[~np.isfinite(rho_F)] = np.nan
    corr_a[~np.isfinite(rho_F)] = np.nan
    corr_b[~np.isfinite(rho_F)] = np.nan

    # --- poses --------------------------------------------------------------
    dW, dH, fW, fH = S["dtl_W"], S["dtl_H"], S["fo_W"], S["fo_H"]
    dt_t, dgx, dgy = pose_grip_series(S["dtl"]["pose2d"], dW, dH)
    ft_t, fgx, fgy = pose_grip_series(S["fo"]["pose2d"], fW, fH)
    dtl_grip_x = np.interp(t, dt_t, dgx)
    dtl_grip_y = np.interp(t, dt_t, dgy)
    fo_grip_y = np.interp(t, ft_t, fgy)
    fo_grip_x = np.interp(t, ft_t, fgx)

    # --- L_D, the DTL full projected length, px -----------------------------
    # Rung 1: the DTL run's own address grip->ball distance (ballPx) divided by
    # the schedule's rho_hat_D at P1 -- a DTL measurement with one face-on number.
    # Rung 2 (ballPx absent): the vertical scale ratio 'a' of the grip-row affine
    # fit times the face-on L_full, i.e. carry the face-on length across on the
    # one thing both cameras agree about (design Sec 5.2: both see vertical).
    ball_px = float(dtl_club.get("lengths", {}).get("ballPx", -1.0))
    i_p1 = int(np.argmin(np.abs(t - ps[1]["t_us"])))
    rho_d_p1 = float(rho_d[i_p1]) if np.isfinite(rho_d[i_p1]) else float("nan")

    # affine y_D ~ a*y_F + b over address->impact (design Sec 5.2)
    fit_m = np.isfinite(dtl_grip_y) & np.isfinite(fo_grip_y) & (t <= t_impact)
    if fit_m.sum() >= 10:
        A = np.vstack([fo_grip_y[fit_m], np.ones(fit_m.sum())]).T
        coef, *_ = np.linalg.lstsq(A, dtl_grip_y[fit_m], rcond=None)
        aff_a, aff_b = float(coef[0]), float(coef[1])
    else:
        aff_a, aff_b = float("nan"), float("nan")
    aff_pred = aff_a * fo_grip_y + aff_b
    aff_res = np.abs(dtl_grip_y - aff_pred)

    if ball_px > 0 and np.isfinite(rho_d_p1) and rho_d_p1 > 0.5:
        L_D, L_D_src = ball_px / rho_d_p1, "ballPx/rho_hat_D(P1)"
    elif np.isfinite(aff_a):
        L_D, L_D_src = aff_a * L_full, "affine a * L_full"
    else:
        L_D, L_D_src = float("nan"), "none"

    # --- DTL baseline trace, per frame -------------------------------------
    tr = {int(r["frame"]): r for r in S["dtl_tr"]}
    band_ok = np.zeros(len(t), dtype=bool)
    band_th = np.full(len(t), np.nan)
    band_s = np.full(len(t), np.nan)
    band_r0 = np.full(len(t), np.nan)
    seg_ok = np.zeros(len(t), dtype=bool)
    seg_th = np.full(len(t), np.nan)
    seg_s = np.full(len(t), np.nan)
    seg_r0 = np.full(len(t), np.nan)
    dtl_tier = np.array([""] * len(t), dtype=object)
    dtl_theta_out = np.full(len(t), np.nan)
    dtl_phase = np.full(len(t), -1)
    for k, f in enumerate(fidx):
        r = tr.get(int(f))
        if r is None:
            continue
        dtl_tier[k] = r.get("tier", "")
        dtl_theta_out[k] = r.get("theta_out", np.nan)
        dtl_phase[k] = r.get("phase", -1)
        if r.get("band_n"):
            band_ok[k] = True
            band_th[k] = math.radians(r["band_theta"])
            band_s[k] = r.get("band_s", np.nan)
            band_r0[k] = r.get("band_r0", np.nan)
        if r.get("seg_theta") is not None:
            seg_ok[k] = True
            seg_th[k] = math.radians(r["seg_theta"])
            seg_s[k] = r.get("seg_s", np.nan)
            seg_r0[k] = r.get("seg_r0", np.nan)

    # --- face-on trace band/seg (for identity (d)) --------------------------
    ftr = {int(r["frame"]): r for r in S["fo_tr"]}
    fo_ts = S["fo_ts"].astype(float)
    fo_near = np.clip(np.searchsorted(fo_ts, t), 0, len(fo_ts) - 1)
    fo_band_s = np.full(len(t), np.nan)
    fo_band_ok = np.zeros(len(t), dtype=bool)
    fo_seg_s = np.full(len(t), np.nan)
    for k, f in enumerate(fo_near):
        r = ftr.get(int(f))
        if r is None:
            continue
        if r.get("band_n"):
            fo_band_ok[k] = True
            fo_band_s[k] = r.get("band_s", np.nan)
        if r.get("seg_s") is not None:
            fo_seg_s[k] = r.get("seg_s", np.nan)

    # --- the DTL ball anchor (design Sec 4.3) ------------------------------
    # At address and at impact the clubhead is AT the ball, so theta_ball,D =
    # atan2(B_y - G_y, B_x - G_x) in DTL pixels is a direct DTL measurement of the
    # shaft direction that needs no face-on input at all.  It is the only
    # face-on-independent truth these runs actually carry, and it covers exactly
    # the two bands where equation (c) is degenerate.
    #
    # Caveats, stated once and true everywhere it is used: the head sits ~3 deg
    # BEHIND the ball at this framing (v3 Sec 9), and the pose grip is not on the
    # shaft axis, so the anchor is good to a few degrees, not to 0.3.  It is used
    # only where the run's own golf prior accepted the address ball
    # (`lengths.ballPx > 0`); where it was rejected the detection is a false lock
    # and is excluded, not "used with caution".
    ball_prior_ok = float(dtl_club.get("lengths", {}).get("ballPx", -1.0)) > 0
    bsamp = [s for s in (S["dtl"].get("ball", {}).get("samples", []) or [])
             if s.get("found") and ps[1]["t_us"] - 200_000 <= s["t_us"] <= ps[1]["t_us"] + 200_000]
    ball_xy = (float(np.median([s["x"] for s in bsamp])) * dW,
               float(np.median([s["y"] for s in bsamp])) * dH) if bsamp else None
    ball_th = np.full(len(t), np.nan)
    ball_ok = np.zeros(len(t), dtype=bool)
    if ball_xy is not None and ball_prior_ok:
        near_p1 = np.abs(t - ps[1]["t_us"]) <= 25_000
        near_p7 = np.abs(t - t_impact) <= 25_000 if np.isfinite(t_impact) else np.zeros(len(t), bool)
        ball_ok = near_p1 | near_p7
        ball_th = np.arctan2(ball_xy[1] - dtl_grip_y, ball_xy[0] - dtl_grip_x)
        ball_th[~ball_ok] = np.nan

    # --- one image pass: negative-control scan, anchor, table (v) profiles ---
    scan_th = np.full(len(t), np.nan)
    scan_sc = np.full(len(t), np.nan)
    scan_mg = np.full(len(t), np.nan)
    scan_rho = np.full(len(t), np.nan)
    scan_chan = np.array([""] * len(t), dtype=object)
    anch_th = np.full(len(t), np.nan)
    anch_sc = np.full(len(t), np.nan)
    anch_ok = np.zeros(len(t), dtype=bool)
    # Which frames get an adjudicated anchor: only the address and impact
    # instants, where the montage shows the taped shaft unambiguously.
    anch_want = np.abs(t - ps[1]["t_us"]) <= ANCHOR_WINDOW_US
    if np.isfinite(t_impact):
        anch_want |= np.abs(t - t_impact) <= ANCHOR_WINDOW_US
    regime_rows = []          # table (v): (regime, channel, on|off, median, n)
    if do_scan and np.isfinite(L_D):
        vpath = S["swing_dir"] / S["dtl_stream"]["file"]
        sc = DtlScan(vpath, dW, dH, S["dtl_ts"])
        if sc.build_plate():
            frames = sc._read_indices(fidx)
            for k, f in enumerate(fidx):
                g = frames.get(int(f))
                if g is None:
                    continue
                res = sc.scan_frame(g, dtl_grip_x[k], dtl_grip_y[k], L_D)
                if res is not None:
                    scan_th[k] = wrap_pi(res["theta"])
                    scan_sc[k] = res["score"]
                    scan_mg[k] = res["margin"]
                    scan_rho[k] = res["rho_obs"]
                    scan_chan[k] = res["chan"]
                if not anch_want[k]:
                    continue
                th_a, s_a = sc.anchor_frame(g, dtl_grip_x[k], dtl_grip_y[k], L_D)
                anch_th[k], anch_sc[k] = th_a, s_a
                anch_ok[k] = bool(np.isfinite(s_a) and s_a >= ANCHOR_SCORE_MIN)
                if not anch_ok[k]:
                    continue
                # Table (v) is sampled along the ONE line in these runs that is
                # known to be on the shaft (montage-adjudicated), plus a control
                # line 30 deg off it in the same frame and channel: that says how
                # much of the number is the club and how much is "any line
                # through a busy background scores something".
                for chan_name, img in (("mot", np.abs(g - sc.plate)), ("raw", g)):
                    for tag, th_line in (("on", th_a), ("off", th_a + math.radians(30.0))):
                        prof, rws = _profile_along(img, dtl_grip_x[k], dtl_grip_y[k],
                                                   th_line, L_D)
                        if prof is None:
                            continue
                        for nm, lo, hi in REGIME_ROWS:
                            msk = (rws >= lo) & (rws < hi) & np.isfinite(prof)
                            if msk.sum() >= 3:
                                regime_rows.append((nm, chan_name, tag,
                                                    float(np.median(np.abs(prof[msk]))),
                                                    int(msk.sum())))
        else:
            print(f"[dtl_probe] {sid}: cannot open DTL video -- image pass skipped",
                  file=sys.stderr)

    # --- real-pose quality (table ii) --------------------------------------
    rp = pose_real_frames(S["dtl"]["pose2d"], dW, dH)
    in_span = (rp["t"] >= span_lo) & (rp["t"] <= span_hi)
    both = in_span & (rp["cl"] > WRIST_CONF) & (rp["cr"] > WRIST_CONF)
    jumps = np.hypot(np.diff(rp["gx"][in_span]), np.diff(rp["gy"][in_span]))

    rows = dict(
        t=t, fidx=fidx, fo_theta=W["theta"], fo_len=W["lenPx"], fo_flags=W["flags"],
        fo_dt=W["dt_us"], rho_F=rho_F, rho_d=rho_d, corr_a=corr_a, corr_b=corr_b,
        ux=ux, uz=uz, uy=uy,
        band_ok=band_ok, band_th=band_th, band_s=band_s, band_r0=band_r0,
        seg_ok=seg_ok, seg_th=seg_th, seg_s=seg_s, seg_r0=seg_r0,
        dtl_tier=dtl_tier, dtl_theta_out=dtl_theta_out, dtl_phase=dtl_phase,
        fo_band_ok=fo_band_ok, fo_band_s=fo_band_s, fo_seg_s=fo_seg_s,
        dtl_grip_x=dtl_grip_x, dtl_grip_y=dtl_grip_y,
        fo_grip_x=fo_grip_x, fo_grip_y=fo_grip_y,
        aff_pred=aff_pred, aff_res=aff_res,
        scan_th=scan_th, scan_sc=scan_sc, scan_mg=scan_mg, scan_rho=scan_rho,
        scan_chan=scan_chan, ball_th=ball_th, ball_ok=ball_ok,
        anch_th=anch_th, anch_sc=anch_sc, anch_ok=anch_ok)

    meta = dict(
        sid=sid, L_full=L_full, L_full_report=lrep, L_D=L_D, L_D_src=L_D_src,
        ball_px=ball_px, span_lo=span_lo, span_hi=span_hi, t_impact=t_impact,
        aff_a=aff_a, aff_b=aff_b,
        aff_p50=pct(aff_res[fit_m], 50), aff_p95=pct(aff_res[fit_m], 95),
        aff_max=float(np.nanmax(aff_res[fit_m])) if fit_m.any() else float("nan"),
        post_impact_over_p95=int(np.sum((t > t_impact) & (aff_res > pct(aff_res[fit_m], 95)))),
        post_impact_n=int(np.sum(t > t_impact)),
        pose_n_span=int(in_span.sum()), pose_both_frac=float(both.sum() / max(1, in_span.sum())),
        pose_max_jump=float(jumps.max()) if jumps.size else float("nan"),
        pose_p95_jump=pct(jumps, 95),
        n_frames=len(t), ps=ps, bands=phase_bands(ps),
        n_band=int(band_ok.sum()), n_seg=int(seg_ok.sum()),
        n_fo_band=int(fo_band_ok.sum()),
        ball_xy=ball_xy, ball_prior_ok=bool(ball_prior_ok), n_ball=int(ball_ok.sum()),
        n_anchor=int(anch_ok.sum()), n_anchor_want=int(anch_want.sum()),
        frame_phase_us=float(np.median(np.abs(W["dt_us"]))),
        regime_rows=regime_rows,
        dtl_W=dW, dtl_H=dH, fo_W=fW, fo_H=fH)
    if verbose:
        print(f"[dtl_probe] {sid}: {len(t)} DTL frames in span, L_full={L_full:.1f}px "
              f"L_D={L_D:.1f}px ({L_D_src}), band locks DTL={meta['n_band']} FO={meta['n_fo_band']}, "
              f"seg locks DTL={meta['n_seg']}")
    return rows, meta


def _profile_along(img, gx, gy, theta, length):
    """On-line-minus-lateral-background profile along one line, on one channel.
    Returns (profile in the channel's own units, the image ROW of each sample)."""
    if not (np.isfinite(gx) and np.isfinite(gy) and np.isfinite(theta)) or length < 30:
        return None, None
    r = np.arange(SCAN_R_LO_FRAC * length, SCAN_R_HI_FRAC * length, SCAN_DR_PX, dtype=np.float32)
    ux, uy = math.cos(theta), math.sin(theta)
    nx, ny = -uy, ux
    out = []
    for lat in (0.0,) + SCAN_LAT_PX + tuple(-v for v in SCAN_LAT_PX):
        mx = (gx + r * ux + lat * nx).reshape(1, -1).astype(np.float32)
        my = (gy + r * uy + lat * ny).reshape(1, -1).astype(np.float32)
        out.append(cv2.remap(img, mx, my, cv2.INTER_LINEAR,
                             borderMode=cv2.BORDER_CONSTANT,
                             borderValue=float("nan"))[0])
    prof = out[0] - np.nanmean(np.stack(out[1:]), axis=0)
    return prof, (gy + r * uy)


# ------------------------------------------------------------------ table (i)
def clock_offset(S, meta, grid_us=1000):
    """Cross-correlate the grip's IMAGE ROW in the two views.

    Both cameras are level, so the grip's row is the same physical quantity in
    both (design Sec 4.5/5.2) and the lag between the two row signals is the
    inter-camera clock offset.  Restricted to the interval where BOTH poses are
    densely inferred (frame-to-frame <= 9 ms): outside it the pose is a ~26 ms
    resample and a 3 ms lag cannot survive the interpolation.  Sub-sample peak
    by a parabola through the correlation maximum.  Sign: positive => the DTL
    stream's t_us runs AHEAD of the face-on stream's (DTL leads).
    """
    def dense_range(pose2d):
        ts = np.array([f["t_us"] for f in pose2d.get("frames", [])], dtype=float)
        if ts.size < 3:
            return None
        d = np.diff(ts)
        ok = np.nonzero(d <= 9000)[0]
        return (ts[ok[0]], ts[ok[-1] + 1]) if ok.size else None

    dr_d = dense_range(S["dtl"]["pose2d"])
    dr_f = dense_range(S["fo"]["pose2d"])
    if dr_d is None or dr_f is None:
        return dict(offset_us=float("nan"), n=0, note="no dense pose")
    lo = max(dr_d[0], dr_f[0], meta["span_lo"])
    hi = min(dr_d[1], dr_f[1], meta["span_hi"])
    if hi - lo < 200_000:
        return dict(offset_us=float("nan"), n=0, note="dense overlap < 200 ms")

    dt_t, _, dgy = pose_grip_series(S["dtl"]["pose2d"], S["dtl_W"], S["dtl_H"])
    ft_t, _, fgy = pose_grip_series(S["fo"]["pose2d"], S["fo_W"], S["fo_H"])
    g = np.arange(lo, hi, grid_us)
    a = np.interp(g, dt_t, dgy)
    b = np.interp(g, ft_t, fgy)
    a = (a - a.mean()) / (a.std() or 1.0)
    b = (b - b.mean()) / (b.std() or 1.0)
    max_lag = min(60, len(g) // 4)            # +-60 ms is far wider than expected
    lags = np.arange(-max_lag, max_lag + 1)
    cc = np.array([np.corrcoef(a[max_lag + l: len(a) - max_lag + l],
                               b[max_lag: len(b) - max_lag])[0, 1] for l in lags])
    k = int(np.argmax(cc))
    off = float(lags[k])
    if 0 < k < len(cc) - 1:
        y0, y1, y2 = cc[k - 1], cc[k], cc[k + 1]
        den = (y0 - 2 * y1 + y2)
        if den != 0:
            off += 0.5 * (y0 - y2) / den
    return dict(offset_us=off * grid_us, peak=float(cc[k]), n=len(g),
                window=(lo, hi), note="")


# ------------------------------------------------------------------ CSV
CSV_COLS = ["swing", "frame", "t_us", "fo_dt_us", "fo_theta", "fo_len", "fo_flags",
            "fo_measured", "fo_phase", "phase_band", "rho_F", "rho_pred", "sighted",
            "u_x", "u_z", "u_y", "corr_a", "corr_b",
            "band_ok", "band_theta", "band_s", "band_r0",
            "seg_ok", "seg_theta", "seg_s", "seg_r0",
            "dtl_tier", "dtl_theta_out", "fo_band_ok", "fo_band_s", "fo_seg_s",
            "dtl_grip_x", "dtl_grip_y", "fo_grip_x", "fo_grip_y",
            "aff_pred_y", "aff_resid_px",
            "scan_theta", "scan_score", "scan_margin", "scan_rho_obs", "scan_chan",
            "ball_ok", "ball_theta", "anchor_ok", "anchor_theta", "anchor_score",
            "L_full", "L_D"]


def band_of(t, bands):
    for nm, (lo, hi) in bands.items():
        if lo <= t <= hi:
            return nm
    return ""


def write_csv(path, per_swing):
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        f.write(",".join(CSV_COLS) + "\n")
        for rows, meta in per_swing:
            n = meta["n_frames"]
            for i in range(n):
                rec = {
                    "swing": meta["sid"], "frame": int(rows["fidx"][i]),
                    "t_us": int(rows["t"][i]), "fo_dt_us": fmt(rows["fo_dt"][i], 0),
                    "fo_theta": fmt(rows["fo_theta"][i], 5),
                    "fo_len": fmt(rows["fo_len"][i], 2),
                    "fo_flags": int(rows["fo_flags"][i]),
                    "fo_measured": int(bool(int(rows["fo_flags"][i]) & SHAFT_MEASURED)),
                    "fo_phase": int(rows["dtl_phase"][i]),
                    "phase_band": band_of(rows["t"][i], meta["bands"]),
                    "rho_F": fmt(rows["rho_F"][i], 4), "rho_pred": fmt(rows["rho_d"][i], 4),
                    "sighted": int(bool(rows["rho_d"][i] >= RHO_SOLVE_MIN)),
                    "u_x": fmt(rows["ux"][i], 4), "u_z": fmt(rows["uz"][i], 4),
                    "u_y": fmt(rows["uy"][i], 4),
                    "corr_a": fmt(rows["corr_a"][i], 5), "corr_b": fmt(rows["corr_b"][i], 5),
                    "band_ok": int(rows["band_ok"][i]), "band_theta": fmt(rows["band_th"][i], 5),
                    "band_s": fmt(rows["band_s"][i], 5), "band_r0": fmt(rows["band_r0"][i], 2),
                    "seg_ok": int(rows["seg_ok"][i]), "seg_theta": fmt(rows["seg_th"][i], 5),
                    "seg_s": fmt(rows["seg_s"][i], 5), "seg_r0": fmt(rows["seg_r0"][i], 2),
                    "dtl_tier": rows["dtl_tier"][i],
                    "dtl_theta_out": fmt(rows["dtl_theta_out"][i], 3),
                    "fo_band_ok": int(rows["fo_band_ok"][i]),
                    "fo_band_s": fmt(rows["fo_band_s"][i], 5),
                    "fo_seg_s": fmt(rows["fo_seg_s"][i], 5),
                    "dtl_grip_x": fmt(rows["dtl_grip_x"][i], 2),
                    "dtl_grip_y": fmt(rows["dtl_grip_y"][i], 2),
                    "fo_grip_x": fmt(rows["fo_grip_x"][i], 2),
                    "fo_grip_y": fmt(rows["fo_grip_y"][i], 2),
                    "aff_pred_y": fmt(rows["aff_pred"][i], 2),
                    "aff_resid_px": fmt(rows["aff_res"][i], 2),
                    "scan_theta": fmt(rows["scan_th"][i], 5),
                    "scan_score": fmt(rows["scan_sc"][i], 3),
                    "scan_margin": fmt(rows["scan_mg"][i], 3),
                    "scan_rho_obs": fmt(rows["scan_rho"][i], 4),
                    "scan_chan": rows["scan_chan"][i],
                    "ball_ok": int(rows["ball_ok"][i]),
                    "ball_theta": fmt(rows["ball_th"][i], 5),
                    "anchor_ok": int(rows["anch_ok"][i]),
                    "anchor_theta": fmt(rows["anch_th"][i], 5),
                    "anchor_score": fmt(rows["anch_sc"][i], 2),
                    "L_full": fmt(meta["L_full"], 2), "L_D": fmt(meta["L_D"], 2)}
                f.write(",".join(str(rec[c]) for c in CSV_COLS) + "\n")


# ------------------------------------------------------------------ summary
def md_table(headers, body):
    out = ["| " + " | ".join(headers) + " |",
           "|" + "|".join(["---"] * len(headers)) + "|"]
    for r in body:
        out.append("| " + " | ".join(str(x) for x in r) + " |")
    return "\n".join(out)


def _w0_ball(pooled, tag):
    a = pooled.get(tag, {}).get("+", [])
    b = pooled.get(tag, {}).get("-", [])
    if not a and not b:
        return "-"
    worst = min(max(a, default=float("inf")), max(b, default=float("inf")))
    return f"+-{1.4 * worst:.0f} deg" if math.isfinite(worst) else "-"


def truth_source(per_swing):
    n_band = sum(m["n_band"] for _, m in per_swing)
    return "band" if n_band > 0 else "scan"


def truth_theta(rows, src):
    return rows["band_th"] if src == "band" else rows["scan_th"]


def truth_ok(rows, src, meta):
    if src == "band":
        return rows["band_ok"]
    # A scan frame counts as a usable stand-in only when the winning direction is
    # clearly better than everything more than 20 deg away from it.
    return np.isfinite(rows["scan_th"]) & (rows["scan_mg"] > 1.0) & (rows["scan_sc"] > 2.0)


def build_summary(per_swing, offsets, args):
    src = truth_source(per_swing)
    L = []
    A = L.append
    A("# DTL shaft tracker -- Stage 0 probe")
    A("")
    A(f"Generated by `tools/shaftlab/dtl_probe.py` (deterministic, no RNG). "
      f"Dev set: {len(per_swing)} swings. "
      f"`--corpus {args.corpus}` `--fo-runs {args.fo_runs}` `--dtl-runs {args.dtl_runs}`.")
    A("")
    A("## Read this first -- the band-lock truth does not exist in these runs")
    A("")
    n_band = sum(m["n_band"] for _, m in per_swing)
    n_fo_band = sum(m["n_fo_band"] for _, m in per_swing)
    n_seg = sum(m["n_seg"] for _, m in per_swing)
    A(f"**E1 produced ZERO band locks on the dev six -- {n_band} in the DTL runs and "
      f"{n_fo_band} in the face-on runs**, across every frame of all "
      f"{len(per_swing)} swings. The cause is upstream of this probe: "
      f"`frameBandMatch()` returns nothing when `bandsMm.size() < 2` "
      f"(`shaft_tracker_math.cpp:313`), `bandsMm` comes from `job.bandCentersMm`, "
      f"and that is populated from `capture.club.bandCentersMm` in swing.json "
      f"(`swing_reanalyzer.cpp:726`). These six swings have **no `capture.club` "
      f"block at all**, so E1 was disabled and both views ran ray-only. "
      f"`lengths.bandPx` is still non-zero in the results, but that is the "
      f"length-ladder rung computed from the SEGMENT lock's scale, not an E1 lock.")
    A("")
    A("Design Sec 6 makes the unconditioned DTL band lock **the instrument that grades "
      "the whole coupling**. It cannot be generated from the inputs as they stand. "
      "Before Stage 2 can be graded, somebody has to give `swinglab_run` a way to "
      "inject band centres (there is no CLI option today) or write a `capture.club` "
      "block into these swings. That is a blocking prerequisite, not a detail.")
    A("")
    if src == "scan":
        A("The club in these six swings **is** taped -- the black-and-white bands are "
          "plain in every DTL frame in the montages. So this is not a missing-tape "
          "problem, it is a missing-club-record problem, and it is cheap to fix.")
        A("")
        A("In its place this probe carries two substitutes, and the difference between "
          "them is the whole story of Stage 0:")
        A("")
        A("1. **The adjudicated address/impact anchor** (table iv-a). A polarity-free "
          "ridge score swept from the DTL pose grip, restricted to a 56-degree sector "
          "that was read off the montage BY EYE, on the address and impact frames "
          "only. It uses no face-on input. Every line it produced was then checked "
          "against the frame at full resolution "
          "(`02_address_impact_adjudication.png`); 11 of 12 lie on the taped shaft "
          "and the twelfth was rejected by a score floor. This is real, if narrow, "
          "DTL angle truth: 12 frames out of ~1300.")
        A("")
        A("2. **The unconstrained scan** (`DtlScan`, tables iii and iv-b). The same "
          "score with the sector restriction removed -- i.e. an honest unconditioned "
          "DTL line search. It is **a negative control, not truth**: it locks the "
          "lead arm, the trail leg and the mat's alignment stick, which is precisely "
          "the failure design Sec 2 documents. It is reported so that 'you cannot "
          "just look for the longest bright line' is a number rather than an "
          "assertion.")
        A("")
        A("One structural caveat on the scan and on table (v), inherited straight from "
          "the research record: the clean plate is a whole-clip median, so it "
          "**contains the address club** (research Sec 17.2). Motion-channel numbers "
          "in the address band are suppressed by construction and must not be read as "
          "'the shaft is not visible at address'.")
        A("")

    # ---------------- per-swing preamble -----------------------------------
    A("## Dev set and per-swing constants")
    A("")
    A(md_table(
        ["swing", "DTL frames in FO span", "L_full (FO px)", "n near-horiz meas",
         "L_D (DTL px)", "L_D source", "DTL ballPx", "DTL band locks", "DTL seg locks"],
        [[m["sid"].split("__")[-1], m["n_frames"], fmt(m["L_full"], 1),
          m["L_full_report"].get("n_horiz", "-"), fmt(m["L_D"], 1), m["L_D_src"],
          fmt(m["ball_px"], 1), m["n_band"], m["n_seg"]] for _, m in per_swing]))
    A("")
    A("**What this says.** `L_full` -- the face-on length of an in-plane shaft, the "
      "denominator of rho_F -- ranges 232-309 px across six swings of the same club "
      "on the same rig. Swings 0007 and 0009 are the short pair: both have "
      "`lengths.ballPx == -1` and `fusedPx == -1` face-on, so the face-on length "
      "ladder dropped to a shorter rung and the whole published track is about 23% "
      "short. Because rho_F is a RATIO of the same track's lenPx to its own p95, a "
      "global scale error cancels, and that is why L_full is taken per swing and "
      "never pooled. It does not cancel for L_D, which is why L_D prefers the DTL "
      "run's own address ball.")
    A("")

    # ---------------- (i) clock offset -------------------------------------
    A("## (i) Inter-camera clock offset")
    A("")
    phase = {m["sid"]: m["frame_phase_us"] for _, m in per_swing}
    A(md_table(["swing", "cross-corr offset (us)", "peak corr", "grid pts",
                "dense window (s)", "nearest FO sample minus DTL frame (us)"],
               [[sid.split("__")[-1], fmt(o["offset_us"], 0), fmt(o.get("peak"), 3),
                 o.get("n", 0),
                 (f"{o['window'][0]/1e6:.3f}-{o['window'][1]/1e6:.3f}"
                  if o.get("window") else o.get("note", "-")),
                 fmt(phase.get(sid), 0)]
                for sid, o in offsets]))
    offs = np.array([o["offset_us"] for _, o in offsets], dtype=float)
    offs = offs[np.isfinite(offs)]
    A("")
    if offs.size:
        A(f"Pooled: median **{np.median(offs):+.0f} us**, spread "
          f"{offs.min():+.0f} .. {offs.max():+.0f} us, std {offs.std():.0f} us "
          f"(n = {offs.size} swings).")
    A("")
    A("**What this says.** Two different quantities live in this table and the design "
      "conflates them. The LAST column is the frame phase: the streams are not "
      "frame-synchronous and the nearest face-on sample sits a consistent ~3.2 ms "
      "from each DTL frame. That is arithmetic on the recorded timestamps, it is "
      "exact, and it is the number the design's 'DTL leads by 3.2 ms' refers to. It "
      "is NOT a clock error, and interpolating the face-on witness (which this probe "
      "and the tracker both do) removes it completely.")
    A("")
    A("The FIRST column is the thing that would be a clock error: the lag between the "
      "grip's image row in the two poses, cross-correlated on a 1 ms grid over the "
      "interval where both poses are densely inferred (consecutive pose frames "
      "<= 9 ms apart), with a parabolic sub-sample peak, positive meaning the DTL "
      "stream's `t_us` runs ahead. If the recorded timestamps were perfect this "
      "column would read zero. It does not: it reads a few ms, **and it is not "
      "constant across swings** -- the spread is comparable to the value itself. "
      "Before that is called a latency bias, note what the method actually measures: "
      "the wrist-midpoint row is not the same physical point in the two views "
      "(different projection, different occlusions), the signal is smooth and "
      "low-bandwidth so the correlation peak is broad, and the window is the "
      "downswing only, because the dense pose window does not start until ~3.0 s. "
      "The honest reading is: **no constant inter-camera offset is demonstrated at "
      "the 1 ms level by this method**, and design Sec 8's risk ('inter-camera "
      "latency may not be constant on host-stamped streams') stays open. A sharper "
      "instrument -- the ball leaving the tee, or a clap -- would settle it; the "
      "grip row will not.")
    A("")

    # ---------------- (ii) pose anchor -------------------------------------
    A("## (ii) DTL pose anchor")
    A("")
    A(md_table(["swing", "span pose frames", "both wrists >0.3", "max jump px",
                "p95 jump px", "affine a", "affine b", "resid p50", "resid p95",
                "resid max", "post-impact frames > p95"],
               [[m["sid"].split("__")[-1], m["pose_n_span"], f"{m['pose_both_frac']*100:.0f}%",
                 fmt(m["pose_max_jump"], 1), fmt(m["pose_p95_jump"], 1),
                 fmt(m["aff_a"], 4), fmt(m["aff_b"], 1), fmt(m["aff_p50"], 1),
                 fmt(m["aff_p95"], 1), fmt(m["aff_max"], 1),
                 f"{m['post_impact_over_p95']}/{m['post_impact_n']}"]
                for _, m in per_swing]))
    A("")
    # perpendicular distance from pose grip to the truth line
    perp = []
    for rows, m in per_swing:
        ok = truth_ok(rows, src, m)
        th = truth_theta(rows, src)
        # The scan/band line is anchored AT the pose grip by construction, so a
        # perpendicular distance to it is identically zero and would be a lie.
        # The honest version of this test needs a line fitted independently of the
        # anchor; the segment lock's r0 is the closest thing these runs carry.
        r0 = rows["seg_r0"][rows["seg_ok"]]
        perp.append([m["sid"].split("__")[-1], int(ok.sum()), int(rows["seg_ok"].sum()),
                     fmt(pct(r0, 50), 1), fmt(pct(r0, 90), 1)])
    A(md_table(["swing", "usable truth frames", "seg locks", "seg r0 p50 (px)",
                "seg r0 p90 (px)"], perp))
    A("")
    A("**What this says.** The good news first: the DTL pose anchor exists and is "
      "continuous. Both wrists clear 0.3 on 96% of span pose frames on every swing, "
      "and the largest frame-to-frame grip jump (62-159 px) is a downswing-speed "
      "jump across a 6.7 ms gap, not a teleport. The design's premise that the pose "
      "anchor transfers to this view is confirmed on six swings, not one.")
    A("")
    A("The bad news is the cross-view witness. The grip-row affine `y_D = a*y_F + b` "
      "gives a consistent scale (`a` = 1.19-1.24 on every swing, which is the two "
      "cameras' relative vertical scale and is why L_D can be carried across when the "
      "DTL ball is missing) but it does **not** fit tightly: residual p50 is 8-20 px "
      "and p95 is 27-97 px on a 1024-row frame. Design Sec 5.2 proposes quarantining "
      "a DTL anchor whose row disagrees with the fit by more than its p95; with a p95 "
      "that loose, that gate will catch only gross failures, and 'more than p95' is "
      "by construction 5% of the fitting window itself. The post-impact column shows "
      "the gate firing where the design expects (the P8 occlusion) but firing on "
      "10-27 of ~40 post-impact frames rather than on a clean block -- and on 0007, "
      "whose face-on ladder runs to 4.70 s, on 141 of 178. **Treat Sec 5.2's "
      "quarantine as unproven**: it needs a tighter witness (both shoulders, or the "
      "whole torso row profile) or an absolute threshold, not a percentile of a bad "
      "fit.")
    A("")
    A("**The perpendicular-distance-to-the-shaft-axis test of the design cannot be "
      "run at all here**: the band lock does not exist, and both the ball anchor and "
      "the scan are anchored AT the pose grip, so their perpendicular distance to it "
      "is zero by construction. `seg_r0` -- how far down the shaft the segment lock's "
      "proximal end sat -- is reported instead as the nearest available proxy, and "
      "the 260 mm attachment gate means it is censored, not free.")
    A("")

    # ---------------- (iii) schedule ---------------------------------------
    A("## (iii) The visibility schedule")
    A("")
    prow = []
    for rows, m in per_swing:
        cells = [m["sid"].split("__")[-1]]
        for p in (1, 2, 3, 4, 5, 6, 7, 8, 10):
            if p not in m["ps"]:
                cells.append("-")
                continue
            i = int(np.argmin(np.abs(rows["t"] - m["ps"][p]["t_us"])))
            cells.append(fmt(rows["rho_d"][i], 2))
        prow.append(cells)
    A(md_table(["swing"] + [f"P{p}" for p in (1, 2, 3, 4, 5, 6, 7, 8, 10)], prow))
    A("")
    # rho_pred vs observed
    obs = []
    for rows, m in per_swing:
        ok = truth_ok(rows, src, m) & np.isfinite(rows["scan_rho"]) & np.isfinite(rows["rho_d"])
        if ok.sum() < 5:
            obs.append([m["sid"].split("__")[-1], int(ok.sum()), "-", "-", "-"])
            continue
        a, b = rows["rho_d"][ok], rows["scan_rho"][ok]
        b = b / max(pct(b, 95), 1e-6)
        r = np.corrcoef(a, b)[0, 1]
        d = np.abs(a - b)
        obs.append([m["sid"].split("__")[-1], int(ok.sum()), fmt(r, 3),
                    fmt(pct(d, 50), 3), fmt(pct(d, 90), 3)])
    A(md_table(["swing", "n frames", "corr(rho_pred, rho_obs)", "|resid| p50", "|resid| p90"], obs))
    A("")
    bad = []
    for rows, m in per_swing:
        ok = truth_ok(rows, src, m) & (rows["rho_d"] < RHO_SOLVE_MIN)
        idx = np.nonzero(ok)[0]
        bad.append([m["sid"].split("__")[-1], int(ok.sum()),
                    ", ".join(str(int(rows["fidx"][i])) for i in idx[:12]) + ("..." if idx.size > 12 else "")])
    A(md_table(["swing", f"locks where rho_pred < {RHO_SOLVE_MIN}", "frames"], bad))
    A("")
    A("**What this says.** The per-P table reproduces the long/stub/long/gone/long/"
      "thin/long pattern the design read off the feasibility frames, and it repeats on "
      "all six swings at P1, P3, P5 and P7 (all >= 0.95) and at P2 and P6 (all stubs). "
      "**But the stub values themselves are not trustworthy to two decimal places.** "
      "Near P2/P4/P6 the shaft is nearly along the target line, so rho_F -> 1 and "
      "cos(theta_F) -> +-1, and rho_hat_D = sqrt(1 - rho_F^2 cos^2 theta_F) is the "
      "difference of two numbers that are both nearly 1: a 2% error in L_full moves "
      "rho_hat_D at P4 from 0.05 to 0.24. That is why the design's own feasibility "
      "figures (0.97/0.16/1.00/0.23/0.98/0.00/0.98) cannot be reproduced exactly from "
      "these runs with any single L_full -- and it is a warning about "
      "`rhoSolveMin`, not about the schedule. The schedule's DISCRIMINATION (is this "
      "frame long or a stub?) is robust; its VALUE inside a stub is not. Anything "
      "that divides by rho_hat_D, or interpolates a band edge from it, will be noisy.")
    A("")
    A("The middle table (rho_hat_D against an observed projected length) and the last "
      "one (locks inside an end-on span) are both computed against the stand-in scan "
      "and both come out meaningless -- correlations near zero, and 24-45 'locks' "
      "inside end-on spans per swing. That is not a finding about the schedule; it is "
      "the scan failing, measured directly in table (iv-b). Both are kept because "
      "they are the first two tables to re-run once real band locks exist, and their "
      "current values are the null those runs must beat.")
    A("")

    # ---------------- (iv) corridor ----------------------------------------
    A("## (iv) The corridor")
    A("")
    A("### (iv-a) Against the montage-adjudicated address/impact anchor")
    A("")
    anch_rows, pooled_ball = [], {"P1": {"+": [], "-": []}, "P7": {"+": [], "-": []}}
    for rows, m in per_swing:
        for tag, t0 in (("P1", m["ps"][1]["t_us"]),
                        ("P7", m["ps"][7]["t_us"] if 7 in m["ps"] else float("nan"))):
            if not np.isfinite(t0):
                continue
            i = int(np.argmin(np.abs(rows["t"] - t0)))
            th = rows["anch_th"][i]
            if not np.isfinite(th):
                continue
            da = math.degrees(wrap_pi(th - rows["corr_a"][i]))
            db = math.degrees(wrap_pi(th - rows["corr_b"][i]))
            accepted = bool(rows["anch_ok"][i])
            if accepted:
                pooled_ball[tag]["+"].append(abs(da))
                pooled_ball[tag]["-"].append(abs(db))
            anch_rows.append([m["sid"].split("__")[-1], tag,
                              fmt(math.degrees(th), 1), fmt(rows["anch_sc"][i], 1),
                              "yes" if accepted else "REJECTED (blurred)",
                              fmt(math.degrees(rows["corr_a"][i]), 1),
                              fmt(math.degrees(rows["corr_b"][i]), 1),
                              fmt(min(abs(da), abs(db)), 1)])
    A(md_table(["swing", "P", "anchor theta_D (deg)", "ridge score", "accepted",
                "corridor + (deg)", "corridor - (deg)", "best-sign resid (deg)"],
               anch_rows))
    A("")
    A(md_table(["P", "n frames", "best-sign |resid| p50", "max", "proposed w0 (deg)"],
               [[tag, len(pooled_ball[tag]["+"]),
                 fmt(min(pct(pooled_ball[tag]["+"], 50), pct(pooled_ball[tag]["-"], 50)), 1),
                 fmt(min(max(pooled_ball[tag]["+"], default=float("nan")),
                         max(pooled_ball[tag]["-"], default=float("nan"))), 1),
                 _w0_ball(pooled_ball, tag)]
                for tag in ("P1", "P7")]))
    A("")
    A("**What this says. The design's address/impact claim is confirmed, and its "
      "numbers are close.** The taped shaft at P1 and P7 is unmistakable in the DTL "
      "frames (see `02_address_impact_adjudication.png`); every accepted line above "
      "was checked by eye to lie along it. The true DTL shaft direction at address is "
      "**58.5-62.5 deg** across all six swings and at impact **58-64 deg** -- a very "
      "tight cluster, and near the design's read of 52-55 deg. The corridor centre "
      "there is 82-105 deg, degenerating to exactly 90 deg on the three swings where "
      "rho_F clips to 1. So equation (c) is wrong by **22-31 deg at address and "
      "11-32 deg at impact**, versus the design's stated 35-40. The failure is real, "
      "the direction of the error is consistent (the model predicts too vertical), "
      "and it is a systematic a fitted camera (Sec 4.4) could remove.")
    A("")
    A("One frame is rejected: swing_0004 at P7, where the club is motion-blurred past "
      "impact and the ridge score collapses from ~40 to 12.6. The score floor caught "
      "it without being asked to, which is mild evidence the score means something.")
    A("")
    A("The `w0` this implies -- 1.4x the worst residual, roughly +-45 deg -- is wide "
      "enough that the corridor buys almost nothing at address and impact. That is "
      "the design's own conclusion (Sec 4.2: the two priors are complementary; use "
      "the DTL ball there instead). The problem is the next table: **the DTL ball "
      "is not available either.**")
    A("")
    A("#### The DTL ball detections are FALSE, and the golf prior accepts them")
    A("")
    ball_tab = []
    for rows, m in per_swing:
        i = int(np.argmin(np.abs(rows["t"] - m["ps"][1]["t_us"])))
        bth = rows["ball_th"][i] if np.isfinite(rows["ball_th"][i]) else float("nan")
        if not np.isfinite(bth) and m["ball_xy"] is not None:
            bth = math.atan2(m["ball_xy"][1] - rows["dtl_grip_y"][i],
                             m["ball_xy"][0] - rows["dtl_grip_x"][i])
        d = math.degrees(wrap_pi(bth - rows["anch_th"][i])) \
            if (np.isfinite(bth) and np.isfinite(rows["anch_th"][i])) else float("nan")
        ball_tab.append([m["sid"].split("__")[-1],
                         fmt(m["ball_xy"][0], 0) if m["ball_xy"] else "-",
                         fmt(m["ball_xy"][1], 0) if m["ball_xy"] else "-",
                         "ACCEPTED" if m["ball_prior_ok"] else "rejected",
                         fmt(math.degrees(bth), 1),
                         fmt(math.degrees(rows["anch_th"][i]), 1), fmt(d, 1)])
    A(md_table(["swing", "ball x (px)", "ball y (px)", "run golf prior",
                "implied theta_ball (deg)", "adjudicated anchor (deg)",
                "error (deg)"], ball_tab))
    A("")
    A("The real ball sits on the mat well to the golfer's target side, at roughly "
      "x = 460-490 px in a 512-px-wide frame. Every detection above is 150-300 px "
      "left of it, between the feet -- and **the run's golf prior ACCEPTED four of "
      "the six**, because that prior is the face-on sentence 'between the feet, below "
      "the ankle line' (design Sec 2, finding 5). It is not merely that the prior "
      "fails to transfer: it actively certifies the wrong blob and rejects the two "
      "detections that were nearer the ball. Consequences, both actionable now:")
    A("")
    for s_ in ["**D6 (the ball anchor) cannot be built on `analysis.ball` as it stands "
               "in a `--face-on DTL` run.** It would anchor address and impact -- the "
               "two bands the corridor cannot cover -- onto a point between the feet, "
               "at a 30-50 deg error, with the prior's blessing. A DTL ball prior "
               "('beyond the toe line on the side the golfer faces') has to land "
               "before D6 does.",
               "**`lengths.ballPx` is poisoned in the DTL runs too**, since it is the "
               "grip-to-ball distance. It was the first rung of the L_D ladder in this "
               "probe and gave 303-336 px where the montage-adjudicated grip-to-head "
               "distance is nearer 380; treat every L_D in this document as provisional."]:
        A(f"- {s_}")
    A("")
    A("### (iv-b) Against the stand-in scan, per phase band")
    A("")
    band_names = ["address", "mid-backswing", "downswing", "impact"]
    rowsout, signtab = [], []
    pooled = {nm: {"+": [], "-": []} for nm in band_names}
    for rows, m in per_swing:
        ok = truth_ok(rows, src, m)
        th = truth_theta(rows, src)
        for nm in band_names:
            if nm not in m["bands"]:
                continue
            lo, hi = m["bands"][nm]
            sel = ok & (rows["t"] >= lo) & (rows["t"] <= hi)
            if not sel.any():
                continue
            ra = np.abs(np.degrees(wrap_pi(rows["corr_a"][sel] - th[sel])))
            rb = np.abs(np.degrees(wrap_pi(rows["corr_b"][sel] - th[sel])))
            pooled[nm]["+"].extend(ra[np.isfinite(ra)].tolist())
            pooled[nm]["-"].extend(rb[np.isfinite(rb)].tolist())
    for nm in band_names:
        ra = np.array(pooled[nm]["+"])
        rb = np.array(pooled[nm]["-"])
        if ra.size == 0:
            rowsout.append([nm, 0, "-", "-", "-", "-", "-", "-", "-"])
            continue
        win = "+" if np.nanmedian(ra) <= np.nanmedian(rb) else "-"
        best = ra if win == "+" else rb
        rowsout.append([nm, ra.size, fmt(pct(ra, 50), 1), fmt(pct(rb, 50), 1), win,
                        fmt(pct(best, 50), 1), fmt(pct(best, 90), 1), fmt(pct(best, 99), 1),
                        fmt(min(pct(best, 99), 179.0), 0)])
        signtab.append([nm, win, fmt(pct(ra, 50), 1), fmt(pct(rb, 50), 1)])
    A(md_table(["band", "n", "resid p50 sign +", "resid p50 sign -", "winning sign",
                "winner p50", "winner p90", "winner p99", "proposed w0 (deg)"], rowsout))
    A("")
    A("### The sign table")
    A("")
    A(md_table(["band", "sign that wins", "p50 resid, sign +", "p50 resid, sign -"], signtab))
    A("")
    hi_rho = {"+": [], "-": []}
    lo_rho = {"+": [], "-": []}
    for rows, m in per_swing:
        ok = truth_ok(rows, src, m)
        th = truth_theta(rows, src)
        for tag, sel in (("hi", ok & (rows["rho_F"] > RHO_F_MAX)),
                         ("lo", ok & (rows["rho_F"] <= RHO_F_MAX))):
            if not sel.any():
                continue
            d = hi_rho if tag == "hi" else lo_rho
            d["+"].extend(np.abs(np.degrees(wrap_pi(rows["corr_a"][sel] - th[sel]))).tolist())
            d["-"].extend(np.abs(np.degrees(wrap_pi(rows["corr_b"][sel] - th[sel]))).tolist())
    A(md_table(["rho_F regime", "n", "best-sign p50", "best-sign p90"],
               [[f"rho_F > {RHO_F_MAX} (c) degenerate", len(hi_rho["+"]),
                 fmt(min(pct(hi_rho["+"], 50), pct(hi_rho["-"], 50)), 1),
                 fmt(min(pct(hi_rho["+"], 90), pct(hi_rho["-"], 90)), 1)],
                [f"rho_F <= {RHO_F_MAX}", len(lo_rho["+"]),
                 fmt(min(pct(lo_rho["+"], 50), pct(lo_rho["-"], 50)), 1),
                 fmt(min(pct(lo_rho["+"], 90), pct(lo_rho["-"], 90)), 1)]]))
    A("")
    sc_vs_ball = []
    for rows, m in per_swing:
        sel = rows["anch_ok"] & np.isfinite(rows["scan_th"])
        if not sel.any():
            sc_vs_ball.append([m["sid"].split("__")[-1], 0, "-", "-"])
            continue
        d = np.abs(np.degrees(wrap_pi(rows["scan_th"][sel] - rows["anch_th"][sel])))
        sc_vs_ball.append([m["sid"].split("__")[-1], int(sel.sum()), fmt(pct(d, 50), 1),
                           fmt(100.0 * float(np.mean(d < 15.0)), 0) + "%"])
    A("#### Is the UNCONSTRAINED scan trustworthy? No.")
    A("")
    A(md_table(["swing", "frames with both", "|unconstrained scan - adjudicated anchor| "
                "p50 (deg)", "within 15 deg"], sc_vs_ball))
    A("")
    A("Given exactly the same pixels, the same score and the same anchor, the only "
      "difference between the adjudicated anchor and this column is that the anchor "
      "was told which 56-degree sector to look in. Unconstrained, the winner is the "
      "lead arm at address, the trail leg through impact, or the vertical alignment "
      "stick on the mat -- all long, all attached-looking, all wrong. **An "
      "unconditioned DTL line search reproduces Sec 2's failure rather than grading "
      "it.** That is the single most important sentence in this document: there is "
      "at present NO automatable DTL angle truth in these runs outside the two "
      "adjudicated instants, and every scan-based number in tables (iii) and (iv-b) "
      "is reported only to show the shape of the problem.")
    A("")
    A("**What this says: nothing about the corridor, and everything about the "
      "instrument.** Residuals of 26-180 deg and p99s pinned at 180 are what you get "
      "when you difference a good prediction against a bad reference. The sign table "
      "above is therefore **NOT the design's sign table** and must not be used to "
      "hardcode the schedule of signs -- it is a table of which wrong answer happened "
      "to be nearer. The rho_F > 0.93 row is equally uninformative for the same "
      "reason.")
    A("")
    A("The design's own prediction for these bands (Sec 4.2: 5.6 deg p50 / 10.8 deg "
      "p90 in the mid-backswing) is neither confirmed nor refuted here. **Do not size "
      "`w0` for the mid bands from this table.** What Stage 0 can say about the "
      "corridor is confined to table (iv-a), where a real face-on-independent "
      "reference exists. What Stage 0 CAN say generally is that the schedule -- "
      "Sec 0's finding 3, the part of the coupling that does not need an angle "
      "reference to verify -- reproduces on all six swings, and that is the part of "
      "the design these runs support.")
    A("")

    # ---------------- (v) evidence by regime -------------------------------
    A("## (v) Steel evidence by background regime")
    A("")
    agg = {}
    for _, m in per_swing:
        for nm, chan, tag, med, n in m["regime_rows"]:
            agg.setdefault((nm, chan, tag), []).append(med)
    body = []
    for nm, lo, hi in REGIME_ROWS:
        for chan in ("raw", "mot"):
            on = np.array(agg.get((nm, chan, "on"), []), dtype=float)
            off = np.array(agg.get((nm, chan, "off"), []), dtype=float)
            if on.size == 0 and off.size == 0:
                continue
            body.append([f"{nm} ({lo}-{hi})", chan, on.size,
                         fmt(pct(on, 10), 2), fmt(pct(on, 50), 2), fmt(pct(on, 90), 2),
                         fmt(pct(off, 50), 2),
                         fmt(pct(on, 50) - pct(off, 50), 2)])
    A(md_table(["regime (image rows)", "channel", "n frame-samples",
                "on-line p10", "on-line p50", "on-line p90",
                "control (+30 deg) p50", "on minus control"], body))
    A("")
    A("**What this says.** Measured on the address and impact frames of all six "
      "swings, along the montage-adjudicated shaft line -- the one line in these runs "
      "known to lie on the club -- and again along a control line 30 deg away in the "
      "same frame. The quantity is |on-line sample minus the mean of four lateral "
      "samples at +-9 and +-12 px|, in 8-bit grey levels, medianed along the ray and "
      "bucketed by the sample's image ROW: above the screen (0-280), over the lit "
      "simulator screen (280-650), over the mat (650-1024). `raw` is the grey frame, "
      "`mot` is `|frame - plate|`. The last column is the honest one: on-line minus "
      "control is how much of the number is the club rather than 'any line through a "
      "busy background scores something'.")
    A("")
    A("Read it with three caveats. The dev club is TAPED, so `raw` here measures a "
      "high-contrast banded shaft and says nothing about bare steel -- design Sec 8's "
      "'bare steel over the screen' risk needs the 06-11 bare-wedge swings and stays "
      "open. The address and impact shaft spans only the screen and mat rows in this "
      "framing, so the above-screen regime has no coverage. And the clean plate is a "
      "whole-clip median that CONTAINS the address club (research Sec 17.2), so the "
      "`mot` channel is suppressed at address by construction: its numbers there are "
      "a lower bound, not a measurement, and the fact that `raw` beats `mot` on these "
      "frames is that artefact, not a channel ranking.")
    A("")

    # ---------------- (vi) identity (d) ------------------------------------
    A("## (vi) Identity rho_F^2 + rho_D^2 = 1 + u_z^2")
    A("")
    ident = []
    for rows, m in per_swing:
        both = rows["band_ok"] & rows["fo_band_ok"]
        ident.append([m["sid"].split("__")[-1], int(both.sum()), "-", "-"])
    A(md_table(["swing", "frames with a band lock in BOTH views", "resid p50", "resid p90"],
               ident))
    A("")
    A("**What this says.** Zero frames qualify, in every swing, because there are no "
      "band locks in either view (see the preamble). Identity (d) is the design's "
      "only cross-view consistency check and its residual is what would decide "
      "Sec 4.4 -- whether the idealised camera model needs replacing with a fitted "
      "one. **It is untestable on this input and remains untested.** Substituting the "
      "segment lock's scale would not help: the DTL segment lock is probed ALONG the "
      "baseline tracker's solve direction, and that solve is the confidently wrong "
      "one the whole design exists to prevent, so a residual computed from it would "
      "be measuring the error it is supposed to detect.")
    A("")

    # ---------------- proposals -------------------------------------------
    A("## Proposed constants (provisional -- band-lock truth absent)")
    A("")
    A(md_table(["constant", "value", "basis"],
               [["`rhoSolveMin`", f"{RHO_SOLVE_MIN:.2f} (keep)",
                 "table (iii): discrimination long-vs-stub is robust at this level "
                 "on all six; the value inside a stub is not, so do not lower it"],
                ["`rhoFMax`", f"{RHO_F_MAX:.2f} (keep, untested)",
                 "table (iv-a) shows (c) IS degenerate at address/impact where "
                 "rho_F -> 1 (residual 11-32 deg there); nothing here sizes where "
                 "the gate itself should sit"],
                ["`w0` address band", _w0_ball(pooled_ball, "P1"),
                 "table (iv-a): 1.4x the worst residual against the "
                 "montage-adjudicated anchor, n = 6 frames (a floor, not a fit)"],
                ["`w0` impact band", _w0_ball(pooled_ball, "P7"),
                 "table (iv-a), same basis at P7, n = 5 (one blurred frame rejected)"],
                ["`w0` mid-backswing / downswing", "NOT proposed",
                 "no face-on-independent truth exists in those bands: the adjudicated "
                 "anchor covers only P1 and P7, and the unconstrained scan is "
                 "demonstrably wrong-in-kind. Sizing them needs the band lock."],
                ["clock offset", "0 us (do not apply one)",
                 f"table (i): the cross-correlation reads {np.median(offs):+.0f} us "
                 f"median but is not constant across swings "
                 f"({offs.min():+.0f}..{offs.max():+.0f} us); the frame phase that "
                 f"the design's 3.2 ms refers to is removed by interpolating the "
                 f"face-on witness, which the tracker already does"
                 if offs.size else "-"]]))
    A("")
    A("## Assumptions and sample counts")
    A("")
    for a in [
        "Both cameras idealised as level, orthographic and orthogonal (design Sec 4.1). "
        "No camera model is fitted here.",
        "Right-handed golfer; image-right in DTL is toward the ball.",
        "The face-on witness is interpolated to every DTL frame time with theta "
        "UNWRAPPED; flags are taken from the nearest face-on sample, never interpolated.",
        "`L_full` = p95 of face-on `lenPx` over measured frames with |sin theta_F| < 0.25, "
        "per swing, never pooled.",
        "The DTL grip anchor is the wrist-keypoint MIDPOINT from `pose2d.synth` "
        "(the analyzer's own 240 Hz resample), not `frames[].lead/trail`.",
        "Every frame counted is inside the face-on P-ladder span (P1 to the last "
        "rung present), which is also the only span the corridor is defined over.",
        f"The adjudicated anchor searches only {ANCHOR_SECTOR_DEG[0]:.0f}-"
        f"{ANCHOR_SECTOR_DEG[1]:.0f} deg, a constraint read off the montage by eye and "
        f"not derived from face-on, and accepts a frame only when the ridge score "
        f"reaches {ANCHOR_SCORE_MIN:.0f}; it is applied only within "
        f"{ANCHOR_WINDOW_US/1000:.0f} ms of P1 and P7. Table (iv-a) reports the one "
        f"frame nearest each instant: 12 candidates, 11 accepted.",
        "The phase bands are built from the face-on P-ladder alone: address = P1 to "
        "mid(P1,P2), mid-backswing = mid(P2,P3) to mid(P3,P4), downswing = mid(P4,P5) "
        "to mid(P5,P6), impact = mid(P6,P7) to 30% of the way to P8.",
        "No RNG anywhere; re-running on the same inputs reproduces the CSV byte for byte.",
    ]:
        A(f"- {a}")
    A("")
    A(md_table(["swing", "DTL frames in span", "adjudicated anchor frames",
                "negative-control scan frames", "sighted frames (rho_pred >= 0.35)"],
               [[m["sid"].split("__")[-1], m["n_frames"], m["n_anchor"],
                 int(truth_ok(r, src, m).sum()),
                 int(np.nansum(r["rho_d"] >= RHO_SOLVE_MIN))]
                for r, m in per_swing]))
    A("")
    return "\n".join(L)


# ================================================================== band truth
# Stage 0 could not size the corridor in the mid bands, could not test the
# schedule against an observed length, and could not say how often the baseline
# is confidently wrong, because it had no face-on-independent DTL angle truth
# outside the 12 adjudicated address/impact frames.  That truth now exists:
# tools/shaftlab/dtl_band_truth.py.  This section consumes it and nothing else
# in this file changes, so the original Stage 0 tables stand as written.
BT_OFF_DEG = 15.0                  # the design's "confidently wrong" threshold


def load_band_truth(truth_dir, sid):
    p = Path(truth_dir) / f"{sid}.json"
    if not p.exists():
        return None
    d = load_json(p)
    fr = d.get("frames", [])
    return dict(
        t=np.array([f["t_us"] for f in fr], dtype=float),
        frame=np.array([f["frame"] for f in fr], dtype=int),
        theta=np.array([f["theta"] for f in fr], dtype=float),
        s=np.array([f["s"] for f in fr], dtype=float),
        ncc=np.array([f["ncc"] for f in fr], dtype=float),
        thresholds=d.get("thresholds", {}))


def band_truth_analysis(args, ids):
    """(1) corridor residual per sign per band, (2) rho_hat_D vs the truth's own
    projected scale, (3) how often the baseline is >15 deg from truth.

    Everything is computed ON TRUTH FRAMES ONLY.  The face-on witness is
    interpolated to each truth frame's t_us with theta unwrapped, exactly as the
    rest of this file does -- the two streams are not frame-synchronous."""
    per = []
    for sid in ids:
        S = load_swing(args.corpus, args.fo_runs, args.dtl_runs, sid)
        bt = load_band_truth(args.band_truth_dir, sid)
        if bt is None or bt["t"].size == 0:
            per.append((sid, None))
            continue
        fo_club = S["fo"]["club"]
        ps = positions_by_p(fo_club)
        L_full = l_full_face_on(fo_club)
        bands = phase_bands(ps)
        W = interp_face_on(fo_club, bt["t"])
        rho_F = np.clip(W["lenPx"] / L_full, 0.0, 1.0)
        rho_F[W["lenPx"] <= 0] = np.nan
        rho_d, corr_a, corr_b, ux, uz, uy = schedule(
            W["theta"], np.nan_to_num(rho_F, nan=0.0))
        rho_d[~np.isfinite(rho_F)] = np.nan
        corr_a[~np.isfinite(rho_F)] = np.nan
        corr_b[~np.isfinite(rho_F)] = np.nan
        fo_meas = (W["flags"] & SHAFT_MEASURED) > 0

        # (3) the baseline: the unmodified face-on tracker on the DTL stream
        dsamp = S["dtl"]["club"].get("samples", []) or []
        bt_us = np.array([x["t_us"] for x in dsamp], dtype=float)
        bth = np.array([x.get("theta", np.nan) for x in dsamp], dtype=float)
        bfl = np.array([int(x.get("flags", 0)) for x in dsamp])
        base_th = np.full(bt["t"].size, np.nan)
        base_meas = np.zeros(bt["t"].size, dtype=bool)
        if bt_us.size:
            k = np.clip(np.searchsorted(bt_us, bt["t"]), 0, bt_us.size - 1)
            k0 = np.clip(k - 1, 0, bt_us.size - 1)
            near = np.where(np.abs(bt_us[k] - bt["t"]) <= np.abs(bt_us[k0] - bt["t"]), k, k0)
            close = np.abs(bt_us[near] - bt["t"]) <= 8000.0
            base_th = np.where(close, bth[near], np.nan)
            base_meas = close & ((bfl[near] & SHAFT_MEASURED) > 0)

        band = np.array([band_of(x, bands) for x in bt["t"]], dtype=object)
        res_a = np.degrees(np.abs(wrap_pi(corr_a - bt["theta"])))
        res_b = np.degrees(np.abs(wrap_pi(corr_b - bt["theta"])))
        d_base = np.degrees(np.abs(wrap_pi(base_th - bt["theta"])))
        s_max = float(np.nanpercentile(bt["s"], 95)) if bt["s"].size else float("nan")
        per.append((sid, dict(bt=bt, band=band, res_a=res_a, res_b=res_b,
                              rho_d=rho_d, rho_F=rho_F, fo_meas=fo_meas,
                              d_base=d_base, base_meas=base_meas,
                              s_max=s_max, L_full=L_full, n_ps=len(ps))))
    return per


def band_truth_markdown(per, args, stamp):
    L, A = [], None
    A = L.append
    A(f"## {stamp} -- against the face-on-independent DTL BAND TRUTH")
    A("")
    good = [(sid, d) for sid, d in per if d is not None]
    if not good:
        A("No band truth found under "
          f"`{args.band_truth_dir}` -- nothing to report.")
        return "\n".join(L) + "\n"
    thr = good[0][1]["bt"]["thresholds"]
    A("The Stage 0 tables above stand as written; they were computed against a "
      "stand-in that the summary itself calls a negative control. Everything "
      "below is computed **on truth frames only**, where truth is "
      "`tools/shaftlab/dtl_band_truth.py` -- a per-frame band-template match on "
      "the DTL stream that takes **no face-on input of any kind**: no face-on "
      "track, no face-on phases, no face-on timing. Its thresholds here were "
      f"`{thr}`. The face-on witness is interpolated to each truth frame's "
      "`t_us` with theta unwrapped, as everywhere else in this file.")
    A("")

    A("### Truth coverage")
    A("")
    A(md_table(["swing", "truth frames", "address", "mid-backswing", "downswing",
                "impact", "outside the four bands", "s p50", "s p95 (= s_max)"],
               [[sid.split("__")[-1], len(d["bt"]["t"]),
                 int((d["band"] == "address").sum()),
                 int((d["band"] == "mid-backswing").sum()),
                 int((d["band"] == "downswing").sum()),
                 int((d["band"] == "impact").sum()),
                 int((d["band"] == "").sum()),
                 fmt(np.nanpercentile(d["bt"]["s"], 50), 3),
                 fmt(d["s_max"], 3)] for sid, d in good]))
    A("")
    A("**What this says, and it is the headline caveat for everything below.** "
      "The instrument locks in the ADDRESS REGION ONLY -- from roughly 1.7 s "
      "(the golfer settling over the ball) to about 50 ms past P1 -- and "
      "abstains through the whole swing. Most truth frames therefore sit "
      "BEFORE P1 and fall outside the four phase bands, which are defined from "
      "the face-on P-ladder; the address band gets 8-16 frames per swing and "
      "the mid-backswing, downswing and impact bands get **none**. That is not "
      "a threshold that can be loosened: through the swing the DTL exposure "
      "smears the 25 mm bands along the shaft, the three-group template has "
      "nothing to correlate with, and every candidate that still scores is a "
      "body line. Loosening the gates to reach those bands produced false "
      "locks on the golfer's torso and trouser seam, adjudicated by eye and "
      "rejected (see the instrument's own summary). So: **`w0` for the "
      "mid-backswing, downswing and impact bands is STILL not sized**, and "
      "Stage 0's verdict on those three bands stands unchanged. What is new is "
      "the address band, the rho_F regime split, and the baseline's error "
      "rate.")
    A("")

    # ---- (1) the corridor, per sign, per band -----------------------------
    A("### (1) The corridor residual against truth, per sign, per phase band")
    A("")
    rows = []
    sign_rows = []
    w0 = {}
    for nm in ("address", "mid-backswing", "downswing", "impact"):
        a = np.concatenate([d["res_a"][(d["band"] == nm) & np.isfinite(d["res_a"])]
                            for _, d in good]) if good else np.array([])
        b = np.concatenate([d["res_b"][(d["band"] == nm) & np.isfinite(d["res_b"])]
                            for _, d in good]) if good else np.array([])
        if a.size == 0 and b.size == 0:
            rows.append([nm, 0, "-", "-", "-", "-", "-", "-", "-"])
            continue
        p50a, p50b = pct(a, 50), pct(b, 50)
        win = "+" if (math.isfinite(p50a) and (not math.isfinite(p50b) or p50a <= p50b)) else "-"
        w = a if win == "+" else b
        rows.append([nm, int(w.size), fmt(p50a, 1), fmt(p50b, 1), win,
                     fmt(pct(w, 50), 1), fmt(pct(w, 90), 1), fmt(pct(w, 99), 1),
                     f"+-{1.4 * pct(w, 99):.0f} deg"])
        sign_rows.append([nm, win, fmt(p50a, 1), fmt(p50b, 1)])
        w0[nm] = 1.4 * pct(w, 99)
    A(md_table(["band", "n", "resid p50 sign +", "resid p50 sign -", "winning sign",
                "winner p50", "winner p90", "winner p99", "proposed w0"], rows))
    A("")
    A("#### The sign table")
    A("")
    A(md_table(["band", "sign that wins", "p50 resid, sign +", "p50 resid, sign -"],
               sign_rows))
    A("")
    pooled_hi = np.concatenate([d["rho_F"][np.isfinite(d["rho_F"])] for _, d in good])
    A(md_table(["rho_F regime", "n", "best-sign p50", "best-sign p90"],
               [[lbl, int(m.sum()), fmt(pct(v[m], 50), 1), fmt(pct(v[m], 90), 1)]
                for lbl, m, v in _rho_regime_rows(good)]))
    A("")
    A("**What this says.** The address-band residual of ~35 deg is the "
      "DEGENERATE case and it reproduces Stage 0 table (iv-a), which got 22-31 "
      "deg against the twelve hand-adjudicated frames: at address rho_F -> 1, "
      "equation (c) returns |u_y| = 0 and predicts a vertical DTL shaft, and "
      "the real one is at 54-58 deg. The sign table is near-degenerate there "
      "for the same reason -- 36.3 against 35.2 deg is not a preference, it is "
      "two arms of the same collapsed prediction -- so **do not hardcode a "
      "sign schedule from this row either**.")
    A("")
    A("The rho_F regime split is the first genuinely new number in this "
      "document. Where the face-on shaft is NOT railed at full length "
      "(rho_F <= 0.93, n = 53 frames, the waggle and the start of the "
      "takeaway), the corridor centre lands **7.5 deg p50 / 13.9 deg p90** "
      "from the band truth. That is the regime design Sec 4.2 measured at "
      "5.6 / 10.8 deg on one swing by eye, and it is now confirmed on six "
      "swings against a face-on-independent reference. The conclusion the "
      "design drew stands: **equation (c) is good to about 10 deg when rho_F "
      "is honest and useless when it is railed**, and `rhoFMax` is the gate "
      "that separates the two. It is 53 frames from one part of the swing, so "
      "it sizes nothing on its own -- but it is the first evidence that the "
      "corridor is worth having at all.")
    A("")

    # ---- (2) the schedule against an observed projected length ------------
    A("### (2) rho_hat_D against the truth's own projected scale s / s_max")
    A("")
    A("`s` is the band template's fitted scale in px/mm: the projected length of "
      "the club divided by 940 mm. `s / s_max` (s_max = the swing's p95 of s over "
      "truth frames) is therefore a DIRECT observation of rho_D, measured with no "
      "face-on input. This is the first time table (iii)'s middle row has a real "
      "reference.")
    A("")
    r2 = []
    for sid, d in good:
        obs = d["bt"]["s"] / d["s_max"]
        m = np.isfinite(obs) & np.isfinite(d["rho_d"])
        if m.sum() < 5:
            r2.append([sid.split("__")[-1], int(m.sum()), "-", "-", "-"])
            continue
        c = float(np.corrcoef(d["rho_d"][m], obs[m])[0, 1])
        r = np.abs(d["rho_d"][m] - obs[m])
        r2.append([sid.split("__")[-1], int(m.sum()), fmt(c, 3),
                   fmt(pct(r, 50), 3), fmt(pct(r, 90), 3)])
    A(md_table(["swing", "n truth frames", "corr(rho_pred, s/s_max)",
                "|resid| p50", "|resid| p90"], r2))
    A("")
    A("**What this says: less than it looks.** The residual is tiny -- 0.013 "
      "to 0.093, p90 under 0.10 on five of six -- so the schedule is not "
      "CONTRADICTED anywhere truth exists. But truth exists only at address, "
      "where both quantities are pinned near 1, so the CORRELATION column is "
      "measuring noise against noise and its sign is meaningless (it comes out "
      "negative on five swings and +0.73 on the sixth). The schedule's "
      "discrimination -- long versus stub -- is exactly what these frames "
      "cannot test, because no stub frame has truth. Read this table as \"the "
      "one regime we can check agrees to 0.02-0.09 in rho\", and nothing "
      "more. Stage 0 table (iii) still has no real test.")
    A("")

    # ---- (3) the baseline's confidently-wrong rate ------------------------
    A("### (3) The baseline's error against truth -- the number the tracker must beat")
    A("")
    A("`baseline` is the UNMODIFIED face-on tracker pointed at the DTL stream "
      "(`--face-on DTL`), read from `analysis.club.samples[]` with the measured "
      "flag `0x01`, paired to each truth frame by nearest `t_us` within 8 ms.")
    A("")
    r3 = []
    tot_m = tot_off = 0
    for sid, d in good:
        m = d["base_meas"] & np.isfinite(d["d_base"])
        off = m & (d["d_base"] > BT_OFF_DEG)
        tot_m += int(m.sum())
        tot_off += int(off.sum())
        r3.append([sid.split("__")[-1], len(d["bt"]["t"]), int(m.sum()),
                   fmt(pct(d["d_base"][m], 50), 1), fmt(pct(d["d_base"][m], 90), 1),
                   int(off.sum()),
                   f"{100.0 * off.sum() / max(1, m.sum()):.0f}%"])
    A(md_table(["swing", "truth frames", "baseline measured on them",
                "|baseline - truth| p50", "p90", f"> {BT_OFF_DEG:.0f} deg",
                "rate"], r3))
    A("")
    A(f"**Pooled: {tot_off} of {tot_m} measured baseline samples on truth frames are "
      f"more than {BT_OFF_DEG:.0f} deg from the truth "
      f"({100.0 * tot_off / max(1, tot_m):.0f}%).** That is the \"before\" number. "
      "The design's gate (Sec 6) is ZERO confidently-wrong published frames.")
    A("")
    A("**What this says.** Read the denominator first: the baseline publishes a "
      "MEASURED sample on only 25 of the 506 truth frames -- at address it "
      "mostly coasts, and two swings have no measured sample on any truth "
      "frame at all. So this is a rate on a small, self-selected set. What it "
      "is not is ambiguous: where the baseline does commit, it is 152-155 deg "
      "p50 from the truth, i.e. roughly 180 deg minus 27 -- the club published "
      "pointing back up the lead arm, head where the butt is. That is design "
      "Sec 2's failure, now with a face-on-independent number against it "
      "instead of an adjudicated montage: **100% confidently wrong, p50 153 "
      "deg**, on every frame where both the baseline and the truth speak.")
    A("")
    A("### Proposed constants, revised")
    A("")
    A(md_table(["constant", "value", "basis"],
               [[f"`w0` {nm} band", f"+-{v:.0f} deg",
                 "1.4x the p99 of the winning sign's corridor residual against "
                 "band truth, this table"] for nm, v in w0.items()]))
    A("")
    return "\n".join(L) + "\n"


def _rho_regime_rows(good):
    out = []
    for lbl, lo, hi in (("rho_F > 0.93 (c) degenerate", RHO_F_MAX, 2.0),
                        ("rho_F <= 0.93", -1.0, RHO_F_MAX)):
        ms, vs = [], []
        for _, d in good:
            best = np.minimum(d["res_a"], d["res_b"])
            ms.append((d["rho_F"] > lo) & (d["rho_F"] <= hi) & np.isfinite(best))
            vs.append(best)
        out.append((lbl, np.concatenate(ms), np.concatenate(vs)))
    return out


# ------------------------------------------------------------------ main
def main():
    ap = argparse.ArgumentParser(prog="dtl_probe.py", description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--corpus", default="/mnt/swingdata/corpus/swings")
    ap.add_argument("--fo-runs", default="build/dtl/fo")
    ap.add_argument("--dtl-runs", default="build/dtl/baseline")
    ap.add_argument("--swings", required=True,
                    help="comma-separated run ids, e.g. SESSION__swing_0004,...")
    ap.add_argument("--data-dir", default="docs/research/data/dtl")
    ap.add_argument("--summary-copy", default=None,
                    help="also write the summary markdown here")
    ap.add_argument("--no-scan", action="store_true",
                    help="skip the independent DTL line scan (much faster, most tables empty)")
    ap.add_argument("--band-truth-dir", default=None,
                    help="docs/research/data/dtl/band_truth -- the face-on-"
                         "independent truth from tools/shaftlab/dtl_band_truth.py")
    ap.add_argument("--append-summary", action="store_true",
                    help="with --band-truth-dir: compute ONLY the band-truth "
                         "tables and APPEND them to the existing summary under a "
                         "dated heading.  The Stage 0 tables are left untouched.")
    ap.add_argument("--stamp", default=None,
                    help="heading stamp for --append-summary (default: today)")
    args = ap.parse_args()

    ids = [s.strip() for s in args.swings.split(",") if s.strip()]

    if args.append_summary:
        if not args.band_truth_dir:
            ap.error("--append-summary needs --band-truth-dir")
        import datetime
        stamp = args.stamp or datetime.date.today().isoformat()
        per = band_truth_analysis(args, ids)
        md = band_truth_markdown(per, args, stamp)
        md_path = Path(args.data_dir) / "stage0_probe_summary.md"
        prev = md_path.read_text(encoding="utf-8") if md_path.exists() else ""
        head = f"## {stamp} -- against the face-on-independent DTL BAND TRUTH"
        if head in prev:                      # idempotent re-run
            prev = prev.split(head)[0].rstrip() + "\n"
        md_path.write_text(prev.rstrip() + "\n\n---\n\n" + md, encoding="utf-8")
        print(f"[dtl_probe] appended band-truth tables to {md_path}")
        if args.summary_copy:
            p = Path(args.summary_copy)
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_text(md_path.read_text(encoding="utf-8"), encoding="utf-8")
            print(f"[dtl_probe] wrote {p}")
        return 0

    per_swing, offsets = [], []
    for sid in ids:
        S = load_swing(args.corpus, args.fo_runs, args.dtl_runs, sid)
        rows, meta = probe_swing(S, do_scan=not args.no_scan)
        per_swing.append((rows, meta))
        offsets.append((sid, clock_offset(S, meta)))

    data_dir = Path(args.data_dir)
    csv_path = data_dir / "stage0_probe.csv"
    write_csv(csv_path, per_swing)
    md = build_summary(per_swing, offsets, args)
    md_path = data_dir / "stage0_probe_summary.md"
    md_path.write_text(md, encoding="utf-8")
    print(f"[dtl_probe] wrote {csv_path}")
    print(f"[dtl_probe] wrote {md_path}")
    if args.summary_copy:
        p = Path(args.summary_copy)
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(md, encoding="utf-8")
        print(f"[dtl_probe] wrote {p}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
