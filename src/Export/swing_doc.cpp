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

#include "swing_doc.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <algorithm>
#include <cmath>

#include "swing_paths.h"
#include "../Analysis/swing_analysis.h"

namespace pinpoint {
namespace {

// Serialise the score breakdown into the "score" object (schema /3+, design §B.0/§B.7).
// Always carries kind + overall; resemblance fields only for the Wrist estimand; the
// uncertainty interval only once computed (WP-4). Pre-/3 docs stored a bare int here —
// readSwingJson() stays tolerant of both forms.
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
    return o;
}

// Serialise the analyzed swing into the additive "analysis" object (schema
// pinpoint.analysis/3 — versions the embedded block, distinct from the document's
// pinpoint.swing/2). Mirrors the QML analysisDetail shape, t_us as JSON numbers.
// /3 promotes "score" from a bare int to the ScoreBreakdown object (design §B.0a/§B.7).
QJsonObject serializeAnalysis(const analysis::SwingAnalysis &a, qint64 windowT0)
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
    o[QStringLiteral("tier")]   = a.tier;
    o[QStringLiteral("score")]  = serializeScore(a.score);

    QJsonArray metrics;
    for (const MetricSeries &m : a.series) {
        QJsonArray ts, vs, samples;
        for (const int64_t t : m.t_us) ts.append(rel(t));
        for (const double v : m.value) vs.append(v);
        for (const PhaseSample &ps : m.phaseSamples)
            samples.append(QJsonObject{ { QStringLiteral("phase"), int(ps.phase) },
                                        { QStringLiteral("t_us"),  rel(ps.t_us) },
                                        { QStringLiteral("value"), ps.value },
                                        { QStringLiteral("band"),  ps.band } });
        metrics.append(QJsonObject{ { QStringLiteral("key"),   m.key },
                                    { QStringLiteral("label"), m.label },
                                    { QStringLiteral("unit"),  m.unit },
                                    { QStringLiteral("t_us"),  ts },
                                    { QStringLiteral("value"), vs },
                                    { QStringLiteral("phaseSamples"), samples } });
    }
    o[QStringLiteral("metrics")] = metrics;

    QJsonArray phases;
    for (const PhaseEvent &e : a.phases)
        phases.append(QJsonObject{ { QStringLiteral("phase"),   int(e.phase) },
                                   { QStringLiteral("t_us"),    rel(e.t_us) },
                                   { QStringLiteral("conf"),    e.conf },
                                   { QStringLiteral("segment"), int(e.provenance) } });
    o[QStringLiteral("phases")] = phases;

    // Additive "assessment" block = the AI-COACH feedback feed (design §B.0): lead-wrist
    // faults/strengths, decoupled from the headline resemblance score. Now written on every live
    // Wrist shot (ShotProcessor sets runAssessment for type 1), and the SwingLab known-groups
    // diagnosis input. score.py reads `findings[]` for fault recall / FP-on-clean. scoreV2 is
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
    if (a.timings.totalMs >= 0)
        o[QStringLiteral("timings")] = QJsonObject{
            { QStringLiteral("poseMs"),  a.timings.poseMs },
            { QStringLiteral("ballMs"),  a.timings.ballMs },
            { QStringLiteral("shaftMs"), a.timings.shaftMs },
            { QStringLiteral("totalMs"), a.timings.totalMs } };

    // Additive segmentation block (v3 G2, design A.7): the swing bounds +
    // ladder meta. Missing block on reload = full-window bounds.
    if (a.segmentation.swingEndUs > a.segmentation.swingStartUs)
        o[QStringLiteral("segmentation")] = QJsonObject{
            { QStringLiteral("swingStartUs"), rel(a.segmentation.swingStartUs) },
            { QStringLiteral("swingEndUs"),   rel(a.segmentation.swingEndUs) },
            { QStringLiteral("conf"),    double(a.segmentation.conf) },
            { QStringLiteral("version"), a.segmentation.version } };

    // Additive IMU-binding snapshot (SwingLab): the per-device calibration
    // (A/M) keyed by device serial, so the offline runner can re-fuse a
    // recorded swing with the exact anatomical transforms the app used.
    if (!a.bindings.empty()) {
        QJsonArray binds;
        for (const BindingRecord &b : a.bindings)
            binds.append(QJsonObject{
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
                { QStringLiteral("calibAgeSec"),          b.calibAgeSec } });
        o[QStringLiteral("bindings")] = binds;
    }

    // Additive ShaftTracker blocks (S3). pose2d keypoints are already
    // normalized 0..1 frame coords; club grip/head are normalized here by the
    // camera dims so every consumer (replay overlay, reload) is
    // resolution-independent. The club block is written only for a VALID
    // track — the all-or-nothing consumer contract.
    if (!a.pose2d.frames.empty()) {
        QJsonArray frames;
        for (const PoseFrame2D &f : a.pose2d.frames) {
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
        QJsonObject pose2d{
            { QStringLiteral("camera"), int(a.pose2d.camera) },
            { QStringLiteral("keypointCount"), kWholeBodyJoints },
            { QStringLiteral("frames"), frames } };
        // WB1 accuracy-pass provenance (wholebody_pose_design.md §3). Written ONLY
        // when non-legacy — decode when DARK, cropRect when a crop was actually
        // used — so a flags-off (crop disabled + argmax) run serialises exactly as
        // the pre-WB1 tree (the WB1 byte-parity gate). cropRect is full-frame
        // normalized {x,y,w,h}.
        if (a.pose2d.decode == QLatin1String("dark"))
            pose2d.insert(QStringLiteral("decode"), a.pose2d.decode);
        if (a.pose2d.cropRect) {
            const QRectF &r = *a.pose2d.cropRect;
            pose2d.insert(QStringLiteral("cropRect"),
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
        if (!a.pose2d.smoothed.empty()) {
            QJsonArray smoothed;
            const size_t n = std::min(a.pose2d.smoothed.size(), a.pose2d.smoothedAux.size());
            for (size_t i = 0; i < n; ++i) {
                const PoseFrame2D &f = a.pose2d.smoothed[i];
                const PoseKpAux   &x = a.pose2d.smoothedAux[i];
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
            pose2d.insert(QStringLiteral("smoothed"), smoothed);
        }
        o[QStringLiteral("pose2d")] = pose2d;
    }
    if (a.shaft.valid && !a.shaft.samples.empty()
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
        for (const ShaftSample2D &s : a.shaft.synth)
            synth.append(QJsonObject{
                { QStringLiteral("t_us"),  rel(s.t_us) },
                { QStringLiteral("grip"),  QJsonArray{ s.gripPx.x() * iw, s.gripPx.y() * ih } },
                { QStringLiteral("head"),  QJsonArray{ s.headPx.x() * iw, s.headPx.y() * ih } },
                { QStringLiteral("theta"), s.thetaRad },
                { QStringLiteral("thetaDot"), s.thetaDotRadS },
                { QStringLiteral("lenPx"), s.visibleLenPx },
                { QStringLiteral("conf"),  double(s.conf) },
                { QStringLiteral("headConf"),  double(s.headConf) },
                { QStringLiteral("headSigma"), double(s.headSigmaPx) },
                { QStringLiteral("flags"), int(s.flags) } });
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
        for (const ShaftPosition &p : a.shaft.positions)
            positions.append(QJsonObject{
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
                { QStringLiteral("source"), int(p.source) } });
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
            { QStringLiteral("samples"),       samples },
            { QStringLiteral("predicted"),     predicted } };
        if (!positions.isEmpty()) clubObj.insert(QStringLiteral("positions"), positions);
        if (!synth.isEmpty())     clubObj.insert(QStringLiteral("synth"), synth);
        o[QStringLiteral("club")] = clubObj;
    }
    // Ball track (v3.4 design §9) for the replay overlay — normalized [0,1]
    // full-frame center + radius; rel() keeps t_us window-relative like pose2d/
    // club. found=false marks the post-launch gap. Read by disk_replay_source
    // (replay overlay) and SwingDocReader.
    if (!a.ball.frames.empty()) {
        QJsonArray samples;
        for (const BallSample2D &s : a.ball.frames)
            samples.append(QJsonObject{
                { QStringLiteral("t_us"),  rel(s.t_us) },
                { QStringLiteral("x"),     s.center.x() },
                { QStringLiteral("y"),     s.center.y() },
                { QStringLiteral("r"),     double(s.radiusNorm) },
                { QStringLiteral("conf"),  double(s.conf) },
                { QStringLiteral("found"), s.found } });
        o[QStringLiteral("ball")] = QJsonObject{
            { QStringLiteral("camera"),    int(a.ball.camera) },
            { QStringLiteral("valid"),     true },
            { QStringLiteral("launchTUs"), rel(a.ball.launchTUs) },
            { QStringLiteral("samples"),   samples } };
    }
    return o;
}

} // namespace

bool SwingDocWriter::writeSwingJson(const QString &swingDir, const QJsonObject &rawManifest,
                                    const analysis::SwingAnalysis *analysis, QString *error)
{
    QJsonObject root = rawManifest;
    root[QStringLiteral("schema")] = QStringLiteral("pinpoint.swing/2");
    if (analysis) {
        // clock.t0_us = the absolute window start; serializeAnalysis uses it to
        // emit analysis t_us window-relative regardless of the source domain.
        const qint64 t0 = qint64(rawManifest.value(QStringLiteral("clock")).toObject()
                                            .value(QStringLiteral("t0_us")).toDouble());
        root[QStringLiteral("analysis")] = serializeAnalysis(*analysis, t0);
    }

    const QString path = swingDir + QStringLiteral("/swing.json");
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = QStringLiteral("cannot write %1: %2").arg(path, file.errorString());
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error) *error = QStringLiteral("failed to commit %1: %2").arg(path, file.errorString());
        return false;
    }
    return true;
}

bool SwingDocWriter::updateReview(const QString &swingDir, int rating, const QString &note,
                                  const QString &club, QString *error)
{
    const QString path = swingDir + QStringLiteral("/swing.json");

    QFile in(path);
    if (!in.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("cannot read %1: %2").arg(path, in.errorString());
        return false;
    }
    QJsonParseError pe;
    QJsonObject root = QJsonDocument::fromJson(in.readAll(), &pe).object();
    in.close();
    if (pe.error != QJsonParseError::NoError) {
        if (error) *error = QStringLiteral("cannot parse %1: %2").arg(path, pe.errorString());
        return false;
    }

    // Additive "review" block — additive, readers ignore unknown keys. Club is
    // the user's chosen club for the shot (until capture-time club selection
    // exists it starts as the "DRIVER" stub and is only ever set here).
    root[QStringLiteral("review")] = QJsonObject{
        { QStringLiteral("rating"), std::clamp(rating, 0, 5) },
        { QStringLiteral("note"),   note },
        { QStringLiteral("club"),   club },
    };

    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly)) {
        if (error) *error = QStringLiteral("cannot write %1: %2").arg(path, out.errorString());
        return false;
    }
    out.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!out.commit()) {
        if (error) *error = QStringLiteral("failed to commit %1: %2").arg(path, out.errorString());
        return false;
    }
    return true;
}

// ── reader ──────────────────────────────────────────────────────────────────

PersistedShot SwingDocReader::readSwingJson(const QString &swingDir)
{
    PersistedShot ps;
    ps.swingDir = swingDir;

    QFile f(swingDir + QStringLiteral("/swing.json"));
    if (!f.open(QIODevice::ReadOnly))
        return ps;
    QJsonParseError pe;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll(), &pe).object();
    if (pe.error != QJsonParseError::NoError || root.isEmpty())
        return ps;

    ps.ordinal = root[QStringLiteral("swing")].toObject()[QStringLiteral("index")].toInt();
    ps.club    = QStringLiteral("DRIVER");   // stub default; overridden by review.club below

    const QString wc = root[QStringLiteral("clock")].toObject()[QStringLiteral("wallclock")].toString();
    const QDateTime dt = QDateTime::fromString(wc, Qt::ISODateWithMs);
    ps.timestampLabel = dt.isValid() ? dt.toLocalTime().toString(QStringLiteral("hh:mm:ss")) : wc;
    ps.wallclockMs    = dt.isValid() ? dt.toMSecsSinceEpoch() : 0;

    // Only video presence is reconstructed here. imu / pose streams and the raw
    // sidecar are not parsed on reload yet — see the "Reload & replay consumer
    // contract" in docs/developer/swing_export_developer_guide.md for the shapes a future
    // consumer must honor (IMU json/csv/binary, pose coco17, raw reconstruction).
    for (const QJsonValue &v : root[QStringLiteral("streams")].toArray())
        if (v.toObject()[QStringLiteral("kind")].toString() == QLatin1String("video")) { ps.hasVideo = true; break; }

    const QJsonObject thumb = root[QStringLiteral("thumbnail")].toObject();
    if (!thumb.isEmpty())
        ps.thumbnailPath = swingDir + QStringLiteral("/")
                         + thumb[QStringLiteral("file")].toString(QStringLiteral("thumb.jpg"));

    if (root.contains(QStringLiteral("analysis"))) {
        const QJsonObject an = root[QStringLiteral("analysis")].toObject();
        // "score" is a bare int in /2 docs, a ScoreBreakdown object in /3+ (design §B.0a).
        const QJsonValue scoreVal = an[QStringLiteral("score")];
        const QJsonObject scoreObj = scoreVal.toObject();   // empty if /2 (a number)
        const int overall = scoreVal.isObject()
                                ? scoreObj[QStringLiteral("overall")].toInt()
                                : scoreVal.toInt();
        ps.score = overall;

        // analysisDetail in the live QML-role shape (metrics→series, score→overall).
        ps.analysisDetail = QVariantMap{
            { QStringLiteral("tier"),    an[QStringLiteral("tier")].toInt() },
            { QStringLiteral("overall"), overall },
            { QStringLiteral("series"),  an[QStringLiteral("metrics")].toArray().toVariantList() },
            { QStringLiteral("phases"),  an[QStringLiteral("phases")].toArray().toVariantList() },
        };
        // Resemblance estimand + uncertainty interval (/3): surfaced as sibling keys so
        // QML reads detail.pattern / .resemblance / .interval directly. Absent for /2 or
        // for adherence (Swing/GRF) scores.
        if (scoreVal.isObject()) {
            if (scoreObj.contains(QStringLiteral("pattern")))
                ps.analysisDetail.insert(QStringLiteral("pattern"),
                                         scoreObj[QStringLiteral("pattern")].toString());
            if (scoreObj.contains(QStringLiteral("blended")))
                ps.analysisDetail.insert(QStringLiteral("blended"),
                                         scoreObj[QStringLiteral("blended")].toBool());
            if (scoreObj.contains(QStringLiteral("resemblance")))
                ps.analysisDetail.insert(QStringLiteral("resemblance"),
                                         scoreObj[QStringLiteral("resemblance")].toObject().toVariantMap());
            if (scoreObj.contains(QStringLiteral("interval")))
                ps.analysisDetail.insert(QStringLiteral("interval"),
                                         scoreObj[QStringLiteral("interval")].toObject().toVariantMap());
        }
        // Additive ShaftTracker + segmentation blocks — same variant shapes as
        // the live toAnalysisDetail (shot_processor.cpp); absent in older files
        // (missing segmentation = full-window bounds).
        if (an.contains(QStringLiteral("pose2d")))
            ps.analysisDetail.insert(QStringLiteral("pose2d"),
                                     an[QStringLiteral("pose2d")].toObject().toVariantMap());
        if (an.contains(QStringLiteral("club")))
            ps.analysisDetail.insert(QStringLiteral("club"),
                                     an[QStringLiteral("club")].toObject().toVariantMap());
        if (an.contains(QStringLiteral("ball")))
            ps.analysisDetail.insert(QStringLiteral("ball"),
                                     an[QStringLiteral("ball")].toObject().toVariantMap());
        if (an.contains(QStringLiteral("segmentation")))
            ps.analysisDetail.insert(QStringLiteral("segmentation"),
                                     an[QStringLiteral("segmentation")].toObject().toVariantMap());
        if (an.contains(QStringLiteral("bindings")))
            ps.analysisDetail.insert(QStringLiteral("bindings"),
                                     an[QStringLiteral("bindings")].toArray().toVariantList());
        if (an.contains(QStringLiteral("timings")))
            ps.analysisDetail.insert(QStringLiteral("timings"),
                                     an[QStringLiteral("timings")].toObject().toVariantMap());

        // Flat metrics: each metric's value at Impact, signed degrees.
        QVariantMap metrics;
        for (const QJsonValue &mv : an[QStringLiteral("metrics")].toArray()) {
            const QJsonObject m = mv.toObject();
            bool found = false;
            double impact = 0.0;
            for (const QJsonValue &sv : m[QStringLiteral("phaseSamples")].toArray()) {
                const QJsonObject s = sv.toObject();
                if (s[QStringLiteral("phase")].toInt() == int(analysis::Phase::Impact)) {
                    impact = s[QStringLiteral("value")].toDouble(); found = true; break;
                }
            }
            if (!found)
                continue;
            const long r = std::lround(impact);
            const QString val = (r > 0 ? QStringLiteral("+") : QString()) + QString::number(r) + QStringLiteral("°");
            metrics.insert(m[QStringLiteral("key")].toString(),
                           QVariantMap{ { QStringLiteral("label"), m[QStringLiteral("label")].toString() },
                                        { QStringLiteral("value"), val } });
        }
        ps.metrics = metrics;
    }

    // User review (rating/note/club) — written through by updateReview after edits.
    if (root.contains(QStringLiteral("review"))) {
        const QJsonObject rv = root[QStringLiteral("review")].toObject();
        ps.rating = std::clamp(rv[QStringLiteral("rating")].toInt(), 0, 5);
        ps.note   = rv[QStringLiteral("note")].toString();
        // Older review blocks predate club; keep the stub default when absent/empty.
        const QString club = rv[QStringLiteral("club")].toString();
        if (!club.isEmpty())
            ps.club = club;
    }

    // IMU data-integrity verdict (additive top-level block from ShotProcessor's
    // offline re-fusion parity check). Legacy swings lack it → no warning.
    if (root.contains(QStringLiteral("imuIntegrity"))) {
        const QJsonObject ii = root[QStringLiteral("imuIntegrity")].toObject();
        if (ii.value(QStringLiteral("sourcesChecked")).toInt() > 0)
            ps.dataWarning = !ii.value(QStringLiteral("refusionOk")).toBool(true);
    }

    ps.ok = true;
    return ps;
}

QStringList SwingDocReader::findSwingDirs(const QString &sessionDir)
{
    QDir d(sessionDir);
    QStringList out;
    for (const QString &name : d.entryList(QStringList{ QStringLiteral("swing_*") },
                                           QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
        out.append(sessionDir + QStringLiteral("/") + name);
    return out;
}

QString SwingDocReader::latestSessionDir(const QString &libraryRoot, const QString &athleteName)
{
    if (libraryRoot.isEmpty() || athleteName.isEmpty())
        return {};
    const QString athleteDir = libraryRoot + QStringLiteral("/") + SwingPaths::sanitise(athleteName);
    const QFileInfoList sessions =
        QDir(athleteDir).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    if (sessions.isEmpty())
        return {};
    // Pick the most recently modified session dir. Folder names now embed the
    // naming pattern's tokens (date / athlete / session-type), so a plain name
    // sort no longer tracks recency when several session types share one day.
    const QFileInfo *newest = &sessions.first();
    for (const QFileInfo &fi : sessions)
        if (fi.lastModified() > newest->lastModified())
            newest = &fi;
    return newest->absoluteFilePath();
}

QStringList SwingDocReader::sessionDirs(const QString &libraryRoot, const QString &athleteName)
{
    if (libraryRoot.isEmpty() || athleteName.isEmpty())
        return {};
    const QString athleteDir = libraryRoot + QStringLiteral("/") + SwingPaths::sanitise(athleteName);
    QFileInfoList sessions =
        QDir(athleteDir).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    // Most-recently-modified first — same recency basis as latestSessionDir(),
    // since the naming pattern means a plain name sort doesn't track recency.
    std::sort(sessions.begin(), sessions.end(), [](const QFileInfo &a, const QFileInfo &b) {
        return a.lastModified() > b.lastModified();
    });
    QStringList out;
    out.reserve(sessions.size());
    for (const QFileInfo &fi : sessions)
        out.append(fi.absoluteFilePath());
    return out;
}

} // namespace pinpoint
