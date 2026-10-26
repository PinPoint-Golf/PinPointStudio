#!/usr/bin/env python3
"""trunk_hw — the trunk-IMU hardware measurement session, analysed.

docs/design/trunk_imu_sequence_grf_design.md §16 step 2: "Hardware measurements before the
estimator". Each subcommand answers one question that decides whether a planned piece of the
pipeline is needed at all, and writes a CSV plus a short human summary (also printed):

    rate  <recorder files | swing dirs ...>   §4.2  does BLE sustain 200 Hz with 3 Witmotions?
    still <recorder files | swing dirs ...>   §4.2  gyro bias / noise / drift over a still hold
    hop   <swing dirs ...>                    §6.3 step 4, §7.1  IMU <-> camera clock offset, trunk + wG3
    tilt  <refusion.json ...>                 §7.2  does the live filter keep trunk tilt?
    hips  <bound result dir> <unbound dir>    Part K  IMU hip peak vs the camera's
    ks    <bound> <unbound> <armunbound> [--fullswing DIR]
                                              §13, §14.4, §8.4  sensor vs camera peak per segment,
                                              the signed chart, plot_fullswing.py sheets
    selftest                                  synthetic data with known answers for all six

Inputs
  * Recorder files the app writes when PINPOINT_IMU_RECORD_DIR is set:
        # pinpoint.imu_record/1
        # serial=<serial> name=<name> role=<role or empty> configuredRateHz=<n> startedUtc=<ISO>
        t_us,ax,ay,az,gx,gy,gz,qw,qx,qy,qz
    t_us = host-arrival micros (bursty BLE), accel g, gyro deg/s, sensor frame.
  * Swing documents (swing.ppsw / swing.json), read ONLY through tools/pp_swingdoc.py.
    IMU lanes are doc["streams"][kind=="imu"] (src/Export/swing_exporter.cpp ~570-830):
    source.serial, device{outputRateHz, role, roleName, ...}, samples{t_us, data[[ax ay az gx gy
    gz qw qx qy qz]]}.
  * swinglab refusion output refusion.json (pinpoint.refusion/1).
  * swinglab result trees (<run>/<swing>/result.json) or written-back swing dirs.

TIME BASE (checked in the source, 17 Oct 2026). IMU samples.t_us and the face-on pose
analysis.pose2d.frames[].t_us are on ONE clock: both are EventBuffer host-monotonic stamps made
window-relative by subtracting the same t0 = window.startTimestampUs() (swing_exporter.cpp:141,
:305 video, :605 IMU; swing_doc.cpp:1182 poseTrackToJson's rel() subtracts the same windowT0 and
passes already-relative values through). Pose frames carry their video frame's stamp, which marks
exposure END (memory note camera-timestamps-from-the-camera); IMU stamps mark host ARRIVAL. So the
domains agree and the hop offset measures exactly the physical lag between the two kinds of
stamp — BLE latency plus burst queueing minus the exposure-end convention.

wG3 (HackMotion) lanes (checked 26 Oct 2026). The raw recorder takes Witmotions only, so the hop
reads the wG3 from the swing document: two imu streams with "instrument": "hackmotion", serial
"<mac>#lowerArm" and "<mac>#palm", device.role 0 (the role is in analysis.bindings: 5 leadForearm,
6 leadHand). Their samples.t_us are the wG3 clockFit's index -> host-time mapping
(device.history.clockFit, hm_session_worker.cpp ~1350), made window-relative by the same t0, so
they are on the video clock already; both units of one record carry the SAME stamp (skewUs is
provenance, not applied). The per-unit device_time_us is not written to the document, and is not
wanted here: the hop measures exactly what the analysis uses. The spacing is whatever the deferred
history pull covered: 1.25-2.5 ms inside it, 10 ms or 40 ms (live) outside (18 Aug swings).
The accel is gravity-REMOVED linear acceleration (≈0 at rest); |f| is rebuilt with the unit's own
quaternion (world->body), see wg3_fmag().

Stdlib only: the HackMotion venv this runs under has no numpy/scipy, and nothing here needs them.
The one exception is `ks`'s sheets: plot_fullswing.py runs as a subprocess under a Python that
has matplotlib (this one, else /opt/homebrew/bin/python3, else python3 on PATH).

    ~/Projects/.venv-hackmotion/bin/python -I tools/trunk/trunk_hw.py selftest
    ~/Projects/.venv-hackmotion/bin/python -I tools/trunk/trunk_hw.py rate --out DIR FILES...
"""

import argparse
import bisect
import csv
import datetime as _dt
import json
import math
import os
import random
import re
import shutil
import statistics
import sys
import tempfile

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.normpath(os.path.join(_HERE, "..")))  # tools/ -> pp_swingdoc
from pp_swingdoc import find_swing_dirs, has_swing, load_swing  # noqa: E402

G = 9.80665
RECORD_MAGIC = "# pinpoint.imu_record/1"
# SegmentRole (src/Analysis/segment_role.h) — APPEND-ONLY ints persisted in swing documents.
ROLE_BY_INT = {1: "pelvis", 2: "thorax", 3: "t12", 4: "leadUpperArm", 5: "leadForearm",
               6: "leadHand", 7: "trailThigh", 8: "leadThigh", 9: "club"}
TRUNK = ("pelvis", "thorax")
HEADER_KEYS = ("serial", "name", "role", "configuredRateHz", "startedUtc")


# ─────────────────────────────────────────────────────────────────────────────── small maths

def pct(xs, p):
    """Percentile p (0..100) by linear interpolation; None on empty."""
    if not xs:
        return None
    s = sorted(xs)
    k = (len(s) - 1) * p / 100.0
    lo = int(math.floor(k))
    hi = min(lo + 1, len(s) - 1)
    return s[lo] + (s[hi] - s[lo]) * (k - lo)


def linfit(xs, ys):
    """Least-squares y = a + b x; returns (a, b, residuals)."""
    n = len(xs)
    mx = sum(xs) / n
    my = sum(ys) / n
    sxx = sum((x - mx) ** 2 for x in xs)
    b = sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / sxx if sxx > 0 else 0.0
    a = my - b * mx
    return a, b, [y - (a + b * x) for x, y in zip(xs, ys)]


def solve(m, v):
    """Gaussian elimination with partial pivoting for a small dense system."""
    n = len(v)
    a = [row[:] + [v[i]] for i, row in enumerate(m)]
    for c in range(n):
        p = max(range(c, n), key=lambda r: abs(a[r][c]))
        a[c], a[p] = a[p], a[c]
        if abs(a[c][c]) < 1e-300:
            raise ZeroDivisionError("singular system")
        for r in range(c + 1, n):
            f = a[r][c] / a[c][c]
            for k in range(c, n + 1):
                a[r][k] -= f * a[c][k]
    x = [0.0] * n
    for r in range(n - 1, -1, -1):
        x[r] = (a[r][n] - sum(a[r][k] * x[k] for k in range(r + 1, n))) / a[r][r]
    return x


def local_poly(ts, ys, half, order):
    """Savitzky–Golay done as what it is: a least-squares polynomial of `order` over the 2*half+1
    nearest samples, evaluated at each sample. On uniform spacing this is exactly SG; on the real
    pose track (dropped frames, low-confidence frames removed) it stays correct because it fits
    the actual timestamps. Windows at the ends shift inward (same width) rather than shrink.
    Returns (value, first derivative, second derivative) lists, per unit of ts."""
    n = len(ts)
    w = 2 * half + 1
    if n < w:
        raise ValueError(f"need >= {w} samples for a {w}-point fit, have {n}")
    dts = [ts[i + 1] - ts[i] for i in range(n - 1)]
    scale = statistics.median(dts) or 1.0
    val, d1, d2 = [], [], []
    for i in range(n):
        lo = min(max(0, i - half), n - w)
        xs = [(ts[j] - ts[i]) / scale for j in range(lo, lo + w)]
        yy = ys[lo:lo + w]
        m = [[sum(x ** (r + c) for x in xs) for c in range(order + 1)] for r in range(order + 1)]
        v = [sum(y * x ** r for x, y in zip(xs, yy)) for r in range(order + 1)]
        c = solve(m, v)
        val.append(c[0])
        d1.append(c[1] / scale)
        d2.append(2.0 * c[2] / scale ** 2 if order >= 2 else 0.0)
    return val, d1, d2


def parabolic_peak(ts, ys, j):
    """Vertex time and height of the parabola through samples j-1, j, j+1 (non-uniform ts);
    falls back to the sample itself at the ends or on a degenerate fit."""
    if j <= 0 or j >= len(ts) - 1:
        return ts[j], ys[j]
    t0, t1, t2 = ts[j - 1], ts[j], ts[j + 1]
    y0, y1, y2 = ys[j - 1], ys[j], ys[j + 1]
    den = (t0 - t1) * (t0 - t2) * (t1 - t2)
    if den == 0:
        return t1, y1
    a = (t2 * (y1 - y0) + t1 * (y0 - y2) + t0 * (y2 - y1)) / den
    b = (t2 * t2 * (y0 - y1) + t1 * t1 * (y2 - y0) + t0 * t0 * (y1 - y2)) / den
    if a >= 0:
        return t1, y1
    tv = -b / (2 * a)
    tv = min(max(tv, t0), t2)
    c = y1 - a * t1 * t1 - b * t1
    return tv, a * tv * tv + b * tv + c


def sliding_min(xs, w):
    """Trailing minimum over the last w values (monotonic deque, O(n))."""
    from collections import deque
    q, out = deque(), []
    for i, x in enumerate(xs):
        while q and xs[q[-1]] >= x:
            q.pop()
        q.append(i)
        if q[0] <= i - w:
            q.popleft()
        out.append(xs[q[0]])
    return out


def norm3(a, b, c):
    return math.sqrt(a * a + b * b + c * c)


def sfmt(x, nd=1):
    """fmt() with the sign always shown (a time difference)."""
    return "-" if x is None or (isinstance(x, float) and not math.isfinite(x)) else f"{x:+.{nd}f}"


def fmt(x, nd=2, unit=""):
    if x is None or (isinstance(x, float) and not math.isfinite(x)):
        return "-"
    return f"{x:.{nd}f}{unit}"


# ─────────────────────────────────────────────────────────────────────────────── IMU lanes

class Lane:
    """One IMU stream: arrival stamps (us, relative within its source) and 10-wide rows."""

    def __init__(self, label, serial, role, configured_hz, t_us, rows, source,
                 instrument="witmotion", name="", rate_assumed=False):
        self.label = label
        self.serial = serial
        self.role = role or ""
        self.configured_hz = configured_hz
        self.t_us = t_us
        self.rows = rows
        self.source = source
        self.instrument = instrument
        self.name = name
        self.rate_assumed = rate_assumed

    def fmag(self):
        return [norm3(r[0], r[1], r[2]) for r in self.rows]

    def gmag(self):
        return [norm3(r[3], r[4], r[5]) for r in self.rows]


def _parse_header(line):
    body = line.lstrip("#").strip()
    keys = "|".join(HEADER_KEYS)
    out = {}
    for m in re.finditer(rf"({keys})=(.*?)(?=\s+(?:{keys})=|$)", body):
        out[m.group(1)] = m.group(2).strip()
    return out


def is_record_file(path):
    try:
        with open(path, encoding="utf-8", errors="replace") as fh:
            return fh.readline().strip() == RECORD_MAGIC
    except (OSError, IsADirectoryError):
        return False


def load_record_file(path):
    """A PINPOINT_IMU_RECORD_DIR file -> Lane."""
    hdr = {}
    t_us, rows = [], []
    with open(path, encoding="utf-8") as fh:
        first = fh.readline().strip()
        if first != RECORD_MAGIC:
            raise ValueError(f"{path}: not a {RECORD_MAGIC} file (first line {first!r})")
        for line in fh:
            line = line.strip()
            if not line:
                continue
            if line.startswith("#"):
                hdr.update(_parse_header(line))
                continue
            if line.startswith("t_us"):
                continue
            parts = line.split(",")
            if len(parts) < 11:
                continue
            try:
                t_us.append(int(float(parts[0])))
                rows.append([float(x) for x in parts[1:11]])
            except ValueError:
                continue
    try:
        hz = float(hdr.get("configuredRateHz") or 0) or None
    except ValueError:
        hz = None
    role = _norm_role(hdr.get("role", ""))
    serial = hdr.get("serial", "")
    label = role or hdr.get("name") or serial or os.path.basename(path)
    return Lane(label, serial, role, hz or 200.0, t_us, rows, path, name=hdr.get("name", ""),
                rate_assumed=hz is None)


def _norm_role(r):
    """'Pelvis' / 'pelvis' / 1 -> 'pelvis'; '' / 0 / unknown -> ''."""
    if r is None:
        return ""
    if isinstance(r, (int, float)) and not isinstance(r, bool):
        return ROLE_BY_INT.get(int(r), "")
    s = str(r).strip()
    if not s or s.lower() == "unknown":
        return ""
    if s.isdigit():
        return ROLE_BY_INT.get(int(s), "")
    return s[0].lower() + s[1:]


def lanes_from_doc(doc, source):
    """Every IMU stream of a loaded swing document as a Lane. Role from device.role / roleName,
    else the analysis.bindings row with the same serial (HackMotion lanes carry role 0 in the
    device block but the right role in bindings)."""
    by_serial = {}
    for b in (doc.get("analysis") or {}).get("bindings") or []:
        r = _norm_role(b.get("roleName")) or _norm_role(b.get("role"))
        if r and b.get("serial"):
            by_serial[b["serial"]] = r
    out = []
    for s in doc.get("streams") or []:
        if s.get("kind") != "imu":
            continue
        smp = s.get("samples") or {}
        t_us = [int(x) for x in smp.get("t_us") or []]
        rows = [list(map(float, r)) for r in smp.get("data") or []]
        if not t_us or len(t_us) != len(rows):
            continue
        serial = (s.get("source") or {}).get("serial", "")
        dev = s.get("device") or {}
        role = _norm_role(dev.get("roleName")) or _norm_role(dev.get("role")) or by_serial.get(serial, "")
        hz = dev.get("outputRateHz")
        assumed = not hz
        inst = s.get("instrument") or "witmotion"
        label = role or s.get("alias") or serial
        out.append(Lane(label, serial, role, float(hz) if hz else 200.0, t_us, rows, source,
                        instrument=inst, name=s.get("alias", ""), rate_assumed=assumed))
    return out


def gather_lanes(paths):
    """Recorder files, swing dirs/documents, or dirs holding recorder files."""
    lanes = []
    for p in paths:
        if os.path.isdir(p) and not has_swing(p):
            inner = [os.path.join(p, f) for f in sorted(os.listdir(p))
                     if os.path.isfile(os.path.join(p, f)) and is_record_file(os.path.join(p, f))]
            if inner:
                lanes += [load_record_file(f) for f in inner]
                continue
            for sd in find_swing_dirs(p):
                lanes += lanes_from_doc(load_swing(sd), sd)
            continue
        if os.path.isfile(p) and is_record_file(p):
            lanes.append(load_record_file(p))
        elif has_swing(p):
            lanes += lanes_from_doc(load_swing(p), p)
        else:
            print(f"[trunk_hw] skip {p}: neither a recorder file nor a swing", file=sys.stderr)
    return lanes


# ─────────────────────────────────────────────────────────────────────────────── re-timing (§7.1)

# The same algorithm as src/IMU/imu_retime.h (W2.4), kept line-for-line so the tool and the
# app date a Witmotion lane identically. Read the header for the why; in short:
#   * the period is FITTED (the WT901BLE67 runs ~0.7 % slow), by the lower convex hull of
#     (index, arrival), the edge spanning the mean index;
#   * the delivery floor is a SAWTOOTH (8-sample batches every ~40 ms against ~30 ms
#     connection events), so a short-window envelope step is NOT loss. Loss is read as a
#     persistent change of the batch residue (part of a batch) or a floor rise of at least
#     20 ms over 0.5 s either side (whole batches; 70 % of a batch when batching);
#   * a gap of 250 ms re-anchors (a new segment). The old default here, max(25 ms, 5T),
#     was SHORTER than the ~30 ms between bursts, so it cut a segment at almost every burst.
RETIME_CFG = {
    "segmentGapUs": 250000.0, "minFitSamples": 64, "minFitSpanUs": 2000000.0,
    "burstSplitFrac": 0.5, "envelopeWindowUs": 500000.0, "envelopeMinBursts": 4,
    "envelopeStepMinUs": 20000.0, "batchMinFraction": 0.6, "residuePersist": 2,
    "residueMaxChangesPer10s": 2.0,
    "maxResidualP95Us": 150000.0, "maxIterations": 4,
}


def _hull_fit(k, a):
    """Lower convex hull of (k, a); the edge spanning mean k. (c, T) or None."""
    n = len(k)
    if n < 2:
        return None
    h = []
    for i in range(n):
        while len(h) >= 2:
            i1, i2 = h[-2], h[-1]
            if (k[i2] - k[i1]) * (a[i] - a[i1]) - (a[i2] - a[i1]) * (k[i] - k[i1]) <= 0:
                h.pop()
            else:
                break
        h.append(i)
    kbar = sum(k) / n
    i1, i2 = h[0], h[-1]
    for j in range(len(h) - 1):
        if k[h[j]] <= kbar <= k[h[j + 1]]:
            i1, i2 = h[j], h[j + 1]
            break
    if k[i2] <= k[i1]:
        return None
    T = (a[i2] - a[i1]) / (k[i2] - k[i1])
    if not (T > 0 and math.isfinite(T)):
        return None
    return a[i1] - T * k[i1], T


def _deliveries(a, split_us):
    return [0] + [i for i in range(1, len(a)) if a[i] - a[i - 1] >= split_us] if a else []


def _dominant_size(starts, n, min_fraction):
    if len(starts) < 8:
        return 0
    sizes = [(starts[b + 1] if b + 1 < len(starts) else n) - starts[b] for b in range(len(starts))]
    hist = {}
    for s in sizes:
        hist[s] = hist.get(s, 0) + 1
    best, cnt = 0, 0
    for s in sorted(hist):
        if hist[s] > cnt:
            best, cnt = s, hist[s]
    return best if best >= 2 and cnt >= min_fraction * len(starts) else 0


def _residue_losses(starts, n, B, persist):
    """(lost, kind, changes)."""
    lost, kind, changes = {}, {}, 0
    if B < 2:
        return lost, kind, changes
    cum, settled, cand, cand_n, last = 0, -1, -1, 0, 0
    for b in range(len(starts)):
        sz = (starts[b + 1] if b + 1 < len(starts) else n) - starts[b]
        cum += sz
        if sz % B:
            continue
        rho = cum % B
        if settled < 0:
            settled, last = rho, b
            continue
        if rho == settled:
            cand, cand_n, last = -1, 0, b
            continue
        if rho == cand:
            cand_n += 1
        else:
            cand, cand_n = rho, 1
        if cand_n >= persist:
            at = starts[min(last + 1, len(starts) - 1)]
            lost[at] = lost.get(at, 0) + (settled - rho) % B
            kind[at] = "batchResidue"
            settled, cand, cand_n, last = rho, -1, 0, b
            changes += 1
    return lost, kind, changes


def _envelope_losses(a, k, starts, T, B, cfg):
    out = {}
    n, nb = len(a), len(starts)
    if nb < 2 * cfg["envelopeMinBursts"]:
        return out
    tb, fb = [], []
    for b in range(nb):
        e = starts[b + 1] if b + 1 < nb else n
        tb.append(a[starts[b]])
        fb.append(min(a[i] - T * k[i] for i in range(starts[b], e)))
    W, mb = cfg["envelopeWindowUs"], cfg["envelopeMinBursts"]
    back, fwd = [None] * nb, [None] * nb
    lo = hi = 0
    for b in range(nb):
        while tb[lo] < tb[b] - W:
            lo += 1
        hi = max(hi, b)
        while hi + 1 < nb and tb[hi + 1] <= tb[b] + W:
            hi += 1
        if b - lo >= mb:
            back[b] = min(fb[lo:b])
        if hi + 1 - b >= mb:
            fwd[b] = min(fb[b:hi + 1])
    thr = (max(cfg["envelopeStepMinUs"], 0.7 * B * T) if B >= 2
           else max(cfg["envelopeStepMinUs"], 0.6 * T))
    tol = max(0.4 * T, 2000.0)

    def step(b):
        return fwd[b] - back[b] if back[b] is not None and fwd[b] is not None else -1e300

    b = 1
    while b < nb:
        if step(b) < thr:
            b += 1
            continue
        rs, pre, S, re_ = b, back[b], 0.0, b
        while re_ < nb and step(re_) >= thr:
            S = max(S, step(re_))
            re_ += 1
        L = B * int(math.floor(S / (B * T) + 0.3)) if B >= 2 else int(round(S / T))
        if L < 1:
            b = re_
            continue
        bstar = next((j for j in range(rs, re_) if fwd[j] - pre >= S - tol), rs)
        btouch = next((j for j in range(bstar, nb) if fb[j] - pre <= S + tol), bstar)
        at, best = max(1, bstar), -1.0
        for j in range(max(1, bstar), btouch + 1):
            if tb[j] - tb[j - 1] > best:
                best, at = tb[j] - tb[j - 1], j
        out[starts[at]] = out.get(starts[at], 0) + L
        b = max(re_, btouch + 1)
    return out


def _fit_segment(a, fixed_T, hint_T, cfg):
    n = len(a)

    def index_with(L):
        k, cum = [], 0
        for i in range(n):
            cum += L.get(i, 0)
            k.append(float(i + cum))
        return k

    k = [float(i) for i in range(n)]
    T = fixed_T
    if T <= 0:
        f = _hull_fit(k, a)
        T = f[1] if f else hint_T
    starts = _deliveries(a, cfg["burstSplitFrac"] * T)
    B = _dominant_size(starts, n, cfg["batchMinFraction"])
    residue, kind, changes = _residue_losses(starts, n, B, cfg["residuePersist"])
    # A dominant delivery size is not always a device batch: one-sample-at-a-time delivery is
    # clustered by the connection interval too, and its residue walks for ever (see the header).
    span_s = (a[-1] - a[0]) / 1e6 if n > 1 else 0.0
    if changes > max(2.0, cfg["residueMaxChangesPer10s"] * span_s / 10.0):
        residue, kind, B = {}, {}, 0
    k_res = index_with(residue)
    env, allL, k = {}, dict(residue), k_res
    for it in range(cfg["maxIterations"]):
        if fixed_T <= 0:
            f = _hull_fit(k, a)
            if f:
                T = f[1]
        env2 = _envelope_losses(a, k_res, starts, T, B, cfg)
        allL = dict(residue)
        for i, L in env2.items():
            allL[i] = allL.get(i, 0) + L
        k = index_with(allL)
        if env2 == env and it > 0:
            break
        env = env2
    if fixed_T <= 0:
        f = _hull_fit(k, a)
        if f:
            T = f[1]
    c = min(a[i] - T * k[i] for i in range(n))
    for i in env:
        kind.setdefault(i, "envelopeStep")
    return {"c": c, "T": T, "k": k, "lost": allL, "kind": kind, "batch": B}


def retime_fit(t_us, nominal_hz, cfg=None):
    """§7.1 step 1 (W2.4): index x the stream's own fitted period, anchored on the lower envelope,
    loss inferred where the device count must have jumped. The Python twin of
    imu_retime::retimeArrivals — returns a dict with retimedUs, residualsUs, periodUs, segments,
    losses [(index, samples, kind)], lostSamples, batchSize, applied, why and residual p50/p95/max."""
    cfg = dict(RETIME_CFG, **(cfg or {}))
    n = len(t_us)
    out = {"applied": False, "why": "", "retimedUs": [float(x) for x in t_us], "residualsUs": [0.0] * n,
           "segments": [], "losses": [], "periodUs": 0.0, "batchSize": 0, "lostSamples": 0,
           "nominalPeriodUs": 1e6 / nominal_hz if nominal_hz else 0.0,
           "residualP50Us": 0.0, "residualP95Us": 0.0, "residualMaxUs": 0.0}
    if n < 2:
        out["why"] = "fewer than two samples"
        return out
    cuts = [0] + [i for i in range(1, n) if t_us[i] - t_us[i - 1] > cfg["segmentGapUs"]] + [n]
    t0 = float(t_us[0])
    hint_T = out["nominalPeriodUs"] or (t_us[-1] - t_us[0]) / (n - 1)
    segs = []
    for s, e in zip(cuts[:-1], cuts[1:]):
        fit = e - s >= cfg["minFitSamples"] and t_us[e - 1] - t_us[s] >= cfg["minFitSpanUs"]
        segs.append({"b": s, "e": e, "fit": fit})
    fitted = [i for i, s in enumerate(segs) if s["fit"]]
    if not fitted:
        cand = [i for i, s in enumerate(segs) if s["e"] - s["b"] >= cfg["minFitSamples"]]
        fitted = cand
        if cand:
            longest = max(cand, key=lambda i: segs[i]["e"] - segs[i]["b"])
            segs[longest]["fit"] = True
            fitted = [longest]
    longest = max(fitted, key=lambda i: segs[i]["e"] - segs[i]["b"]) if fitted else None

    def rel(s):
        return [float(t_us[i]) - float(t_us[s["b"]]) for i in range(s["b"], s["e"])]

    for s in segs:
        if s["fit"]:
            s["f"] = _fit_segment(rel(s), 0.0, hint_T, cfg)
    borrow_T = segs[longest]["f"]["T"] if longest is not None else hint_T
    for s in segs:
        if not s["fit"]:
            s["f"] = _fit_segment(rel(s), borrow_T, hint_T, cfg)
    out["periodUs"] = borrow_T
    rt, res = [0.0] * n, [0.0] * n
    prev_end = None
    for si, s in enumerate(segs):
        f, base = s["f"], float(t_us[s["b"]]) - t0
        out["segments"].append({"begin": s["b"], "end": s["e"], "periodUs": f["T"],
                                "source": "fitted" if s["fit"] else ("borrowed" if longest is not None
                                                                     else "nominal")})
        out["batchSize"] = max(out["batchSize"], f["batch"])
        for i, L in sorted(f["lost"].items()):
            if L > 0:
                out["losses"].append((s["b"] + i, L, f["kind"].get(i, "envelopeStep")))
        first = base + f["c"] + f["T"] * f["k"][0]
        if prev_end is not None:
            L = int(round((first - prev_end) / f["T"])) - 1
            if L > 0:
                out["losses"].append((s["b"], L, "segmentBreak"))
        for i in range(s["b"], s["e"]):
            t = base + f["c"] + f["T"] * f["k"][i - s["b"]]
            rt[i] = t0 + t
            res[i] = float(t_us[i]) - (t0 + t)
        prev_end = base + f["c"] + f["T"] * f["k"][-1]
    out["losses"].sort()
    out["lostSamples"] = sum(L for _, L, _ in out["losses"])
    out["residualP50Us"], out["residualP95Us"], out["residualMaxUs"] = pct(res, 50), pct(res, 95), max(res)
    if out["residualP95Us"] > cfg["maxResidualP95Us"]:
        out["why"] = "arrivals do not follow one sample clock"
        return out
    out.update({"applied": True, "retimedUs": rt, "residualsUs": res})
    return out


def retime(t_us, nominal_hz):
    """(retimed_us list, arrival-minus-retimed residuals us) — see retime_fit()."""
    f = retime_fit(t_us, nominal_hz)
    return f["retimedUs"], f["residualsUs"]


# ─────────────────────────────────────────────────────────────────────────────── rate

def rate_stats(lane, burst_ms=1.0):
    t = lane.t_us
    n = len(t)
    nom = lane.configured_hz or 200.0
    T_ms = 1000.0 / nom
    dur = (t[-1] - t[0]) / 1e6 if n > 1 else 0.0
    dts = [(t[i] - t[i - 1]) / 1000.0 for i in range(1, n)]
    mean_hz = (n - 1) / dur if dur > 0 else None
    expected = int(round(dur * nom)) + 1
    short = max(0, expected - n)
    hist = [0] * 41  # 40 bins of 0.5 ms over 0–20 ms, then >= 20 ms
    for d in dts:
        k = int(d / 0.5) if d >= 0 else 0
        hist[min(k, 40)] += 1
    # Clusters: runs of arrivals < burst_ms apart (one BLE connection event's delivery).
    clusters = []
    cur = 1
    starts = [0]
    for i, d in enumerate(dts, start=1):
        if d < burst_ms:
            cur += 1
        else:
            clusters.append(cur)
            cur = 1
            starts.append(i)
    clusters.append(cur)
    half_t = [1]
    for d in dts:
        if d < T_ms / 2:
            half_t[-1] += 1
        else:
            half_t.append(1)
    cl_iv = [(t[starts[k]] - t[starts[k - 1]]) / 1000.0 for k in range(1, len(starts))]
    # Gaps: arrival dt > 2 nominal periods. On bursty BLE most are inter-burst spacing, not loss.
    # Loss shows in the deficit D_j = (t_j - t_0)/T - j: lossless bursty delivery only lifts D
    # transiently (queueing), but every lost sample lifts its LOWER ENVELOPE by one for good. The
    # envelope is the trailing 1 s minimum of D (long enough for the queueing delay to touch its
    # floor); each rise of >= 0.75 sample above the settled level is a loss event, attributed to
    # the longest arrival gap in that trailing second (a later fall retracts it: a backlog that
    # drained for > 1 s); events on the same gap or within 100 ms are one. A slow device clock reads as a slow envelope rise, i.e. as loss — the same thing
    # 'short' counts.
    gaps = sum(1 for d in dts if d > 2 * T_ms)
    D = [(t[j] - t[0]) / (T_ms * 1000.0) - j for j in range(n)]
    W = max(4, int(round(1.0 * nom)))
    B = sliding_min(D, W)
    events = []   # [gap index, samples lost]; a later fall of the envelope retracts the newest
    if n > W:
        level = B[W - 1]
        for j in range(W, n):
            if B[j] < level:
                # The envelope fell: what looked like loss was a backlog still draining (or the
                # latency floor improved). Loss is permanent, so retract it, newest first.
                k = int(math.floor(level - B[j] + 0.25))
                while k > 0 and events:
                    r = min(k, events[-1][1])
                    events[-1][1] -= r
                    k -= r
                    if events[-1][1] == 0:
                        events.pop()
                level = B[j]
                continue
            lost = int(math.floor(B[j] - level + 0.25))
            if lost <= 0:
                continue
            level += lost
            g = max(range(max(1, j - W + 1), j + 1), key=lambda i: t[i] - t[i - 1])
            if events and events[-1][0] == g:
                events[-1][1] += lost
            else:
                events.append([g, lost])
    gap_loss = sum(e[1] for e in events)
    # The re-timing's own read (W2.4): the period fitted per stream, so a slow device clock
    # is not counted as loss — the deficit envelope above, against the NOMINAL period, does.
    rf = retime_fit(t, nom) if n >= 2 else None
    lossy_gaps, last = 0, None
    for g, _ in events:
        if last is None or t[g] - t[last] > 100000:
            lossy_gaps += 1
        last = g
    return {
        "device": lane.label, "serial": lane.serial, "role": lane.role,
        "instrument": lane.instrument, "source": lane.source,
        "configuredHz": nom, "rateAssumed": lane.rate_assumed,
        "samples": n, "durationS": dur, "meanHz": mean_hz,
        "pctOfConfigured": (100.0 * mean_hz / nom) if mean_hz else None,
        "p50GapMs": pct(dts, 50), "p95GapMs": pct(dts, 95), "p99GapMs": pct(dts, 99),
        "maxGapMs": max(dts) if dts else None,
        "gapsOver2T": gaps, "lossyGaps": lossy_gaps, "gapLostEst": gap_loss,
        "expectedSamples": expected, "shortSamples": short,
        "lostPct": 100.0 * short / expected if expected else None,
        "burstMs": burst_ms, "clusters": len(clusters),
        "meanCluster": statistics.mean(clusters) if clusters else None,
        "maxCluster": max(clusters) if clusters else None,
        "clusterSizeHist": {k: clusters.count(k) for k in sorted(set(clusters))},
        "p50ClusterIntervalMs": pct(cl_iv, 50),
        "clustersHalfT": len(half_t), "meanClusterHalfT": statistics.mean(half_t),
        "maxClusterHalfT": max(half_t),
        "hist": hist,
        "retimeApplied": bool(rf and rf["applied"]),
        "retimePeriodUs": rf["periodUs"] if rf else None,
        "retimeHz": 1e6 / rf["periodUs"] if rf and rf["periodUs"] else None,
        "retimeBatch": rf["batchSize"] if rf else None,
        "retimeSegments": len(rf["segments"]) if rf else None,
        "retimeP50Ms": rf["residualP50Us"] / 1000.0 if rf else None,
        "retimeP95Ms": rf["residualP95Us"] / 1000.0 if rf else None,
        "retimeMaxMs": rf["residualMaxUs"] / 1000.0 if rf else None,
        "retimeLossEvents": len(rf["losses"]) if rf else None,
        "retimeLostSamples": rf["lostSamples"] if rf else None,
    }


def rate_verdict(r):
    nom = r["configuredHz"]
    mh, lp = r["meanHz"], r["lostPct"]
    if mh is None:
        return f"{r['device']}: too few samples"
    assumed = (f" (configured rate unknown, {nom:g} Hz assumed)" if r["rateAssumed"] else "")
    if abs(nom - 200.0) < 1e-6:
        if mh >= 0.97 * 200.0 and lp < 1.0:
            return f"{r['device']}: 200 Hz sustained ({mh:.1f} Hz mean, {lp:.2f} % short){assumed}"
        return (f"{r['device']}: 200 Hz NOT sustained — achieved {mh:.1f} Hz mean "
                f"({r['pctOfConfigured']:.1f} % of 200), {lp:.2f} % of samples short{assumed}")
    ok = mh >= 0.97 * nom and lp < 1.0
    return (f"{r['device']}: configured {nom:g} Hz (not 200) — achieved {mh:.1f} Hz "
            f"({r['pctOfConfigured']:.1f} %), {lp:.2f} % short -> "
            f"{'sustained' if ok else 'NOT sustained'}")


def cmd_rate(paths, out, burst_ms=1.0, quiet=False, assume_hz=None):
    lanes = gather_lanes(paths)
    if not lanes:
        raise SystemExit("rate: no IMU lanes found")
    if assume_hz:  # only for lanes whose configured rate was never recorded
        for ln in lanes:
            if ln.rate_assumed:
                ln.configured_hz = float(assume_hz)
    res = [rate_stats(ln, burst_ms) for ln in lanes]
    cols = ["source", "device", "serial", "role", "instrument", "configuredHz", "rateAssumed",
            "samples", "durationS", "meanHz", "pctOfConfigured", "p50GapMs", "p95GapMs",
            "p99GapMs", "maxGapMs", "gapsOver2T", "lossyGaps", "gapLostEst", "expectedSamples",
            "shortSamples", "lostPct", "clusters", "meanCluster", "maxCluster",
            "p50ClusterIntervalMs", "clustersHalfT", "meanClusterHalfT", "maxClusterHalfT",
            "retimeApplied", "retimePeriodUs", "retimeHz", "retimeBatch", "retimeSegments",
            "retimeP50Ms", "retimeP95Ms", "retimeMaxMs", "retimeLossEvents", "retimeLostSamples"]
    write_csv(os.path.join(out, "rate.csv"), cols, res)
    hrows = []
    for r in res:
        for k, c in enumerate(r["hist"]):
            hrows.append({"source": r["source"], "device": r["device"], "binLoMs": k * 0.5,
                          "binHiMs": (k + 1) * 0.5 if k < 40 else "", "count": c})
    write_csv(os.path.join(out, "rate_hist.csv"),
              ["source", "device", "binLoMs", "binHiMs", "count"], hrows)
    lines = ["trunk_hw rate — design §4.2 / §16 step 2 (BLE rate)", ""]
    for r in res:
        lines.append(f"{r['device']}  [{r['serial']}]  {os.path.basename(str(r['source']))}")
        lines.append(f"  {r['samples']} samples over {r['durationS']:.2f} s, mean "
                     f"{fmt(r['meanHz'], 1)} Hz vs configured {r['configuredHz']:g}"
                     f"{' (assumed)' if r['rateAssumed'] else ''}; "
                     f"short {r['shortSamples']} of {r['expectedSamples']} ({fmt(r['lostPct'])} %)")
        lines.append(f"  inter-arrival p50 {fmt(r['p50GapMs'])} / p95 {fmt(r['p95GapMs'])} / "
                     f"p99 {fmt(r['p99GapMs'])} / max {fmt(r['maxGapMs'])} ms")
        lines.append(f"  gaps > 2T: {r['gapsOver2T']}; steps in the deficit's lower envelope at the "
                     f"NOMINAL period: {r['lossyGaps']}, ~{r['gapLostEst']} samples (a slow device "
                     f"clock reads as these)")
        if r["retimePeriodUs"]:
            lines.append(f"  re-timed (§7.1, W2.4): period {r['retimePeriodUs']:.2f} us "
                         f"({r['retimeHz']:.3f} Hz) in {r['retimeSegments']} segment(s), batches of "
                         f"{r['retimeBatch'] or '-'}; arrival - retimed p50 {fmt(r['retimeP50Ms'])} / "
                         f"p95 {fmt(r['retimeP95Ms'])} / max {fmt(r['retimeMaxMs'])} ms; loss "
                         f"{r['retimeLossEvents']} event(s), {r['retimeLostSamples']} sample(s)"
                         f"{'' if r['retimeApplied'] else ' — NOT applied'}")
        hist = ", ".join(f"{k}:{v}" for k, v in list(r["clusterSizeHist"].items())[:8])
        lines.append(f"  bursts (< {burst_ms:g} ms apart): {r['clusters']} clusters, mean "
                     f"{fmt(r['meanCluster'])} / max {r['maxCluster']} samples, cluster interval "
                     f"p50 {fmt(r['p50ClusterIntervalMs'])} ms; sizes {hist}")
        lines.append(f"  sub-period clusters (< T/2 = {500.0 / r['configuredHz']:.2f} ms apart): "
                     f"{r['clustersHalfT']}, mean {fmt(r['meanClusterHalfT'])} / max "
                     f"{r['maxClusterHalfT']} samples")
        lines.append("  VERDICT " + rate_verdict(r))
        if r["instrument"] == "hackmotion":
            lines.append("  note: a HackMotion lane's device.outputRateHz is not its stream rate "
                         "(live + deferred history are stitched); read the achieved mean only")
        elif r["rateAssumed"] and r["meanHz"] and abs(r["meanHz"] - 100.0) < 5.0 \
                and r["configuredHz"] != 100.0:
            lines.append("  note: no configured rate recorded and the mean is ~100 Hz — consistent "
                         "with the Witmotion 100 Hz default, so 'short' against 200 is not loss")
        lines.append("")
    lines.append("'short' = expected (duration x configured rate + 1) minus received: the loss "
                 "figure the verdict uses (it also absorbs a slow device clock). 'gaps' counts "
                 "arrival gaps > 2 periods; on bursty BLE most are inter-burst spacing, so a gap "
                 "counts as loss only where the deficit (t - t0)/T - index steps up for good.")
    finish(out, "rate", lines, quiet)
    return res


# ─────────────────────────────────────────────────────────────────────────────── still

def still_span(lane, gyro_dps=3.0, f_tol=0.05, debounce=2):
    """Longest span with |gyro| < gyro_dps and | |f| - 1 g | < f_tol; up to `debounce`
    consecutive failing samples (a BLE glitch, a heartbeat) do not end the span."""
    gm, fm = lane.gmag(), lane.fmag()
    ok = [g < gyro_dps and abs(f - 1.0) < f_tol for g, f in zip(gm, fm)]
    best = (0, -1)
    start, last_good, bad = None, None, 0
    for i, o in enumerate(ok + [False] * (debounce + 1)):
        if o:
            if start is None:
                start = i
            last_good = i
            bad = 0
        elif start is not None:
            bad += 1
            if bad > debounce:
                if lane.t_us[last_good] - lane.t_us[start] > \
                        (lane.t_us[best[1]] - lane.t_us[best[0]] if best[1] >= 0 else -1):
                    best = (start, last_good)
                start, bad = None, 0
    return best


def still_stats(lane, block_s=30.0):
    a, b = still_span(lane)
    if b < 0 or b - a < 10:
        return None
    t = [(x - lane.t_us[a]) / 1e6 for x in lane.t_us[a:b + 1]]
    rows = lane.rows[a:b + 1]
    tm = [x / 60.0 for x in t]
    out = {"device": lane.label, "serial": lane.serial, "role": lane.role,
           "instrument": lane.instrument, "source": lane.source,
           "spanStartS": (lane.t_us[a] - lane.t_us[0]) / 1e6, "spanS": t[-1], "samples": len(t)}
    blocks = []
    nb = int(t[-1] // block_s)
    for k in range(nb + 1):
        sel = [i for i, x in enumerate(t) if k * block_s <= x < (k + 1) * block_s]
        if len(sel) < 10 or (k == nb and (t[-1] - k * block_s) < block_s / 3):
            continue
        blocks.append({"device": lane.label, "serial": lane.serial, "blockStartS": k * block_s,
                       "samples": len(sel),
                       **{f"g{ax}Mean": statistics.mean(rows[i][3 + j] for i in sel)
                          for j, ax in enumerate("xyz")}})
    for j, ax in enumerate("xyz"):
        g = [r[3 + j] for r in rows]
        _, slope, res = linfit(tm, g)
        out[f"g{ax}Bias"] = statistics.mean(g)
        out[f"g{ax}Sd"] = statistics.pstdev(g)
        out[f"g{ax}SdDetrended"] = statistics.pstdev(res)
        out[f"g{ax}SlopePerMin"] = slope
    f = [norm3(r[0], r[1], r[2]) for r in rows]
    out["fMeanG"] = statistics.mean(f)
    out["fDevPct"] = 100.0 * (out["fMeanG"] - 1.0)
    out["fSdG"] = statistics.pstdev(f)
    bias = [out[f"g{ax}Bias"] for ax in "xyz"]
    out["worstBias"] = max(abs(x) for x in bias)
    out["biasNorm"] = norm3(*bias)
    out["drift15sWorstDeg"] = out["worstBias"] * 1.5
    out["drift15sNormDeg"] = out["biasNorm"] * 1.5
    out["worstSlopePerMin"] = max(abs(out[f"g{ax}SlopePerMin"]) for ax in "xyz")
    return out, blocks


def cmd_still(paths, out, quiet=False):
    lanes = gather_lanes(paths)
    res, blocks, lines = [], [], ["trunk_hw still — design §4.2 bias, §6.3 sensor-health gates", ""]
    for ln in lanes:
        if ln.instrument == "hackmotion":
            lines.append(f"{ln.label}: HackMotion lane skipped (accel is gravity-removed; the "
                         "still test is for Witmotion raw lanes)")
            continue
        r = still_stats(ln)
        if r is None:
            lines.append(f"{ln.label}: no still span found (|gyro| < 3 deg/s, |f| within 5 % of 1 g)")
            continue
        s, b = r
        res.append(s)
        blocks += b
        lines.append(f"{s['device']}  [{s['serial']}]  {os.path.basename(str(s['source']))}")
        lines.append(f"  still span {s['spanS']:.1f} s from {s['spanStartS']:.1f} s "
                     f"({s['samples']} samples)" + ("  ⚠ shorter than 60 s" if s["spanS"] < 60 else ""))
        for ax in "xyz":
            lines.append(f"  g{ax}: bias {s[f'g{ax}Bias']:+.3f} deg/s, SD {s[f'g{ax}Sd']:.3f} "
                         f"(detrended {s[f'g{ax}SdDetrended']:.3f}), drift "
                         f"{s[f'g{ax}SlopePerMin']:+.4f} deg/s per min")
        lines.append(f"  |f| mean {s['fMeanG']:.4f} g ({s['fDevPct']:+.2f} % from 1 g), SD "
                     f"{s['fSdG']:.4f} g")
        if s["worstBias"] < 0.5:
            lines.append(f"  VERDICT bias {s['worstBias']:.3f} deg/s < 0.5 -> bias estimator not "
                         f"needed for a 1.5 s swing (drift over 1.5 s: worst axis "
                         f"{s['drift15sWorstDeg']:.2f} deg, vector {s['drift15sNormDeg']:.2f} deg)")
        else:
            lines.append(f"  VERDICT bias {s['worstBias']:.3f} deg/s >= 0.5 -> bias estimator "
                         f"needed (§7.2 address-stillness bias); drift over 1.5 s "
                         f"{s['drift15sNormDeg']:.2f} deg")
        lines.append(f"  VERDICT |f| {s['fDevPct']:+.2f} % -> "
                     f"{'within' if abs(s['fDevPct']) <= 3.0 else 'OUTSIDE'} the §6.3 ±3 % health gate (seated on W0)")
        lines.append("")
    cols = ["source", "device", "serial", "role", "instrument", "spanStartS", "spanS", "samples",
            "gxBias", "gyBias", "gzBias", "gxSd", "gySd", "gzSd",
            "gxSdDetrended", "gySdDetrended", "gzSdDetrended",
            "gxSlopePerMin", "gySlopePerMin", "gzSlopePerMin", "worstBias", "biasNorm",
            "drift15sWorstDeg", "drift15sNormDeg", "worstSlopePerMin", "fMeanG", "fDevPct", "fSdG"]
    write_csv(os.path.join(out, "still.csv"), cols, res)
    write_csv(os.path.join(out, "still_blocks.csv"),
              ["device", "serial", "blockStartS", "samples", "gxMean", "gyMean", "gzMean"], blocks)
    finish(out, "still", lines, quiet)
    return res


# ─────────────────────────────────────────────────────────────────────────────── hop

DIP_G, SPIKE_G = 0.4, 2.0
MIN_FLIGHT_MS = 80.0                # a 2 cm hop is ~130 ms in the air; a swing's jolts are shorter
SG_HALF, SG_ORDER = 4, 3            # 9-point cubic: 53 ms at 150 fps
SG_SENSITIVITY = (4, 5, 6)          # 9/11/13-point windows: the fit term's spread
XCORR_PRE_S, XCORR_POST_S = 0.15, 0.5   # video frames from apex-150 ms to apex+500 ms


def imu_landing(lane, timeline_us, f=None):
    """Largest |f| spike (> 2 g) within 250 ms after a flight dip (|f| < 0.4 g for >= 80 ms).
    Returns dict or None; times in us on `timeline_us`. `f` overrides lane.fmag() (the wG3)."""
    f = lane.fmag() if f is None else f
    n = len(f)
    best = None
    i = 0
    while i < n:
        if f[i] < DIP_G:
            j = i
            while j + 1 < n and f[j + 1] < DIP_G:
                j += 1
            if j - i + 1 >= 2 and timeline_us[j] - timeline_us[i] >= MIN_FLIGHT_MS * 1000:
                k = j + 1
                pk = None
                while k < n and timeline_us[k] - timeline_us[j] <= 250000:
                    if pk is None or f[k] > f[pk]:
                        pk = k
                    k += 1
                if pk is not None and f[pk] > SPIKE_G and (best is None or f[pk] > best["peakG"]):
                    best = {"dipStartUs": timeline_us[i], "dipEndUs": timeline_us[j],
                            "peakIdx": pk, "peakG": f[pk]}
            i = j + 1
        else:
            i += 1
    if best is None:
        return None
    tv, _ = parabolic_peak([float(x) for x in timeline_us], f, best["peakIdx"])
    best["landUs"] = tv
    best["flightMs"] = (best["dipEndUs"] - best["dipStartUs"]) / 1000.0
    return best


WG3_MAX_SPACING_MS = 12.5   # passes the ~100 Hz history rate (10.01 ms on the 1.25 ms index grid);
                            # the 40 ms live rate gives the ~25 ms landing pulse < 1 sample
WG3_WINDOW_MS = 150.0       # no flight dip on the wrist: largest |f| this close to the video landing


def wg3_unit(lane):
    """'palm' / 'lowerArm' from the wG3 serial's '#unit' suffix, else the bound role."""
    parts = lane.serial.rsplit("#", 1)
    return parts[1] if len(parts) == 2 and parts[1] else (lane.role or lane.label)


def wg3_fmag(lane, up=1.0):
    """|f| (g) for a wG3 lane, whose accel is gravity-removed linear acceleration (≈0 at rest):
    f = a + up_body, up_body = q (0,0,up) q* with q world->body. The direction was checked against
    the gyro on 18 Aug Wrist_02 swing_0001 (world->body fits the gyro ~14x better than its
    conjugate). Whether the wG3 world's up is +Z or -Z is not documented; wg3_up() lets the hop
    decide. Only gravity's direction is taken from the quaternion, never heading (§3.7 #7)."""
    out = []
    for r in lane.rows:
        w, x, y, z = r[6], r[7], r[8], r[9]
        ub = (2 * (x * z + w * y) * up, 2 * (y * z - w * x) * up, (1 - 2 * (x * x + y * y)) * up)
        out.append(norm3(r[0] + ub[0], r[1] + ub[1], r[2] + ub[2]))
    return out


def wg3_up(lane, t_lo, t_hi):
    """In flight the true |f| is ~0 and the wrong up gives ~2 g, so the sign whose minimum |f|
    over [t_lo, t_hi] is lower wins. Returns (up, f) or (None, None) when no sample is there."""
    idx = [i for i, t in enumerate(lane.t_us) if t_lo <= t <= t_hi]
    if not idx:
        return None, None
    best = None
    for up in (1.0, -1.0):
        f = wg3_fmag(lane, up)
        m = min(f[i] for i in idx)
        if best is None or m < best[1]:
            best = (up, m, f)
    return best[0], best[2]


def hip_track(doc, min_conf=0.3):
    """Face-on hip centre: mean image y of COCO-WholeBody 11 and 12 (normalised, y DOWN)."""
    frames = ((doc.get("analysis") or {}).get("pose2d") or {}).get("frames") or []
    ts, ys = [], []
    for fr in frames:
        kp = fr.get("kp") or []
        if len(kp) < 39:
            continue
        y11, c11 = kp[34], kp[35]
        y12, c12 = kp[37], kp[38]
        if None in (y11, y12, c11, c12) or c11 < min_conf or c12 < min_conf:
            continue
        ts.append(float(fr["t_us"]))
        ys.append(0.5 * (y11 + y12))
    return ts, ys


def video_accel(ts_us, ys, half=SG_HALF, order=SG_ORDER):
    """Smoothed hip height and its upward acceleration (-y'', normalised image units / s^2);
    apex = highest smoothed hip (min y); landing = largest upward-acceleration peak in the 400 ms
    after it (parabolic sub-frame)."""
    ts = [t / 1e6 for t in ts_us]
    val, _, d2 = local_poly(ts, ys, half, order)
    up = [-a for a in d2]
    ia = min(range(len(val)), key=lambda i: val[i])
    cand = [i for i in range(ia + 1, len(ts)) if ts[i] - ts[ia] <= 0.4]
    if not cand:
        return None
    ip = max(cand, key=lambda i: up[i])
    tv, pk = parabolic_peak(ts, up, ip)
    return {"apexIdx": ia, "apexUs": ts[ia] * 1e6, "landUs": tv * 1e6, "peakUp": pk,
            "apexRise": statistics.median(val) - val[ia], "up": up}


def xcorr_offset(ts_us, va, rt_us, fmag, guess_us):
    """Design §7.1 step 2: Pearson cross-correlation of the IMU's (|f| - 1 g) on its timeline
    against the video's upward hip acceleration over the hop (apex -150 ms .. +500 ms). The IMU
    is interpolated at (frame time + tau); the video's SG kernel is symmetric, so smoothing one
    side only does not move the correlation peak. Coarse 2 ms search +-200 ms around the
    peak-picking guess, then 0.1 ms within +-4 ms, parabolic refinement. Returns (tau_us, r)."""
    ia = va["apexIdx"]
    t_ap = ts_us[ia]
    sel = [i for i in range(len(ts_us))
           if -XCORR_PRE_S * 1e6 <= ts_us[i] - t_ap <= XCORR_POST_S * 1e6]
    a = [(x - 1.0) * G for x in fmag]
    up = va["up"]

    def corr_at(tau):
        xs, vs = [], []
        for i in sel:
            t = ts_us[i] + tau
            k = bisect.bisect_left(rt_us, t)
            if k <= 0 or k >= len(rt_us):
                continue
            u = (t - rt_us[k - 1]) / ((rt_us[k] - rt_us[k - 1]) or 1.0)
            xs.append(a[k - 1] * (1 - u) + a[k] * u)
            vs.append(up[i])
        if len(xs) < 10 or statistics.pstdev(xs) == 0 or statistics.pstdev(vs) == 0:
            return None
        return statistics.correlation(xs, vs)

    def scan(c0, half, step):
        taus, cs = [], []
        tau = c0 - half
        while tau <= c0 + half + 1e-9:
            c = corr_at(tau)
            if c is not None:
                taus.append(tau)
                cs.append(c)
            tau += step
        return taus, cs

    taus, cs = scan(guess_us, 200000, 2000)
    if not cs:
        return None, None
    j = max(range(len(cs)), key=lambda k: cs[k])
    taus, cs = scan(taus[j], 4000, 100)
    j = max(range(len(cs)), key=lambda k: cs[k])
    tv, cv = parabolic_peak(taus, cs, j)
    return tv, cv


def wg3_hop(ln, ts, vids, frame_ms):
    """One wG3 unit through the hop, like a trunk sensor but on its own fit-dated stamps (no
    re-timing: samples.t_us are already index x period through the clockFit) and on |f| rebuilt
    by wg3_fmag(). The landing is the trunk rule's dip + spike when the wrist shows one, else the
    largest |f| within WG3_WINDOW_MS of the video landing (the arm need not be ballistic)."""
    v = vids[SG_HALF]
    s = {"sensor": f"wG3 {wg3_unit(ln)}", "serial": ln.serial, "role": ln.role,
         "instrument": "hackmotion"}
    up, f = wg3_up(ln, v["apexUs"] - 300000, v["landUs"] + 300000)
    if up is None:
        s["error"] = "the wG3 lane holds no sample over the hop (apex -300 .. landing +300 ms)"
        return s
    rt = [float(x) for x in ln.t_us]
    lr = imu_landing(ln, rt, f)
    anchor = "dip"
    if lr is None or abs(lr["landUs"] - v["landUs"]) > 500000:
        near = [i for i, t in enumerate(rt) if abs(t - v["landUs"]) <= WG3_WINDOW_MS * 1000]
        if not near:
            s["error"] = f"no wG3 sample within {WG3_WINDOW_MS:.0f} ms of the video landing"
            return s
        pk = max(near, key=lambda i: f[i])
        tv, _ = parabolic_peak(rt, f, pk)
        lr = {"landUs": tv, "peakG": f[pk], "flightMs": None}
        anchor = "window"
    gaps = [rt[i + 1] - rt[i] for i in range(len(rt) - 1) if abs(rt[i] - lr["landUs"]) <= 100000]
    spacing = statistics.median(gaps) / 1000.0 if gaps else None
    guess = lr["landUs"] - v["landUs"]
    taus = {}
    for h, vv in vids.items():
        tau, r = xcorr_offset(ts, vv, rt, f, guess)
        if tau is not None:
            taus[h] = (tau, r)
    s.update({"upSign": int(up), "landingAnchor": anchor, "spacingMs": spacing,
              "coarse": spacing is None or spacing > WG3_MAX_SPACING_MS})
    if SG_HALF not in taus:
        s["error"] = "cross-correlation found no overlap between the wG3 and the video"
        return s
    tv = [x[0] for x in taus.values()]
    fit = 0.5 * (max(tv) - min(tv)) / 1000.0
    s.update({"imuLandUs": lr["landUs"], "peakG": lr["peakG"], "flightMs": lr["flightMs"],
              "offsetMs": taus[SG_HALF][0] / 1000.0, "xcorrR": taus[SG_HALF][1],
              "offsetPeakMs": guess / 1000.0, "fitMs": fit, "residualMs": frame_ms / 2.0 + fit})
    return s


def hop_one(path, doc):
    all_lanes = lanes_from_doc(doc, path)
    wg3 = [ln for ln in all_lanes if ln.instrument == "hackmotion"]
    lanes = [ln for ln in all_lanes if ln.instrument != "hackmotion"]
    trunk = [ln for ln in lanes if ln.role in TRUNK]
    warn = []
    if not trunk:
        trunk = lanes
        if lanes:
            warn.append("no lane bound pelvis/thorax — using every Witmotion lane, labelled by serial")
    ts, ys = hip_track(doc)
    out = {"swing": path, "warn": warn, "sensors": []}
    if len(ts) < 2 * max(SG_SENSITIVITY) + 1:
        out["error"] = f"face-on pose track too short ({len(ts)} usable hip frames)"
        return out
    frame_ms = statistics.median([(ts[i + 1] - ts[i]) / 1000.0 for i in range(len(ts) - 1)])
    vids = {h: v for h in SG_SENSITIVITY if (v := video_accel(ts, ys, h, SG_ORDER))}
    if SG_HALF not in vids:
        out["error"] = "no video landing found (no upward-acceleration peak after the apex)"
        return out
    v = vids[SG_HALF]
    lands = [x["landUs"] for x in vids.values()]
    out.update({"frameMs": frame_ms, "videoLandUs": v["landUs"], "videoApexUs": v["apexUs"],
                "apexRise": v["apexRise"],
                "videoFitMs": 0.5 * (max(lands) - min(lands)) / 1000.0})
    for ln in trunk:
        rt, res = retime(ln.t_us, ln.configured_hz)
        lr = imu_landing(ln, rt)
        s = {"sensor": ln.label, "serial": ln.serial, "role": ln.role, "instrument": ln.instrument,
             "spacingMs": 1000.0 / ln.configured_hz}
        if lr is None:
            s["error"] = (f"no landing (no |f| < {DIP_G} g dip of >= {MIN_FLIGHT_MS:.0f} ms followed "
                          f"by a > {SPIKE_G} g spike) — is there a hop in this shot?")
            out["sensors"].append(s)
            continue
        fm = ln.fmag()
        guess = lr["landUs"] - v["landUs"]
        taus = {}
        for h, vv in vids.items():
            tau, r = xcorr_offset(ts, vv, rt, fm, guess)
            if tau is not None:
                taus[h] = (tau, r)
        raw_tau, _ = xcorr_offset(ts, v, [float(x) for x in ln.t_us], fm, guess)
        if SG_HALF not in taus:
            s["error"] = "cross-correlation found no overlap between the IMU and the video"
            out["sensors"].append(s)
            continue
        tv = [x[0] for x in taus.values()]
        fit = 0.5 * (max(tv) - min(tv)) / 1000.0
        s.update({"imuLandUs": lr["landUs"], "peakG": lr["peakG"], "flightMs": lr["flightMs"],
                  "offsetMs": taus[SG_HALF][0] / 1000.0, "xcorrR": taus[SG_HALF][1],
                  "offsetRawMs": raw_tau / 1000.0 if raw_tau is not None else None,
                  "offsetPeakMs": guess / 1000.0,
                  "fitMs": fit, "residualMs": frame_ms / 2.0 + fit,
                  "retimeP50Ms": pct(res, 50) / 1000.0, "retimeP95Ms": pct(res, 95) / 1000.0})
        if abs(s["offsetMs"]) > 500:
            warn.append(f"{ln.label}: offset {s['offsetMs']:+.0f} ms is implausible for one "
                        "clock — check that the shot holds the hop in view of the face-on camera")
        out["sensors"].append(s)
    ok = [s for s in out["sensors"] if "offsetMs" in s]
    if len(ok) >= 2:
        out["agreementMs"] = abs(ok[0]["offsetMs"] - ok[1]["offsetMs"])
        out["agreementPeakMs"] = abs(ok[0]["imuLandUs"] - ok[1]["imuLandUs"]) / 1000.0
    # §7.1 "the arm is a third clock": both wG3 units, kept apart from the trunk's own figures.
    # Witmotion <-> wG3 = mean wG3 offset - mean trunk offset = t_wG3 - t_Witmotion for one event
    # (it includes the body's own landing lag from sacrum to wrist). Coarse units still report,
    # with their ⚠, but only resolved units make the hop's figure unless none is resolved.
    out["wg3"] = [wg3_hop(ln, ts, vids, frame_ms) for ln in wg3]
    wg = [u for u in out["wg3"] if "offsetMs" in u]
    for u in wg:
        if u["coarse"]:
            warn.append(f"{u['sensor']}: the landing is sampled every {fmt(u['spacingMs'], 1)} ms "
                        f"(> {WG3_MAX_SPACING_MS:.1f} ms): the ~25 ms pulse is not resolved; "
                        "offset shown, left out of the Witmotion <-> wG3 verdict")
    if len(wg) >= 2:
        out["wg3AgreeMs"] = abs(wg[0]["offsetMs"] - wg[1]["offsetMs"])
    use = [u for u in wg if not u["coarse"]] or wg
    if ok and use:
        out["witWg3Ms"] = (statistics.mean(u["offsetMs"] for u in use)
                           - statistics.mean(s["offsetMs"] for s in ok))
        out["witWg3Coarse"] = all(u["coarse"] for u in use)
    return out


def cmd_hop(paths, out, quiet=False):
    rows, results = [], []
    lines = ["trunk_hw hop — design §6.3 step 4 / §7.1 step 2 (IMU <-> camera clock offset)", "",
             f"IMU landing: largest |f| > {SPIKE_G} g within 250 ms after a |f| < {DIP_G} g dip "
             f"(>= {MIN_FLIGHT_MS:.0f} ms), parabolic peak, on the §7.1 re-timed timeline.",
             "Video landing: hip centre (mean of COCO-WholeBody 11, 12; conf >= 0.3) image y, "
             f"Savitzky–Golay {2 * SG_HALF + 1}-point cubic (local least squares on the real frame "
             "times), second derivative, upward = -y''; largest peak <= 400 ms after the apex.",
             "offset = t_imu - t_video from the §7.1 cross-correlation of IMU (|f| - 1 g) with the "
             "video's upward hip acceleration over the hop (apex -150..+500 ms), seeded by the "
             "two landing peaks (offsetPeak). offsetRaw = the same on raw arrival stamps.",
             "Residual = half the frame period + fit, fit = half the spread of the offset over "
             f"{'/'.join(str(2 * h + 1) for h in SG_SENSITIVITY)}-point SG windows.",
             "wG3 (palm, lower arm), when worn: read from the swing document on its clockFit "
             "stamps (no re-timing), |f| rebuilt from the gravity-removed accel with the unit's "
             "own quaternion (up sign chosen by the flight dip); landing by the same dip + spike "
             f"rule, else the largest |f| within {WG3_WINDOW_MS:.0f} ms of the video landing; the "
             "same cross-correlation and residual. Witmotion <-> wG3 = mean wG3 offset - mean "
             "trunk offset per hop (t_wG3 - t_Witmotion for one event).", ""]
    swings = []
    for p in paths:
        swings += [p] if has_swing(p) else find_swing_dirs(p)
    for sd in swings:
        r = hop_one(sd, load_swing(sd))
        results.append(r)
        lines.append(f"{sd}")
        for w in r["warn"]:
            lines.append(f"  ⚠ {w}")
        if "error" in r:
            lines.append(f"  ✗ {r['error']}")
            rows.append({"swing": sd, "error": r["error"]})
            continue
        lines.append(f"  video: landing {r['videoLandUs'] / 1000:.1f} ms, apex rise "
                     f"{r['apexRise']:.4f} (normalised image units), frame {r['frameMs']:.2f} ms")
        for s in r["sensors"]:
            row = {"swing": sd, "frameMs": r["frameMs"], "videoLandMs": r["videoLandUs"] / 1000,
                   "agreementMs": r.get("agreementMs"), "witWg3Ms": r.get("witWg3Ms"),
                   "wg3AgreeMs": r.get("wg3AgreeMs"), **s}
            if "offsetMs" in s:
                row["imuLandMs"] = s["imuLandUs"] / 1000
                lines.append(f"  {s['sensor']}: offset t_imu - t_video {s['offsetMs']:+.2f} ms "
                             f"(r {s['xcorrR']:.3f}; peaks {s['offsetPeakMs']:+.2f}; raw arrival "
                             f"{fmt(s['offsetRawMs'])}), fit {s['fitMs']:.2f} -> residual "
                             f"{s['residualMs']:.2f} ms; landing {s['peakG']:.2f} g after a "
                             f"{s['flightMs']:.0f} ms dip; arrival lag over re-timed p50 "
                             f"{s['retimeP50Ms']:.1f} / p95 {s['retimeP95Ms']:.1f} ms")
            else:
                lines.append(f"  {s['sensor']}: ✗ {s['error']}")
            rows.append(row)
        if "agreementMs" in r:
            lines.append(f"  sensors agree within {r['agreementMs']:.2f} ms "
                         f"(landing peaks {r['agreementPeakMs']:.2f} ms)")
        for s in r.get("wg3", []):
            row = {"swing": sd, "frameMs": r["frameMs"], "videoLandMs": r["videoLandUs"] / 1000,
                   "agreementMs": r.get("agreementMs"), "witWg3Ms": r.get("witWg3Ms"),
                   "wg3AgreeMs": r.get("wg3AgreeMs"), **s}
            if "offsetMs" in s:
                row["imuLandMs"] = s["imuLandUs"] / 1000
                land = (f"{s['peakG']:.2f} g after a {s['flightMs']:.0f} ms dip"
                        if s["landingAnchor"] == "dip" else
                        f"{s['peakG']:.2f} g, largest within {WG3_WINDOW_MS:.0f} ms of the video "
                        "landing (no flight dip)")
                lines.append(f"  {s['sensor']}: offset t_imu - t_video {s['offsetMs']:+.2f} ms "
                             f"(r {s['xcorrR']:.3f}; peaks {s['offsetPeakMs']:+.2f}), fit "
                             f"{s['fitMs']:.2f} -> residual {s['residualMs']:.2f} ms; landing "
                             f"{land}; samples every {fmt(s['spacingMs'], 1)} ms there; up "
                             f"{'+' if s['upSign'] > 0 else '-'}Z")
            else:
                lines.append(f"  {s['sensor']}: ✗ {s['error']}")
            rows.append(row)
        if "witWg3Ms" in r:
            agree = (f"; wG3 units agree within {r['wg3AgreeMs']:.2f} ms"
                     if "wg3AgreeMs" in r else "")
            lines.append(f"  Witmotion <-> wG3: t_wG3 - t_Witmotion {r['witWg3Ms']:+.2f} ms"
                         f"{' ⚠ coarse wG3 only' if r['witWg3Coarse'] else ''}{agree}")
        lines.append("")
    cols = ["swing", "sensor", "serial", "role", "offsetMs", "xcorrR", "offsetPeakMs",
            "offsetRawMs", "imuLandMs", "videoLandMs", "peakG", "flightMs", "frameMs", "fitMs",
            "residualMs", "agreementMs", "retimeP50Ms", "retimeP95Ms", "error",
            "instrument", "spacingMs", "landingAnchor", "upSign", "witWg3Ms", "wg3AgreeMs"]
    write_csv(os.path.join(out, "hop.csv"), cols, rows)
    summ = hop_summary(results)
    lines += summ["lines"]
    finish(out, "hop", lines, quiet)
    return results, summ


def hop_summary(results):
    good = [r for r in results if "error" not in r and any("offsetMs" in s for s in r["sensors"])]
    lines = [f"OVER {len(good)} HOP(S)"]
    if not good:
        return {"lines": lines + ["  no usable hop"], "n": 0, "pass": False}
    per_sensor = {}
    session = []
    for r in good:
        offs = [s["offsetMs"] for s in r["sensors"] if "offsetMs" in s]
        session.append(statistics.mean(offs))
        for s in r["sensors"]:
            if "offsetMs" in s:
                per_sensor.setdefault(s["sensor"], []).append(s["offsetMs"])
    out = {"n": len(good), "meanMs": statistics.mean(session),
           "sdMs": statistics.stdev(session) if len(session) > 1 else None,
           "rangeMs": max(session) - min(session), "perSensor": {}}
    lines.append(f"  session offset (mean of the sensors per hop): {out['meanMs']:+.2f} ms, SD "
                 f"{fmt(out['sdMs'])} ms (repeatability), range {out['rangeMs']:.2f} ms")
    for k, v in per_sensor.items():
        sd = statistics.stdev(v) if len(v) > 1 else None
        out["perSensor"][k] = {"meanMs": statistics.mean(v), "sdMs": sd}
        lines.append(f"  {k}: mean {statistics.mean(v):+.2f} ms, SD {fmt(sd)} ms, range "
                     f"{max(v) - min(v):.2f} ms over {len(v)}")
    res_max = max(s["residualMs"] for r in good for s in r["sensors"] if "residualMs" in s)
    agr = [r["agreementMs"] for r in good if "agreementMs" in r]
    agr_max = max(agr) if agr else None
    out["residualMaxMs"] = res_max
    out["agreementMaxMs"] = agr_max
    ok_res = res_max < 5.0
    ok_agr = agr_max is not None and agr_max < 5.0
    lines.append(f"  VERDICT residual worst {res_max:.2f} ms -> {'PASS' if ok_res else 'FAIL'} "
                 "(§6.3: < 5 ms)")
    lines.append(f"  VERDICT sensors agree worst {fmt(agr_max)} ms -> "
                 f"{'PASS' if ok_agr else ('FAIL' if agr_max is not None else 'UNTESTED (one sensor)')} "
                 "(§6.3: within 5 ms)")
    if out["sdMs"] is not None:
        rep = ("one offset per session is enough" if out["sdMs"] < 5.0 else
               "NOT repeatable: the per-swing clock check (§7.1 step 3) must carry the offset")
        lines.append(f"  VERDICT repeatability SD {out['sdMs']:.2f} ms -> {rep}")
    out["lines"] = lines
    out["pass"] = ok_res and ok_agr
    out.update(wg3_summary(results, lines))
    return out


def wg3_summary(results, lines):
    """§7.1: the verdict that matters for the chart — the Witmotion <-> wG3 offset per hop and
    its spread across hops. Appends to `lines`; adds nothing when no hop carried a wG3."""
    units = [u for r in results if "error" not in r for u in r.get("wg3", [])]
    if not units:
        return {}
    lines.append("  wG3 (offset to the video per unit, coarse hops included; Witmotion <-> wG3 "
                 "uses resolved units only):")
    per_unit = {}
    for u in units:
        if "offsetMs" in u:
            per_unit.setdefault(u["sensor"], []).append(u["offsetMs"])
    for k, v in per_unit.items():
        sd = statistics.stdev(v) if len(v) > 1 else None
        lines.append(f"  {k}: mean {statistics.mean(v):+.2f} ms, SD {fmt(sd)} ms, range "
                     f"{max(v) - min(v):.2f} ms over {len(v)}")
    hops = [r for r in results if "witWg3Ms" in r]
    good = [r["witWg3Ms"] for r in hops if not r["witWg3Coarse"]]
    coarse = [r for r in hops if r["witWg3Coarse"]]
    out = {"wg3N": len(good), "wg3PerUnit": {k: statistics.mean(v) for k, v in per_unit.items()},
           "witWg3": good, "wg3Pass": False}
    sp = [u["spacingMs"] for u in units if u.get("spacingMs") is not None]
    if sp:
        lines.append(f"  wG3 samples at the landing every {min(sp):.1f}..{max(sp):.1f} ms "
                     f"(resolved when <= {WG3_MAX_SPACING_MS:.1f} ms)")
    for r in coarse:
        lines.append(f"  ⚠ {r['swing']}: t_wG3 - t_Witmotion {r['witWg3Ms']:+.2f} ms from coarse "
                     "wG3 samples only — shown, left out of the verdict")
    if not good:
        lines.append("  VERDICT Witmotion <-> wG3 -> UNTESTED (no hop with a resolved wG3 landing)")
        return out
    med = statistics.median(good)
    sd = statistics.stdev(good) if len(good) > 1 else None
    rng = max(good) - min(good)
    res_w = max(u["residualMs"] for r in hops if not r["witWg3Coarse"]
                for u in r["wg3"] if "residualMs" in u and not u["coarse"])
    out.update({"witWg3MedianMs": med, "witWg3SdMs": sd, "witWg3RangeMs": rng,
                "wg3ResidualMaxMs": res_w})
    lines.append(f"  Witmotion <-> wG3 (t_wG3 - t_Witmotion) per hop: "
                 f"{', '.join(f'{x:+.2f}' for x in good)} ms -> median {med:+.2f} ms, SD "
                 f"{fmt(sd)} ms, range {rng:.2f} ms over {len(good)}")
    ok_res = res_w < 5.0
    lines.append(f"  VERDICT wG3 residual worst {res_w:.2f} ms -> {'PASS' if ok_res else 'FAIL'} "
                 "(§6.3: < 5 ms)")
    if sd is None:
        lines.append("  VERDICT Witmotion <-> wG3 spread -> UNTESTED (one hop; §7.1 needs its "
                     "spread across hops)")
        return out
    ok_sp = sd < 5.0
    lines.append(f"  VERDICT Witmotion <-> wG3 spread SD {sd:.2f} ms -> "
                 + ("PASS: one offset per session ties the arm to the trunk (§7.1: within 5 ms)"
                    if ok_sp else
                    "FAIL: the arm-to-trunk gap carries both clocks' σ; the per-swing arm check "
                    "(top reversal, §7.1) must carry the offset"))
    out["wg3Pass"] = ok_res and ok_sp
    return out


# ─────────────────────────────────────────────────────────────────────────────── tilt

TILT_KEYS = ("addressDeg", "topDeg", "impactDeg", "finishDeg", "finishVsAddressGravityDeg")


def cmd_tilt(paths, out, quiet=False):
    rows = []
    for p in paths:
        with open(p, encoding="utf-8") as fh:
            doc = json.load(fh)  # refusion.json is a swinglab research output, not a swing doc
        if doc.get("schema") != "pinpoint.refusion/1":
            print(f"[trunk_hw] {p}: schema {doc.get('schema')!r}, reading anyway", file=sys.stderr)
        for s in doc.get("sources") or []:
            role = _norm_role(s.get("roleName")) or _norm_role(s.get("role"))
            tilt = s.get("tilt")
            row = {"file": p, "serial": s.get("serial", ""), "role": role,
                   "samples": s.get("samples"), "parityMaxDeg": s.get("maxDeg"),
                   "tilt": "present" if isinstance(tilt, dict) else "absent"}
            for k in TILT_KEYS:
                v = tilt.get(k) if isinstance(tilt, dict) else None
                row[k] = v if isinstance(v, (int, float)) and not isinstance(v, bool) else None
            rows.append(row)
    write_csv(os.path.join(out, "tilt.csv"),
              ["file", "serial", "role", "samples", "parityMaxDeg", "tilt"] + list(TILT_KEYS), rows)
    lines = ["trunk_hw tilt — design §7.2 (live vs re-fused trunk tilt)", "",
             f"{'role':<12} {'serial':<20} " + " ".join(f"{k[:-3]:>9}" for k in TILT_KEYS[:4])
             + "  finishVsAddrG"]
    for r in rows:
        if r["tilt"] == "absent":
            lines.append(f"{r['role'] or '-':<12} {r['serial'][-20:]:<20} tilt fields absent")
            continue
        lines.append(f"{r['role'] or '-':<12} {r['serial'][-20:]:<20} "
                     + " ".join(f"{fmt(r[k]):>9}" for k in TILT_KEYS[:4])
                     + f"  {fmt(r['finishVsAddressGravityDeg']):>8}")
    have = [r for r in rows if r["impactDeg"] is not None]
    trunk = [r for r in have if r["role"] in TRUNK]
    pool = trunk or have
    lines.append("")
    if not pool:
        verdict = "UNDECIDED: no lane carries tilt.impactDeg (tilt fields absent)"
    else:
        worst = max(pool, key=lambda r: abs(r["impactDeg"]))
        scope = "trunk lanes" if trunk else "all lanes (none bound pelvis/thorax)"
        if abs(worst["impactDeg"]) < 1.0:
            verdict = (f"max |impact tilt error| {abs(worst['impactDeg']):.2f} deg < 1 deg over "
                       f"{len(pool)} {scope} -> live filter keeps tilt; trunk re-fusion optional")
        else:
            verdict = (f"max |impact tilt error| {abs(worst['impactDeg']):.2f} deg ({worst['role'] or worst['serial']}) "
                       f">= 1 deg over {len(pool)} {scope} -> re-fusion needed (§7.2)")
    lines.append("VERDICT " + verdict)
    finish(out, "tilt", lines, quiet)
    return rows, verdict


# ─────────────────────────────────────────────────────────────────────────────── hips

RESULT_NAMES = ("result.json", "swing.json", "swing.ppsw")


def newest_doc(d):
    """The newest of result.json / swing.json / swing.ppsw in a dir — swinglab's old rename onto
    an existing result.json failed silently and left a stale one beside the fresh swing.json
    (memory note swinglab-run-output-files), so the newer file is the run's answer."""
    c = [os.path.join(d, n) for n in RESULT_NAMES if os.path.isfile(os.path.join(d, n))]
    if not c:
        return None, []
    c.sort(key=lambda p: os.path.getmtime(p), reverse=True)
    return c[0], c[1:]


def collect_results(root):
    """{key: (file, skipped)} for a result.json file, a swing dir, or a tree of them."""
    if os.path.isfile(root):
        return {os.path.basename(os.path.dirname(os.path.abspath(root))): (root, [])}
    out = {}
    for dirpath, dirnames, _files in os.walk(root):
        dirnames[:] = sorted(d for d in dirnames if not d.startswith("."))
        f, skipped = newest_doc(dirpath)
        if f:
            key = os.path.relpath(dirpath, root)
            out[os.path.basename(os.path.abspath(root)) if key == "." else key] = (f, skipped)
    return out


def pelvis_node(path):
    doc = load_swing(path)
    ks = (doc.get("analysis") or {}).get("kinematicSequence") or {}
    for n in ks.get("nodes") or []:
        if n.get("segment") == "pelvis":
            return n
    return None


def cmd_hips(bound, unbound, out, quiet=False):
    A, B = collect_results(bound), collect_results(unbound)
    keys = sorted(set(A) & set(B))
    if not keys:  # fall back to swing-dir basenames when unique on both sides
        ba = {os.path.basename(k): k for k in A}
        bb = {os.path.basename(k): k for k in B}
        if len(ba) == len(A) and len(bb) == len(B):
            common = sorted(set(ba) & set(bb))
            A = {k: A[ba[k]] for k in common}
            B = {k: B[bb[k]] for k in common}
            keys = common
    rows, lines = [], ["trunk_hw hips — Part K: the hip (pelvis) KS node, trunk sensors bound vs "
                       "ignored", "", "ms vs impact: positive = the peak came AFTER impact "
                       "(= -beforeImpactMs).", ""]
    for k in keys:
        fa, sa = A[k]
        fb, sb = B[k]
        na, nb = pelvis_node(fa), pelvis_node(fb)
        row = {"swing": k, "boundFile": fa, "unboundFile": fb}
        for tag, n in (("imu", na), ("cam", nb)):
            if n:
                row[f"{tag}Placed"] = bool(n.get("placed"))
                row[f"{tag}Route"] = n.get("routeId", "")
                bi = n.get("beforeImpactMs")
                row[f"{tag}VsImpactMs"] = -float(bi) if bi is not None and n.get("placed") else None
                row[f"{tag}SigmaMs"] = n.get("tSigmaMs")
        if row.get("imuVsImpactMs") is not None and row.get("camVsImpactMs") is not None:
            row["diffMs"] = row["imuVsImpactMs"] - row["camVsImpactMs"]
            row["diffSigmaMs"] = math.hypot(row.get("imuSigmaMs") or 0.0, row.get("camSigmaMs") or 0.0)
        notes = []
        if sa or sb:
            notes.append("older " + ",".join(os.path.basename(x) for x in sa + sb) + " ignored")
        if na and na.get("routeId") and "Imu" not in na.get("routeId", ""):
            notes.append(f"bound run's pelvis route is {na.get('routeId')!r}, not an IMU route")
        if nb and "Imu" in (nb.get("routeId") or ""):
            notes.append(f"unbound run's pelvis route is {nb.get('routeId')!r} (IMU!)")
        row["note"] = "; ".join(notes)
        rows.append(row)
        lines.append(f"{k:<40} IMU {fmt(row.get('imuVsImpactMs'), 1):>7} ± {fmt(row.get('imuSigmaMs'), 1):<5}"
                     f" [{row.get('imuRoute', '-')}]   cam {fmt(row.get('camVsImpactMs'), 1):>7} ± "
                     f"{fmt(row.get('camSigmaMs'), 1):<5} [{row.get('camRoute', '-')}]   diff "
                     f"{fmt(row.get('diffMs'), 1):>7}" + (f"   ⚠ {row['note']}" if row["note"] else ""))
    cols = ["swing", "imuVsImpactMs", "imuSigmaMs", "imuPlaced", "imuRoute", "camVsImpactMs",
            "camSigmaMs", "camPlaced", "camRoute", "diffMs", "diffSigmaMs", "note",
            "boundFile", "unboundFile"]
    write_csv(os.path.join(out, "hips.csv"), cols, rows)
    both = [r for r in rows if r.get("diffMs") is not None]
    imu = [r["imuVsImpactMs"] for r in rows if r.get("imuVsImpactMs") is not None]
    cam = [r["camVsImpactMs"] for r in rows if r.get("camVsImpactMs") is not None]
    summ = {"n": len(both), "matched": len(keys)}
    lines.append("")
    lines.append(f"{len(keys)} swings matched, {len(both)} with both hip peaks placed")
    if both:
        summ["medianDiffMs"] = statistics.median(r["diffMs"] for r in both)
        summ["medianImuMs"] = statistics.median(imu)
        summ["medianCamMs"] = statistics.median(cam)
        after = sum(1 for r in both if r["imuVsImpactMs"] > (r.get("imuSigmaMs") or 0.0))
        summ["imuAfterBeyondSigma"] = after
        survives = summ["medianImuMs"] > 0
        summ["survives"] = survives
        lines.append(f"median IMU hip peak {summ['medianImuMs']:+.1f} ms vs impact, camera "
                     f"{summ['medianCamMs']:+.1f} ms; median difference (IMU - camera) "
                     f"{summ['medianDiffMs']:+.1f} ms")
        lines.append(f"IMU puts the hip peak after impact by more than its σ on {after}/{len(both)}")
        lines.append("VERDICT " + (
            f"the camera's 'hips peak after impact' SURVIVES with the IMU (median "
            f"{summ['medianImuMs']:+.1f} ms)" if survives else
            f"the camera's 'hips peak after impact' does NOT survive: the IMU puts the hip peak "
            f"{-summ['medianImuMs']:.1f} ms BEFORE impact (median)"))
    else:
        lines.append("VERDICT undecided: no swing has both nodes placed")
    finish(out, "hips", lines, quiet)
    return rows, summ


# ─────────────────────────────────────────────────────────────────────────────── ks

# Design §13 / §14.4 / §8.4. Keys read from the result documents (checked against
# src/Kinematics/kinematic_sequence_json.h and an 18 Aug wG3 swing, 26 Oct 2026):
#   analysis.kinematicSequence{impactUs, order[], verdict, nodes[{segment pelvis|thorax|leadArm|club,
#     placed, tPeakUs, beforeImpactMs, tSigmaMs, routeId, probable{peakMs, tSigmaMs, how}}]}
#   analysis.metrics[{key pelvisAngularSpeed|thoraxAngularSpeed|leadArmAngularSpeed|
#     clubAngularSpeed, t_us[], value[] (signed: towards the target +), valid[]}]
#   analysis.phases[{phase, t_us}], phase 2 = the top (P4).
# ⚠ By default (sequence.throughP8) a series is CUT to P4..P8, so a reversal before the top is not in
# it; the studio's `fullswing` pass (the bound pass with {"sequence":{"throughP8":false}}) keeps the
# curve whole, and `ks --fullswing` reads the signed-chart check and the bound sheet from it.
KS_SEGS = ("pelvis", "thorax", "leadArm", "club")
KS_SERIES = {"pelvis": "pelvisAngularSpeed", "thorax": "thoraxAngularSpeed",
             "leadArm": "leadArmAngularSpeed", "club": "clubAngularSpeed"}
KS_CAMERA_PASS = {"pelvis": "unbound", "thorax": "unbound", "leadArm": "armunbound"}
KS_PROBABLE_KIND = {"bridge": "bridged", "seen": "indicated", "rising": "rising", "fell": "fell"}
PHASE_TOP, PHASE_TRANSITION = 2, 3
KS_TOP_WINDOW_MS = 250.0    # a reversal further than this from the top is another event
# X for the §8.4 check. The body reverses proximal to distal through the transition: the hips start
# down a few tens of ms before the club's top, the chest after them, the arm near the top, so the
# three crossings spread over a few tens of ms by design. A sign or axis error does not nudge a
# crossing by tens of ms: it inverts the curve (the polarity check) or moves the crossing to another
# event — takeaway ~0.7 s before the top, or past impact ~0.25 s after it. 80 ms from the three's
# median sits above the natural transition spread and well below either.
KS_CROSS_TOL_MS = 80.0
KS_SEG_GAP_MS = 60.0        # samples further apart than this do not make a crossing (pose gaps)
BOOT_N = 4000
ESTIMATED_MEDIAN_MS, ESTIMATED_COVERAGE = 20.0, 0.68   # §14.4 / KS §9 stage 2


def match_results(roots):
    """{name: root} -> (keys, {name: {key: (file, skipped)}}): swings present in every root, by
    relative path, else by swing-dir basename when that is unique in each root (like `hips`)."""
    maps = {k: collect_results(r) for k, r in roots.items()}
    keys = set.intersection(*(set(m) for m in maps.values())) if maps else set()
    if not keys:
        bases = {k: {os.path.basename(x): x for x in m} for k, m in maps.items()}
        if all(len(bases[k]) == len(maps[k]) for k in maps):
            keys = set.intersection(*(set(b) for b in bases.values()))
            maps = {k: {b: maps[k][bases[k][b]] for b in keys} for k in maps}
    return sorted(keys), maps


def ks_peak(node):
    """A node's peak as ms vs impact (+ after), its σ_t, route and kind: 'placed'; for an unplaced
    chest with a probable peak 'bridged' / 'indicated' / 'rising' / 'fell' (gap_bridge.h: rising and
    fell are the earliest / latest the peak can be); else 'unplaced' with no time."""
    if not node:
        return None
    p = {"route": node.get("routeId", ""), "kind": "unplaced", "ms": None, "sigmaMs": None}
    pr = node.get("probable")
    if node.get("placed") and node.get("beforeImpactMs") is not None:
        p.update(kind="placed", ms=-float(node["beforeImpactMs"]), sigmaMs=node.get("tSigmaMs"))
    elif pr and pr.get("peakMs") is not None:
        p.update(kind=KS_PROBABLE_KIND.get(pr.get("how", "bridge"), pr.get("how", "probable")),
                 ms=float(pr["peakMs"]), sigmaMs=pr.get("tSigmaMs"))
    return p


def ks_order(ks):
    order = ">".join(ks.get("order") or []) or "-"
    return f"{order} ({ks.get('verdict') or '-'})"


def phase_time(a, phase):
    for p in a.get("phases") or []:
        if p.get("phase") == phase:
            return p.get("t_us")
    return None


def signed_crossing(a, seg):
    """§8.4: the segment's reversal into the downswing on its signed series. Polarity = the sign of
    the series' area between the top and impact (the downswing; + = towards the target): an area,
    not the largest |rate|, because a camera curve's blind dip near square can outweigh its true
    peak (18 Aug face-on pelvis). The crossing = walking back from the downswing's largest rate of
    that sign to the last sign change (linear interpolation), in ms from the top. None when the series is absent; crossMs None when the walk ran off the series'
    start (a cut series) or the crossing is more than KS_TOP_WINDOW_MS from the top."""
    m = next((m for m in a.get("metrics") or [] if m.get("key") == KS_SERIES[seg]), None)
    top = phase_time(a, PHASE_TOP)
    imp = (a.get("kinematicSequence") or {}).get("impactUs")
    if not m or top is None or imp is None:
        return None
    # As plot_fullswing.py draws it: inside the sequence's domain (transition, else top, to impact)
    # an invalid sample is unmeasured; outside it a sample is masked only because the sequence does
    # not read it, so it counts unless it is an exact 0.
    valid = m.get("valid") or [1] * len(m["t_us"])
    lo = phase_time(a, PHASE_TRANSITION)
    lo = top if lo is None else lo
    pts = [(float(t), float(v)) for t, v, k in zip(m["t_us"], m["value"], valid)
           if k or (not lo <= t <= imp and v != 0.0)]
    down = [i for i, (t, _) in enumerate(pts) if top <= t <= imp]
    if not down:
        return {"crossMs": None, "polarity": 0, "note": "no sample between the top and impact"}
    area = sum(pts[i][1] * (pts[i + 1][0] - pts[i][0]) for i in down if i + 1 < len(pts))
    pol = 1 if area >= 0 else -1
    ipk = max(down, key=lambda i: pol * pts[i][1])
    out = {"polarity": pol, "crossMs": None, "route": m.get("routeId", "")}
    j = ipk
    while j > 0 and (pts[j - 1][1] > 0) == (pol > 0) and pts[j - 1][1] != 0.0:
        j -= 1
    if j == 0:
        out["note"] = (f"no reversal in the series (it starts {(pts[0][0] - top) / 1000:+.0f} ms "
                       "from the top: cut at P4? run the fullswing pass)")
        return out
    (t0, v0), (t1, v1) = pts[j - 1], pts[j]
    if t1 - t0 > KS_SEG_GAP_MS * 1000:
        out["note"] = f"reversal inside a {(t1 - t0) / 1000:.0f} ms gap in the series"
        return out
    tc = t0 + (t1 - t0) * (-v0 / (v1 - v0) if v1 != v0 else 0.0)
    if abs(tc - top) > KS_TOP_WINDOW_MS * 1000:
        out["note"] = f"reversal {(tc - top) / 1000:+.0f} ms from the top (> {KS_TOP_WINDOW_MS:.0f})"
        return out
    out["crossMs"] = (tc - top) / 1000.0
    return out


def signed_check(a):
    """§8.4 on one swing: pelvis, chest and arm crossings and polarities; flags for a polarity
    against the others' (or against + when they split evenly) and for a crossing more than
    KS_CROSS_TOL_MS from the median crossing."""
    cr = {s: signed_crossing(a, s) for s in ("pelvis", "thorax", "leadArm")}
    have = {s: c for s, c in cr.items() if c and c.get("polarity")}
    flags = []
    if have:
        pols = [c["polarity"] for c in have.values()]
        major = 1 if pols.count(1) >= pols.count(-1) else -1
        for s, c in have.items():
            if c["polarity"] != major:
                flags.append(f"{s} polarity {'+' if c['polarity'] > 0 else '-'} against the others' "
                             f"{'+' if major > 0 else '-'} (upside down: reads as a sequence fault)")
            elif major < 0:
                flags.append(f"{s} reads negative into the downswing (towards the target is +)")
        seen = {s: c["crossMs"] for s, c in have.items() if c.get("crossMs") is not None}
        if len(seen) >= 2:
            med = statistics.median(seen.values())
            for s, t in seen.items():
                if abs(t - med) > KS_CROSS_TOL_MS:
                    flags.append(f"{s} crosses {t:+.0f} ms from the top, {t - med:+.0f} ms from the "
                                 f"median (> {KS_CROSS_TOL_MS:.0f})")
    return cr, flags


def boot_median_ci(xs, n=BOOT_N, seed=20261026):
    """Percentile bootstrap 95 % CI of the median (seeded: the same data gives the same CI)."""
    if len(xs) < 3:
        return None, None
    rng = random.Random(seed)
    meds = sorted(statistics.median(rng.choices(xs, k=len(xs))) for _ in range(n))
    return pct(meds, 2.5), pct(meds, 97.5)


def _plot_python():
    """A Python with matplotlib for plot_fullswing.py: this one, else Homebrew's, else PATH's."""
    import subprocess
    for py in (sys.executable, "/opt/homebrew/bin/python3", shutil.which("python3")):
        if py and os.path.exists(py):
            try:
                if subprocess.run([py, "-c", "import matplotlib"], capture_output=True,
                                  timeout=60).returncode == 0:
                    return py
            except (OSError, subprocess.SubprocessError):
                continue
    return None


def ks_sheets(keys, maps, panels, out):
    """One sheet per swing, a panel per pass (plot_fullswing.py draws one swing per panel and
    cannot overlay passes): plot_fullswing titles a panel by its directory's name and loads a
    directory's swing document, so each panel is a temp dir named '<swing> · <pass>' holding a
    link to that pass's result. Returns (written, problems)."""
    import subprocess
    tool = os.path.normpath(os.path.join(_HERE, "..", "kinematics", "plot_fullswing.py"))
    py = _plot_python()
    if py is None:
        return [], ["sheets skipped: no Python with matplotlib found (tried this one, "
                    "/opt/homebrew/bin/python3, python3 on PATH)"]
    sheets = os.path.join(out, "sheets")
    os.makedirs(sheets, exist_ok=True)
    tmp = tempfile.mkdtemp(prefix="trunk_hw_ks_sheets_")
    written, problems = [], []
    try:
        for k in keys:
            dirs = []
            for name, label in panels:
                f = maps[name][k][0]
                d = os.path.join(tmp, f"{os.path.basename(k)} · {label}")
                os.makedirs(d, exist_ok=True)
                link = os.path.join(d, "swing.ppsw" if f.endswith(".ppsw") else "swing.json")
                os.symlink(os.path.abspath(f), link)
                dirs.append(d)
            png = os.path.join(sheets, k.replace(os.sep, "__") + ".png")
            try:
                r = subprocess.run([py, tool, png, *dirs], capture_output=True, text=True,
                                   timeout=300)
            except subprocess.TimeoutExpired:
                problems.append(f"{k}: plot_fullswing.py timed out")
                continue
            if r.returncode == 0:
                written.append(png)
            else:
                last = (r.stderr.strip().splitlines() or ["?"])[-1]
                problems.append(f"{k}: plot_fullswing.py failed ({last})")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    return written, problems


def cmd_ks(bound, unbound, armunbound, out, fullswing=None, sheets=True, quiet=False):
    roots = {"bound": bound, "unbound": unbound, "armunbound": armunbound}
    warn = []
    if fullswing and os.path.isdir(fullswing):
        roots["fullswing"] = fullswing
    elif fullswing:
        warn.append(f"no fullswing pass at {fullswing}: the signed-chart check reads the bound "
                    "pass, whose series are cut at the top")
    keys, maps = match_results(roots)
    signed_from = "fullswing" if "fullswing" in roots else "bound"
    lines = ["trunk_hw ks — design §13 three passes, §14.4 peak times, §8.4 the signed chart", "",
             "Peak times in ms vs impact, + = after impact. Δ = camera - sensor (+ = the camera "
             "peak is later). Pelvis and chest: trunk unbound vs bound; arm: arm unbound vs bound "
             "(the wG3). An unplaced camera chest is read from its probable peak (bridged / "
             "indicated / rising / fell, with its ±).",
             f"Signed chart (§8.4) on the {signed_from} pass: each segment's polarity (the sign of "
             "its area between the top and impact; + = towards the target) and its reversal into "
             "the downswing (back from its largest rate of that sign to the last sign change), ms "
             "from the top; flagged when a "
             f"polarity is against the others' or a crossing is > {KS_CROSS_TOL_MS:.0f} ms from "
             "the median crossing.", ""]
    lines += [f"⚠ {w}" for w in warn]
    rows = []
    for k in keys:
        docs = {n: (load_swing(maps[n][k][0]).get("analysis") or {}) for n in roots}
        kss = {n: d.get("kinematicSequence") or {} for n, d in docs.items()}
        nodes = {n: {x.get("segment"): x for x in ks.get("nodes") or []} for n, ks in kss.items()}
        row = {"swing": k, "boundFile": maps["bound"][k][0], "unboundFile": maps["unbound"][k][0],
               "armunboundFile": maps["armunbound"][k][0]}
        notes = []
        lines.append(k)
        for seg in ("pelvis", "thorax", "leadArm"):
            sp = ks_peak(nodes["bound"].get(seg))
            cp = ks_peak(nodes[KS_CAMERA_PASS[seg]].get(seg))
            for tag, p in (("Sensor", sp), ("Cam", cp)):
                if p:
                    row[f"{seg}{tag}Ms"] = p["ms"]
                    row[f"{seg}{tag}SigmaMs"] = p["sigmaMs"]
                    row[f"{seg}{tag}Route"] = p["route"]
                    row[f"{seg}{tag}Kind"] = p["kind"]
            if sp and "Imu" not in sp["route"]:
                notes.append(f"bound {seg} route is {sp['route']!r}, not a sensor route")
            if cp and "Imu" in cp["route"]:
                notes.append(f"{KS_CAMERA_PASS[seg]} {seg} route is {cp['route']!r} (a sensor!)")
            if sp and cp and sp["ms"] is not None and cp["ms"] is not None:
                row[f"{seg}DiffMs"] = cp["ms"] - sp["ms"]
            lines.append(
                f"  {seg:<8} sensor {fmt(sp and sp['ms'], 1):>7} ± {fmt(sp and sp['sigmaMs'], 1):<5}"
                f" [{(sp or {}).get('route', '-')}]   camera {fmt(cp and cp['ms'], 1):>7} ± "
                f"{fmt(cp and cp['sigmaMs'], 1):<5} [{(cp or {}).get('route', '-')}"
                f"{'' if not cp or cp['kind'] == 'placed' else ', ' + cp['kind']}]   Δ "
                f"{sfmt(row.get(f'{seg}DiffMs')):>7}")
        club = ks_peak(nodes["bound"].get("club"))
        if club:
            row["clubBoundMs"], row["clubBoundRoute"] = club["ms"], club["route"]
            lines.append(f"  club     {fmt(club['ms'], 1):>7} ± {fmt(club['sigmaMs'], 1)} "
                         f"[{club['route']}] (bound pass; the camera's in every pass)")
        for n, col in (("bound", "orderBound"), ("unbound", "orderUnbound"),
                       ("armunbound", "orderArmUnbound")):
            row[col] = ks_order(kss[n])
        lines.append(f"  order    bound {row['orderBound']} | trunk unbound {row['orderUnbound']}"
                     f" | arm unbound {row['orderArmUnbound']}")
        cr, flags = signed_check(docs[signed_from])
        parts = []
        for seg, c in cr.items():
            if c is None:
                parts.append(f"{seg} -")
                continue
            row[f"{seg}CrossMs"] = c.get("crossMs")
            row[f"{seg}Polarity"] = c.get("polarity")
            p = "+" if c.get("polarity", 0) > 0 else ("-" if c.get("polarity", 0) < 0 else "?")
            parts.append(f"{seg} {sfmt(c.get('crossMs'), 0)} ms ({p})"
                         + (f" [{c['note']}]" if c.get("note") else ""))
        row["signedFlags"] = "; ".join(flags)
        lines.append(f"  signed   reversal vs top: {', '.join(parts)} -> "
                     + ("OK" if not flags else "⚠ " + "; ".join(flags)))
        for n in roots:
            if maps[n][k][1]:
                notes.append(f"{n}: older " + ",".join(os.path.basename(x) for x in maps[n][k][1])
                             + " ignored")
        row["note"] = "; ".join(notes)
        if notes:
            lines.append(f"  ⚠ {row['note']}")
        rows.append(row)
        lines.append("")
    cols = ["swing"]
    for seg in ("pelvis", "thorax", "leadArm"):
        cols += [f"{seg}SensorMs", f"{seg}SensorSigmaMs", f"{seg}SensorRoute", f"{seg}CamMs",
                 f"{seg}CamSigmaMs", f"{seg}CamRoute", f"{seg}CamKind", f"{seg}DiffMs"]
    cols += ["clubBoundMs", "clubBoundRoute", "orderBound", "orderUnbound", "orderArmUnbound"]
    for seg in ("pelvis", "thorax", "leadArm"):
        cols += [f"{seg}CrossMs", f"{seg}Polarity"]
    cols += ["signedFlags", "note", "boundFile", "unboundFile", "armunboundFile"]
    write_csv(os.path.join(out, "ks.csv"), cols, rows)
    summ = ks_summary(rows, len(keys), lines)
    if sheets and keys:
        panels = [("bound", "bound")]
        if "fullswing" in roots:
            panels.append(("fullswing", "bound, uncut"))
        panels += [("unbound", "trunk unbound"), ("armunbound", "arm unbound")]
        written, problems = ks_sheets(keys, maps, panels, out)
        lines.append(f"sheets: {len(written)} in {os.path.join(out, 'sheets')} (one per swing, "
                     f"panels {' | '.join(p[1] for p in panels)})")
        lines += [f"  ⚠ {p}" for p in problems]
        summ["sheets"] = written
    finish(out, "ks", lines, quiet)
    return rows, summ


def ks_summary(rows, matched, lines):
    """The §14.4 rows: per segment the median camera - sensor Δt with a bootstrap 95 % CI, n,
    median |Δt| and the camera σ_t's coverage (the KS §9 stage 2 rule); the order agreement;
    the §8.4 flags."""
    summ = {"matched": matched, "segments": {}}
    lines.append(f"OVER {matched} SWING(S) matched in all passes")
    what = {"pelvis": "hips, camera - IMU", "thorax": "chest, camera/bridged - IMU",
            "leadArm": "arm, camera - wG3"}
    for seg in ("pelvis", "thorax", "leadArm"):
        d = [r for r in rows if r.get(f"{seg}DiffMs") is not None]
        if not d:
            lines.append(f"  {seg} ({what[seg]}): n 0")
            summ["segments"][seg] = {"n": 0}
            continue
        xs = [r[f"{seg}DiffMs"] for r in d]
        lo, hi = boot_median_ci(xs)
        med = statistics.median(xs)
        mabs = statistics.median(abs(x) for x in xs)
        sig = [r for r in d if r.get(f"{seg}CamSigmaMs")]
        cov = (sum(1 for r in sig if abs(r[f"{seg}DiffMs"]) <= r[f"{seg}CamSigmaMs"]) / len(sig)
               if sig else None)
        sen = statistics.median(r[f"{seg}SensorMs"] for r in d)
        s = {"n": len(xs), "medianMs": med, "ciLoMs": lo, "ciHiMs": hi, "medianAbsMs": mabs,
             "coverage": cov, "medianSensorMs": sen}
        summ["segments"][seg] = s
        ci = f"[{lo:+.1f}, {hi:+.1f}]" if lo is not None else "- (n < 3)"
        kinds = {}
        for r in d:
            kinds[r.get(f"{seg}CamKind", "?")] = kinds.get(r.get(f"{seg}CamKind", "?"), 0) + 1
        rung = ("Estimated" if mabs <= ESTIMATED_MEDIAN_MS and cov is not None
                and cov >= ESTIMATED_COVERAGE else "PLANNED with the measured σ")
        lines.append(f"  {seg} ({what[seg]}): median Δt {med:+.1f} ms, 95 % CI {ci} (bootstrap of "
                     f"the median), n {len(xs)}; median |Δt| {mabs:.1f} ms; camera σ_t covers "
                     f"{fmt(cov and 100 * cov, 0)} %; sensor peak median {sen:+.1f} ms vs impact"
                     + (f"; camera chest {', '.join(f'{k} {v}' for k, v in sorted(kinds.items()))}"
                        if seg == "thorax" else ""))
        lines.append(f"    -> §14.4 / KS §9 stage 2 (median |Δt| <= {ESTIMATED_MEDIAN_MS:.0f} ms "
                     f"and coverage >= {100 * ESTIMATED_COVERAGE:.0f} %): {rung}")
    for col, name in (("orderUnbound", "trunk unbound"), ("orderArmUnbound", "arm unbound")):
        same = sum(1 for r in rows if r.get(col) == r.get("orderBound"))
        summ[f"sameOrder_{col}"] = same
        lines.append(f"  sequence order + verdict, {name} = bound on {same}/{len(rows)}")
    flagged = [r for r in rows if r.get("signedFlags")]
    summ["signedFlagged"] = [r["swing"] for r in flagged]
    lines.append(f"  VERDICT signed chart (§8.4): "
                 + ("pelvis, chest and arm reverse together with one polarity on every swing"
                    if not flagged else
                    f"⚠ {len(flagged)}/{len(rows)} swing(s) flagged: "
                    + "; ".join(f"{r['swing']}: {r['signedFlags']}" for r in flagged)))
    return summ


# ─────────────────────────────────────────────────────────────────────────────── output

def write_csv(path, cols, rows):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", newline="", encoding="utf-8") as fh:
        w = csv.DictWriter(fh, fieldnames=cols, extrasaction="ignore")
        w.writeheader()
        for r in rows:
            w.writerow({k: ("" if r.get(k) is None else
                            (f"{r[k]:.6g}" if isinstance(r[k], float) else r[k])) for k in cols})


def finish(out, name, lines, quiet):
    txt = "\n".join(lines) + "\n"
    with open(os.path.join(out, f"{name}_summary.txt"), "w", encoding="utf-8") as fh:
        fh.write(txt)
    if not quiet:
        print(txt, end="")
        print(f"[trunk_hw] wrote {os.path.join(out, name + '.csv')} and {name}_summary.txt")


def default_out(sub):
    stamp = _dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    base = "/mnt/swingdata/scratch/trunk-hw"
    if not os.path.isdir("/mnt/swingdata/scratch"):
        base = os.path.join(tempfile.gettempdir(), "trunk-hw")
    return os.path.join(base, f"{sub}-{stamp}")


# ─────────────────────────────────────────────────────────────────────────────── selftest

def _ble_arrivals(sample_us, rng, conn_ms=15.1, lead_us=0.0):
    """Arrival stamps for sample instants: each sample waits for the next BLE connection event,
    and every sample queued for that event arrives in one cluster ~0.1 ms apart."""
    ev0 = rng.uniform(0, conn_ms * 1000)
    out = []
    last_ev, k_in = None, 0
    for s in sample_us:
        ev = ev0 + math.ceil((s - ev0) / (conn_ms * 1000)) * conn_ms * 1000
        k_in = k_in + 1 if ev == last_ev else 0
        last_ev = ev
        out.append(int(round(ev + 100 * k_in - lead_us)))
    return out


def _write_record(path, serial, role, hz, t_us, rows):
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(RECORD_MAGIC + "\n")
        fh.write(f"# serial={serial} name=WT901 BLE {serial[-5:]} role={role} "
                 f"configuredRateHz={hz} startedUtc=2026-10-17T10:00:00Z\n")
        fh.write("t_us,ax,ay,az,gx,gy,gz,qw,qx,qy,qz\n")
        for t, r in zip(t_us, rows):
            fh.write(f"{t}," + ",".join(f"{x:.6f}" for x in r) + "\n")


def _bump(t, c, s):
    return math.exp(-0.5 * ((t - c) / s) ** 2)


def _smooth01(x):
    x = min(max(x, 0.0), 1.0)
    return x * x * (3 - 2 * x)


def _hop_signal(dt=0.0005, t_end=2.4, land_s=0.012):
    """Specific force (in g, along the body's vertical) for a small counter-movement hop, on a
    fine grid. The landing pulse's amplitude is solved so the body comes to rest. Returns
    (ts, f, h, t_land) with t_land the true landing (peak) instant."""
    t_to, t_td = 0.80, 1.00            # take-off, touch-down
    t_land = t_td + 0.030              # pulse peak 30 ms after touch-down
    edge = 0.008
    ts = [i * dt for i in range(int(t_end / dt) + 1)]

    def base(t):
        f = 1.0 - 0.35 * _bump(t, 0.55, 0.05) + 1.2 * _bump(t, 0.70, 0.05)
        w = _smooth01((t - (t_to - edge)) / edge) * (1 - _smooth01((t - t_td) / edge))
        return f * (1 - w)

    f0 = [base(t) for t in ts]
    pulse = [_bump(t, t_land, land_s) for t in ts]
    v_end = sum((f - 1.0) * G * dt for f in f0)
    a_pulse = -v_end / (G * sum(p * dt for p in pulse))
    f = [x + a_pulse * p for x, p in zip(f0, pulse)]
    v, h, hs = 0.0, 0.0, []
    for x in f:
        v += (x - 1.0) * G * dt
        h += v * dt
        hs.append(h)
    return ts, f, hs, t_land


def _interp(ts, ys, t):
    dt = ts[1] - ts[0]
    k = min(max(int(t / dt), 0), len(ts) - 2)
    u = (t - ts[k]) / dt
    return ys[k] * (1 - u) + ys[k + 1] * u


def _qmul(a, b):
    w1, x1, y1, z1 = a
    w2, x2, y2, z2 = b
    return (w1 * w2 - x1 * x2 - y1 * y2 - z1 * z2, w1 * x2 + x1 * w2 + y1 * z2 - z1 * y2,
            w1 * y2 - x1 * z2 + y1 * w2 + z1 * x2, w1 * z2 + x1 * y2 - y1 * x2 + z1 * w2)


def _wg3_streams(rng, ts, f, lag_ms, arm_delay_ms, period_ms, up):
    """The wG3's two units as the exporter writes them: instrument "hackmotion", '#unit' serials,
    role 0, gravity-removed accel a = (|f| - 1) up_body with up_body = q (0,0,up) q* (q world->
    body, a fixed tilt per unit), stamps on the 1.25 ms index grid every period_ms, fit-dated
    (no BLE bursts) lag_ms late. The wrist sees the hop arm_delay_ms after the sacrum."""
    sh = int(round(arm_delay_ms / 1000.0 / (ts[1] - ts[0])))
    fa = [f[0]] * sh + f[:len(f) - sh]
    streams = []
    for unit in ("lowerArm", "palm"):
        ax = [rng.gauss(0, 1) for _ in range(3)]
        n = norm3(*ax)
        ang = rng.uniform(0.3, 1.2)
        q = (math.cos(ang / 2),) + tuple(math.sin(ang / 2) * c / n for c in ax)
        ub = _qmul(_qmul(q, (0.0, 0.0, 0.0, up)), (q[0], -q[1], -q[2], -q[3]))[1:]
        phase = 1.25e-3 * rng.randrange(int(period_ms / 1.25))
        smp = [phase + k * period_ms / 1000 for k in range(int((ts[-1] - phase) / (period_ms / 1000)))]
        rows = []
        for t in smp:
            m = _interp(ts, fa, t) - 1.0
            rows.append([m * ub[0] + rng.gauss(0, 0.01), m * ub[1] + rng.gauss(0, 0.01),
                         m * ub[2] + rng.gauss(0, 0.01), rng.gauss(0, 0.5), rng.gauss(0, 0.5),
                         rng.gauss(0, 0.5), *q])
        t_us = [int(round(t * 1e6 + lag_ms * 1000)) for t in smp]
        streams.append({"kind": "imu", "alias": f"HackMotion wG3 04:87:27:00:00:01 · {unit}",
                        "schema": "imu_sample_v2", "instrument": "hackmotion",
                        "source": {"serial": f"04:87:27:00:00:01#{unit}"},
                        "device": {"outputRateHz": 26, "role": 0, "roleName": ""},
                        "samples": {"count": len(t_us), "t_us": t_us, "data": rows}})
    return streams


def _synth_hop_doc(rng, lead_ms=37.0, thorax_delay_ms=3.0, fps=150.0, wg3_lag_ms=None,
                   arm_delay_ms=4.0, wg3_period_ms=10.0, wg3_up=1.0):
    ts, f, h, t_land = _hop_signal()
    # The chest sensor sees the same hop thorax_delay_ms later (the spine's lag), whole signal.
    sh = int(round(thorax_delay_ms / 1000.0 / (ts[1] - ts[0])))
    f_th = [f[0]] * sh + f[:len(f) - sh]
    streams = []
    floors = {}   # the BLE latency floor per sensor: what an ideal re-timing still carries
    for role_int, serial, ff in ((1, "AA:00:00:00:00:01", f), (2, "AA:00:00:00:00:02", f_th)):
        phase = rng.uniform(0, 0.005)
        smp = [phase + k * 0.005 for k in range(int((ts[-1] - phase) / 0.005))]
        ax = (0.2, 0.3, 0.93)
        nrm = norm3(*ax)
        ax = [x / nrm for x in ax]
        rows = []
        for s in smp:
            m = _interp(ts, ff, s)
            rows.append([m * ax[0] + rng.gauss(0, 0.01), m * ax[1] + rng.gauss(0, 0.01),
                         m * ax[2] + rng.gauss(0, 0.01), rng.gauss(0, 0.5), rng.gauss(0, 0.5),
                         rng.gauss(0, 0.5), 1.0, 0.0, 0.0, 0.0])
        # 11.25 ms connection interval: the sample/event phase sweeps every few events, so the
        # re-timing's lower envelope reaches the true sample instants (see README: with an
        # interval that is a multiple of the period the floor sits anywhere in [0, T)).
        arr = _ble_arrivals([s * 1e6 for s in smp], rng, conn_ms=11.25, lead_us=lead_ms * 1000)
        floors[role_int] = pct([a - (x * 1e6 - lead_ms * 1000) for a, x in zip(arr, smp)], 1) / 1000
        streams.append({"kind": "imu", "alias": f"Sensor {role_int}", "schema": "imu_sample_v2",
                        "source": {"serial": serial},
                        "device": {"outputRateHz": 200, "role": role_int,
                                   "roleName": "Pelvis" if role_int == 1 else "Thorax"},
                        "samples": {"count": len(arr), "t_us": arr, "data": rows}})
    frames = []
    t = rng.uniform(0, 1 / fps)
    while t < ts[-1] - 0.01:
        y = 0.55 - 0.35 * _interp(ts, h, t) + rng.gauss(0, 0.0003)
        kp = [0.0] * (133 * 3)
        kp[33], kp[34], kp[35] = 0.48, y + 0.002, 0.9
        kp[36], kp[37], kp[38] = 0.52, y - 0.002, 0.9
        frames.append({"t_us": int(round(t * 1e6)), "kp": kp})
        t += 1 / fps
    doc = {"schema": "pinpoint.swing/synthetic", "streams": streams,
           "analysis": {"pose2d": {"camera": 0, "keypointCount": 133, "frames": frames}}}
    if wg3_lag_ms is not None:
        streams += _wg3_streams(rng, ts, f, wg3_lag_ms, arm_delay_ms, wg3_period_ms, wg3_up)
        doc["analysis"]["bindings"] = [
            {"serial": "04:87:27:00:00:01#lowerArm", "role": 5, "roleName": "LeadForearm",
             "hackMotion": True},
            {"serial": "04:87:27:00:00:01#palm", "role": 6, "roleName": "LeadHand",
             "hackMotion": True}]
    return doc, t_land, floors


def _ks_rate(t, tc, tp, peak, back=300.0, span=600.0, decay=80.0):
    """A signed segment rate (deg/s) at t (ms): a negative backswing hump ending at the reversal
    tc, a rise to `peak` at tp, an exponential fall after it."""
    if tc - span <= t < tc:
        return -back * math.sin(math.pi * (tc - t) / span)
    if tc <= t <= tp:
        return peak * math.sin(0.5 * math.pi * (t - tc) / (tp - tc))
    if t > tp:
        return peak * math.exp(-(t - tp) / decay)
    return 0.0


def _synth_ks_doc(rng, side, lag, cross, flip, impact_us=1_500_000):
    """One pass's result document: KS nodes (ms vs impact) and the uncut signed series. Sensor
    peaks: pelvis -30, chest -15, arm -100, club -20 ms vs impact; the top 280 ms before impact.
    'unbound' puts the camera on pelvis (placed) and chest (unplaced, bridged); 'armunbound' on the
    arm."""
    top = impact_us - 280000
    sensor = {"pelvis": -30.0, "thorax": -15.0, "leadArm": -100.0}
    route = {"pelvis": "pelvisImu", "thorax": "thoraxImu", "leadArm": "leadArmImus"}
    nodes = []
    for seg, ms in sensor.items():
        cam = side == "armunbound" if seg == "leadArm" else side == "unbound"
        n = {"segment": seg, "placed": True, "routeId": route[seg], "quality": "direct",
             "tSigmaMs": 2.5, "peakDps": 600.0}
        if cam:
            ms = ms + lag[seg] + rng.gauss(0, 2.0)
            n.update(routeId="faceOn", quality="estimated", tSigmaMs=rng.uniform(8, 25))
            if seg == "thorax":
                n["placed"] = False
                n["probable"] = {"peakMs": ms, "tSigmaMs": 15.0, "peakDps": 550.0,
                                 "peakSigmaDps": 60.0, "how": "bridge"}
                ms = ms - 60.0   # the unplaced attempt, which must not be read
        n["beforeImpactMs"] = -ms
        n["tPeakUs"] = int(impact_us + ms * 1000)
        nodes.append(n)
    nodes.append({"segment": "club", "placed": True, "routeId": "faceOnClub", "quality": "estimated",
                  "beforeImpactMs": 20.0, "tSigmaMs": 9.0, "tPeakUs": impact_us - 20000,
                  "peakDps": 2000.0})
    placed = sorted((n for n in nodes if n["placed"]), key=lambda n: n["tPeakUs"])
    metrics = []
    for seg, key in KS_SERIES.items():
        if seg == "club":
            continue
        tc = (top - impact_us) / 1000.0 + cross[seg]
        tp = sensor[seg]
        ph = rng.uniform(0, 5.0)
        ts = [-1200.0 + ph + 5.0 * k for k in range(300)]
        sgn = -1.0 if seg in flip else 1.0
        metrics.append({"key": key, "unit": "°/s", "routeId": route[seg],
                        "t_us": [int(impact_us + t * 1000) for t in ts],
                        "value": [sgn * _ks_rate(t, tc, tp, 800.0) for t in ts],
                        "valid": [1] * len(ts)})
    return {"schema": "pinpoint.swing/synthetic", "analysis": {
        "phases": [{"phase": 0, "t_us": impact_us - 1100000}, {"phase": PHASE_TOP, "t_us": top},
                   {"phase": PHASE_TRANSITION, "t_us": top + 20000}, {"phase": 5, "t_us": impact_us}],
        "metrics": metrics,
        "kinematicSequence": {"impactUs": impact_us, "nodes": nodes,
                              "order": [n["segment"] for n in placed], "verdict": "other"}}}


def selftest():
    rng = random.Random(20261017)
    tmp = tempfile.mkdtemp(prefix="trunk_hw_selftest_")
    fails = []

    def check(name, cond, detail):
        print(f"  {'PASS' if cond else 'FAIL'}  {name}: {detail}")
        if not cond:
            fails.append(name)

    try:
        # ── rate: 60 s at 200 Hz, BLE bursts every 15.1 ms, 10 gaps of 5 lost samples
        print("rate")
        drop = set()
        for g in range(10):
            k0 = 1000 + g * 1100
            drop.update(range(k0, k0 + 5))
        smp = [k * 5000.0 for k in range(12000) if k not in drop]
        arr = _ble_arrivals(smp, rng)
        rows = [[0, 0, 1, 0, 0, 0, 1, 0, 0, 0]] * len(arr)
        os.makedirs(os.path.join(tmp, "rec"))
        p1 = os.path.join(tmp, "rec", "good.csv")
        _write_record(p1, "E1:00:00:00:00:01", "pelvis", 200, arr, rows)
        smp2 = [k * 5000.0 for k in range(12000) if rng.random() > 0.25]
        arr2 = _ble_arrivals(smp2, rng)
        p2 = os.path.join(tmp, "rec", "bad.csv")
        _write_record(p2, "E1:00:00:00:00:02", "", 200, arr2, [[0, 0, 1, 0, 0, 0, 1, 0, 0, 0]] * len(arr2))
        out = os.path.join(tmp, "rate")
        res = cmd_rate([os.path.join(tmp, "rec")], out, quiet=True)   # a PINPOINT_IMU_RECORD_DIR
        g = next(r for r in res if r["serial"].endswith("01"))
        b = next(r for r in res if r["serial"].endswith("02"))
        check("rate.mean", abs(g["meanHz"] - 199.17) < 0.3, f"{g['meanHz']:.2f} Hz (expect 199.17)")
        check("rate.short", abs(g["shortSamples"] - 50) <= 3, f"{g['shortSamples']} (expect 50)")
        check("rate.lossyGaps", abs(g["lossyGaps"] - 10) <= 2,
              f"{g['lossyGaps']} loss events (expect 10 ± 2: the latency floor can split one)")
        check("rate.gapLost", abs(g["gapLostEst"] - 50) <= 3, f"{g['gapLostEst']} (expect 50)")
        check("rate.burst", abs(g["meanCluster"] - 3.02) < 0.15, f"mean cluster {g['meanCluster']:.2f} (expect ~3.0)")
        check("rate.clusterIv", abs(g["p50ClusterIntervalMs"] - 15.1) < 0.3, f"{g['p50ClusterIntervalMs']:.2f} ms (expect 15.1)")
        check("rate.verdictGood", "200 Hz sustained" in rate_verdict(g), rate_verdict(g))
        check("rate.verdictBad", "NOT sustained" in rate_verdict(b) and 145 < b["meanHz"] < 155,
              rate_verdict(b))
        check("rate.role", g["role"] == "pelvis" and g["device"] == "pelvis", f"role {g['role']!r}")
        check("rate.retime", g["retimeApplied"] and g["retimeLostSamples"] == 50
              and abs(g["retimePeriodUs"] - 5000.0) < 1.0 and g["retimeSegments"] == 1,
              f"re-timed: {g['retimeLostSamples']} lost in {g['retimeLossEvents']} events, period "
              f"{g['retimePeriodUs']:.2f} us, {g['retimeSegments']} segment (expect 50, 5000, 1)")
        rf = retime_fit(arr, 200.0)
        err = [r - t for r, t in zip(rf["retimedUs"], smp)]
        e0 = statistics.median(err)
        check("rate.retimeTruth", max(abs(e - e0) for e in err) < 1000.0 and 0 <= e0 < 5000,
              f"retimed - truth: offset {e0 / 1000:.2f} ms, max |e| "
              f"{max(abs(e - e0) for e in err) / 1000:.2f} ms about it (expect < 1)")
        # The W0 geometry: 8-sample batches every ~40 ms on a 30 ms connection grid, the device
        # 0.7 % slow, nothing lost. The delivery floor is a sawtooth; it is not loss.
        T8 = 5000.0 / 0.993
        smp8, arr8 = [], []
        for bb in range(700):
            ready = T8 * (bb * 8 + 7) + 1500.0
            ev = 7300.0 + math.ceil((ready - 7300.0) / 30000.0) * 30000.0
            if rng.random() < 0.03:
                ev += 30000.0
            host = 3000.0 * rng.random() * rng.random()
            for i in range(8):
                smp8.append(T8 * (bb * 8 + i))
                arr8.append(max(ev + host + 180.0 * i, (arr8[-1] + 1) if arr8 else 0))
        rf8 = retime_fit(arr8, 200.0)
        err8 = [r - t for r, t in zip(rf8["retimedUs"], smp8)]
        e8 = statistics.median(err8)
        check("rate.sawtooth", rf8["lostSamples"] == 0 and abs(rf8["periodUs"] / T8 - 1) < 2e-4
              and max(abs(e - e8) for e in err8) < 1000.0,
              f"lossless slow clock: {rf8['lostSamples']} lost, period {rf8['periodUs']:.2f} us "
              f"(truth {T8:.2f}), max |e| {max(abs(e - e8) for e in err8) / 1000:.2f} ms (expect 0, <1)")

        # ── still: 300 s hold between two 5 s turns; bias (0.3, -0.2, 0.1) deg/s, x drifts
        print("still")
        hz, pre, hold = 200, 5.0, 300.0
        n = int((2 * pre + hold) * hz)
        bias = (0.3, -0.2, 0.1)
        slope = 0.02  # deg/s per minute on x, centred on the hold so the mean stays 0.3
        mid = pre + hold / 2
        tt, rr = [], []
        gx_dir = (0.1, 0.2, 0.97)
        gn = norm3(*gx_dir)
        for k in range(n):
            t = k / hz
            moving = t < pre or t > pre + hold
            turn = 40.0 * math.sin(2 * math.pi * t / 2.5) if moving else 0.0
            gx = bias[0] + slope * (t - mid) / 60.0 + rng.gauss(0, 0.05)
            gy = bias[1] + rng.gauss(0, 0.05)
            gz = bias[2] + turn + rng.gauss(0, 0.05)
            fm = 1.003
            rr.append([fm * gx_dir[0] / gn + rng.gauss(0, 0.004), fm * gx_dir[1] / gn + rng.gauss(0, 0.004),
                       fm * gx_dir[2] / gn + rng.gauss(0, 0.004), gx, gy, gz, 1, 0, 0, 0])
            tt.append(int(t * 1e6))
        p3 = os.path.join(tmp, "still.csv")
        _write_record(p3, "E1:00:00:00:00:03", "thorax", 200, tt, rr)
        s = cmd_still([p3], os.path.join(tmp, "still"), quiet=True)[0]
        check("still.span", abs(s["spanS"] - hold) < 1.0, f"{s['spanS']:.2f} s (expect {hold})")
        for j, ax in enumerate("xyz"):
            check(f"still.bias.{ax}", abs(s[f"g{ax}Bias"] - bias[j]) < 0.01,
                  f"{s[f'g{ax}Bias']:+.4f} (expect {bias[j]:+.2f})")
        check("still.slope", abs(s["gxSlopePerMin"] - slope) < 0.003,
              f"{s['gxSlopePerMin']:+.4f} deg/s/min (expect {slope})")
        check("still.noise", abs(s["gxSdDetrended"] - 0.05) < 0.005 and abs(s["gySd"] - 0.05) < 0.005,
              f"x detrended {s['gxSdDetrended']:.4f}, y {s['gySd']:.4f} (expect 0.05)")
        check("still.f", abs(s["fDevPct"] - 0.3) < 0.05, f"{s['fDevPct']:+.3f} % (expect +0.30)")
        check("still.drift", abs(s["drift15sWorstDeg"] - 0.45) < 0.02,
              f"{s['drift15sWorstDeg']:.3f} deg over 1.5 s (expect 0.45)")

        # ── hop: four hops, IMU stamps lead the video by 37 ms, thorax lands 3 ms after pelvis
        print("hop")
        hop_dirs, expect = [], []
        for i in range(4):
            doc, _, floors = _synth_hop_doc(rng)
            expect.append(-37.0 + floors[1])
            d = os.path.join(tmp, "hops", f"swing_{i + 1:04d}")
            os.makedirs(d)
            with open(os.path.join(d, "swing.json"), "w", encoding="utf-8") as fh:
                json.dump(doc, fh)
            hop_dirs.append(d)
        results, summ = cmd_hop([os.path.join(tmp, "hops")], os.path.join(tmp, "hop"), quiet=True)
        check("hop.n", summ["n"] == 4, f"{summ['n']} hops (expect 4)")
        pel = summ["perSensor"].get("pelvis", {}).get("meanMs")
        exp = statistics.mean(expect)
        check("hop.offset", pel is not None and abs(pel - exp) < 2.5,
              f"pelvis mean offset {fmt(pel)} ms (expect {exp:+.2f} ± 2.5 = -37 lead + "
              f"{exp + 37:.2f} BLE latency floor)")
        check("hop.repeat", summ["sdMs"] is not None and summ["sdMs"] < 3.0,
              f"SD {fmt(summ['sdMs'])} ms (expect < 3: ~1.2 ms per hop, tails to 3, at "
              f"0.3 px-equivalent pose noise)")
        agr = [r["agreementMs"] for r in results if "agreementMs" in r]
        check("hop.agree", agr and all(abs(a - 3.0) < 1.5 for a in agr),
              f"agreement {', '.join(f'{a:.2f}' for a in agr)} ms (expect 3 ± 1.5)")
        check("hop.residual", summ["residualMaxMs"] < 5.0, f"worst residual {summ['residualMaxMs']:.2f} ms (< 5)")
        check("hop.pass", summ.get("pass") is True, "§6.3 verdict PASS")
        check("hop.noWg3", "wg3N" not in summ and all(not r.get("wg3") for r in results),
              "no wG3 lanes -> no wG3 figures, trunk output unchanged")

        # ── hop with the wG3 worn: its stamps 6 ms late (fit-dated, no bursts), the wrist lands
        # 4 ms after the sacrum, ~100 Hz at the landing (outside the deferred pull). Hop 3's wG3
        # world is -Z up; hop 4's wG3 is live-only (40 ms) and must be shown but left out.
        print("hop + wG3")
        wg_dirs, wexp = [], []
        for i in range(4):
            doc, _, floors = _synth_hop_doc(rng, wg3_lag_ms=6.0, arm_delay_ms=4.0,
                                            wg3_period_ms=40.0 if i == 3 else 10.0,
                                            wg3_up=-1.0 if i == 2 else 1.0)
            trunk = statistics.mean([-37.0 + floors[1], -37.0 + floors[2] + 3.0])
            wexp.append(6.0 + 4.0 - trunk)
            d = os.path.join(tmp, "wg3hops", f"swing_{i + 1:04d}")
            os.makedirs(d)
            with open(os.path.join(d, "swing.json"), "w", encoding="utf-8") as fh:
                json.dump(doc, fh)
        results, summ = cmd_hop([os.path.join(tmp, "wg3hops")], os.path.join(tmp, "wg3hop"),
                                quiet=True)
        units = [u for r in results for u in r.get("wg3", [])]
        check("wg3.lanes", len(units) == 8 and all(u["role"] in ("leadForearm", "leadHand")
                                                    for u in units),
              f"{len(units)} wG3 units read, roles from bindings (expect 8)")
        ups = [u.get("upSign") for u in units]
        check("wg3.upSign", ups == [1, 1, 1, 1, -1, -1, 1, 1],
              f"up signs {ups} (hop 3 is -Z)")
        offs = [u["offsetMs"] for u in units[:6] if "offsetMs" in u]
        check("wg3.offset", len(offs) == 6 and all(abs(o - 10.0) < 2.5 for o in offs),
              f"wG3 offsets to video {', '.join(f'{o:+.2f}' for o in offs)} ms (expect +10 ± 2.5 "
              "= 6 lag + 4 arm)")
        got = [r.get("witWg3Ms") for r in results]
        check("wg3.witWg3", all(g is not None and abs(g - e) < 2.5
                                for g, e in zip(got[:3], wexp[:3])),
              f"t_wG3 - t_Witmotion {', '.join(fmt(g) for g in got)} ms (expect "
              f"{', '.join(f'{e:+.2f}' for e in wexp)} ± 2.5)")
        check("wg3.coarse", results[3].get("witWg3Coarse") is True and summ.get("wg3N") == 3,
              f"40 ms hop flagged coarse and left out: verdict over {summ.get('wg3N')} hops (expect 3)")
        check("wg3.spread", summ.get("witWg3SdMs") is not None and summ["witWg3SdMs"] < 3.0,
              f"SD {fmt(summ.get('witWg3SdMs'))} ms, median {fmt(summ.get('witWg3MedianMs'))} ms")
        check("wg3.pass", summ.get("wg3Pass") is True and summ.get("pass") is True,
              "Witmotion <-> wG3 verdict PASS, trunk verdict unchanged")
        with open(os.path.join(tmp, "wg3hop", "hop.csv"), encoding="utf-8") as fh:
            hdr = fh.readline().strip().split(",")
        check("wg3.csv", hdr[:19] == ["swing", "sensor", "serial", "role", "offsetMs", "xcorrR",
                                      "offsetPeakMs", "offsetRawMs", "imuLandMs", "videoLandMs",
                                      "peakG", "flightMs", "frameMs", "fitMs", "residualMs",
                                      "agreementMs", "retimeP50Ms", "retimeP95Ms", "error"]
              and "witWg3Ms" in hdr, "existing hop.csv columns unchanged, wG3 columns appended")

        # ── tilt
        print("tilt")
        rf1 = os.path.join(tmp, "refusion_a.json")
        with open(rf1, "w", encoding="utf-8") as fh:
            json.dump({"schema": "pinpoint.refusion/1", "sources": [
                {"serial": "P", "role": 1, "samples": 500, "maxDeg": 0.0,
                 "tilt": {"addressDeg": 0.1, "topDeg": 0.6, "impactDeg": 0.4, "finishDeg": 0.3,
                          "finishVsAddressGravityDeg": 0.5}},
                {"serial": "T", "role": 2, "samples": 500, "maxDeg": 0.0,
                 "tilt": {"addressDeg": 0.2, "topDeg": 2.1, "impactDeg": -1.6, "finishDeg": 0.9,
                          "finishVsAddressGravityDeg": 1.2}},
                {"serial": "A", "role": 4, "samples": 500, "maxDeg": 0.0}]}, fh)
        rows, v = cmd_tilt([rf1], os.path.join(tmp, "tilt_a"), quiet=True)
        check("tilt.absent", rows[2]["tilt"] == "absent", f"arm lane tilt {rows[2]['tilt']}")
        check("tilt.needed", "re-fusion needed" in v and "1.60" in v, v)
        rf2 = os.path.join(tmp, "refusion_b.json")
        with open(rf2, "w", encoding="utf-8") as fh:
            json.dump({"schema": "pinpoint.refusion/1", "sources": [
                {"serial": "P", "role": 1, "tilt": {"impactDeg": 0.4}},
                {"serial": "T", "roleName": "Thorax", "tilt": {"impactDeg": -0.7}}]}, fh)
        _, v2 = cmd_tilt([rf2], os.path.join(tmp, "tilt_b"), quiet=True)
        check("tilt.optional", "re-fusion optional" in v2, v2)

        # ── hips: five swings; bound (IMU) peaks before impact, camera after; one stale file
        print("hips")
        imu_bi = [30.0, 25.0, 40.0, 10.0, 35.0]   # beforeImpactMs: + = before impact
        cam_bi = [-20.0, -15.0, -30.0, -18.0, -22.0]
        for side, vals, route in (("bound", imu_bi, "pelvisImu"), ("unbound", cam_bi, "faceOn")):
            for i, bi in enumerate(vals):
                d = os.path.join(tmp, side, "2026-10-20_Mark_Wrist_01", f"swing_{i + 1:04d}")
                os.makedirs(d)
                node = {"segment": "pelvis", "placed": True, "beforeImpactMs": bi,
                        "tSigmaMs": 4.0 if side == "bound" else 12.0, "routeId": route}
                doc = {"analysis": {"kinematicSequence": {"nodes": [node]}}}
                with open(os.path.join(d, "result.json"), "w", encoding="utf-8") as fh:
                    json.dump(doc, fh)
        # the trap: a stale result.json beside a NEWER swing.json — the newer must win
        d = os.path.join(tmp, "bound", "2026-10-20_Mark_Wrist_01", "swing_0001")
        stale = os.path.join(d, "result.json")
        with open(stale, "w", encoding="utf-8") as fh:
            json.dump({"analysis": {"kinematicSequence": {"nodes": [
                {"segment": "pelvis", "placed": True, "beforeImpactMs": -999.0, "tSigmaMs": 1.0,
                 "routeId": "pelvisImu"}]}}}, fh)
        os.utime(stale, (1.7e9, 1.7e9))
        with open(os.path.join(d, "swing.json"), "w", encoding="utf-8") as fh:
            json.dump({"analysis": {"kinematicSequence": {"nodes": [
                {"segment": "pelvis", "placed": True, "beforeImpactMs": 30.0, "tSigmaMs": 4.0,
                 "routeId": "pelvisImu"}]}}}, fh)
        rows, summ = cmd_hips(os.path.join(tmp, "bound"), os.path.join(tmp, "unbound"),
                              os.path.join(tmp, "hips"), quiet=True)
        diffs = sorted(-a + c for a, c in zip(imu_bi, cam_bi))
        check("hips.n", summ["n"] == 5, f"{summ['n']} swings (expect 5)")
        check("hips.median", abs(summ["medianDiffMs"] - statistics.median(diffs)) < 1e-6,
              f"median diff {summ['medianDiffMs']:+.1f} ms (expect {statistics.median(diffs):+.1f})")
        check("hips.stale", all(r.get("imuVsImpactMs") != 999.0 for r in rows),
              "the newer swing.json beat the stale result.json")
        check("hips.verdict", summ["survives"] is False and summ["medianCamMs"] == 20.0,
              f"camera median {summ['medianCamMs']:+.1f} ms, IMU {summ['medianImuMs']:+.1f} ms, survives={summ['survives']}")

        # ── ks: six swings, three passes. The camera is late by +20 (hips), +10 (chest, bridged)
        # and +43 ms (arm); the sensors reverse 40 / 15 ms before and 5 ms after the top. Swing 3's
        # chest curve is upside down (polarity flag); swing 4's arm reverses 150 ms after the top
        # (timing flag).
        print("ks")
        lag = {"pelvis": 20.0, "thorax": 10.0, "leadArm": 43.0}
        for i in range(6):
            flip = {"thorax"} if i == 2 else set()
            cross = {"pelvis": -40.0, "thorax": -15.0, "leadArm": 150.0 if i == 3 else 5.0}
            for side in ("bound", "unbound", "armunbound"):
                doc = _synth_ks_doc(rng, side, lag, cross, flip)
                d = os.path.join(tmp, "ks", side, "2026-10-27_Mark_Wrist_01", f"swing_{i + 1:04d}")
                os.makedirs(d)
                with open(os.path.join(d, "result.json"), "w", encoding="utf-8") as fh:
                    json.dump(doc, fh)
        root = os.path.join(tmp, "ks")
        rows, summ = cmd_ks(os.path.join(root, "bound"), os.path.join(root, "unbound"),
                            os.path.join(root, "armunbound"), os.path.join(tmp, "ksout"),
                            sheets=False, quiet=True)
        check("ks.n", summ["matched"] == 6, f"{summ['matched']} swings matched (expect 6)")
        for seg, want in lag.items():
            sg = summ["segments"].get(seg, {})
            check(f"ks.{seg}", sg.get("n") == 6 and abs(sg.get("medianMs", 1e9) - want) < 3.0
                  and sg["ciLoMs"] <= sg["medianMs"] <= sg["ciHiMs"],
                  f"median Δt {fmt(sg.get('medianMs'))} ms, CI [{fmt(sg.get('ciLoMs'))}, "
                  f"{fmt(sg.get('ciHiMs'))}] (expect {want:+.0f} ± 3)")
        check("ks.bridged", all(r.get("thoraxCamKind") == "bridged" for r in rows),
              "the unplaced camera chest read from its bridged probable peak")
        fl = {r["swing"].split(os.sep)[-1]: r.get("signedFlags", "") for r in rows}
        check("ks.flip", "thorax polarity -" in fl["swing_0003"] and "leadArm" not in fl["swing_0003"]
              and "pelvis" not in fl["swing_0003"], f"swing_0003: {fl['swing_0003']!r}")
        check("ks.late", "leadArm crosses" in fl["swing_0004"] and "polarity" not in fl["swing_0004"],
              f"swing_0004: {fl['swing_0004']!r}")
        clean = [k for k in fl if k not in ("swing_0003", "swing_0004") and fl[k]]
        check("ks.clean", not clean, f"no flag on the other four (flagged: {clean or 'none'})")
        cr = [r.get("pelvisCrossMs") for r in rows]
        check("ks.cross", all(c is not None and abs(c + 40.0) < 3.0 for c in cr),
              f"pelvis reversal {', '.join(fmt(c, 1) for c in cr)} ms from the top (expect -40)")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print(f"selftest: {'PASS' if not fails else 'FAIL (' + ', '.join(fails) + ')'}")
    return 0 if not fails else 1


# ─────────────────────────────────────────────────────────────────────────────── CLI

def main(argv=None):
    ap = argparse.ArgumentParser(prog="trunk_hw", description=__doc__.split("\n\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    for name, hlp in (("rate", "BLE rate per device"), ("still", "gyro bias/noise/drift"),
                      ("hop", "IMU <-> camera clock offset from hops"),
                      ("tilt", "live vs re-fused tilt from refusion.json")):
        p = sub.add_parser(name, help=hlp)
        p.add_argument("inputs", nargs="+")
        p.add_argument("--out", help="output dir (default /mnt/swingdata/scratch/trunk-hw/<cmd>-<time>)")
        if name == "rate":
            p.add_argument("--burst-ms", type=float, default=1.0,
                           help="arrivals closer than this are one burst (default 1 ms)")
            p.add_argument("--assume-hz", type=float,
                           help="nominal rate for lanes that record none (default 200)")
    p = sub.add_parser("hips", help="hip KS node: bound vs unbound analysis")
    p.add_argument("bound")
    p.add_argument("unbound")
    p.add_argument("--out")
    p = sub.add_parser("ks", help="KS peaks: bound vs trunk-unbound vs arm-unbound, signed chart")
    p.add_argument("bound")
    p.add_argument("unbound", help="the trunk-unbound pass (--ignore-role pelvis,thorax)")
    p.add_argument("armunbound", help="the arm-unbound pass (--ignore-role leadForearm,leadHand)")
    p.add_argument("--fullswing", help="the bound pass with the curve uncut (throughP8 false): "
                   "the signed-chart check and an extra sheet panel read it")
    p.add_argument("--no-sheets", action="store_true", help="skip the plot_fullswing.py sheets")
    p.add_argument("--out")
    sub.add_parser("selftest", help="synthetic data with known answers; non-zero exit on failure")
    a = ap.parse_args(argv)
    if a.cmd == "selftest":
        return selftest()
    out = a.out or default_out(a.cmd)
    os.makedirs(out, exist_ok=True)
    if a.cmd == "rate":
        cmd_rate(a.inputs, out, a.burst_ms, assume_hz=a.assume_hz)
    elif a.cmd == "still":
        cmd_still(a.inputs, out)
    elif a.cmd == "hop":
        cmd_hop(a.inputs, out)
    elif a.cmd == "tilt":
        cmd_tilt(a.inputs, out)
    elif a.cmd == "hips":
        cmd_hips(a.bound, a.unbound, out)
    elif a.cmd == "ks":
        cmd_ks(a.bound, a.unbound, a.armunbound, out, fullswing=a.fullswing,
               sheets=not a.no_sheets)
    return 0


if __name__ == "__main__":
    sys.exit(main())
