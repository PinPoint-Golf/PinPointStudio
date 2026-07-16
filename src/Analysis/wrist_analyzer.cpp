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

#include <QElapsedTimer>
#include <QPointF>
#include <QString>
#include <algorithm>
#include <cmath>
#include <limits>
#include <variant>

#include "analysis_stage.h"
#include "analysis_tuning.h"
#include "ball_runner.h"
#include "foot_metrics.h"
#include "hand_axis.h"
#include "head_track.h"
#include "pose_wrist_angle_source.h"
#include "imu_vision_fuser.h"
#include "orientation_refuse_tuning.h"
#include "metric_extractor.h"
#include "phase_segmenter.h"
#include "pose_runner.h"
#include "pose_smoother.h"
#include "pose_synthesis.h"
#include "shaft_tracker.h"
#include "wrist_resemblance.h"
#include "score_uncertainty.h"
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
    // Lean = deviation of the grip→head ray from straight-down (+90° in the y-down
    // image convention). thetaRad lives on the atan2 branch cut, so accumulate a
    // CONTINUOUS angle (np.unwrap: wrap each inter-sample DELTA into (−π, π] and
    // sum) instead of wrapping every sample independently — the shaft rotates
    // smoothly through the top/finish where the raw angle crosses ±180°, and a
    // per-sample wrap renders that as a spurious ~360° flip. The shaft never turns
    // >180° between (dense-near-impact) samples, so the unwrap is unambiguous. The
    // first sample is normalised into (−π, π] so the curve starts canonically.
    double cont = 0.0, prevRaw = 0.0;
    bool first = true;
    for (const ShaftSample2D &s : shaft.samples) {
        const double raw = s.thetaRad - kPiD / 2.0;
        if (first) { cont = std::remainder(raw, 2.0 * kPiD); first = false; }
        else       { cont += std::remainder(raw - prevRaw, 2.0 * kPiD); }
        prevRaw = raw;
        const double deg = sgn * cont * 180.0 / kPiD;
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

// ── Stage-pipeline implementation (analysis_pipeline_fusion_architecture §10.5) ──
//
// TEMPORARY, flag-gated (analyzer.staged): reached only from analyzeStaged() below,
// which WristAnalyzer::analyze() calls when the flag is set. Every stage's run()
// body is the monolith's code verbatim (same call/argument expressions, same order
// of writes) so codegen is identical from this one TU — the file-local helpers above
// serve both paths. Deleted with the monolith after the byte-parity corpus gate.
//
// The five parity hazards (proposal §10.5) are handled structurally: the LOCAL
// `series` (scorer/metrics/trace) and `detail->series` are distinct context homes
// (ctx.series vs ctx.detail->series); the segmentation triple ctx.segImu/segVision/
// seg keeps pre- vs post-adoption readers honest; RequireProducts halts via
// ctx.halted so stages 11–17 provably never run in the fail case; the two nested
// progress-rescale lambdas capture ctx.job (a stack-local of analyzeStaged); the
// per-stage timers wrap the identical calls.

// 1. IMU resample/fusion (monolith 282-287). Runs iff the job bound at least one
//    IMU; when the bindings are unfusable the fused streams come back empty and
//    hasImuStreams() is false, exactly as the monolith's unconditional fuse would.
struct ImuResampleStage : AnalysisStage {
    QString name() const override { return QStringLiteral("ImuResample"); }
    bool canRun(const AnalysisContext &ctx) const override { return !ctx.caps.imus.empty(); }
    void run(AnalysisContext &ctx) override
    {
        // filter.refuse (SwingLab §5.3.1): re-derive orientation offline from raw accel+gyro under the
        // filter.* schedule and feed THAT into the fusion, so the adaptive filter drives the wrist metric.
        // Off by default ⇒ the stored live quaternion (production), byte-identical.
        pinpoint::RefuseConfig refusion;
        ctx.doRefuse = tuningWantsRefusion(ctx.job.tuningOverrides);
        if (ctx.doRefuse)
            refusion = refuseConfigFromTuning(ctx.job.tuningOverrides, ctx.job.impactUs);
        ctx.streams = ImuVisionFuser::fuse(*ctx.window, ctx.job.imuBindings, 200.0,
                                           ctx.doRefuse ? &refusion : nullptr);
    }
};

// 2. Phase segmentation (monolith 300-301) — IMU-derived, so it needs fused streams.
struct ImuSegmentationStage : AnalysisStage {
    QString name() const override { return QStringLiteral("ImuSegmentation"); }
    bool canRun(const AnalysisContext &ctx) const override { return ctx.hasImuStreams(); }
    void run(AnalysisContext &ctx) override
    {
        ctx.segImu = PhaseSegmenter::segment(ctx.streams, ctx.job.impactUs,
                                             segConfigFor(ctx.job.tuningOverrides));
    }
};

// 3. Wrist metrics (monolith 302-306). ctx.series is empty here (first writer), so
//    assigning the extractor output matches the monolith `series = extract(...)`.
//    Metric grids span address → finish: hand the extractor a trimmed copy when the
//    bounds are real; shaft qHand sampling keeps the full streams.
struct WristMetricsStage : AnalysisStage {
    QString name() const override { return QStringLiteral("WristMetrics"); }
    bool canRun(const AnalysisContext &ctx) const override { return ctx.segImu.has_value(); }
    void run(AnalysisContext &ctx) override
    {
        const Segmentation &segImu = *ctx.segImu;
        ctx.series = MetricExtractor::extract(
            segImu.conf > 0.f
                ? trimStreams(ctx.streams, segImu.swingStartUs, segImu.swingEndUs)
                : ctx.streams,
            segImu.events, ctx.job.handedness);
    }
};

// 4. Offline pose pass (monolith 321-375). Heavy (ViTPose per frame) — runs after
//    the cheap IMU stages; failures degrade to an empty track, never a failed
//    analysis. The pose pass owns 10-70% of the progress budget. Publishes the
//    resolved runner options for the ball/shaft stages.
struct PoseStage : AnalysisStage {
    QString name() const override { return QStringLiteral("Pose"); }
    bool canRun(const AnalysisContext &ctx) const override
    {
        return ctx.caps.hasCamera(CameraPlacement::FaceOn);
    }
    void run(AnalysisContext &ctx) override
    {
        // Pre-adoption segmentation drives the scan bounds (≡ monolith line 333 —
        // the vision-segmentation fallback hasn't been adopted yet).
        const Segmentation segImu = ctx.segImu.value_or(Segmentation{});

        ShotAnalysisRunnerOptions opt;
        opt.impactUs   = ctx.job.impactUs;
        opt.handedness = ctx.job.handedness;
        opt.motionCaptureQuality = ctx.job.motionCaptureQuality;   // High -> ViTPose++-L (if downloaded)
        opt.tuningOverrides = ctx.job.tuningOverrides;             // WB1 pose.crop.* / pose.decode.dark
        // Heavy-stage bounding (v3 G3): scan only the detected swing span (+pad for
        // pass-1 timing error). conf 0 ⇒ full window. job.fullWindow opts out.
        if (segImu.conf > 0.f && !ctx.job.fullWindow) {
            constexpr int64_t kScanPadUs = 150000;
            opt.scanStartUs = segImu.swingStartUs - kScanPadUs;
            opt.scanEndUs   = segImu.swingEndUs   + kScanPadUs;
            // v3.4 plan §2: G3's scanStartUs is pinned close to Takeaway, so a real
            // still address sits almost entirely before it — reach back further,
            // sparsely, so the ball-anchor pass has address-hold frames to work with.
            opt.addressScanPadUs = 4'000'000;   // 4 s default reach, capped by window start
            {
                namespace tn = pinpoint::analysis::tuning;
                tn::apply(ctx.job.tuningOverrides, "shaft.addressScanPadUs", opt.addressScanPadUs);
                tn::apply(ctx.job.tuningOverrides, "shaft.addressStride",    opt.addressStride);
            }
        } else if (!ctx.job.fullWindow) {
            // No IMU-derived span (conf 0 ⇒ camera-only): break the pose/span
            // chicken-and-egg with the two-pass pose (plan §5). fullWindow still
            // opts out (correctness over speed on explicit re-analysis).
            opt.twoPass = true;
            {
                namespace tn = pinpoint::analysis::tuning;
                tn::apply(ctx.job.tuningOverrides, "pose.coarseStride", opt.coarseStride);
                tn::apply(ctx.job.tuningOverrides, "pose.densePreMs",   opt.densePreMs);
                tn::apply(ctx.job.tuningOverrides, "pose.densePostMs",  opt.densePostMs);
                tn::apply(ctx.job.tuningOverrides, "pose.denseStride",  opt.denseStride);
            }
        }
        if (ctx.job.progress) {
            ctx.job.progress(0.10f);
            opt.progress = [&job = ctx.job](float f) { job.progress(0.10f + 0.60f * f); };
        }
        QElapsedTimer poseWall;
        poseWall.start();
        ctx.detail->pose2d = ctx.job.poseTrackPath.isEmpty()
                                 ? PoseRunner::run(*ctx.window, ctx.job.cameraSources.front(), opt)
                                 : PoseRunner::loadFromJson(ctx.job.poseTrackPath,
                                                            ctx.job.cameraSources.front());
        ctx.detail->timings.poseMs = int(poseWall.elapsed());
        ctx.runnerOpt = std::move(opt);
    }
};

// 5. Motion-overlay pose smoother (monolith 376-424). runnerOpt present ≡ hasCamera,
//    so the gate is the monolith's inner `if (!frames.empty())` under hasCamera.
struct PoseSmoothStage : AnalysisStage {
    QString name() const override { return QStringLiteral("PoseSmooth"); }
    bool canRun(const AnalysisContext &ctx) const override
    {
        return ctx.runnerOpt.has_value() && !ctx.detail->pose2d.frames.empty();
    }
    void run(AnalysisContext &ctx) override
    {
        // Run the offline non-causal RTS smoother ONCE here on the worker, caching a
        // companion smoothed track parallel to pose2d.frames. Frame pixel dims come
        // from the face-on camera format; when it is unavailable the smoother is
        // skipped and the "smoothed" block is simply omitted from swing.json.
        const pinpoint::FormatDescriptor &fd = ctx.window->formatOf(ctx.job.cameraSources.front());
        if (const auto *cfmt = std::get_if<pinpoint::CameraFormat>(&fd.format);
            cfmt && cfmt->width > 0 && cfmt->height > 0) {
            PoseSmootherOutput so = smoothPoseTrack(ctx.detail->pose2d.frames,
                                                    int(cfmt->width), int(cfmt->height));
            ctx.detail->pose2d.smoothed    = std::move(so.smoothed);
            ctx.detail->pose2d.smoothedAux = std::move(so.aux);

            // WB4 smoothed-hands grip anchor (pose.gripFromSmoothedHands, dark).
            // Recompute each smoothed frame's grip anchors from its SMOOTHED hand
            // keypoints, then mirror onto the parallel raw frame — ShaftTracker reads
            // pose2d.frames. OFF ⇒ both tracks are byte-identical.
            bool gripFromSmoothed = pinpoint::tuned::pose::grip::kFromSmoothedHands;
            tuning::apply(ctx.job.tuningOverrides, "pose.gripFromSmoothedHands", gripFromSmoothed);
            if (gripFromSmoothed
                    && ctx.detail->pose2d.smoothed.size() == ctx.detail->pose2d.frames.size()) {
                const bool leftLeads = (ctx.job.handedness != 2);
                for (size_t k = 0; k < ctx.detail->pose2d.smoothed.size(); ++k) {
                    PoseFrame2D &sf = ctx.detail->pose2d.smoothed[k];
                    computeGripAnchors(sf, leftLeads);
                    ctx.detail->pose2d.frames[k].leadHand  = sf.leadHand;
                    ctx.detail->pose2d.frames[k].trailHand = sf.trailHand;
                    ctx.detail->pose2d.frames[k].handConf  = sf.handConf;
                }
            }

            // Motion overlay: dense VIZ-tier upsample of the smoothed skeleton onto a
            // fixed 240 Hz grid so replay overlays scrub smoothly. Viz-only; empty ⇒
            // pose2d.synth omitted (byte-identical).
            PoseSynthConfig psCfg;
            tuning::apply(ctx.job.tuningOverrides, "poseSynth.enabled", psCfg.enabled);
            tuning::apply(ctx.job.tuningOverrides, "poseSynth.rateHz",  psCfg.rateHz);
            ctx.detail->pose2d.smoothedSynth =
                synthesizePoseTrack(ctx.detail->pose2d.smoothed, psCfg);
        }
    }
};

// 6. Ball-track resolve (monolith 435-443). v3.4 §9: an explicit injection path wins
//    (SwingLab fixtures), else whatever the job already carries, else replay the
//    production ball detector offline over this frozen window. Empty is a valid no-op.
struct BallStage : AnalysisStage {
    QString name() const override { return QStringLiteral("Ball"); }
    bool canRun(const AnalysisContext &ctx) const override
    {
        return ctx.runnerOpt.has_value() && !ctx.detail->pose2d.frames.empty();
    }
    void run(AnalysisContext &ctx) override
    {
        QElapsedTimer ballWall;
        ballWall.start();
        ctx.ball = !ctx.job.ballTrackPath.isEmpty()
            ? BallRunner::loadFromJson(ctx.job.ballTrackPath, ctx.job.cameraSources.front())
            : (!ctx.job.ballTrack.frames.empty()
                   ? ctx.job.ballTrack
                   : BallRunner::run(*ctx.window, ctx.job.cameraSources.front(), ctx.detail->pose2d,
                                     *ctx.runnerOpt, ctx.job.ballSearchRoi, ctx.job.ballBaseline));
        ctx.detail->timings.ballMs = int(ballWall.elapsed());
    }
};

// 7. Shaft track (monolith 425-427, 446-454). The `sub` job copy (only the shaft
//    tracker consumes it) rescales progress to 70-98%; moving its construction after
//    the ball resolve is unobservable. The 5th arg is the PRE-adoption segmentation
//    (≡ monolith line 450); the vision phase model is captured for SegResolve but
//    only when there is no IMU segmentation to fall back on.
struct ShaftStage : AnalysisStage {
    QString name() const override { return QStringLiteral("Shaft"); }
    bool canRun(const AnalysisContext &ctx) const override { return ctx.ball.has_value(); }
    void run(AnalysisContext &ctx) override
    {
        ShotAnalysisJob sub = ctx.job;
        if (ctx.job.progress)
            sub.progress = [&job = ctx.job](float f) { job.progress(0.70f + 0.28f * f); };
        // Capture the tracker's hands-only phase model only when there is no IMU
        // segmentation to fall back on (the trace is free otherwise).
        ShaftTracker::ShaftTrace strace;
        QElapsedTimer shaftWall;
        shaftWall.start();
        ctx.detail->shaft = ShaftTracker::track(*ctx.window, ctx.detail->pose2d, *ctx.ball,
                                                ctx.streams, ctx.segImu.value_or(Segmentation{}),
                                                sub, ctx.hasImuStreams() ? nullptr : &strace);
        ctx.detail->timings.shaftMs = int(shaftWall.elapsed());
        // Surface the resolved ball track for the replay overlay (design §9).
        ctx.detail->ball = *ctx.ball;
        if (!ctx.hasImuStreams())
            ctx.segVision = strace.segmentation;
    }
};

// 8. Segmentation resolve (monolith 458-459) — the mutable-local semantics: keep the
//    IMU segmentation if present, else adopt the vision one when it has confidence,
//    else the default (conf 0 ⇒ "bounds are just the window").
struct SegResolveStage : AnalysisStage {
    QString name() const override { return QStringLiteral("SegResolve"); }
    void run(AnalysisContext &ctx) override
    {
        ctx.seg = ctx.segImu
                      ? *ctx.segImu
                      : (ctx.segVision && ctx.segVision->conf > 0.f ? *ctx.segVision
                                                                    : Segmentation{});
    }
};

// 9. Shaft-lean series (monolith 460-462) — appended to the LOCAL series after the
//    wrist metrics, preserving element order for the scorer/metrics/trace.
struct ShaftLeanStage : AnalysisStage {
    QString name() const override { return QStringLiteral("ShaftLean"); }
    bool canRun(const AnalysisContext &ctx) const override { return ctx.detail->shaft.valid; }
    void run(AnalysisContext &ctx) override
    {
        ctx.series.push_back(buildShaftLeanSeries(ctx.detail->shaft, ctx.job.handedness,
                                                  ctx.job.impactUs));
    }
};

// 10. Require at least one usable product (monolith 470-476) — IMU-derived wrist
//     metrics OR camera-derived pose. With neither, halt: projectResult degrades the
//     shot to video-only (the prior no-IMU contract).
struct RequireProductsStage : AnalysisStage {
    QString name() const override { return QStringLiteral("RequireProducts"); }
    void run(AnalysisContext &ctx) override
    {
        if (ctx.series.empty() && ctx.detail->pose2d.frames.empty()) {
            ctx.halted    = true;
            ctx.haltError = ctx.hasImuStreams()
                                ? QStringLiteral("no wrist metrics (need forearm + hand IMUs)")
                                : QStringLiteral("no IMU and no pose data in window");
        }
    }
};

// 11. Bind the local products onto the detail (monolith 479-482). detail->phases is
//     bound AFTER the vision-segmentation fallback may have reassigned it (ctx.seg).
struct BindDetailStage : AnalysisStage {
    QString name() const override { return QStringLiteral("BindDetail"); }
    void run(AnalysisContext &ctx) override
    {
        ctx.detail->series       = ctx.series;
        ctx.detail->phases       = ctx.seg.events;
        ctx.detail->segmentation = ctx.seg;
    }
};

// 12. Head tracking (monolith 497-515) — Address-referenced sway/lift/tilt appended
//     to the DETAIL series only (never the local series the scorer/carousel read).
//     Reads the post-adoption segmentation. UNSCORED.
struct HeadTrackStage : AnalysisStage {
    QString name() const override { return QStringLiteral("HeadTrack"); }
    bool canRun(const AnalysisContext &ctx) const override
    {
        return ctx.caps.hasCamera(CameraPlacement::FaceOn) && !ctx.detail->pose2d.frames.empty();
    }
    void run(AnalysisContext &ctx) override
    {
        const std::vector<PhaseEvent> &phases = ctx.seg.events;
        const pinpoint::FormatDescriptor &fd = ctx.window->formatOf(ctx.job.cameraSources.front());
        if (const auto *cfmt = std::get_if<pinpoint::CameraFormat>(&fd.format);
            cfmt && cfmt->width > 0 && cfmt->height > 0) {
            int64_t addressUs = -1;
            if (ctx.seg.conf > 0.f)
                if (const PhaseEvent *a = ctx.seg.eventFor(Phase::Address))
                    addressUs = a->t_us;
            const HeadTrackConfig hcfg = HeadTrackConfig::fromOverrides(ctx.job.tuningOverrides);
            const HeadTrackResult head =
                trackHead(ctx.detail->pose2d, int(cfmt->width), int(cfmt->height), addressUs, hcfg);
            // Head-plane px→mm from the inter-ear ruler; ≤ 0 ⇒ ×frame units.
            const double pxPerMm = (head.addrScalePx > 0.0 && hcfg.earWidthMm > 0.0)
                                       ? head.addrScalePx / hcfg.earWidthMm : -1.0;
            for (const MetricSeries &m : buildHeadSeries(head, phases, pxPerMm))
                ctx.detail->series.push_back(m);
        }
    }
};

// 13. Setup + footwork metrics (monolith 525-540) — stance width, per-foot flare,
//     toe-line angle + lead-heel-lift trace. DETAIL series only, UNSCORED.
struct FootMetricsStage : AnalysisStage {
    QString name() const override { return QStringLiteral("FootMetrics"); }
    bool canRun(const AnalysisContext &ctx) const override
    {
        return ctx.caps.hasCamera(CameraPlacement::FaceOn) && !ctx.detail->pose2d.frames.empty();
    }
    void run(AnalysisContext &ctx) override
    {
        const std::vector<PhaseEvent> &phases = ctx.seg.events;
        const pinpoint::FormatDescriptor &fd = ctx.window->formatOf(ctx.job.cameraSources.front());
        if (const auto *cfmt = std::get_if<pinpoint::CameraFormat>(&fd.format);
            cfmt && cfmt->width > 0 && cfmt->height > 0) {
            int64_t addressUs = -1;
            if (ctx.seg.conf > 0.f)
                if (const PhaseEvent *a = ctx.seg.eventFor(Phase::Address))
                    addressUs = a->t_us;
            const bool leadIsLeft = (ctx.job.handedness != 2);
            const FootMetricsResult feet =
                trackFeet(ctx.detail->pose2d, int(cfmt->width), int(cfmt->height), leadIsLeft,
                         addressUs, FootMetricsConfig::fromOverrides(ctx.job.tuningOverrides));
            for (const MetricSeries &m : buildFootSeries(feet, phases))
                ctx.detail->series.push_back(m);
        }
    }
};

// 14. IMU calibration bindings (monolith 542-555) — one BindingRecord per bound
//     device, keyed by the stable device serial.
struct BindingsStage : AnalysisStage {
    QString name() const override { return QStringLiteral("Bindings"); }
    void run(AnalysisContext &ctx) override
    {
        for (const ImuSegmentBinding &b : ctx.job.imuBindings) {
            BindingRecord rec;
            rec.serial = QString::fromStdString(ctx.window->formatOf(b.source).device_serial);
            rec.role   = b.role;
            rec.alignA = b.alignA;
            rec.mountM = b.mountM;
            rec.anatCalibrated       = b.anatCalibrated;
            rec.calibrated           = b.calibrated;
            rec.mountDeviationDeg    = b.mountDeviationDeg;
            rec.mountGravityErrorDeg = b.mountGravityErrorDeg;
            rec.calibratedAtUtc      = b.calibratedAtUtc;
            rec.calibAgeSec          = b.calibAgeSec;
            ctx.detail->bindings.push_back(std::move(rec));
        }
    }
};

// 15. Resemblance score + uncertainty (monolith 556-573). The scorer/interval read
//     the LOCAL series (never detail->series, which carries head/foot metrics). The
//     §B.7 interval brackets the resemblance value while it IS `overall`; the
//     Assessment stage clears it when it takes over the headline. filterImpactStepDeg
//     is populated only when offline re-fusion drove the orientation.
struct ResemblanceStage : AnalysisStage {
    QString name() const override { return QStringLiteral("Resemblance"); }
    void run(AnalysisContext &ctx) override
    {
        const std::vector<PhaseEvent> &phases = ctx.seg.events;
        ctx.detail->tier = static_cast<int>(ctx.hasImuStreams()
                                                ? ReconstructionTier::Mono3DPlusImu
                                                : ReconstructionTier::Angles2D);
        // Wrist estimand = per-archetype resemblance (design §B.0a).
        ctx.detail->score = WristResemblanceScorer::score(ctx.series, ctx.job.tuningOverrides);
        ctx.detail->score.interval = ScoreUncertainty::wristInterval(ctx.detail->score, ctx.series,
                                                                     phases, ctx.job.tuningOverrides);
        if (ctx.doRefuse && ctx.hasImuStreams())
            ctx.detail->filterImpactStepDeg = impactContinuityDeg(ctx.streams, ctx.job.impactUs);
    }
};

// 16. Tier-2 wrist assessment (monolith 581-600) — the AI-coach feed. Gated on the
//     LOCAL series, but reads detail->series/detail->phases (includes head/foot — the
//     monolith does, deliberately). Overrides the headline score, clears the interval.
struct AssessmentStage : AnalysisStage {
    QString name() const override { return QStringLiteral("Assessment"); }
    bool canRun(const AnalysisContext &ctx) const override
    {
        return ctx.job.runAssessment && ctx.hasImuStreams() && !ctx.series.empty();
    }
    void run(AnalysisContext &ctx) override
    {
        const InMemoryWristAngleSource src = buildWristAngleSource(ctx.detail->series,
                                                                   ctx.detail->phases);
        const auto provider = makeReferenceBandProvider(BandProviderKind::Archetype);
        const WristAssessmentConfig acfg = wristAssessmentConfigFor(ctx.job.tuningOverrides);
        const PpWristAssessmentResult ar = WristAssessmentEngine::assess(src, *provider, acfg);
        ctx.detail->findings        = ar.findings;
        ctx.detail->assessmentScore = ar.score.total;
        // Headline overall score is now the penalty-based assessment score (0-100).
        ctx.detail->score.overall   = ar.score.total;
        // The §B.7 interval brackets the resemblance value, NOT this penalty-based
        // score — clear it until the assessment score's own error model exists.
        ctx.detail->score.interval  = ScoreInterval{};

        ppInfo() << "[WristAnalysis] assessment:" << ctx.detail->findings.size() << "findings, score v2"
                 << ctx.detail->assessmentScore;
    }
};

// 17. WB4 IMU-less pose wrist assessment (monolith 609-628, dark by default). Runs
//     ONLY when NO IMU wrist source exists and the camera pose track is present — the
//     IMU assessment path stays primary. fromOverrides is pure/cheap, so canRun
//     re-derives .enabled and run() rebuilds the full config.
struct PoseAssessmentStage : AnalysisStage {
    QString name() const override { return QStringLiteral("PoseAssessment"); }
    bool canRun(const AnalysisContext &ctx) const override
    {
        return PoseWristAngleConfig::fromOverrides(ctx.job.tuningOverrides).enabled
            && !ctx.hasImuStreams() && ctx.job.runAssessment
            && ctx.caps.hasCamera(CameraPlacement::FaceOn)
            && !ctx.detail->pose2d.frames.empty();
    }
    void run(AnalysisContext &ctx) override
    {
        const PoseWristAngleConfig poseWristCfg =
            PoseWristAngleConfig::fromOverrides(ctx.job.tuningOverrides);
        const pinpoint::FormatDescriptor &fd = ctx.window->formatOf(ctx.job.cameraSources.front());
        if (const auto *cfmt = std::get_if<pinpoint::CameraFormat>(&fd.format);
            cfmt && cfmt->width > 0 && cfmt->height > 0) {
            const PoseWristAngleSource src(ctx.detail->pose2d, ctx.detail->phases, ctx.job.handedness,
                                           int(cfmt->width), int(cfmt->height), poseWristCfg);
            const auto provider = makeReferenceBandProvider(BandProviderKind::Archetype);
            const WristAssessmentConfig acfg = wristAssessmentConfigFor(ctx.job.tuningOverrides);
            const PpWristAssessmentResult ar = WristAssessmentEngine::assess(src, *provider, acfg);
            ctx.detail->findings        = ar.findings;
            ctx.detail->assessmentScore = ar.score.total;
            ctx.detail->score.overall   = ar.score.total;
            ctx.detail->score.interval  = ScoreInterval{};   // apparent-angle proxy: no §B.7 interval
            ppInfo() << "[WristAnalysis] pose (IMU-less) assessment:" << ctx.detail->findings.size()
                     << "findings, score v2" << ctx.detail->assessmentScore;
        }
    }
};

// The Wrist session's stage list, in monolith block order. File-local until the
// Swing session needs to share it (§10.5 step 4 — explicitly not now).
SessionProfile wristProfile()
{
    SessionProfile p;
    p.name = QStringLiteral("Wrist");
    p.stages.push_back(std::make_unique<ImuResampleStage>());
    p.stages.push_back(std::make_unique<ImuSegmentationStage>());
    p.stages.push_back(std::make_unique<WristMetricsStage>());
    p.stages.push_back(std::make_unique<PoseStage>());
    p.stages.push_back(std::make_unique<PoseSmoothStage>());
    p.stages.push_back(std::make_unique<BallStage>());
    p.stages.push_back(std::make_unique<ShaftStage>());
    p.stages.push_back(std::make_unique<SegResolveStage>());
    p.stages.push_back(std::make_unique<ShaftLeanStage>());
    p.stages.push_back(std::make_unique<RequireProductsStage>());
    p.stages.push_back(std::make_unique<BindDetailStage>());
    p.stages.push_back(std::make_unique<HeadTrackStage>());
    p.stages.push_back(std::make_unique<FootMetricsStage>());
    p.stages.push_back(std::make_unique<BindingsStage>());
    p.stages.push_back(std::make_unique<ResemblanceStage>());
    p.stages.push_back(std::make_unique<AssessmentStage>());
    p.stages.push_back(std::make_unique<PoseAssessmentStage>());
    return p;
}

// Project the resolved context onto the flat ShotAnalysisResult (monolith 470-476
// halt return + 630-639 success tail). On halt: ok=false + haltError with a NULL
// detail and everything else default-constructed, exactly like the early return.
ShotAnalysisResult projectResult(AnalysisContext &ctx)
{
    ShotAnalysisResult r;
    if (ctx.halted) {
        r.ok    = false;
        r.error = ctx.haltError;
        return r;
    }
    r.metrics     = buildMetricsMap(ctx.series);
    r.tracePoints = buildTrace(ctx.series, ctx.seg.events);
    r.score       = ctx.detail->score.overall;
    ctx.detail->timings.totalMs = int(ctx.wall.elapsed());
    r.detail      = ctx.detail;
    r.ok          = true;

    ppInfo() << "[WristAnalysis]" << ctx.series.size() << "metrics, score" << r.score
             << "— grid" << static_cast<qint64>(ctx.streams.timeGrid.size());
    return r;
}

// Flag-gated entry point (analyzer.staged). Builds the context, runs the Wrist
// profile, projects the result. ctx.wall starts at the top of the work so totalMs is
// equivalent to the monolith's whole-analyze() wall. The "no fusable IMU" log fires
// gated on !hasImuStreams() (≡ monolith line 289) — so it still fires when the
// resample stage was skipped for zero bindings.
ShotAnalysisResult analyzeStaged(const pinpoint::SwingWindow &window,
                                 const ShotAnalysisJob &job)
{
    AnalysisContext ctx{ CaptureCapabilities::fromJob(job), job, &window };
    ctx.detail = std::make_shared<SwingAnalysis>();
    ctx.wall.start();

    runStages(wristProfile(), ctx);

    if (!ctx.hasImuStreams())
        ppInfo() << "[WristAnalysis] no fusable IMU streams — camera-only (pose) analysis";

    return projectResult(ctx);
}

} // namespace

ShotAnalysisResult WristAnalyzer::analyze(const pinpoint::SwingWindow &window,
                                          const ShotAnalysisJob &job)
{
    // TEMPORARY (removed with the monolith after the byte-parity corpus gate):
    // route to the stage-pipeline implementation when analyzer.staged is set.
    // Default false ⇒ production runs the monolith below, byte-identical.
    bool staged = false;
    tuning::apply(job.tuningOverrides, "analyzer.staged", staged);
    if (staged)
        return analyzeStaged(window, job);

    ShotAnalysisResult r;
    QElapsedTimer wall;   // whole-analyze() wall time (plan §2 telemetry)
    wall.start();

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
    // Analysis degrades by device presence: IMU-only (segmentation + wrist
    // metrics, no pose/shaft), camera-only (pose + shaft + vision-derived phase
    // landmarks, reduced metrics), or fused (both). hasCamera gates the pose/
    // shaft pass; the vision segmentation fallback is adopted below when !hasImu.
    const bool hasCamera = job.faceOnCameraCount > 0 && !job.cameraSources.empty();

    auto detail   = std::make_shared<SwingAnalysis>();

    // ShaftTracker (S3): offline pose + club track over the face-on camera.
    // Heavy (ViTPose per frame) — runs after the cheap IMU stages; failures
    // degrade to empty/invalid tracks, never to a failed analysis. Progress
    // budget: the IMU stages are near-instant, so the pose pass owns 10–70%
    // and the shaft detection scan 70–98% (the assembly tail is cheap).
    if (hasCamera) {
        ShotAnalysisRunnerOptions opt;
        opt.impactUs   = job.impactUs;
        opt.handedness = job.handedness;
        opt.motionCaptureQuality = job.motionCaptureQuality;   // High -> ViTPose++-L (if downloaded)
        opt.tuningOverrides = job.tuningOverrides;              // WB1 pose.crop.* / pose.decode.dark
        // Heavy-stage bounding (v3 G3): scan only the detected swing span
        // (+pad for pass-1 timing error). The shaft detection loop follows
        // pose coverage, so this bounds both heavy stages. conf 0 ⇒ full
        // window, exactly today's behaviour. job.fullWindow (explicit
        // re-analysis) opts out of the bound too — correctness over speed
        // when the user asks for it directly.
        if (segmentation.conf > 0.f && !job.fullWindow) {
            constexpr int64_t kScanPadUs = 150000;
            opt.scanStartUs = segmentation.swingStartUs - kScanPadUs;
            opt.scanEndUs   = segmentation.swingEndUs   + kScanPadUs;
            // v3.4 plan §2: G3's scanStartUs is pinned close to Takeaway, so a
            // real still address sits almost entirely before it — invisible to
            // pose, and therefore to ShaftTracker (strictly bounded to pose
            // coverage). Reach back further, sparsely, so the ball-anchor pass
            // has address-hold frames to work with at all. Sweepable via
            // shaft.addressScanPadUs / shaft.addressStride.
            opt.addressScanPadUs = 4'000'000;   // 4 s default reach, capped by window start
            {
                namespace tn = pinpoint::analysis::tuning;
                tn::apply(job.tuningOverrides, "shaft.addressScanPadUs", opt.addressScanPadUs);
                tn::apply(job.tuningOverrides, "shaft.addressStride",    opt.addressStride);
            }
        } else if (!job.fullWindow) {
            // No IMU-derived span (conf 0 ⇒ camera-only): break the pose/span
            // chicken-and-egg with the two-pass pose (plan §5). A coarse
            // full-window pass finds the swing span, then the dense pass fills
            // only it — subsuming addressScanPadUs (the coarse frames ARE the
            // address-hold coverage). fullWindow still opts out (correctness
            // over speed on explicit re-analysis).
            opt.twoPass = true;
            {
                namespace tn = pinpoint::analysis::tuning;
                tn::apply(job.tuningOverrides, "pose.coarseStride", opt.coarseStride);
                tn::apply(job.tuningOverrides, "pose.densePreMs",   opt.densePreMs);
                tn::apply(job.tuningOverrides, "pose.densePostMs",  opt.densePostMs);
                tn::apply(job.tuningOverrides, "pose.denseStride",  opt.denseStride);
            }
        }
        if (job.progress) {
            job.progress(0.10f);
            opt.progress = [&job](float f) { job.progress(0.10f + 0.60f * f); };
        }
        QElapsedTimer poseWall;
        poseWall.start();
        detail->pose2d = job.poseTrackPath.isEmpty()
                             ? PoseRunner::run(window, job.cameraSources.front(), opt)
                             : PoseRunner::loadFromJson(job.poseTrackPath,
                                                        job.cameraSources.front());
        detail->timings.poseMs = int(poseWall.elapsed());
        if (!detail->pose2d.frames.empty()) {
            // Motion overlay (Phase 2): run the offline non-causal RTS smoother
            // ONCE here on the worker (never at load/paint), caching a companion
            // smoothed track parallel to pose2d.frames. Frame pixel dims come from
            // the face-on camera format — the SAME source PoseRunner / ShaftTracker
            // de-normalize with (window.formatOf(camera) → CameraFormat width/
            // height). When the format is unavailable (e.g. a SwingLab poseTrackPath
            // injection over a source with no camera descriptor) the smoother is
            // skipped — empty vectors degrade gracefully and the "smoothed" block is
            // simply omitted from swing.json (additive-only).
            const pinpoint::FormatDescriptor &fd = window.formatOf(job.cameraSources.front());
            if (const auto *cfmt = std::get_if<pinpoint::CameraFormat>(&fd.format);
                cfmt && cfmt->width > 0 && cfmt->height > 0) {
                PoseSmootherOutput so = smoothPoseTrack(detail->pose2d.frames,
                                                        int(cfmt->width), int(cfmt->height));
                detail->pose2d.smoothed    = std::move(so.smoothed);
                detail->pose2d.smoothedAux = std::move(so.aux);

                // WB4 smoothed-hands grip anchor (pose.gripFromSmoothedHands, dark).
                // Recompute each smoothed frame's leadHand/trailHand/handConf from
                // its SMOOTHED hand keypoints (same centroid math + wrist fallback as
                // pose_runner) so the anchor inherits the smoother's honesty tiers,
                // then mirror it onto the parallel raw frame — ShaftTracker reads
                // pose2d.frames, so this is how it consumes the smoothed grip. OFF ⇒
                // the whole block is skipped and both tracks are byte-identical.
                bool gripFromSmoothed = pinpoint::tuned::pose::grip::kFromSmoothedHands;
                tuning::apply(job.tuningOverrides, "pose.gripFromSmoothedHands", gripFromSmoothed);
                if (gripFromSmoothed
                        && detail->pose2d.smoothed.size() == detail->pose2d.frames.size()) {
                    const bool leftLeads = (job.handedness != 2);
                    for (size_t k = 0; k < detail->pose2d.smoothed.size(); ++k) {
                        PoseFrame2D &sf = detail->pose2d.smoothed[k];
                        computeGripAnchors(sf, leftLeads);
                        detail->pose2d.frames[k].leadHand  = sf.leadHand;
                        detail->pose2d.frames[k].trailHand = sf.trailHand;
                        detail->pose2d.frames[k].handConf  = sf.handConf;
                    }
                }

                // Motion overlay: dense VIZ-tier upsample of the smoothed skeleton
                // (pose_synthesis.h) onto a fixed 240 Hz grid so the replay overlays
                // scrub smoothly — the body sibling of club.synth. Viz-only (nothing
                // reads it for metrics); empty ⇒ pose2d.synth omitted (byte-identical).
                PoseSynthConfig psCfg;
                tuning::apply(job.tuningOverrides, "poseSynth.enabled", psCfg.enabled);
                tuning::apply(job.tuningOverrides, "poseSynth.rateHz",  psCfg.rateHz);
                detail->pose2d.smoothedSynth =
                    synthesizePoseTrack(detail->pose2d.smoothed, psCfg);
            }
            ShotAnalysisJob sub = job;
            if (job.progress)
                sub.progress = [&job](float f) { job.progress(0.70f + 0.28f * f); };
            // v3.4 (design §9, plan §3): resolve the face-on ball track — an
            // explicit injection path wins (SwingLab synthetic fixtures), else
            // whatever the job already carries (live app capture / a recorded
            // swing.json block), else replay the production ball detector
            // offline over this same frozen window (archival swings that
            // predate live ball recording). Empty on every path is a valid,
            // additive-only no-op (design §9.6).
            QElapsedTimer ballWall;
            ballWall.start();
            const BallTrack2D ball = !job.ballTrackPath.isEmpty()
                ? BallRunner::loadFromJson(job.ballTrackPath, job.cameraSources.front())
                : (!job.ballTrack.frames.empty()
                       ? job.ballTrack
                       : BallRunner::run(window, job.cameraSources.front(), detail->pose2d, opt,
                                         job.ballSearchRoi, job.ballBaseline));
            detail->timings.ballMs = int(ballWall.elapsed());
            // Capture the tracker's hands-only phase model only when there is no
            // IMU segmentation to fall back on (the trace is free otherwise).
            ShaftTracker::ShaftTrace strace;
            QElapsedTimer shaftWall;
            shaftWall.start();
            detail->shaft = ShaftTracker::track(window, detail->pose2d, ball, streams,
                                                segmentation, sub, hasImu ? nullptr : &strace);
            detail->timings.shaftMs = int(shaftWall.elapsed());
            // Surface the resolved ball track for the replay overlay (design §9);
            // the same normalized [0,1] track the shaft anchor consumed above.
            detail->ball = ball;
            // Camera-only: adopt the vision-derived segmentation so a webcam-only
            // Wrist analysis still carries Address/Top/Impact/Finish landmarks +
            // swing bounds (vision-grade conf; the UI fades low-confidence ticks).
            if (!hasImu && strace.segmentation.conf > 0.f)
                segmentation = strace.segmentation;
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

    // Bound AFTER the vision-segmentation fallback may have reassigned it.
    const std::vector<PhaseEvent> &phases = segmentation.events;
    detail->series       = series;
    detail->phases       = phases;
    detail->segmentation = segmentation;

    // Head tracking (WB2) — head position is a first-class golf metric. From the
    // smoothed (else raw) face-on pose track: per-frame conf-gated head centre +
    // scale + tilt, then Address-referenced sway/lift/tilt series appended to the
    // DETAIL only (swing.json/charts/data-viewer) — deliberately not into the local
    // `series` above, so the wrist carousel/resemblance scorer/trace are untouched.
    // Camera-only path too (needs only body head keypoints nose/eyes/ears). UNSCORED:
    // no corpus reference bands yet. Sway/lift are emitted in MILLIMETRES (address =
    // 0) via a head-plane px→mm scale: the robust inter-ear pixel distance at address
    // (head.addrScalePx) over a nominal inter-ear breadth (head.earWidthMm). That
    // ruler sits at the head's OWN depth — the right scale for a head metric — and
    // being address-measured it needs no camera calibration. Scale-source priority
    // for later: 2D camera calibration → inter-ear (here) → athlete height → club
    // length. Falls back to ×frame units when no ear/eye keypoint ever resolves.
    if (hasCamera && !detail->pose2d.frames.empty()) {
        const pinpoint::FormatDescriptor &fd = window.formatOf(job.cameraSources.front());
        if (const auto *cfmt = std::get_if<pinpoint::CameraFormat>(&fd.format);
            cfmt && cfmt->width > 0 && cfmt->height > 0) {
            int64_t addressUs = -1;
            if (segmentation.conf > 0.f)
                if (const PhaseEvent *a = segmentation.eventFor(Phase::Address))
                    addressUs = a->t_us;
            const HeadTrackConfig hcfg = HeadTrackConfig::fromOverrides(job.tuningOverrides);
            const HeadTrackResult head =
                trackHead(detail->pose2d, int(cfmt->width), int(cfmt->height), addressUs, hcfg);
            // Head-plane px→mm from the inter-ear ruler (calibration/height/club
            // override this later); ≤ 0 ⇒ buildHeadSeries stays in ×frame units.
            const double pxPerMm = (head.addrScalePx > 0.0 && hcfg.earWidthMm > 0.0)
                                       ? head.addrScalePx / hcfg.earWidthMm : -1.0;
            for (const MetricSeries &m : buildHeadSeries(head, phases, pxPerMm))
                detail->series.push_back(m);
        }
    }

    // Setup + footwork metrics (WB3, wholebody_pose_design.md §2.1/§5) — stance
    // width, per-foot flare, toe-line angle (address) + the lead-heel-lift
    // trace. Same shape as the head-tracking block above: DETAIL-only (never
    // the local `series` the resemblance scorer/carousel/trace read), camera-
    // only path included (needs only foot keypoints), UNSCORED. Lead foot
    // follows the same handedness convention already used for the lead arm
    // (pose_runner.cpp / metric_extractor.cpp): left foot leads unless the
    // golfer is left-handed (handedness == 2).
    if (hasCamera && !detail->pose2d.frames.empty()) {
        const pinpoint::FormatDescriptor &fd = window.formatOf(job.cameraSources.front());
        if (const auto *cfmt = std::get_if<pinpoint::CameraFormat>(&fd.format);
            cfmt && cfmt->width > 0 && cfmt->height > 0) {
            int64_t addressUs = -1;
            if (segmentation.conf > 0.f)
                if (const PhaseEvent *a = segmentation.eventFor(Phase::Address))
                    addressUs = a->t_us;
            const bool leadIsLeft = (job.handedness != 2);
            const FootMetricsResult feet =
                trackFeet(detail->pose2d, int(cfmt->width), int(cfmt->height), leadIsLeft,
                         addressUs, FootMetricsConfig::fromOverrides(job.tuningOverrides));
            for (const MetricSeries &m : buildFootSeries(feet, phases))
                detail->series.push_back(m);
        }
    }

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
    // Wrist estimand = per-archetype resemblance (design §B.0a). SwingScorer's impact-only
    // one-sided model is retired here; it remains the Swing/GRF adherence scorer.
    detail->score  = WristResemblanceScorer::score(series, job.tuningOverrides);
    // Measurement-uncertainty interval (§B.7) on the RESEMBLANCE value (max R_p): propagate the
    // per-cell FE error budget through the winning pattern. Low confidence widens it; the central
    // value is unchanged. NOTE this brackets `overall` only while `overall` IS that resemblance
    // value — i.e. when the penalty-based assessment headline below does not run. The moment the
    // assessment score takes over the headline the interval is cleared (see below), so the
    // ScoreBreakdown invariant "interval brackets overall" always holds.
    detail->score.interval = ScoreUncertainty::wristInterval(detail->score, series, phases,
                                                             job.tuningOverrides);

    // Filter-quality objective (only when re-fusion drove the orientation): the impact-continuity
    // diagnostic gives filter.* an IMU-only score.py check, independent of a vision shaft track.
    if (doRefuse && hasImu)
        detail->filterImpactStepDeg = impactContinuityDeg(streams, job.impactUs);

    // Wrist faults/strengths = the AI-COACH feedback feed (design §B.0): the headline
    // is now the assessment score (WP-4); resemblance values are a descriptive style diagnostic.
    // runAssessment is now ON for the live Wrist pipeline (ShotProcessor) so swing.json always
    // carries the coach feed, and stays the SwingLab observability hook for sampler.*/rules.*/bands.*.
    // Bands stay neutral (archetype 0): the resemblance scorer owns archetype classification now,
    // and a fixed fault reference avoids the archetype-shift sensitivity loss (validation C2).
    if (job.runAssessment && hasImu && !series.empty()) {
        const InMemoryWristAngleSource src = buildWristAngleSource(detail->series, detail->phases);
        const auto provider = makeReferenceBandProvider(BandProviderKind::Archetype);
        const WristAssessmentConfig acfg = wristAssessmentConfigFor(job.tuningOverrides);
        const PpWristAssessmentResult ar = WristAssessmentEngine::assess(src, *provider, acfg);
        detail->findings        = ar.findings;
        detail->assessmentScore = ar.score.total;
        
        // Headline overall score is now the penalty-based assessment score (0-100).
        detail->score.overall   = ar.score.total;
        // The §B.7 interval computed above brackets the resemblance value, NOT this penalty-based
        // score — attaching it here would show a ± band that belongs to a different quantity (and
        // would fail score.py's `0 ≤ lo ≤ overall ≤ hi` check). The assessment score's own
        // measurement-uncertainty model (propagating finding confidence rather than the FE z-score
        // budget) is deferred, so clear the interval until that model exists.
        detail->score.interval  = ScoreInterval{};

        ppInfo() << "[WristAnalysis] assessment:" << detail->findings.size() << "findings, score v2"
                 << detail->assessmentScore;
    }

    // WB4 IMU-less pose wrist assessment (pose.wristAngles.enabled, dark). Runs
    // ONLY when NO IMU wrist source exists (!hasImu) and the camera pose track is
    // present — the IMU assessment path above stays untouched and primary. Builds
    // APPARENT camera-plane lead-wrist angles (PoseWristAngleSource) and drives the
    // same assessment engine, so an IMU-less (webcam-only) swing still gets the
    // coach feed + score. OFF (the default) ⇒ the source is never constructed and
    // findings/assessmentScore/score stay exactly as above (byte-identical).
    const PoseWristAngleConfig poseWristCfg =
        PoseWristAngleConfig::fromOverrides(job.tuningOverrides);
    if (poseWristCfg.enabled && !hasImu && job.runAssessment && hasCamera
            && !detail->pose2d.frames.empty()) {
        const pinpoint::FormatDescriptor &fd = window.formatOf(job.cameraSources.front());
        if (const auto *cfmt = std::get_if<pinpoint::CameraFormat>(&fd.format);
            cfmt && cfmt->width > 0 && cfmt->height > 0) {
            const PoseWristAngleSource src(detail->pose2d, detail->phases, job.handedness,
                                           int(cfmt->width), int(cfmt->height), poseWristCfg);
            const auto provider = makeReferenceBandProvider(BandProviderKind::Archetype);
            const WristAssessmentConfig acfg = wristAssessmentConfigFor(job.tuningOverrides);
            const PpWristAssessmentResult ar = WristAssessmentEngine::assess(src, *provider, acfg);
            detail->findings        = ar.findings;
            detail->assessmentScore = ar.score.total;
            detail->score.overall   = ar.score.total;
            detail->score.interval  = ScoreInterval{};   // apparent-angle proxy: no §B.7 interval
            ppInfo() << "[WristAnalysis] pose (IMU-less) assessment:" << detail->findings.size()
                     << "findings, score v2" << detail->assessmentScore;
        }
    }

    r.metrics     = buildMetricsMap(series);
    r.tracePoints = buildTrace(series, phases);
    r.score       = detail->score.overall;
    detail->timings.totalMs = int(wall.elapsed());
    r.detail      = detail;
    r.ok          = true;

    ppInfo() << "[WristAnalysis]" << series.size() << "metrics, score" << r.score
             << "— grid" << static_cast<qint64>(streams.timeGrid.size());
    return r;
}
