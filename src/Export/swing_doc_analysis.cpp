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

// swing_doc_analysis.cpp — the `analysis` block serialisers of a swing document: serializeScore,
// serializeAnalysis (the whole block, called by SwingDocWriter::writeSwingJson in
// swing_doc_writer.cpp), and the two public track builders poseTrackToJson and impactTrackJson
// (also used by the live detail). The writer is swing_doc_writer.cpp, the reader
// swing_doc_reader.cpp; the public API is swing_doc.h. Split out of swing_doc.cpp verbatim
// (refactor step 1.0, 14 Nov 2026).

#include "swing_doc.h"
#include "swing_doc_internal.h"

#include <QJsonArray>
#include <QSet>
#include <algorithm>
#include <cmath>

#include "../Analysis/swing_analysis.h"
#include "../Kinematics/kinematic_sequence_json.h" // kinematicSequenceToJson — one shape, three paths
#include "../Analysis/uncertainty_json.h"         // σ fields — one spelling, four paths
#include "../Club/shaft_fusion_json.h"             // shaftTrack3dToJson — analysis.club3d
#include "../Skeleton3d/skeleton3d_json.h" // skeleton3dToJson — analysis.skeleton3d
#include "../Club/dtl_shaft_json.h"                // dtlShaftTrackToJson — analysis.clubDtl == club_dtl.json
#include "../Body/address_marks_json.h"            // addressMarksToJson — analysis.addressMarks
#include "../Body/dtl_ruler_json.h"                // dtlRulerToJson — analysis.dtlRuler

namespace pinpoint {

namespace {

// Serialise the score breakdown into the "score" object (schema /3+, design §B.0/§B.7).
// Always carries kind + overall; resemblance fields only for the Wrist estimand; the
// uncertainty interval only once computed (WP-4). Pre-/3 docs stored a bare int here —
// every reader takes the overall through overallScoreFromAnalysis(), which reads both forms.
QJsonObject serializeScore(const analysis::ScoreBreakdown &s)
{
    QJsonObject o;
    o[QStringLiteral("kind")] = (s.kind == analysis::ScoreKind::Resemblance)
                                    ? QStringLiteral("resemblance") : QStringLiteral("adherence");
    o[QStringLiteral("overall")] = s.overall;
    if (s.kind == analysis::ScoreKind::Resemblance) {
        QJsonObject res;
        for (auto it = s.resemblance.constBegin(); it != s.resemblance.constEnd(); ++it)
            res[it.key()] = it.value();
        o[QStringLiteral("resemblance")] = res;
        o[QStringLiteral("pattern")]     = s.patternLabel;
        o[QStringLiteral("blended")]     = s.blended;
    }
    if (s.interval.valid())
        o[QStringLiteral("interval")] = QJsonObject{
            { QStringLiteral("halfWidth"), s.interval.halfWidth },
            { QStringLiteral("lo"),        s.interval.lo },
            { QStringLiteral("hi"),        s.interval.hi } };
    // Adherence contribution maps (§B.0). Additive and OMITTED WHEN EMPTY, so a
    // resemblance score's doc is byte-identical to before this key existed. A score
    // the athlete can take apart is worth more than one they can only read.
    auto emitBuckets = [&o](const char *name, const QHash<QString,int> &h) {
        if (h.isEmpty()) return;
        QJsonObject b;
        for (auto it = h.constBegin(); it != h.constEnd(); ++it)
            b[it.key()] = it.value();
        o[QString::fromLatin1(name)] = b;
    };
    emitBuckets("perRegion", s.perRegion);
    emitBuckets("perPhase",  s.perPhase);
    return o;
}

} // namespace

namespace swingdoc_detail {

// Serialise the analyzed swing into the additive "analysis" object (schema
// pinpoint.analysis/3 — versions the embedded block, distinct from the document's
// pinpoint.swing/2). Mirrors the QML analysisDetail shape, t_us as JSON numbers.
// /3 promotes "score" from a bare int to the ScoreBreakdown object (design §B.0a/§B.7).
QJsonObject serializeAnalysis(const analysis::SwingAnalysis &a, qint64 windowT0,
                              const QString &dtlAlias, const QString &dtlFile)
{
    using namespace analysis;
    // Analysis timestamps are produced in the SwingWindow's own domain — ABSOLUTE
    // (EventBuffer clock) for a live capture, but WINDOW-RELATIVE (0-based) for a
    // reconstructed re-analysis window. Persist them CONSISTENTLY window-relative
    // (matching streams / capture.impactUs / window bounds), so every consumer
    // sees one domain: subtract windowT0 (= clock.t0_us, the absolute window
    // start) only when the value is in the absolute domain (>= windowT0); pass
    // already-relative values (≪ windowT0) through unchanged.
    auto rel = [windowT0](int64_t t) -> qint64 {
        const qint64 tt = static_cast<qint64>(t);
        return tt >= windowT0 ? tt - windowT0 : tt;
    };
    QJsonObject o;
    o[QStringLiteral("schema")] = QStringLiteral("pinpoint.analysis/3");
    // Which way each decision went (AnalysisPath): the versions below say what produced a
    // track, this says whether this run produced it or loaded it, and why.
    if (!a.path.pose.isEmpty() || !a.path.ladder.isEmpty()) {
        const auto put = [](QJsonObject &j, const char *k, const QString &v) {
            if (!v.isEmpty()) j.insert(QString::fromLatin1(k), v);
        };
        QJsonObject frames;
        put(frames, "faceOn", a.path.framesFaceOn);
        put(frames, "dtl", a.path.framesDtl);
        QJsonObject reuse;
        for (auto it = a.path.reuse.cbegin(); it != a.path.reuse.cend(); ++it)
            reuse.insert(it.key(), it.value());
        QJsonObject p{ { QStringLiteral("origin"), a.path.origin },
                       { QStringLiteral("forced"), a.path.forced },
                       { QStringLiteral("fullWindow"), a.path.fullWindow },
                       { QStringLiteral("overrides"), QJsonArray::fromStringList(a.path.overrides) },
                       { QStringLiteral("frames"), frames } };
        if (!reuse.isEmpty()) p.insert(QStringLiteral("reuse"), reuse);
        put(p, "pose", a.path.pose);
        put(p, "ladder", a.path.ladder);
        put(p, "witness", a.path.witness);
        put(p, "trackerSpan", a.path.trackerSpan);
        o[QStringLiteral("path")] = p;
    }
    // Producer versions (analysis_versions.h): what produced pose / ball / shaft.
    // Re-analysis reuses a recorded stage output when these match the code that
    // would run now. Additive — absent on swings analysed before 2026-09-09.
    if (a.versions.stamped()) {
        QJsonObject v{
            { QStringLiteral("pose"),  QJsonObject{ { QStringLiteral("code"),  a.versions.pose },
                                                    { QStringLiteral("model"), a.versions.poseModel },
                                                    { QStringLiteral("scope"), a.versions.poseScope } } },
            { QStringLiteral("ball"),  QJsonObject{ { QStringLiteral("code"),  a.versions.ball } } },
            { QStringLiteral("shaft"), QJsonObject{ { QStringLiteral("code"),  a.versions.shaft } } },
            { QStringLiteral("impact"), QJsonObject{ { QStringLiteral("code"), a.versions.impact } } } };
        // The down-the-line stages, ONLY when they ran — a single-camera swing's block is
        // byte-identical to before they existed.
        if (a.versions.poseDtl > 0)
            v.insert(QStringLiteral("poseDtl"),  QJsonObject{ { QStringLiteral("code"),  a.versions.poseDtl },
                                                              { QStringLiteral("model"), a.versions.poseDtlModel } });
        if (a.versions.shaftDtl > 0)
            v.insert(QStringLiteral("shaftDtl"), QJsonObject{ { QStringLiteral("code"), a.versions.shaftDtl } });
        if (a.versions.shaftFusion > 0)
            v.insert(QStringLiteral("shaftFusion"), QJsonObject{ { QStringLiteral("code"), a.versions.shaftFusion } });
        if (a.versions.skeleton3d > 0)
            v.insert(QStringLiteral("skeleton3d"), QJsonObject{ { QStringLiteral("code"), a.versions.skeleton3d } });
        if (a.versions.addressMarks > 0)
            v.insert(QStringLiteral("addressMarks"), QJsonObject{ { QStringLiteral("code"), a.versions.addressMarks } });
        if (a.versions.dtlPosture > 0)
            v.insert(QStringLiteral("dtlPosture"), QJsonObject{ { QStringLiteral("code"), a.versions.dtlPosture } });
        if (a.versions.shaftDirection > 0)
            v.insert(QStringLiteral("shaftDirection"), QJsonObject{ { QStringLiteral("code"), a.versions.shaftDirection } });
        if (a.versions.skeleton3dBody > 0)
            v.insert(QStringLiteral("skeleton3dBody"), QJsonObject{ { QStringLiteral("code"), a.versions.skeleton3dBody } });
        if (a.versions.hipInternalRotation > 0)
            v.insert(QStringLiteral("hipInternalRotation"), QJsonObject{ { QStringLiteral("code"), a.versions.hipInternalRotation } });
        if (a.versions.shoulderTiltVsBend > 0)
            v.insert(QStringLiteral("shoulderTiltVsBend"), QJsonObject{ { QStringLiteral("code"), a.versions.shoulderTiltVsBend } });
        if (a.versions.clubSlotTop > 0)
            v.insert(QStringLiteral("clubSlotTop"), QJsonObject{ { QStringLiteral("code"), a.versions.clubSlotTop } });
        o[QStringLiteral("versions")] = v;
    }
    o[QStringLiteral("tier")]   = a.tier;
    o[QStringLiteral("score")]  = serializeScore(a.score);

    QJsonArray metrics;
    for (const MetricSeries &m : a.series) {
        QJsonArray ts, vs, samples;
        for (const int64_t t : m.t_us) ts.append(rel(t));
        for (const double v : m.value) vs.append(v);
        for (const PhaseSample &ps : m.phaseSamples) {
            QJsonObject po{ { QStringLiteral("phase"), int(ps.phase) },
                            { QStringLiteral("t_us"),  rel(ps.t_us) },
                            { QStringLiteral("value"), ps.value },
                            { QStringLiteral("band"),  ps.band } };
            analysis::insertPhaseSampleSigma(po, ps);   // per-instant σ — only when set
            samples.append(po);
        }
        QJsonObject mo{ { QStringLiteral("key"),   m.key },
                        { QStringLiteral("label"), m.label },
                        { QStringLiteral("unit"),  m.unit },
                        { QStringLiteral("t_us"),  ts },
                        { QStringLiteral("value"), vs },
                        { QStringLiteral("phaseSamples"), samples } };
        // 1σ measurement uncertainty — emitted ONLY when the producer actually
        // propagated an error budget. Absent means "not characterised", never
        // "zero", so every metric that predates this field serialises exactly as
        // before (the optional-absence contract at the serialization layer).
        if (m.sigma)
            mo.insert(QStringLiteral("sigma"), *m.sigma);
        if (m.sigmaKind > 0)
            mo.insert(QStringLiteral("sigmaKind"), int(m.sigmaKind));
        // Per-sample validity, parallel to t_us (design §5.1): 0 marks a sample the grid
        // BRIDGED across a gated or absent run, so a reader knows which part of the curve
        // was measured and which was drawn between measurements. Same optional-absence
        // contract as `sigma` directly above — emitted ONLY when at least one sample is
        // actually invalid, so every metric with nothing to mark serialises byte-identically
        // to before the field existed. The QML bridge (shot_processor.cpp toAnalysisDetail)
        // and the reload (disk_replay_source.cpp) carry the same key on the same rule.
        if (std::find(m.valid.begin(), m.valid.end(), uint8_t(0)) != m.valid.end()) {
            QJsonArray vd;
            for (const uint8_t f : m.valid) vd.append(int(f));
            mo.insert(QStringLiteral("valid"), vd);
        }
        // The producer route and its quality (MetricSeries::routeId), same optional-absence
        // contract: written only when the producer stated a route, so every other series
        // serialises byte-identically. The phase grid (measure_sample.cpp) is the reader.
        if (!m.routeId.isEmpty())
            mo.insert(QStringLiteral("routeId"), m.routeId);
        if (m.routeDirect)
            mo.insert(QStringLiteral("routeDirect"), true);
        // Drawn but not judged (MetricSeries::withheld), same contract: only when set.
        if (!m.withheld.isEmpty())
            mo.insert(QStringLiteral("withheld"), m.withheld);
        // An angle of a line (MetricSeries::lineAngle), same contract: only when set.
        if (m.lineAngle)
            mo.insert(QStringLiteral("lineAngle"), true);
        metrics.append(mo);
    }
    o[QStringLiteral("metrics")] = metrics;

    // The kinematic sequence (kinematic_sequence_json.h) — the ordered peaks over the four
    // segment angular-speed series above. ABSENT when nothing produced a node, so a swing with no
    // downswing ladder serialises exactly as before the object existed. Additive (schema history
    // 2026-09-17); the live map (shot_processor.cpp) and the reload (disk_replay_source.cpp) carry
    // the same shape through the same helper.
    if (a.kinematicSequence.valid)
        o[QStringLiteral("kinematicSequence")] =
            analysis::kinematicSequenceToJson(a.kinematicSequence, rel);

    // Gate records (gate_record.h; trust_and_gates_design.md §6) — why readings were refused,
    // bounded or withheld. Written whenever an emitting stage ran, even with no record, so a reader
    // can tell "no gate fired" (empty array) from "analysed before records existed" (both absent).
    // Evidence times are ms from impact and need no re-timing. Additive (schema history 2026-10-16).
    if (a.gatesVersion > 0) {
        o[QStringLiteral("gatesVersion")] = a.gatesVersion;
        o[QStringLiteral("gates")]        = analysis::gateRecordsToJson(a.gates);
    }

    // `timing` (TimingClass — how the instant was OBTAINED, orthogonal to conf)
    // is written ONLY on a fusion-arbitrated ladder (segmentation.version >= 5).
    // Every producer stamps the field in memory regardless, but emitting it
    // unconditionally would put a new key on every phase of every swing and
    // break the refine.fusion=false parity baseline the corpus gate rests on
    // (timeline-fusion.md §8 gate 2). Absent ⇒ Measured, which is what the
    // pre-fusion producers all effectively claimed.
    const bool fused = a.segmentation.version >= 5;
    QJsonArray phases;
    for (const PhaseEvent &e : a.phases) {
        QJsonObject po{ { QStringLiteral("phase"),   int(e.phase) },
                        { QStringLiteral("t_us"),    rel(e.t_us) },
                        { QStringLiteral("conf"),    e.conf },
                        { QStringLiteral("segment"), int(e.provenance) } };
        if (fused)
            po.insert(QStringLiteral("timing"), int(e.timing));
        phases.append(po);
    }
    o[QStringLiteral("phases")] = phases;

    // Additive "assessment" block = the AI-COACH feedback feed (design §B.0): lead-wrist
    // faults/strengths, decoupled from the headline resemblance score. Written whenever the
    // assessment stage ran (it needs IMU streams and wrist series) — live capture, re-analysis
    // and SwingLab alike (#110) — and the SwingLab known-groups diagnosis input. score.py reads `findings[]` for fault recall / FP-on-clean. scoreV2 is
    // retained as telemetry only — it is NOT a score (its confidence-coupled central term is
    // removed in WP-4); the resemblance "score" object above is the Wrist headline.
    if (a.assessmentScore >= 0) {
        QJsonArray findings;
        for (const PpWristFinding &f : a.findings) {
            QJsonArray dofs, positions;
            for (const PpJointDof d : f.dofs)      dofs.append(int(d));
            for (const PpSwingPosition p : f.positions) positions.append(int(p));
            findings.append(QJsonObject{
                { QStringLiteral("id"),            f.id },
                { QStringLiteral("name"),          f.name },
                { QStringLiteral("category"),      f.category },
                { QStringLiteral("severity"),      QString::fromLatin1(severityName(f.severity)) },
                { QStringLiteral("magnitudeDeg"),  f.magnitudeDeg },
                { QStringLiteral("weight"),        f.weight },
                { QStringLiteral("confidence"),    double(f.confidence) },
                { QStringLiteral("lowConfidence"), f.lowConfidence },
                { QStringLiteral("dofs"),          dofs },
                { QStringLiteral("positions"),     positions } });
        }
        o[QStringLiteral("assessment")] = QJsonObject{
            { QStringLiteral("scoreV2"),  a.assessmentScore },
            { QStringLiteral("findings"), findings } };
    }

    // Additive orientation-filter quality block (SwingLab filter.refuse — §5.3.1). Only present when
    // offline re-fusion drove the orientation, giving filter.* an IMU-only objective (score.py filter.*).
    if (a.filterImpactStepDeg >= 0.0)
        o[QStringLiteral("filter")] = QJsonObject{
            { QStringLiteral("impactStepDeg"), a.filterImpactStepDeg } };

    // Additive per-stage analyzer wall times (plan §2 telemetry): how long each
    // heavy stage took, so every live shot self-reports the < 20 s budget. Only
    // when measured (totalMs >= 0); a stage that did not run stays -1.
    if (a.timings.totalMs >= 0) {
        QJsonObject timings{
            { QStringLiteral("poseMs"),  a.timings.poseMs },
            { QStringLiteral("ballMs"),  a.timings.ballMs },
            { QStringLiteral("shaftMs"), a.timings.shaftMs },
            { QStringLiteral("impactMs"), a.timings.impactMs },
            { QStringLiteral("totalMs"), a.timings.totalMs } };
        // The DTL pose pass (DtlPoseStage, smoothing included) — measured since the
        // stage existed and never written; -1 = it did not run.
        timings.insert(QStringLiteral("poseDtlMs"), a.timings.poseDtlMs);
        // Where each camera's pose pass went (pose_inference_performance_plan.md
        // step 0): session build / decode / preprocess / Run() / heatmap decode /
        // wall total / frames. Only for a camera actually posed this run — a reused
        // or loaded track has no split, and a zero split would read as "free".
        QJsonObject pose;
        if (a.timings.poseFaceOn.measured())
            pose.insert(QStringLiteral("faceOn"), a.timings.poseFaceOn.toJson());
        if (a.timings.poseDtl.measured())
            pose.insert(QStringLiteral("dtl"), a.timings.poseDtl.toJson());
        if (!pose.isEmpty())
            timings.insert(QStringLiteral("pose"), pose);
        // Every stage as it ran (analysis_dag_design.md step A): wall ms, start/end offset
        // from the start of the analysis, the pool thread, and a skip's reason — the timeline
        // tools/analysis/analysis_dag.py draws. 0.01 ms is below anything a stage measures
        // and keeps a 39-stage list near 4 kB.
        if (!a.timings.stages.empty()) {
            const auto r2 = [](double v) { return double(qRound64(v * 100.0)) / 100.0; };
            QJsonArray stages;
            for (const analysis::StageTiming &s : a.timings.stages) {
                QJsonObject so{
                    { QStringLiteral("name"),    s.name },
                    { QStringLiteral("ran"),     s.ran },
                    { QStringLiteral("ms"),      r2(s.ms) },
                    { QStringLiteral("startMs"), r2(s.startMs) },
                    { QStringLiteral("endMs"),   r2(s.endMs) },
                    { QStringLiteral("thread"),  s.thread } };
                if (!s.source.isEmpty())
                    so.insert(QStringLiteral("source"), s.source);
                if (!s.skipReason.isEmpty())
                    so.insert(QStringLiteral("skipReason"), s.skipReason);
                stages.append(so);
            }
            timings.insert(QStringLiteral("stages"), stages);
        }
        o[QStringLiteral("timings")] = timings;
    }

    // Additive segmentation block (v3 G2, design A.7): the swing bounds +
    // ladder meta. Missing block on reload = full-window bounds.
    if (a.segmentation.swingEndUs > a.segmentation.swingStartUs) {
        QJsonObject seg{
            { QStringLiteral("swingStartUs"), rel(a.segmentation.swingStartUs) },
            { QStringLiteral("swingEndUs"),   rel(a.segmentation.swingEndUs) },
            { QStringLiteral("conf"),    double(a.segmentation.conf) },
            { QStringLiteral("version"), a.segmentation.version } };
        // Additive fusion audit trail (timeline_fusion.h): one entry per
        // arbitrated P-slot, RETENTIONS INCLUDED — the club-vs-IMU delta on a
        // slot that did NOT flip is exactly the calibration data the V2 σ path
        // needs (timeline-fusion.md §5, §9.2), and discarding it would repeat the
        // mistake this design exists to fix. Present only when a fusion pass
        // emitted something, so a fusion-dark or all-abstain run writes nothing.
        // `phase` is the Phase enum int (as phases[].phase), NOT the coaching
        // P-index; winner/loser are SegmentRole; reason is FusionReason.
        if (!a.segmentation.fusion.empty()) {
            QJsonArray fusion;
            for (const analysis::FusionDecision &d : a.segmentation.fusion)
                fusion.append(QJsonObject{
                    { QStringLiteral("phase"),  int(d.phase) },
                    { QStringLiteral("winner"), int(d.winner) },
                    { QStringLiteral("loser"),  int(d.loser) },
                    { QStringLiteral("dtUs"),   qint64(d.deltaUs) },
                    { QStringLiteral("reason"), int(d.reason) } });
            seg.insert(QStringLiteral("fusion"), fusion);
        }
        o[QStringLiteral("segmentation")] = seg;
    }

    // Additive IMU-binding snapshot (SwingLab): the per-device calibration
    // (A/M) keyed by device serial, so the offline runner can re-fuse a
    // recorded swing with the exact anatomical transforms the app used.
    if (!a.bindings.empty()) {
        QJsonArray binds;
        for (const BindingRecord &b : a.bindings) {
            QJsonObject bo{
                { QStringLiteral("serial"),   b.serial },
                { QStringLiteral("role"),     int(b.role) },
                { QStringLiteral("roleName"), segmentRoleName(b.role) },
                { QStringLiteral("alignA"), QJsonArray{ b.alignA.scalar(), b.alignA.x(),
                                                        b.alignA.y(), b.alignA.z() } },
                { QStringLiteral("mountM"), QJsonArray{ b.mountM.scalar(), b.mountM.x(),
                                                        b.mountM.y(), b.mountM.z() } },
                // Calibration status at shot time (additive — SwingLab corpus
                // provenance). calibAgeSec -1 / empty calibratedAt = never.
                { QStringLiteral("calibrated"),           b.calibrated },
                { QStringLiteral("anatCalibrated"),       b.anatCalibrated },
                { QStringLiteral("mountDeviationDeg"),    b.mountDeviationDeg },
                { QStringLiteral("mountGravityErrorDeg"), b.mountGravityErrorDeg },
                { QStringLiteral("calibratedAt"),         b.calibratedAtUtc },
                { QStringLiteral("calibAgeSec"),          b.calibAgeSec },
                // ⚠ WHICH INSTRUMENT, and it is load-bearing on re-analysis rather
                // than merely informative. This array takes precedence over the
                // per-stream fallback when a swing is re-read, so without this key a
                // re-analysed HackMotion swing would skip the raw-quaternion conjugate
                // and invert every wrist sign. See BindingRecord::hackMotion.
                { QStringLiteral("hackMotion"),           b.hackMotion } };
            // The trunk stand-tall solve, when it set A/M (trunk design §6.8): calibrationSource
            // + trunkSolve, both absent otherwise (trunk_calib_provenance.h).
            writeTrunkCalibProvenance(bo, b.trunkCalib);
            binds.append(bo);
        }
        o[QStringLiteral("bindings")] = binds;
    }

    // Additive ShaftTracker blocks (S3). pose2d keypoints are already
    // normalized 0..1 frame coords; club grip/head are normalized here by the
    // camera dims so every consumer (replay overlay, reload) is
    // resolution-independent. The club block is written for a VALID track and
    // for a REFUSED one (valid=false + `refused`, kept for the lab); consumers
    // gate on `valid`, so a refused track still draws nothing.
    if (!a.pose2d.frames.empty())
        o[QStringLiteral("pose2d")] = poseTrackToJson(a.pose2d, windowT0);
    // The DOWN-THE-LINE pose (DtlPoseStage) in the same shape, for the DTL replay tile and
    // for re-analysis reuse (swing_reanalyzer.cpp). Absent when no DTL pose ran.
    if (!a.poseDtl.frames.empty())
        o[QStringLiteral("poseDtl")] = poseTrackToJson(a.poseDtl, windowT0);
    // A REFUSED track (2026-10-01) is written too — valid=false with `refused` and `diag`,
    // samples kept for the lab — so the reason survives into the ⚠ and the reload. A track
    // that merely failed the coverage gate is still omitted, as before.
    if ((a.shaft.valid || a.shaft.refusedReason) && !a.shaft.samples.empty()
        && a.shaft.frameWidth > 0 && a.shaft.frameHeight > 0) {
        const double iw = 1.0 / a.shaft.frameWidth, ih = 1.0 / a.shaft.frameHeight;
        QJsonArray samples;
        for (const ShaftSample2D &s : a.shaft.samples) {
            QJsonObject so{
                { QStringLiteral("t_us"),  rel(s.t_us) },
                { QStringLiteral("grip"),  QJsonArray{ s.gripPx.x() * iw, s.gripPx.y() * ih } },
                { QStringLiteral("head"),  QJsonArray{ s.headPx.x() * iw, s.headPx.y() * ih } },
                { QStringLiteral("theta"), s.thetaRad },
                { QStringLiteral("thetaDot"), s.thetaDotRadS },
                { QStringLiteral("lenPx"), s.visibleLenPx },
                { QStringLiteral("conf"),  double(s.conf) },
                // Stage-2 head confidence + posterior σ (Phase B; −1 = head pass
                // off). Absent in older files ⇒ reader defaults −1 (toVariantMap
                // simply omits the key; consumers treat missing as −1).
                { QStringLiteral("headConf"),  double(s.headConf) },
                { QStringLiteral("headSigma"), double(s.headSigmaPx) },
                { QStringLiteral("flags"), int(s.flags) } };
            // Layer A snap registration (shaft_position_first §2A): ridge support
            // under the drawn line. Written ONLY when measured (≥0) so a snap-off
            // run stays byte-identical; absent ⇒ reader defaults −1.
            if (s.lineConf >= 0.f) so.insert(QStringLiteral("lineConf"), double(s.lineConf));
            analysis::insertShaftSigma(so, s);   // uncertainty design §7 — only when assessed
            samples.append(so);
        }
        // R7 dual output (additive): the pure-model predicted series + its
        // agreement with the prior-free vision measurement. Same normalized shape
        // as `samples`; SwingLab consumes it for residual analysis / β̂ calibration.
        QJsonArray predicted;
        for (const ShaftSample2D &s : a.shaft.predicted)
            predicted.append(QJsonObject{
                { QStringLiteral("t_us"),  rel(s.t_us) },
                { QStringLiteral("grip"),  QJsonArray{ s.gripPx.x() * iw, s.gripPx.y() * ih } },
                { QStringLiteral("head"),  QJsonArray{ s.headPx.x() * iw, s.headPx.y() * ih } },
                { QStringLiteral("theta"), s.thetaRad },
                { QStringLiteral("lenPx"), s.visibleLenPx },
                { QStringLiteral("conf"),  double(s.conf) },
                { QStringLiteral("flags"), int(s.flags) } });
        // Layer C synthesized series (shaft_position_first §2 Layer C): the
        // VISUALIZATION-tier interpolation between P-anchors, each flagged
        // ShaftSynthesized (0x100). Same normalized shape as `samples` MINUS lineConf
        // (synthesis has no ridge registration). Written only when non-empty
        // (synth off / < 2 anchors ⇒ absent, block byte-identical). Consumers must
        // EXCLUDE these from metrics/scoring by the flag.
        QJsonArray synth;
        for (const ShaftSample2D &s : a.shaft.synth) {
            QJsonObject so{
                { QStringLiteral("t_us"),  rel(s.t_us) },
                { QStringLiteral("grip"),  QJsonArray{ s.gripPx.x() * iw, s.gripPx.y() * ih } },
                { QStringLiteral("head"),  QJsonArray{ s.headPx.x() * iw, s.headPx.y() * ih } },
                { QStringLiteral("theta"), s.thetaRad },
                { QStringLiteral("thetaDot"), s.thetaDotRadS },
                { QStringLiteral("lenPx"), s.visibleLenPx },
                { QStringLiteral("conf"),  double(s.conf) },
                { QStringLiteral("headConf"),  double(s.headConf) },
                { QStringLiteral("headSigma"), double(s.headSigmaPx) },
                { QStringLiteral("flags"), int(s.flags) } };
            analysis::insertShaftSigma(so, s);
            synth.append(so);
        }
        // Multi-estimator club-length fusion (club_length_fusion.h) — identical
        // shape in both parity writers (shot_processor.cpp toLengthsDetail is the
        // live-detail twin). Always written, even on abstain (nEstimators==0,
        // fusedPx<0): readers treat <0 as absent.
        const analysis::ClubLengthEstimate &l = a.shaft.lengths;
        const QJsonObject lengths{
            { QStringLiteral("ballPx"),           l.ballPx },
            { QStringLiteral("bandPx"),           l.bandPx },
            { QStringLiteral("headP95Px"),        l.headPx },
            { QStringLiteral("posePx"),           l.posePx },
            { QStringLiteral("priorPx"),          l.priorPx },
            { QStringLiteral("fusedPx"),          l.fusedPx },
            { QStringLiteral("fusedSigmaPx"),     l.fusedSigmaPx },
            { QStringLiteral("fusedConf"),        l.fusedConf },
            { QStringLiteral("fusedInstantPx"),   l.fusedInstantPx },
            { QStringLiteral("fusedInstantConf"), l.fusedInstantConf },
            { QStringLiteral("ladderRung"),       l.ladderRung },
            { QStringLiteral("ladderLenPx"),      l.ladderLenPx },
            { QStringLiteral("nEstimators"),      l.nEstimators },
            { QStringLiteral("priorN"),           l.priorN },
            { QStringLiteral("headMeasN"),        l.headMeasN } };
        // Coaching P-positions P1–P8 (shaft_position_first §2 Layer B) — grip/head
        // normalized by the camera dims like `samples`; t_us window-relative via
        // rel(). Written only when non-empty (positions extraction off / pre-v3.5
        // ⇒ absent, so the block stays byte-identical). "image-plane parallel"
        // (P2/P6/P8) is accepted face-on coaching practice, not 3-D geometry.
        QJsonArray positions;
        for (const ShaftPosition &p : a.shaft.positions) {
            QJsonObject po{
                { QStringLiteral("p"),     p.p },
                { QStringLiteral("t_us"),  rel(p.t_us) },
                { QStringLiteral("grip"),  QJsonArray{ p.gripPx.x() * iw, p.gripPx.y() * ih } },
                { QStringLiteral("head"),  QJsonArray{ p.headPx.x() * iw, p.headPx.y() * ih } },
                { QStringLiteral("theta"), p.thetaRad },
                { QStringLiteral("lenPx"), p.lenPx },
                { QStringLiteral("conf"),  double(p.conf) },
                { QStringLiteral("sigmaThetaDeg"), double(p.sigmaThetaDeg) },
                { QStringLiteral("sigmaLenPx"),    double(p.sigmaLenPx) },
                { QStringLiteral("stackN"), p.stackN },
                { QStringLiteral("source"), int(p.source) },
                // How the instant was OBTAINED (TimingClass) — the follow-through synth gate
                // reads it (2026-09-17). Additive; readers default to Measured.
                { QStringLiteral("timing"), int(p.timing) } };
            if (p.sigmaTUs >= 0.f) po.insert(QStringLiteral("sigmaTUs"), double(p.sigmaTUs));
            positions.append(po);
        }
        // Face-on swing-plane transition delta (shaft_plane.h). Written ALWAYS,
        // even when nothing fitted, so a reader can tell "the producer ran and
        // found nothing" (valid false, channel -1, per-window reject codes set)
        // from "this file predates the producer" (the key is absent). Quality is
        // per channel and must not be compared across them: splitHalf* is the
        // measured tier's error bar and is meaningless on a Hermite; anchors*/
        // anchorConfMin are the synth tier's only honest quality.
        auto planeChan = [](const analysis::ShaftPlaneChannel &c) {
            return QJsonObject{
                { QStringLiteral("fitted"),           c.fitted },
                { QStringLiteral("iotaBackDeg"),      c.iotaBackDeg },
                { QStringLiteral("iotaDownDeg"),      c.iotaDownDeg },
                { QStringLiteral("deltaDeg"),         c.deltaDeg },
                { QStringLiteral("nodeBackDeg"),      c.nodeBackDeg },
                { QStringLiteral("nodeDownDeg"),      c.nodeDownDeg },
                { QStringLiteral("nBack"),            c.nBack },
                { QStringLiteral("nDown"),            c.nDown },
                { QStringLiteral("conicResidBack"),   c.conicResidBack },
                { QStringLiteral("conicResidDown"),   c.conicResidDown },
                { QStringLiteral("ratioBack"),        c.ratioBack },
                { QStringLiteral("ratioDown"),        c.ratioDown },
                { QStringLiteral("splitHalfBackDeg"), c.splitHalfBackDeg },
                { QStringLiteral("splitHalfDownDeg"), c.splitHalfDownDeg },
                { QStringLiteral("anchorsBack"),      c.anchorsBack },
                { QStringLiteral("anchorsDown"),      c.anchorsDown },
                { QStringLiteral("anchorConfMin"),    double(c.anchorConfMin) },
                { QStringLiteral("rejectBack"),       c.rejectBack },
                { QStringLiteral("rejectDown"),       c.rejectDown } };
        };
        // Uncertainty design §4.7: the block-bootstrap σ, written only when assessed.
        auto planeChanSig = [&planeChan](const analysis::ShaftPlaneChannel &c) {
            QJsonObject o = planeChan(c);
            if (c.sigmaIotaBackDeg >= 0.0) o.insert(QStringLiteral("sigmaIotaBackDeg"), c.sigmaIotaBackDeg);
            if (c.sigmaIotaDownDeg >= 0.0) o.insert(QStringLiteral("sigmaIotaDownDeg"), c.sigmaIotaDownDeg);
            if (c.sigmaDeltaDeg    >= 0.0) o.insert(QStringLiteral("sigmaDeltaDeg"),    c.sigmaDeltaDeg);
            if (c.pNeedle          >= 0.0) o.insert(QStringLiteral("pNeedle"),          c.pNeedle);
            return o;
        };
        const QJsonObject plane{
            { QStringLiteral("valid"),    a.shaft.plane.valid },
            { QStringLiteral("channel"),  a.shaft.plane.channel },
            { QStringLiteral("measured"), planeChanSig(a.shaft.plane.measured) },
            { QStringLiteral("synth"),    planeChanSig(a.shaft.plane.synth) } };
        QJsonObject clubObj{
            { QStringLiteral("camera"),        int(a.shaft.camera) },
            { QStringLiteral("valid"),         a.shaft.valid },
            { QStringLiteral("coverage"),      double(a.shaft.coverage) },
            { QStringLiteral("imuVisionCorr"), double(a.shaft.imuVisionCorr) },
            { QStringLiteral("modelVisionResidualDeg"), double(a.shaft.modelVisionResidualDeg) },
            // v3.4 (design §9.4): measured club length in px (grip-to-ball at
            // address) — a scale floor. -1 = unmeasured (no ball anchor).
            { QStringLiteral("measuredClubLenPx"), double(a.shaft.measuredClubLenPx) },
            { QStringLiteral("frameWidth"),    a.shaft.frameWidth },
            { QStringLiteral("frameHeight"),   a.shaft.frameHeight },
            { QStringLiteral("lengths"),       lengths },
            { QStringLiteral("plane"),         plane },
            { QStringLiteral("samples"),       samples },
            { QStringLiteral("predicted"),     predicted } };
        if (!positions.isEmpty()) clubObj.insert(QStringLiteral("positions"), positions);
        if (!synth.isEmpty())     clubObj.insert(QStringLiteral("synth"), synth);
        if (a.shaft.ballAnchored)
            clubObj.insert(QStringLiteral("addressBall"),
                           QJsonArray{ a.shaft.addressBallPx.x() * iw, a.shaft.addressBallPx.y() * ih });
        if (a.shaft.addressBallSigmaPx >= 0.f)
            clubObj.insert(QStringLiteral("addressBallSigmaPx"), double(a.shaft.addressBallSigmaPx));
        if (a.shaft.synthKappa >= 0.f)
            clubObj.insert(QStringLiteral("synthKappa"), double(a.shaft.synthKappa));
        // Robustness self-checks (2026-10-01, swing_analysis.h ShaftTrack2D): how the
        // phase model found its takeaway, whether it was suspect, and the two witness
        // checks. `refused` names the reason when the track was refused (valid=false).
        clubObj.insert(QStringLiteral("diag"), QJsonObject{
            { QStringLiteral("onsetRule"),      a.shaft.onsetRule },
            { QStringLiteral("phaseRetries"),   a.shaft.phaseRetries },
            { QStringLiteral("phaseSuspect"),   a.shaft.phaseSuspect },
            { QStringLiteral("p1BallDeltaDeg"), double(a.shaft.p1BallDeltaDeg) },
            { QStringLiteral("lenBallRatio"),   double(a.shaft.lenBallRatio) },
            { QStringLiteral("onsetTUs"),       a.shaft.onsetTUs >= 0 ? rel(a.shaft.onsetTUs) : -1.0 },
            { QStringLiteral("topTUs"),         a.shaft.topTUs   >= 0 ? rel(a.shaft.topTUs)   : -1.0 },
            { QStringLiteral("handPairFixed"),  a.shaft.handPairFixed },
            { QStringLiteral("handGlitchFixed"), a.shaft.handGlitchFixed },
            { QStringLiteral("ballSuspect"),    a.shaft.ballSuspect },
            { QStringLiteral("handsRetried"),   a.shaft.handsRetried } });
        // Why the tracker found no P8 (plane stack D6), only when it found none: a reused track keeps it.
        if (a.shaft.p8Refusal) {
            QJsonObject dg = clubObj.value(QStringLiteral("diag")).toObject();
            dg.insert(QStringLiteral("p8Refusal"), QString::fromLatin1(analysis::shaftP8RefusalName(a.shaft.p8Refusal)));
            clubObj.insert(QStringLiteral("diag"), dg);
        }
        if (a.shaft.refusedReason)
            clubObj.insert(QStringLiteral("refused"),
                           QString::fromLatin1(analysis::shaftRefusedReasonName(a.shaft.refusedReason)));
        // The blurred frames' timed edge readings (ShaftWedgeObs) as [t_us, theta, kind, sigmaDeg],
        // kind 0 trail / 1 mid / 2 lead. Written only when non-empty.
        if (!a.shaft.wedgeObs.empty()) {
            QJsonArray wo;
            for (const ShaftWedgeObs &w : a.shaft.wedgeObs)
                wo.append(QJsonArray{ rel(w.t_us), w.thetaRad, int(w.kind), double(w.sigmaDeg) });
            clubObj.insert(QStringLiteral("wedgeObs"), wo);
        }
        // The club rate filter's diagnostic (club_rate_filter.h) — developer only, written only when
        // the filter ran, so a filter-off document is byte-identical. shot_processor.cpp mirrors it.
        if (a.shaft.clubFilter.ran) {
            const analysis::ClubFilterStats &f = a.shaft.clubFilter;
            clubObj.insert(QStringLiteral("filter"), QJsonObject{
                { QStringLiteral("version"),        f.version },
                { QStringLiteral("nEvidence"),      f.nEvidence },
                { QStringLiteral("nGated"),         f.nGated },
                { QStringLiteral("nDownweighted"),  f.nDownweighted },
                { QStringLiteral("nisMean"),        f.nisMean },
                { QStringLiteral("residualRmsDeg"), f.residualRmsDeg },
                { QStringLiteral("rateWobble"),     f.rateWobble },
                { QStringLiteral("humpsRaw"),       f.humpsRaw },
                { QStringLiteral("humpsFiltered"),  f.humpsFiltered } });
        }
        o[QStringLiteral("club")] = clubObj;
    }
    // Ball track (v3.4 design §9) for the replay overlay — normalized [0,1]
    // full-frame center + radius; rel() keeps t_us window-relative like pose2d/
    // club. found=false marks the post-launch gap. Read by disk_replay_source
    // (replay overlay) and SwingDocReader.
    if (!a.ball.frames.empty()) {
        QJsonArray samples;
        for (const BallSample2D &s : a.ball.frames) {
            QJsonObject so{
                { QStringLiteral("t_us"),  rel(s.t_us) },
                { QStringLiteral("x"),     s.center.x() },
                { QStringLiteral("y"),     s.center.y() },
                { QStringLiteral("r"),     double(s.radiusNorm) },
                { QStringLiteral("conf"),  double(s.conf) },
                { QStringLiteral("found"), s.found } };
            // W3 club-corridor activity — additive "act" per sample ONLY when it
            // was computed (>= 0); a dark ball.clubActivity run leaves clubActivity
            // at -1 so the field is absent and the file is byte-identical to pre-W3.
            if (s.clubActivity >= 0.f)
                so.insert(QStringLiteral("act"), double(s.clubActivity));
            samples.append(so);
        }
        QJsonObject ballObj{
            { QStringLiteral("camera"),    int(a.ball.camera) },
            { QStringLiteral("valid"),     true },
            { QStringLiteral("launchTUs"), rel(a.ball.launchTUs) },
            { QStringLiteral("samples"),   samples } };
        // Additive (2026-09-09): the pre-launch ball centre the impact anchor reads,
        // so a reloaded track carries it (BallRunner::fromAnalysisJson).
        if (a.ball.launchTUs >= 0)
            ballObj.insert(QStringLiteral("launch"), QJsonObject{ { QStringLiteral("x"), a.ball.launchCenter.x() },
                                                                  { QStringLiteral("y"), a.ball.launchCenter.y() } });
        o[QStringLiteral("ball")] = ballObj;
    }
    // The impact camera's track (impact_camera_design.md §7) — absent when no
    // impact camera ran. Same object the live detail carries (impactTrackJson).
    if (a.impact.valid)
        o[QStringLiteral("impact")] = impactTrackJson(a.impact, windowT0);
    // The down-the-line club track (DtlShaftStage) — pinpoint.clubDtl/1, the very bytes
    // SwingLab writes to club_dtl.json (dtl_shaft_json.h). Written whenever the stage RAN
    // (versions.shaftDtl), valid or not: an invalid track's `summary` and per-frame `reason`
    // are the record of why the DTL tile shows no shaft. Absent when it did not run.
    if (a.versions.shaftDtl > 0)
        o[QStringLiteral("clubDtl")] = dtlShaftTrackToJson(
            a.shaftDtl, windowT0, a.shaftDtl.configJson, a.shaftDtl.configHash, dtlAlias, dtlFile);
    // The fused 3-D shaft (ShaftFusionStage) — pinpoint.club3d/1. Written whenever the stage
    // RAN, valid or not, for clubDtl's reason: the counts are the record of why there is none.
    if (a.versions.shaftFusion > 0)
        o[QStringLiteral("club3d")] = analysis::shaftTrack3dToJson(
            a.shaft3d, a.shaft3dCfg, windowT0, a.versions.shaftFusion);
    // The fitted 3-D skeleton (Skeleton3DStage) — pinpoint.skeleton3d/1, the 3-D swing panel's
    // input. Written whenever the stage RAN, valid or not: `reason` says why there is none.
    if (a.versions.skeleton3d > 0)
        o[QStringLiteral("skeleton3d")] = pinpoint::skeleton3d::skeleton3dToJson(a.skeleton3d, windowT0, a.versions.skeleton3d);
    // The body's outer edges at hip height at address (AddressMarksStage) — pinpoint.addressMarks/2
    // (the DTL P-frames since stage version 3), the coach lines' input. Written whenever the stage
    // RAN: `found` false per view is the record.
    if (a.versions.addressMarks > 0)
        o[QStringLiteral("addressMarks")] = analysis::addressMarksToJson(a.addressMarks, windowT0, a.versions.addressMarks);
    // The DTL ruler (DtlPostureStage) — pinpoint.dtlRuler/1: the cm per DTL pixel pelvisThrust,
    // headRise and hipDepthButt were measured in, for a reader drawing centimetres on that frame.
    // Written whenever the stage RAN; `cmPerPx` null and `source` "none" is the record of no ruler.
    if (a.dtlRuler.ran)
        o[QStringLiteral("dtlRuler")] = analysis::dtlRulerToJson(a.dtlRuler, a.versions.dtlPosture);
    return o;
}

} // namespace swingdoc_detail

QJsonObject poseTrackToJson(const analysis::PoseTrack2D &t, qint64 windowT0)
{
    using namespace analysis;
    // Same domain rule as serializeAnalysis: only an absolute value is shifted.
    auto rel = [windowT0](int64_t v) -> qint64 {
        const qint64 tt = static_cast<qint64>(v);
        return tt >= windowT0 ? tt - windowT0 : tt;
    };
    QJsonArray frames;
    for (const PoseFrame2D &f : t.frames) {
        QJsonArray kp;
        for (int j = 0; j < kWholeBodyJoints; ++j) {
            kp.append(f.kp[size_t(j)].x());
            kp.append(f.kp[size_t(j)].y());
            kp.append(double(f.conf[size_t(j)]));
        }
        frames.append(QJsonObject{
            { QStringLiteral("t_us"), rel(f.t_us) },
            { QStringLiteral("kp"),   kp },
            { QStringLiteral("lead"),  QJsonArray{ f.leadHand.x(),  f.leadHand.y() } },
            { QStringLiteral("trail"), QJsonArray{ f.trailHand.x(), f.trailHand.y() } },
            { QStringLiteral("handConf"), double(f.handConf) } });
    }
    // keypointCount: explicit kp width (COCO-WholeBody 133; indices 0–16
    // are the unchanged COCO body joints, tail = feet/face/hands). Readers
    // use bounded loops so the field is provenance, not a parse contract.
    QJsonObject obj{
        { QStringLiteral("camera"), int(t.camera) },
        { QStringLiteral("keypointCount"), kWholeBodyJoints },
        { QStringLiteral("frames"), frames } };
    // WB1 accuracy-pass provenance (wholebody_pose_design.md §3). Written ONLY
    // when non-legacy — decode when DARK, cropRect when a crop was actually
    // used — so a flags-off (crop disabled + argmax) run serialises exactly as
    // the pre-WB1 tree (the WB1 byte-parity gate). cropRect is full-frame
    // normalized {x,y,w,h}.
    if (t.decode == QLatin1String("dark"))
        obj.insert(QStringLiteral("decode"), t.decode);
    if (t.cropRect) {
        const QRectF &r = *t.cropRect;
        obj.insert(QStringLiteral("cropRect"),
                      QJsonObject{ { QStringLiteral("x"), r.x() },
                                   { QStringLiteral("y"), r.y() },
                                   { QStringLiteral("w"), r.width() },
                                   { QStringLiteral("h"), r.height() } });
    }
    // Motion-overlay smoothed companion track (pose_smoother.cpp): parallel to
    // `frames` on the same t_us grid — kp[x,y,c]×133 flat exactly like `frames`
    // (conf carries the render-alpha contract) plus per-kp honesty tier[133] (int)
    // and sigma[133] (px). No lead/trail/handConf — the hands are NOT smoothed.
    // Written ONLY when non-empty (absent on swings analysed before the smoother
    // existed, or a format-less path), so an empty smoothed vector serializes
    // byte-identically to today. t_us is window-relative via rel(), same as frames'.
    if (!t.smoothed.empty()) {
        QJsonArray smoothed;
        const size_t n = std::min(t.smoothed.size(), t.smoothedAux.size());
        for (size_t i = 0; i < n; ++i) {
            const PoseFrame2D &f = t.smoothed[i];
            const PoseKpAux   &x = t.smoothedAux[i];
            QJsonArray kp, tier, sigma;
            for (int j = 0; j < kWholeBodyJoints; ++j) {
                kp.append(f.kp[size_t(j)].x());
                kp.append(f.kp[size_t(j)].y());
                kp.append(double(f.conf[size_t(j)]));
                tier.append(int(x.tier[size_t(j)]));
                sigma.append(double(x.sigma[size_t(j)]));
            }
            smoothed.append(QJsonObject{
                { QStringLiteral("t_us"),  rel(f.t_us) },
                { QStringLiteral("kp"),    kp },
                { QStringLiteral("tier"),  tier },
                { QStringLiteral("sigma"), sigma } });
        }
        obj.insert(QStringLiteral("smoothed"), smoothed);
    }
    // Phase-5 motion-adaptive window (poseSmooth.adapt.*): the count of keypoints
    // whose adaptive pass was REJECTED by the divergence guard and fell back to the
    // unadapted output. Written ONLY when non-zero — same discipline as sigma/valid —
    // so a swing analysed with the window off (or with nothing falling back)
    // serializes byte-identically. Diagnostic: nothing reads it back; it exists so a
    // sweep can refuse a setting that would have moved the segmentation.
    if (t.adaptFallbacks > 0)
        obj.insert(QStringLiteral("adaptFallbacks"), t.adaptFallbacks);
    // Dense VIZ-tier pose synth (pose_synthesis.h): the smoothed skeleton
    // upsampled to 240 Hz so the replay overlays scrub smoothly — the body
    // sibling of club.synth. Lean shape { t_us, kp[x,y,c]×133 } — no tier/sigma
    // (the overlay's conf-gate skips Off joints, which carry conf 0 here) and no
    // hands (not drawn by the body overlays). Written ONLY when non-empty, so a
    // synth-off run omits it and serializes byte-identically. Metrics NEVER read
    // it (same discipline as the measured/smoothed split above).
    if (!t.smoothedSynth.empty()) {
        QJsonArray synth;
        for (const PoseFrame2D &f : t.smoothedSynth) {
            QJsonArray kp;
            for (int j = 0; j < kWholeBodyJoints; ++j) {
                kp.append(f.kp[size_t(j)].x());
                kp.append(f.kp[size_t(j)].y());
                kp.append(double(f.conf[size_t(j)]));
            }
            synth.append(QJsonObject{
                { QStringLiteral("t_us"), rel(f.t_us) },
                { QStringLiteral("kp"),   kp } });
        }
        obj.insert(QStringLiteral("synth"), synth);
    }
    return obj;
}

QJsonObject impactTrackJson(const analysis::ImpactTrack2D &t, qint64 windowT0)
{
    // Same domain rule as serializeAnalysis: only an absolute value is shifted.
    auto rel = [windowT0](int64_t v) -> qint64 {
        const qint64 tt = static_cast<qint64>(v);
        return tt >= windowT0 ? tt - windowT0 : tt;
    };
    QJsonArray samples;
    for (const analysis::ImpactSample2D &s : t.frames) {
        QJsonObject so{ { QStringLiteral("t_us"), rel(s.t_us) } };
        if (s.ballFound) {
            so.insert(QStringLiteral("bx"), s.ball.x());
            so.insert(QStringLiteral("by"), s.ball.y());
            so.insert(QStringLiteral("br"), double(s.ballR));
        }
        if (s.clubFound) {
            so.insert(QStringLiteral("hx"), s.head.x());
            so.insert(QStringLiteral("hy"), s.head.y());
            if (s.headSeen)
                so.insert(QStringLiteral("hs"), true);
            if (s.atEdge)
                so.insert(QStringLiteral("edge"), true);
            if (!s.outline.empty()) {
                QJsonArray poly;
                for (const QPointF &p : s.outline) { poly.append(p.x()); poly.append(p.y()); }
                so.insert(QStringLiteral("poly"), poly);
            }
            so.insert(QStringLiteral("sax"), s.shaftA.x());
            so.insert(QStringLiteral("say"), s.shaftA.y());
            so.insert(QStringLiteral("sbx"), s.shaftB.x());
            so.insert(QStringLiteral("sby"), s.shaftB.y());
        }
        samples.append(so);
    }
    QJsonObject o{
        { QStringLiteral("camera"),  int(t.camera) },
        { QStringLiteral("valid"),   t.valid },
        { QStringLiteral("width"),   t.width },
        { QStringLiteral("height"),  t.height },
        { QStringLiteral("mmPerPx"), t.mmPerPx },
        { QStringLiteral("samples"), samples } };
    if (t.ballLeaveTUs >= 0)
        o.insert(QStringLiteral("ballLeaveTUs"), rel(t.ballLeaveTUs));
    if (t.ballRestR > 0.f)
        o.insert(QStringLiteral("ballRest"), QJsonObject{ { QStringLiteral("x"), t.ballRest.x() },
                                                          { QStringLiteral("y"), t.ballRest.y() },
                                                          { QStringLiteral("r"), double(t.ballRestR) } });
    if (t.pathValid) {
        QJsonArray pts;
        for (const analysis::ImpactPathPoint &p : t.path)
            pts.append(QJsonObject{ { QStringLiteral("t_us"), rel(p.t_us) },
                                    { QStringLiteral("x"),  p.head.x() },  { QStringLiteral("y"),  p.head.y() },
                                    { QStringLiteral("hx"), p.hosel.x() }, { QStringLiteral("hy"), p.hosel.y() },
                                    { QStringLiteral("th"), p.thetaRad } });
        QJsonArray ribbon;
        for (const QPointF &q : t.ribbon)
            ribbon.append(QJsonObject{ { QStringLiteral("x"), q.x() }, { QStringLiteral("y"), q.y() } });
        o.insert(QStringLiteral("path"), QJsonObject{
            { QStringLiteral("kind"),           QStringLiteral("synth") },
            { QStringLiteral("points"),         pts },
            { QStringLiteral("ribbon"),         ribbon },
            { QStringLiteral("arc"),            QJsonObject{ { QStringLiteral("a"), t.arcA }, { QStringLiteral("b"), t.arcB }, { QStringLiteral("c"), t.arcC },
                                                             { QStringLiteral("thetaAtX0"), t.thetaAtX0 }, { QStringLiteral("thetaSlopePerX"), t.thetaSlopePerX } } },
            { QStringLiteral("halfWidth"),      t.pathHalfWidth },
            { QStringLiteral("headAlongPx"),    t.headAlongPx },
            { QStringLiteral("headAcrossPx"),   t.headAcrossPx },
            { QStringLiteral("headAssumed"),    t.headOffsetAssumed },
            { QStringLiteral("headEvidence"),   t.headEvidenceFrames },
            { QStringLiteral("hoselFit"),       t.hoselFitFrames },
            { QStringLiteral("hoselDropped"),   t.hoselDropped },
            { QStringLiteral("hoselResRmsPx"),  t.hoselResidualRmsPx } });
    }
    return o;
}

} // namespace pinpoint
