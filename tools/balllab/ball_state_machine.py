#!/usr/bin/env python3
"""Ball Detection v2 — the state machine, executable form (docs/design/ball_detection_v2.md §4).

This is the V0 deliverable: the SEARCH -> CANDIDATE -> LOCKED -> VANISHED machine
of §4.2, expressed over the same scale-matched DoG response the corpus evidence
(corpus_separation.py, launch_trace.py) is built on. It is the spec the C++ core
(src/Pose/ball_temporal.h, V1) must match numerically, so keep the math here the
reference: pure functions + one small incremental tracker, no cleverness the
header can't mirror.

Response math (§4.1), in scene-noise units, never absolute luma:
    R = GaussianBlur(g, s1) - GaussianBlur(g, s1*3.2)     s1 = r_hat/1.6
    noise = 1.4826 * MAD(R)                                (robust, per frame)
    N = (R - B) / noise                                    novelty vs baseline B

Baseline B (§4.1 pillar 2): a per-pixel baseline OF THE RESPONSE MAP. Live, it is
learned from the between-shots empty mat; in the recorded-window (batch) form the
window opens with the ball already at rest, so acceptance.py seeds B from the
ball-ABSENT tail (post-launch) — which still contains every static distractor
(painted line, pool edge, chrome) but NOT the ball, so novelty N is high only at
the ball. That is the offline equivalent the design blesses (§5, "windowed batch")
and exactly the prototype's medP - |medA| contrast. Parity with C++ (V1) is on the
response math and the lock/launch/sub-pixel logic, not on where B's samples come
from. See acceptance.py for the batch driver and the §9.1 gates.
"""
import math
import numpy as np
import cv2


# ── parameters (design §8; all dimensionless unless stated) ───────────────────
K_APPEAR         = 5.0      # novelty sigma for a SEARCH peak to be a candidate
K_LOCK           = 5.0      # median novelty over T_lock to LOCK
T_LOCK_S         = 0.5      # candidate must hold this long to lock
LOCK_STABILITY_PX = 2.0     # candidate peak must stay within this radius
OCCL_FLOOR       = 0.40     # dip below this * L0 is occlusion (if it recovers)
T_OCCL_S         = 0.30     # ... provided it recovers within this
COLLAPSE_FLOOR   = 0.25     # launch: at-spot response below this * L0 ...
COLLAPSE_FROM    = 0.80     # ... having been at/above this * L0 the frame before
COLLAPSE_FRAMES  = 2        # ... for this many consecutive frames (the cliff)
NMS_RADIUS_MULT  = 2.0      # non-max-suppression radius = this * r_hat


def dog(gray32, r):
    """Scale-matched band-pass at ball radius r (§4.1). Identical to the prototype."""
    s1 = max(1.0, r / 1.6)
    return cv2.GaussianBlur(gray32, (0, 0), s1) - cv2.GaussianBlur(gray32, (0, 0), s1 * 3.2)


def robust_noise(R):
    """1.4826 * MAD(R) — robust per-frame response noise (§4.1)."""
    med = np.median(R)
    mad = np.median(np.abs(R - med))
    return float(1.4826 * mad) if mad > 0 else 1.0


def subpixel_peak(M, cx, cy):
    """2-D quadratic refine of a discrete peak (cx,cy) over M's 3x3 neighbourhood
    (§4.4). Returns (fx, fy) sub-pixel offset added to (cx,cy); clamped to +-1."""
    h, w = M.shape
    if cx <= 0 or cy <= 0 or cx >= w - 1 or cy >= h - 1:
        return float(cx), float(cy)
    dx = (M[cy, cx + 1] - M[cy, cx - 1])
    dxx = (M[cy, cx + 1] - 2 * M[cy, cx] + M[cy, cx - 1])
    dy = (M[cy + 1, cx] - M[cy - 1, cx])
    dyy = (M[cy + 1, cx] - 2 * M[cy, cx] + M[cy - 1, cx])
    ox = -0.5 * dx / dxx if dxx != 0 else 0.0
    oy = -0.5 * dy / dyy if dyy != 0 else 0.0
    ox = max(-1.0, min(1.0, ox)); oy = max(-1.0, min(1.0, oy))
    return cx + ox, cy + oy


def _at_spot(R, x, y, k=2):
    """Max response in a (2k+1)^2 neighbourhood of (x,y) — the LOCKED monitor (§4.2)."""
    h, w = R.shape
    x0, x1 = max(0, x - k), min(w, x + k + 1)
    y0, y1 = max(0, y - k), min(h, y + k + 1)
    return float(R[y0:y1, x0:x1].max())


_RING8 = [(math.cos(2 * math.pi * k / 8), math.sin(2 * math.pi * k / 8)) for k in range(8)]
BLOB_RING_RATIO = 0.55     # a peak is a disc if its response at ~1.3*r decays below this


def is_blob(R, cx, cy, r, ratio=BLOB_RING_RATIO):
    """Shape / scale-space test (§4.4): a golf ball is an ISOTROPIC ball-scale disc;
    the painted target line and the address shaft are elongated ridges. Sample R on
    a ring of radius ~1.3*r: for a disc every ring sample decays well below the peak;
    for a ridge the two along-ridge samples stay near the peak. Reject when the ring
    max is not below `ratio` * peak. Also rejects anything within a ring of an edge."""
    h, w = R.shape
    rr = max(2, int(round(1.3 * r)))
    if cx - rr < 0 or cy - rr < 0 or cx + rr >= w or cy + rr >= h:
        return False
    peak = float(R[cy, cx])
    if peak <= 0:
        return False
    ring_max = 0.0
    for dx, dy in _RING8:
        v = float(R[int(round(cy + rr * dy)), int(round(cx + rr * dx))])
        if v > ring_max:
            ring_max = v
    return ring_max < ratio * peak


# ── state machine (§4.2) ──────────────────────────────────────────────────────
SEARCH, CANDIDATE, LOCKED, VANISHED = "SEARCH", "CANDIDATE", "LOCKED", "VANISHED"
K_PEAKS = 3                # track up to this many novelty peaks frame-to-frame (§4.2)


class BallTracker:
    """Incremental, causal SEARCH->CANDIDATE->LOCKED->VANISHED tracker over an ROI.

    Frames are pushed one at a time (grayscale float32, already cropped to the
    search band). The baseline B and per-frame noise scale are supplied by the
    caller (acceptance.py seeds B from the ball-absent tail — see module docstring).
    Emits at most one lock and one launch per window via the `locked`/`launched`
    attributes; `false_launches` counts any launch fired before `address_end_idx`.

    SEARCH tracks up to K_PEAKS novelty peaks concurrently (§4.2). This matters:
    at address the chrome clubhead is also novel vs the ball-absent baseline and
    can be the momentary brightest peak, but it MOVES, so its candidate keeps
    resetting while the stationary ball's candidate accrues hold and locks first.
    A single-argmax tracker mislocks onto the club — hence K peaks, not one.
    """

    def __init__(self, r_hat, fps, baseline, noise_scale):
        self.r = float(r_hat)
        self.fps = float(fps)
        self.B = baseline.astype(np.float32)          # response baseline (seeded)
        self.noise0 = float(noise_scale)              # fallback noise if a frame is flat
        self.t_lock_frames = max(1, int(round(T_LOCK_S * fps)))
        self.nms = max(2, int(round(NMS_RADIUS_MULT * r_hat)))

        self.state = SEARCH
        self.idx = -1
        self.cands = []               # [{x,y,hold,Ns}] up to K_PEAKS
        self.locked = None            # {idx,x,y,L0,ix,iy} once LOCKED
        self.launched = None          # {idx,x,y} once VANISHED (launch)
        self.false_launches = 0
        self.address_end_idx = None   # launches before this index are "during address"
        self._L0 = None
        self._Lhist = []              # [(idx, at-spot L)] while LOCKED, for the cliff test

    def push(self, R):
        """Advance one frame. R is the scale-matched DoG response over the search
        band, computed by the caller on a PADDED crop and sliced to the band
        interior (so there is no GaussianBlur crop-boundary artifact along the
        edges — that artifact otherwise mislocks onto the shaft where it enters
        the band top). Keeping DoG in the caller also lets the C++ detector own
        the ROI/padding while the tracker stays pure state + thresholds."""
        self.idx += 1
        noise = robust_noise(R)
        if noise <= 0:
            noise = self.noise0
        N = (R - self.B) / noise

        if self.state in (SEARCH, CANDIDATE):
            self._search(R, N)
        elif self.state == LOCKED:
            self._locked(R)
        # VANISHED is terminal for a window (offline gate does not re-acquire)
        return self.state

    # -- SEARCH / CANDIDATE: K-peak tracking (§4.2) -----------------------------
    def _find_peaks(self, N, R):
        """Local maxima of N above K_APPEAR (NMS radius self.nms), keeping only those
        whose RESPONSE R is a ball-scale disc (is_blob, §4.4). Scans up to 12 maxima
        to fill K_PEAKS blobs, so a ridge distractor (line/shaft) is suppressed and
        skipped rather than occupying a candidate slot the ball needs."""
        M = N.copy()
        out = []
        for _ in range(12):
            cy, cx = np.unravel_index(int(np.argmax(M)), M.shape)
            v = float(M[cy, cx])
            if v < K_APPEAR:
                break
            cv2.circle(M, (int(cx), int(cy)), self.nms, -1e9, -1)   # suppress either way
            if is_blob(R, int(cx), int(cy), self.r):
                out.append((cx, cy, v))
                if len(out) >= K_PEAKS:
                    break
        return out

    def _search(self, R, N):
        peaks = self._find_peaks(N, R)
        stab2 = LOCK_STABILITY_PX ** 2
        used = [False] * len(peaks)
        kept = []
        # continue existing candidates that reappear within the stability radius
        for c in self.cands:
            best, bestd = -1, stab2 + 1e-9
            for i, (px, py, pv) in enumerate(peaks):
                if used[i]:
                    continue
                d = (px - c["x"]) ** 2 + (py - c["y"]) ** 2
                if d <= stab2 and d < bestd:
                    bestd, best = d, i
            if best >= 0:
                used[best] = True
                px, py, pv = peaks[best]
                c["x"], c["y"], c["hold"] = px, py, c["hold"] + 1
                c["Ns"].append(pv)
                kept.append(c)
            # else: candidate not seen this frame -> dropped (it moved / vanished)
        # start new candidates for unmatched peaks
        for i, (px, py, pv) in enumerate(peaks):
            if not used[i]:
                kept.append({"x": px, "y": py, "hold": 1, "Ns": [pv]})
        kept.sort(key=lambda c: c["hold"], reverse=True)
        self.cands = kept[:K_PEAKS]
        self.state = CANDIDATE if self.cands else SEARCH

        # lock the longest-held candidate that meets the criteria (the stationary ball)
        for c in self.cands:
            if c["hold"] >= self.t_lock_frames and float(np.median(c["Ns"])) >= K_LOCK:
                cx, cy = int(round(c["x"])), int(round(c["y"]))
                fx, fy = subpixel_peak(N, cx, cy)
                self._L0 = _at_spot(R, cx, cy)
                self.locked = {"idx": self.idx, "x": fx, "y": fy,
                               "L0": self._L0, "ix": cx, "iy": cy}
                self.state = LOCKED
                self._Lhist = [(self.idx, self._L0)]
                break

    # -- LOCKED / launch edge (§4.5) -------------------------------------------
    # Cliff test: at-spot response < COLLAPSE_FLOOR*L0 for COLLAPSE_FRAMES
    # consecutive frames, the frame BEFORE the run being >= COLLAPSE_FROM*L0.
    # Partial/occlusion dips never reach the floor for two frames, so they are
    # ignored without a separate hysteresis counter.
    def _locked(self, R):
        L = _at_spot(R, self.locked["ix"], self.locked["iy"])
        self._Lhist.append((self.idx, L))
        L0 = self._L0
        if len(self._Lhist) < COLLAPSE_FRAMES + 1:
            return
        window = self._Lhist[-(COLLAPSE_FRAMES + 1):]
        pre_idx, pre_L = window[0]
        below = window[1:]
        if pre_L >= COLLAPSE_FROM * L0 and all(l < COLLAPSE_FLOOR * L0 for _, l in below):
            first_collapse_idx = below[0][0]
            self.launched = {"idx": first_collapse_idx,
                             "x": self.locked["x"], "y": self.locked["y"]}
            if self.address_end_idx is not None and first_collapse_idx < self.address_end_idx:
                self.false_launches += 1
            self.state = VANISHED
