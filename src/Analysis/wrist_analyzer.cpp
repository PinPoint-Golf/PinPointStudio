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

#include "analysis_tuning.h"
#include "imu_vision_fuser.h"
#include "orientation_refuse_tuning.h"
#include "metric_extractor.h"
#include "phase_segmenter.h"
#include "pose_runner.h"
#include "shaft_tracker.h"
#include "swing_scorer.h"
#include "wrist_angles.h"
#include "wrist_analysis_adapter.h"
#include "wrist_assessment_engine.h"
#include "wrist_assessment_tuning.h"
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

// SwingLab tuning: SegmentationConfig with "seg.*" overrides applied.
SegmentationConfig segConfigFor(const QVariantMap &ov)
{
    SegmentationConfig c;
    if (ov.isEmpty()) return c;
    namespace tn = tuning;
    tn::apply(ov, "seg.fcEnvelopeHz",         c.fcEnvelopeHz);
    tn::apply(ov, "seg.stillGyroDps",         c.stillGyroDps);
    tn::apply(ov, "seg.stillAccelTolG",       c.stillAccelTolG);
    tn::apply(ov, "seg.topMinBeforeImpactUs", c.topMinBeforeImpactUs);
    tn::apply(ov, "seg.topMaxBeforeImpactUs", c.topMaxBeforeImpactUs);
    tn::apply(ov, "seg.topImpactSlackUs",     c.topImpactSlackUs);
    tn::apply(ov, "seg.takeawayFracOfPeak",   c.takeawayFracOfPeak);
    tn::apply(ov, "seg.takeawayMinDps",       c.takeawayMinDps);
    tn::apply(ov, "seg.takeawayQuietUs",      c.takeawayQuietUs);
    tn::apply(ov, "seg.backswingMinUs",       c.backswingMinUs);
    tn::apply(ov, "seg.backswingMaxUs",       c.backswingMaxUs);
    tn::apply(ov, "seg.addressStillMinUs",    c.addressStillMinUs);
    tn::apply(ov, "seg.transBeforeTopUs",     c.transBeforeTopUs);
    tn::apply(ov, "seg.transAfterTopUs",      c.transAfterTopUs);
    tn::apply(ov, "seg.transMinMeanDps",      c.transMinMeanDps);
    tn::apply(ov, "seg.voteAgreeUs",          c.voteAgreeUs);
    tn::apply(ov, "seg.thoraxAgreeUs",        c.thoraxAgreeUs);
    tn::apply(ov, "seg.maxSpeedPostImpactUs", c.maxSpeedPostImpactUs);
    tn::apply(ov, "seg.finishGyroDps",        c.finishGyroDps);
    tn::apply(ov, "seg.finishMinAfterImpactUs", c.finishMinAfterImpactUs);
    tn::apply(ov, "seg.finishSustainUs",      c.finishSustainUs);
    tn::apply(ov, "seg.finishMinUs",          c.finishMinUs);
    tn::apply(ov, "seg.finishMaxUs",          c.finishMaxUs);
    tn::apply(ov, "seg.boundPadUs",           c.boundPadUs);
    return c;
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

// Impact-continuity diagnostic (§5.3.1): the max orientation discontinuity across [impactUs ± halfUs]
// BEYOND what gyro propagation predicts — i.e. the accel-correction (or shock-glitch) contribution,
// NOT the legitimate fast downswing rotation (which the raw step would be dominated by). For each
// step we predict q[i] by integrating the body-frame gyro from q[i-1] and measure the geodesic
// residual against the actual fused q[i]. A filter trusting a saturated impact accel spikes this;
// the adaptive schedule's blanking / saturation-reject / gyro-dominant gain drive it toward 0. So it
// rises with accel trust (beta) and falls under blanking — a genuine filter.* objective. Returns -1
// if no usable window.
double impactContinuityDeg(const FusedStreams &streams, int64_t impactUs, int64_t halfUs = 25000)
{
    if (streams.segments.empty() || streams.timeGrid.empty() || impactUs <= 0)
        return -1.0;
    const SegmentStream &seg = streams.segments.front();
    const std::vector<QQuaternion> &q    = seg.qAnat;
    const std::vector<QVector3D>   &gyro = seg.gyroDps;   // deg/s, anatomical body frame
    const std::vector<int64_t>     &grid = streams.timeGrid;
    const size_t n = std::min({ q.size(), gyro.size(), grid.size() });
    double maxRes = -1.0;
    for (size_t i = 1; i < n; ++i) {
        if (std::llabs(grid[i] - impactUs) > halfUs || std::llabs(grid[i - 1] - impactUs) > halfUs)
            continue;
        const double dtS = double(grid[i] - grid[i - 1]) * 1e-6;
        if (dtS <= 0.0)
            continue;
        // Gyro-predicted orientation: body-frame ω post-multiplies (q_new = q_old ⊗ Δq_body).
        const QVector3D w = gyro[i - 1];
        const float wmag = w.length();           // deg/s
        QQuaternion dq;                           // identity when (near-)still
        if (wmag > 1e-6f)
            dq = QQuaternion::fromAxisAndAngle(w.normalized(), float(wmag * dtS));
        const QQuaternion qPred = (q[i - 1] * dq).normalized();
        double d = std::abs(static_cast<double>(QQuaternion::dotProduct(qPred, q[i])));
        d = std::min(1.0, d);
        const double resDeg = 2.0 * std::acos(d) * 57.29577951308232;
        maxRes = std::max(maxRes, resDeg);
    }
    return maxRes;
}

} // namespace

ShotAnalysisResult WristAnalyzer::analyze(const pinpoint::SwingWindow &window,
                                          const ShotAnalysisJob &job)
{
    ShotAnalysisResult r;

    // IMU fusion is OPTIONAL. A webcam-only capture still yields camera-driven
    // (pose + shaft) analysis, so absent IMU streams DEGRADE the result rather
    // than failing it: phase segmentation and wrist metrics are IMU-derived and
    // simply don't run, but the pose pass below still populates analysis.pose2d.
    //
    // filter.refuse (SwingLab §5.3.1): re-derive orientation offline from raw accel+gyro under the
    // filter.* schedule and feed THAT into the fusion, so the adaptive filter drives the wrist metric.
    // Off by default ⇒ the stored live quaternion (production), byte-identical.
    pinpoint::RefuseConfig refusion;
    const bool doRefuse = tuningWantsRefusion(job.tuningOverrides);
    if (doRefuse)
        refusion = refuseConfigFromTuning(job.tuningOverrides, job.impactUs);
    const FusedStreams streams = ImuVisionFuser::fuse(window, job.imuBindings, 200.0,
                                                      doRefuse ? &refusion : nullptr);
    const bool hasImu = !streams.timeGrid.empty() && !streams.segments.empty();
    if (!hasImu)
        ppInfo() << "[WristAnalysis] no fusable IMU streams — camera-only (pose) analysis";

    // Phase segmentation + wrist metrics are IMU-derived. Without IMU the
    // segmentation stays default (conf 0 ⇒ full-window pose scan) and series is
    // empty. Metric grids span address → finish (design A.6): hand the extractor
    // a trimmed copy when the bounds are real; shaft qHand sampling keeps the
    // full streams.
    Segmentation              segmentation;
    std::vector<MetricSeries> series;
    if (hasImu) {
        segmentation = PhaseSegmenter::segment(streams, job.impactUs,
                                               segConfigFor(job.tuningOverrides));
        series = MetricExtractor::extract(
            segmentation.conf > 0.f
                ? trimStreams(streams, segmentation.swingStartUs, segmentation.swingEndUs)
                : streams,
            segmentation.events, job.handedness);
    }
    const std::vector<PhaseEvent> &phases = segmentation.events;

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
        // Heavy-stage bounding (v3 G3): scan only the detected swing span
        // (+pad for pass-1 timing error). The shaft detection loop follows
        // pose coverage, so this bounds both heavy stages. conf 0 ⇒ full
        // window, exactly today's behaviour.
        if (segmentation.conf > 0.f) {
            constexpr int64_t kScanPadUs = 150000;
            opt.scanStartUs = segmentation.swingStartUs - kScanPadUs;
            opt.scanEndUs   = segmentation.swingEndUs   + kScanPadUs;
        }
        if (job.progress) {
            job.progress(0.10f);
            opt.progress = [&job](float f) { job.progress(0.10f + 0.60f * f); };
        }
        detail->pose2d = job.poseTrackPath.isEmpty()
                             ? PoseRunner::run(window, job.cameraSources.front(), opt)
                             : PoseRunner::loadFromJson(job.poseTrackPath,
                                                        job.cameraSources.front());
        if (!detail->pose2d.frames.empty()) {
            ShotAnalysisJob sub = job;
            if (job.progress)
                sub.progress = [&job](float f) { job.progress(0.70f + 0.28f * f); };
            detail->shaft = ShaftTracker::track(window, detail->pose2d, streams,
                                                segmentation, sub);
            if (detail->shaft.valid)
                series.push_back(buildShaftLeanSeries(detail->shaft, job.handedness,
                                                      job.impactUs));
        }
    }

    // Require at least one usable analysis product — IMU-derived wrist metrics
    // OR camera-derived pose. With neither there is nothing to persist, so fail
    // and let the shot degrade to video-only (the prior no-IMU contract, now
    // reached only when the pose pass also produced nothing).
    if (series.empty() && detail->pose2d.frames.empty()) {
        r.ok = false;
        r.error = hasImu
                      ? QStringLiteral("no wrist metrics (need forearm + hand IMUs)")
                      : QStringLiteral("no IMU and no pose data in window");
        return r;
    }

    detail->series       = series;
    detail->phases       = phases;
    detail->segmentation = segmentation;
    for (const ImuSegmentBinding &b : job.imuBindings) {
        BindingRecord rec;
        rec.serial = QString::fromStdString(window.formatOf(b.source).device_serial);
        rec.role   = b.role;
        rec.alignA = b.alignA;
        rec.mountM = b.mountM;
        rec.anatCalibrated       = b.anatCalibrated;
        rec.calibrated           = b.calibrated;
        rec.mountDeviationDeg    = b.mountDeviationDeg;
        rec.mountGravityErrorDeg = b.mountGravityErrorDeg;
        rec.calibratedAtUtc      = b.calibratedAtUtc;
        rec.calibAgeSec          = b.calibAgeSec;
        detail->bindings.push_back(std::move(rec));
    }
    detail->tier   = static_cast<int>(hasImu ? ReconstructionTier::Mono3DPlusImu
                                              : ReconstructionTier::Angles2D);
    detail->score  = SwingScorer::score(series, job.sessionType, job.tuningOverrides);

    // Filter-quality objective (only when re-fusion drove the orientation): the impact-continuity
    // diagnostic gives filter.* an IMU-only score.py check, independent of a vision shaft track.
    if (doRefuse && hasImu)
        detail->filterImpactStepDeg = impactContinuityDeg(streams, job.impactUs);

    // Tier-2 wrist assessment (faults/strengths + score v2), offline opt-in (SwingLab). The live
    // GUI runs this in its own diagnostics model; here it is gated on job.runAssessment so the
    // sampler.*/rules.*/bands.* knobs become observable in swing.json without changing production.
    if (job.runAssessment && hasImu && !series.empty()) {
        const InMemoryWristAngleSource src = buildWristAngleSource(detail->series, detail->phases);
        const auto provider = makeReferenceBandProvider(BandProviderKind::Archetype);
        const WristAssessmentConfig acfg = wristAssessmentConfigFor(job.tuningOverrides);
        const PpWristAssessmentResult ar = WristAssessmentEngine::assess(src, *provider, acfg);
        detail->findings        = ar.findings;
        detail->assessmentScore = ar.score.total;
        ppInfo() << "[WristAnalysis] assessment:" << detail->findings.size() << "findings, score v2"
                 << detail->assessmentScore;
    }

    r.metrics     = buildMetricsMap(series);
    r.tracePoints = buildTrace(series, phases);
    r.score       = detail->score.overall;
    r.detail      = detail;
    r.ok          = true;

    ppInfo() << "[WristAnalysis]" << series.size() << "metrics, score" << r.score
             << "— grid" << static_cast<qint64>(streams.timeGrid.size());
    return r;
}
