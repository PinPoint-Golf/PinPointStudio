#!/usr/bin/env python3
"""dtl_band_truth.py -- a face-on-INDEPENDENT truth instrument for the taped
shaft in the down-the-line view.

Why this exists
---------------
Design `docs/design/dtl_shaft_tracker_design.md` Sec 6 nominates the E1 band lock
on the DTL stream as the instrument that grades the whole face-on -> DTL coupling.
Stage 0 (`docs/research/data/dtl/stage0_probe_summary.md`) measured that it does
not work here: the DTL camera has no ring light, so the tape reads as ordinary
white bands between black tape over a mid-grey shaft, not as saturated retro
blobs, and E1's saturation threshold locks on 0-6 frames per swing even with the
correct band geometry injected.  Stage 0's stand-in -- an unconditioned "longest
attached ridge" scan -- is a NEGATIVE control: it locks the lead forearm, the
trail leg and the mat's alignment stick, at 46-153 deg p50 from the truth.

This tool is the replacement instrument.  It uses NO face-on input of any kind:
no face-on track, no face-on phases, no face-on timing, no P-ladder.  Its only
inputs are the DTL video, the DTL stream's own frame times, and the cached DTL
pose (for a grip anchor).  It is a TEMPLATE MATCH, deliberately: the alternation
of white bands and black tape at known millimetre positions is a signature that a
forearm or a trouser crease cannot counterfeit, and a bare "longest line from the
hands" search demonstrably locks the forearms.

What the pattern actually looks like (measured, not assumed)
------------------------------------------------------------
Profile taken along a hand-fitted shaft line on the address frame of
07-04 swing_0006, mapped to millimetres from the butt by a linear fit to the six
white-band centres (residual rms ~1.5 px over a 214 px baseline):

    260-267 mm   bright  -- the white grip end, below the hands
    270-292 mm   BLACK tape
    295-322 mm   WHITE band          (nominal centre 308)
    325-347 mm   BLACK tape
    350-373 mm   WHITE band          (nominal centre 362)
    378-398 mm   BLACK tape
    400-545 mm   bare steel, a bright mid-grey line, NOT neutral
    550-578 mm   WHITE band          (nominal centre 560)
    582-730 mm   bare steel
    748-775 mm   WHITE band          (nominal centre 758)
    778-797 mm   BLACK tape
    799-822 mm   WHITE band          (nominal centre 808)
    824-842 mm   BLACK tape
    844-868 mm   WHITE band          (nominal centre 854)
    869-882 mm   BLACK tape to the hosel

So the stated geometry is right, and the structure is three GROUPS -- a pair, a
singleton, a trio -- separated by long bare-steel runs.  Two things follow and
both are built into the template:

  * bare steel is strongly POSITIVE against its lateral background (+20..+160
    grey levels here), comparable to the white bands themselves, so amplitude
    alone does not separate band from shaft.  The discriminator is the
    ALTERNATION, not the brightness.
  * the black tape is only negative when the local background is mid-grey; over
    the dark strip behind the mat it reads ~0.  Each group's template is
    therefore made ZERO-MEAN over its own lobes (the black lobes carry a weight
    that balances the white ones), so any smoothly varying background or steel
    level contributes nothing to the correlation.  Polarity is preserved -- white
    positive, black negative -- but only the local alternation is scored.

Method
------
For each posed frame:
  candidate lines = direction theta over the full 360 deg  x  a lateral offset of
  the line's origin along the normal (the pose grip sits up to ~40 px off the
  shaft axis).  Along each candidate a 1-D SIGNED profile
      c(r) = mean(on-line, +-1 px) - median(lateral, +-9 / +-12 px)
  is sampled at 1 px steps.  c is correlated with the band template over a grid
  of scale s (px/mm) and butt offset r0 (mm behind the line origin), by masked
  normalised cross-correlation on the taped groups only.  A coarse pass (2 deg,
  17 offsets) over the whole circle supplies seeds; the top three are refined,
  and so -- separately -- are the best seeds more than 15 deg from the winner,
  because the MARGIN must difference two candidates measured the same way.

A frame is accepted only if all of:
    best NCC >= --ncc-min
    margin over the best candidate more than 15 deg away >= --margin-min
    lobe-support contrast rms >= --amp-min           (noise cannot lock)
    s in [--s-min, --s-max] and band width s*25 mm >= 4.5 px
    implied butt 60..260 mm behind the line origin (the attachment gate)
    at least 2 of the 3 groups fully inside the image
    temporal: part of a run of >= --run-min frames continuous in theta, s AND
              r0 (20 deg, 4% of s, 20 mm per frame gap)

It abstains on most frames.  That is the design: one wrong truth entry poisons
everything downstream.

Outputs
-------
  <out-dir>/<id>.json         per-swing accepted frames
  <out-dir>/band_truth.csv    pooled, one row per accepted frame
  <out-dir>/band_truth_summary.md
  <sheets-dir>/01b_band_truth_<swing>.png   adjudication contact sheets

Deterministic; no RNG anywhere.

Usage
  dtl_band_truth.py --selftest
  dtl_band_truth.py --swings <id,...> [--corpus ...] [--pose-dir ...]
                    [--out-dir docs/research/data/dtl/band_truth]
                    [--sheets-dir ~/Desktop/DTL-shaft-tracker]
"""
import argparse
import json
import math
import os
import sys
from pathlib import Path

import numpy as np
import cv2

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from pp_swingdoc import load_swing, has_swing  # noqa: E402

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
sys.stderr.reconfigure(encoding="utf-8", errors="replace")

# ------------------------------------------------------------------ club model
# Lab 7-iron, millimetres from the butt.  Confirmed against the measured profile
# in the module docstring; do not "improve" these without re-measuring.
BANDS_MM = (308.0, 362.0, 560.0, 758.0, 808.0, 854.0)
BAND_W_MM = 25.0
GROUPS_MM = ((308.0, 362.0), (560.0,), (758.0, 808.0, 854.0))
CLUB_MM = 940.0
HOSEL_MM = 882.0
GRIP_END_MM = 265.0
LEAD_MM = 20.0          # black tape credited either side of a group

# ------------------------------------------------------------------ search grid
# Three stages.  A scores the whole circle at 2 deg and supplies seeds; B
# refines a handful of them; C polishes the winner.  A uses the SAME lateral
# aperture as B and C -- an earlier version scored A with a wide aperture for
# speed, and both the winner and the margin then came out wrong, because a wide
# aperture prefers a broad forearm where a sharp one prefers the 3 px shaft.
# One score, everywhere.
COARSE_DEG = 2.0
COARSE_OFFSETS = np.arange(-40.0, 40.1, 5.0)
COARSE_LAT_ON = (-2.0, -1.0, 0.0, 1.0, 2.0)
MID_DEG = 0.5
MID_SPAN_DEG = 4.0
MID_OFFSETS = np.arange(-40.0, 40.1, 4.0)
MARGIN_FAR_SEEDS = 4   # far rivals refined to stage-B resolution for the margin
FINE_DEG = 0.25
FINE_SPAN_DEG = 0.75
LAT_ON = (-1.0, 0.0, 1.0)
LAT_BG = (-12.0, -9.0, 9.0, 12.0)
COARSE_LAT_BG = LAT_BG
R_LO, R_HI = -30.0, 430.0          # ray sample span, px
C_CLIP = 90.0                      # signed contrast clip, grey levels
MARGIN_SEP_DEG = 15.0
MIN_BAND_PX = 4.5                  # a 25 mm band narrower than this cannot lock
GROUP_VALID_FRAC = 0.90            # of a group's lobe samples must be in-image
MAX_STEP_DEG_PER_FRAME = 20.0      # temporal continuity allowance, theta
MAX_DS_PER_FRAME = 0.04            # ... projected scale, as a fraction of s
MAX_DR0_MM_PER_FRAME = 20.0        # ... butt offset, mm

DEF_S_MIN, DEF_S_MAX = 0.18, 0.48
DEF_NCC_MIN = 0.70
DEF_MARGIN_MIN = 0.12
DEF_RESP_MIN = 40.0
DEF_AMP_MIN = 12.0
DEF_RUN_MIN = 3
# The attachment gate, both ends.  r0 is where the pose WRIST MIDPOINT sits along
# the club, in mm from the butt.  The hands occupy the top ~230 mm of a 265 mm
# grip and the wrist keypoint is proximal to the hand centre, so r0 below ~60 mm
# would put the whole hand off the end of the butt cap, and above 260 mm would
# put it off the grip onto bare shaft.  Both are physically impossible.  The
# measured distribution over accepted frames is 117-136 mm, well inside.  This
# gate is what removes the last false family: torso and trouser-seam fits that
# put the butt AT the wrists and the head up at the shoulder (swing_0004 frames
# 213-215, r0 = 19-24 mm, s = 0.295 against the same swing's true 0.388).
DEF_R0_MIN, DEF_R0_MAX = 60.0, 260.0
DEF_VARIANTS = "V012,V12,V01"

POSE_W, POSE_H = 512, 1024         # the cached DTL pose is normalised to this


# ------------------------------------------------------------------ template
def build_variants():
    """Lobe lists for the three group subsets.

    A lobe is (mm_lo, mm_hi, weight).  Within each group the positive (white)
    lobes and the negative (black) lobes are balanced so the group's template
    integrates to zero -- that is what makes a smoothly varying steel or
    background level contribute nothing to the correlation.
    """
    def group_lobes(g):
        hw = BAND_W_MM / 2.0
        white = [(c - hw, c + hw) for c in g]
        black = []
        lo = max(GRIP_END_MM, g[0] - hw - LEAD_MM)
        if g[0] - hw - lo > 4.0:
            black.append((lo, g[0] - hw))
        for a, b in zip(g, g[1:]):
            if b - a - BAND_W_MM > 4.0:
                black.append((a + hw, b - hw))
        hi = min(HOSEL_MM, g[-1] + hw + LEAD_MM)
        if hi - (g[-1] + hw) > 4.0:
            black.append((g[-1] + hw, hi))
        wlen = sum(b - a for a, b in white)
        blen = sum(b - a for a, b in black)
        w = -(wlen / blen) if blen > 0 else 0.0
        return ([(a, b, 1.0) for a, b in white] +
                [(a, b, w) for a, b in black])

    gl = [group_lobes(g) for g in GROUPS_MM]
    return {
        "V012": [gl[0], gl[1], gl[2]],
        "V01": [gl[0], gl[1]],
        "V12": [gl[1], gl[2]],
    }


VARIANTS = build_variants()


# ------------------------------------------------------------------ small utils
def wrap_deg(a):
    return (np.asarray(a, dtype=float) + 180.0) % 360.0 - 180.0


def load_json(p):
    with open(p, encoding="utf-8") as f:
        return json.load(f)


def swing_dir_for(corpus_root, sid):
    corpus_root = Path(corpus_root)
    if has_swing(corpus_root / sid):
        return corpus_root / sid
    if "__" in sid:
        a, b = sid.split("__", 1)
        if has_swing(corpus_root / a / b):
            return corpus_root / a / b
    return corpus_root / sid


def pick_dtl_stream(doc):
    vids = [s for s in doc.get("streams", []) if s.get("kind") == "video"]
    for s in vids:
        if s.get("setup", {}).get("perspective") == 1:
            return s
    for s in vids:
        blob = (s.get("alias", "") + " " + s.get("file", "")).lower()
        if "dtl" in blob or "down" in blob:
            return s
    return None


# ------------------------------------------------------------------ the matcher
class BandMatcher:
    """Masked NCC of the signed lateral-contrast profile against the band
    template, over a grid of (theta, lateral offset, s, r0)."""

    def __init__(self, s_min, s_max, r0_max=260.0, variants=("V012",)):
        self.r_grid = np.arange(R_LO, R_HI + 0.5, 1.0, dtype=np.float32)
        self.n_r = len(self.r_grid)
        self.s_min, self.s_max = s_min, s_max
        self.s_coarse = np.arange(s_min, s_max + 1e-9, 0.02)
        self.r0_coarse = np.arange(0.0, r0_max + 1e-9, 6.0)
        self.s_vals = self.s_coarse
        self.r0_vals = self.r0_coarse
        self.variants = list(variants)

    # -- sampling -----------------------------------------------------------
    def profiles(self, img, gx, gy, thetas_rad, offsets,
                 lat_on=LAT_ON, lat_bg=LAT_BG):
        """-> c (nray, n_r) signed contrast, m (nray, n_r) validity mask,
        and the per-ray (theta, offset) index arrays."""
        H, W = img.shape
        th = np.asarray(thetas_rad, dtype=np.float32)
        off = np.asarray(offsets, dtype=np.float32)
        ux = np.cos(th)[:, None]
        uy = np.sin(th)[:, None]
        nx, ny = -uy, ux
        # ray origins: (n_theta, n_off)
        ox = gx + off[None, :] * nx
        oy = gy + off[None, :] * ny
        ox = ox.reshape(-1)[:, None]
        oy = oy.reshape(-1)[:, None]
        uxr = np.repeat(ux, len(off), axis=0)
        uyr = np.repeat(uy, len(off), axis=0)
        nxr, nyr = -uyr, uxr
        R = self.r_grid[None, :]

        def samp(lats, reduce_fn):
            vs = []
            for l in lats:
                X = (ox + R * uxr + l * nxr).astype(np.float32)
                Y = (oy + R * uyr + l * nyr).astype(np.float32)
                vs.append(cv2.remap(img, X, Y, cv2.INTER_LINEAR,
                                    borderMode=cv2.BORDER_CONSTANT,
                                    borderValue=float("nan")))
            return reduce_fn(np.stack(vs), axis=0)

        with np.errstate(invalid="ignore"):
            on = samp(lat_on, np.nanmean)
            bg = samp(lat_bg, np.nanmedian)
        c = np.clip(on - bg, -C_CLIP, C_CLIP)
        m = np.isfinite(c).astype(np.float32)
        c = np.where(m > 0, c, 0.0).astype(np.float32)
        return c, m, (ox[:, 0], oy[:, 0])

    # -- correlation --------------------------------------------------------
    def best_fit(self, c, m, s_vals=None, r0_vals=None, variants=None):
        """Best (ncc, s, r0, variant, amp) per ray.  Vectorised over rays and
        over r0; looped over s, variant and lobe."""
        s_vals = self.s_vals if s_vals is None else s_vals
        r0_vals = self.r0_vals if r0_vals is None else r0_vals
        vnames = list(VARIANTS.keys()) if variants is None else list(variants)
        n_ray = c.shape[0]
        cm = np.cumsum(np.concatenate(
            [np.zeros((n_ray, 1), np.float64), m.astype(np.float64)], axis=1), axis=1)
        cc = np.cumsum(np.concatenate(
            [np.zeros((n_ray, 1), np.float64), (m * c).astype(np.float64)], axis=1), axis=1)
        cq = np.cumsum(np.concatenate(
            [np.zeros((n_ray, 1), np.float64), (m * c * c).astype(np.float64)], axis=1), axis=1)

        # One (rank, ncc, aux) per variant, kept apart: the variants are NOT
        # allowed to compete on score.  A shorter template always fits more
        # easily, so letting V01 out-score V012 is exactly how a forearm wins
        # (measured: V01 took every frame of swing_0006 at ncc 0.93 while the
        # three-group fit on the same frame was the real shaft).  The longest
        # template the IMAGE allows is used, and a shorter one only where the
        # longer one has a group out of frame.
        nv = len(vnames)
        best_rank = np.full((nv, n_ray), -2.0)
        best_ncc = np.full((nv, n_ray), -2.0)
        best = np.zeros((nv, n_ray, 5))      # s, r0, variant_idx, amp, resp
        r0 = np.asarray(r0_vals, dtype=float)

        def box(cum, lo_px, hi_px):
            i0 = np.clip(np.rint(lo_px - R_LO).astype(np.int64), 0, self.n_r)
            i1 = np.clip(np.rint(hi_px - R_LO).astype(np.int64), 0, self.n_r)
            i1 = np.maximum(i1, i0)
            return cum[:, i1] - cum[:, i0]

        for s in s_vals:
            if s * BAND_W_MM < MIN_BAND_PX:
                continue
            for vslot, vname in enumerate(vnames):
                groups = VARIANTS[vname]
                vi = list(VARIANTS.keys()).index(vname)
                n_t = np.zeros((n_ray, len(r0)))
                Sc = np.zeros((n_ray, len(r0)))
                Scc = np.zeros((n_ray, len(r0)))
                St = np.zeros((n_ray, len(r0)))
                Stt = np.zeros((n_ray, len(r0)))
                Sct = np.zeros((n_ray, len(r0)))
                grp_ok = np.ones((n_ray, len(r0)), dtype=bool)
                for g in groups:
                    gm = np.zeros((n_ray, len(r0)))
                    gnom = 0.0
                    for (a, b, w) in g:
                        lo = s * (a - r0)
                        hi = s * (b - r0)
                        M = box(cm, lo, hi)
                        C = box(cc, lo, hi)
                        Q = box(cq, lo, hi)
                        n_t += M
                        Sc += C
                        Scc += Q
                        St += w * M
                        Stt += (w * w) * M
                        Sct += w * C
                        gm += M
                        gnom += (b - a) * s
                    grp_ok &= (gm >= GROUP_VALID_FRAC * max(gnom, 1e-6))
                num = n_t * Sct - Sc * St
                d1 = n_t * Scc - Sc * Sc
                d2 = n_t * Stt - St * St
                den = np.sqrt(np.maximum(d1, 0.0) * np.maximum(d2, 0.0))
                with np.errstate(invalid="ignore", divide="ignore"):
                    ncc = np.where((den > 0) & (n_t > 10) & grp_ok, num / den, -2.0)
                    # `resp` is the SIGNED regression coefficient of the measured
                    # contrast on the unit template, in grey levels: how many grey
                    # levels of white-over-black alternation the hypothesis
                    # actually explains.  NCC alone is a shape score and sits
                    # near 0.8 almost everywhere in this scene (measured); resp
                    # is what separates a taped shaft (60-110) from a forearm
                    # edge or a trouser crease (< 25).
                    inv = 1.0 / np.maximum(n_t, 1e-9)
                    t_rms = np.sqrt(np.maximum(Stt * inv - (St * inv) ** 2, 1e-12))
                    resp = (Sct * inv - (Sc * inv) * (St * inv)) / t_rms
                    amp = np.sqrt(np.maximum(Scc * inv - (Sc * inv) ** 2, 0.0))
                ncc = np.where(np.isfinite(ncc), ncc, -2.0)
                resp = np.where(np.isfinite(resp), resp, 0.0)
                # rank on the product: a candidate must be both the right SHAPE
                # and carry real contrast.  Ties on shape alone are what let the
                # 2-group templates lock a forearm.
                rank = np.where(ncc > -1.0, ncc * np.maximum(resp, 0.0), -1.0)
                k = np.argmax(rank, axis=1)
                rows = np.arange(n_ray)
                v = rank[rows, k]
                upd = v > best_rank[vslot]
                best_rank[vslot] = np.where(upd, v, best_rank[vslot])
                best_ncc[vslot] = np.where(upd, ncc[rows, k], best_ncc[vslot])
                best[vslot][upd, 0] = s
                best[vslot][upd, 1] = r0[k][upd]
                best[vslot][upd, 2] = vi
                best[vslot][upd, 3] = amp[rows, k][upd]
                best[vslot][upd, 4] = resp[rows, k][upd]

        # pick the first variant (they are given longest-first) that produced a
        # geometrically valid fit for that ray
        pick = np.zeros(n_ray, dtype=np.int64)
        taken = best_rank[0] > -1.0
        for vslot in range(1, nv):
            nxt = (~taken) & (best_rank[vslot] > -1.0)
            pick = np.where(nxt, vslot, pick)
            taken = taken | nxt
        rows = np.arange(n_ray)
        return best_rank[pick, rows], best_ncc[pick, rows], best[pick, rows]


# ------------------------------------------------------------------ per-frame
def measure_frame(mt, img, gx, gy):
    """-> dict.  No face-on input reaches this function.

    Stage A scores the whole circle at 2 deg and supplies seeds.  Stage B
    refines the top three seeds AND, separately, the best seeds more than 15 deg
    from the winner -- that second refinement is the margin's denominator, so
    the margin compares two candidates measured at the same resolution.  Stage C
    polishes the winner in (theta, lateral offset, s, r0)."""
    vnames = list(VARIANTS.keys())
    # ---- A: the seed map ---------------------------------------------------
    th_a = np.deg2rad(np.arange(0.0, 360.0, COARSE_DEG))
    c, m, _ = mt.profiles(img, gx, gy, th_a, COARSE_OFFSETS,
                          lat_on=COARSE_LAT_ON, lat_bg=COARSE_LAT_BG)
    rank, ncc, aux = mt.best_fit(c, m, mt.s_coarse, mt.r0_coarse, mt.variants)
    n_off = len(COARSE_OFFSETS)
    rank_t = rank.reshape(len(th_a), n_off).max(axis=1)
    aux_t = aux.reshape(len(th_a), n_off, 5)
    off_t = np.argmax(rank.reshape(len(th_a), n_off), axis=1)
    order = np.argsort(-rank_t)

    def pick_seeds(n, exclude_deg=None, exclude_from=None):
        out = []
        for i in order:
            thi = float(np.degrees(th_a[i]))
            if exclude_deg is not None and \
                    abs(wrap_deg(thi - exclude_from)) <= exclude_deg:
                continue
            if all(abs(wrap_deg(thi - float(np.degrees(th_a[j])))) >= 10.0
                   for j in out):
                out.append(int(i))
            if len(out) == n:
                break
        return out

    def refine(seed_idx):
        """Stage B on a list of coarse seeds -> the best refined candidate."""
        best = None
        for i in seed_idx:
            s0, r00 = aux_t[i, off_t[i], 0], aux_t[i, off_t[i], 1]
            thi = float(np.degrees(th_a[i]))
            th_b = np.deg2rad(np.arange(thi - MID_SPAN_DEG,
                                        thi + MID_SPAN_DEG + 1e-9, MID_DEG))
            cb, mb, _o = mt.profiles(img, gx, gy, th_b, MID_OFFSETS)
            s_b = np.arange(max(mt.s_min, s0 - 0.06),
                            min(mt.s_max, s0 + 0.06) + 1e-9, 0.005)
            r0_b = np.arange(max(0.0, r00 - 18.0),
                             min(260.0, r00 + 18.0) + 1e-9, 2.0)
            rb, _n, ab = mt.best_fit(cb, mb, s_b, r0_b, mt.variants)
            j = int(np.argmax(rb))
            if best is None or rb[j] > best[0]:
                best = (float(rb[j]),
                        float(np.degrees(th_b[j // len(MID_OFFSETS)])),
                        float(MID_OFFSETS[j % len(MID_OFFSETS)]),
                        float(ab[j, 0]), float(ab[j, 1]))
        return best

    # ---- B: locate, then re-refine the best FAR rival ----------------------
    # The margin has to compare like with like.  The coarse grid is 2 deg x 5 px
    # in (theta, offset) and 0.02 x 6 mm in (s, r0); on a 3 px shaft that grid
    # loses far more score than it does on a broad body edge, so a margin read
    # off the coarse map alone comes out NEGATIVE on frames whose refined answer
    # is demonstrably right (measured on swing_0004: the true address line, s
    # 0.390 and r0 126 on every frame, scored margin -0.34..+0.06).  The best
    # candidate more than 15 deg away is therefore refined to the same stage-B
    # resolution before the two are differenced.
    bestB = refine(pick_seeds(3))
    r_win, th_mid, offb, s1, r01 = bestB
    farB = refine(pick_seeds(MARGIN_FAR_SEEDS, exclude_deg=MARGIN_SEP_DEG,
                             exclude_from=th_mid))
    r_far = farB[0] if farB is not None else 0.0
    # a ratio, not a difference: the score's units (NCC x grey levels) move with
    # the lighting from frame to frame
    margin = float((r_win - max(r_far, 0.0)) / max(r_win, 1e-6))

    # ---- C: polish ---------------------------------------------------------
    th_c2 = np.deg2rad(np.arange(th_mid - FINE_SPAN_DEG,
                                 th_mid + FINE_SPAN_DEG + 1e-9, FINE_DEG))
    off_c = np.arange(offb - 4.0, offb + 4.01, 1.0)
    cc2, mc2, orgc = mt.profiles(img, gx, gy, th_c2, off_c)
    s_c = np.arange(max(mt.s_min, s1 - 0.01), min(mt.s_max, s1 + 0.01) + 1e-9, 0.0025)
    r0_c = np.arange(max(0.0, r01 - 5.0), min(260.0, r01 + 5.0) + 1e-9, 1.0)
    rankc, nccc, auxc = mt.best_fit(cc2, mc2, s_c, r0_c, mt.variants)
    j = int(np.argmax(rankc))
    ti = j // len(off_c)
    theta = float(th_c2[ti])
    s, r0, vi, amp, resp = auxc[j]
    ox, oy = float(orgc[0][j]), float(orgc[1][j])
    ux, uy = math.cos(theta), math.sin(theta)
    head = (ox + s * (CLUB_MM - r0) * ux, oy + s * (CLUB_MM - r0) * uy)
    return dict(theta=theta, theta_deg=math.degrees(theta) % 360.0,
                s=float(s), r0=float(r0), ncc=float(nccc[j]),
                margin=float(margin), amp=float(amp), resp=float(resp),
                variant=vnames[int(vi)],
                origin=(ox, oy), head=head,
                coarse_rank=r_win, coarse_far=r_far)


# ------------------------------------------------------------------ per-swing
def run_swing(sid, corpus, pose_dir, args):
    sd = swing_dir_for(corpus, sid)
    doc = load_swing(sd)
    st = pick_dtl_stream(doc)
    if st is None:
        raise RuntimeError(f"{sid}: no DTL (perspective 1) video stream")
    ts = np.asarray(st["frames"]["t_us"], dtype=np.int64)
    W = int(st["encoded"]["width"])
    H = int(st["encoded"]["height"])
    video = sd / st["file"]

    pose_path = Path(pose_dir) / f"{sid}__dtl.json"
    pose = load_json(pose_path)["frames"]
    pt = np.array([f["t_us"] for f in pose], dtype=np.float64)
    pg = np.array([[0.5 * (f["lead"][0] + f["trail"][0]) * POSE_W,
                    0.5 * (f["lead"][1] + f["trail"][1]) * POSE_H] for f in pose])
    pconf = np.array([f.get("handConf", 0.0) for f in pose], dtype=float)

    # pose frame -> nearest video frame index, deduplicated, ordered
    want = {}
    for i in range(len(pt)):
        fi = int(np.argmin(np.abs(ts - pt[i])))
        if abs(ts[fi] - pt[i]) > 20_000:
            continue
        want.setdefault(fi, i)
    idxs = sorted(want)

    mt = BandMatcher(args.s_min, args.s_max,
                     variants=[v.strip() for v in args.variants.split(",") if v.strip()])

    rows = []
    cap = cv2.VideoCapture(str(video))
    fi = 0
    hi = max(idxs) if idxs else -1
    while fi <= hi:
        ok, bgr = cap.read()
        if not ok:
            break
        if fi in want:
            gray = cv2.cvtColor(bgr, cv2.COLOR_BGR2GRAY).astype(np.float32)
            pi = want[fi]
            gx, gy = float(pg[pi, 0]) * W / POSE_W, float(pg[pi, 1]) * H / POSE_H
            r = measure_frame(mt, gray, gx, gy)
            r.update(frame=fi, t_us=int(ts[fi]), grip_pose=(gx, gy),
                     hand_conf=float(pconf[pi]))
            rows.append(r)
        fi += 1
    cap.release()
    return dict(sid=sid, W=W, H=H, video=str(video), rows=rows,
                ts=ts, n_pose=len(idxs))


# ------------------------------------------------------------------ acceptance
def accept(rows, args):
    """Per-frame gates then the temporal run gate.  Marks each row in place."""
    for r in rows:
        why = []
        if r["ncc"] < args.ncc_min:
            why.append("ncc")
        if r["margin"] < args.margin_min:
            why.append("margin")
        if r["amp"] < args.amp_min:
            why.append("amp")
        if r.get("resp", 0.0) < args.resp_min:
            why.append("resp")
        if not (args.s_min <= r["s"] <= args.s_max):
            why.append("s")
        if r["s"] * BAND_W_MM < MIN_BAND_PX:
            why.append("bandpx")
        if not (args.r0_min <= r["r0"] <= args.r0_max):
            why.append("r0")
        r["gate"] = why
        r["pass_frame"] = not why

    ok = [r for r in rows if r["pass_frame"]]
    for r in rows:
        r["accepted"] = False
    if not ok:
        return
    # Chain into runs of temporally continuous frames.  Continuity is required in
    # (theta, s, r0), not theta alone.  That matters: the false locks this
    # instrument produces are body-line fits whose theta drifts plausibly but
    # whose implied SCALE and BUTT OFFSET jump -- measured on swing_0004, six
    # accepted frames on the golfer's torso with s stepping 0.295 -> 0.340 ->
    # 0.380 -> 0.260 and r0 stepping 19 -> 258 -> 146 -> 170 mm between
    # neighbours, against true locks that hold s to +-0.005 and r0 to +-15 mm.
    # A real club cannot change its projected length by a third, or slide a
    # quarter of its length through the hands, in one 6.6 ms frame.
    run = [ok[0]]
    runs = []
    for a, b in zip(ok, ok[1:]):
        df = b["frame"] - a["frame"]
        dth = abs(wrap_deg(b["theta_deg"] - a["theta_deg"]))
        ds = abs(b["s"] - a["s"])
        dr0 = abs(b["r0"] - a["r0"])
        if (df <= 3 and dth <= MAX_STEP_DEG_PER_FRAME * df
                and ds <= MAX_DS_PER_FRAME * max(a["s"], 1e-6) * df
                and dr0 <= MAX_DR0_MM_PER_FRAME * df):
            run.append(b)
        else:
            runs.append(run)
            run = [b]
    runs.append(run)
    for run in runs:
        if len(run) >= args.run_min:
            for r in run:
                r["accepted"] = True


# ------------------------------------------------------------------ precision
def theta_jitter_deg(rows):
    """Frame-to-frame jitter of theta inside smooth accepted stretches, after
    removing a local quadratic -- the instrument's angular precision."""
    acc = [r for r in rows if r["accepted"]]
    res = []
    i = 0
    while i < len(acc):
        j = i
        while (j + 1 < len(acc) and acc[j + 1]["frame"] - acc[j]["frame"] <= 2):
            j += 1
        seg = acc[i:j + 1]
        if len(seg) >= 7:
            x = np.array([r["frame"] for r in seg], dtype=float)
            y = np.unwrap(np.radians([r["theta_deg"] for r in seg]))
            y = np.degrees(y)
            for k in range(3, len(seg) - 3):
                xs = x[k - 3:k + 4]
                ys = y[k - 3:k + 4]
                w = np.ones(7)
                w[3] = 0.0            # leave-one-out
                co = np.polyfit(xs[w > 0] - x[k], ys[w > 0], 2)
                res.append(y[k] - np.polyval(co, 0.0))
        i = j + 1
    return np.array(res)


# ------------------------------------------------------------------ rendering
def contact_sheet(swing, rows, out_png, every=1, zoom=2.0, cols=6):
    acc = [r for r in rows if r["accepted"]]
    if not acc:
        return 0
    sel = acc[::every]
    cap = cv2.VideoCapture(swing["video"])
    want = {r["frame"]: r for r in sel}
    tiles = []
    fi = 0
    hi = max(want) if want else -1
    while fi <= hi:
        ok, bgr = cap.read()
        if not ok:
            break
        if fi in want:
            r = want[fi]
            ox, oy = r["origin"]
            hx, hy = r["head"]
            xs = [ox, hx, r["grip_pose"][0]]
            ys = [oy, hy, r["grip_pose"][1]]
            pad = 34
            x0 = int(max(0, min(xs) - pad))
            x1 = int(min(swing["W"], max(xs) + pad))
            y0 = int(max(0, min(ys) - pad))
            y1 = int(min(swing["H"], max(ys) + pad))
            crop = bgr[y0:y1, x0:x1].copy()
            if crop.size:
                crop = cv2.resize(crop, None, fx=zoom, fy=zoom,
                                  interpolation=cv2.INTER_NEAREST)
                p0 = (int((ox - x0) * zoom), int((oy - y0) * zoom))
                p1 = (int((hx - x0) * zoom), int((hy - y0) * zoom))
                cv2.line(crop, p0, p1, (255, 255, 255), 1, cv2.LINE_AA)
                cv2.circle(crop, p0, 3, (0, 255, 255), 1, cv2.LINE_AA)
                cv2.putText(crop, f"{fi} {r['theta_deg']:.1f} n{r['ncc']:.2f}",
                            (3, 13), cv2.FONT_HERSHEY_SIMPLEX, 0.36,
                            (0, 255, 255), 1, cv2.LINE_AA)
                tiles.append(crop)
        fi += 1
    cap.release()
    if not tiles:
        return 0
    tw = max(t.shape[1] for t in tiles)
    th = max(t.shape[0] for t in tiles)
    rows_n = (len(tiles) + cols - 1) // cols
    sheet = np.zeros((rows_n * th, cols * tw, 3), np.uint8)
    for i, t in enumerate(tiles):
        rr, cc = divmod(i, cols)
        sheet[rr * th:rr * th + t.shape[0], cc * tw:cc * tw + t.shape[1]] = t
    Path(out_png).parent.mkdir(parents=True, exist_ok=True)
    cv2.imwrite(str(out_png), sheet)
    return len(tiles)


# ------------------------------------------------------------------ selftest
def _synth_frame(W, H, gx, gy, theta_deg, s, r0, bg_level=120.0, noise=3.0):
    """A deterministic synthetic DTL-like frame: a banded shaft on a textured
    background plus a bright smooth 'forearm' distractor.  No RNG -- the speckle
    is a fixed deterministic pattern."""
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
    img = (bg_level + 18.0 * np.sin(xx / 13.0) * np.cos(yy / 17.0)
           + noise * np.sin(xx * 2.3 + yy * 1.7))
    th = math.radians(theta_deg)
    ux, uy = math.cos(th), math.sin(th)
    nx, ny = -uy, ux
    rr = (xx - gx) * ux + (yy - gy) * uy
    dd = (xx - gx) * nx + (yy - gy) * ny
    onshaft = (np.abs(dd) <= 1.6) & (rr >= s * (GRIP_END_MM - r0)) & \
              (rr <= s * (HOSEL_MM - r0))
    mm = rr / s + r0
    val = np.full_like(img, 0.0)
    val[onshaft] = 150.0                     # bare steel
    for c in BANDS_MM:
        w = onshaft & (np.abs(mm - c) <= BAND_W_MM / 2.0)
        val[w] = 245.0
    for a, b in ((GRIP_END_MM, 295.5), (320.5, 349.5), (374.5, 398.0),
                 (527.5, 547.5), (572.5, 592.5), (725.5, 745.5),
                 (770.5, 795.5), (820.5, 841.5), (866.5, 882.0)):
        w = onshaft & (mm >= a) & (mm <= b)
        val[w] = 35.0
    img = np.where(onshaft, val, img)
    # a bright smooth distractor limb, 70 deg away, longer than the shaft
    th2 = math.radians(theta_deg + 70.0)
    d2 = (xx - gx) * (-math.sin(th2)) + (yy - gy) * math.cos(th2)
    r2 = (xx - gx) * math.cos(th2) + (yy - gy) * math.sin(th2)
    limb = (np.abs(d2) <= 7.0) & (r2 > 0) & (r2 < 400)
    img = np.where(limb, 235.0, img)
    return np.clip(img, 0, 255).astype(np.float32)


def selftest():
    ok = True

    def check(cond, msg):
        nonlocal ok
        ok = ok and bool(cond)
        print(f"[selftest] {'PASS' if cond else 'FAIL'}: {msg}")

    # template sanity: every group integrates to zero
    for name, groups in VARIANTS.items():
        for gi, g in enumerate(groups):
            tot = sum((b - a) * w for a, b, w in g)
            check(abs(tot) < 1e-6, f"{name} group {gi} template is zero-mean "
                                   f"(sum={tot:.3g})")

    mt = BandMatcher(DEF_S_MIN, DEF_S_MAX,
                     variants=[v.strip() for v in DEF_VARIANTS.split(",")])
    W, H = 512, 1024
    args = argparse.Namespace(ncc_min=DEF_NCC_MIN, margin_min=DEF_MARGIN_MIN,
                              resp_min=DEF_RESP_MIN, amp_min=DEF_AMP_MIN,
                              s_min=DEF_S_MIN, s_max=DEF_S_MAX, run_min=1,
                              r0_min=DEF_R0_MIN, r0_max=DEF_R0_MAX)
    got = []
    for (tdeg, s, r0, off, must_accept) in ((54.0, 0.39, 120.0, 0.0, True),
                                            (-118.0, 0.34, 90.0, 25.0, True),
                                            (61.0, 0.26, 40.0, -30.0, False)):
        th = math.radians(tdeg)
        gx, gy = 256.0 - off * (-math.sin(th)), 500.0 - off * math.cos(th)
        img = _synth_frame(W, H, gx + off * (-math.sin(th)),
                           gy + off * math.cos(th), tdeg, s, r0)
        r = measure_frame(mt, img, gx, gy)
        r.update(frame=0, t_us=0, grip_pose=(gx, gy), hand_conf=1.0)
        accept([r], args)
        derr = abs(wrap_deg(r["theta_deg"] - (tdeg % 360.0)))
        got.append((tdeg, r, derr))
        check(derr <= 1.0,
              f"synthetic theta {tdeg:+.1f} recovered to {derr:.2f} deg "
              f"(ncc {r['ncc']:.3f} margin {r['margin']:.3f} resp {r['resp']:.0f} "
              f"s {r['s']:.3f} r0 {r['r0']:.0f})")
        check(abs(r["s"] - s) < 0.03, f"  scale {r['s']:.3f} vs {s:.3f}")
        if must_accept:
            # the two well-resolved cases must survive the gates; the third is a
            # 6.6 px band on a heavily textured synthetic and is allowed to
            # abstain -- abstaining is the designed failure mode
            check(r["accepted"], f"  theta {tdeg:+.1f} passes the gates")

    # the distractor limb must not win
    check(all(abs(wrap_deg(r["theta_deg"] - (t + 70.0))) > 20.0
              for t, r, _ in got), "the bright smooth limb never wins")

    # a frame with no club at all must be rejected
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
    flat = (120.0 + 18.0 * np.sin(xx / 13.0) * np.cos(yy / 17.0)).astype(np.float32)
    r = measure_frame(mt, flat, 256.0, 500.0)
    r.update(frame=0, t_us=0, grip_pose=(256.0, 500.0), hand_conf=1.0)
    accept([r], args)
    check(not r["accepted"], f"a clubless frame is rejected (ncc {r['ncc']:.3f})")

    # determinism
    img = _synth_frame(W, H, 256.0, 500.0, 54.0, 0.39, 120.0)
    a = measure_frame(mt, img, 256.0, 500.0)
    b = measure_frame(mt, img, 256.0, 500.0)
    check(a["theta"] == b["theta"] and a["ncc"] == b["ncc"],
          "re-running on the same pixels is bit-identical")

    print(f"[selftest] {'ALL PASS' if ok else 'SOME CHECKS FAILED'}")
    return ok


# ------------------------------------------------------------------ reporting
def write_outputs(results, args):
    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    csv_rows = []
    for sw in results:
        acc = [r for r in sw["rows"] if r["accepted"]]
        doc = dict(
            swing=sw["sid"],
            generator="tools/shaftlab/dtl_band_truth.py",
            faceOnInput="none",
            frameWidth=sw["W"], frameHeight=sw["H"],
            thresholds=dict(nccMin=args.ncc_min, marginMin=args.margin_min,
                            respMin=args.resp_min, ampMin=args.amp_min,
                            sMin=args.s_min, sMax=args.s_max,
                            runMin=args.run_min, variants=args.variants,
                            r0Min=args.r0_min, r0Max=args.r0_max,
                            marginSepDeg=MARGIN_SEP_DEG),
            bandsMm=list(BANDS_MM), bandWidthMm=BAND_W_MM, clubMm=CLUB_MM,
            frames=[dict(t_us=r["t_us"], frame=r["frame"],
                         theta=round(r["theta"], 6), s=round(r["s"], 5),
                         r0=round(r["r0"], 2),
                         grip=[round(r["origin"][0], 2), round(r["origin"][1], 2)],
                         head=[round(r["head"][0], 2), round(r["head"][1], 2)],
                         ncc=round(r["ncc"], 4), margin=round(r["margin"], 4),
                         resp=round(r.get("resp", 0.0), 2), variant=r["variant"])
                    for r in acc])
        with open(out_dir / f"{sw['sid']}.json", "w", encoding="utf-8") as f:
            json.dump(doc, f, indent=1)
            f.write("\n")
        for r in sw["rows"]:
            csv_rows.append((sw["sid"], r["frame"], r["t_us"],
                             f"{r['theta_deg']:.3f}", f"{r['s']:.4f}",
                             f"{r['r0']:.1f}", f"{r['ncc']:.4f}",
                             f"{r['margin']:.4f}", f"{r['amp']:.2f}",
                             f"{r.get('resp', 0.0):.2f}",
                             r["variant"], int(r["accepted"]),
                             "|".join(r["gate"])))
    with open(out_dir / "band_truth.csv", "w", encoding="utf-8") as f:
        f.write("swing,frame,t_us,theta_deg,s,r0,ncc,margin,amp,resp,variant,"
                "accepted,gate\n")
        for row in csv_rows:
            f.write(",".join(str(v) for v in row) + "\n")
    (out_dir / "band_truth_summary.md").write_text(
        summary_markdown(results, args), encoding="utf-8")
    return out_dir


def _md_table(headers, body):
    out = ["| " + " | ".join(headers) + " |",
           "|" + "|".join(["---"] * len(headers)) + "|"]
    for r in body:
        out.append("| " + " | ".join(str(x) for x in r) + " |")
    return "\n".join(out)


def _p(a, q):
    a = np.asarray([x for x in a if np.isfinite(x)], dtype=float)
    return float(np.percentile(a, q)) if a.size else float("nan")


def summary_markdown(results, args):
    L, A = [], None
    A = L.append
    A("# DTL band truth -- a face-on-independent shaft line")
    A("")
    A("Generated by `tools/shaftlab/dtl_band_truth.py` (deterministic, no RNG). "
      "Inputs: the DTL video, the DTL stream's own `frames.t_us`, and the cached "
      f"DTL pose under `{args.pose_dir}`. **No face-on input of any kind** -- no "
      "face-on track, no face-on phases, no face-on timing, no P-ladder. This is "
      "the instrument design Sec 6 asks for, replacing the E1 band lock that "
      "Stage 0 measured at zero locks on this dev set.")
    A("")
    A("## What the tape actually looks like in this view")
    A("")
    A("Measured, not assumed -- a profile along a hand-fitted shaft line on the "
      "address frame of swing_0006, mapped to millimetres from the butt by a "
      "linear fit to the six white-band centres (residual rms ~1.5 px over a "
      "214 px baseline):")
    A("")
    A("```")
    for line in __doc__.split("What the pattern actually looks like")[1] \
            .split("```")[0].split("\n"):
        if "mm " in line and ("WHITE" in line or "BLACK" in line or "steel" in line
                              or "bright" in line):
            A(line.strip())
    A("```")
    A("")
    A("Two facts drive the template. **Bare steel is not neutral**: against its "
      "lateral background it reads +20..+160 grey levels, as strong as the white "
      "bands, so brightness does not separate band from shaft -- the ALTERNATION "
      "does. And **the black tape is only negative where the local background is "
      "mid-grey**; over the dark strip behind the mat it reads ~0. So each "
      "group's template is made zero-mean over its own lobes (the black lobes "
      "carry a balancing weight) and the correlation scores only the local "
      "alternation, not the level.")
    A("")
    A("## Thresholds, and why")
    A("")
    A(_md_table(["gate", "value", "why"], [
        ["`ncc`", f"{args.ncc_min}",
         "shape. The three groups span three lighting regimes (screen, dark "
         "strip, mat) with different contrast amplitudes, so a correct "
         "three-group fit tops out near 0.80 here, not 0.99 -- 0.80 would reject "
         "every true lock"],
        ["`resp`", f"{args.resp_min} grey levels",
         "amplitude. The regression coefficient of the measured contrast on the "
         "unit template: how many grey levels of white-over-black alternation the "
         "hypothesis explains. True locks measure 45-57; forearm and crease "
         "candidates measure under 35. This is the gate that does the work -- NCC "
         "alone sits near 0.75 almost everywhere in this scene"],
        ["`margin`", f"{args.margin_min}",
         "relative, on the SAME sharp-aperture score as the decision, against the "
         "best candidate more than 15 deg away"],
        ["`s`", f"{args.s_min}..{args.s_max} px/mm, band >= {MIN_BAND_PX} px",
         "a 25 mm band narrower than 4.5 px cannot carry the pattern"],
        ["`r0`", f"{args.r0_min:.0f}..{args.r0_max:.0f} mm",
         "the attachment gate: the pose wrist midpoint must sit ON the grip. "
         "Below 60 mm the whole hand would be off the butt cap; above 260 mm it "
         "would be off the grip onto bare shaft. Measured on accepted frames: "
         "117-136 mm"],
        ["groups", "all groups of the chosen variant >= 90% in-image",
         "V012 (all three groups) is used wherever the image allows it; V12 and "
         "V01 only where a group is off-frame. The variants are NOT allowed to "
         "compete on score -- a shorter template always fits more easily, and "
         "letting V01 out-score V012 is exactly how a forearm wins (measured)"],
        ["run", f">= {args.run_min} frames",
         "temporal: isolated single-frame accepts are dropped; theta must be "
         f"continuous at <= {MAX_STEP_DEG_PER_FRAME:.0f} deg per frame gap"]]))
    A("")
    A("## Accepts per swing")
    A("")
    rows = []
    for sw in results:
        acc = [r for r in sw["rows"] if r["accepted"]]
        n = len(sw["rows"])
        rows.append([sw["sid"].split("__")[-1], n, len(acc),
                     f"{100.0 * len(acc) / max(1, n):.0f}%",
                     f"{min((r['frame'] for r in acc), default=-1)}-"
                     f"{max((r['frame'] for r in acc), default=-1)}",
                     f"{_p([r['ncc'] for r in acc], 50):.3f}",
                     f"{_p([r['resp'] for r in acc], 50):.1f}",
                     f"{_p([r['margin'] for r in acc], 50):.2f}",
                     f"{_p([r['s'] for r in acc], 50):.4f}",
                     f"{_p([r['r0'] for r in acc], 50):.0f}",
                     "/".join(sorted({r["variant"] for r in acc}))])
    A(_md_table(["swing", "posed frames", "accepted", "rate", "frame range",
                 "ncc p50", "resp p50", "margin p50", "s p50", "r0 p50",
                 "variants"], rows))
    A("")
    A("## Score distributions -- accepted against everything else")
    A("")
    acc = [r for sw in results for r in sw["rows"] if r["accepted"]]
    rej = [r for sw in results for r in sw["rows"] if not r["accepted"]]
    rows = []
    for nm, key in (("ncc", "ncc"), ("resp", "resp"), ("margin", "margin"),
                    ("s", "s"), ("amp", "amp")):
        rows.append([nm] +
                    [f"{_p([r[key] for r in acc], q):.3f}" for q in (5, 50, 95)] +
                    [f"{_p([r[key] for r in rej], q):.3f}" for q in (50, 95, 99)])
    A(_md_table(["quantity", "accepted p5", "accepted p50", "accepted p95",
                 "not accepted p50", "p95", "p99"], rows))
    A("")
    A("## Precision")
    A("")
    prows = []
    allres = []
    for sw in results:
        res = theta_jitter_deg(sw["rows"])
        allres.append(res)
        prows.append([sw["sid"].split("__")[-1], len(res),
                      f"{_p(np.abs(res), 50):.3f}", f"{_p(np.abs(res), 90):.3f}",
                      f"{float(np.std(res)) if res.size else float('nan'):.3f}"])
    A("Frame-to-frame jitter of theta inside smooth accepted stretches, after "
      "removing a LOCAL QUADRATIC fitted leave-one-out over +-3 frames. This is "
      "the instrument's own noise, not its accuracy against an external "
      "reference -- there is no such reference. The search grid's own floor is "
      f"{FINE_DEG} deg.")
    A("")
    A(_md_table(["swing", "n residuals", "|resid| p50 (deg)", "p90 (deg)",
                 "std (deg)"], prows))
    pooled = np.concatenate([r for r in allres if r.size]) if any(
        r.size for r in allres) else np.array([])
    if pooled.size:
        A("")
        A(f"**Pooled: p50 {_p(np.abs(pooled), 50):.3f} deg, "
          f"p90 {_p(np.abs(pooled), 90):.3f} deg, "
          f"std {float(np.std(pooled)):.3f} deg (n = {pooled.size}).**")
    A("")
    A("## Adjudication")
    A("")
    A("Every accepted frame is drawn as a thin white line over a zoomed "
      "native-resolution crop in `01b_band_truth_<swing>.png` (every third frame "
      "where a swing has more than 60 accepts) and counted by eye. Nothing here "
      "is trusted that was not looked at.")
    A("")
    return "\n".join(L) + "\n"


def main():
    ap = argparse.ArgumentParser(prog="dtl_band_truth.py", description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--corpus", default="/mnt/swingdata/corpus/swings")
    ap.add_argument("--pose-dir", default="build/dtl/pose")
    ap.add_argument("--swings", default=None)
    ap.add_argument("--out-dir", default="docs/research/data/dtl/band_truth")
    ap.add_argument("--sheets-dir", default=None)
    ap.add_argument("--ncc-min", type=float, default=DEF_NCC_MIN)
    ap.add_argument("--margin-min", type=float, default=DEF_MARGIN_MIN)
    ap.add_argument("--amp-min", type=float, default=DEF_AMP_MIN)
    ap.add_argument("--resp-min", type=float, default=DEF_RESP_MIN)
    ap.add_argument("--r0-min", type=float, default=DEF_R0_MIN)
    ap.add_argument("--r0-max", type=float, default=DEF_R0_MAX)
    ap.add_argument("--variants", default=DEF_VARIANTS,
                    help="comma-separated template subsets to allow: V012 (all "
                         "three groups, the default and the only one that is "
                         "hard to counterfeit), V01, V12")
    ap.add_argument("--s-min", type=float, default=DEF_S_MIN)
    ap.add_argument("--s-max", type=float, default=DEF_S_MAX)
    ap.add_argument("--run-min", type=int, default=DEF_RUN_MIN)
    ap.add_argument("--raw-out", default=None,
                    help="cache every frame's raw measurement here (npz/json) "
                         "so thresholds can be re-swept without re-measuring")
    ap.add_argument("--raw-in", default=None)
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()

    if a.selftest:
        sys.exit(0 if selftest() else 1)
    if not a.swings:
        ap.error("--swings is required (unless --selftest)")

    sids = [s.strip() for s in a.swings.split(",") if s.strip()]
    results = []
    for sid in sids:
        if a.raw_in:
            raw = load_json(Path(a.raw_in) / f"{sid}.raw.json")
            sw = dict(sid=sid, W=raw["W"], H=raw["H"], video=raw["video"],
                      rows=raw["rows"], n_pose=raw["n_pose"])
            for r in sw["rows"]:
                r["origin"] = tuple(r["origin"])
                r["head"] = tuple(r["head"])
                r["grip_pose"] = tuple(r["grip_pose"])
        else:
            print(f"[band-truth] {sid} ...", flush=True)
            sw = run_swing(sid, a.corpus, a.pose_dir, a)
            sw.pop("ts", None)
        if a.raw_out:
            Path(a.raw_out).mkdir(parents=True, exist_ok=True)
            with open(Path(a.raw_out) / f"{sid}.raw.json", "w", encoding="utf-8") as f:
                json.dump(dict(W=sw["W"], H=sw["H"], video=sw["video"],
                               n_pose=sw["n_pose"], rows=sw["rows"]), f)
        accept(sw["rows"], a)
        results.append(sw)
        n_acc = sum(1 for r in sw["rows"] if r["accepted"])
        print(f"[band-truth] {sid}: {n_acc}/{len(sw['rows'])} accepted", flush=True)

    out_dir = write_outputs(results, a)
    print(f"[band-truth] wrote {out_dir}")

    if a.sheets_dir:
        for sw in results:
            n_acc = sum(1 for r in sw["rows"] if r["accepted"])
            every = 3 if n_acc > 60 else 1
            short = sw["sid"].split("__")[-1]
            p = Path(os.path.expanduser(a.sheets_dir)) / f"01b_band_truth_{short}.png"
            n = contact_sheet(sw, sw["rows"], p, every=every)
            print(f"[band-truth] sheet {p} ({n} tiles, every {every})")


if __name__ == "__main__":
    main()
