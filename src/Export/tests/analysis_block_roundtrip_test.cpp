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

// analysis_block_roundtrip_test — refactor step 1.3a (information_model.md §7 "One time base",
// §8 "One serialiser per analysis block").
//
//   1. Each analysis block with a writer and a typed reader: block → JSON → typed → JSON, equal.
//      The fixture sets every field each writer emits, optional ones included.
//   2. One time base: a Clock analysis (the stages' output, on the ring clock) written with the
//      document's t0 is byte-for-byte the same block as its Window twin written as it is, and the
//      block says `timeBase: "window"`.
//   3. The document read back typed (analysisFromDocument) re-serialises to the block it was read
//      from (writer-only blocks aside).
//   4. The legacy path: an old document (absolute times, no `timeBase`) reads to the same typed
//      values as its rebased twin; a document with no `timeBase` whose times are already relative
//      reads unchanged; reading never rewrites the document.
//   5. The pipeline's one rebase (step 1.5, swing_analysis_rebase.h): a clock-time analysis rebased
//      in memory with its t0 and written as it is gives the very block the writer makes from it with
//      that t0 — every block, the writer-only ones (clubDtl, club3d, skeleton3d, addressMarks)
//      included — and rebasing twice changes nothing.
//
// Frame sizes are powers of two so normalise-then-denormalise is exact in binary floating point.

#include "../swing_doc.h"
#include "../../Analysis/swing_analysis.h"
#include "../../Analysis/swing_analysis_json.h"
#include "../analysis_path_json.h"
#include "../../Analysis/recorded_products.h"
#include "../../Analysis/gate_record.h"
#include "../../Pose/pose_track_json.h"
#include "../../Ball/ball_track_json.h"
#include "../../Ball/impact_track_json.h"
#include "../../Kinematics/kinematic_sequence_json.h"
#include "../../Body/dtl_ruler_json.h"
#include "analysis_fixture.h"
#include "../../Analysis/swing_analysis_rebase.h"   // the pipeline's one rebase (step 1.5)

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <cstdio>

using namespace pinpoint;
using namespace pinpoint::analysis;

static int g_fail = 0;
static void check(bool c, const char *label)
{
    std::fprintf(stderr, "  %s %s\n", c ? "ok  " : "FAIL", label);
    if (!c) ++g_fail;
}

// The first differing path, for a failure message ("" = equal).
static QString firstDiff(const QJsonValue &a, const QJsonValue &b, const QString &at)
{
    if (a.type() != b.type()) return at + QStringLiteral(" (type)");
    if (a.isObject()) {
        const QJsonObject oa = a.toObject(), ob = b.toObject();
        for (auto it = oa.begin(); it != oa.end(); ++it) {
            if (!ob.contains(it.key())) return at + QLatin1Char('.') + it.key() + QStringLiteral(" (missing)");
            const QString d = firstDiff(it.value(), ob.value(it.key()), at + QLatin1Char('.') + it.key());
            if (!d.isEmpty()) return d;
        }
        for (auto it = ob.begin(); it != ob.end(); ++it)
            if (!oa.contains(it.key())) return at + QLatin1Char('.') + it.key() + QStringLiteral(" (extra)");
        return QString();
    }
    if (a.isArray()) {
        const QJsonArray xa = a.toArray(), xb = b.toArray();
        if (xa.size() != xb.size()) return at + QStringLiteral(" (size)");
        for (int i = 0; i < xa.size(); ++i) {
            const QString d = firstDiff(xa[i], xb[i], at + QStringLiteral("[%1]").arg(i));
            if (!d.isEmpty()) return d;
        }
        return QString();
    }
    return a == b ? QString() : at;
}

static void checkEqual(const QJsonValue &a, const QJsonValue &b, const char *label)
{
    const QString d = firstDiff(a, b, QStringLiteral("$"));
    if (!d.isEmpty()) std::fprintf(stderr, "       first difference at %s\n", d.toUtf8().constData());
    check(d.isEmpty(), label);
}

// The fixture (kT0, makeAnalysis): analysis_fixture.h, shared with analysis_detail_projection_test.
using analysis_fixture::kT0;
using analysis_fixture::makeAnalysis;

// The blocks serializeAnalysis writes that analysisFromDocument does not read back (their readers
// build other shapes, or nothing reads them as SwingAnalysis).
static QJsonObject minusWriterOnly(QJsonObject an)
{
    for (const char *k : { "assessment", "timings", "bindings", "clubDtl", "club3d", "skeleton3d", "addressMarks" })
        an.remove(QString::fromLatin1(k));
    return an;
}

static QJsonObject document(const QJsonObject &analysisBlock, int64_t t0)
{
    return QJsonObject{ { QStringLiteral("clock"), QJsonObject{ { QStringLiteral("t0_us"), qint64(t0) } } },
                        { QStringLiteral("analysis"), analysisBlock } };
}

static QJsonObject writeBlock(const SwingAnalysis &a, int64_t t0);

int main()
{
    const SwingAnalysis win = makeAnalysis(0);     // window time
    const SwingAnalysis clk = makeAnalysis(kT0);   // the ring clock, as the stages produce it live

    std::fprintf(stderr, "[1] each block: block -> JSON -> typed -> JSON\n");
    {
        const QJsonObject j = scoreToJson(win.score);
        checkEqual(j, scoreToJson(scoreFromJson(j)), "score (resemblance)");
        ScoreBreakdown ad; ad.overall = 55; ad.perRegion.insert(QStringLiteral("tempo"), 40);
        const QJsonObject ja = scoreToJson(ad);
        checkEqual(ja, scoreToJson(scoreFromJson(ja)), "score (adherence)");
        check(scoreFromJson(QJsonValue(77)).overall == 77, "score: a pre-/3 bare int reads as its overall");
    }
    {
        const QJsonArray j = metricsToJson(win.series, 0);
        checkEqual(j, metricsToJson(metricsFromJson(j), 0), "metrics (every optional field, and none)");
    }
    {
        const QJsonObject j = analysisPathToJson(win.path);
        checkEqual(j, analysisPathToJson(analysisPathFromJson(j)), "path");
        const QJsonObject v = analysisVersionsToJson(win.versions);
        checkEqual(v, analysisVersionsToJson(analysisVersionsFromJson(v)), "versions");
        const QJsonObject f = filterToJson(win.filterImpactStepDeg);
        checkEqual(f, filterToJson(filterFromJson(f)), "filter");
    }
    {
        const QJsonArray j = phasesToJson(win.phases, true, 0);
        checkEqual(j, phasesToJson(phasesFromJson(j), true, 0), "phases (fused: timing)");
        const QJsonArray u = phasesToJson(win.phases, false, 0);
        checkEqual(u, phasesToJson(phasesFromJson(u), false, 0), "phases (unfused: no timing)");
        const QJsonObject s = segmentationToJson(win.segmentation, 0);
        checkEqual(s, segmentationToJson(segmentationFromJson(s), 0), "segmentation (bounds + fusion)");
        const QJsonObject an{ { QStringLiteral("phases"), j }, { QStringLiteral("segmentation"), s } };
        const std::optional<Segmentation> ladder = segmentationFromAnalysisJson(an);
        check(ladder && ladder->events.size() == win.phases.size() && ladder->swingEndUs == win.segmentation.swingEndUs,
              "the reuse ladder reads phases[] + segmentation");
    }
    {
        const QJsonObject j = shaftTrackToAnalysisJson(win.shaft, 0);
        const ShaftTrack2D t = shaftTrackFromAnalysisJson(j, win.shaft.camera);
        checkEqual(j, shaftTrackToAnalysisJson(t, 0), "club (samples, predicted, synth, positions, plane, diag, filter)");
        check(t.onsetTUs == win.shaft.onsetTUs && t.topTUs == -1 && t.clubFilter.ran,
              "club: diag onset/top and the filter diagnostic are read back (lossless reuse)");
        ShaftTrack2D refused = win.shaft; refused.valid = false; refused.refusedReason = 2;
        const QJsonObject r = shaftTrackToAnalysisJson(refused, 0);
        checkEqual(r, shaftTrackToAnalysisJson(shaftTrackFromAnalysisJson(r, refused.camera), 0), "club (refused)");
    }
    {
        const QJsonObject j = poseTrackToAnalysisJson(win.pose2d, 0);
        const PoseTrack2D t = poseTrackFromAnalysisJson(j);
        checkEqual(j, poseTrackToAnalysisJson(t, 0), "pose2d (frames, decode, cropRect, smoothed, adaptFallbacks, synth)");
        check(t.camera == win.pose2d.camera, "pose2d: the block's camera");
        const QJsonObject d = poseTrackToAnalysisJson(win.poseDtl, 0);
        checkEqual(d, poseTrackToAnalysisJson(poseTrackFromAnalysisJson(d), 0), "poseDtl (frames only)");
    }
    {
        const QJsonObject j = ballTrackToAnalysisJson(win.ball, 0);
        checkEqual(j, ballTrackToAnalysisJson(ballTrackFromAnalysisJson(j), 0), "ball");
        QJsonObject old = j; old.remove(QStringLiteral("launch"));   // pre-2026-09-09: no launch centre
        check(ballTrackFromAnalysisJson(old).launchCenter == win.ball.launchCenter,
              "ball: an old record's launch centre is the last found sample before launch");
        const QJsonObject i = impactTrackToAnalysisJson(win.impact, 0);
        checkEqual(i, impactTrackToAnalysisJson(impactTrackFromAnalysisJson(i), 0), "impact (ball, club, outline, path)");
    }
    {
        const auto id = [](int64_t t) { return qint64(t); };
        const QJsonObject j = kinematicSequenceToJson(win.kinematicSequence, id);
        checkEqual(j, kinematicSequenceToJson(kinematicSequenceFromJson(j), id),
                   "kinematicSequence (bounds, probable peak, bridge)");
        const QJsonArray g = gateRecordsToJson(win.gates);
        checkEqual(g, gateRecordsToJson(gateRecordsFromJson(g)), "gates");
        const QJsonObject r = dtlRulerToJson(win.dtlRuler, 4);
        checkEqual(r, dtlRulerToJson(dtlRulerFromJson(r), 4), "dtlRuler");
        DtlRuler none; none.ran = true;
        const QJsonObject rn = dtlRulerToJson(none, 4);
        checkEqual(rn, dtlRulerToJson(dtlRulerFromJson(rn), 4), "dtlRuler (no ruler: null, \"none\")");
    }

    std::fprintf(stderr, "[2] one time base\n");
    const QJsonObject fromClock  = writeBlock(clk, kT0);
    const QJsonObject fromWindow = writeBlock(win, kT0);   // Window: t0 is not applied
    check(fromClock.value(QStringLiteral("timeBase")).toString() == QLatin1String("window"),
          "the block says timeBase: \"window\"");
    checkEqual(fromClock, fromWindow, "a Clock analysis written with its t0 == its Window twin written as it is");
    check(fromClock.value(QStringLiteral("phases")).toArray().at(0).toObject().value(QStringLiteral("t_us")).toInteger() == 800,
          "phases[0].t_us is window time (800 µs)");
    check(fromClock.value(QStringLiteral("club")).toObject().value(QStringLiteral("diag")).toObject()
              .value(QStringLiteral("topTUs")).toDouble() == -1.0,
          "an unset marker (-1) stays -1 on the ring clock");
    check(windowTimeUs(-1, kT0) == -1 && windowTimeUs(kT0 + 5, kT0) == 5 && windowTimeUs(42, 0) == 42,
          "windowTimeUs: markers kept, absolute made relative, t0 0 is the identity");

    std::fprintf(stderr, "[3] the document read back typed\n");
    const QJsonObject doc = document(fromClock, kT0);
    check(analysisReadOriginUs(doc) == 0, "a stamped document is read as written (origin 0)");
    const SwingAnalysis typed = analysisFromDocument(doc);
    check(typed.timeBase == TimeBase::Window, "the typed read is window time");
    checkEqual(minusWriterOnly(fromClock), minusWriterOnly(writeBlock(typed, kT0)),
               "document -> typed -> document, equal (writer-only blocks aside)");

    std::fprintf(stderr, "[4] the legacy path (no timeBase)\n");
    {
        // An old document: the stages' ring-clock times written as they were, no timeBase.
        QJsonObject legacyBlock = writeBlock(clk, 0);
        legacyBlock.remove(QStringLiteral("timeBase"));
        const QJsonObject legacy = document(legacyBlock, kT0);
        check(legacyBlock.value(QStringLiteral("phases")).toArray().at(0).toObject()
                  .value(QStringLiteral("t_us")).toInteger() == kT0 + 800,
              "the legacy fixture is absolute (phases[0].t_us = t0 + 800)");
        check(analysisReadOriginUs(legacy) == kT0, "an unstamped document is read against its clock.t0_us");
        const QJsonObject before = legacy;
        const SwingAnalysis oldTyped = analysisFromDocument(legacy);
        check(legacy == before, "reading never rewrites the document");
        checkEqual(minusWriterOnly(writeBlock(oldTyped, 0)), minusWriterOnly(writeBlock(typed, 0)),
                   "an old absolute-time document reads to the same typed values as its rebased twin");
        check(!oldTyped.phases.empty() && oldTyped.phases.front().t_us == 800 && oldTyped.shaft.topTUs == -1
                  && oldTyped.ball.launchTUs == 11200,
              "legacy typed values are window time; markers stay markers");

        // An unstamped document whose times are already window-relative (every writer since rel()).
        QJsonObject relBlock = fromClock;
        relBlock.remove(QStringLiteral("timeBase"));
        const SwingAnalysis relTyped = analysisFromDocument(document(relBlock, kT0));
        checkEqual(minusWriterOnly(writeBlock(relTyped, 0)), minusWriterOnly(writeBlock(typed, 0)),
                   "an unstamped, already-relative document reads unchanged");
    }

    std::fprintf(stderr, "[5] the pipeline's one rebase (step 1.5)\n");
    {
        // A clock-time analysis rebased in memory with its t0 and written as it is == the writer's own
        // conversion of the clock-time analysis: the rebase takes exactly the instants the writers do.
        SwingAnalysis reb = clk;
        rebaseToWindowTime(reb, kT0);
        check(reb.timeBase == TimeBase::Window, "the rebased analysis says Window");
        checkEqual(writeBlock(reb, 0), fromClock, "rebased then written == written with t0 (every block)");
        checkEqual(writeBlock(reb, kT0), fromClock, "a Window analysis ignores the writer's t0");
        check(reb.segmentation.events.size() == clk.segmentation.events.size()
                  && std::equal(reb.segmentation.events.begin(), reb.segmentation.events.end(),
                                clk.segmentation.events.begin(),
                                [](const PhaseEvent &w, const PhaseEvent &c) { return w.t_us == windowTimeUs(c.t_us, kT0); }),
              "the ladder's events (the live replay's Finish) are window time too");
        SwingAnalysis again = reb;
        rebaseToWindowTime(again, kT0);
        checkEqual(writeBlock(again, 0), fromClock, "rebasing a Window analysis again changes nothing");

        // The writer-only blocks the fixture leaves empty: clubDtl, club3d, skeleton3d, addressMarks.
        SwingAnalysis c2 = clk;
        const auto T = [](int64_t rel) { return kT0 + rel; };
        c2.versions.shaftDtl = 1;
        c2.shaftDtl.frameWidth = 1024; c2.shaftDtl.frameHeight = 512;
        DtlSample ds; ds.t_us = T(1200);
        c2.shaftDtl.samples = { ds };
        DtlBand db; db.loUs = T(1000); db.hiUs = T(1400); db.name = QStringLiteral("P2-P3");
        c2.shaftDtl.bands = { db };
        DtlSynth3DSample sy; sy.t_us = T(1300);
        c2.shaftDtl.synth3d = { sy };
        c2.versions.shaftFusion = 1;
        fusion::Sample3D s3; s3.t_us = T(1250);
        c2.shaft3d.samples = { s3 };
        c2.shaft3d.eta.fitted = true;
        c2.shaft3d.eta.knotsUs = { T(1000), T(2000) };
        c2.shaft3d.eta.values = { 1.0, 2.0 };
        c2.shaft3d.eta.near = { 0, 1 };
        c2.versions.skeleton3d = 1;
        c2.skeleton3d.valid = true;
        c2.skeleton3d.t_us = { T(1100), T(1150) };
        c2.skeleton3d.theta.resize(2); c2.skeleton3d.joints.resize(2); c2.skeleton3d.tier.resize(2);
        c2.skeleton3d.sigmaM.resize(2); c2.skeleton3d.flags.resize(2); c2.skeleton3d.grip.resize(2);
        c2.skeleton3d.shaftDir.resize(2); c2.skeleton3d.shaftTier.resize(2);
        c2.versions.addressMarks = 3;
        c2.addressMarks.faceOn.frameTUs = T(500);
        c2.addressMarks.dtl.frameTUs = T(600);
        addressmarks::DtlPhaseMarks pm; pm.v.frameTUs = T(700); pm.eventTUs = T(720);
        c2.addressMarks.dtlPhases = { pm };
        const QJsonObject c2Clock = writeBlock(c2, kT0);
        for (const char *k : { "clubDtl", "club3d", "skeleton3d", "addressMarks" })
            check(c2Clock.contains(QLatin1String(k)), "the writer-only block is written");
        SwingAnalysis c2reb = c2;
        rebaseToWindowTime(c2reb, kT0);
        checkEqual(writeBlock(c2reb, 0), c2Clock,
                   "clubDtl, club3d, skeleton3d, addressMarks: rebased then written == written with t0");
    }

    std::fprintf(stderr, "%s: %d failure(s)\n", g_fail ? "FAIL" : "PASS", g_fail);
    return g_fail ? 1 : 0;
}

// The document's `analysis` block, through the production composition (serializeAnalysis is
// internal to swing_doc_analysis.cpp; SwingDocWriter would need a store, so the test reaches it
// through the internal header).
#include "../swing_doc_internal.h"
static QJsonObject writeBlock(const SwingAnalysis &a, int64_t t0)
{
    return swingdoc_detail::serializeAnalysis(a, qint64(t0), QString(), QString());
}
