#!/usr/bin/env python3
"""trunk_hw — the trunk-IMU hardware measurement session, analysed.

docs/design/trunk_imu_sequence_grf_design.md §16 step 2: "Hardware measurements before the
estimator". Each subcommand answers one question that decides whether a planned piece of the
pipeline is needed at all, and writes a CSV plus a short human summary (also printed):

    rate  <recorder files | swing dirs ...>   §4.2  does BLE sustain 200 Hz with 3 Witmotions?
    still <recorder files | swing dirs ...>   §4.2  gyro bias / noise / drift over a still hold
    hop   <swing dirs ...>                    §6.3 step 4, §7.1  IMU <-> camera clock offset
    tilt  <refusion.json ...>                 §7.2  does the live filter keep trunk tilt?
    hips  <bound result dir> <unbound dir>    Part K  IMU hip peak vs the camera's
    selftest                                  synthetic data with known answers for all five

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

Stdlib only: the HackMotion venv this runs under has no numpy/scipy, and nothing here needs them.

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

def retime(t_us, nominal_hz, loss_gap_ms=None):
    """§7.1 step 1: sample index x period, anchored by a robust fit to the arrival stamps.

    One index per arrived sample; a new segment starts only at an UNMISTAKABLE loss gap (an arrival
    gap longer than `loss_gap_ms`, default max(25 ms, 5 periods)) — bursty BLE delivers backlog in
    clusters, so a 15–40 ms inter-burst gap is not loss and must not advance the index. Per segment:
    least squares, two passes trimming residuals beyond 3 MAD, slope kept only on >= 50 samples
    (else the nominal period), then the line is moved down to the 1st-percentile residual — the
    LOWER envelope, because an arrival can only ever be later than its sample. Returns
    (retimed_us list, arrival-minus-retimed residuals us)."""
    n = len(t_us)
    T = 1e6 / nominal_hz
    gap = (loss_gap_ms * 1000.0) if loss_gap_ms else max(25000.0, 5 * T)
    cuts = [0] + [i for i in range(1, n) if t_us[i] - t_us[i - 1] > gap] + [n]
    out = [0.0] * n
    for a, b in zip(cuts[:-1], cuts[1:]):
        idx = list(range(b - a))
        ts = [float(x) for x in t_us[a:b]]
        if len(ts) >= 50:
            keep = idx
            for _ in range(2):
                c0, slope, res = linfit([idx[k] for k in keep], [ts[k] for k in keep])
                mad = statistics.median([abs(r) for r in res]) or 1.0
                keep2 = [k for k, r in zip(keep, res) if abs(r) <= 3 * 1.4826 * mad]
                if len(keep2) < 20:
                    break
                keep = keep2
            c0, slope, _ = linfit([idx[k] for k in keep], [ts[k] for k in keep])
        else:
            slope = T
            c0 = statistics.median([t - k * T for k, t in zip(idx, ts)])
        res = [t - (c0 + slope * k) for k, t in zip(idx, ts)]
        c0 += pct(res, 1)
        for k in idx:
            out[a + k] = c0 + slope * k
    return out, [t - r for t, r in zip(t_us, out)]


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
            "p50ClusterIntervalMs", "clustersHalfT", "meanClusterHalfT", "maxClusterHalfT"]
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
        lines.append(f"  gaps > 2T: {r['gapsOver2T']}; loss events (steps in the deficit's lower "
                     f"envelope): {r['lossyGaps']}, ~{r['gapLostEst']} samples lost")
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
                     f"{'within' if abs(s['fDevPct']) <= 2.0 else 'OUTSIDE'} the §6.3 ±2 % health gate")
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


def imu_landing(lane, timeline_us):
    """Largest |f| spike (> 2 g) within 250 ms after a flight dip (|f| < 0.4 g for >= 80 ms).
    Returns dict or None; times in us on `timeline_us`."""
    f = lane.fmag()
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


def hop_one(path, doc):
    lanes = [ln for ln in lanes_from_doc(doc, path) if ln.instrument != "hackmotion"]
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
        s = {"sensor": ln.label, "serial": ln.serial, "role": ln.role}
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
             f"{'/'.join(str(2 * h + 1) for h in SG_SENSITIVITY)}-point SG windows.", ""]
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
                   "agreementMs": r.get("agreementMs"), **s}
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
        lines.append("")
    cols = ["swing", "sensor", "serial", "role", "offsetMs", "xcorrR", "offsetPeakMs",
            "offsetRawMs", "imuLandMs", "videoLandMs", "peakG", "flightMs", "frameMs", "fitMs",
            "residualMs", "agreementMs", "retimeP50Ms", "retimeP95Ms", "error"]
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


def _synth_hop_doc(rng, lead_ms=37.0, thorax_delay_ms=3.0, fps=150.0):
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
    return doc, t_land, floors


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
    return 0


if __name__ == "__main__":
    sys.exit(main())
