/*
 * Copyright (c) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#pragma once

// Which frames the offline pose pass poses, and how a consumer reads a track that was not
// posed at one rate (pose_inference_performance_plan.md step 4). Pure std so the selection
// and the bracket rule are unit-testable (pose_pipeline_test.cpp, skeleton3d_test.cpp).
//
//   selectSinglePass / selectTwoPass*  — the face-on schedules exactly as PoseRunner ran them
//                                        inline before step 4 (the test pins them to a copy of
//                                        the old loops: the face-on schedule is load-bearing —
//                                        thinning it moved phases by up to 951 ms, 6 Oct).
//   selectZoneSchedule                 — the general one: a list of {from, to, stride} zones
//                                        and a rest stride; the DTL's schedule B uses it.
//   dtlZoneSchedule                    — schedule B off the inherited ladder (P1, P2, P8).
//   dtlEarlyLadder                     — that ladder before the resolve (pose.dtlEarly).
//   localGapUs / bracketAt             — a consumer's "is this a bracket or a hole" test that
//                                        follows the track's own local spacing.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace pinpoint::analysis {

// ── The face-on schedules, unchanged ────────────────────────────────────────

// Single pass (an IMU/G3-bounded scan): [iAddr0, i0) is the address-hold pad at addrStride;
// [i0, i1) poses every frame inside [denseLo, denseHi] (when impactUs >= 0 — denseStride is
// NOT applied here, as before) and every stride-th elsewhere. The modulus is on the ABSOLUTE
// entry index, as before.
inline std::vector<size_t> selectSinglePass(const std::vector<int64_t> &ts, size_t iAddr0, size_t i0,
                                            size_t i1, int addrStride, int sparseStride,
                                            int64_t impactUs, int64_t denseLo, int64_t denseHi)
{
    const size_t aS = size_t(std::max(1, addrStride)), sS = size_t(std::max(1, sparseStride));
    std::vector<size_t> out;
    for (size_t i = iAddr0; i < i1; ++i) {
        const bool inAddressZone = i < i0;
        const bool dense = !inAddressZone && impactUs >= 0 && ts[i] >= denseLo && ts[i] <= denseHi;
        if (inAddressZone) {
            if ((i % aS) != 0) continue;
        } else if (!dense && (i % sS) != 0) {
            continue;
        }
        out.push_back(i);
    }
    return out;
}

// Two-pass, pass 1: every coarse-th frame of the whole window.
inline std::vector<size_t> selectTwoPassCoarse(size_t n, int coarseStride)
{
    const size_t c = size_t(std::max(1, coarseStride));
    std::vector<size_t> out;
    for (size_t i = 0; i < n; i += c) out.push_back(i);
    return out;
}

// Two-pass, pass 2: fill [pLo, pHi) — denseStride inside [denseLo, denseHi], sparseStride
// elsewhere, never a frame pass 1 already posed (i % coarse == 0).
inline std::vector<size_t> selectTwoPassFill(const std::vector<int64_t> &ts, size_t pLo, size_t pHi,
                                             int coarseStride, int denseStride, int sparseStride,
                                             int64_t impactUs, int64_t denseLo, int64_t denseHi)
{
    const size_t c = size_t(std::max(1, coarseStride));
    const size_t dS = size_t(std::max(1, denseStride)), sS = size_t(std::max(1, sparseStride));
    std::vector<size_t> out;
    for (size_t i = pLo; i < pHi; ++i) {
        if ((i % c) == 0) continue;
        const bool dense = impactUs >= 0 && ts[i] >= denseLo && ts[i] <= denseHi;
        if ((i % (dense ? dS : sS)) != 0) continue;
        out.push_back(i);
    }
    return out;
}

// ── The general zone schedule ───────────────────────────────────────────────

struct PoseZone {
    int64_t fromUs = 0;   // inclusive
    int64_t toUs   = 0;   // inclusive
    int     stride = 1;
};

// Frames [i0, i1) of `ts` (ascending). Each frame belongs to the FIRST zone whose [from, to]
// contains it, else to the rest. A RUN is a maximal stretch of consecutive frames with the same
// owner; inside a run the stride counts from the run's first frame, and the run's last frame is
// posed too. So every zone edge is posed whatever the stride and the phase of the entry index —
// a band edge in the dense zone is never one frame short — and the extra frame a run end costs
// is at most one per run.
inline std::vector<size_t> selectZoneSchedule(const std::vector<int64_t> &ts, size_t i0, size_t i1,
                                              const std::vector<PoseZone> &zones, int restStride)
{
    std::vector<size_t> out;
    if (i1 <= i0) return out;
    auto owner = [&](int64_t t) {
        for (size_t z = 0; z < zones.size(); ++z)
            if (t >= zones[z].fromUs && t <= zones[z].toUs) return int(z);
        return -1;
    };
    size_t runStart = i0;
    int    runOwner = owner(ts[i0]);
    for (size_t i = i0; i < i1; ++i) {
        const int o = owner(ts[i]);
        if (o != runOwner) { runStart = i; runOwner = o; }
        const bool lastOfRun = i + 1 >= i1 || owner(ts[i + 1]) != o;
        const size_t s = size_t(std::max(1, o >= 0 ? zones[size_t(o)].stride : restStride));
        if (((i - runStart) % s) == 0 || lastOfRun) out.push_back(i);
    }
    return out;
}

// ── Schedule B for the down-the-line camera ─────────────────────────────────
//
// The 6 Oct experiments (docs/research/data/pose/pose_performance_20261006.md §2, §4): stride 1
// in [P2 − 50 ms, P8 + 150 ms], stride 2 in [P1 − 100 ms, P2 − 50 ms), stride 4 elsewhere —
// 54 % of the DTL frames over 21 corpus swings, and, with the consumers reading the local
// gap (bracketAt below), nothing downstream moves: 425 truth pairs 0 worse, fused planes
// ≤ 0.06°, every phase sample within σ on every swing, no metric lost. Schedule A (dense from
// P3 − 50 ms, rest from P8 + 50 ms) kept 48 % but moved the back plane up to 7.8° on three
// swings and left one truth frame worse in the stride-4 finish: the stride-2 backswing between
// P2 and P3 and the rest zone right after P8 were the two things it got wrong.
struct DtlScheduleConfig {
    int     denseStride = 1;
    int     backStride  = 2;
    int     restStride  = 4;
    int64_t densePreUs  = 50000;    // before P2
    int64_t densePostUs = 150000;   // after P8
    int64_t backPreUs   = 100000;   // before P1
    // A missing phase's stand-in. No P2: P1 + 250 ms (address to shaft-parallel is 0.2–0.35 s
    // on the corpus). No P8: impact + 150 ms — the DTL tracker's own fallback for its
    // late-escape rule (dtl_shaft_post.cpp), the shaft being back to parallel by then.
    int64_t p2FromP1Us     = 250000;
    int64_t p8FromImpactUs = 150000;
};

struct DtlSchedule {
    bool                  ok = false;     // false ⇒ pose every frame (today's DTL pass)
    std::vector<PoseZone> zones;
    int                   restStride = 1;
    bool                  p2Fallback = false, p8Fallback = false;
    std::string           why;            // set when !ok
};

// Times in µs; < 0 = absent. P1 (address) is required: without it there is no address hold to
// thin and no anchor for the backswing zone. P8 falls back to impact + 150 ms; with neither
// there is no dense zone to protect and the schedule is refused (stride 1 everywhere).
inline DtlSchedule dtlZoneSchedule(int64_t p1Us, int64_t p2Us, int64_t p8Us, int64_t impactUs,
                                   const DtlScheduleConfig &c = {})
{
    DtlSchedule s;
    if (p1Us < 0) { s.why = "no Address (P1) on the ladder"; return s; }
    if (p2Us < 0) { p2Us = p1Us + c.p2FromP1Us; s.p2Fallback = true; }
    if (p8Us < 0) {
        if (impactUs < 0) { s.why = "no P8 and no impact on the ladder"; return s; }
        p8Us = impactUs + c.p8FromImpactUs;
        s.p8Fallback = true;
    }
    const int64_t denseLo = p2Us - c.densePreUs, denseHi = p8Us + c.densePostUs;
    if (denseHi <= denseLo) { s.why = "P8 + 150 ms is not after P2 − 50 ms"; return s; }
    s.zones.push_back({ denseLo, denseHi, c.denseStride });
    // The backswing zone is listed second, so the frame AT P2 − 50 ms belongs to the dense one.
    if (p1Us - c.backPreUs < denseLo) s.zones.push_back({ p1Us - c.backPreUs, denseLo, c.backStride });
    s.restStride = c.restStride;
    s.ok = true;
    return s;
}

// ── The DTL pass's ladder, from what exists right after the face-on pose ─────
//
// pose.dtlEarly (analysis_dag_design.md step F). The DTL pass needs a swing span (its scan
// window: span − 1 s … span + 0.3 s) and three instants (schedule B's P1, P2, P8, plus impact).
// Today it reads them off the RESOLVED ladder, which puts it behind Ball and Shaft(FO) on the
// critical path. Early, it reads them off what the face-on pose stage already left:
//
//   IMU ladder (segImu conf > 0)   its span, its Address / P2 / P8 / Impact where it carries
//                                  them (absent ⇒ schedule B's own fallbacks); impact else the job's.
//   camera only                    the two-pass pose's coarse span estimate (estimateSwingSpanUs:
//                                  true takeaway → finish0, bounded to [impact − 1.5 s, impact + 1 s]),
//                                  P1 := onset − 100 ms (the address hold
//                                  is before the onset, so the backswing zone starts 200 ms before
//                                  takeaway), no P2 (⇒ P1 + 250 ms), no P8 (⇒ impact + 150 ms),
//                                  impact the job's.
//   neither                        no span (the caller's impact − 2.5 s / + 0.8 s fallback), no P1
//                                  (schedule refused ⇒ every frame).
//
// Both are OUTPUT changes against the resolved ladder: the window is the same formula fed by an
// earlier opinion of the span (the coarse onset sits within a coarse step of the vision ladder's
// swing start, and finish0 at or after its swing end, so the window is a superset or a near-one),
// and the dense zone starts at the fallback P2 − 50 ms (onset + 100 ms) rather than the measured P2 − 50 ms
// — usually earlier (address → P2 is 0.2–0.35 s on the corpus), so more frames at stride 1. Gated.
struct DtlEarlyLadder {
    int64_t     swingStartUs = 0, swingEndUs = 0;   // end ≤ start ⇒ no span
    int64_t     p1Us = -1, p2Us = -1, p8Us = -1, impactUs = -1;
    const char *source = "none";                     // "imu" | "poseSpan" | "none"
};

struct DtlEarlyInputs {
    bool    imu = false;                             // an IMU ladder with conf > 0
    int64_t imuStartUs = 0, imuEndUs = 0;
    int64_t imuP1Us = -1, imuP2Us = -1, imuP8Us = -1, imuImpactUs = -1;
    bool    spanOk = false;                          // the coarse pose span estimate
    int64_t spanStartUs = 0, spanEndUs = 0;
    int64_t jobImpactUs = -1;                        // ≤ 0 ⇒ none
};

inline DtlEarlyLadder dtlEarlyLadder(const DtlEarlyInputs &in, int64_t p1BeforeOnsetUs = 100000,
                                     int64_t spanBeforeImpactUs = 1500000,
                                     int64_t spanAfterImpactUs = 1000000)
{
    DtlEarlyLadder l;
    const int64_t jobImpact = in.jobImpactUs > 0 ? in.jobImpactUs : -1;
    if (in.imu && in.imuEndUs > in.imuStartUs) {
        l.swingStartUs = in.imuStartUs;
        l.swingEndUs   = in.imuEndUs;
        l.p1Us = in.imuP1Us;
        l.p2Us = in.imuP2Us;
        l.p8Us = in.imuP8Us;
        l.impactUs = in.imuImpactUs >= 0 ? in.imuImpactUs : jobImpact;
        l.source = "imu";
    } else if (in.spanOk && in.spanEndUs > in.spanStartUs) {
        // Bounded by impact: the coarse finish0 sat 1.1–1.4 s past the resolved swing end on
        // the corpus (6 Oct; the resolved end is ≈ impact + 0.7 s), which grew the DTL window
        // 2.75 → 4.1 s and moved the person crop. So the end is min(finish0, impact + 1 s) and
        // the start max(onset, impact − 1.5 s) — the window [start − 1 s, end + 0.3 s] is then
        // never wider than today's no-span fallback (impact − 2.5 s / + 1.3 s here). P1 stays
        // on the raw onset.
        l.swingStartUs = in.spanStartUs;
        l.swingEndUs   = in.spanEndUs;
        if (jobImpact > 0) {
            l.swingStartUs = std::max(l.swingStartUs, jobImpact - spanBeforeImpactUs);
            l.swingEndUs   = std::min(l.swingEndUs, jobImpact + spanAfterImpactUs);
            if (l.swingEndUs <= l.swingStartUs) {   // a span that misses impact: trust impact
                l.swingStartUs = jobImpact - spanBeforeImpactUs;
                l.swingEndUs   = jobImpact + spanAfterImpactUs;
            }
        }
        l.p1Us = in.spanStartUs - p1BeforeOnsetUs;
        l.impactUs = jobImpact;
        l.source = "poseSpan";
    } else {
        l.impactUs = jobImpact;
    }
    return l;
}

// ── Reading a track that was not posed at one rate ──────────────────────────
//
// The track's own spacing around the interval (k, k+1): the median of the up-to-7 frame gaps
// centred on it. Seven, not two: a run end can leave one odd gap (a stride-4 rest run ending a
// frame or two before the backswing zone), and a single dropped frame in a stride-1 zone must
// still read as a 2× hole against its 6.5 ms neighbours, not set the local spacing itself.
// `timeAt(i)` returns the i-th timestamp; n frames, n >= 2, k <= n - 2.
template <class TimeAt>
int64_t localGapUs(size_t n, TimeAt timeAt, size_t k)
{
    if (n < 2) return 0;
    const size_t lo = k >= 3 ? k - 3 : 0, hi = std::min(n - 2, k + 3);
    std::vector<int64_t> g;
    for (size_t j = lo; j <= hi; ++j) {
        const int64_t d = timeAt(j + 1) - timeAt(j);
        if (d > 0) g.push_back(d);
    }
    if (g.empty()) return 0;
    std::nth_element(g.begin(), g.begin() + g.size() / 2, g.end());
    return g[g.size() / 2];
}

inline int64_t localGapUs(const std::vector<int64_t> &t, size_t k)
{
    return localGapUs(t.size(), [&](size_t i) { return t[i]; }, k);
}

// A two-frame bracket at instant t, or the nearest frame, or nothing.
//   bracket accepted when its span ≤ bracketUs        (OFF: 12 ms, skeleton3d before step 4)
//   else the nearest frame when ≤ nearestUs away      (OFF: 6 ms)
// With localGap, both limits follow the track: max(floor, bracketGapScale × local gap) and
// max(floor, nearestGapScale × local gap). At 155 fps a stride-2 backswing is 13 ms and a stride-4
// address 26 ms — both over 12 ms, so the fixed rule paired nothing there and skeleton3d dropped
// the DTL camera on 4 of 21 swings; 1.5× the local spacing accepts the schedule's own gap and
// still refuses a dropout (a missing frame doubles the gap).
struct BracketRule {
    int64_t bracketUs = 12000;
    int64_t nearestUs = 6000;
    bool    localGap  = false;
    double  bracketGapScale = 1.5;
    double  nearestGapScale = 0.75;
};

struct Bracket {
    bool   ok = false;
    size_t a = 0, b = 0;   // a == b ⇒ one frame
    double w = 0.0;        // value = v[a] + w (v[b] − v[a])
};

template <class TimeAt>
Bracket bracketAt(size_t n, TimeAt timeAt, int64_t t, const BracketRule &r)
{
    Bracket out;
    if (n == 0) return out;
    // First frame at or after t (std::lower_bound over the accessor).
    size_t lo = 0, hi = n;
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        if (timeAt(mid) < t) lo = mid + 1; else hi = mid;
    }
    const size_t h = lo;
    auto limits = [&](size_t k, double &brk, double &nrst) {
        brk = double(r.bracketUs);
        nrst = double(r.nearestUs);
        if (r.localGap && n >= 2) {
            const double g = double(localGapUs(n, timeAt, std::min(k, n - 2)));
            brk  = std::max(brk,  r.bracketGapScale * g);
            nrst = std::max(nrst, r.nearestGapScale * g);
        }
    };
    double brk = 0, nrst = 0;
    size_t a = 0, b = 0;
    double w = 0;
    if (h == n) { a = b = n - 1; limits(n >= 2 ? n - 2 : 0, brk, nrst); }
    else if (h == 0) { a = b = 0; limits(0, brk, nrst); }
    else {
        b = h;
        a = b - 1;
        limits(a, brk, nrst);
        const double span = double(timeAt(b) - timeAt(a));
        if (span > brk || span <= 0) {
            const size_t m = (t - timeAt(a)) <= (timeAt(b) - t) ? a : b;
            if (double(std::llabs(timeAt(m) - t)) > nrst) return out;
            a = b = m;
        } else {
            w = double(t - timeAt(a)) / span;
        }
    }
    if (a == b && double(std::llabs(timeAt(a) - t)) > nrst) return out;
    out.ok = true; out.a = a; out.b = b; out.w = w;
    return out;
}

} // namespace pinpoint::analysis
