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

#include "wrist_analyzer.h"

#include <QPointF>
#include <QString>
#include <algorithm>
#include <cmath>
#include <limits>

#include "imu_vision_fuser.h"
#include "metric_extractor.h"
#include "phase_segmenter.h"
#include "pose_runner.h"
#include "shaft_tracker.h"
#include "swing_scorer.h"
#include "wrist_angles.h"
#include "swing_window.h"
#include "../Core/pp_debug.h"

using namespace pinpoint::analysis;

namespace {

constexpr double kPiD = 3.14159265358979323846;

// Shaft lean vs image-vertical from the smoothed shaft track, signed toward
// the target: a face-on camera shows the golfer's left on image-right, so for
// a right-handed golfer positive = shaft (grip-to-head) tilted away from
// image-right = hands ahead of the clubhead. PROVISIONAL sign pending the
// hardware sign-lock pass (the wrist metrics needed the same treatment).
// Unscored — no validated reference band yet; the scorer's band table simply
// doesn't list the key.
MetricSeries buildShaftLeanSeries(const ShaftTrack2D &shaft, int handedness,
                                  int64_t impactUs)
{
    MetricSeries m;
    m.key   = QStringLiteral("impactShaftLean");
    m.label = QStringLiteral("Shaft lean");
    m.unit  = QStringLiteral("°");

    const double sgn = (handedness == 2) ? -1.0 : 1.0;
    int64_t bestDt = std::numeric_limits<int64_t>::max();
    PhaseSample impact;
    impact.phase = Phase::Impact;
    for (const ShaftSample2D &s : shaft.samples) {
        // Lean = deviation of the grip→head ray from straight-down (+90° in
        // the y-down image convention), wrapped near zero.
        double lean = s.thetaRad - kPiD / 2.0;
        lean = std::remainder(lean, 2.0 * kPiD);
        const double deg = sgn * lean * 180.0 / kPiD;
        m.t_us.push_back(s.t_us);
        m.value.push_back(deg);
        const int64_t dt = std::llabs(s.t_us - impactUs);
        if (dt < bestDt) { bestDt = dt; impact.t_us = s.t_us; impact.value = deg; }
    }
    if (bestDt != std::numeric_limits<int64_t>::max())
        m.phaseSamples.push_back(impact);
    return m;
}

const MetricSeries *find(const std::vector<MetricSeries> &v, const QString &key)
{
    for (const MetricSeries &m : v)
        if (m.key == key) return &m;
    return nullptr;
}

// Copy of the fused streams restricted to [fromUs, toUs] — the metric grid
// spans the detected swing, not the raw 5 s ring. Timestamps stay absolute.
FusedStreams trimStreams(const FusedStreams &in, int64_t fromUs, int64_t toUs)
{
    const auto lo = std::lower_bound(in.timeGrid.begin(), in.timeGrid.end(), fromUs);
    const auto hi = std::upper_bound(in.timeGrid.begin(), in.timeGrid.end(), toUs);
    const size_t a = size_t(lo - in.timeGrid.begin());
    const size_t b = size_t(hi - in.timeGrid.begin());
    if (a >= b)
        return in;   // degenerate bounds — keep the full grid
    FusedStreams out;
    out.timeGrid.assign(in.timeGrid.begin() + long(a), in.timeGrid.begin() + long(b));
    for (const SegmentStream &s : in.segments) {
        if (s.qAnat.size() != in.timeGrid.size())
            continue;   // malformed stream — drop rather than misalign
        SegmentStream t;
        t.role = s.role;
        t.qAnat.assign(s.qAnat.begin() + long(a), s.qAnat.begin() + long(b));
        if (s.gyroDps.size() == in.timeGrid.size())
            t.gyroDps.assign(s.gyroDps.begin() + long(a), s.gyroDps.begin() + long(b));
        if (s.accelG.size() == in.timeGrid.size())
            t.accelG.assign(s.accelG.begin() + long(a), s.accelG.begin() + long(b));
        out.segments.push_back(std::move(t));
    }
    return out;
}

double phaseValue(const MetricSeries &m, Phase p)
{
    for (const PhaseSample &s : m.phaseSamples)
        if (s.phase == p) return s.value;
    return m.value.empty() ? 0.0 : m.value.back();
}

// Build the flat key → {label, value} map the carousel renders, sampled at Impact.
// Value strings use the shared wristMetricLabel() (bow/cup, hinge, roll — wrist_angles.h).
QVariantMap buildMetricsMap(const std::vector<MetricSeries> &series)
{
    QVariantMap out;
    for (const MetricSeries &m : series) {
        out.insert(m.key, QVariantMap{
            { QStringLiteral("label"), m.label },
            { QStringLiteral("value"), wristMetricLabel(m.key, phaseValue(m, Phase::Impact)) },
        });
    }
    return out;
}

// Lead-wrist FE curve from Address → Impact, ~24 points, normalised to 0..1 (y up
// = more flexion) for the existing PpTrace sparkline.
QVariantList buildTrace(const std::vector<MetricSeries> &series,
                        const std::vector<PhaseEvent> &phases)
{
    QVariantList out;
    const MetricSeries *fe = find(series, QStringLiteral("leadWristFlexExt"));
    if (!fe || fe->value.empty() || fe->t_us.empty())
        return out;

    int64_t addrT = fe->t_us.front(), impT = fe->t_us.back();
    for (const PhaseEvent &e : phases) {
        if (e.phase == Phase::Address) addrT = e.t_us;
        if (e.phase == Phase::Impact)  impT  = e.t_us;
    }
    const auto lo = std::lower_bound(fe->t_us.begin(), fe->t_us.end(), addrT) - fe->t_us.begin();
    auto hi = std::lower_bound(fe->t_us.begin(), fe->t_us.end(), impT) - fe->t_us.begin();
    hi = std::min<long>(hi, static_cast<long>(fe->value.size()) - 1);
    if (hi <= lo) return out;

    double vmin = fe->value[lo], vmax = fe->value[lo];
    for (long i = lo; i <= hi; ++i) { vmin = std::min(vmin, fe->value[i]); vmax = std::max(vmax, fe->value[i]); }
    const double span = std::max(vmax - vmin, 1e-6);

    const int kPts = 24;
    for (int k = 0; k < kPts; ++k) {
        const double f = double(k) / (kPts - 1);
        const long i = lo + std::lround(f * (hi - lo));
        const double x = f;
        const double y = 0.9 - 0.8 * (fe->value[i] - vmin) / span;   // y-down, flexion up
        out.append(QPointF(x, y));
    }
    return out;
}

} // namespace

ShotAnalysisResult WristAnalyzer::analyze(const pinpoint::SwingWindow &window,
                                          const ShotAnalysisJob &job)
{
    ShotAnalysisResult r;

    const FusedStreams streams = ImuVisionFuser::fuse(window, job.imuBindings);
    if (streams.timeGrid.empty() || streams.segments.empty()) {
        r.ok = false;
        r.error = QStringLiteral("no IMU data in window");
        ppWarn() << "[WristAnalysis] no fusable IMU streams — degraded result";
        return r;
    }

    const Segmentation segmentation = PhaseSegmenter::segment(streams, job.impactUs);
    const std::vector<PhaseEvent> &phases = segmentation.events;
    // Metric grids span address → finish, not the raw ring (design A.6): hand
    // the extractor a trimmed copy when the bounds are real. Everything else
    // (shaft qHand sampling) keeps the full streams.
    std::vector<MetricSeries> series = MetricExtractor::extract(
        segmentation.conf > 0.f
            ? trimStreams(streams, segmentation.swingStartUs, segmentation.swingEndUs)
            : streams,
        phases, job.handedness);
    if (series.empty()) {
        r.ok = false;
        r.error = QStringLiteral("no wrist metrics (need forearm + hand IMUs)");
        return r;
    }

    auto detail   = std::make_shared<SwingAnalysis>();

    // ShaftTracker (S3): offline pose + club track over the face-on camera.
    // Heavy (ViTPose per frame) — runs after the cheap IMU stages; failures
    // degrade to empty/invalid tracks, never to a failed analysis. Progress
    // budget: the IMU stages are near-instant, so the pose pass owns 10–70%
    // and the shaft detection scan 70–98% (the assembly tail is cheap).
    if (job.faceOnCameraCount > 0 && !job.cameraSources.empty()) {
        ShotAnalysisRunnerOptions opt;
        opt.impactUs   = job.impactUs;
        opt.handedness = job.handedness;
        if (job.progress) {
            job.progress(0.10f);
            opt.progress = [&job](float f) { job.progress(0.10f + 0.60f * f); };
        }
        detail->pose2d = PoseRunner::run(window, job.cameraSources.front(), opt);
        if (!detail->pose2d.frames.empty()) {
            ShotAnalysisJob sub = job;
            if (job.progress)
                sub.progress = [&job](float f) { job.progress(0.70f + 0.28f * f); };
            detail->shaft = ShaftTracker::track(window, detail->pose2d, streams, phases, sub);
            if (detail->shaft.valid)
                series.push_back(buildShaftLeanSeries(detail->shaft, job.handedness,
                                                      job.impactUs));
        }
    }

    detail->series       = series;
    detail->phases       = phases;
    detail->segmentation = segmentation;
    detail->tier   = static_cast<int>(ReconstructionTier::Mono3DPlusImu);
    detail->score  = SwingScorer::score(series, job.sessionType);

    r.metrics     = buildMetricsMap(series);
    r.tracePoints = buildTrace(series, phases);
    r.score       = detail->score.overall;
    r.detail      = detail;
    r.ok          = true;

    ppInfo() << "[WristAnalysis]" << series.size() << "metrics, score" << r.score
             << "— grid" << static_cast<qint64>(streams.timeGrid.size());
    return r;
}
