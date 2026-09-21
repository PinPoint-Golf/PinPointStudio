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
#include "analysis_profiling.h"
#include "analysis_tuning.h"
#include "ball_position.h"
#include "ball_runner.h"
#include "body_rotation.h"
#include "impact_runner.h"
#include "club_delivery.h"
#include "dtl_face_on_witness.h"
#include "dtl_shaft_config.h"
#include "dtl_shaft_json.h"
#include "shaft_fusion_json.h"   // shaftFusionConfigFromOverrides (ShaftFusionStage)
#include "dtl_posture.h"         // buildDtlPosture (DtlPostureStage)
#include "dtl_shaft_tracker.h"
#include "event_refine.h"
#include "foot_metrics.h"
#include "upper_body_metrics.h"
#include "hand_axis.h"
#include "head_track.h"
#include "lower_body_metrics.h"
#include "pose_wrist_angle_source.h"
#include "imu_vision_fuser.h"
#include "orientation_refuse_tuning.h"
#include "metric_extractor.h"
#include "kinematic_series.h"
#include "phase_segmenter.h"
#include "positions_ladder.h"
#include "pose_runner.h"
#include "pose_smoother.h"
#include "stream_trim.h"
#include "pose_synthesis.h"
#include "segment_rates.h"
#include "shaft_plane.h"
#include "shaft_tracker.h"
#include "tempo_metrics.h"
#include "timeline_fusion.h"
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
    // THE INSTRUMENT'S ERROR, measured rather than assumed. Against 39 hand-marked P7 frames
    // (2026-09-14; four sessions, two clubs) the tracker's impact lean overshoots the grip-to-head
    // chord by a median +12° with an sd of 9.5° — and the overshoot is NOT a timing offset (the
    // implied offset scatters 0-20 ms, and one session reads MORE lean 15 ms after impact, when
    // physics says less), so it cannot be corrected here by shifting the sample. σ carries the
    // spread so no corridor grades this narrower than the instrument; the BIAS is deliberately not
    // subtracted — a producer that quietly corrects itself hides the fault that needs fixing
    // (candidates: the 6.5 ms exposure smear at impact, the P7 emission geometry).
    m.sigma = 9.5;

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

// Instrument ladder: a HackMotion swing publishes `hm.<key>` and never the bare key.
// See swing_analysis.h's findSeriesByLadder for why this is not four separate loops.
const MetricSeries *find(const std::vector<MetricSeries> &v, const QString &key)
{
    return findSeriesByLadder(v, key);
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

// trimStreams() moved to stream_trim.h so it can be tested: it was a field-by-field
// copy in here, unreachable from any test, and it silently dropped SegmentStream's
// `hackMotion` the day that field was added. See the note in that header.

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

// ── Stage-pipeline implementation (analysis_pipeline_fusion_architecture §10) ──
//
// WristAnalyzer::analyze() runs the Wrist profile (wristProfile() below) as a
// capability-gated stage list over a shared AnalysisContext (constrained blackboard):
// each stage wraps one block of the analysis, stage order is the analysis block order,
// and the file-local helpers above serve the stages.
//
// Five ordering invariants (proposal §10.5) are enforced structurally: the LOCAL
// `series` (scorer/metrics/trace) and `detail->series` are distinct context homes
// (ctx.series vs ctx.detail->series); the segmentation triple ctx.segImu/segVision/
// seg keeps pre- vs post-adoption readers honest; RequireProducts halts via
// ctx.halted so stages 11–17 never run in the fail case; the two nested progress-
// rescale lambdas capture ctx.job (a stack-local of analyze()); the per-stage timers
// wrap the individual heavy calls.

// 1. IMU resample/fusion. Runs iff the job bound at least one IMU; when the bindings
//    are unfusable the fused streams come back empty and hasImuStreams() is false, so
//    the analysis degrades to the camera-only (pose) path.
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
        // ⚠ THE GRID FOLLOWS THE DATA (design §4.3). Floored at what this stage
        // used to hardcode, so a capture with no high-rate lane is bit-identical
        // to what it produced before; raised only by a lane that actually
        // carries more.
        const double gridHz =
            ImuVisionFuser::gridHzForWindow(*ctx.window, ctx.job.imuBindings);
        ctx.streams = ImuVisionFuser::fuse(*ctx.window, ctx.job.imuBindings, gridHz,
                                           ctx.doRefuse ? &refusion : nullptr);
    }
};

// 2. Phase segmentation — IMU-derived, so it needs fused streams.
struct ImuSegmentationStage : AnalysisStage {
    QString name() const override { return QStringLiteral("ImuSegmentation"); }
    bool canRun(const AnalysisContext &ctx) const override { return ctx.hasImuStreams(); }
    void run(AnalysisContext &ctx) override
    {
        ctx.segImu = PhaseSegmenter::segment(ctx.streams, ctx.job.impactUs,
                                             segConfigFor(ctx.job.tuningOverrides));
    }
};

// 3. Wrist metrics. ctx.series is empty here (first writer). Metric grids span
//    address → finish: hand the extractor a trimmed copy when the bounds are real;
//    shaft qHand sampling keeps the full streams.
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

// 4. Offline pose pass. Heavy (ViTPose per frame) — runs after
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
        // Pre-adoption segmentation drives the scan bounds — the vision-segmentation
        // fallback hasn't been adopted yet.
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
        if (!ctx.job.posePreloaded.frames.empty()) {
            ctx.detail->pose2d = ctx.job.posePreloaded;            // version-gated reuse (analysis_versions.h)
            ctx.detail->pose2d.camera = ctx.job.cameraSources.front();
        } else {
            ctx.detail->pose2d = ctx.job.poseTrackPath.isEmpty()
                                     ? PoseRunner::run(*ctx.window, ctx.job.cameraSources.front(), opt)
                                     : PoseRunner::loadFromJson(ctx.job.poseTrackPath,
                                                                ctx.job.cameraSources.front());
        }
        ctx.detail->timings.poseMs = int(poseWall.elapsed());
        ctx.detail->versions.pose      = kPoseStageVersion;
        ctx.detail->versions.poseModel = PoseRunner::modelIdentity(ctx.job.motionCaptureQuality);
        ctx.detail->versions.poseScope = ctx.job.fullWindow ? QStringLiteral("full") : QStringLiteral("span");
        ctx.runnerOpt = std::move(opt);
    }
};

// 5. Motion-overlay pose smoother. runnerOpt present ≡ hasCamera, so the gate is the
//    `!frames.empty()` check under hasCamera.
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
            // Smoother-window keys (metric_presentation_honesty.md §5.4): the phase-4
            // legs group (poseSmooth.legsSigmaScale / poseSmooth.legsJerkScale, both
            // still 1.0 — NOT promoted) and the phase-5 motion-adaptive window
            // (poseSmooth.adapt.mode / .group / .minScale / .aRefPxS2 / .expo / .leadMs /
            // .innovRef / .innovRun). fromOverrides applies exactly those keys and nothing
            // else. ⚠ adapt.mode ships "accel" since 2026-09-05 (promoted on the C15 gate),
            // so the legs keypoints 11–16 DO get a per-frame window here and the hip-derived
            // series move; `poseSmooth.adapt.mode=off` is the parity switch that reproduces
            // the pre-phase-5 output exactly (no scale vector is built at all).
            const PoseSmootherConfig smCfg =
                PoseSmootherConfig::fromOverrides(ctx.job.tuningOverrides);
            PoseSmootherOutput so = smoothPoseTrack(ctx.detail->pose2d.frames,
                                                    int(cfmt->width), int(cfmt->height), smCfg);
            ctx.detail->pose2d.smoothed    = std::move(so.smoothed);
            ctx.detail->pose2d.smoothedAux = std::move(so.aux);
            // Phase-5 divergence guard (pose_smoother.cpp): how many keypoints had their
            // adaptive pass rejected because it would have changed the segmentation. 0
            // with the window off, and the C15 gate must see a non-zero count — hence it
            // rides to swing.json rather than staying a local.
            ctx.detail->pose2d.adaptFallbacks = so.adaptFallbacks;

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

// 6. Ball-track resolve. v3.4 §9: an explicit injection path wins
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
        ctx.ball = !ctx.job.ballPreloaded.frames.empty()
            ? ctx.job.ballPreloaded                                  // version-gated reuse (analysis_versions.h)
            : !ctx.job.ballTrackPath.isEmpty()
            ? BallRunner::loadFromJson(ctx.job.ballTrackPath, ctx.job.cameraSources.front())
            : (!ctx.job.ballTrack.frames.empty()
                   ? ctx.job.ballTrack
                   : BallRunner::run(*ctx.window, ctx.job.cameraSources.front(), ctx.detail->pose2d,
                                     *ctx.runnerOpt, ctx.job.ballSearchRoi, ctx.job.ballBaseline));
        ctx.detail->timings.ballMs = int(ballWall.elapsed());
        ctx.detail->versions.ball = kBallStageVersion;
    }
};

// 6b. Impact camera track (impact_camera_design.md §7, §10.3): the ball and the
//     club in the impact clip, and the club's path. Gated on the DATA — a
//     window holding frames from a camera in the Impact placement — never on
//     the session or on the face-on stages (it needs no pose). Empty is a
//     valid no-op: the overlay draws nothing and the doc carries no block.
struct ImpactStage : AnalysisStage {
    QString name() const override { return QStringLiteral("Impact"); }
    bool canRun(const AnalysisContext &ctx) const override
    {
        return ctx.window && ctx.job.impactSource != pinpoint::kInvalidSourceId
            && !ctx.window->entriesFor(ctx.job.impactSource).empty();
    }
    void run(AnalysisContext &ctx) override
    {
        QElapsedTimer wallImpact;
        wallImpact.start();
        ctx.detail->impact = ImpactRunner::run(*ctx.window, ctx.job.impactSource, ctx.job.impactUs);
        ctx.detail->timings.impactMs = int(wallImpact.elapsed());
        ctx.detail->versions.impact = kImpactStageVersion;
    }
};

// 7. Shaft track. The `sub` job copy (only the shaft tracker consumes it) rescales
//    progress to 70-98%. The 5th arg is the PRE-adoption segmentation; the vision
//    phase model is captured for SegResolve but only when there is no IMU
//    segmentation to fall back on.
struct ShaftStage : AnalysisStage {
    QString name() const override { return QStringLiteral("Shaft"); }
    bool canRun(const AnalysisContext &ctx) const override { return ctx.ball.has_value(); }
    void run(AnalysisContext &ctx) override
    {
        ShotAnalysisJob sub = ctx.job;
        if (ctx.job.progress)
            sub.progress = [&job = ctx.job](float f) { job.progress(0.70f + 0.28f * f); };
        // Capture the tracker's hands-only phase model only when there is no IMU
        // segmentation to fall back on (the trace is free otherwise). The decide trace is
        // ALSO wanted when a down-the-line tracker will run: its tier column is the face-on
        // witness's (dtl_face_on_witness.h). The trace is meant as a write-only sink, but that
        // has only ever been exercised on camera-only swings (which always traced), so an IMU
        // swing without a DTL camera still passes none and its path is unchanged by
        // construction. An IMU swing WITH one now traces — no such swing exists yet to gate on.
        const bool wantWitness = ctx.job.dtlSource != pinpoint::kInvalidSourceId
            && DtlShaftConfig::fromOverrides(ctx.job.tuningOverrides).enabled;
        ShaftTracker::ShaftTrace strace;
        QElapsedTimer shaftWall;
        shaftWall.start();
        if (!ctx.job.shaftPreloaded.samples.empty()) {
            // Version-gated reuse (analysis_versions.h, recorded_products.h): the recorded
            // samples, anchors, lengths and plane fit come back as written; only the Layer C
            // visualisation tier is rebuilt, from those samples, so it follows the current
            // rule (the grip is the hands). The recorded ladder rides along as the vision
            // segmentation and the ladder stages hand it through untouched.
            ctx.detail->shaft = ctx.job.shaftPreloaded;
            ctx.detail->shaft.camera = ctx.job.cameraSources.front();
            const ShaftV3Config v3 = ShaftV3Config::fromOverrides(ctx.job.tuningOverrides);
            // The follow-through plausibility pass the live tracker runs before locating its
            // anchors (shaft_track_assembly.h) — applied here too, so a document written before
            // the pass existed gets the same rule. The forearm is the lead elbow→hands line from
            // the recorded pose at each sample's instant, the same construction the tracker uses.
            {
                const PoseTrack2D &pose = ctx.detail->pose2d;
                const int leadElbow = (ctx.job.handedness == 2) ? 8 : 7;
                std::vector<double> forearm(ctx.detail->shaft.samples.size(),
                                            std::numeric_limits<double>::quiet_NaN());
                // The reloaded track carries the camera frame it was measured in; the pose is
                // normalised, so that is the scale the two are compared at.
                const double fw = ctx.detail->shaft.frameWidth, fh = ctx.detail->shaft.frameHeight;
                if (!pose.frames.empty() && fw > 0 && fh > 0) {
                    size_t pi = 0;
                    for (size_t k = 0; k < forearm.size(); ++k) {
                        const int64_t t = ctx.detail->shaft.samples[k].t_us;
                        while (pi + 1 < pose.frames.size() && pose.frames[pi + 1].t_us <= t) ++pi;
                        const PoseFrame2D &pf = pose.frames[pi];
                        if (pf.conf[size_t(leadElbow)] <= 0.30f) continue;
                        const double grx = 0.5 * (pf.leadHand.x() + pf.trailHand.x()) * fw;
                        const double gry = 0.5 * (pf.leadHand.y() + pf.trailHand.y()) * fh;
                        const double ex = pf.kp[size_t(leadElbow)].x() * fw, ey = pf.kp[size_t(leadElbow)].y() * fh;
                        if (std::hypot(ex - grx, ey - gry) < 8.0) continue;
                        forearm[k] = std::atan2(ey - gry, ex - grx) * 180.0 / 3.14159265358979323846;
                    }
                }
                const std::optional<int64_t> impactP = phaseTimeOpt(ctx.job.ladderPreloaded ? ctx.job.ladderPreloaded->events
                                                                                              : ctx.seg.events, Phase::Impact);
                const int64_t impactUs = impactP ? *impactP : ctx.job.impactUs;
                const int demoted = demoteImplausibleFollowThrough(ctx.detail->shaft, forearm, impactUs, v3);
                if (demoted > 0)
                    ppInfo() << "[WristAnalysis] shaft: follow-through plausibility demoted" << demoted << "reused samples";
            }
            resynthesizeLayerC(ctx.detail->shaft, v3);
            if (!ctx.hasImuStreams() && ctx.job.ladderPreloaded)
                ctx.segVision = *ctx.job.ladderPreloaded;
            ppInfo() << "[WristAnalysis] shaft: recorded track reused —"
                     << ctx.detail->shaft.samples.size() << "samples,"
                     << ctx.detail->shaft.positions.size() << "positions, synth"
                     << ctx.detail->shaft.synth.size() << "ticks";
            // No decide run ⇒ no trace: the witness's tiers come from the sample flags.
            if (wantWitness)
                ctx.foWitness = std::make_shared<const FaceOnWitness>(
                    buildFaceOnWitness(ctx.detail->shaft, nullptr, ctx.job.impactUs));
        } else {
            const bool traced = !ctx.hasImuStreams() || wantWitness;
            ctx.detail->shaft = ShaftTracker::track(*ctx.window, ctx.detail->pose2d, *ctx.ball,
                                                    ctx.streams, ctx.segImu.value_or(Segmentation{}),
                                                    sub, traced ? &strace : nullptr);
            if (!ctx.hasImuStreams())
                ctx.segVision = strace.segmentation;
            // Built HERE, off the tracker's own output and its own trace, before the ladder
            // stages rewrite positions — the pair SwingLab's --dtl block builds from.
            if (wantWitness)
                ctx.foWitness = std::make_shared<const FaceOnWitness>(
                    buildFaceOnWitness(ctx.detail->shaft, &strace, ctx.job.impactUs));
        }
        ctx.detail->timings.shaftMs = int(shaftWall.elapsed());
        ctx.detail->versions.shaft = kShaftStageVersion;
        // Surface the resolved ball track for the replay overlay (design §9).
        ctx.detail->ball = *ctx.ball;
    }
};

// 8. Segmentation resolve — the mutable-local semantics: keep the
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

// 9. Shaft-lean series — appended to the LOCAL series after the
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

// 10. Require at least one usable product — IMU-derived wrist
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

// 10b. EventRefine (P3 event fusion) — fine-tune the timeline events users see
//     from the FINISHED shaft/ball products (event_refine.h), slotted after
//     RequireProducts so halted contexts skip it free and BEFORE BindDetail so the
//     refined ctx.seg binds with zero extra plumbing (every downstream consumer —
//     HeadTrack/FootMetrics addressUs, assessment P1, buildTrace, swing.json,
//     timeline — picks the refined times up automatically). canRun gates on "the
//     ladder we'd mutate is the VISION one" (no IMU segmentation — fused-swing
//     refine is documented future work, cross-ref ball_anchor.cpp's tk0 TODO) plus
//     a valid shaft product and real vision conf. The refine.enabled gate lives in
//     canRun too (mirroring PoseAssessmentStage): dark ⇒ the stage is SKIPPED, not
//     a no-op run, so ctx.seg is code-path-identical when off.
struct EventRefineStage : AnalysisStage {
    QString name() const override { return QStringLiteral("EventRefine"); }
    bool canRun(const AnalysisContext &ctx) const override
    {
        return EventRefineConfig::fromOverrides(ctx.job.tuningOverrides).enabled
            && !ctx.job.ladderPreloaded   // a reused ladder is already refined (recorded_products.h)
            && !ctx.segImu.has_value() && ctx.caps.hasCamera(CameraPlacement::FaceOn)
            && ctx.detail->shaft.valid && ctx.seg.conf > 0.f;
    }
    void run(AnalysisContext &ctx) override
    {
        const EventRefineConfig cfg = EventRefineConfig::fromOverrides(ctx.job.tuningOverrides);
        // Impact is NEVER refined (marker contract; all truth swings acoustic-
        // anchored). refine.impactResidual is log-only P6 telemetry (launch −
        // impact); the job.impactUs < 0 legitimate-refine path is out of scope for
        // V1. P2/P3/P5/P6/P8 ladder promotion lives in PositionsLadderStage (10c)
        // behind the refine.positionsLadder key.
        const EventRefineResult r = refineEvents(ctx.seg, ctx.detail->shaft, ctx.detail->ball,
                                                 ctx.job.impactUs, cfg);
        if (r.impactResidualValid)
            ppInfo() << "[WristAnalysis] refine impactResidual (launch−impact) us"
                     << qint64(r.impactResidualUs);
        if (r.refined)
            ppInfo() << "[WristAnalysis] refine → version 3:"
                     << (r.takeawayRefined ? "Takeaway" : "-") << (r.addressRefined ? "Address" : "-")
                     << (r.p1Synced ? "+P1" : "-") << "conf" << r.conf << "tier" << r.tier
                     << "L" << r.departFrame;
    }
};

// 10c. PositionsLadder — promote the club track's located P-positions
//     (detail->shaft.positions[], Layer B) into ctx.seg PhaseEvents
//     (positions_ladder.h holds the mapping + insert-if-monotone contract).
//     Slotted after EventRefine so candidates measure against the REFINED
//     anchors, and before BindDetail so the appended events bind/persist with
//     zero extra plumbing. canRun gates on available DATA only (positions
//     present, a resolved ladder present) — never on session type or IMU
//     presence: on an IMU ladder the pass simply skips phases the segmenter
//     already emitted (duplicate ⇒ IMU proxy wins, no arbitration). The
//     refine.positionsLadder gate lives in canRun (mirroring EventRefineStage):
//     dark ⇒ the stage is SKIPPED, not a no-op run, so ctx.seg is
//     code-path-identical when off.
//
//     SUPERSEDED BY TimelineFusionStage when refine.fusion is on — the two are
//     mutually exclusive by canRun (insertion into an empty slot is just
//     arbitration against an absent incumbent, so the ladder's behaviour is a
//     strict subset of fusion's). This stage retires when that flag freezes ON.
struct PositionsLadderStage : AnalysisStage {
    QString name() const override { return QStringLiteral("PositionsLadder"); }
    bool canRun(const AnalysisContext &ctx) const override
    {
        return PositionsLadderConfig::fromOverrides(ctx.job.tuningOverrides).enabled
            && !ctx.job.ladderPreloaded   // a reused ladder already carries its P-positions
            && !TimelineFusionConfig::fromOverrides(ctx.job.tuningOverrides).enabled
            && !ctx.detail->shaft.positions.empty() && !ctx.seg.events.empty();
    }
    void run(AnalysisContext &ctx) override
    {
        const PositionsLadderResult r =
            emitPositionsLadder(ctx.seg, ctx.detail->shaft.positions);
        ppInfo() << "[WristAnalysis] positionsLadder → version" << ctx.seg.version
                 << "inserted" << r.inserted << "abstained" << r.abstained
                 << "duplicate" << r.duplicate;
    }
};

// 10c′. TimelineFusion — the same pipeline slot, arbitrating instead of
//     back-filling (timeline_fusion.h; docs/design/timeline-fusion.md). Takes
//     PositionsLadderStage's place when refine.fusion is on, so exactly one of
//     the two ever mutates ctx.seg. Same gating discipline: DATA only (positions
//     present, a resolved ladder present) plus the flag, and dark ⇒ SKIPPED, not
//     a no-op run, so ctx.seg is code-path-identical with fusion off — the
//     parity baseline the corpus gate measures against.
struct TimelineFusionStage : AnalysisStage {
    QString name() const override { return QStringLiteral("TimelineFusion"); }
    bool canRun(const AnalysisContext &ctx) const override
    {
        return TimelineFusionConfig::fromOverrides(ctx.job.tuningOverrides).enabled
            && !ctx.job.ladderPreloaded   // a reused ladder is already fused (its decisions rode along)
            && !ctx.detail->shaft.positions.empty() && !ctx.seg.events.empty();
    }
    void run(AnalysisContext &ctx) override
    {
        const TimelineFusionConfig cfg =
            TimelineFusionConfig::fromOverrides(ctx.job.tuningOverrides);
        const TimelineFusionResult r =
            fuseTimeline(ctx.seg, ctx.detail->shaft.positions, cfg);
        ppInfo() << "[WristAnalysis] timelineFusion → version" << ctx.seg.version
                 << "inserted" << r.inserted << "replaced" << r.replaced
                 << "retained" << r.retained << "disputed" << r.disputed
                 << "abstained" << r.abstained;
        // One line per slot that actually FLIPPED — the retentions are persisted
        // (seg.fusion) rather than logged, since they are calibration data, not
        // news. Δ is winner − loser in ms.
        for (const FusionDecision &d : r.decisions)
            if (d.reason == FusionReason::ClassBeat || d.reason == FusionReason::OwnerBeat)
                ppInfo() << "[WristAnalysis]   fusion phase" << int(d.phase)
                         << "→" << segmentRoleName(d.winner)
                         << "over" << segmentRoleName(d.loser)
                         << "Δms" << qint64(d.deltaUs / 1000)
                         << "reason" << int(d.reason);
    }
};

// 11. Bind the local products onto the detail. detail->phases is
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

// 12. Head tracking — Address-referenced sway/lift/tilt appended
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

// 13. Setup + footwork metrics — stance width, per-foot flare, toe-line angle +
//     lead-heel-lift trace, PLUS ball-position-along-the-stance. DETAIL series
//     only, UNSCORED.
//
//     Ball position lives here rather than in its own stage because it needs the
//     address heel pair AND the stance width as its denominator — both already
//     computed by trackFeet over a resolved address reference. A separate stage
//     would have to redo that reference resolution to say the same thing. The
//     ball track it reads is written by BallStage (6) / ShaftStage (7), long
//     before this stage runs, so no reordering is involved.
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

            // Ball position + the ball-diameter px→mm ruler. The ruler resolves
            // independently of the heel geometry, so a swing whose feet are
            // unusable can still put stance width in mm — and vice versa.
            const BallPositionResult bp =
                computeBallPosition(ctx.detail->ball, feet.setup.leadHeelPxAddr,
                                    feet.setup.trailHeelPxAddr,
                                    feet.setup.heelsValid ? addressUs : -1,
                                    int(cfmt->width), int(cfmt->height),
                                    BallPositionConfig::fromOverrides(ctx.job.tuningOverrides));

            for (const MetricSeries &m : buildFootSeries(feet, phases, bp.mmPerPx))
                ctx.detail->series.push_back(m);

            if (bp.valid && feet.setup.heelsValid) {
                // Anchor instant: the Address event when we have one, else the
                // first posed frame — the same "ultimate fallback" buildFootSeries
                // uses for its own setup scalars, so every address-time reading
                // agrees about when address was.
                const int64_t addrT = addressUs >= 0            ? addressUs
                                    : ctx.detail->pose2d.frames.empty()
                                        ? 0
                                        : ctx.detail->pose2d.frames.front().t_us;
                // Address-time setup scalar — empty curve, one Address
                // phaseSample (the foot_metrics.h representation note).
                MetricSeries m;
                m.key   = QStringLiteral("ballPosition");
                m.label = QStringLiteral("Ball position");
                m.unit  = QStringLiteral("% stance width");
                // 0 % at the LEAD heel, 100 % at the trail heel — the scale other golf software
                // uses, which is why it is not the lead-positive convention the displacement
                // metrics follow. See docs/design/pinpoint_sign_conventions.md: where a number is
                // read alongside numbers we did not produce, the established convention wins.
                //
                //     lead heel  frac 0.0  ->    0 %       (a driver sits about here)
                //     centre     frac 0.5  ->   50 %       (a wedge sits about here)
                //     trail heel frac 1.0  ->  100 %
                //
                // Unclamped: forward of the lead heel is a real driver setup and reads BELOW 0 %.
                m.phaseSamples.push_back({ Phase::Address, addrT,
                                           bp.fracOfStance * 100.0, QString() });
                ctx.detail->series.push_back(std::move(m));
            }
        }
    }
};

// 13b. Lower-body frontal-plane metrics — lead-knee drift, pelvis sway, pelvis
//      lift, hip-line tilt. DETAIL series only, UNSCORED.
//
//      Separate from FootMetricsStage even though both are lower-body and both
//      are face-on, because they do not read the same thing and do not fail
//      together: the feet need the WholeBody tail (17–22), this needs only COCO
//      body 11–16. A legacy 17-kp track produces nothing at all from the feet and
//      all four of these — folding them into one stage would tie the wider
//      availability to the narrower one for no reason but tidiness.
//
//      Runs after BindDetail (12) for the same reason the tempo and foot stages
//      do: detail->series is assigned wholesale there, so anything appended
//      before it is overwritten.
struct LowerBodyMetricsStage : AnalysisStage {
    QString name() const override { return QStringLiteral("LowerBodyMetrics"); }
    bool canRun(const AnalysisContext &ctx) const override
    {
        return ctx.caps.hasCamera(CameraPlacement::FaceOn) && !ctx.detail->pose2d.frames.empty();
    }
    void run(AnalysisContext &ctx) override
    {
        const std::vector<PhaseEvent> &phases = ctx.seg.events;
        const pinpoint::FormatDescriptor &fd = ctx.window->formatOf(ctx.job.cameraSources.front());
        const auto *cfmt = std::get_if<pinpoint::CameraFormat>(&fd.format);
        if (cfmt == nullptr || cfmt->width <= 0 || cfmt->height <= 0)
            return;

        int64_t addressUs = -1;
        if (ctx.seg.conf > 0.f)
            if (const PhaseEvent *a = ctx.seg.eventFor(Phase::Address))
                addressUs = a->t_us;
        const bool leadIsLeft = (ctx.job.handedness != 2);

        // The ball-diameter px->mm ruler, for plumbBobDistance's inches.
        //
        // DEGENERATE HEELS, but the REAL Address instant. The heel pair is genuinely irrelevant —
        // computeBallPosition resolves the centre and the ruler before it gates on the heels, so a
        // hip metric never fails because a foot was occluded. `addressUs` is NOT irrelevant, and
        // passing -1 here (copied from ClubDeliveryStage, which has no address instant to give) cost
        // three corpus swings their reading: with an Address the ruler is built from the samples
        // around it, and without one it falls back to every pre-launch sample, which on a long
        // pre-roll drifts far enough that the cluster filter leaves fewer than `minSamples`. The
        // stage has the instant in hand — FootMetricsStage passes it — so withholding it produced a
        // metric that was absent on swings whose ball was tracked perfectly well.
        const BallPositionResult bp =
            computeBallPosition(ctx.detail->ball, QPointF(), QPointF(), addressUs,
                                int(cfmt->width), int(cfmt->height),
                                BallPositionConfig::fromOverrides(ctx.job.tuningOverrides));

        const LowerBodyResult lb =
            trackLowerBody(ctx.detail->pose2d, int(cfmt->width), int(cfmt->height), leadIsLeft,
                           addressUs, LowerBodyConfig::fromOverrides(ctx.job.tuningOverrides),
                           bp.mmPerPx);
        for (const MetricSeries &m : buildLowerBodySeries(lb, phases))
            ctx.detail->series.push_back(m);
    }
};

// 13c. Upper-body frontal-plane metrics — secondary axis tilt, spine side bend,
//      thorax lateral drift, the shoulder and elbow lines, trail elbow height,
//      swing width, lead-arm connection and the lead-arm-to-torso angle. DETAIL
//      series only, UNSCORED.
//
//      The chest-and-arms counterpart to LowerBodyMetricsStage, and a separate
//      stage for the same reason that one is separate from the feet: they read
//      different keypoints and do not fail together. It reads only COCO body
//      5–16 through the anatomy vocabulary, so like the lower body it answers on
//      a legacy 17-keypoint track.
struct UpperBodyMetricsStage : AnalysisStage {
    QString name() const override { return QStringLiteral("UpperBodyMetrics"); }
    bool canRun(const AnalysisContext &ctx) const override
    {
        return ctx.caps.hasCamera(CameraPlacement::FaceOn) && !ctx.detail->pose2d.frames.empty();
    }
    void run(AnalysisContext &ctx) override
    {
        const pinpoint::FormatDescriptor &fd = ctx.window->formatOf(ctx.job.cameraSources.front());
        const auto *cfmt = std::get_if<pinpoint::CameraFormat>(&fd.format);
        if (cfmt == nullptr || cfmt->width <= 0 || cfmt->height <= 0)
            return;

        int64_t addressUs = -1;
        if (ctx.seg.conf > 0.f)
            if (const PhaseEvent *a = ctx.seg.eventFor(Phase::Address))
                addressUs = a->t_us;
        const UpperBodyResult ub =
            trackUpperBody(ctx.detail->pose2d, int(cfmt->width), int(cfmt->height),
                           ctx.job.handedness != 2, addressUs,
                           UpperBodyConfig::fromOverrides(ctx.job.tuningOverrides));
        for (const MetricSeries &m : buildUpperBodySeries(ub, ctx.seg.events))
            ctx.detail->series.push_back(m);

        // The trail wrist's apparent bow / cup, from the same pose track. It lives with the
        // upper body rather than in its own stage because it shares this stage's inputs exactly
        // and adds no gate of its own beyond the WholeBody hand keypoints, which it checks
        // itself by producing nothing when they are absent.
        for (const MetricSeries &m :
             buildTrailWristSeries(ctx.detail->pose2d, ctx.seg.events, ctx.job.handedness,
                                   int(cfmt->width), int(cfmt->height),
                                   PoseWristAngleConfig::fromOverrides(ctx.job.tuningOverrides)))
            ctx.detail->series.push_back(m);
    }
};

// 13d. Axial body rotation — pelvis turn, thorax turn, X-factor and X-factor
//      stretch. DETAIL series only, UNSCORED.
//
//      The one stage that reads BOTH an IMU and the camera, and prefers whichever
//      it has: a bound Pelvis or Thorax stream is used directly, and where there
//      is none the turn is estimated from the collapse of the hip or shoulder span
//      in the image. canRun therefore admits either input — refusing because the
//      ideal sensor is absent would produce nothing on every swing this product
//      actually records.
struct BodyRotationStage : AnalysisStage {
    QString name() const override { return QStringLiteral("BodyRotation"); }
    bool canRun(const AnalysisContext &ctx) const override
    {
        const bool hasPose = ctx.caps.hasCamera(CameraPlacement::FaceOn)
                             && !ctx.detail->pose2d.frames.empty();
        const bool hasTrunkImu = ctx.streams.streamFor(SegmentRole::Pelvis) != nullptr
                                 || ctx.streams.streamFor(SegmentRole::Thorax) != nullptr;
        return hasPose || hasTrunkImu;
    }
    QString skipReason(const AnalysisContext &) const override
    {
        return QStringLiteral("no face-on pose track and no pelvis/thorax IMU");
    }
    void run(AnalysisContext &ctx) override
    {
        const pinpoint::FormatDescriptor &fd = ctx.window->formatOf(ctx.job.cameraSources.front());
        const auto *cfmt = std::get_if<pinpoint::CameraFormat>(&fd.format);
        const int w = cfmt ? int(cfmt->width) : 0;
        const int h = cfmt ? int(cfmt->height) : 0;

        const BodyRotationResult br =
            trackBodyRotation(ctx.detail->pose2d, ctx.streams, w, h, ctx.job.handedness != 2,
                              ctx.seg.events,
                              BodyRotationConfig::fromOverrides(ctx.job.tuningOverrides));
        for (const MetricSeries &m : buildBodyRotationSeries(br, ctx.seg.events))
            ctx.detail->series.push_back(m);
    }
};

// 13e. Club delivery from the face-on camera — backswing length at the top, attack
//      angle and low point relative to the ball. DETAIL series only, UNSCORED.
//
//      Needs the shaft track WITH a measured clubhead: every reading here is taken
//      from headPx, and a projected head carries the grip's motion rather than the
//      club's. The producer enforces that per sample; canRun only checks that a
//      valid track exists at all.
struct ClubDeliveryStage : AnalysisStage {
    QString name() const override { return QStringLiteral("ClubDelivery"); }
    bool canRun(const AnalysisContext &ctx) const override
    {
        return ctx.detail->shaft.valid && !ctx.detail->shaft.samples.empty();
    }
    QString skipReason(const AnalysisContext &) const override
    {
        return QStringLiteral("no valid shaft track");
    }
    void run(AnalysisContext &ctx) override
    {
        const pinpoint::FormatDescriptor &fd = ctx.window->formatOf(ctx.job.cameraSources.front());
        const auto *cfmt = std::get_if<pinpoint::CameraFormat>(&fd.format);
        if (cfmt == nullptr || cfmt->width <= 0 || cfmt->height <= 0)
            return;

        // The address ball centre and the px→mm ruler, from the same robust pass FootMetricsStage
        // uses. Deliberately called with a degenerate heel pair: this stage does not need the
        // stance geometry, and computeBallPosition resolves the centre and the ruler BEFORE it
        // gates on the heels — the documented "ruler survives, position does not" path. Reaching
        // for the feet here would make a club metric fail because a foot was occluded.
        const BallPositionResult bp =
            computeBallPosition(ctx.detail->ball, QPointF(), QPointF(), -1,
                                int(cfmt->width), int(cfmt->height),
                                BallPositionConfig::fromOverrides(ctx.job.tuningOverrides));
        const bool ballOk = bp.samples > 0 && bp.mmPerPx > 0.0;

        const ClubDeliveryResult cd =
            trackClubDelivery(ctx.detail->shaft, ctx.seg.events, bp.addressBallPx, ballOk,
                              bp.mmPerPx,
                              ClubDeliveryConfig::fromOverrides(ctx.job.tuningOverrides));
        for (const MetricSeries &m : buildClubDeliverySeries(cd, ctx.seg.events))
            ctx.detail->series.push_back(m);
    }
};

// 13a. Tempo — backswing duration (Address→Top) and the tempo ratio
//      ((Top−Address)/(Impact−Top)). DETAIL series only, UNSCORED.
//
//      Segmentation-only: no pose, no club, no IMU, so it works on every capture
//      regime that produced a real ladder. It must sit AFTER EventRefine (11) so
//      the refined Address is what it reads, and AFTER BindDetail (12) which
//      assigns detail->series wholesale — appending before that would be
//      overwritten. Writing to detail->series rather than the local scored series
//      keeps the resemblance score byte-identical (ordering invariant §5.1).
//
//      The engine refuses outright on an untrustworthy ladder (conf gate, or any
//      of Address/Top/Impact missing — the IMU clampFallback path has no Top at
//      all), so canRun and the engine agree: no ladder, no series, never a
//      plausible-looking wrong number.
struct TempoStage : AnalysisStage {
    QString name() const override { return QStringLiteral("Tempo"); }
    bool canRun(const AnalysisContext &ctx) const override
    {
        const TempoConfig cfg = TempoConfig::fromOverrides(ctx.job.tuningOverrides);
        return cfg.enabled && double(ctx.seg.conf) > cfg.minConf
            && ctx.seg.eventFor(Phase::Address) && ctx.seg.eventFor(Phase::Top)
            && ctx.seg.eventFor(Phase::Impact);
    }
    QString skipReason(const AnalysisContext &) const override
    {
        return QStringLiteral("disabled or no confident Address/Top/Impact ladder");
    }
    void run(AnalysisContext &ctx) override
    {
        for (const MetricSeries &m :
             buildTempoSeries(ctx.seg, TempoConfig::fromOverrides(ctx.job.tuningOverrides)))
            ctx.detail->series.push_back(m);
    }
};

// 13b. Club kinematics — clubhead speed / hand speed (mph) + lag angle (°),
//      appended to the DETAIL series only (unscored), mirroring HeadTrack/FootMetrics.
//      Derived purely from the face-on camera products: the shaft track (clubhead/grip
//      px → linear speed, preferring the dense synth channel) and pose (lead forearm vs
//      shaft direction → lag). No IMU input; a curve is omitted, never fabricated, when
//      its product is absent. Lands DARK behind kinematics.enabled (developer guide
//      §6.3): OFF ⇒ skipped ⇒ detail->series byte-identical to the pre-stage pipeline.
//      Runs after RequireProducts, so a halted shot skips it free.
struct KinematicsStage : AnalysisStage {
    QString name() const override { return QStringLiteral("Kinematics"); }
    bool canRun(const AnalysisContext &ctx) const override
    {
        return KinematicSeriesConfig::fromOverrides(ctx.job.tuningOverrides).enabled;
    }
    QString skipReason(const AnalysisContext &) const override
    {
        return QStringLiteral("kinematics disabled (dark)");
    }
    void run(AnalysisContext &ctx) override
    {
        KinematicSeriesInputs in;
        in.shaft       = ctx.detail->shaft.valid ? &ctx.detail->shaft : nullptr;
        in.pose        = ctx.detail->pose2d.frames.empty() ? nullptr : &ctx.detail->pose2d;
        in.impactUs    = ctx.job.impactUs;
        in.handedness  = ctx.job.handedness;
        in.clubLengthM = ctx.job.clubLengthM;
        in.phases      = ctx.seg.events;
        in.composed    = KinematicSeriesConfig::fromOverrides(ctx.job.tuningOverrides).composed;
        in.gripDownM   = ShaftV3Config::fromOverrides(ctx.job.tuningOverrides).lenGripDownM;
        for (MetricSeries &m : buildKinematicSeries(in))
            ctx.detail->series.push_back(std::move(m));
    }
};

// Dark-flag config for the swing-plane stage, mirroring KinematicSeriesConfig.
// Master gate only — the conic gates themselves are NOT tunable: relaxing them is
// a measured mistake (brief §9), not a knob.
struct ShaftPlaneConfig {
    bool enabled = pinpoint::tuned::shaftPlane::kEnabled;   // shaftPlane.enabled — master gate (dark)

    static ShaftPlaneConfig fromOverrides(const QVariantMap &ov)
    {
        using namespace tuning;
        ShaftPlaneConfig c;
        apply(ov, "shaftPlane.enabled", c.enabled);
        return c;
    }
};

// 13c. Face-on swing plane — the transition delta the over_the_top axis has been
//      waiting for (shaft_plane.h; transition_plane_producer_brief.md). Fits a conic
//      to the SHAFT VECTOR (headPx − gripPx) over takeaway→top and top→impact and
//      reports the change in plane inclination between them. EXPERIMENTAL and
//      normless: the measure it feeds is `planned` with no norm row, so it cannot
//      fire a fault — which is exactly what licenses fitting the synth tier as a
//      second channel. Both channels are fitted and tagged on every swing; measured
//      is the headline whenever both its windows fit, synth is the fallback, and a
//      synth-tagged emission carries the measured channel's per-window reject codes
//      so the coverage gap is visible instead of silent.
//
//      GATED ON AVAILABLE DATA, NEVER ON SESSION TYPE (standing convention): a valid
//      shaft track with a frame size, plus takeaway/top/impact on the ladder. Absent
//      any of those it emits nothing and skipReason names WHICH input was missing.
struct ShaftPlaneStage : AnalysisStage {
    QString name() const override { return QStringLiteral("ShaftPlane"); }
    bool canRun(const AnalysisContext &ctx) const override
    {
        if (!ShaftPlaneConfig::fromOverrides(ctx.job.tuningOverrides).enabled) return false;
        const ShaftTrack2D &s = ctx.detail->shaft;
        return s.valid && s.frameWidth > 0 && s.frameHeight > 0 && !s.samples.empty()
            && ctx.seg.eventFor(Phase::Takeaway) && ctx.seg.eventFor(Phase::Top)
            && ctx.seg.eventFor(Phase::Impact);
    }
    QString skipReason(const AnalysisContext &ctx) const override
    {
        if (!ShaftPlaneConfig::fromOverrides(ctx.job.tuningOverrides).enabled)
            return QStringLiteral("shaft plane disabled (dark)");
        const ShaftTrack2D &s = ctx.detail->shaft;
        if (!s.valid)                              return QStringLiteral("no valid shaft track");
        if (s.frameWidth <= 0 || s.frameHeight <= 0) return QStringLiteral("no camera frame size");
        if (s.samples.empty())                     return QStringLiteral("no shaft samples");
        return QStringLiteral("no takeaway/top/impact ladder");
    }
    void run(AnalysisContext &ctx) override
    {
        const ShaftTrack2D &track = ctx.detail->shaft;

        ShaftPlaneInput in;
        // The honest channel: measured heads only. ShaftSynthesized never appears
        // in samples[] by construction, but the flag test is kept as explicit
        // parity with plane_probe.load_run — the selection this is graded against.
        for (const ShaftSample2D &s : track.samples) {
            if (!(s.headConf > 0.f)) continue;
            if (s.flags & ShaftSynthesized) continue;
            in.measured.push_back({ s.t_us, s.headPx.x() - s.gripPx.x(),
                                            s.headPx.y() - s.gripPx.y() });
        }
        // The synth tier carries a decayed conf rather than a head measurement,
        // so no headConf filter applies to it.
        for (const ShaftSample2D &s : track.synth)
            in.synth.push_back({ s.t_us, s.headPx.x() - s.gripPx.x(),
                                         s.headPx.y() - s.gripPx.y() });
        for (const ShaftPosition &p : track.positions)
            in.anchors.push_back({ p.t_us, p.conf });

        const PhaseEvent *tw = ctx.seg.eventFor(Phase::Takeaway);
        const PhaseEvent *tp = ctx.seg.eventFor(Phase::Top);
        const PhaseEvent *im = ctx.seg.eventFor(Phase::Impact);
        in.takeawayUs  = tw->t_us;
        in.topUs       = tp->t_us;
        in.impactUs    = im->t_us;
        in.haveWindows = true;

        const ShaftPlaneResult r = fitShaftPlane(in);

        auto record = [](ShaftPlaneChannel &dst, const PlaneChannelFit &src, bool isSynth) {
            dst.fitted         = src.fitted;
            dst.iotaBackDeg    = src.back.fit.iotaDeg;
            dst.iotaDownDeg    = src.down.fit.iotaDeg;
            dst.deltaDeg       = src.deltaDeg;
            dst.nodeBackDeg    = src.back.fit.nodeDeg;
            dst.nodeDownDeg    = src.down.fit.nodeDeg;
            dst.nBack          = src.back.fit.n;
            dst.nDown          = src.down.fit.n;
            dst.conicResidBack = src.back.fit.conicResid;
            dst.conicResidDown = src.down.fit.conicResid;
            dst.ratioBack      = src.back.fit.ok ? src.back.fit.ratioMinorMajor : -1.0;
            dst.ratioDown      = src.down.fit.ok ? src.down.fit.ratioMinorMajor : -1.0;
            dst.rejectBack     = int(src.back.fit.reject);
            dst.rejectDown     = int(src.down.fit.reject);
            // Channel-appropriate quality only — see ShaftPlaneChannel's comment.
            if (isSynth) {
                dst.anchorsBack   = src.back.anchors;
                dst.anchorsDown   = src.down.anchors;
                dst.anchorConfMin = src.anchorConfMin;
            } else {
                dst.splitHalfBackDeg = src.back.splitHalfDeg;
                dst.splitHalfDownDeg = src.down.splitHalfDeg;
            }
        };
        ShaftPlaneEstimate est;
        est.valid   = r.valid;
        est.channel = int(r.channel);
        record(est.measured, r.measured, false);
        record(est.synth,    r.synth,    true);
        ctx.detail->shaft.plane = est;

        if (!r.valid) {
            ppInfo() << "[WristAnalysis] shaft plane: no channel fitted — measured back/down"
                     << conicRejectName(r.measured.back.fit.reject)
                     << conicRejectName(r.measured.down.fit.reject) << "synth back/down"
                     << conicRejectName(r.synth.back.fit.reject)
                     << conicRejectName(r.synth.down.fit.reject);
            return;
        }

        // Single-sample scalars: empty curve + exactly one phaseSample, the shape
        // every setup metric already uses (foot_metrics.h §"degenerate series").
        //
        // The PHASE LABEL on the delta is load-bearing, not cosmetic. The measure
        // m_transitionPlaneDelta reduces `at` the `transition` anchor, and
        // measure_sample.cpp's At reducer looks up a PhaseGridValue whose .phase ==
        // Phase::Transition; for an empty-curve metric every value comes from the
        // labelled fallback in buildPhaseGrid, which keys on this label. Stamp it
        // anything else and the measure silently resolves nothing. The TIME is
        // free, and top-of-backswing is the honest instant — it is where the two
        // windows meet.
        auto push = [&ctx](const QString &key, const QString &label,
                           Phase phase, int64_t tUs, double value) {
            MetricSeries m;
            m.key   = key;
            m.label = label;
            m.unit  = QStringLiteral("°");
            m.phaseSamples.push_back({ phase, tUs, value, QString() });
            ctx.detail->series.push_back(std::move(m));
        };
        push(QStringLiteral("transitionPlaneDelta"),
             QStringLiteral("Transition plane delta"), Phase::Transition, tp->t_us, r.deltaDeg);
        // The absolute inclinations are NOT calibrated (brief §9 bounds a 64° body-depth
        // bias) — carried for the trace and the node-line research, never as a coaching
        // output, which is why neither has a measure or a catalogue descriptor.
        push(QStringLiteral("swingPlaneIotaBack"),
             QStringLiteral("Swing plane inclination, backswing"), Phase::Top, tp->t_us, r.iotaBackDeg);
        push(QStringLiteral("swingPlaneIotaDown"),
             QStringLiteral("Swing plane inclination, downswing"), Phase::Impact, im->t_us, r.iotaDownDeg);

        ppInfo() << "[WristAnalysis] shaft plane:" << planeChannelName(r.channel)
                 << "delta" << r.deltaDeg << "deg (back" << r.iotaBackDeg
                 << "down" << r.iotaDownDeg << ")"
                 << (r.channel == PlaneChannel::Synth
                         ? QStringLiteral("— measured fell back: back=%1 down=%2")
                               .arg(QString::fromLatin1(conicRejectName(r.measured.back.fit.reject)),
                                    QString::fromLatin1(conicRejectName(r.measured.down.fit.reject)))
                         : QString());
    }
};

// 14. IMU calibration bindings — one BindingRecord per bound
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
            rec.hackMotion           = b.hackMotion;
            ctx.detail->bindings.push_back(std::move(rec));
        }
    }
};

// 15. Resemblance score + uncertainty. The scorer/interval read
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

// 16. Tier-2 wrist assessment — the AI-coach feed. Gated on the LOCAL series, but reads
//     detail->series/detail->phases (includes head/foot — deliberately; do not "fix").
//     Overrides the headline score, clears the interval.
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
        const auto provider = makeReferenceBandProvider();
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

// 17. WB4 IMU-less pose wrist assessment (dark by default). Runs
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
            const auto provider = makeReferenceBandProvider();
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

// The body-metric block, shared by EVERY profile.
//
// ANALYSIS IS AGNOSTIC OF SESSION TYPE. What a stage may produce is decided by the data and the
// devices actually present for the shot, never by which session the operator happened to pick, and
// every stage below already states its own requirement in canRun() — a face-on camera and a pose
// track, a bound trunk IMU, a valid shaft track, a confident phase ladder. Listing them in one
// profile and not the other put a second, invisible gate in front of those: a swing recorded
// perfectly well under the Swing session produced no head, foot, lower-body or tempo metrics, and
// the metric catalogue then reported "produced in Wrist Motion sessions only", which reads to the
// golfer as a statement about their equipment. A session type is a capture INTENT; it is not
// evidence about what was captured.
//
// Order still matters and is the only contract: every stage here appends to detail->series, which
// BindDetail assigns wholesale, so all of them must sit after it.
// 13d. Kinematic sequence — the four segment angular-speed series and the ordered
//      peaks over them (segment_rates.h; kinematic_sequence_design.md). Resolved PER
//      SEGMENT from whatever is bound: a pelvis / thorax / lead-arm / club IMU where
//      there is one, the face-on camera where there is not. Runs AFTER ShaftPlane
//      because the face-on arm and club rates de-project through the downswing conic
//      it fits, and after BodyRotation for the same fused streams.
//
//      GATED ON AVAILABLE DATA, NEVER ON SESSION TYPE (standing convention): an
//      Impact on the ladder plus at least one input that can carry a segment — a
//      face-on pose track, a valid shaft track, or a bound segment IMU. Absent all
//      of those it emits nothing and skipReason names which.
// 13c-bis. The DOWN-THE-LINE pose pass. Two consumers: the kinematic sequence's paired trunk
//      route (segment_rates.h "faceOn+dtl"; kinematic_sequence_design.md §5.2) and the DTL club
//      track (DtlShaftStage below; dtl_shaft_tracker_design.md), whose anchors it is. Runs when
//      EITHER is enabled.
//
//      WHERE IT SITS, AND WHY. Immediately before DtlShaft and KinematicSequence, which is after
//      SegResolve / EventRefine / PositionsLadder / TimelineFusion / BindDetail: its scan bounds
//      are read off the RESOLVED swing span, and reading them off a pre-resolve one would pose a
//      different window depending on which stage had run. It is also strictly after the face-on
//      pose, ball and shaft stages, so nothing it does can move them.
//
//      THE SPAN IS swinglab_run --dtl's, the one the DTL tracker was validated on (2026-09-21;
//      until then this stage posed Address → Impact + 150 ms at stride 6/1 for the trunk route
//      alone, which starves the tracker's address band and every band after impact): the
//      face-on swing span widened back 1 s to cover the address hold and forward 0.3 s past the
//      finish, every frame (stride 1 dense and sparse). No span ⇒ impact −2.5 s / +0.8 s.
//
//      IT BUILDS ITS OWN RUNNER OPTIONS AND NEVER TOUCHES ctx.runnerOpt. The ball and shaft
//      stages and the face-on smoother all gate on that slot; overwriting it with a second
//      camera's options would silently re-aim them.
//
//      The time base is INHERITED, never rediscovered — the two streams share the window clock,
//      and a second, independent opinion about when the swing was is the thing
//      dtl_shaft_tracker_design.md §5.3 forbids. Hence explicit bounds and twoPass = false.
struct DtlPoseStage : AnalysisStage {
    QString name() const override { return QStringLiteral("DtlPose"); }
    static bool pairWanted(const AnalysisContext &ctx)
    {
        const SegmentRatesConfig c = SegmentRatesConfig::fromOverrides(ctx.job.tuningOverrides);
        return c.enabled && c.pairTrunkEnabled;
    }
    static bool shaftWanted(const AnalysisContext &ctx)
    {
        return DtlShaftConfig::fromOverrides(ctx.job.tuningOverrides).enabled;
    }
    bool canRun(const AnalysisContext &ctx) const override
    {
        if (!pairWanted(ctx) && !shaftWanted(ctx)) return false;
        if (ctx.job.dtlSource == pinpoint::kInvalidSourceId) return false;
        if (!ctx.window || ctx.window->entriesFor(ctx.job.dtlSource).empty()) return false;
        return ctx.caps.hasCamera(CameraPlacement::FaceOn) && !ctx.detail->pose2d.frames.empty();
    }
    QString skipReason(const AnalysisContext &ctx) const override
    {
        if (!pairWanted(ctx) && !shaftWanted(ctx))
            return QStringLiteral("pair trunk route and DTL shaft tracker both disabled (dark)");
        if (ctx.job.dtlSource == pinpoint::kInvalidSourceId)
            return QStringLiteral("no down-the-line camera");
        if (!ctx.window || ctx.window->entriesFor(ctx.job.dtlSource).empty())
            return QStringLiteral("the down-the-line stream holds no frames in this window");
        return QStringLiteral("no face-on pose to pair with");
    }
    void run(AnalysisContext &ctx) override
    {
        const std::vector<pinpoint::IndexEntry> entries = ctx.window->entriesFor(ctx.job.dtlSource);
        const Segmentation &seg = ctx.seg;       // resolved; == detail->segmentation (BindDetail)
        int64_t scanLo, scanHi;
        if (seg.swingEndUs > seg.swingStartUs) {
            scanLo = seg.swingStartUs - 1000000;
            scanHi = seg.swingEndUs   +  300000;
        } else {
            if (ctx.job.impactUs <= 0) {
                ppWarn() << "[WristAnalysis] dtl pose: no swing span and no impact instant — skipping";
                return;
            }
            scanLo = ctx.job.impactUs - 2500000;   // no face-on span: impact ± the design's fallback
            scanHi = ctx.job.impactUs +  800000;
        }
        scanLo = std::max(scanLo, entries.front().timestamp_us);
        scanHi = std::min(scanHi, entries.back().timestamp_us);
        // AN EMPTY INTERSECTION IS A REAL CONDITION — a camera that started after the swing, or a
        // clock the two streams do not share. Left alone it hands PoseRunner an INVERTED range,
        // which reads as "no bounds" and silently becomes a full-ring stride-1 ViTPose pass. The
        // same guard swinglab_run's --dtl block carries, for the same reason.
        if (scanHi <= scanLo) {
            ppWarn() << "[WristAnalysis] dtl pose: the down-the-line stream and the swing span do"
                     << "not overlap (scan window" << qlonglong(scanLo) << ".."
                     << qlonglong(scanHi) << "us is empty) — no down-the-line products";
            return;
        }

        int dw = 0, dh = 0;
        const pinpoint::FormatDescriptor &fd = ctx.window->formatOf(ctx.job.dtlSource);
        if (const auto *cfmt = std::get_if<pinpoint::CameraFormat>(&fd.format)) {
            dw = int(cfmt->width);
            dh = int(cfmt->height);
        }

        ShotAnalysisRunnerOptions dopt;                      // LOCAL — never ctx.runnerOpt
        dopt.impactUs             = ctx.job.impactUs;
        dopt.handedness           = ctx.job.handedness;
        dopt.motionCaptureQuality = ctx.job.motionCaptureQuality;
        dopt.tuningOverrides      = ctx.job.tuningOverrides;
        dopt.twoPass              = false;                   // the span is inherited, not discovered
        dopt.scanStartUs          = scanLo;
        dopt.scanEndUs            = scanHi;
        // EVERY frame in the bound. The DTL bands are short and the tracker solves inside them
        // frame by frame; a sparse zone would turn a two-frame band edge into no band at all.
        dopt.denseStride          = 1;
        dopt.sparseStride         = 1;

        QElapsedTimer dtlWall;
        dtlWall.start();
        if (!ctx.job.poseDtlPreloaded.frames.empty()) {
            ctx.detail->poseDtl = ctx.job.poseDtlPreloaded;  // version-gated reuse (analysis_versions.h)
            ctx.detail->poseDtl.camera = ctx.job.dtlSource;
        } else {
            ctx.detail->poseDtl = ctx.job.poseDtlTrackPath.isEmpty()
                                      ? PoseRunner::run(*ctx.window, ctx.job.dtlSource, dopt)
                                      : PoseRunner::loadFromJson(ctx.job.poseDtlTrackPath,
                                                                 ctx.job.dtlSource);
        }
        // Smoothed HERE rather than through PoseSmoothStage: that stage also rewrites the grip
        // anchors from the smoothed hands, a face-on product the face-on shaft tracker reads —
        // the DTL tracker's anchors are the RAW frames, as in SwingLab. The pair route reads the
        // smoother's posterior σ on the hips and shoulders; the replay tile draws the smoothed
        // skeleton and its 240 Hz synth, built with PoseSmoothStage's own keys.
        if (dw > 0 && dh > 0 && !ctx.detail->poseDtl.frames.empty()) {
            const PoseSmootherConfig smCfg =
                PoseSmootherConfig::fromOverrides(ctx.job.tuningOverrides);
            PoseSmootherOutput so = smoothPoseTrack(ctx.detail->poseDtl.frames, dw, dh, smCfg);
            ctx.detail->poseDtl.smoothed       = std::move(so.smoothed);
            ctx.detail->poseDtl.smoothedAux    = std::move(so.aux);
            ctx.detail->poseDtl.adaptFallbacks = so.adaptFallbacks;
            PoseSynthConfig psCfg;
            tuning::apply(ctx.job.tuningOverrides, "poseSynth.enabled", psCfg.enabled);
            tuning::apply(ctx.job.tuningOverrides, "poseSynth.rateHz",  psCfg.rateHz);
            ctx.detail->poseDtl.smoothedSynth =
                synthesizePoseTrack(ctx.detail->poseDtl.smoothed, psCfg);
        }
        ctx.detail->timings.poseDtlMs = int(dtlWall.elapsed());
        if (!ctx.detail->poseDtl.frames.empty()) {
            ctx.detail->versions.poseDtl      = kDtlPoseStageVersion;
            ctx.detail->versions.poseDtlModel = PoseRunner::modelIdentity(ctx.job.motionCaptureQuality);
        }
        ppInfo() << "[WristAnalysis] dtl pose:" << qlonglong(ctx.detail->poseDtl.frames.size())
                 << "frames over" << qlonglong((scanHi - scanLo) / 1000) << "ms in"
                 << ctx.detail->timings.poseDtlMs << "ms";
    }
};

// 13c-ter. The DOWN-THE-LINE club track (DtlShaftTracker; dtl_shaft_tracker_design.md). Reads the
//      DTL pose DtlPoseStage just produced and the face-on witness ShaftStage built from the
//      tracker's own output; writes detail->shaftDtl and nothing else. One direction only
//      (§5.10): no face-on product, metric or score reads it — it is drawn on the DTL replay
//      tile and persisted as analysis.clubDtl, the same bytes SwingLab's club_dtl.json holds.
//
//      No synth, no predicted tier, no bridging of end-on gaps (§5.9): where no frame published
//      an angle the tile shows no shaft, and that is the honest picture.
struct DtlShaftStage : AnalysisStage {
    QString name() const override { return QStringLiteral("DtlShaft"); }
    bool canRun(const AnalysisContext &ctx) const override
    {
        return DtlShaftConfig::fromOverrides(ctx.job.tuningOverrides).enabled
            && ctx.window && !ctx.detail->poseDtl.frames.empty()
            && ctx.detail->shaft.valid && ctx.foWitness;
    }
    QString skipReason(const AnalysisContext &ctx) const override
    {
        if (!DtlShaftConfig::fromOverrides(ctx.job.tuningOverrides).enabled)
            return QStringLiteral("DTL shaft tracker disabled (shaft.dtl.enabled)");
        if (ctx.detail->poseDtl.frames.empty())
            return QStringLiteral("no down-the-line pose");
        if (!ctx.detail->shaft.valid)
            return QStringLiteral("no valid face-on shaft track to witness with");
        return QStringLiteral("no face-on witness");
    }
    void run(AnalysisContext &ctx) override
    {
        QElapsedTimer wall;
        wall.start();
        const FaceOnWitness *wit = ctx.foWitness->tUs.empty() ? nullptr : ctx.foWitness.get();
        DtlShaftTrack2D t = DtlShaftTracker::track(*ctx.window, ctx.detail->poseDtl, wit, ctx.job);
        {
            const DtlShaftConfig cfg = DtlShaftConfig::fromOverrides(ctx.job.tuningOverrides);
            t.configJson = dtlShaftConfigJson(cfg);
            t.configHash = dtlConfigHash(cfg);
        }
        t.streamSerial = QString::fromStdString(ctx.window->formatOf(ctx.job.dtlSource).device_serial);
        int published = 0;
        for (const DtlSample &s : t.samples)
            if (s.tier >= DtlTier::Ray) ++published;
        ctx.detail->shaftDtl = std::move(t);
        ctx.detail->versions.shaftDtl = kDtlShaftStageVersion;
        const DtlShaftTrack2D &d = ctx.detail->shaftDtl;
        ppInfo() << "[WristAnalysis] dtl shaft:" << (d.valid ? "valid," : "INVALID,")
                 << published << "/" << qlonglong(d.samples.size()) << "frames published in"
                 << qlonglong(d.bands.size()) << "bands, sighted" << d.sightedFrac
                 << ", ball" << qPrintable(d.ball.found ? d.ball.source : QStringLiteral("not found"))
                 << ", L" << qPrintable(d.lFullSource) << ", witness"
                 << (ctx.job.shaftPreloaded.samples.empty() ? "traced" : "from reused flags")
                 << "," << qlonglong(wall.elapsed()) << "ms";
    }
};

// 13c-quater. The shaft in THREE dimensions (shaft_fusion.h; shaft_fusion_design.md): the two
//      image angles the face-on and down-the-line trackers published, intersected. Reads both club
//      tracks and feeds NEITHER — the DTL design's one-direction rule (§5.10) stands; what comes
//      back is a plane and a list of frames where the two views disagree. A few microseconds.
//
//      The face-on angle is taken from the real frames where the tracker MEASURED both sides of
//      the DTL instant, else from its synth track and marked bridged; a bridged sample is checked
//      against the downswing plane but never fitted into it. That is how the face-on bridge
//      through impact gets a second opinion from the view that sees impact sharply.
struct ShaftFusionStage : AnalysisStage {
    QString name() const override { return QStringLiteral("ShaftFusion"); }
    bool canRun(const AnalysisContext &ctx) const override
    {
        return shaftFusionConfigFromOverrides(ctx.job.tuningOverrides).enabled
            && ctx.detail->shaft.valid && ctx.detail->shaftDtl.valid
            && ctx.seg.eventFor(Phase::Top) && ctx.seg.eventFor(Phase::Impact);
    }
    QString skipReason(const AnalysisContext &ctx) const override
    {
        if (!shaftFusionConfigFromOverrides(ctx.job.tuningOverrides).enabled)
            return QStringLiteral("shaft fusion disabled (shaft.fusion.enabled)");
        if (!ctx.detail->shaft.valid)    return QStringLiteral("no valid face-on shaft track");
        if (!ctx.detail->shaftDtl.valid) return QStringLiteral("no valid down-the-line shaft track");
        return QStringLiteral("no Top or no Impact on the ladder");
    }
    static std::vector<fusion::FoSample> angleTrack(const std::vector<ShaftSample2D> &v, bool allMeasured)
    {
        std::vector<fusion::FoSample> out;
        out.reserve(v.size());
        double prev = 0.0;
        for (const ShaftSample2D &s : v) {
            double th = s.thetaRad;
            if (!out.empty()) th += 2.0 * M_PI * std::round((prev - th) / (2.0 * M_PI));
            prev = th;
            const bool measured = allMeasured
                || ((s.flags & ShaftMeasured)
                    && !(s.flags & (ShaftCoasted | ShaftSynthesized | ShaftImplausible | ShaftKinematicPredicted)));
            out.push_back({ s.t_us, th, measured });
        }
        return out;
    }
    void run(AnalysisContext &ctx) override
    {
        const fusion::Config cfg = shaftFusionConfigFromOverrides(ctx.job.tuningOverrides);
        const ShaftTrack2D    &fo = ctx.detail->shaft;
        const DtlShaftTrack2D &dt = ctx.detail->shaftDtl;
        std::vector<fusion::DtlSampleIn> dtl;
        for (const DtlSample &s : dt.samples)
            if (s.tier >= DtlTier::Ray && std::isfinite(s.thetaRad))
                dtl.push_back({ s.t_us - dt.clockOffsetUs, s.thetaRad, s.band });
        const int64_t topUs    = ctx.seg.eventFor(Phase::Top)->t_us;
        const int64_t impactUs = ctx.seg.eventFor(Phase::Impact)->t_us;
        int64_t backFromUs = topUs - 900000;
        if (const auto tk = ctx.seg.eventFor(Phase::Takeaway); tk && tk->t_us < topUs)
            backFromUs = tk->t_us;
        // The address plane is read from the DTL frames up to the takeaway (P1 on this ladder).
        const int64_t addressToUs = ctx.seg.eventFor(Phase::Address)
                                        ? ctx.seg.eventFor(Phase::Address)->t_us : backFromUs;
        ctx.detail->shaft3d = fusion::fuseTracks(angleTrack(fo.samples, false), angleTrack(fo.synth, true),
                                                 dtl, backFromUs, topUs, impactUs + 20000, cfg, addressToUs);
        ctx.detail->shaft3dCfg = cfg;
        ctx.detail->versions.shaftFusion = kShaftFusionStageVersion;
        const fusion::Track3D &t = ctx.detail->shaft3d;
        // swingPlane — the DOWNSWING shaft plane against the ADDRESS shaft plane, in degrees, + =
        // delivered steeper (above the plane the shaft started on). One fitted plane, so the series
        // is that one number held over top → impact and nowhere else: the P2→P4 backswing reading
        // of the same key finds no sample and stays absent, which is the truth — the backswing is
        // not one plane and the DTL view is end-on at both of its ends.
        if (std::isfinite(t.deliveryVsAddressDeg)) {
            MetricSeries m;
            m.key   = QStringLiteral("swingPlane");
            m.label = QStringLiteral("Swing plane");
            m.unit  = QStringLiteral("°");
            for (int64_t us = topUs; us <= impactUs; us += 5000) {
                m.t_us.push_back(us);
                m.value.push_back(t.deliveryVsAddressDeg);
            }
            for (Phase p : { Phase::ArmParallelDown, Phase::Delivery })
                if (const PhaseEvent *e = ctx.seg.eventFor(p); e && e->t_us >= topUs && e->t_us <= impactUs)
                    m.phaseSamples.push_back({ p, e->t_us, t.deliveryVsAddressDeg, QString() });
            m.sigma = t.down.oopRmsDeg;
            if (m.t_us.size() >= 2) ctx.detail->series.push_back(std::move(m));
        }
        ppInfo() << "[WristAnalysis] shaft fusion:" << qlonglong(t.samples.size()) << "/" << t.nDtlPublished
                 << "DTL frames fused (" << t.nBridged << "against the face-on bridge," << t.nNoFaceOn
                 << "with no face-on ); downswing plane"
                 << (t.down.fitted ? QStringLiteral("%1° to the ground, heading %2°, %3° rms over %4 frames%5")
                                         .arg(t.down.inclDeg, 0, 'f', 1).arg(t.down.azimDeg, 0, 'f', 1)
                                         .arg(t.down.oopRmsDeg, 0, 'f', 2).arg(t.down.n)
                                         .arg(t.down.offered(cfg) ? QString() : QStringLiteral(" — NOT offered"))
                                   : QStringLiteral("not fitted (%1 frames)").arg(t.down.n))
                 << "; disagreements: sign" << t.nSignDisagree << "off-plane" << t.nOffPlane
                 << "; address plane" << t.addressInclDeg << "° over" << t.addressN << "frames, delivery"
                 << t.deliveryVsAddressDeg << "° above it"
                 << (t.backIncoherent ? "; BACKSWING INCOHERENT — suspect a mirrored DTL band" : "");
    }
};

// 13c-quinquies. Down-the-line POSTURE (dtl_posture.h; dtl_posture_design.md): pelvis thrust,
//      spine forward bend, the two knee flexions, ball reach and the heel-toe balance proxy — the
//      sagittal set the face-on producers refuse. Reads the DTL pose, the face-on ladder, the DTL
//      ball and (for shoulder width and the fallback ruler) the face-on pose. DETAIL series only.
struct DtlPostureStage : AnalysisStage {
    QString name() const override { return QStringLiteral("DtlPosture"); }
    bool canRun(const AnalysisContext &ctx) const override
    {
        return DtlPostureConfig::fromOverrides(ctx.job.tuningOverrides).enabled
            && ctx.window && ctx.job.dtlSource != pinpoint::kInvalidSourceId
            && !ctx.detail->poseDtl.frames.empty() && ctx.seg.eventFor(Phase::Address);
    }
    QString skipReason(const AnalysisContext &ctx) const override
    {
        if (!DtlPostureConfig::fromOverrides(ctx.job.tuningOverrides).enabled)
            return QStringLiteral("DTL posture disabled (dtlPosture.enabled)");
        if (ctx.detail->poseDtl.frames.empty()) return QStringLiteral("no down-the-line pose");
        return QStringLiteral("no Address on the ladder");
    }
    void run(AnalysisContext &ctx) override
    {
        const auto *dfmt = std::get_if<pinpoint::CameraFormat>(&ctx.window->formatOf(ctx.job.dtlSource).format);
        if (!dfmt || dfmt->width <= 0 || dfmt->height <= 0) return;
        DtlPostureInputs in;
        in.poseDtl = &ctx.detail->poseDtl;
        in.dtlW = int(dfmt->width); in.dtlH = int(dfmt->height);
        in.phases = &ctx.seg.events;
        in.leadIsLeft = ctx.job.handedness != 2;
        if (!ctx.detail->pose2d.frames.empty() && !ctx.job.cameraSources.empty()) {
            const auto *cfmt = std::get_if<pinpoint::CameraFormat>(
                &ctx.window->formatOf(ctx.job.cameraSources.front()).format);
            if (cfmt && cfmt->width > 0 && cfmt->height > 0) {
                in.poseFo = &ctx.detail->pose2d;
                in.foW = int(cfmt->width); in.foH = int(cfmt->height);
                // The face-on ball-diameter ruler, exactly as LowerBodyMetricsStage builds it.
                const BallPositionResult bp =
                    computeBallPosition(ctx.detail->ball, QPointF(), QPointF(),
                                        ctx.seg.eventFor(Phase::Address)->t_us, in.foW, in.foH,
                                        BallPositionConfig::fromOverrides(ctx.job.tuningOverrides));
                in.foMmPerPx = bp.mmPerPx;
            }
        }
        const DtlBall &b = ctx.detail->shaftDtl.ball;
        if (ctx.detail->shaftDtl.valid && b.found) {
            in.ballFound = true;
            in.ballBright = b.source == QLatin1String("bright");
            in.ballX = b.x; in.ballY = b.y; in.ballRadiusPx = b.radiusPx;
        }
        DtlPostureResult r = buildDtlPosture(in, DtlPostureConfig::fromOverrides(ctx.job.tuningOverrides));
        if (!r.valid) {
            ppInfo() << "[WristAnalysis] dtl posture: refused —" << qPrintable(r.reason);
            return;
        }
        ppInfo() << "[WristAnalysis] dtl posture:" << qlonglong(r.series.size()) << "series, ball"
                 << (r.toward > 0 ? "image-right" : "image-left") << ", ruler" << qPrintable(r.ruler)
                 << r.cmPerPx << "cm/px, scale ratio" << r.scaleRatio << ", shoulders" << r.shoulderWidthCm
                 << "cm, foot" << r.footLenCm << "cm"
                 << (r.rulerRefused.isEmpty() ? QString() : QStringLiteral("; RULER REFUSED: ") + r.rulerRefused);
        for (MetricSeries &m : r.series) ctx.detail->series.push_back(std::move(m));
    }
};

struct KinematicSequenceStage : AnalysisStage {
    QString name() const override { return QStringLiteral("KinematicSequence"); }
    static bool anyInput(const AnalysisContext &ctx)
    {
        const bool pose  = ctx.caps.hasCamera(CameraPlacement::FaceOn)
                        && !ctx.detail->pose2d.frames.empty();
        const bool shaft = ctx.detail->shaft.valid && !ctx.detail->shaft.samples.empty();
        const bool imu   = ctx.streams.has(SegmentRole::Pelvis) || ctx.streams.has(SegmentRole::Thorax)
                        || ctx.streams.has(SegmentRole::LeadForearm)
                        || ctx.streams.has(SegmentRole::LeadUpperArm) || ctx.streams.has(SegmentRole::Club);
        return pose || shaft || imu;
    }
    bool canRun(const AnalysisContext &ctx) const override
    {
        if (!SegmentRatesConfig::fromOverrides(ctx.job.tuningOverrides).enabled) return false;
        return ctx.seg.eventFor(Phase::Impact) && anyInput(ctx);
    }
    QString skipReason(const AnalysisContext &ctx) const override
    {
        if (!SegmentRatesConfig::fromOverrides(ctx.job.tuningOverrides).enabled)
            return QStringLiteral("sequence disabled (dark)");
        if (!ctx.seg.eventFor(Phase::Impact)) return QStringLiteral("no Impact on the ladder");
        return QStringLiteral("no face-on pose, no shaft track and no segment IMU");
    }
    void run(AnalysisContext &ctx) override
    {
        int w = 0, h = 0;
        if (!ctx.job.cameraSources.empty()) {
            const pinpoint::FormatDescriptor &fd = ctx.window->formatOf(ctx.job.cameraSources.front());
            if (const auto *cfmt = std::get_if<pinpoint::CameraFormat>(&fd.format)) {
                w = int(cfmt->width);
                h = int(cfmt->height);
            }
        }
        SegmentRatesInputs in;
        in.pose       = ctx.detail->pose2d.frames.empty() ? nullptr : &ctx.detail->pose2d;
        in.frameW     = w;
        in.frameH     = h;
        in.leadIsLeft = ctx.job.handedness != 2;
        in.streams    = ctx.hasImuStreams() ? &ctx.streams : nullptr;
        in.shaft      = ctx.detail->shaft.valid ? &ctx.detail->shaft : nullptr;
        in.phases     = &ctx.seg.events;
        in.impactUs   = ctx.job.impactUs;
        // The paired trunk route's second leg (DtlPoseStage). Absent ⇒ the trunk reads off the
        // face-on span exactly as it did before.
        if (!ctx.detail->poseDtl.frames.empty()
            && ctx.job.dtlSource != pinpoint::kInvalidSourceId && ctx.window) {
            const pinpoint::FormatDescriptor &dfd = ctx.window->formatOf(ctx.job.dtlSource);
            if (const auto *dfmt = std::get_if<pinpoint::CameraFormat>(&dfd.format);
                dfmt && dfmt->width > 0 && dfmt->height > 0) {
                in.poseDtl   = &ctx.detail->poseDtl;
                in.dtlFrameW = int(dfmt->width);
                in.dtlFrameH = int(dfmt->height);
            }
        }
        // The downswing plane the shaft fusion measured, when it offered one (ShaftFusionStage).
        if (ctx.detail->shaft3d.valid && ctx.detail->shaft3d.down.offered(ctx.detail->shaft3dCfg)) {
            in.fusedClubPlane.have    = true;
            in.fusedClubPlane.ratio   = ctx.detail->shaft3d.down.foRatio;
            in.fusedClubPlane.nodeRad = ctx.detail->shaft3d.down.foNodeDeg * M_PI / 180.0;
        }
        // The same track's headline linear speed at impact, from the Kinematics stage that ran
        // before this one — the club node's credibility gate (segment_rates.h).
        for (const MetricSeries &m : ctx.detail->series) {
            if (m.key != QLatin1String("clubheadSpeed")) continue;
            for (const PhaseSample &ps : m.phaseSamples)
                if (ps.phase == Phase::Impact) in.clubheadSpeedImpactMph = ps.value;
        }

        const SegmentRatesResult r =
            buildSegmentRates(in, SegmentRatesConfig::fromOverrides(ctx.job.tuningOverrides));
        if (!r.valid) {
            ppInfo() << "[WristAnalysis] sequence: no segment produced a rate";
            return;
        }
        for (MetricSeries &m : segmentRateSeries(r))
            ctx.detail->series.push_back(std::move(m));
        ctx.detail->kinematicSequence = r.sequence;

        QString placed;
        for (const KsNode &n : r.sequence.nodes) {
            placed += QStringLiteral(" %1%2(%3 %4ms±%5")
                          .arg(QString::fromLatin1(seqSegmentKey(n.segment)),
                               n.placed ? QString() : QStringLiteral("?"), n.routeId)
                          .arg(n.beforeImpactMs, 0, 'f', 0).arg(n.tSigmaMs, 0, 'f', 0);
            // A bounded trunk node: the peak was out of the camera's sight (design §12.4).
            if (std::isfinite(n.peakNoEarlierThanMs))
                placed += QStringLiteral(" ≤%1ms").arg(n.peakNoEarlierThanMs, 0, 'f', 0);
            if (std::isfinite(n.peakNoLaterThanMs))
                placed += QStringLiteral(" ≥%1ms").arg(n.peakNoLaterThanMs, 0, 'f', 0);
            placed += QLatin1Char(')');
        }
        ppInfo() << "[WristAnalysis] sequence:" << r.sequence.verdict << r.sequence.routeSummary
                 << qPrintable(placed);
        // ONE LINE PER SWING about the pair route, so the corpus pass can read what the geometry
        // did without opening the series. Diagnostic only — nothing gates on it and nothing
        // serialises it.
        if (r.pair.attempted) {
            const auto segLine = [](const char *tag, const PairSegmentDiag &d) {
                return d.refusal.isEmpty()
                    ? QStringLiteral(" %1(corr %2/%3 closure %4/%5 n%6 rEll %7 sgn %8/%9"
                                     " swaps %10/%11 drop %12")
                          .arg(QLatin1String(tag))
                          .arg(d.corrAbs, 0, 'f', 2).arg(d.corrSigned, 0, 'f', 2)
                          .arg(d.closureP50, 0, 'f', 3).arg(d.closureP90, 0, 'f', 3)
                          .arg(d.nPaired).arg(d.rEllipse, 0, 'f', 3)
                          .arg(d.signFo).arg(d.signDtl)
                          .arg(d.nSwapsFo).arg(d.nSwapsDtl).arg(d.nSwapFramesDropped)
                        + QStringLiteral(" src %1/%2 rl %3/%4 invalid %5")
                              .arg(d.srcFo, d.srcDtl).arg(d.nRateLimitedFo).arg(d.nRateLimitedDtl)
                              .arg(d.invalidFrac, 0, 'f', 3) + QStringLiteral(")")
                    : QStringLiteral(" %1(REFUSED: %2 swaps %3/%4 drop %5 src %6/%7 rl %8/%9)")
                          .arg(QLatin1String(tag), d.refusal)
                          .arg(d.nSwapsFo).arg(d.nSwapsDtl).arg(d.nSwapFramesDropped)
                          .arg(d.srcFo, d.srcDtl).arg(d.nRateLimitedFo).arg(d.nRateLimitedDtl);
            };
            QString line = QStringLiteral("[WristAnalysis] sequence pair: rVert %1 (%2/%3 px, n%4)")
                               .arg(r.pair.rVertical, 0, 'f', 3)
                               .arg(r.pair.extentFoPx, 0, 'f', 0).arg(r.pair.extentDtlPx, 0, 'f', 0)
                               .arg(r.pair.addrSamples);
            if (!r.pair.refusal.isEmpty()) line += QStringLiteral(" REFUSED: ") + r.pair.refusal;
            else line += segLine("pelvis", r.pair.pelvis) + segLine("thorax", r.pair.thorax);
            ppInfo() << qPrintable(line);
        }
    }
};

void appendBodyMetricStages(SessionProfile &p)
{
    p.stages.push_back(std::make_unique<HeadTrackStage>());
    p.stages.push_back(std::make_unique<FootMetricsStage>());
    p.stages.push_back(std::make_unique<LowerBodyMetricsStage>());
    p.stages.push_back(std::make_unique<UpperBodyMetricsStage>());
    p.stages.push_back(std::make_unique<BodyRotationStage>());
    p.stages.push_back(std::make_unique<ClubDeliveryStage>());
    p.stages.push_back(std::make_unique<TempoStage>());
}

// The Wrist session's stage list, in analysis block order.
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
    p.stages.push_back(std::make_unique<ImpactStage>());
    p.stages.push_back(std::make_unique<ShaftStage>());
    p.stages.push_back(std::make_unique<SegResolveStage>());
    p.stages.push_back(std::make_unique<ShaftLeanStage>());
    p.stages.push_back(std::make_unique<RequireProductsStage>());
    p.stages.push_back(std::make_unique<EventRefineStage>());
    // Slot 10c, one of two: PositionsLadder back-fills, TimelineFusion
    // arbitrates, and refine.fusion decides which one canRun lets through.
    p.stages.push_back(std::make_unique<PositionsLadderStage>());
    p.stages.push_back(std::make_unique<TimelineFusionStage>());
    p.stages.push_back(std::make_unique<BindDetailStage>());
    appendBodyMetricStages(p);
    p.stages.push_back(std::make_unique<KinematicsStage>());
    p.stages.push_back(std::make_unique<ShaftPlaneStage>());
    p.stages.push_back(std::make_unique<DtlPoseStage>());
    p.stages.push_back(std::make_unique<DtlShaftStage>());
    p.stages.push_back(std::make_unique<ShaftFusionStage>());
    p.stages.push_back(std::make_unique<DtlPostureStage>());
    p.stages.push_back(std::make_unique<KinematicSequenceStage>());
    p.stages.push_back(std::make_unique<BindingsStage>());
    p.stages.push_back(std::make_unique<ResemblanceStage>());
    p.stages.push_back(std::make_unique<AssessmentStage>());
    p.stages.push_back(std::make_unique<PoseAssessmentStage>());
    return p;
}

// Project the resolved context onto the flat ShotAnalysisResult. On halt: ok=false +
// haltError with a NULL detail and everything else default-constructed.
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

} // namespace

ShotAnalysisResult WristAnalyzer::analyze(const pinpoint::SwingWindow &window,
                                          const ShotAnalysisJob &job)
{
    // Build the capability-gated context, run the Wrist profile, and project the flat
    // result. ctx.wall starts at the top of the work so timings.totalMs spans the whole
    // analyze() call. The "no fusable IMU" log fires gated on !hasImuStreams(), so it
    // still fires when the resample stage skipped for zero IMU bindings.
    AnalysisContext ctx{ CaptureCapabilities::fromJob(job, window), job, &window };
    ctx.detail = std::make_shared<SwingAnalysis>();
    ctx.wall.start();

    runStages(wristProfile(), ctx);

    if (!ctx.hasImuStreams())
        ppInfo() << "[WristAnalysis] no fusable IMU streams — camera-only (pose) analysis";

    ShotAnalysisResult r = projectResult(ctx);
    recordAnalysisRun(QStringLiteral("Wrist"), ctx);   // per-stage profiler + run history
    return r;
}

// Shared camera-only profile for the non-Wrist session types (declared in
// wrist_analyzer.h). External linkage so shot_analyzer.cpp's CameraKinematicsAnalyzer
// can run it; the stage structs it constructs stay file-local in the anon namespace
// above (still visible for the rest of this translation unit). No IMU/scoring stages —
// SegResolve adopts the vision segmentation the Shaft stage emits, BindDetail binds the
// (empty) local products, and KinematicsStage appends the display series to the detail.
namespace pinpoint::analysis {
SessionProfile cameraKinematicsProfile()
{
    SessionProfile p;
    p.name = QStringLiteral("CameraKinematics");
    p.stages.push_back(std::make_unique<PoseStage>());
    p.stages.push_back(std::make_unique<PoseSmoothStage>());
    p.stages.push_back(std::make_unique<BallStage>());
    p.stages.push_back(std::make_unique<ImpactStage>());
    p.stages.push_back(std::make_unique<ShaftStage>());
    p.stages.push_back(std::make_unique<SegResolveStage>());
    // PositionsLadder is data-gated (positions + ladder present), not
    // session-gated, so it runs here too — the club P-events are a property of
    // the camera, not of the session type. Same for its fusion successor: on a
    // camera-only ladder every interior slot is a pure insertion and every anchor
    // slot ties, so arbitration reproduces the ladder and adds the audit trail.
    p.stages.push_back(std::make_unique<PositionsLadderStage>());
    p.stages.push_back(std::make_unique<TimelineFusionStage>());
    p.stages.push_back(std::make_unique<BindDetailStage>());
    // The same body-metric block the Wrist profile runs. It used to be absent here, which is what
    // made a face-on metric look like a property of the session rather than of the camera — see
    // appendBodyMetricStages. There is no EventRefine in this profile, so these stages read the
    // SegResolve ladder rather than the refined one; each of them already tolerates a coarser
    // ladder (they read ctx.seg.events and fall back per phase), and a coarser Address is a
    // slightly noisier reference, not a wrong one.
    appendBodyMetricStages(p);
    p.stages.push_back(std::make_unique<KinematicsStage>());
    p.stages.push_back(std::make_unique<ShaftPlaneStage>());
    p.stages.push_back(std::make_unique<DtlPoseStage>());
    p.stages.push_back(std::make_unique<DtlShaftStage>());
    p.stages.push_back(std::make_unique<ShaftFusionStage>());
    p.stages.push_back(std::make_unique<DtlPostureStage>());
    p.stages.push_back(std::make_unique<KinematicSequenceStage>());
    return p;
}
} // namespace pinpoint::analysis
