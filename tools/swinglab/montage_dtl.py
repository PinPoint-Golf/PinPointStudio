#!/usr/bin/env python3
"""montage_dtl.py -- down-the-line adjudication montages for the DTL shaft tracker
(docs/design/dtl_shaft_tracker_design.md Sec 6: "adjudicated by montage at full
resolution before it is trusted").

ONE ROW PER SWING, one column per face-on P-ladder position P1..P8,P10.  Each tile
is the DTL frame nearest that P's t_us, cropped to a FIXED golfer box for the whole
swing (derived from the DTL pose extent + margin), so the end-on tiles still show
the golfer instead of zooming onto a stub.  Drawn on each tile:

  published line  a PAIR OF RAILS offset +-6 rendered px either side of the
                  grip->head segment, in the tier colour (BAND red, SEG magenta,
                  RAY amber; baseline: measured amber, coasted grey) with a dark
                  keyline, a filled dot at the grip and an open circle at the
                  head.  The rails never cover the shaft, so the reviewer can
                  see the club BETWEEN them.
  thin WHITE      the DTL band "truth" -- a single centre line with perpendicular
                  end ticks, so "truth runs between the rails" reads at a glance.
                  From --truth-dir (tools/shaftlab/dtl_band_truth.py) when given,
                  else the run's own E1 band lock.  Drawn in ALL THREE modes.
  dashed CYAN     the face-on corridor centre(s), drawn from the grip at 0.6 of
                  the predicted projected length rho_hat_D * L_D.  In track mode
                  this is drawn ONLY where the frame's own corridor is on: a
                  corridor the tracker gated off is not a prediction it made,
                  and drawn anyway it reads as the tracker's line missing the
                  shaft.
  tier stamp      top-left, in the tier colour, on EVERY tile (RAY/BAND/SEG as
                  well as END-ON/OCCLUDED/UNSEEN); on an absence tile the
                  frame's `reason` is printed under the stamp at the caption's
                  size, wrapped to two lines -- on those tiles the reason is the
                  whole story of the tile.
  inset           the face-on frame at the same instant, ~25% of tile width
  caption         "P3 rho=1.00 RAY  th=243deg len=326  D=+1.2deg", the delta
                  graded green <=5 deg, amber <=15 deg, red above.
  TIMELINE        a strip under each row: x = time over the whole DTL span, one
                  column per DTL frame -- row 1 tier colour, row 2 rho_hat_D as
                  grey, row 3 truth coverage (white) and published-but->15-deg
                  (red), with white ticks + labels at the face-on P instants.
                  This is where coverage BETWEEN the P rungs is reviewed.

Three modes, one CLI:
  --mode baseline  source = <dtl-runs>/<id>/result.json analysis.club.samples[]
                   -- the UNMODIFIED face-on tracker pointed at the DTL stream.
                   Colour: measured (flags & 1) amber, coasted grey.
  --mode probe     no tracker at all: white band truth, cyan corridor, rho_hat_D
                   caption, END-ON stamped where rho_hat_D < 0.35.  This is the
                   Stage 0 picture.
  --mode track     source = <dtl-runs>/<id>/club_dtl.json, the future C++ tracker
                   output (schema pinpoint.clubDtl/1, see CLUB_DTL_SCHEMA below).

Outputs: <out-prefix>.png (all swings, width capped ~3000 px) and, per swing,
<out-prefix>_<swing>_fullres.png with the tiles at native crop resolution.

  montage_dtl.py --mode probe --corpus /mnt/swingdata/corpus/swings \
      --fo-runs build/dtl/fo --dtl-runs build/dtl/baseline \
      --swings id1,id2 --out-prefix ~/Desktop/DTL-shaft-tracker/01_stage0_probe_dev6
  montage_dtl.py --selftest <tmpdir>

Geometry is design Sec 4.1 and is duplicated from tools/shaftlab/dtl_probe.py on
purpose: the two tools are allowed to disagree only by being read side by side.
"""
import argparse
import json
import math
import shutil
import sys
from pathlib import Path

import numpy as np
import cv2

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
sys.stderr.reconfigure(encoding="utf-8", errors="replace")

sys.path.insert(0, str(Path(__file__).parent))
import montage_positions as mp   # noqa: E402  (same dir; resize_smart/draw_line_alpha/...)
from swinglab import Swing, RunResult  # noqa: E402
from pp_swingdoc import has_swing  # noqa: E402  (tools/, put on sys.path by swinglab)

# ---------------------------------------------------------------- constants
TILE = 430                      # tile HEIGHT in the capped montage, px (the tile
                                # width follows the crop's aspect -- the crop is
                                # full frame width, so tiles are portrait)
GRID_MAX_W = 3000               # total montage width cap
P_ORDER = (1, 2, 3, 4, 5, 6, 7, 8, 10)
CROP_MARGIN_FRAC = 0.22         # golfer-box margin as a fraction of the box side
CLUB_REACH = 1.15               # rows kept either side of the grip, in units of L_D
BALL_ROW_FRAC = 0.80            # the ball sits on the mat, ~80% down the DTL frame
RHO_SOLVE_MIN = 0.35            # design Sec 5.7 rhoSolveMin
BAND_MATCH_FRAMES = 1           # white truth line drawn when within +-1 DTL frame
SHAFT_MEASURED = 0x01
INSET_FRAC = 0.25

COL_BAND    = (60, 60, 235)     # BGR red      -- tier BAND
COL_SEG     = (235, 60, 235)    # BGR magenta  -- tier SEG
COL_RAY     = (0, 165, 245)     # BGR amber    -- tier RAY (and baseline "measured")
COL_COAST   = (150, 150, 150)   # grey         -- baseline "coasted"
COL_TRUTH   = (255, 255, 255)   # white        -- DTL band-lock truth
COL_CORR    = (235, 235, 0)     # cyan         -- face-on corridor centre
COL_GRIP    = (0, 235, 255)     # yellow       -- grip anchor
COL_STAMP   = (170, 170, 255)
COL_REASON  = (225, 225, 225)   # near-white   -- the refusal reason under it
COL_MISSING = (40, 40, 40)
COL_OUTLINE = (16, 16, 16)      # near-black    -- the 1 px keyline under every
                                # rail, so a rail reads on a white shaft AND on
                                # the dark mat
COL_ENDON   = (170, 140, 105)   # blue-grey     -- END_ON
COL_OCCL    = (165, 55, 135)    # purple        -- OCCLUDED
COL_UNSEEN  = (70, 70, 70)      # dark grey     -- UNSEEN
COL_SIGHTED = (120, 205, 120)   # green         -- probe-mode "sighted"
COL_OK      = (90, 220, 110)    # green  -- |dtheta| <= 5 deg
COL_WARN    = (0, 190, 245)     # amber  -- |dtheta| <= 15 deg
COL_BAD     = (60, 60, 250)     # red    -- |dtheta| > 15 deg

# --- published-line rendering (item 1): the line never covers the shaft ------
RAIL_OFF_PX  = 6.0              # rail offset either side of the centre, in
                                # RENDERED px (scaled up before drawing so it
                                # survives the tile downscale)
RAIL_W_PX    = 1.7              # rail thickness, rendered px
TRUTH_W_PX   = 1.4              # white truth centre line, rendered px
TRUTH_TICK_PX = 6.0             # half-length of the truth end ticks
HEAD_R_PX    = 6.0              # open circle at the head end
GRIPDOT_R_PX = 4.0              # filled dot at the grip point
CORR_LEN_FRAC = 0.6             # corridor centres shortened (item 1)
CORR_DASH_PX = 9.0
CORR_GAP_PX  = 7.0
REASON_LINES = 2                # the reason wraps onto at most this many lines

TIER_COLOR = {"BAND": COL_BAND, "SEG": COL_SEG, "RAY": COL_RAY,
              "END_ON": COL_ENDON, "OCCLUDED": COL_OCCL, "UNSEEN": COL_UNSEEN,
              "MEAS": COL_RAY, "COAST": COL_COAST, "SIGHTED": COL_SIGHTED}
# the timeline wants UNSEEN recessive; a STAMP has to be readable on a black
# tile, so the two use different greys for the same tier
STAMP_COLOR = dict(TIER_COLOR, UNSEEN=(165, 165, 165))
NO_LINE_TIERS = ("END_ON", "OCCLUDED", "UNSEEN")
PUBLISHED_TIERS = ("BAND", "SEG", "RAY", "MEAS", "COAST")
# an absence tile publishes nothing, so its `reason` is all the reader gets --
# END-ON is one of them: "end-on rho^=0.48" is why that tile is blank
REASON_TIERS = ("UNSEEN", "OCCLUDED", "END_ON")

# --- timeline strip (item 4) -------------------------------------------------
TL_H = 48                       # strip height at grid scale, px
TL_H_FULL = 84                  # strip height on the fullres strips, px
TL_PAIR_US = 4_000              # truth pairing window (item 5): +-4 ms
TL_OFF_DEG = 15.0               # "confidently wrong" gate

LEGEND = ("PUBLISHED LINE = a PAIR OF RAILS +-6 px either side of the shaft (the shaft "
          "stays visible between them): RED=BAND  MAGENTA=SEG  AMBER=RAY, filled dot=grip, "
          "open circle=head  |  thin WHITE centre line with end ticks = DTL band truth "
          "(truth should run BETWEEN the rails)  |  dashed CYAN=face-on corridor centre  "
          "|  tier stamped top-left in its colour  |  inset=face-on, same instant")
LEGEND2 = ("TIMELINE under each row: x=time over the whole DTL span, one column per DTL "
           "frame -- row 1 tier (RED BAND / AMBER RAY / MAGENTA SEG / BLUE-GREY END-ON / "
           "PURPLE OCCLUDED / DARK GREY UNSEEN), row 2 rho_hat_D as grey, row 3 truth "
           "(WHITE=truth within 4 ms, RED=published >15 deg from truth).  White ticks = "
           "the face-on P ladder.")
LEGEND_BASELINE = ("baseline = the UNMODIFIED face-on tracker on the DTL stream: "
                   "AMBER=measured (flags&1), GREY=coasted  |  " + LEGEND)

CLUB_DTL_SCHEMA = "pinpoint.clubDtl/1"


# ---------------------------------------------------------------- geometry
def wrap_pi(a):
    return (np.asarray(a) + np.pi) % (2 * np.pi) - np.pi


def l_full_face_on(club):
    """p95 of face-on lenPx over measured frames with the shaft near horizontal
    (|sin theta_F| < 0.25, i.e. in the face-on image plane so rho_F == 1)."""
    s = club.get("samples", []) or []
    if not s:
        return float("nan")
    th = np.array([x.get("theta", 0.0) for x in s], dtype=float)
    ln = np.array([x.get("lenPx", 0.0) for x in s], dtype=float)
    fl = np.array([int(x.get("flags", 0)) for x in s])
    m = ((fl & SHAFT_MEASURED) > 0) & (ln > 0)
    h = m & (np.abs(np.sin(th)) < 0.25)
    use = h if h.sum() >= 8 else m
    return float(np.percentile(ln[use], 95)) if use.any() else float("nan")


def interp_face_on(club, t_us):
    """Face-on witness at one DTL instant: theta (unwrapped interpolation), lenPx.
    The streams are not frame-synchronous -- never pair by index (design Sec 4.5)."""
    s = club.get("samples", []) or []
    if not s:
        return float("nan"), float("nan")
    t = np.array([x["t_us"] for x in s], dtype=float)
    th = np.unwrap(np.array([x.get("theta", 0.0) for x in s], dtype=float))
    ln = np.array([x.get("lenPx", 0.0) for x in s], dtype=float)
    return float(np.interp(t_us, t, th)), float(np.interp(t_us, t, ln))


def schedule(theta_F, rho_F):
    """(a) rho_hat_D and (c) the two corridor centres, design Sec 4.1."""
    rho = min(max(float(rho_F), 0.0), 1.0)
    ux = rho * math.cos(theta_F)
    uy = math.sqrt(max(0.0, 1.0 - rho * rho))
    rho_d = math.sqrt(max(0.0, 1.0 - ux * ux))
    return rho_d, math.atan2(rho * math.sin(theta_F), -uy), \
        math.atan2(rho * math.sin(theta_F), +uy)


# ---------------------------------------------------------------- load data
def swing_dir_for(corpus_dir, sid):
    corpus_dir = Path(corpus_dir)
    if has_swing(corpus_dir / sid):
        return corpus_dir / sid
    if "__" in sid:
        a, b = sid.split("__", 1)
        if has_swing(corpus_dir / a / b):
            return corpus_dir / a / b
    return corpus_dir / sid


def pick_stream(swing, perspective, needles):
    """DTL stream selection: recorded perspective wins, alias substring is the
    fallback for legacy captures (assessment Sec 5 item 1)."""
    for s in swing.video_streams():
        if s.get("setup", {}).get("perspective") == perspective:
            return s
    for s in swing.video_streams():
        blob = (s.get("alias", "") + " " + s.get("file", "")).lower()
        if any(n in blob for n in needles):
            return s
    return None


def trace_by_frame(run_dir):
    t = Path(run_dir) / "trace.jsonl"
    if not t.exists():
        return {}
    out = {}
    with open(t, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            o = json.loads(line)
            if "summary" in o:
                continue
            out[int(o["frame"])] = o
    return out


def pose_grip_series(pose2d, W, H):
    src = pose2d.get("synth") or pose2d.get("frames") or []
    t = np.array([f["t_us"] for f in src], dtype=float)
    gx = np.empty(len(src))
    gy = np.empty(len(src))
    for i, f in enumerate(src):
        kp = np.asarray(f["kp"], dtype=float).reshape(-1, 3)
        gx[i] = 0.5 * (kp[9, 0] + kp[10, 0]) * W
        gy[i] = 0.5 * (kp[9, 1] + kp[10, 1]) * H
    return t, gx, gy


def pose_extent_px(pose2d, W, H, t_lo, t_hi, conf_min=0.3):
    xs, ys = [], []
    for f in pose2d.get("frames", []) or []:
        if not (t_lo <= f["t_us"] <= t_hi):
            continue
        kp = np.asarray(f["kp"], dtype=float).reshape(-1, 3)
        m = kp[:, 2] >= conf_min
        if not m.any():
            continue
        xs.append(kp[m, 0] * W)
        ys.append(kp[m, 1] * H)
    if not xs:
        return None
    xs, ys = np.concatenate(xs), np.concatenate(ys)
    return float(xs.min()), float(ys.min()), float(xs.max()), float(ys.max())


def golfer_box(pose2d, W, H, t_lo, t_hi, L_D=None, grip_series=None):
    """ONE crop for every tile of a swing.

    It is the FULL frame width and a row range that always contains the grip
    plus and minus CLUB_REACH * L_D and the ball region.  The earlier square
    golfer box cropped too tightly: at address and at impact the club ran out of
    the tile and the ball -- which is the one DTL landmark the tracker's address
    and impact anchor needs -- sat outside it entirely.  Fixed for the whole
    swing on purpose: a per-tile crop around the drawn line hides the golfer
    exactly on the end-on frames the montage exists to adjudicate."""
    ext = pose_extent_px(pose2d, W, H, t_lo, t_hi)
    lo, hi = (0.0, float(H)) if ext is None else (ext[1], ext[3])
    if grip_series is not None and L_D and math.isfinite(L_D) and L_D > 0:
        gt, gx, gy = grip_series
        sel = (gt >= t_lo) & (gt <= t_hi) if gt.size else np.array([], dtype=bool)
        if sel.any():
            lo = min(lo, float(gy[sel].min()) - CLUB_REACH * L_D)
            hi = max(hi, float(gy[sel].max()) + CLUB_REACH * L_D)
    hi = max(hi, BALL_ROW_FRAC * H)          # the ball region is always in shot
    m = CROP_MARGIN_FRAC * max(1.0, hi - lo) * 0.25
    y0 = int(round(max(0.0, lo - m)))
    y1 = int(round(min(float(H), hi + m)))
    if y1 - y0 < 16:
        y0, y1 = 0, H
    return 0, y0, int(W), y1


def load_swing_data(corpus_dir, fo_runs, dtl_runs, sid, mode, truth_dir=None):
    """Everything one row needs.  (Adapted from montage_positions.load_swing_data,
    which is face-on hard-wired: one stream, one run dir, no second view.)"""
    sd = swing_dir_for(corpus_dir, sid)
    try:
        swing = Swing(sd)
    except Exception as e:
        return None, f"{sid}: cannot read swing.json ({e})"
    dtl_s = pick_stream(swing, 1, ("dtl", "down"))
    fo_s = pick_stream(swing, 2, ("face", "fo"))
    if dtl_s is None:
        return None, f"{sid}: no DTL video stream (perspective 1 / alias dtl|down)"
    if fo_s is None:
        return None, f"{sid}: no face-on video stream (perspective 2)"
    dtl_v, fo_v = sd / dtl_s.get("file", ""), sd / fo_s.get("file", "")
    if not dtl_v.exists():
        return None, f"{sid}: DTL video missing: {dtl_v}"

    fo_dir, dtl_dir = Path(fo_runs) / sid, Path(dtl_runs) / sid
    if not (fo_dir / "result.json").exists():
        return None, f"{sid}: no face-on result.json under {fo_dir}"
    fo_run = RunResult(fo_dir)
    fo_club = fo_run.club
    if not fo_club.get("positions"):
        return None, f"{sid}: face-on run has no P-ladder"

    # club_dtl.json is loaded BEFORE the dimensions because in track mode it IS
    # the source of them.  `swinglab_run --dtl` writes the FACE-ON result.json
    # into the DTL run dir beside the DTL sidecars: its analysis.club is
    # 1280x1024 while the DTL stream is 512x1024, and its analysis.pose2d is the
    # face-on pose.  Scaling club_dtl's normalised grip/head by that width put
    # the rails ~120 px off the shaft while the graded angle still read 0.3 deg.
    # So track mode reads the DTL sidecars only -- club_dtl.json for the frame
    # size, pose_dtl.json for the pose -- and a DTL-only run dir with no
    # result.json in it renders exactly the same picture.
    club_dtl = None
    if mode == "track":
        p = dtl_dir / "club_dtl.json"
        if not p.exists():
            return None, f"{sid}: --mode track needs {p}"
        club_dtl = json.load(open(p, encoding="utf-8"))
        if club_dtl.get("schema") != CLUB_DTL_SCHEMA:
            return None, f"{sid}: club_dtl.json schema {club_dtl.get('schema')!r} != {CLUB_DTL_SCHEMA!r}"

    dtl_res, dtl_pose, dtl_samples = {}, {}, []
    if mode == "track":
        pp = dtl_dir / "pose_dtl.json"
        if pp.exists():
            dtl_pose = json.load(open(pp, encoding="utf-8")) or {}
    elif (dtl_dir / "result.json").exists():
        # baseline and probe are pointed at a `--face-on DTL` run, where the
        # result.json really is the DTL stream's: baseline's whole source is
        # analysis.club.samples[].
        dtl_run = RunResult(dtl_dir)
        dtl_res = dtl_run.club
        dtl_pose = dtl_run.analysis.get("pose2d", {}) or {}
        dtl_samples = dtl_res.get("samples", []) or []

    dim_src = club_dtl if mode == "track" else dtl_res
    dW = int(dim_src.get("frameWidth") or dtl_s.get("encoded", {}).get("width") or 0)
    dH = int(dim_src.get("frameHeight") or dtl_s.get("encoded", {}).get("height") or 0)
    fW = int(fo_club.get("frameWidth") or fo_s.get("encoded", {}).get("width") or 0)
    fH = int(fo_club.get("frameHeight") or fo_s.get("encoded", {}).get("height") or 0)
    if not (dW and dH and fW and fH):
        return None, f"{sid}: cannot resolve frame dimensions"

    return dict(
        sid=sid, swing_dir=sd,
        dtl_video=dtl_v, fo_video=fo_v if fo_v.exists() else None,
        dtl_ts=np.asarray(dtl_s.get("frames", {}).get("t_us", []), dtype=np.int64),
        fo_ts=np.asarray(fo_s.get("frames", {}).get("t_us", []), dtype=np.int64),
        dtl_W=dW, dtl_H=dH, fo_W=fW, fo_H=fH,
        fo_club=fo_club, fo_pose=fo_run.analysis.get("pose2d", {}) or {},
        dtl_club=dtl_res, dtl_pose=dtl_pose, dtl_samples=dtl_samples,
        dtl_trace=trace_by_frame(dtl_dir),
        _truth=load_band_truth(truth_dir, sid),
        club_dtl=club_dtl), None


# ---------------------------------------------------------------- per-frame model
def nearest_index(ts, t_us):
    if ts.size == 0:
        return -1
    i = int(np.clip(np.searchsorted(ts, t_us), 0, ts.size - 1))
    if i > 0 and abs(int(ts[i - 1]) - int(t_us)) <= abs(int(ts[i]) - int(t_us)):
        i -= 1
    return i


def band_truth_at(trace, frame_idx):
    """The E1 band lock within +-BAND_MATCH_FRAMES of this DTL frame, or None.
    Absent in every run the design's Stage 0 was given -- see dtl_probe's summary.
    `--truth-dir` supersedes this; see load_band_truth()."""
    for d in range(BAND_MATCH_FRAMES + 1):
        for f in ((frame_idx,) if d == 0 else (frame_idx - d, frame_idx + d)):
            r = trace.get(int(f))
            if r and r.get("band_n"):
                return math.radians(float(r["band_theta"])), int(f)
    return None


def load_band_truth(truth_dir, sid):
    """The face-on-independent DTL band truth written by
    tools/shaftlab/dtl_band_truth.py -> {frame: {theta, grip, head}}.

    This is the instrument design Sec 6 asks for.  E1 cannot produce it on these
    runs (no ring light on the DTL camera), so the truth is generated outside the
    tracker and read in here; when --truth-dir is given it supersedes the trace's
    band lock in EVERY mode, so the same white line means the same thing on the
    baseline, probe and track montages."""
    if not truth_dir:
        return {}
    p = Path(truth_dir) / f"{sid}.json"
    if not p.exists():
        return {}
    doc = json.load(open(p, encoding="utf-8"))
    return {int(f["frame"]): f for f in doc.get("frames", [])}


def truth_at(truth, frame_idx):
    """Nearest truth frame within +-BAND_MATCH_FRAMES -> (theta, grip, head)."""
    for d in range(BAND_MATCH_FRAMES + 1):
        for f in ((frame_idx,) if d == 0 else (frame_idx - d, frame_idx + d)):
            r = truth.get(int(f))
            if r:
                return (float(r["theta"]), tuple(r["grip"]), tuple(r["head"]))
    return None


def tile_model(data, mode, p, t_us, frame_idx, grip_px, L_D):
    """-> dict(tier, line=(theta,len_px)|None, seg=(p0,p1)|None, stamp, reason,
    rho_pred, corridor=[(theta,len)], truth_theta|None, truth_seg|None, delta_deg)."""
    theta_F, len_F = interp_face_on(data["fo_club"], t_us)
    L_full = data["_L_full"]
    rho_F = (len_F / L_full) if (L_full and L_full > 0 and len_F > 0) else float("nan")
    if math.isfinite(rho_F):
        rho_d, ca, cb = schedule(theta_F, rho_F)
    else:
        rho_d, ca, cb = float("nan"), float("nan"), float("nan")

    corridor = []
    if math.isfinite(rho_d) and math.isfinite(L_D):
        for c in (ca, cb):
            if math.isfinite(c):
                corridor.append((c, max(8.0, rho_d * L_D)))

    tr = truth_at(data.get("_truth", {}), frame_idx)
    if tr is not None:
        truth_theta, truth_seg = tr[0], (tr[1], tr[2])
    else:
        bt = band_truth_at(data["dtl_trace"], frame_idx)
        truth_theta = bt[0] if bt else None
        truth_seg = None

    tier, line, stamp, reason, rec = "UNSEEN", None, None, "", None
    if mode == "baseline":
        s = data["_samples_by_frame"].get(int(frame_idx))
        if s is not None:
            meas = bool(int(s.get("flags", 0)) & SHAFT_MEASURED)
            tier = "MEAS" if meas else "COAST"
            ln = float(s.get("lenPx", 0.0)) or (rho_d * L_D if math.isfinite(rho_d) else 0.0)
            line = (float(s.get("theta", 0.0)), max(8.0, ln))
        else:
            stamp = "NO SAMPLE"
    elif mode == "probe":
        if not math.isfinite(rho_d):
            tier, stamp = "UNSEEN", "NO FACE-ON"
        elif rho_d < RHO_SOLVE_MIN:
            tier, stamp = "END_ON", "END-ON"
        else:
            tier, stamp = "SIGHTED", None
    else:  # track
        fr = data["_dtl_frames_by_t"]
        k = nearest_index(data["_dtl_frame_ts"], t_us) if data["_dtl_frame_ts"].size else -1
        rec = fr[k] if 0 <= k < len(fr) else None
        if rec is None:
            stamp = "NO FRAME"
        else:
            tier = str(rec.get("tier", "UNSEEN"))
            reason = str(rec.get("reason") or "")
            if tier not in NO_LINE_TIERS and rec.get("theta") is not None:
                ln = float(rec.get("lenPx") or 0.0) or (rho_d * L_D if math.isfinite(rho_d) else 0.0)
                line = (float(rec["theta"]), max(8.0, ln))
            if rec.get("escape"):
                stamp = "ESC"
            if rec.get("rhoPred") is not None:
                rho_d = float(rec["rhoPred"])
        # The corridor is the tracker's OWN gate: club_dtl.json carries
        # `corridor` as null, or as {centres, half, on} with `on` false, on
        # every frame where it was not applied (at P1 and P7 the rhoFMax gate
        # turns it off).  Drawing it anyway put two near-vertical dashed lines
        # on the tile that a reviewer reads as the tracker's own output missing
        # the shaft.  They are not its output, so they are not drawn.
        co = (rec or {}).get("corridor")
        if not (isinstance(co, dict) and bool(co.get("on"))):
            corridor = []

    # the published SEGMENT in pixels -- what the rails are drawn around.  In
    # track mode the record carries its own grip/head (normalised) and that IS
    # the published segment; baseline/probe get a ray from the pose grip.
    seg = None
    if line is not None and grip_px is not None:
        th, ln = line
        seg = ((float(grip_px[0]), float(grip_px[1])),
               (grip_px[0] + ln * math.cos(th), grip_px[1] + ln * math.sin(th)))
    if (mode == "track" and rec is not None and line is not None
            and rec.get("grip") and rec.get("head")):
        W, H = float(data["dtl_W"]), float(data["dtl_H"])
        g = (float(rec["grip"][0]) * W, float(rec["grip"][1]) * H)
        hd = (float(rec["head"][0]) * W, float(rec["head"][1]) * H)
        if math.hypot(hd[0] - g[0], hd[1] - g[1]) > 4.0:
            seg = (g, hd)

    delta_deg = None
    if line is not None and truth_theta is not None:
        delta_deg = float(math.degrees(wrap_pi(line[0] - truth_theta)))

    return dict(tier=tier, line=line, seg=seg, stamp=stamp, reason=reason,
                rho_pred=rho_d, corridor=corridor, truth_theta=truth_theta,
                truth_seg=truth_seg, grip=grip_px, delta_deg=delta_deg)


# ---------------------------------------------------------------- drawing
def tier_colour(tier):
    return TIER_COLOR.get(tier, COL_STAMP)


def delta_colour(d_deg):
    a = abs(float(d_deg))
    return COL_OK if a <= 5.0 else (COL_WARN if a <= 15.0 else COL_BAD)


def _ipt(p):
    return (int(round(p[0])), int(round(p[1])))


def _th(v, sc):
    """A rendered-px width expressed in the native pixels we draw in."""
    return max(1, int(round(v * sc)))


def _unit_normal(p0, p1):
    dx, dy = p1[0] - p0[0], p1[1] - p0[1]
    L = math.hypot(dx, dy)
    if L < 1e-6:
        return None, None, 0.0
    return (-dy / L, dx / L), (dx / L, dy / L), L


def draw_rails(img, p0, p1, colour, sc):
    """The published line as a PAIR of rails either side of the shaft, so the
    shaft itself is never hidden under the annotation (work package A3 item 1).
    Each rail gets a dark keyline so it reads on a white shaft and on the mat."""
    n, u, L = _unit_normal(p0, p1)
    if n is None:
        return
    off = RAIL_OFF_PX * sc
    t = _th(RAIL_W_PX, sc)
    t_out = t + 2 * max(1, int(round(sc)))
    for s in (+1.0, -1.0):
        a = (p0[0] + s * off * n[0], p0[1] + s * off * n[1])
        b = (p1[0] + s * off * n[0], p1[1] + s * off * n[1])
        cv2.line(img, _ipt(a), _ipt(b), COL_OUTLINE, t_out, cv2.LINE_AA)
        cv2.line(img, _ipt(a), _ipt(b), colour, t, cv2.LINE_AA)
    # open circle at the HEAD end, filled dot at the GRIP end
    r = max(2, int(round(HEAD_R_PX * sc)))
    cv2.circle(img, _ipt(p1), r + max(1, int(round(sc))), COL_OUTLINE, t_out, cv2.LINE_AA)
    cv2.circle(img, _ipt(p1), r, colour, t, cv2.LINE_AA)
    rg = max(2, int(round(GRIPDOT_R_PX * sc)))
    cv2.circle(img, _ipt(p0), rg + max(1, int(round(sc))), COL_OUTLINE, -1, cv2.LINE_AA)
    cv2.circle(img, _ipt(p0), rg, colour, -1, cv2.LINE_AA)


def draw_truth_seg(img, p0, p1, sc):
    """The truth as a single thin WHITE centre line with perpendicular end
    ticks -- so "truth runs between the rails" reads at a glance."""
    n, u, L = _unit_normal(p0, p1)
    t = _th(TRUTH_W_PX, sc)
    t_out = t + 2 * max(1, int(round(sc)))
    cv2.line(img, _ipt(p0), _ipt(p1), COL_OUTLINE, t_out, cv2.LINE_AA)
    cv2.line(img, _ipt(p0), _ipt(p1), COL_TRUTH, t, cv2.LINE_AA)
    if n is None:
        return
    tick = TRUTH_TICK_PX * sc
    for q in (p0, p1):
        a = (q[0] - tick * n[0], q[1] - tick * n[1])
        b = (q[0] + tick * n[0], q[1] + tick * n[1])
        cv2.line(img, _ipt(a), _ipt(b), COL_OUTLINE, t_out, cv2.LINE_AA)
        cv2.line(img, _ipt(a), _ipt(b), COL_TRUTH, t, cv2.LINE_AA)


def draw_dashed(img, p0, p1, colour, sc, thickness=None):
    n, u, L = _unit_normal(p0, p1)
    if u is None:
        return
    t = thickness or _th(1.4, sc)
    dash, gap = CORR_DASH_PX * sc, CORR_GAP_PX * sc
    s = 0.0
    while s < L:
        e = min(L, s + dash)
        cv2.line(img, _ipt((p0[0] + u[0] * s, p0[1] + u[1] * s)),
                 _ipt((p0[0] + u[0] * e, p0[1] + u[1] * e)), colour, t, cv2.LINE_AA)
        s = e + gap


FONT = cv2.FONT_HERSHEY_SIMPLEX


def _text_w(text, scale):
    return cv2.getTextSize(text, FONT, scale, 1)[0][0]


def put_text_parts(img, parts, org, scale, max_w=None, floor=0.30, bg=(0, 0, 0)):
    """Left-to-right coloured runs on one baseline -- the caption's delta is
    graded green/amber/red (item 3).  The scale shrinks (to a floor) so a long
    caption never silently runs off the edge of the tile."""
    if max_w:
        while scale > floor:
            tot = sum(_text_w(t, scale) + max(4, int(round(12 * scale)))
                      for t, _ in parts if t)
            if tot <= max_w:
                break
            scale -= 0.02
    x, y = int(org[0]), int(org[1])
    for text, colour in parts:
        if not text:
            continue
        mp.put_text(img, text, (x, y), scale=scale, color=colour, bg=bg)
        x += _text_w(text, scale) + max(4, int(round(12 * scale)))


# cv2's Hershey fonts are ASCII only and the tracker writes its reasons with
# Greek glyphs ("end-on rho-hat = 0.48"), so those few are spelled out rather
# than replaced with question marks
GLYPHS = {"ρ": "rho", "θ": "th", "̂": "^", "°": "deg",
          "±": "+-", "→": "->", "≤": "<=", "≥": ">="}


def _ascii(text):
    for g, r in GLYPHS.items():
        text = text.replace(g, r)
    return text.encode("ascii", "replace").decode("ascii")


def wrap_text(text, scale, max_w, max_lines=REASON_LINES):
    """Greedy word wrap at ONE scale.  The reason strings are a sentence and a
    tile at grid scale is only ~215 px wide, so they wrap instead of shrinking
    below the caption's size or being cut off mid-word."""
    lines, cur = [], ""
    for wd in _ascii(text).split():
        trial = (cur + " " + wd).strip()
        if cur and _text_w(trial, scale) > max_w:
            lines.append(cur)
            cur = wd
            if len(lines) >= max_lines:
                cur, tail = "", lines[-1]
                while tail and _text_w(tail + " ...", scale) > max_w:
                    tail = tail.rsplit(" ", 1)[0] if " " in tail else tail[:-1]
                lines[-1] = tail + " ..."
                break
        else:
            cur = trial
    if cur and len(lines) < max_lines:
        lines.append(cur)
    return lines


def put_text_fit(img, text, org, scale, colour, max_w, floor=0.28, **kw):
    """One line, shrunk to fit and then hard-truncated -- the `reason` strings
    are long and a tile at grid scale is only ~215 px wide."""
    text = _ascii(text)
    while scale > floor and _text_w(text, scale) > max_w:
        scale -= 0.02
    while len(text) > 4 and _text_w(text, scale) > max_w:
        text = text[:-1]
    mp.put_text(img, text, org, scale=scale, color=colour, **kw)


def make_tile(frame, box, model, p, fo_inset, tile_size):
    """One P tile at FULL crop resolution when tile_size is None, else resized
    to (w, h) -- the crop is full frame width now, so tiles are portrait."""
    img = frame.copy()
    g = model["grip"]
    x0, y0, x1, y1 = box
    # native px per RENDERED px, so every annotation is the same apparent size
    # on the grid tile and on the fullres strip
    sc = 1.0 if tile_size is None else max(1.0, (y1 - y0) / float(max(1, tile_size[1])))

    # ---- corridor centres first (dashed, shortened -- they are context) -----
    if g is not None:
        for c, ln in model["corridor"]:
            ln = ln * CORR_LEN_FRAC
            draw_dashed(img, g, (g[0] + ln * math.cos(c), g[1] + ln * math.sin(c)),
                        COL_CORR, sc)

    # ---- truth centre line --------------------------------------------------
    if model.get("truth_seg") is not None:
        # the band truth carries its OWN grip and head points -- draw the
        # segment it measured, not a ray from the pose grip
        draw_truth_seg(img, model["truth_seg"][0], model["truth_seg"][1], sc)
    elif model["truth_theta"] is not None and g is not None:
        ln = model["rho_pred"] * model.get("_L_D", 0.0) if model.get("_L_D") else 0.0
        ln = ln if ln > 8 else 120.0
        draw_truth_seg(img, g, (g[0] + ln * math.cos(model["truth_theta"]),
                                g[1] + ln * math.sin(model["truth_theta"])), sc)

    # ---- the published line, as rails either side of the shaft --------------
    if model.get("seg") is not None:
        draw_rails(img, model["seg"][0], model["seg"][1], tier_colour(model["tier"]), sc)
    elif g is not None:
        # nothing published: the yellow pose-grip anchor still shows where the
        # tracker was looking
        gp = _ipt(g)
        cv2.circle(img, gp, _th(5, sc), COL_OUTLINE, -1, cv2.LINE_AA)
        cv2.circle(img, gp, _th(3.5, sc), COL_GRIP, -1, cv2.LINE_AA)

    crop = img[y0:y1, x0:x1]
    if crop.size == 0:
        crop = img
    if tile_size is not None:
        crop = mp.resize_smart(crop, (int(tile_size[0]), int(tile_size[1])))
    tile = np.ascontiguousarray(crop)
    h, w = tile.shape[:2]

    if fo_inset is not None:
        iw = max(48, int(w * INSET_FRAC))
        ih = max(48, int(iw * fo_inset.shape[0] / max(1, fo_inset.shape[1])))
        ih = min(ih, h // 3)
        ins = mp.resize_smart(fo_inset, (iw, ih))
        tile[4:4 + ih, w - iw - 4:w - 4] = ins
        cv2.rectangle(tile, (w - iw - 4, 4), (w - 5, 4 + ih - 1), (90, 90, 90), 1)

    scale = max(0.42, min(0.8, w / 520.0))

    # ---- item 2: the tier stamped top-left, in the tier colour -------------
    tier = model["tier"]
    stamp = tier.replace("_", "-")
    if model["stamp"]:
        stamp += " " + model["stamp"]
    y_stamp = int(round(22 * scale / 0.5)) + 4
    put_text_fit(tile, stamp, (6, y_stamp), scale * 1.25,
                 STAMP_COLOR.get(tier, COL_STAMP), w - 12, floor=0.4,
                 thickness=2 if scale >= 0.55 else 1)
    # an absence tile publishes no line, so the reason under the stamp IS the
    # tile: it is set at the caption's size and wrapped, not shrunk to fine
    # print that a reviewer has to lean in for
    if tier in REASON_TIERS and model.get("reason"):
        y_r = y_stamp + int(round(24 * scale / 0.5))
        for ln in wrap_text(model["reason"], scale, w - 12):
            put_text_fit(tile, ln, (6, y_r), scale, COL_REASON, w - 12, floor=scale)
            y_r += int(round(20 * scale / 0.5))

    # ---- item 3: caption with the per-tile angle readout -------------------
    rho = model["rho_pred"]
    cap = f"P{p}  rho={rho:.2f}  {tier}" if math.isfinite(rho) else f"P{p}  rho=?  {tier}"
    parts = [(cap, (240, 240, 240))]
    if model["line"] is not None:
        th_deg = math.degrees(model["line"][0]) % 360.0
        parts.append((f"th={th_deg:.0f}deg len={model['line'][1]:.0f}", (210, 230, 255)))
        if model.get("delta_deg") is not None:
            d = model["delta_deg"]
            parts.append((f"D={d:+.1f}deg", delta_colour(d)))
    put_text_parts(tile, parts, (6, h - 10), scale, max_w=w - 12)
    cv2.rectangle(tile, (0, 0), (w - 1, h - 1), (55, 55, 55), 1)
    return tile


def missing_tile(p, size):
    w, h = (size, size) if isinstance(size, int) else (int(size[0]), int(size[1]))
    img = np.full((h, w, 3), COL_MISSING, dtype=np.uint8)
    cv2.rectangle(img, (2, 2), (w - 3, h - 3), (70, 70, 70), 1)
    mp.put_text(img, f"P{p}", (10, 32), scale=0.7, color=(150, 150, 150), bg=None)
    mp.put_text(img, "no FO ladder rung", (10, h - 14), scale=0.40,
                color=(120, 120, 120), bg=None)
    return img


# ------------------------------------------------- whole-span model (items 4/5)
def timeline_frames(data, mode):
    """One entry per DTL frame over the WHOLE swing -- not just the nine P
    tiles.  -> [dict(t_us, tier, rho, theta, reason)] in time order.

    track   : straight off club_dtl.json (tier, rhoPred, theta).
    baseline: the face-on tracker's own samples mapped onto DTL frames,
              MEAS / COAST / NOSAMPLE, with rho_hat_D from the schedule.
    probe   : no tracker -- SIGHTED / END_ON from the schedule alone."""
    out = []
    if mode == "track" and data.get("club_dtl") is not None:
        for f in data["club_dtl"].get("frames", []) or []:
            th = f.get("theta")
            rp = f.get("rhoPred")
            out.append(dict(t_us=int(f["t_us"]), tier=str(f.get("tier", "UNSEEN")),
                            rho=float(rp) if rp is not None else float("nan"),
                            theta=float(th) if th is not None else None,
                            reason=str(f.get("reason") or "")))
        out.sort(key=lambda f: f["t_us"])
        return out

    ts = data["dtl_ts"]
    if ts.size == 0:
        return out
    s = data["fo_club"].get("samples", []) or []
    L_full = data.get("_L_full") or float("nan")
    if s and math.isfinite(L_full) and L_full > 0:
        ft = np.array([x["t_us"] for x in s], dtype=float)
        fth = np.unwrap(np.array([x.get("theta", 0.0) for x in s], dtype=float))
        fln = np.array([x.get("lenPx", 0.0) for x in s], dtype=float)
        th_i = np.interp(ts.astype(float), ft, fth)
        ln_i = np.interp(ts.astype(float), ft, fln)
        rho_f = np.clip(ln_i / L_full, 0.0, 1.0)
        rho_d = np.sqrt(np.clip(1.0 - (rho_f * np.cos(th_i)) ** 2, 0.0, 1.0))
    else:
        rho_d = np.full(ts.size, np.nan)
    for i in range(ts.size):
        r = float(rho_d[i])
        if mode == "baseline":
            smp = data["_samples_by_frame"].get(int(i))
            if smp is None:
                tier, th = "UNSEEN", None
            else:
                tier = "MEAS" if int(smp.get("flags", 0)) & SHAFT_MEASURED else "COAST"
                th = float(smp.get("theta", 0.0))
        else:
            tier = "SIGHTED" if (math.isfinite(r) and r >= RHO_SOLVE_MIN) else "END_ON"
            th = None
        out.append(dict(t_us=int(ts[i]), tier=tier, rho=r, theta=th, reason=""))
    return out


def truth_arrays(data):
    tr = sorted((data.get("_truth") or {}).values(), key=lambda f: int(f["t_us"]))
    if not tr:
        return np.array([], dtype=np.int64), np.array([], dtype=float)
    return (np.array([int(f["t_us"]) for f in tr], dtype=np.int64),
            np.array([float(f["theta"]) for f in tr], dtype=float))


def annotate_truth(tl, truth_ts, truth_th, tol_us=TL_PAIR_US):
    """Tag every timeline frame with _cov (a truth frame within +-tol) and
    _delta (signed degrees published-minus-truth) -- item 5 pairs within 4 ms."""
    for f in tl:
        f["_cov"], f["_delta"] = False, None
        if truth_ts.size == 0:
            continue
        i = nearest_index(truth_ts, f["t_us"])
        if abs(int(truth_ts[i]) - int(f["t_us"])) > tol_us:
            continue
        f["_cov"] = True
        if f["theta"] is not None:
            f["_delta"] = float(math.degrees(wrap_pi(f["theta"] - truth_th[i])))
    return tl


def span_stats(tl, rho_min):
    """Published / sighted / truth agreement over ALL frames (item 5)."""
    n_pub = sum(1 for f in tl if f["tier"] in PUBLISHED_TIERS)
    n_sight = sum(1 for f in tl if math.isfinite(f["rho"]) and f["rho"] >= rho_min)
    d = np.array([abs(f["_delta"]) for f in tl if f.get("_delta") is not None], dtype=float)
    return dict(all_n=len(tl), all_pub=n_pub, all_sighted=n_sight,
                all_paired=int(d.size),
                all_p50=float(np.percentile(d, 50)) if d.size else float("nan"),
                all_p90=float(np.percentile(d, 90)) if d.size else float("nan"),
                all_off15=int((d > TL_OFF_DEG).sum()),
                all_cov=sum(1 for f in tl if f.get("_cov")))


def build_timeline(tl, p_times, width, height, rho_min):
    """The per-swing coverage strip (item 4): x = time over the whole DTL span,
    one thin column per DTL frame.  Row 1 = tier, row 2 = rho_hat_D as grey,
    row 3 = truth coverage (white) / published >15 deg from truth (red).
    White ticks + labels mark the face-on P ladder instants."""
    img = np.full((max(8, int(height)), max(16, int(width)), 3), 18, dtype=np.uint8)
    h, w = img.shape[:2]
    gut = int(min(110, max(44, w * 0.020)))
    x_lo, x_hi = gut, w - 4
    ts = 0.30 * (h / float(TL_H))
    ts = max(0.28, min(0.55, ts))
    lab_h = max(14, int(h * 0.36))
    tier_h = max(4, int(h * 0.30))
    rho_h = max(3, int(h * 0.18))
    y_tier, y_rho = lab_h, lab_h + tier_h
    y_tru, y_end = y_rho + rho_h, h - 1
    if y_tru >= y_end:
        y_tru = max(y_rho + 1, y_end - 2)

    for (y0, y1, name) in ((y_tier, y_rho, "tier"), (y_rho, y_tru, "rho"),
                           (y_tru, y_end, "truth")):
        mp.put_text(img, name, (4, min(h - 2, y1 - 1)), scale=ts * 0.85,
                    color=(150, 150, 150), bg=None)

    if tl:
        t0 = min(f["t_us"] for f in tl)
        t1 = max(f["t_us"] for f in tl)
        span = float(max(1, t1 - t0))
        cw = max(1, int(round((x_hi - x_lo) / float(max(1, len(tl))))))
        avail = max(1, x_hi - x_lo - cw)

        def xof(t):
            return x_lo + int(round((float(t) - t0) / span * avail))

        for f in tl:
            x = xof(f["t_us"])
            cv2.rectangle(img, (x, y_tier), (x + cw - 1, y_rho - 1),
                          tier_colour(f["tier"]), -1)
            r = f["rho"]
            gv = int(10 + 235 * min(max(r, 0.0), 1.0)) if math.isfinite(r) else 0
            cv2.rectangle(img, (x, y_rho), (x + cw - 1, y_tru - 1), (gv, gv, gv), -1)
            d = f.get("_delta")
            if d is not None and abs(d) > TL_OFF_DEG:
                cv2.rectangle(img, (x, y_tru), (x + cw - 1, y_end - 1), COL_BAD, -1)
            elif f.get("_cov"):
                cv2.rectangle(img, (x, y_tru), (x + cw - 1, y_end - 1), COL_TRUTH, -1)

        for i, (p, t) in enumerate(sorted(p_times)):
            if not (t0 - span * 0.02 <= t <= t1 + span * 0.02):
                continue
            x = int(min(max(xof(t), x_lo), x_hi - 1))
            cv2.line(img, (x, max(2, lab_h - 2)), (x, y_end), (255, 255, 255), 1,
                     cv2.LINE_AA)
            ly = int(lab_h * 0.52) if i % 2 == 0 else max(4, lab_h - 3)
            mp.put_text(img, f"P{p}", (x + 2, ly), scale=ts, color=(255, 255, 255),
                        bg=(0, 0, 0), bg_alpha=0.5)
        mp.put_text(img, f"{(t1 - t0) / 1000.0:.0f} ms, {len(tl)} DTL frames, "
                         f"rhoSolveMin={rho_min:.2f}",
                    (x_lo + 2, max(4, int(lab_h * 0.52))), scale=ts * 0.9,
                    color=(160, 200, 160), bg=(0, 0, 0), bg_alpha=0.5)
    # hairline separators: without them the white truth row and a bright rho
    # row run into each other
    for y in (y_tier, y_rho, y_tru):
        cv2.line(img, (0, y), (w - 1, y), (30, 30, 30), 1)
    cv2.rectangle(img, (0, 0), (w - 1, h - 1), (55, 55, 55), 1)
    return img


# ---------------------------------------------------------------- per-swing row
def build_row(data, mode, tile_px):
    """-> (row_image, stats).  tile_px None => native crop resolution."""
    fo_club = data["fo_club"]
    ps = mp.positions_by_p(fo_club.get("positions", []) or [])
    data["_L_full"] = l_full_face_on(fo_club)
    data["_samples_by_frame"] = {}
    dtl_ts = data["dtl_ts"].astype(float)
    for s in data["dtl_samples"]:
        k = nearest_index(data["dtl_ts"], int(s["t_us"]))
        if k >= 0:
            data["_samples_by_frame"][k] = s
    if data["club_dtl"] is not None:
        data["_dtl_frames_by_t"] = data["club_dtl"].get("frames", [])
        data["_dtl_frame_ts"] = np.array([f["t_us"] for f in data["_dtl_frames_by_t"]],
                                         dtype=np.int64)
    else:
        data["_dtl_frames_by_t"], data["_dtl_frame_ts"] = [], np.array([], dtype=np.int64)

    t_lo = float(min(p["t_us"] for p in ps.values()))
    t_hi = float(max(p["t_us"] for p in ps.values()))

    pt, pgx, pgy = pose_grip_series(data["dtl_pose"], data["dtl_W"], data["dtl_H"]) \
        if data["dtl_pose"] else (np.array([]), np.array([]), np.array([]))

    # L_D: the DTL full projected length, px.  Rung 1 = the DTL run's own address
    # grip->ball distance divided by rho_hat_D at P1 (a DTL measurement); rung 2 =
    # the DTL band truth's own median grip->head distance; rung 3 = the frame.
    # Computed BEFORE the crop box, because the box is now sized from it.
    L_D, L_D_src = float("nan"), "none"
    ball_px = float((data["dtl_club"].get("lengths", {}) or {}).get("ballPx", -1.0))
    if 1 in ps:
        thF, lnF = interp_face_on(fo_club, ps[1]["t_us"])
        rho_F1 = lnF / data["_L_full"] if data["_L_full"] > 0 else float("nan")
        if math.isfinite(rho_F1):
            rd1, _, _ = schedule(thF, rho_F1)
            if ball_px > 0 and rd1 > 0.5:
                L_D, L_D_src = ball_px / rd1, "ballPx/rho(P1)"
    if not math.isfinite(L_D) and data.get("_truth"):
        d = [math.hypot(f["head"][0] - f["grip"][0], f["head"][1] - f["grip"][1])
             for f in data["_truth"].values()]
        if d:
            L_D, L_D_src = float(np.percentile(d, 90)), "band-truth p90 grip->head"
    if not math.isfinite(L_D):
        pose_px = float((data["dtl_club"].get("lengths", {}) or {}).get("posePx", -1.0))
        if pose_px > 0:
            L_D, L_D_src = pose_px, "DTL posePx"
        else:
            L_D, L_D_src = 0.35 * data["dtl_H"], "0.35*frame height"

    box = golfer_box(data["dtl_pose"], data["dtl_W"], data["dtl_H"], t_lo, t_hi,
                     L_D=L_D, grip_series=(pt, pgx, pgy))

    dcap = cv2.VideoCapture(str(data["dtl_video"]))
    fcap = cv2.VideoCapture(str(data["fo_video"])) if data["fo_video"] else None
    tiles, n_line, n_truth, n_endon = [], 0, 0, 0
    n_sighted, n_off15, n_vs_truth = 0, 0, 0
    bw, bh = max(8, box[2] - box[0]), max(8, box[3] - box[1])
    if tile_px is None:
        tile_size = None
    else:
        tile_size = (max(8, int(round(tile_px * bw / bh))), tile_px)
    for p in P_ORDER:
        entry = ps.get(p)
        if entry is None:
            tiles.append(missing_tile(p, tile_size or (bw, bh)))
            continue
        t_us = int(entry["t_us"])
        frame, fidx = mp.frame_at(dcap, data["dtl_ts"], t_us)
        if frame is None:
            tiles.append(missing_tile(p, tile_size or (bw, bh)))
            continue
        grip = None
        if pt.size:
            grip = (float(np.interp(t_us, pt, pgx)), float(np.interp(t_us, pt, pgy)))
        model = tile_model(data, mode, p, t_us, fidx, grip, L_D)
        model["_L_D"] = L_D
        inset = None
        if fcap is not None:
            fimg, _ = mp.frame_at(fcap, data["fo_ts"], t_us)
            if fimg is not None:
                inset = fimg
        tiles.append(make_tile(frame, box, model, p, inset, tile_size))
        n_line += 1 if model["line"] is not None else 0
        n_truth += 1 if model["truth_theta"] is not None else 0
        n_endon += 1 if model["tier"] == "END_ON" else 0
        rp = model["rho_pred"]
        if math.isfinite(rp) and rp >= RHO_SOLVE_MIN:
            n_sighted += 1
        if model["line"] is not None and model["truth_theta"] is not None:
            n_vs_truth += 1
            d = abs(math.degrees(wrap_pi(model["line"][0] - model["truth_theta"])))
            if d > 15.0:
                n_off15 += 1
    dcap.release()
    if fcap is not None:
        fcap.release()

    h = max(t.shape[0] for t in tiles)
    w = max(t.shape[1] for t in tiles)
    tiles = [t if t.shape[:2] == (h, w) else mp.resize_smart(t, (w, h)) for t in tiles]
    row = np.concatenate(tiles, axis=1)
    # ---- whole-span model: the row header and the timeline strip -----------
    rho_min = RHO_SOLVE_MIN
    if data.get("club_dtl"):
        rho_min = float((data["club_dtl"].get("config", {}) or {})
                        .get("rhoSolveMin", RHO_SOLVE_MIN))
    truth_ts, truth_th = truth_arrays(data)
    tl = annotate_truth(timeline_frames(data, mode), truth_ts, truth_th)
    sp = span_stats(tl, rho_min)

    hdr_txt = (f"{data['sid']}   mode={mode}   L_full(FO)={data['_L_full']:.1f}px   "
               f"L_D={L_D:.1f}px ({L_D_src})   tiles: lines={n_line}/{len(P_ORDER)}   "
               f"band-truth={n_truth}   END-ON={n_endon}")
    hdr2 = (f"ALL {sp['all_n']} DTL frames:  published/sighted="
            f"{sp['all_pub']}/{sp['all_sighted']}"
            f"   truth-paired={sp['all_paired']} (of {sp['all_cov']} truth-covered, "
            f"+-{TL_PAIR_US // 1000} ms)"
            f"   |dtheta| p50={sp['all_p50']:.1f} deg p90={sp['all_p90']:.1f} deg"
            f"   >15 deg={sp['all_off15']}")
    hdr = mp.make_header(row.shape[1], [hdr_txt, hdr2], line_h=24)
    tl_h = TL_H_FULL if tile_px is None else TL_H
    strip = build_timeline(tl, [(p, int(e["t_us"])) for p, e in ps.items()],
                           row.shape[1], tl_h, rho_min)
    return np.concatenate([hdr, row, strip], axis=0), dict(
        n_line=n_line, n_truth=n_truth, n_endon=n_endon, L_D=L_D, L_D_src=L_D_src,
        L_full=data["_L_full"], n_p=len(ps), n_sighted=n_sighted,
        n_off15=n_off15, n_vs_truth=n_vs_truth, box=box, **sp)


# ---------------------------------------------------------------- driver
def run_montage(corpus_dir, fo_runs, dtl_runs, swings, mode, out_prefix,
                summary_md=None, truth_dir=None):
    out_prefix = Path(out_prefix)
    out_prefix.parent.mkdir(parents=True, exist_ok=True)
    rows, stats = [], []
    for sid in swings:
        data, err = load_swing_data(corpus_dir, fo_runs, dtl_runs, sid, mode,
                                    truth_dir=truth_dir)
        if err:
            print(f"[montage_dtl] SKIP {err}")
            continue
        full, st = build_row(data, mode, tile_px=None)
        fp = out_prefix.parent / f"{out_prefix.name}_{sid.split('__')[-1]}_fullres.png"
        cv2.imwrite(str(fp), full)
        row, _ = build_row(data, mode, tile_px=TILE)
        rows.append(row)
        stats.append((sid, st))
        print(f"[montage_dtl] {sid}: tiles {st['n_line']} lines, {st['n_truth']} band-truth, "
              f"{st['n_endon']} END-ON; ALL frames published/sighted="
              f"{st['all_pub']}/{st['all_sighted']} of {st['all_n']}, truth-paired="
              f"{st['all_paired']}, |dth| p50={st['all_p50']:.1f} p90={st['all_p90']:.1f}, "
              f">15deg={st['all_off15']}, L_D={st['L_D']:.1f}px -> {fp.name}")
    if not rows:
        print("[montage_dtl] nothing rendered")
        return []

    width = max(r.shape[1] for r in rows)
    title = mp.make_header(width, [
        f"PinPoint Studio -- DTL shaft montage, mode={mode} ({len(rows)} swings). "
        f"Columns P1..P8,P10 from the FACE-ON ladder; tiles are the nearest DTL frame.",
        LEGEND_BASELINE if mode == "baseline" else LEGEND,
        LEGEND2], line_h=24)
    canvas = mp.stack_rows_capped([title] + rows, max_w=GRID_MAX_W)
    out_png = out_prefix.with_suffix(".png") if out_prefix.suffix == "" else \
        out_prefix.parent / (out_prefix.name + ".png")
    cv2.imwrite(str(out_png), canvas)
    print(f"[montage_dtl] wrote {out_png}")

    if summary_md:
        L = [f"# DTL montage -- mode `{mode}`", "",
             f"{len(rows)} swings. Columns are the face-on P-ladder; each tile is the "
             f"nearest DTL frame, on a fixed per-swing golfer crop.", "",
             mp.__name__ and ""]
        L.append(_md_table(["swing", "P rungs", "tiles: lines", "tiles: band-truth",
                            "tiles: END-ON", "ALL frames", "published/sighted (all)",
                            "truth-paired", "abs dtheta p50 deg", "abs dtheta p90 deg",
                            ">15 deg",
                            "L_full (FO px)", "L_D (DTL px)", "L_D source"],
                           [[sid.split("__")[-1], st["n_p"], st["n_line"], st["n_truth"],
                             st["n_endon"], st["all_n"],
                             f"{st['all_pub']}/{st['all_sighted']}",
                             st["all_paired"],
                             f"{st['all_p50']:.1f}", f"{st['all_p90']:.1f}",
                             st["all_off15"],
                             f"{st['L_full']:.1f}", f"{st['L_D']:.1f}",
                             st["L_D_src"]] for sid, st in stats]))
        p = Path(summary_md)
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text("\n".join(L) + "\n", encoding="utf-8")
        print(f"[montage_dtl] wrote {p}")
    return stats


def _md_table(headers, body):
    out = ["| " + " | ".join(headers) + " |",
           "|" + "|".join(["---"] * len(headers)) + "|"]
    for r in body:
        out.append("| " + " | ".join(str(x) for x in r) + " |")
    return "\n".join(out)


# =================================================================== selftest
def _fab_video(path, W, H, n, fps, draw):
    vw = cv2.VideoWriter(str(path), cv2.VideoWriter_fourcc(*"mp4v"), fps, (W, H))
    if not vw.isOpened():
        path = path.with_suffix(".avi")
        vw = cv2.VideoWriter(str(path), cv2.VideoWriter_fourcc(*"XVID"), fps, (W, H))
    dt = int(round(1e6 / fps))
    ts = []
    for i in range(n):
        ts.append(i * dt)
        img = np.full((H, W, 3), 28, dtype=np.uint8)
        cv2.rectangle(img, (0, int(H * 0.28)), (W, int(H * 0.63)), (70, 70, 70), -1)
        draw(img, i / max(1, n - 1))
        cv2.putText(img, f"f{i}", (6, 18), cv2.FONT_HERSHEY_SIMPLEX, 0.45,
                    (120, 120, 120), 1, cv2.LINE_AA)
        vw.write(img)
    vw.release()
    return path, ts, dt


def fabricate_two_stream_swing(root, name, W=256, H=512, fWf=320, fHf=512,
                               fps=100, n=60, with_club_dtl=True):
    """A two-stream swing + a matching club_dtl.json, enough to render every
    branch of every mode.  No corpus needed."""
    sd = root / "corpus" / name
    fo_dir = root / "fo" / name
    dtl_dir = root / "dtl" / name
    for d in (sd, fo_dir, dtl_dir):
        d.mkdir(parents=True, exist_ok=True)

    def state(frac):
        th = math.radians(-150.0 + 300.0 * frac)
        grip = (0.50 - 0.04 * frac, 0.52 - 0.05 * frac)
        return th, grip

    def draw_dtl(img, frac):
        th, g = state(frac)
        gp = (int(g[0] * W), int(g[1] * H))
        cv2.line(img, gp, (int(gp[0] + 90 * math.cos(th)), int(gp[1] + 90 * math.sin(th))),
                 (230, 230, 230), 2, cv2.LINE_AA)

    def draw_fo(img, frac):
        th, g = state(frac)
        gp = (int(g[0] * fWf), int(g[1] * fHf))
        cv2.line(img, gp, (int(gp[0] + 110 * math.cos(th + 0.6)),
                           int(gp[1] + 110 * math.sin(th + 0.6))), (200, 255, 200), 2, cv2.LINE_AA)

    dtl_v, dtl_ts, dt = _fab_video(sd / "DTL.mp4", W, H, n, fps, draw_dtl)
    fo_v, fo_ts, _ = _fab_video(sd / "Face-On.mp4", fWf, fHf, n, fps, draw_fo)
    fo_ts = [t + 3200 for t in fo_ts]          # the real rig's ~3.2 ms lead
    span = (n - 1) * dt

    def pose_block(W_, H_, ts):
        frames = []
        for i, t in enumerate(ts):
            _, g = state(i / max(1, n - 1))
            kp = [0.0, 0.0, 0.0] * 133
            for idx in (5, 6, 11, 12):
                kp[idx * 3 + 0] = g[0] + (0.06 if idx % 2 else -0.06)
                kp[idx * 3 + 1] = g[1] - (0.18 if idx < 10 else 0.02)
                kp[idx * 3 + 2] = 0.9
            for idx in (9, 10):
                kp[idx * 3 + 0] = g[0] + (0.01 if idx == 9 else -0.01)
                kp[idx * 3 + 1] = g[1]
                kp[idx * 3 + 2] = 0.85
            for idx in (7, 8):
                kp[idx * 3 + 0] = g[0] + (0.05 if idx == 7 else -0.05)
                kp[idx * 3 + 1] = g[1] - 0.09
                kp[idx * 3 + 2] = 0.8
            frames.append({"t_us": int(t), "kp": kp, "handConf": 0.8,
                           "lead": [g[0], g[1]], "trail": [g[0], g[1]]})
        return {"camera": 0, "keypointCount": 133, "frames": frames,
                "synth": [{"t_us": f["t_us"], "kp": f["kp"]} for f in frames],
                "smoothed": [], "decode": "dark", "cropRect": {"x": 0, "y": 0, "w": 1, "h": 1}}

    p_fracs = {1: 0.00, 2: 0.14, 3: 0.28, 4: 0.42, 5: 0.56, 6: 0.68, 7: 0.80, 8: 0.90, 10: 1.0}
    fo_samples, fo_positions = [], []
    for i in range(n):
        frac = i / max(1, n - 1)
        th, g = state(frac)
        # lenPx dips at P2/P4/P6 so the schedule produces real END-ON tiles
        rho = 1.0 if abs(math.sin(th)) > 0.30 else 1.0
        fo_samples.append({"t_us": int(fo_ts[i]), "theta": th, "lenPx": 110.0 * rho,
                           "grip": [g[0], g[1]],
                           "head": [g[0] + 0.2 * math.cos(th), g[1] + 0.2 * math.sin(th)],
                           "conf": 0.7, "flags": SHAFT_MEASURED if i % 7 else 0x04})
    for p, frac in p_fracs.items():
        th, g = state(frac)
        fo_positions.append({"p": p, "t_us": int(3200 + frac * span), "theta": th,
                             "lenPx": 110.0, "conf": 0.6,
                             "grip": [g[0], g[1]],
                             "head": [g[0] + 0.2 * math.cos(th), g[1] + 0.2 * math.sin(th)]})

    (sd / "swing.json").write_text(json.dumps({
        "streams": [
            {"kind": "video", "alias": "DTL", "file": dtl_v.name,
             "setup": {"perspective": 1}, "encoded": {"width": W, "height": H},
             "frames": {"t_us": dtl_ts}},
            {"kind": "video", "alias": "Face-On", "file": fo_v.name,
             "setup": {"perspective": 2}, "encoded": {"width": fWf, "height": fHf},
             "frames": {"t_us": fo_ts}}],
        "capture": {"sessionType": 1}, "analysis": {}}, indent=1), encoding="utf-8")

    (fo_dir / "result.json").write_text(json.dumps({"analysis": {
        "club": {"frameWidth": fWf, "frameHeight": fHf,
                 "samples": fo_samples, "positions": fo_positions,
                 "lengths": {"ballPx": 110.0, "posePx": 120.0}},
        "pose2d": pose_block(fWf, fHf, fo_ts)}}, indent=1), encoding="utf-8")
    (fo_dir / "runmeta.json").write_text('{"ok": true}', encoding="utf-8")

    dtl_samples = [{"t_us": int(dtl_ts[i]), "theta": state(i / max(1, n - 1))[0] + 0.25,
                    "lenPx": 95.0, "conf": 0.5,
                    "grip": [0.5, 0.5], "head": [0.6, 0.6],
                    "flags": SHAFT_MEASURED if i % 3 else 0x04} for i in range(n)]
    (dtl_dir / "result.json").write_text(json.dumps({"analysis": {
        "club": {"frameWidth": W, "frameHeight": H, "samples": dtl_samples,
                 "lengths": {"ballPx": 100.0, "posePx": 130.0}},
        "pose2d": pose_block(W, H, dtl_ts)}}, indent=1), encoding="utf-8")
    (dtl_dir / "runmeta.json").write_text('{"ok": true}', encoding="utf-8")

    # The DTL pose sidecar `swinglab_run --dtl` writes: {"frames": [...]}, the
    # same shape pose_grip_series()/pose_extent_px() already read.  Track mode
    # takes the pose from here, not from the run dir's result.json.
    (dtl_dir / "pose_dtl.json").write_text(
        json.dumps({"frames": pose_block(W, H, dtl_ts)["frames"]}, indent=1),
        encoding="utf-8")

    # A trace WITH band locks, so the white truth line has something to draw --
    # the real dev-six runs have none (E1 disabled, no capture.club).
    with open(dtl_dir / "trace.jsonl", "w", encoding="utf-8") as f:
        for i in range(n):
            th, _ = state(i / max(1, n - 1))
            line = {"frame": i, "phase": 0, "tier": "ray", "theta_dp": math.degrees(th),
                    "theta_out": math.degrees(th), "conf": 0.5}
            if i % 4 == 0:
                line.update({"band_theta": math.degrees(th) + 2.0, "band_s": 0.8,
                             "band_r0": 40.0, "band_n": 3})
            f.write(json.dumps(line) + "\n")
        f.write(json.dumps({"summary": {"impact": n // 2}}) + "\n")

    # A --truth-dir band truth in the dtl_band_truth.py schema.  Every third
    # frame, on the real drawn line, so the white line must land on the club and
    # the "published tile > 15 deg from truth" counter has something to count
    # (the fabricated DTL samples are deliberately 0.25 rad = 14.3 deg off, and
    # club_dtl's frames are on it, so the counter must read 0 in track mode).
    tdir = root / "truth"
    tdir.mkdir(parents=True, exist_ok=True)
    tframes = []
    for i in range(0, n, 3):
        th, g = state(i / max(1, n - 1))
        gp = [g[0] * W, g[1] * H]
        tframes.append({"t_us": int(dtl_ts[i]), "frame": i, "theta": th,
                        "s": 0.30, "r0": 100.0, "grip": gp,
                        "head": [gp[0] + 90 * math.cos(th), gp[1] + 90 * math.sin(th)],
                        "ncc": 0.9, "margin": 0.5, "resp": 70.0, "variant": "V012"})
    (tdir / f"{name}.json").write_text(json.dumps(
        {"swing": name, "faceOnInput": "none", "frames": tframes}, indent=1),
        encoding="utf-8")

    if with_club_dtl:
        tiers = ["BAND", "SEG", "RAY", "END_ON", "OCCLUDED", "UNSEEN"]
        frames = []
        for i in range(n):
            th, g = state(i / max(1, n - 1))
            tier = tiers[i % len(tiers)]
            has = tier in ("BAND", "SEG", "RAY")
            frames.append({
                "t_us": int(dtl_ts[i]), "tier": tier,
                "grip": [g[0], g[1]] if has else None,
                "head": [g[0] + 0.2 * math.cos(th), g[1] + 0.2 * math.sin(th)] if has else None,
                "theta": th if has else None, "lenPx": 95.0 if has else 0.0,
                "conf": 0.6 if has else 0.0,
                "rhoPred": 0.95 if has else 0.2,
                # the real tracker writes corridor null, or an object with `on`
                # false, wherever the gate turned it off -- both must render
                # WITHOUT the dashed cyan lines
                "corridor": (None if i % 5 == 0 else
                             {"centres": [th - 0.1, th + 0.1], "half": 0.4,
                              "on": i % 5 != 1}),
                "escape": bool(i % 11 == 0), "band": i % 3 - 1, "reason": tier.lower()})
        (dtl_dir / "club_dtl.json").write_text(json.dumps({
            "schema": CLUB_DTL_SCHEMA, "stageVersion": 1,
            "stream": {"alias": "DTL", "file": dtl_v.name},
            "frameWidth": W, "frameHeight": H, "clockOffsetUs": -3200,
            "config": {"rhoSolveMin": RHO_SOLVE_MIN}, "frames": frames,
            "bands": [{"lo_us": 0, "hi_us": span // 2, "name": "backswing"},
                      {"lo_us": span // 2, "hi_us": span, "name": "downswing"}],
            "truth": [{"t_us": int(dtl_ts[i]), "theta": state(i / max(1, n - 1))[0],
                       "s": 0.8, "r0": 40.0, "grip": [0.5, 0.5], "head": [0.6, 0.6]}
                      for i in range(0, n, 4)],
            "summary": {"sightedFrac": 0.5,
                        "coverageByBand": {"backswing": 0.6, "downswing": 0.4},
                        "publishedInEndOn": 0}}, indent=1), encoding="utf-8")
    return sd, fo_dir, dtl_dir


def selftest(tmpdir):
    tmpdir = Path(tmpdir)
    if tmpdir.exists():
        shutil.rmtree(tmpdir)
    tmpdir.mkdir(parents=True)
    print(f"[selftest] fabricating a two-stream swing under {tmpdir}")
    fabricate_two_stream_swing(tmpdir, "selftest_swing")
    fabricate_two_stream_swing(tmpdir, "selftest_noclubdtl", with_club_dtl=False)

    ok = True

    def check(cond, msg):
        nonlocal ok
        print(f"[selftest] {'PASS' if cond else 'FAIL'}: {msg}")
        ok = ok and bool(cond)

    for mode in ("baseline", "probe", "track"):
        pref = tmpdir / "out" / f"m_{mode}"
        stats = run_montage(tmpdir / "corpus", tmpdir / "fo", tmpdir / "dtl",
                            ["selftest_swing"], mode, pref)
        png = pref.parent / (pref.name + ".png")
        check(png.exists() and png.stat().st_size > 20_000,
              f"{mode}: montage png rendered ({png.stat().st_size if png.exists() else 0} B)")
        fr = pref.parent / f"{pref.name}_selftest_swing_fullres.png"
        check(fr.exists() and fr.stat().st_size > 20_000, f"{mode}: fullres strip rendered")
        check(len(stats) == 1, f"{mode}: one row of stats")
        if stats:
            st = stats[0][1]
            check(st["n_p"] == len(P_ORDER), f"{mode}: all {len(P_ORDER)} P rungs present")
            if mode == "baseline":
                check(st["n_line"] == len(P_ORDER), "baseline: a line on every tile")
            if mode == "probe":
                check(st["n_truth"] > 0, "probe: band-truth line drawn on >=1 tile")
            if mode == "track":
                check(st["n_line"] > 0 and st["n_endon"] >= 0,
                      "track: club_dtl.json tiers rendered")
        img = cv2.imread(str(png))
        check(img is not None and img.shape[1] <= GRID_MAX_W,
              f"{mode}: width {img.shape[1] if img is not None else -1} <= {GRID_MAX_W}")
        # The tiles must not be blank: a rendered row has real variance.
        check(img is not None and float(img.std()) > 12.0,
              f"{mode}: montage has image content (std {float(img.std()):.1f})")

    # ---- the tiles must be full frame width and reach the ball row ----------
    data, err = load_swing_data(tmpdir / "corpus", tmpdir / "fo", tmpdir / "dtl",
                                "selftest_swing", "probe",
                                truth_dir=tmpdir / "truth")
    check(err is None, f"load_swing_data ok ({err})")
    if data is not None:
        full, st = build_row(data, "probe", tile_px=None)
        # the row is 9 tiles wide, so a tile is the full DTL frame width
        tile_w = full.shape[1] / len(P_ORDER)
        check(abs(tile_w - data["dtl_W"]) < 2.0,
              f"tile width {tile_w:.0f} == full DTL frame width {data['dtl_W']}")
        bx0, by0, bx1, by1 = st["box"]
        check(bx0 == 0 and bx1 == data["dtl_W"], "crop keeps the full frame width")
        check(by1 >= BALL_ROW_FRAC * data["dtl_H"] - 1,
              f"crop bottom row {by1} reaches the ball row "
              f"({BALL_ROW_FRAC * data['dtl_H']:.0f})")
        pt_, pgx_, pgy_ = pose_grip_series(data["dtl_pose"], data["dtl_W"], data["dtl_H"])
        reach = CLUB_REACH * st["L_D"]
        check(by0 <= pgy_.min() - reach + 1 and by1 >= pgy_.max() + reach - 1,
              f"crop rows {by0}..{by1} contain grip +- {reach:.0f} px "
              f"({pgy_.min() - reach:.0f}..{pgy_.max() + reach:.0f})")

    # ---- --truth-dir: the white line is drawn in EVERY mode -----------------
    for mode in ("baseline", "probe", "track"):
        d2, e2 = load_swing_data(tmpdir / "corpus", tmpdir / "fo", tmpdir / "dtl",
                                 "selftest_swing", mode, truth_dir=tmpdir / "truth")
        check(e2 is None and len(d2["_truth"]) > 0,
              f"{mode}: --truth-dir band truth loaded ({len(d2['_truth']) if d2 else 0} frames)")
        _, st2 = build_row(d2, mode, tile_px=TILE)
        check(st2["n_truth"] > 0, f"{mode}: truth line drawn on {st2['n_truth']} tiles")
        if mode == "track":
            check(st2["n_sighted"] > 0, "track: sighted tiles counted")
            check(st2["n_vs_truth"] > 0, "track: published tiles compared to truth")
            check(st2["n_off15"] == 0,
                  f"track: 0 published tiles >15 deg from truth (got {st2['n_off15']})")
        if mode == "baseline":
            # the fabricated baseline samples sit 0.25 rad = 14.3 deg off truth,
            # i.e. just inside the gate -- so the counter must read 0 there too
            check(st2["n_vs_truth"] > 0, "baseline: published tiles compared to truth")

    d3, _ = load_swing_data(tmpdir / "corpus", tmpdir / "fo", tmpdir / "dtl",
                            "selftest_swing", "probe", truth_dir=None)
    check(len(d3["_truth"]) == 0, "no --truth-dir: falls back to the trace band lock")

    stats = run_montage(tmpdir / "corpus", tmpdir / "fo", tmpdir / "dtl",
                        ["selftest_noclubdtl"], "track", tmpdir / "out" / "m_missing")
    check(stats == [], "track: a swing with no club_dtl.json is skipped, not crashed")

    md = tmpdir / "out" / "summary.md"
    run_montage(tmpdir / "corpus", tmpdir / "fo", tmpdir / "dtl",
                ["selftest_swing"], "probe", tmpdir / "out" / "m_md", summary_md=md)
    mdtxt = md.read_text(encoding="utf-8") if md.exists() else ""
    check("L_D source" in mdtxt, "summary markdown written")
    check("abs dtheta p90 deg" in mdtxt and "published/sighted (all)" in mdtxt,
          "summary markdown carries the whole-span truth columns (item 5)")

    # ================= work package A3 =====================================
    # ---- item 1: the rails leave the shaft's own pixels visible ------------
    canvas = np.full((90, 220, 3), 30, dtype=np.uint8)
    cv2.line(canvas, (20, 45), (200, 45), (250, 250, 250), 5, cv2.LINE_AA)
    before = canvas.copy()
    draw_rails(canvas, (20.0, 45.0), (200.0, 45.0), COL_RAY, 1.0)
    mid = slice(70, 150)
    check(bool((canvas[45, mid] == before[45, mid]).all()),
          "rails: the shaft's own centre pixels are NOT painted over")
    off = int(round(RAIL_OFF_PX))
    hit_up = int(np.abs(canvas[45 - off, mid].astype(int)
                        - before[45 - off, mid].astype(int)).max())
    hit_dn = int(np.abs(canvas[45 + off, mid].astype(int)
                        - before[45 + off, mid].astype(int)).max())
    check(hit_up > 20 and hit_dn > 20,
          f"rails: both rails painted at +-{off} px (up {hit_up}, down {hit_dn})")
    # the dark keyline is there, so a rail reads on a white shaft
    strip_up = canvas[45 - off - 3:45 - off + 4, mid].reshape(-1, 3)
    check(bool((strip_up.sum(axis=1) < 90).any()),
          "rails: a dark keyline sits under the rail")
    # head end = OPEN circle (its centre is not filled), grip end = filled dot
    check(bool((canvas[45, 200] == before[45, 200]).all()),
          "rails: the head marker is an OPEN circle (centre untouched)")
    check(not bool((canvas[45, 20] == before[45, 20]).all()),
          "rails: the grip marker is a FILLED dot")

    # scale: the offset follows the tile scale, so it is 6 px AFTER the resize
    canvas2 = np.full((90, 220, 3), 30, dtype=np.uint8)
    before2 = canvas2.copy()
    draw_rails(canvas2, (20.0, 45.0), (200.0, 45.0), COL_RAY, 2.0)
    d2rows = [r for r in range(20, 45)
              if np.abs(canvas2[r, mid].astype(int) - before2[r, mid].astype(int)).max() > 20]
    check(bool(d2rows) and abs((45 - max(d2rows)) - 2 * off) <= 3,
          f"rails: at scale 2 the offset doubles (rows {d2rows[:3] if d2rows else []})")

    # ---- item 1: truth is a centre line with end ticks --------------------
    tcanvas = np.full((90, 220, 3), 30, dtype=np.uint8)
    draw_truth_seg(tcanvas, (20.0, 45.0), (200.0, 45.0), 1.0)
    check(int(tcanvas[45, 110].max()) > 200, "truth: white centre line drawn")
    tick = int(round(TRUTH_TICK_PX))
    check(int(tcanvas[45 - tick + 1, 200].max()) > 120 or
          int(tcanvas[45 + tick - 1, 200].max()) > 120,
          "truth: perpendicular end ticks drawn")

    # ---- item 1: the corridor centre is dashed ---------------------------
    dcanvas = np.zeros((20, 220, 3), dtype=np.uint8)
    draw_dashed(dcanvas, (5.0, 10.0), (215.0, 10.0), COL_CORR, 1.0)
    lit = dcanvas[10, 5:215].max(axis=1)
    check(bool((lit == 0).any()) and bool((lit > 0).any()), "corridor: dashed, not solid")
    check(abs(CORR_LEN_FRAC - 0.6) < 1e-9, "corridor: shortened to 0.6 of its length")

    # ---- item 3: the delta grading ---------------------------------------
    check(delta_colour(3.0) == COL_OK and delta_colour(9.0) == COL_WARN
          and delta_colour(40.0) == COL_BAD, "caption delta graded green/amber/red")

    # ---- items 2/4/5: the whole-span model and the timeline strip ---------
    d5, e5 = load_swing_data(tmpdir / "corpus", tmpdir / "fo", tmpdir / "dtl",
                             "selftest_swing", "track", truth_dir=tmpdir / "truth")
    check(e5 is None, f"track: swing loaded for the timeline checks ({e5})")
    row5, st5 = build_row(d5, "track", tile_px=TILE)
    n_cd = len(d5["club_dtl"]["frames"])
    check(st5["all_n"] == n_cd,
          f"span: every club_dtl frame counted ({st5['all_n']} of {n_cd})")
    check(st5["all_pub"] == n_cd // 2,
          f"span: published over ALL frames = {st5['all_pub']} (expected {n_cd // 2})")
    check(st5["all_paired"] > 0 and st5["all_off15"] == 0,
          f"span: {st5['all_paired']} frames paired to truth within "
          f"{TL_PAIR_US // 1000} ms, {st5['all_off15']} over 15 deg")
    check(math.isfinite(st5["all_p50"]) and math.isfinite(st5["all_p90"]),
          f"span: |dtheta| p50={st5['all_p50']:.2f} p90={st5['all_p90']:.2f}")
    tlf = timeline_frames(d5, "track")
    check(any(f["reason"] for f in tlf if f["tier"] in REASON_TIERS),
          "item 2: UNSEEN/OCCLUDED frames carry a `reason` to stamp")

    strip = row5[-TL_H:, :, :]
    check(row5.shape[0] > TILE + TL_H, "timeline: a strip is appended under the row")
    check(float(strip.std()) > 8.0,
          f"timeline: the strip has content (std {float(strip.std()):.1f})")
    cols = {tuple(int(v) for v in c) for c in strip.reshape(-1, 3).tolist()}
    for t in ("BAND", "SEG", "RAY", "END_ON", "OCCLUDED", "UNSEEN"):
        check(tuple(TIER_COLOR[t]) in cols, f"timeline: a {t} column is drawn")
    check(tuple(COL_TRUTH) in cols, "timeline: truth-covered frames marked white")

    # baseline and probe get a timeline too (they share the row code)
    for mode in ("baseline", "probe"):
        d6, _ = load_swing_data(tmpdir / "corpus", tmpdir / "fo", tmpdir / "dtl",
                                "selftest_swing", mode, truth_dir=tmpdir / "truth")
        row6, st6 = build_row(d6, mode, tile_px=TILE)
        check(st6["all_n"] == len(d6["dtl_ts"]),
              f"{mode}: span model covers all {st6['all_n']} DTL frames")
        check(float(row6[-TL_H:, :, :].std()) > 6.0, f"{mode}: timeline strip rendered")
    fs = build_timeline([], [], 400, TL_H_FULL, 0.5)
    check(fs.shape[:2] == (TL_H_FULL, 400), "timeline: an empty span still renders")

    # ---- track reads the DTL sidecars, never the run dir's result.json ------
    # `swinglab_run --dtl` leaves the FACE-ON result.json in the DTL run dir.
    # Its analysis.club is the face-on frame size and its analysis.pose2d is the
    # face-on pose; taking either scaled club_dtl's normalised grip/head wrongly
    # and drew the rails off the shaft.  Render, plant that file, render again.
    d7, e7 = load_swing_data(tmpdir / "corpus", tmpdir / "fo", tmpdir / "dtl",
                             "selftest_swing", "track", truth_dir=tmpdir / "truth")
    check(e7 is None, f"track: swing loaded for the sidecar check ({e7})")
    dims7 = (d7["dtl_W"], d7["dtl_H"])
    check(bool(d7["dtl_pose"].get("frames")),
          "track: the DTL pose comes from pose_dtl.json")
    row7, _ = build_row(d7, "track", tile_px=TILE)

    dtl_run_dir = tmpdir / "dtl" / "selftest_swing"
    good_res = (dtl_run_dir / "result.json").read_text(encoding="utf-8")
    bogus = json.loads(good_res)
    bogus["analysis"]["club"]["frameWidth"] = dims7[0] * 4
    bogus["analysis"]["club"]["frameHeight"] = dims7[1] // 2
    for f in bogus["analysis"]["pose2d"]["frames"]:
        f["kp"] = [0.87] * len(f["kp"])
    bogus["analysis"]["pose2d"]["synth"] = []
    (dtl_run_dir / "result.json").write_text(json.dumps(bogus, indent=1), encoding="utf-8")
    d8, e8 = load_swing_data(tmpdir / "corpus", tmpdir / "fo", tmpdir / "dtl",
                             "selftest_swing", "track", truth_dir=tmpdir / "truth")
    check(e8 is None, f"track: swing loaded with a face-on result.json planted ({e8})")
    check((d8["dtl_W"], d8["dtl_H"]) == dims7,
          f"track: frame size stays {dims7} (got {(d8['dtl_W'], d8['dtl_H'])})")
    check(d8["dtl_pose"].get("frames")
          and d8["dtl_pose"]["frames"][0]["kp"][:3] != [0.87, 0.87, 0.87],
          "track: the planted result.json pose is ignored")
    row8, _ = build_row(d8, "track", tile_px=TILE)
    check(row8.shape == row7.shape and bool(np.array_equal(row8, row7)),
          "track: a face-on result.json in the DTL run dir does not move the rails")
    (dtl_run_dir / "result.json").write_text(good_res, encoding="utf-8")

    # ---- track: a corridor the tracker gated off is not drawn --------------
    d9, e9 = load_swing_data(tmpdir / "corpus", tmpdir / "fo", tmpdir / "dtl",
                             "selftest_swing", "track", truth_dir=tmpdir / "truth")
    check(e9 is None, f"track: swing loaded for the corridor checks ({e9})")
    fr9 = d9["club_dtl"]["frames"]
    fts9 = np.array([f["t_us"] for f in fr9], dtype=np.int64)
    ps9 = mp.positions_by_p(d9["fo_club"].get("positions", []) or [])
    recs9 = [fr9[nearest_index(fts9, int(ps9[p]["t_us"]))] for p in P_ORDER]

    def corridor_on(rec):
        co = rec.get("corridor")
        return isinstance(co, dict) and bool(co.get("on"))

    off_j = next((j for j, r in enumerate(recs9) if not corridor_on(r)), None)
    check(off_j is not None and any(corridor_on(r) for r in recs9),
          "fixture: the fabricated swing has both corridor-on and corridor-off tiles")
    tw9 = d9["dtl_W"]

    def cyan_px(row):
        """Cyan-ish pixels per tile -- COL_CORR is anti-aliased, and nothing
        else on a fabricated tile is blue+green without red."""
        band = row[:row.shape[0] - TL_H_FULL].astype(int)
        m = (band[:, :, 0] > 120) & (band[:, :, 1] > 120) & (band[:, :, 2] < 90)
        return [int(m[:, j * tw9:(j + 1) * tw9].sum()) for j in range(len(P_ORDER))]

    row9, _ = build_row(d9, "track", tile_px=None)
    cy = cyan_px(row9)
    check(sum(cy) > 0, f"corridor: the corridor-on tiles still draw it ({sum(cy)} px)")
    check(off_j is not None and cy[off_j] == 0,
          f"track: no corridor is drawn where the frame's corridor is off "
          f"(tile P{P_ORDER[off_j] if off_j is not None else '?'}, "
          f"{cy[off_j] if off_j is not None else -1} px)")

    # ---- track: the refusal reason is set at the caption's size ------------
    rec9 = recs9[off_j if off_j is not None else 0]
    saved9 = {k: rec9.get(k) for k in ("tier", "theta", "grip", "head", "lenPx",
                                       "reason")}
    jr = off_j if off_j is not None else 0
    reason_txt = "anchor quarantined: row residual 54 px, corridor gate off"
    rec9.update(tier="UNSEEN", theta=None, grip=None, head=None, lenPx=0.0,
                reason=reason_txt)
    row_a, st_a = build_row(d9, "track", tile_px=None)
    rec9["reason"] = ""
    row_b, _ = build_row(d9, "track", tile_px=None)
    th9 = st_a["box"][3] - st_a["box"][1]                 # the tile, without the
    hdr9 = row_a.shape[0] - TL_H_FULL - th9               # header or the strip
    ta = row_a[hdr9:hdr9 + th9, jr * tw9:(jr + 1) * tw9].astype(int)
    tb = row_b[hdr9:hdr9 + th9, jr * tw9:(jr + 1) * tw9].astype(int)
    ys9, xs9 = np.nonzero(np.abs(ta - tb).max(axis=2) > 0)
    check(ys9.size > 0, "track: the reason is drawn on an absence tile")
    sc_cap = max(0.42, min(0.8, tw9 / 520.0))
    wrapped = wrap_text(reason_txt, sc_cap, tw9 - 12)
    check(len(wrapped) == 2, f"track: a long reason wraps onto two lines ({wrapped})")
    exp_w = max(_text_w(t, sc_cap) for t in wrapped) + 4
    got_w = int(xs9.max() - xs9.min() + 1) if xs9.size else 0
    check(abs(got_w - exp_w) <= 8,
          f"track: the reason is drawn at the caption's size "
          f"(width {got_w} px, caption width {exp_w} px)")
    fine_w = max(_text_w(t, sc_cap * 0.78) for t in wrapped) + 4
    check(got_w > fine_w + 6,
          f"track: the reason is bigger than the old fine print ({got_w} > {fine_w})")
    check(ys9.size and int(ys9.min()) < 0.25 * ta.shape[0],
          f"track: the reason sits under the tier stamp (top row {int(ys9.min())})")
    check("END_ON" in REASON_TIERS, "track: END-ON tiles print their reason too")
    check(_ascii("end-on ρ̂=0.48") == "end-on rho^=0.48",
          "the Greek rho-hat in a reason is spelled out, not replaced")
    rec9.update(saved9)

    print(f"[selftest] {'ALL PASS' if ok else 'SOME CHECKS FAILED'}")
    return ok


# --------------------------------------------------------------------- CLI
def main():
    ap = argparse.ArgumentParser(prog="montage_dtl.py", description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--mode", choices=("baseline", "probe", "track"), default="probe")
    ap.add_argument("--corpus", default="/mnt/swingdata/corpus/swings")
    ap.add_argument("--fo-runs", default="build/dtl/fo")
    ap.add_argument("--dtl-runs", default="build/dtl/baseline")
    ap.add_argument("--swings", default=None, help="comma-separated run ids")
    ap.add_argument("--out-prefix", default=None)
    ap.add_argument("--summary-md", default=None)
    ap.add_argument("--truth-dir", default=None,
                    help="docs/research/data/dtl/band_truth -- the face-on-"
                         "independent DTL band truth written by "
                         "tools/shaftlab/dtl_band_truth.py.  Drawn as a thin "
                         "white line in EVERY mode.")
    ap.add_argument("--selftest", default=None, metavar="TMPDIR")
    args = ap.parse_args()

    if args.selftest:
        sys.exit(0 if selftest(args.selftest) else 1)
    if not args.swings or not args.out_prefix:
        ap.error("--swings and --out-prefix are required (unless --selftest TMPDIR)")
    swings = [s.strip() for s in args.swings.split(",") if s.strip()]
    stats = run_montage(args.corpus, args.fo_runs, args.dtl_runs, swings, args.mode,
                        args.out_prefix, args.summary_md, truth_dir=args.truth_dir)
    sys.exit(0 if stats else 1)


if __name__ == "__main__":
    main()
