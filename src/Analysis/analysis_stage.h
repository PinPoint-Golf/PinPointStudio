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

// Capability-gated stage pipeline over a shared typed context (constrained
// blackboard) — the target orchestration architecture from
// docs/design/analysis_pipeline_fusion_architecture_proposal.md §10. Every future
// fusion proposal (P1–P11) lands as one AnalysisStage with an off-switch instead
// of another branch inside a monolithic analyze(). This header owns only the
// mechanism: the typed context, the stage ABC, the profile, and the dumb
// orchestrator. The per-session stage lists live with their analyzer (wrist
// stages are file-local in wrist_analyzer.cpp until the Swing session needs to
// share them — §10.5 step 4).
//
// Anti-goals (§10.6): no stage registration, no dependency sorting, no
// parallelism. Stages run in authored order; the orchestrator is a loop.
//
// That loop is still the default (analysis.parallel off). Since 6 Oct 2026 each stage
// also DECLARES what it reads and writes on the context (StageDecl below) and
// analysis_dag.h can run a profile as the dependency graph those declarations imply —
// docs/design/analysis_dag_design.md: 39 stages, one 18.6 s chain on the studio. The
// authored order stays the contract the graph is derived from: every edge points from
// an earlier stage to a later one, so the sequential loop is always one valid schedule.

#include <QElapsedTimer>
#include <QSet>
#include <QString>
#include <initializer_list>
#include <memory>
#include <optional>
#include <vector>

#include "shot_analyzer.h"      // ShotAnalysisJob
#include "swing_analysis.h"     // Segmentation, MetricSeries, BallTrack2D, SwingAnalysis, SegmentRole, ImuSegmentBinding
#include "imu_vision_fuser.h"   // FusedStreams
#include "pose_runner.h"        // ShotAnalysisRunnerOptions

namespace pinpoint { class SwingWindow; }

namespace pinpoint::analysis {

struct FaceOnWitness;   // dtl_shaft_types.h — held by pointer so this header stays OpenCV-free
struct FrameStoreSlot;  // frame_store.h — likewise

// Where a camera sits relative to the golfer. FaceOn is the placement most of the
// analysis runs off (pose/shaft/head/foot all do). DownTheLine is populated from
// ShotAnalysisJob::dtlSource; DtlPoseStage gates on it and poses that stream for two
// consumers — the kinematic sequence's PAIRED trunk route (segment_rates.h
// "faceOn+dtl", since 2026-09-20) and DtlShaftStage, the down-the-line club track
// (dtl_shaft_tracker_design.md, in the app since 2026-09-21). So a capture that holds
// a down-the-line camera analyses differently from one that does not; the face-on
// products themselves are untouched by it (§5.10: nothing flows back).
enum class CameraPlacement { FaceOn, DownTheLine };

// The capture's device inventory, resolved once from the job before any stage
// runs. Stages gate on it (canRun) so a webcam-only or IMU-only capture skips
// the stages it can't feed instead of branching inside them.
struct CaptureCapabilities {
    struct BoundImu {
        SegmentRole role        = SegmentRole::Unknown;
        bool        calibValid  = false;   // composite calibration gate (ImuSegmentBinding::calibrated)
        double      calibAgeSec = -1.0;    // age at shot time; -1 = never calibrated

        // ── Deferred sources (design §4.4) ───────────────────────────────────
        // ⚠ MEASURED OVER THIS WINDOW, NEVER TAKEN FROM A DEVICE DECLARATION.
        // That is the whole point: a pull that silently under-delivered then
        // gates exactly like no pull at all. The device HOLES an over-wide
        // request rather than clamping it — 33-58 % coverage with no error — so
        // a stage that trusted a declared rate would run on a window that is
        // mostly absent and report a number for it.
        //
        // ⚠ AND THIS IS A RATE, NOT A COUNT. Coverage is intervals and density;
        // a sample count cannot distinguish a dense span from a holed one.
        double      effectiveHz = 0.0;
        // The sub-span that exceeded the base rate, if any — empty when the lane
        // is uniformly live-rate. A stage needing high-rate input can then land
        // dark and skip with a reason until the data is actually present.
        int64_t     highRateSpanUs[2]{};

        // WHICH INSTRUMENT, carried from ImuSegmentBinding::hackMotion. `role` says
        // which segment; this says what measured it. Nothing gates on it here — the
        // conjugate is applied at the composition site and qAnat arrives in our
        // convention either way — it exists so the shot's route context can state
        // that a wG3 was worn, which `imuRoles` alone cannot express.
        bool        hackMotion  = false;
    };

    QSet<CameraPlacement> cameras;
    std::vector<BoundImu> imus;

    bool hasCamera(CameraPlacement p) const { return cameras.contains(p); }
    bool hasRole(SegmentRole r) const {
        for (const BoundImu &b : imus)
            if (b.role == r) return true;
        return false;
    }
    bool hasRoles(std::initializer_list<SegmentRole> roles) const {
        for (SegmentRole r : roles)
            if (!hasRole(r)) return false;
        return true;
    }

    // One BoundImu per job.imuBindings entry (imus.empty() == imuBindings.empty()) —
    // the fuser still drops Unknown-role / under-sampled bindings downstream, so a
    // present-but-unfusable binding still counts here (matching the monolith, where
    // the resample stage runs and hasImuStreams() then reports the empty result).
    // FaceOn present iff the job carries a face-on camera source — the exact gate
    // the monolith's `hasCamera` local used. DownTheLine iff the job resolved a
    // dtlSource, which DtlPoseStage gates on (see the enum's note).
    static CaptureCapabilities fromJob(const ShotAnalysisJob &job)
    {
        CaptureCapabilities caps;
        if (job.faceOnCameraCount > 0 && !job.cameraSources.empty())
            caps.cameras.insert(CameraPlacement::FaceOn);
        if (job.dtlSource != pinpoint::kInvalidSourceId)
            caps.cameras.insert(CameraPlacement::DownTheLine);
        caps.imus.reserve(job.imuBindings.size());
        for (const ImuSegmentBinding &b : job.imuBindings) {
            BoundImu bi{};
            bi.role        = b.role;
            bi.calibValid  = b.calibrated;
            bi.calibAgeSec = b.calibAgeSec;
            bi.hackMotion  = b.hackMotion;
            caps.imus.push_back(bi);
        }
        return caps;
    }

    // Did a HackMotion measure any segment on this shot? Gates the `hm.*` routes,
    // and gates nothing else.
    bool hasHackMotion() const {
        for (const BoundImu &b : imus)
            if (b.hackMotion) return true;
        return false;
    }

    // As above, but with the rate fields MEASURED FROM THE WINDOW rather than
    // left at zero (design §4.4) — so a stage gating on high-rate input sees
    // what this capture actually carries, not what a device claimed.
    static CaptureCapabilities fromJob(const ShotAnalysisJob &job,
                                       const pinpoint::SwingWindow &window)
    {
        CaptureCapabilities caps = fromJob(job);
        // ⚠ MEASURED AROUND IMPACT, not across the whole window. A stitched lane
        // is ~100 Hz over the still pre-roll and full rate through the swing, so
        // a window-wide average answers a question nobody asked: what gates a
        // metric computed AT IMPACT is the density THERE. ±125 ms is the region
        // the library's own guidance names for exactly this decision.
        constexpr int64_t kImpactHalfSpanUs = 125'000;
        if (job.impactUs <= 0)
            return caps;   // no anchor to measure around — leave NOT MEASURED
        const int64_t lo = job.impactUs - kImpactHalfSpanUs;
        const int64_t hi = job.impactUs + kImpactHalfSpanUs;
        for (size_t i = 0; i < caps.imus.size() && i < job.imuBindings.size(); ++i) {
            const SourceId src = job.imuBindings[i].source;
            caps.imus[i].effectiveHz =
                ImuVisionFuser::effectiveHzFor(window, src, lo, hi);
            ImuVisionFuser::highRateSpanFor(window, src,
                                            ImuVisionFuser::kGridHzMin,
                                            caps.imus[i].highRateSpanUs);
        }
        return caps;
    }
};

// Per-stage orchestration record — timing + skip provenance. The source of
// analysis.timings.stages (bindStageTimings below copies it onto the detail, which
// persists it); the trace itself stays in memory.
struct StageTraceEntry {
    QString name;
    bool    ran        = false;   // false ⇒ the stage was skipped
    QString skipReason;           // "halted", or the canRun skip reason; empty when ran
    qint64  elapsedNs  = 0;       // run() wall time; 0 when skipped
    // WHEN, not just how long (analysis_dag_design.md step A): offsets from the start of
    // the analysis (ctx.wall when it runs, else the orchestrator's own clock) and the pool
    // thread that ran it — 0 for the sequential loop. A skipped stage carries the instant
    // it was decided. Without these a parallel run's per-stage ms add up to more than the
    // wall and say nothing about what overlapped.
    qint64  startNs    = 0;
    qint64  endNs      = 0;
    int     thread     = 0;
    // The trace indices of the stages this one waited on (the profile's StageGraph preds;
    // attachStagePreds in analysis_dag.h). Telemetry for the monitor's critical path only —
    // never persisted; empty for a trace built without the graph (the unit tests).
    std::vector<int> preds;
};

// ── Declared data flow (analysis_dag_design.md §2) ──────────────────────────────
// The names a stage may declare. A FIXED table, not free strings, so a typo is caught by
// isKnownResource() (the DAG test checks every profile) rather than silently becoming a
// resource nobody else names — which would drop an edge and race.
//
//   window.*      a camera's frames FETCHED through SwingWindow::payloadOf (formatOf /
//                 entriesFor are not fetches). Each camera has ONE sequential reader
//                 (swing_payload_source.h), so the executor never runs two stages holding
//                 the same camera at once; window.imu is RAM-backed and not held.
//   ctx.* slots   streams, doRefuse, segImu, segVision, seg, ctx.series, runnerOpt, ball,
//                 foWitness, halted (every stage reads halted implicitly).
//   detail.*      the SwingAnalysis members the stages fill. detail.phases covers phases +
//                 segmentation (BindDetail writes both); detail.score covers tier, score,
//                 findings, assessmentScore and filterImpactStepDeg. versions/timings are
//                 written field-disjoint by their own stages and are not resources.
namespace res {
inline constexpr const char *kWindowFaceOn  = "window.faceOn";
inline constexpr const char *kWindowDtl     = "window.dtl";
inline constexpr const char *kWindowImpact  = "window.impact";
inline constexpr const char *kWindowImu     = "window.imu";
// A camera's decoded frames, held once for the analysis (frame_store.h, decode.frameStore):
// written by FrameDecodeStage from window.<cam>, read-only after. Reading it holds no camera,
// so two of its readers run at once. A stage that still reads window.<cam> reads it too
// (buildStageGraph adds the read), so it is ordered after the store whether or not it uses it.
inline constexpr const char *kFramesFaceOn  = "frames.faceOn";
inline constexpr const char *kFramesDtl     = "frames.dtl";
inline constexpr const char *kStreams       = "streams";
inline constexpr const char *kDoRefuse      = "doRefuse";
inline constexpr const char *kSegImu        = "segImu";
inline constexpr const char *kSegVision     = "segVision";
inline constexpr const char *kSeg           = "seg";
inline constexpr const char *kCtxSeries     = "ctx.series";
inline constexpr const char *kRunnerOpt     = "runnerOpt";
inline constexpr const char *kBall          = "ball";
inline constexpr const char *kFoWitness     = "foWitness";
inline constexpr const char *kHalted        = "halted";
inline constexpr const char *kPose2d        = "detail.pose2d";
inline constexpr const char *kPoseDtl       = "detail.poseDtl";
inline constexpr const char *kShaft         = "detail.shaft";
inline constexpr const char *kShaftDtl      = "detail.shaftDtl";
inline constexpr const char *kShaft3d       = "detail.shaft3d";
inline constexpr const char *kSkeleton3d    = "detail.skeleton3d";
inline constexpr const char *kSeries        = "detail.series";
inline constexpr const char *kPhases        = "detail.phases";
inline constexpr const char *kImpact        = "detail.impact";
inline constexpr const char *kAddressMarks  = "detail.addressMarks";
inline constexpr const char *kDetailBall    = "detail.ball";
inline constexpr const char *kKinSeq        = "detail.kinematicSequence";
inline constexpr const char *kScore         = "detail.score";
inline constexpr const char *kBindings      = "detail.bindings";

inline const std::vector<QString> &all()
{
    static const std::vector<QString> k = {
        kWindowFaceOn, kWindowDtl, kWindowImpact, kWindowImu, kFramesFaceOn, kFramesDtl,
        kStreams, kDoRefuse, kSegImu,
        kSegVision, kSeg, kCtxSeries, kRunnerOpt, kBall, kFoWitness, kHalted, kPose2d, kPoseDtl,
        kShaft, kShaftDtl, kShaft3d, kSkeleton3d, kSeries, kPhases, kImpact, kAddressMarks,
        kDetailBall, kKinSeq, kScore, kBindings };
    return k;
}
inline bool isKnownResource(const QString &r)
{
    for (const QString &k : all())
        if (k == r) return true;
    return false;
}
inline bool isCameraResource(const QString &r)
{
    return r.startsWith(QLatin1String("window.")) && r != QLatin1String(kWindowImu);
}
// The store a camera's window feeds (empty for window.impact / window.imu: no store).
inline QString framesFor(const QString &window)
{
    if (window == QLatin1String(kWindowFaceOn)) return QString::fromLatin1(kFramesFaceOn);
    if (window == QLatin1String(kWindowDtl))    return QString::fromLatin1(kFramesDtl);
    return QString();
}
} // namespace res

// What one stage touches. `reads` includes what canRun() reads. `writes` is an
// assignment OR an in-place mutation — for ordering the two are the same (ImpactAnchor
// and ShaftPlane mutate detail.shaft; PoseSmooth mutates detail.pose2d; DtlSynth3D
// mutates detail.shaftDtl). `appends` is only ever detail.series: an append commutes with
// every other append, provided the results are spliced back in authored order, which the
// executor does. A stage that READS detail.series (or overwrites it — BindDetail) sees
// exactly the prefix the sequential loop would have shown it.
//
// `barrier` is the default: a stage that has not declared anything is ordered after
// every earlier stage and before every later one — slow, never wrong.
struct StageDecl {
    std::vector<QString> reads;
    std::vector<QString> writes;
    std::vector<QString> appends;
    bool                 barrier = false;
    // The stage cannot run in a context that RequireProducts halts (its own canRun needs what
    // the halt is the absence of), so it need not wait for the halt decision: the implicit
    // `halted` read is not added. A halted analysis returns no detail, so nothing it wrote is
    // seen either way. DtlPose under pose.dtlEarly (canRun needs a face-on pose; the halt
    // needs none) — without it the halt's read of ctx.series chains it behind Shaft(FO).
    bool                 haltSafe = false;
};

// Where a stage's detail.series appends go. The sequential loop leaves it null and every
// append lands in detail->series directly, exactly as before. The executor points it at
// the running stage's own buffer on that stage's thread (thread_local — the stage runs on
// one thread) and splices the buffers in authored order. See AnalysisContext::seriesOut().
inline thread_local std::vector<MetricSeries> *tl_seriesSink = nullptr;

// The shared typed context the stages read and write — a constrained blackboard.
// Typed slots (not a bag of variants) so a stage's inputs/outputs are the compiler's
// business. `window` is a pointer so the orchestrator is unit-testable with no
// window (nullptr). Aggregate: construct with { caps, job, &window }; every other
// slot default-initializes.
struct AnalysisContext {
    CaptureCapabilities          caps;
    const ShotAnalysisJob        &job;
    const pinpoint::SwingWindow  *window = nullptr;

    FusedStreams                 streams;      // resampled IMU streams (ImuResample)
    bool                         doRefuse = false;
    std::optional<Segmentation>  segImu;       // IMU-derived segmentation (pre-adoption)
    std::optional<Segmentation>  segVision;    // camera-derived segmentation (pre-adoption)
    Segmentation                 seg;          // resolved segmentation (SegResolve onward)
    std::vector<MetricSeries>    series;        // LOCAL series — the scorer/metrics/trace read this
    std::optional<ShotAnalysisRunnerOptions> runnerOpt;   // pose/ball runner knobs (Pose stage)
    std::optional<BallTrack2D>   ball;          // resolved ball track (Ball stage)
    // The face-on witness for the down-the-line tracker (dtl_face_on_witness.h), built by
    // ShaftStage from the tracker's OWN output and decide trace before any later stage
    // touches the track — the object SwingLab's --dtl block builds. Null unless the job has
    // a DTL camera and the DTL tracker is enabled; DtlShaftStage reads it.
    std::shared_ptr<const FaceOnWitness> foWitness;
    // The decoded-frame stores (frame_store.h, decode.frameStore), one per camera, owned here so
    // they die with the analysis; consumers find them through the (window, source) registry.
    // Written only by the camera's FrameDecodeStage.
    std::shared_ptr<FrameStoreSlot> framesFaceOn, framesDtl;
    std::shared_ptr<SwingAnalysis> detail;      // the rich SwingAnalysis, written in place
    bool                         halted = false;
    QString                      haltError;
    QElapsedTimer                wall;           // whole-analysis wall time
    std::vector<StageTraceEntry> trace;

    // Fusable IMU orientation present — the monolith's `hasImu` gate verbatim.
    bool hasImuStreams() const {
        return !streams.timeGrid.empty() && !streams.segments.empty();
    }

    // The vector a stage that only APPENDS detail.series pushes to: detail->series under
    // the sequential loop, the stage's own buffer under the executor (tl_seriesSink).
    // A stage that also reads detail.series writes detail->series directly — the executor
    // runs it when the series holds exactly its authored prefix and nothing else touches it.
    std::vector<MetricSeries> &seriesOut() {
        return tl_seriesSink ? *tl_seriesSink : detail->series;
    }
};

// One pipeline stage. canRun gates on capabilities/context; skipReason annotates a
// skip for the trace; run mutates the context. Stages own no state — all state
// lives in the context, so a profile is reusable and order is the only contract.
class AnalysisStage {
public:
    virtual ~AnalysisStage() = default;
    virtual QString name() const = 0;
    virtual bool    canRun(const AnalysisContext &) const { return true; }
    virtual QString skipReason(const AnalysisContext &) const { return QString(); }
    virtual void    run(AnalysisContext &) = 0;
    // What run() and canRun() read and write (StageDecl). Undeclared ⇒ a barrier.
    virtual StageDecl decl() const { StageDecl d; d.barrier = true; return d; }
    // The same for one job, where a tuning key changes what the stage reads (DtlPose under
    // pose.dtlEarly). The executor builds its graph from this; decl() is the defaults' answer.
    virtual StageDecl declFor(const ShotAnalysisJob &) const { return decl(); }
};

// An ordered, named stage list for one session type.
struct SessionProfile {
    QString                                     name;
    std::vector<std::unique_ptr<AnalysisStage>> stages;
};

// The orchestrator: authored order, halted short-circuit (recorded as skip
// "halted"), canRun gate (recorded with the stage's skipReason), per-stage wall
// clock into ctx.trace only. No registration, no sorting, no parallelism.
inline void runStages(const SessionProfile &profile, AnalysisContext &ctx)
{
    // Offsets are from ctx.wall (started by the analyzer at the top of analyze()); a
    // context without one — the unit tests — gets the loop's own clock.
    QElapsedTimer own;
    own.start();
    const QElapsedTimer &clock = ctx.wall.isValid() ? ctx.wall : own;
    for (const std::unique_ptr<AnalysisStage> &stage : profile.stages) {
        StageTraceEntry e;
        e.name = stage->name();
        e.startNs = clock.nsecsElapsed();
        if (ctx.halted) {
            e.skipReason = QStringLiteral("halted");
        } else if (!stage->canRun(ctx)) {
            e.skipReason = stage->skipReason(ctx);
        } else {
            QElapsedTimer t;
            t.start();
            stage->run(ctx);
            e.ran       = true;
            e.elapsedNs = t.nsecsElapsed();
        }
        e.endNs = clock.nsecsElapsed();
        ctx.trace.push_back(std::move(e));
    }
}

// ctx.trace → detail->timings.stages (analysis.timings.stages in the document; swinglab's
// runmeta.json). Wall-clock telemetry, nothing reads it back.
inline void bindStageTimings(AnalysisContext &ctx)
{
    if (!ctx.detail) return;
    std::vector<StageTiming> &out = ctx.detail->timings.stages;
    out.clear();
    out.reserve(ctx.trace.size());
    for (const StageTraceEntry &e : ctx.trace) {
        StageTiming s;
        s.name       = e.name;
        s.ran        = e.ran;
        s.skipReason = e.skipReason;
        s.ms         = double(e.elapsedNs) / 1e6;
        s.startMs    = double(e.startNs) / 1e6;
        s.endMs      = double(e.endNs) / 1e6;
        s.thread     = e.thread;
        out.push_back(std::move(s));
    }
}

} // namespace pinpoint::analysis
