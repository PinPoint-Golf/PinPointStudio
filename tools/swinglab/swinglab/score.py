# Scorecards: Tier-1 physics invariants (the "expected tracking shape"),
# Tier-2 cross-modal consistency, Tier-3 truth metrics where truth.json
# exists. Every check is a named verdict with a value and a threshold so a
# model (or a human) can act on it without watching video.

import math

import numpy as np

from . import RunResult, Swing, git_sha


def _check(name, ok, value, threshold, severity="fail"):
    return {"name": name, "pass": bool(ok), "value": value,
            "threshold": threshold, "severity": severity}


def invariants(run: RunResult, scope="full"):
    """Tier 1+2 — no labels needed. `scope` is the markup swing scope
    (full/pitch/chip/putt); full-swing-only checks are skipped for partial
    swings, whose arc/tempo legitimately fall outside the full-swing bounds."""
    full = (scope or "full") == "full"
    checks = []
    an = run.analysis
    club = run.club
    samples = run.club_samples()
    impact_us = None
    for p in an.get("phases", []):
        if p.get("phase") == 5:
            impact_us = p["t_us"]

    checks.append(_check("club.valid", club.get("valid", False), club.get("valid"), True))
    cov = club.get("coverage", 0.0)
    checks.append(_check("club.coverage", cov >= 0.6, round(cov, 3), ">=0.6"))

    if samples:
        t = np.array([s["t_us"] for s in samples], dtype=np.int64)
        th = np.array([s["theta"] for s in samples])
        flags = np.array([s.get("flags", 0) for s in samples], dtype=int)
        heads = np.array([s["head"] for s in samples])  # normalized 0..1
        lens = np.array([s.get("lenPx", 0.0) for s in samples])

        checks.append(_check("track.monotonic_t", bool(np.all(np.diff(t) > 0)),
                             int(np.sum(np.diff(t) <= 0)), "0 inversions"))

        # No teleporting theta between MEASURED neighbours (coasted spans may
        # legitimately bridge larger angles).
        meas = (flags & 0x01) != 0
        dth = np.abs(np.diff(th))
        both_meas = meas[1:] & meas[:-1]
        worst = float(np.max(dth[both_meas])) if np.any(both_meas) else 0.0
        checks.append(_check("track.theta_step", worst < math.radians(25),
                             round(math.degrees(worst), 1), "<25 deg/frame"))

        # Downswing sweep: total |theta| travel in the 400 ms before impact.
        # Full-swing-only: a pitch/chip/putt has far less arc, so the [86, 458]
        # band doesn't apply — skip it rather than fail it.
        if impact_us is not None:
            if full:
                win = (t >= impact_us - 400_000) & (t <= impact_us)
                sweep = float(np.sum(np.abs(np.diff(th[win])))) if np.sum(win) > 3 else 0.0
                checks.append(_check("track.downswing_sweep", 1.5 <= sweep <= 8.0,
                                     round(math.degrees(sweep), 0), "[86, 458] deg"))

            # theta_dot peak near impact.
            dt = np.diff(t) / 1e6
            rate = np.abs(np.diff(th)) / np.maximum(dt, 1e-6)
            if len(rate):
                pk = int(np.argmax(rate))
                pk_dt_ms = abs(int(t[pk] - impact_us)) / 1000.0
                checks.append(_check("track.peak_rate_near_impact", pk_dt_ms <= 120,
                                     round(pk_dt_ms, 0), "<=120 ms", severity="warn"))

        # Head-point continuity (normalized units; 0.25 ~ a quarter frame).
        dhead = np.linalg.norm(np.diff(heads, axis=0), axis=1)
        worst_head = float(np.max(dhead)) if len(dhead) else 0.0
        checks.append(_check("track.head_step", worst_head < 0.25,
                             round(worst_head, 3), "<0.25 frame/frame"))

        # L smoothness between measured neighbours.
        dlen = np.abs(np.diff(lens))
        worst_len = float(np.max(dlen[both_meas])) if np.any(both_meas) else 0.0
        checks.append(_check("track.len_step", worst_len < 80,
                             round(worst_len, 0), "<80 px/frame", severity="warn"))

    # Tier 2 — cross-modal.
    corr = club.get("imuVisionCorr", 0.0)
    has_bindings = bool(an.get("bindings"))
    checks.append(_check("xmodal.imu_vision_corr",
                         (corr >= 0.9) if has_bindings else True,
                         round(corr, 3), ">=0.9 when IMUs bound",
                         severity="warn"))

    # Segmentation sanity.
    phases = {p["phase"]: p for p in an.get("phases", [])}
    ts = [p["t_us"] for p in an.get("phases", [])]
    checks.append(_check("seg.monotone", ts == sorted(ts), 0, "ordered"))
    # Tempo ratio is a full-swing notion (backswing vs downswing); partial swings
    # don't have a comparable top, so skip it for pitch/chip/putt.
    if full and 1 in phases and 2 in phases and 5 in phases:
        back = (phases[2]["t_us"] - phases[1]["t_us"]) / 1e6
        down = (phases[5]["t_us"] - phases[2]["t_us"]) / 1e6
        ratio = back / down if down > 0 else 0
        checks.append(_check("seg.tempo_ratio", 1.2 <= ratio <= 6.0,
                             round(ratio, 2), "[1.2, 6.0]", severity="warn"))
    return checks


def truth_metrics(run: RunResult, swing: Swing):
    """Tier 3 — only when truth.json exists."""
    truth = swing.truth()
    if not truth or "shaft" not in truth:
        return []
    samples = run.club_samples()
    if not samples:
        return [_check("truth.theta_rms", False, None, "track empty")]
    club = run.club
    Wpx, Hpx = club.get("frameWidth", 1), club.get("frameHeight", 1)

    t = np.array([s["t_us"] for s in samples], dtype=np.int64)
    th = np.unwrap(np.array([s["theta"] for s in samples]))
    heads = np.array([s["head"] for s in samples]) * np.array([Wpx, Hpx])

    errs_th, errs_head = [], []
    for tf in truth["shaft"]:
        tt = tf["t_us"]
        if tt < t[0] or tt > t[-1]:
            continue
        i = int(np.searchsorted(t, tt))
        i = min(max(i, 1), len(t) - 1)
        f = (tt - t[i - 1]) / max(t[i] - t[i - 1], 1)
        thi = th[i - 1] + f * (th[i] - th[i - 1])
        # wrap-free comparison
        d = (thi - tf["theta"] + math.pi) % (2 * math.pi) - math.pi
        errs_th.append(d)
        hi = heads[i - 1] + f * (heads[i] - heads[i - 1])
        errs_head.append(float(np.linalg.norm(hi - np.array(tf["head"]))))

    out = []
    if errs_th:
        rms = math.degrees(float(np.sqrt(np.mean(np.square(errs_th)))))
        out.append(_check("truth.theta_rms_deg", rms < 3.0, round(rms, 2), "<3 deg"))
        med_head = float(np.median(errs_head))
        out.append(_check("truth.head_median_px", med_head < 25, round(med_head, 1),
                          "<25 px"))
    ev = truth.get("events", {})
    if ev:
        t0 = ev.get("t0_us", 0)
        phases = {p["phase"]: p["t_us"] for p in run.analysis.get("phases", [])}

        # P-system event keys → analyzer Phase enum (swing_analysis.h):
        #   p1→Address(0) p3→MidBackswing(8) p4→Top(2) p6→Delivery(9)
        #   p7→Impact(5) p9→FollowThrough(11) p10→Finish(7).
        # p2/p5/p8 (shaft/arm parallel) have no analyzer event — see the parallel
        # check below. (key, enum, label, tol_s, severity)
        P_CHECKS = [
            ("p1",  0,  "p1_address",      0.05, "warn"),
            ("p3",  8,  "p3_armpar_back",  0.06, "warn"),
            ("p4",  2,  "p4_top",          0.03, "fail"),
            ("p6",  9,  "p6_delivery",     0.04, "warn"),
            ("p7",  5,  "p7_impact",       0.03, "warn"),  # impact is often an input, not output
            ("p9",  11, "p9_armpar_fwd",   0.08, "warn"),
            ("p10", 7,  "p10_finish",      0.12, "warn"),
        ]
        # Legacy vocabulary (pre-P fixtures); applied only if the P-key for that
        # enum was not present, so we never double-check one phase.
        LEGACY = [("takeaway", 1, "takeaway", 0.08, "warn"),
                  ("top",      2, "top",      0.03, "fail"),
                  ("finish",   7, "finish",   0.12, "warn")]

        checked = set()
        for key, ph, name, tol, sev in P_CHECKS:
            want = ev.get(f"{key}_s")
            if want is None or ph not in phases:
                continue
            err = abs((phases[ph] - t0) / 1e6 - want)
            out.append(_check(f"truth.event_{name}_s", err <= tol, round(err, 3), f"<={tol}s", severity=sev))
            checked.add(ph)
        for key, ph, name, tol, sev in LEGACY:
            if ph in checked:
                continue
            want = ev.get(f"{key}_s")
            if want is None or ph not in phases:
                continue
            err = abs((phases[ph] - t0) / 1e6 - want)
            out.append(_check(f"truth.event_{name}_s", err <= tol, round(err, 3), f"<={tol}s", severity=sev))

        # Parallel-geometry consistency (label sanity): at P2/P6/P8 the shaft is
        # ~parallel to the ground, so the labelled club angle should be ~horizontal
        # in a face-on frame. theta = atan2(dy, dx); |sin(theta)| is the vertical
        # fraction → asin gives degrees off horizontal (0=flat, 90=vertical).
        shaft_by_tus = {f["t_us"]: f for f in truth.get("shaft", [])}
        for pkey in ("p2", "p6", "p8"):
            wsec = ev.get(f"{pkey}_s")
            if wsec is None or not shaft_by_tus:
                continue
            tus = t0 + int(round(wsec * 1e6))
            nearest = min(shaft_by_tus, key=lambda t: abs(t - tus))
            if abs(nearest - tus) > 30000:          # no labelled club within 30 ms of this P
                continue
            off_deg = math.degrees(math.asin(min(1.0, abs(math.sin(shaft_by_tus[nearest]["theta"])))))
            out.append(_check(f"truth.parallel_{pkey}_deg", off_deg <= 20.0, round(off_deg, 1),
                              "<=20 deg (shaft ~horizontal)", severity="warn"))
    return out


def scorecard(run_dir, swing_dir):
    run = RunResult(run_dir)
    swing = Swing(swing_dir)
    conditions = swing.truth_meta()
    checks = invariants(run, conditions.get("scope")) + truth_metrics(run, swing)
    fails = [c for c in checks if not c["pass"] and c["severity"] == "fail"]
    warns = [c for c in checks if not c["pass"] and c["severity"] == "warn"]
    score = round(100.0 * sum(c["pass"] for c in checks) / max(len(checks), 1))
    return {
        "swing": swing.name,
        "score": score,
        "ok": run.meta.get("ok", False),
        "frames": run.meta.get("frames"),
        "analyzeMs": run.meta.get("analyzeMs"),
        "conditions": conditions,   # markup meta: scope/tempo/contact/club/shaft/lighting
        "sha": git_sha(),
        "failures": [c["name"] for c in fails],
        "warnings": [c["name"] for c in warns],
        "checks": checks,
    }
