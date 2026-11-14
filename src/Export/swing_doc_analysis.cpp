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

// swing_doc_analysis.cpp — the `analysis` block of a swing document, COMPOSED from the per-block
// serialisers that live beside their producers (refactor step 1.3a; information_model.md §8 "One
// serialiser per analysis block"):
//
//   block                     writer / typed reader                       where
//   score, metrics,           scoreToJson / scoreFromJson, metrics…,      Analysis/swing_analysis_json.h
//   versions, filter          analysisVersions…, filter…
//   path                      analysisPathToJson / analysisPathFromJson   Export/analysis_path_json.h
//   assessment, timings,      writer only (nothing reads them back typed)  Analysis/swing_analysis_json.h
//   bindings
//   phases, segmentation,     phasesToJson / phasesFromJson,              Analysis/recorded_products.h
//   club                      segmentation…, shaftTrack…AnalysisJson
//   pose2d, poseDtl           poseTrackToAnalysisJson / …FromAnalysisJson Pose/pose_track_json.h
//   ball                      ballTrackToAnalysisJson / …FromAnalysisJson Ball/ball_track_json.h
//   impact                    impactTrackToAnalysisJson / …FromAnalysisJson Ball/impact_track_json.h
//   kinematicSequence         kinematicSequenceToJson / …FromJson         Kinematics/kinematic_sequence_json.h
//   gates                     gateRecordsToJson / gateRecordsFromJson     Analysis/gate_record.h
//   dtlRuler                  dtlRulerToJson / dtlRulerFromJson           Body/dtl_ruler_json.h
//   clubDtl, club3d,          writer only (their readers build other      Club/dtl_shaft_json.h,
//   skeleton3d, addressMarks  shapes: the replay payloads, Skeleton3DTrack) Club/shaft_fusion_json.h, …
//
// serializeAnalysis (called by SwingDocWriter::writeSwingJson in swing_doc_writer.cpp) writes the
// whole block; analysisFromDocument reads it back typed. ONE TIME BASE: the document is window
// time and says so (`analysis.timeBase: "window"`, IM-2); every time goes through windowTimeUs.
// The writer is swing_doc_writer.cpp, the reader swing_doc_reader.cpp; the public API is swing_doc.h.

#include "swing_doc.h"
#include "swing_doc_internal.h"

#include <QJsonArray>

#include "../Analysis/swing_analysis.h"
#include "../Analysis/swing_analysis_json.h"       // score, metrics, versions, filter, assessment, timings, bindings
#include "analysis_path_json.h"                 // path
#include "../Analysis/recorded_products.h"         // phases, segmentation, club
#include "../Analysis/gate_record.h"               // gates
#include "../Pose/pose_track_json.h"               // pose2d, poseDtl
#include "../Ball/ball_track_json.h"               // ball
#include "../Ball/impact_track_json.h"             // impact
#include "../Kinematics/kinematic_sequence_json.h" // kinematicSequence
#include "../Club/shaft_fusion_json.h"             // club3d
#include "../Skeleton3d/skeleton3d_json.h"         // skeleton3d
#include "../Club/dtl_shaft_json.h"                // clubDtl == club_dtl.json
#include "../Body/address_marks_json.h"            // addressMarks
#include "../Body/dtl_ruler_json.h"                // dtlRuler

namespace pinpoint {

namespace swingdoc_detail {

// Serialise the analyzed swing into the additive "analysis" object (schema
// pinpoint.analysis/3 — versions the embedded block, distinct from the document's
// pinpoint.swing/2). /3 promotes "score" from a bare int to the ScoreBreakdown object
// (design §B.0a/§B.7).
QJsonObject serializeAnalysis(const analysis::SwingAnalysis &a, qint64 windowT0,
                              const QString &dtlAlias, const QString &dtlFile)
{
    using namespace analysis;
    // ONE TIME BASE (A-03). The document is window time. An analysis already in window time is
    // written as it is; one on its window's own clock (TimeBase::Clock — what the stages produce:
    // the ring clock live, already relative for a window loaded from disk) is taken there by the
    // one rule, windowTimeUs, with the document's clock.t0_us.
    const int64_t t0 = a.timeBase == TimeBase::Window ? 0 : int64_t(windowT0);
    QJsonObject o;
    o[QStringLiteral("schema")]   = QStringLiteral("pinpoint.analysis/3");
    o[QStringLiteral("timeBase")] = QString::fromLatin1(kAnalysisTimeBaseWindow);
    if (analysisPathIsWritten(a.path))
        o[QStringLiteral("path")] = analysisPathToJson(a.path);
    if (a.versions.stamped())
        o[QStringLiteral("versions")] = analysisVersionsToJson(a.versions);
    o[QStringLiteral("tier")]    = a.tier;
    o[QStringLiteral("score")]   = scoreToJson(a.score);
    o[QStringLiteral("metrics")] = metricsToJson(a.series, t0);

    // The kinematic sequence — the ordered peaks over the four segment angular-speed series.
    // ABSENT when nothing produced a node, so a swing with no downswing ladder serialises exactly
    // as before the object existed (schema history 2026-09-17).
    if (a.kinematicSequence.valid)
        o[QStringLiteral("kinematicSequence")] =
            kinematicSequenceToJson(a.kinematicSequence, [t0](int64_t t) { return qint64(windowTimeUs(t, t0)); });

    // Gate records (gate_record.h; trust_and_gates_design.md §6) — why readings were refused,
    // bounded or withheld. Written whenever an emitting stage ran, even with no record, so a reader
    // can tell "no gate fired" (empty array) from "analysed before records existed" (both absent).
    // Evidence times are ms from impact and need no re-timing. Additive (schema history 2026-10-16).
    if (a.gatesVersion > 0) {
        o[QStringLiteral("gatesVersion")] = a.gatesVersion;
        o[QStringLiteral("gates")]        = gateRecordsToJson(a.gates);
    }

    o[QStringLiteral("phases")] = phasesToJson(a.phases, a.segmentation.version >= 5, t0);

    if (a.assessmentScore >= 0)
        o[QStringLiteral("assessment")] = assessmentToJson(a.findings, a.assessmentScore);
    if (a.filterImpactStepDeg >= 0.0)
        o[QStringLiteral("filter")] = filterToJson(a.filterImpactStepDeg);
    if (a.timings.totalMs >= 0)
        o[QStringLiteral("timings")] = timingsToJson(a.timings);
    if (segmentationIsWritten(a.segmentation))
        o[QStringLiteral("segmentation")] = segmentationToJson(a.segmentation, t0);
    if (!a.bindings.empty())
        o[QStringLiteral("bindings")] = bindingsToJson(a.bindings);

    if (!a.pose2d.frames.empty())
        o[QStringLiteral("pose2d")] = poseTrackToAnalysisJson(a.pose2d, t0);
    // The DOWN-THE-LINE pose (DtlPoseStage) in the same shape, for the DTL replay tile and
    // for re-analysis reuse (swing_reanalyzer.cpp). Absent when no DTL pose ran.
    if (!a.poseDtl.frames.empty())
        o[QStringLiteral("poseDtl")] = poseTrackToAnalysisJson(a.poseDtl, t0);
    if (shaftTrackIsWritten(a.shaft))
        o[QStringLiteral("club")] = shaftTrackToAnalysisJson(a.shaft, t0);
    if (!a.ball.frames.empty())
        o[QStringLiteral("ball")] = ballTrackToAnalysisJson(a.ball, t0);
    // The impact camera's track (impact_camera_design.md §7) — absent when no impact camera ran.
    if (a.impact.valid)
        o[QStringLiteral("impact")] = impactTrackToAnalysisJson(a.impact, t0);
    // The down-the-line club track (DtlShaftStage) — pinpoint.clubDtl/1, the very bytes
    // SwingLab writes to club_dtl.json (dtl_shaft_json.h). Written whenever the stage RAN
    // (versions.shaftDtl), valid or not: an invalid track's `summary` and per-frame `reason`
    // are the record of why the DTL tile shows no shaft. Absent when it did not run.
    if (a.versions.shaftDtl > 0)
        o[QStringLiteral("clubDtl")] = dtlShaftTrackToJson(
            a.shaftDtl, t0, a.shaftDtl.configJson, a.shaftDtl.configHash, dtlAlias, dtlFile);
    // The fused 3-D shaft (ShaftFusionStage) — pinpoint.club3d/1. Written whenever the stage
    // RAN, valid or not, for clubDtl's reason: the counts are the record of why there is none.
    if (a.versions.shaftFusion > 0)
        o[QStringLiteral("club3d")] = shaftTrack3dToJson(a.shaft3d, a.shaft3dCfg, t0, a.versions.shaftFusion);
    // The fitted 3-D skeleton (Skeleton3DStage) — pinpoint.skeleton3d/1, the 3-D swing panel's
    // input. Written whenever the stage RAN, valid or not: `reason` says why there is none.
    if (a.versions.skeleton3d > 0)
        o[QStringLiteral("skeleton3d")] = pinpoint::skeleton3d::skeleton3dToJson(a.skeleton3d, t0, a.versions.skeleton3d);
    // The body's outer edges at hip height at address (AddressMarksStage) — pinpoint.addressMarks/2
    // (the DTL P-frames since stage version 3), the coach lines' input. Written whenever the stage
    // RAN: `found` false per view is the record.
    if (a.versions.addressMarks > 0)
        o[QStringLiteral("addressMarks")] = addressMarksToJson(a.addressMarks, t0, a.versions.addressMarks);
    // The DTL ruler (DtlPostureStage) — pinpoint.dtlRuler/1: the cm per DTL pixel pelvisThrust,
    // headRise and hipDepthButt were measured in. Written whenever the stage RAN; `cmPerPx` null
    // and `source` "none" is the record of no ruler.
    if (a.dtlRuler.ran)
        o[QStringLiteral("dtlRuler")] = dtlRulerToJson(a.dtlRuler, a.versions.dtlPosture);
    return o;
}

} // namespace swingdoc_detail

qint64 analysisReadOriginUs(const QJsonObject &root)
{
    // THE ONE PLACE a document without `analysis.timeBase` is guessed (the legacy path, IM-2). A
    // stamped document is window time: read as written. An unstamped one is read through the one
    // rule against its clock.t0_us, which leaves the window-relative values every writer since the
    // rel() normalisation produced untouched and takes an absolute one (a value at or after the
    // window's start on the ring clock) to window time. Reading never rewrites the document.
    const QJsonObject an = root.value(QStringLiteral("analysis")).toObject();
    if (an.value(QStringLiteral("timeBase")).toString() == QLatin1String(analysis::kAnalysisTimeBaseWindow))
        return 0;
    return qint64(root.value(QStringLiteral("clock")).toObject().value(QStringLiteral("t0_us")).toDouble(0));
}

analysis::SwingAnalysis analysisFromDocument(const QJsonObject &root)
{
    using namespace analysis;
    const int64_t t0 = analysisReadOriginUs(root);
    const QJsonObject an = root.value(QStringLiteral("analysis")).toObject();
    SwingAnalysis a;
    a.timeBase = TimeBase::Window;
    if (an.isEmpty()) return a;
    a.tier     = an.value(QStringLiteral("tier")).toInt(a.tier);
    a.score    = scoreFromJson(an.value(QStringLiteral("score")));
    a.series   = metricsFromJson(an.value(QStringLiteral("metrics")).toArray(), t0);
    if (an.contains(QStringLiteral("path")))
        a.path = analysisPathFromJson(an.value(QStringLiteral("path")).toObject());
    a.versions = analysisVersionsFromJson(an.value(QStringLiteral("versions")).toObject());
    a.phases   = phasesFromJson(an.value(QStringLiteral("phases")).toArray(), t0);
    a.segmentation        = segmentationFromJson(an.value(QStringLiteral("segmentation")).toObject(), t0);
    a.segmentation.events = a.phases;   // "phases IS segmentation.events" (SwingAnalysis)
    a.kinematicSequence = kinematicSequenceFromJson(an.value(QStringLiteral("kinematicSequence")).toObject(), t0);
    if (an.contains(QStringLiteral("gatesVersion"))) {
        a.gatesVersion = an.value(QStringLiteral("gatesVersion")).toInt();
        a.gates        = gateRecordsFromJson(an.value(QStringLiteral("gates")).toArray());
    }
    if (an.contains(QStringLiteral("filter")))
        a.filterImpactStepDeg = filterFromJson(an.value(QStringLiteral("filter")).toObject());
    if (an.contains(QStringLiteral("pose2d")))
        a.pose2d = poseTrackFromAnalysisJson(an.value(QStringLiteral("pose2d")).toObject(), t0);
    if (an.contains(QStringLiteral("poseDtl")))
        a.poseDtl = poseTrackFromAnalysisJson(an.value(QStringLiteral("poseDtl")).toObject(), t0);
    {
        const QJsonObject club = an.value(QStringLiteral("club")).toObject();
        a.shaft = shaftTrackFromAnalysisJson(
            club, pinpoint::SourceId(club.value(QStringLiteral("camera")).toInt(int(pinpoint::kInvalidSourceId))), t0);
    }
    if (an.contains(QStringLiteral("ball")))
        a.ball = ballTrackFromAnalysisJson(an.value(QStringLiteral("ball")).toObject(), t0);
    a.impact   = impactTrackFromAnalysisJson(an.value(QStringLiteral("impact")).toObject(), t0);
    a.dtlRuler = dtlRulerFromJson(an.value(QStringLiteral("dtlRuler")).toObject());
    return a;
}

QJsonObject poseTrackToJson(const analysis::PoseTrack2D &t, qint64 windowT0)
{
    return analysis::poseTrackToAnalysisJson(t, windowT0);
}

QJsonObject impactTrackJson(const analysis::ImpactTrack2D &t, qint64 windowT0)
{
    return analysis::impactTrackToAnalysisJson(t, windowT0);
}

} // namespace pinpoint
