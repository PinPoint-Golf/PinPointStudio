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
 * with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

// Capture data-integrity check: did every camera lane in the frozen window
// deliver its frames on time?
//
// 2026-08-18 swing_0003 lost ~80 face-on frames starting 33 ms after impact —
// the host stalled for ~0.8 s and the backlog then arrived stamped ~1 ms apart
// (arrival times, not exposure times). Nothing told the user. The analysis
// still ran, its post-impact milestones were fabricated from the burst, and
// the shot counted in the session assessment like any other. This check is the
// verdict that makes such a recording visible: a ⚠ on the shot, a persisted
// "captureIntegrity" block in swing.json, and exclusion from session-level
// aggregation. It judges TIMESTAMPS only — it never says the frames' content is
// wrong, only that frames are missing between them.
//
// A hole is one inter-frame interval longer than `holePeriods` × the lane's
// median period (3 periods by default: real jitter on the 150 fps rig peaks at
// ~1.6 periods; a single dropped frame is 2). `impactUs` (window domain, −1 =
// unknown) splits holes into pre- and post-impact: a pre-impact hole means the
// swing itself has missing frames; a post-impact one means only the follow-
// through is affected (the analysis' phase model clips its candidacy at that
// hole — shaft_track_assembly captureHolePeriods — but the milestones after it
// are still not trustworthy).
//
// COVERAGE (2026-10-20 Jess Wrist_03 swing_0010, issue #53). Holes are not the only way frames go
// missing: that swing's video began ~264 ms AFTER impact on both cameras — 232 frames over 1.5 s of
// a 4 s window — and the check above passed it, because there was no gap BETWEEN frames. So each lane
// is also held to the capture window it was cut from: its first frame should reach back to the
// window's start and its last forward to its end, within `edgeToleranceMs`. Two severities, under
// the house rule "show with a warning, never hide":
//   • a lane with NO frame at or before impact FAILS (ok = false; whatever the window): the swing
//     is not on it, so nothing can be judged and the shot leaves the session assessment;
//   • a lane that only starts late or stops early (lateStart / earlyEnd) still has the swing on
//     it — 2026-10-24 Paddy Wrist_01 s10 starts 0.6 s late; Stop pressed in the post-roll ends a
//     clip early — so `ok` stays true: the shot carries the ⚠ and stays assessed.
// The verdict says how much is missing, per camera, on any lane with at least one frame. Lanes
// on another clock or cut to a band by design — the impact camera (shot_processor kImpactKeep*),
// a phone clip timed from its own first frame (ppcp_clip_filer.cpp) — are exempt from coverage,
// but their holes are still judged.
//
// Header-only and free of Qt, mirroring imu_refusion_check.h, so the GUI join
// (ShotProcessor), the offline re-analysis and the tools all reach one verdict.
//
// THE GATE RECORD (trust stage 6) is not fired here: the verdict persists as the
// manifest's captureIntegrity block, and manifest_gates.h turns that block into the
// `shot.captureHoles` / `shot.captureShort` records each time the document's analysis is written.

#include "swing_window.h"
#include "format_descriptor.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <variant>
#include <vector>

namespace pinpoint {

// A hole is an interval longer than this many of the lane's median frame periods (see above).
constexpr double kCaptureHolePeriods = 3.0;

// How far a lane's first / last frame may sit inside the capture window's start / end before the
// clip counts as cut short. Measured on 431 normal library lanes (234 swings, 30 Oct 2026, ~150 fps):
// the first frame sat at most 6.9 ms after the window's start (one frame period) and the last at
// most 20.3 ms before its end (delivery lag); 50 ms leaves 2.5× that margin, and a lane is never
// held tighter than two of its own frame periods, for a slower camera. Measured on the FLIR
// (machine-vision) lanes only — no other camera kind was in the library; past it is a warning only.
constexpr double kCaptureEdgeToleranceMs = 50.0;

// What the coverage half of the check is told about the capture. `judge` false is the old,
// holes-only check. The window is in the lanes' own domain (absolute buffer clock live, the
// document's window block offline); an unknown window (end <= start) still judges impact.
struct CaptureCoverageSpec {
    bool    judge           = false;
    int64_t windowStartUs   = 0;
    int64_t windowEndUs     = -1;
    double  edgeToleranceMs = kCaptureEdgeToleranceMs;
    bool windowKnown() const { return windowEndUs > windowStartUs; }
};

// One camera lane against its capture window. NaN / −1 = not known.
struct CaptureLaneCoverage {
    std::string serial;                                               // the camera, when the format says
    double firstFrameMs   = std::numeric_limits<double>::quiet_NaN(); // ms from the window's start
    double lastFrameMs    = std::numeric_limits<double>::quiet_NaN();
    double missingStartMs = std::numeric_limits<double>::quiet_NaN(); // window start → first frame
    double missingEndMs   = std::numeric_limits<double>::quiet_NaN(); // last frame → window end
    int    framesBeforeImpact = -1;                                   // frames at or before impact
};

struct CaptureIntegrityVerdict {
    bool   ok             = true;   // no hole, and (when judged) a frame before impact, on every checked lane
    int    camerasChecked = 0;      // camera lanes with enough frames to judge
    int    holes          = 0;      // holes across all checked lanes
    int    framesLost     = 0;      // frames the holes swallowed (period-rounded)
    double worstHoleMs    = 0.0;    // the longest single hole
    double firstHoleUs    = -1.0;   // window-domain instant the first hole opens (−1 = none)
    // The same instant in ms from impact, + after (the Charts' shot clock) — NaN when there is no
    // hole or impact is unknown. Domain-free, so the gate record (manifest_gates.h) can quote it
    // whichever window domain the check ran in.
    double firstHoleMs    = std::numeric_limits<double>::quiet_NaN();
    bool   preImpact      = false;  // a hole opened at or before impact (or impact unknown)
    bool   postImpact     = false;  // a hole opened after impact
    double holePeriods    = kCaptureHolePeriods;   // the boundary this verdict used

    // Coverage (issue #53) — all false / NaN / −1 when it was not judged.
    bool   coverageJudged     = false;  // the lanes were held to the capture window
    double windowMs           = std::numeric_limits<double>::quiet_NaN();   // the window's length
    double edgeToleranceMs    = kCaptureEdgeToleranceMs;
    double missingStartMs     = std::numeric_limits<double>::quiet_NaN();   // the worst lane's
    double missingEndMs       = std::numeric_limits<double>::quiet_NaN();
    int    framesBeforeImpact = -1;     // the fewest on any lane (−1 = impact unknown)
    bool   lateStart          = false;  // a lane starts later than the window's start allows
    bool   earlyEnd           = false;  // a lane stops earlier than the window's end allows
    bool   startsAfterImpact  = false;  // a lane has no frame at or before impact
    std::vector<CaptureLaneCoverage> cameras;   // per judged lane, in window order

    bool clipped() const { return lateStart || earlyEnd || startsAfterImpact; }

    // The shot is WITHHELD only when a lane was actually checked and failed (holes, or no frame
    // before impact). A late start or early end alone leaves `ok` true: a warning, not a withhold.
    bool warns() const { return camerasChecked > 0 && !ok; }
    bool edgesShort() const { return camerasChecked > 0 && (lateStart || earlyEnd); }
};

// Judge ONE lane from its frame timestamps (ascending, window domain). Holes need at least 3
// frames; coverage, when asked for, is judged on any lane with a frame (a 2-frame lane after impact
// is exactly the clip that must not pass). camerasChecked is 1 when either was judged.
inline CaptureIntegrityVerdict captureIntegrityOf(const std::vector<int64_t> &tUs,
                                                  int64_t impactUs,
                                                  double holePeriods = kCaptureHolePeriods,
                                                  const CaptureCoverageSpec &cov = {},
                                                  const std::string &serial = {})
{
    CaptureIntegrityVerdict v;
    v.holePeriods = holePeriods;
    if (tUs.empty() || holePeriods <= 0.0) return v;

    std::vector<int64_t> dt;
    dt.reserve(tUs.size() - 1);
    for (size_t i = 1; i < tUs.size(); ++i) dt.push_back(tUs[i] - tUs[i - 1]);
    double periodUs = 0.0;                         // 0 = unknown (one frame)
    if (!dt.empty()) {
        std::vector<int64_t> sorted = dt;
        std::nth_element(sorted.begin(), sorted.begin() + sorted.size() / 2, sorted.end());
        periodUs = std::max(0.0, double(sorted[sorted.size() / 2]));
    }
    const bool holesJudged = tUs.size() >= 3 && periodUs > 0.0;
    const double holeUs = holePeriods * periodUs;

    if (holesJudged) v.camerasChecked = 1;
    for (size_t i = 0; holesJudged && i < dt.size(); ++i) {
        const double gap = double(dt[i]);
        if (gap <= holeUs) continue;
        ++v.holes;
        v.framesLost += std::max(0, int(std::lround(gap / periodUs)) - 1);
        v.worstHoleMs = std::max(v.worstHoleMs, gap * 1e-3);
        const int64_t opensAt = tUs[i];                // the last frame before the hole
        if (v.firstHoleUs < 0.0) {
            v.firstHoleUs = double(opensAt);
            if (impactUs >= 0) v.firstHoleMs = double(opensAt - impactUs) / 1000.0;
        }
        if (impactUs < 0 || opensAt <= impactUs) v.preImpact = true;
        else                                     v.postImpact = true;
    }

    if (cov.judge) {
        v.camerasChecked  = 1;
        v.coverageJudged  = true;
        v.edgeToleranceMs = cov.edgeToleranceMs;
        CaptureLaneCoverage c;
        c.serial = serial;
        if (impactUs >= 0) {
            c.framesBeforeImpact = int(std::count_if(tUs.begin(), tUs.end(),
                                                     [impactUs](int64_t t) { return t <= impactUs; }));
            v.framesBeforeImpact = c.framesBeforeImpact;
            v.startsAfterImpact  = c.framesBeforeImpact == 0;
        }
        if (cov.windowKnown()) {
            const double tolMs = std::max(cov.edgeToleranceMs, 2.0 * periodUs * 1e-3);
            v.windowMs       = double(cov.windowEndUs - cov.windowStartUs) * 1e-3;
            c.firstFrameMs   = double(tUs.front() - cov.windowStartUs) * 1e-3;
            c.lastFrameMs    = double(tUs.back()  - cov.windowStartUs) * 1e-3;
            c.missingStartMs = std::max(0.0, c.firstFrameMs);
            c.missingEndMs   = std::max(0.0, v.windowMs - c.lastFrameMs);
            v.missingStartMs = c.missingStartMs;
            v.missingEndMs   = c.missingEndMs;
            v.lateStart      = c.missingStartMs > tolMs;
            v.earlyEnd       = c.missingEndMs   > tolMs;
        }
        v.cameras.push_back(std::move(c));
    }
    v.ok = v.holes == 0 && !v.startsAfterImpact;
    return v;
}

// Merge two lane verdicts into one shot-level verdict.
inline CaptureIntegrityVerdict mergeCaptureIntegrity(CaptureIntegrityVerdict a,
                                                     const CaptureIntegrityVerdict &b)
{
    a.camerasChecked += b.camerasChecked;
    a.holes          += b.holes;
    a.framesLost     += b.framesLost;
    a.worstHoleMs     = std::max(a.worstHoleMs, b.worstHoleMs);
    if (b.firstHoleUs >= 0.0 && (a.firstHoleUs < 0.0 || b.firstHoleUs < a.firstHoleUs)) {
        a.firstHoleUs = b.firstHoleUs;
        a.firstHoleMs = b.firstHoleMs;
    }
    a.preImpact  = a.preImpact  || b.preImpact;
    a.postImpact = a.postImpact || b.postImpact;
    if (b.coverageJudged) {
        // The worst lane speaks for the shot: the most missing at either edge, the fewest frames
        // before impact. A lane that did not know (NaN / −1) never outvotes one that did.
        const auto worst = [](double x, double y) { return std::isnan(x) ? y : std::isnan(y) ? x : std::max(x, y); };
        if (!a.coverageJudged) a.edgeToleranceMs = b.edgeToleranceMs;
        a.coverageJudged = true;
        if (std::isnan(a.windowMs)) a.windowMs = b.windowMs;
        a.missingStartMs = worst(a.missingStartMs, b.missingStartMs);
        a.missingEndMs   = worst(a.missingEndMs,   b.missingEndMs);
        if (b.framesBeforeImpact >= 0)
            a.framesBeforeImpact = a.framesBeforeImpact >= 0 ? std::min(a.framesBeforeImpact, b.framesBeforeImpact)
                                                             : b.framesBeforeImpact;
        a.lateStart         = a.lateStart         || b.lateStart;
        a.earlyEnd          = a.earlyEnd          || b.earlyEnd;
        a.startsAfterImpact = a.startsAfterImpact || b.startsAfterImpact;
        a.cameras.insert(a.cameras.end(), b.cameras.begin(), b.cameras.end());
    }
    a.ok = a.ok && b.ok;
    return a;
}

// Judge every camera lane in the window. `impactUs` must be in the window's own
// domain (absolute buffer clock for a live window, window-relative for the
// offline loader's) — the same contract the analyzer's job.impactUs follows, and
// `cov`'s window with it. The lanes in `coverageExempt` are judged for holes only:
// the impact camera (a band around impact, never the whole window) and phone clips
// (timed from their own first frame, so neither the window nor impact is on their clock).
inline CaptureIntegrityVerdict checkCaptureIntegrity(const SwingWindow &window,
                                                     int64_t impactUs,
                                                     const CaptureCoverageSpec &cov = {},
                                                     const std::vector<SourceId> &coverageExempt = {},
                                                     double holePeriods = kCaptureHolePeriods)
{
    CaptureIntegrityVerdict v;
    v.holePeriods = holePeriods;
    std::vector<SourceId> cams;
    for (const IndexEntry &e : window.entries())
        if (std::holds_alternative<CameraFormat>(window.formatOf(e.source_id).format)
            && std::find(cams.begin(), cams.end(), e.source_id) == cams.end())
            cams.push_back(e.source_id);
    for (const SourceId sid : cams) {
        std::vector<int64_t> tUs;
        for (const IndexEntry &e : window.entriesFor(sid)) tUs.push_back(e.timestamp_us);
        const bool exempt = std::find(coverageExempt.begin(), coverageExempt.end(), sid) != coverageExempt.end();
        const CaptureCoverageSpec laneCov = exempt ? CaptureCoverageSpec{} : cov;
        v = mergeCaptureIntegrity(v, captureIntegrityOf(tUs, impactUs, holePeriods, laneCov,
                                                        window.formatOf(sid).device_serial));
    }
    return v;
}

} // namespace pinpoint
