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

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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

constexpr int64_t kT0 = 4'815'162'342LL;   // a ring-clock window start (µs), far above any window time

// Every field each writer emits, at window time + `base` (0 = window time, kT0 = the ring clock).
// Unset markers (−1) stay −1 on both clocks, as the stages leave them.
static SwingAnalysis makeAnalysis(int64_t base)
{
    const auto T = [base](int64_t rel) { return base + rel; };
    SwingAnalysis a;
    a.timeBase = base == 0 ? TimeBase::Window : TimeBase::Clock;
    a.tier = int(ReconstructionTier::Mono3DPlusImu);

    a.path.origin = QStringLiteral("reanalysis");
    a.path.forced = true;
    a.path.fullWindow = true;
    a.path.overrides = { QStringLiteral("pose.crop.enabled") };
    a.path.framesFaceOn = QStringLiteral("raw");
    a.path.framesDtl = QStringLiteral("mp4");
    a.path.reuse.insert(QStringLiteral("pose"), QStringLiteral("reused"));
    a.path.reuse.insert(QStringLiteral("shaft"), QStringLiteral("version 12, want 13"));
    a.path.pose = QStringLiteral("recorded");
    a.path.ladder = QStringLiteral("vision");
    a.path.witness = QStringLiteral("traced");
    a.path.trackerSpan = QStringLiteral("bounded");

    a.versions.pose = 2; a.versions.ball = 2; a.versions.shaft = 13; a.versions.impact = 1;
    a.versions.poseDtl = 3; a.versions.shaftDirection = 1; a.versions.skeleton3dBody = 2;
    a.versions.hipInternalRotation = 1; a.versions.shoulderTiltVsBend = 1; a.versions.clubSlotTop = 1;
    a.versions.dtlPosture = 4;
    a.versions.poseModel = QStringLiteral("vitpose_b.onnx@123"); a.versions.poseScope = QStringLiteral("span");
    a.versions.poseDtlModel = QStringLiteral("vitpose_l.onnx@456");

    a.score.kind = ScoreKind::Resemblance;
    a.score.overall = 71;
    a.score.resemblance.insert(QStringLiteral("bowed"), 71);
    a.score.resemblance.insert(QStringLiteral("cupped"), 22);
    a.score.patternLabel = QStringLiteral("bowed");
    a.score.blended = true;
    a.score.interval.halfWidth = 9; a.score.interval.lo = 62; a.score.interval.hi = 80;
    a.score.perRegion.insert(QStringLiteral("wrist"), 70);
    a.score.perPhase.insert(QStringLiteral("impact"), 65);

    MetricSeries m;
    m.key = QStringLiteral("leadWristFlexExt"); m.label = QStringLiteral("Lead wrist"); m.unit = QStringLiteral("°");
    m.t_us = { T(1000), T(6000), T(11000) };
    m.value = { 0.5, 12.25, -8.0 };
    PhaseSample ps{ Phase::Impact, T(11000), -8.0, QStringLiteral("green") };
    ps.sigma = 1.5; ps.sigmaKind = 2; ps.grossRisk = 0.125f;
    m.phaseSamples.push_back(ps);
    m.phaseSamples.push_back({ Phase::Top, T(6000), 12.25, QString() });
    m.sigma = 0.75; m.sigmaKind = 1;
    m.valid = { 1, 0, 1 };
    m.routeId = QStringLiteral("wristImu"); m.routeDirect = true;
    m.withheld = QStringLiteral("KB-18"); m.lineAngle = true;
    a.series.push_back(m);
    MetricSeries bare;
    bare.key = QStringLiteral("tempoRatio"); bare.unit = QStringLiteral(":1");
    bare.phaseSamples.push_back({ Phase::Impact, T(11000), 3.0, QString() });
    a.series.push_back(bare);

    KsNode pel;
    pel.segment = SeqSegment::Pelvis; pel.placed = true; pel.tPeakUs = T(9000);
    pel.beforeImpactMs = 2.0; pel.peakDps = 450.5; pel.tSigmaMs = 4.0; pel.peakSigmaDps = 12.0;
    pel.routeId = QStringLiteral("pelvisImu"); pel.direct = true;
    KsNode thx;
    thx.segment = SeqSegment::Thorax; thx.placed = false; thx.tPeakUs = T(9500);
    thx.peakNoEarlierThanMs = 30.0;
    thx.probable = true; thx.probableMs = -12.0; thx.probableSigmaMs = 3.0;
    thx.probableDps = 610.0; thx.probableSigmaDps = 25.0; thx.probableHow = QStringLiteral("rising");
    thx.bridgeMs = { -20.0, -10.0 }; thx.bridgeMean = { 500.5, 600.5 }; thx.bridgeSd = { 10.5, 20.5 };
    thx.earlierMs = -40.0; thx.earlierDps = 620.0;
    a.kinematicSequence.valid = true;
    a.kinematicSequence.impactUs = T(11000);
    a.kinematicSequence.nodes = { pel, thx };
    a.kinematicSequence.order = { SeqSegment::Pelvis };
    a.kinematicSequence.orderResolved = false;
    a.kinematicSequence.verdict = QStringLiteral("unresolved");
    a.kinematicSequence.routeSummary = QStringLiteral("mixed");
    a.kinematicSequence.pelvisDecelerates = 1;

    GateRecord g;
    g.gate = QStringLiteral("sequence.sightedBand"); g.subject = QStringLiteral("ks.thorax");
    g.effect = GateEffect::Bounded;
    g.evidence.insert(QStringLiteral("code"), QStringLiteral("edge"));
    g.bound.insert(QStringLiteral("noEarlierThanMs"), 30.0);
    g.blocks = { QStringLiteral("thoraxPeak") }; g.covers = { QStringLiteral("thoraxRate") };
    a.gates = { g };
    a.gatesVersion = 3;

    a.segmentation.swingStartUs = T(500);
    a.segmentation.swingEndUs = T(14000);
    a.segmentation.conf = 0.75f;
    a.segmentation.version = 5;   // fused: phases carry `timing`
    a.segmentation.fusion.push_back({ Phase::Top, SegmentRole::LeadHand, SegmentRole::Club, 1500, FusionReason(1) });
    a.phases.push_back({ Phase::Address, T(800), 0.5f, SegmentRole::Club });
    a.phases.push_back({ Phase::Top, T(6000), 0.875f, SegmentRole::LeadHand });
    a.phases.back().timing = TimingClass(1);
    a.phases.push_back({ Phase::Impact, T(11000), 1.0f });
    a.segmentation.events = a.phases;

    a.assessmentScore = 64;
    a.filterImpactStepDeg = 2.5;
    a.timings.totalMs = 900; a.timings.poseMs = 400;
    a.timings.stages.push_back({ QStringLiteral("Pose"), true, QStringLiteral("computed"), QString(), 400.0, 0.0, 400.0, 1 });
    BindingRecord b;
    b.serial = QStringLiteral("WT901-1"); b.role = SegmentRole::LeadHand; b.calibrated = true;
    a.bindings = { b };

    const auto poseFrame = [&T](int64_t rel, double off) {
        PoseFrame2D f;
        f.t_us = T(rel);
        for (int j = 0; j < kWholeBodyJoints; ++j) {
            f.kp[size_t(j)] = QPointF(0.25 + off + j / 1024.0, 0.5 - j / 2048.0);
            f.conf[size_t(j)] = float(j % 4) * 0.25f;
        }
        f.leadHand = QPointF(0.375, 0.625); f.trailHand = QPointF(0.4375, 0.5625); f.handConf = 0.75f;
        return f;
    };
    a.pose2d.camera = 3;
    a.pose2d.frames = { poseFrame(1000, 0.0), poseFrame(5000, 0.0625) };
    a.pose2d.decode = QStringLiteral("dark");
    a.pose2d.cropRect = QRectF(0.125, 0.25, 0.5, 0.625);
    a.pose2d.smoothed = a.pose2d.frames;
    PoseKpAux aux;
    for (int j = 0; j < kWholeBodyJoints; ++j) { aux.tier[size_t(j)] = uint8_t(j % 3); aux.sigma[size_t(j)] = float(j) * 0.5f; }
    a.pose2d.smoothedAux = { aux, aux };
    a.pose2d.adaptFallbacks = 2;
    a.pose2d.smoothedSynth = { poseFrame(1000, 0.0), poseFrame(3000, 0.03125), poseFrame(5000, 0.0625) };
    a.poseDtl.camera = 5;
    a.poseDtl.frames = { poseFrame(2000, 0.125) };

    ShaftTrack2D &s = a.shaft;
    s.camera = 3; s.valid = true; s.frameWidth = 1024; s.frameHeight = 512;
    s.coverage = 0.75f; s.imuVisionCorr = 0.5f; s.modelVisionResidualDeg = 2.25f; s.measuredClubLenPx = 300.5f;
    ShaftSample2D ss;
    ss.t_us = T(2000); ss.gripPx = QPointF(512, 256); ss.headPx = QPointF(640, 384);
    ss.thetaRad = 0.5; ss.thetaDotRadS = 2.5; ss.visibleLenPx = 200.5; ss.conf = 0.75f; ss.flags = 0x101;
    ss.headConf = 0.5f; ss.headSigmaPx = 3.5f; ss.lineConf = 0.25f;
    ss.sigmaThetaDeg = 1.25f; ss.pGross = 0.0625f; ss.tier = 2;
    s.samples = { ss };
    ShaftSample2D sp = ss; sp.t_us = T(2500); sp.lineConf = -1.f; sp.headConf = -1.f; sp.headSigmaPx = -1.f;
    sp.sigmaThetaDeg = -1.f; sp.pGross = -1.f; sp.tier = -1; sp.thetaDotRadS = 0.0;
    s.predicted = { sp };
    ShaftSample2D sy = ss; sy.t_us = T(3000); sy.lineConf = -1.f;
    s.synth = { sy };
    s.lengths.ballPx = 310.5; s.lengths.fusedPx = 305.25; s.lengths.nEstimators = 3; s.lengths.ladderRung = 2;
    s.plane.valid = true; s.plane.channel = 0;
    s.plane.measured.fitted = true; s.plane.measured.iotaBackDeg = 45.5; s.plane.measured.sigmaDeltaDeg = 1.5;
    s.plane.measured.pNeedle = 0.25; s.plane.synth.rejectBack = 3;
    ShaftPosition pos;
    pos.p = 2; pos.t_us = T(4000); pos.gripPx = QPointF(256, 128); pos.headPx = QPointF(768, 256);
    pos.thetaRad = 1.25; pos.lenPx = 250.5; pos.conf = 0.875f; pos.sigmaThetaDeg = 2.5f; pos.sigmaLenPx = 4.5f;
    pos.stackN = 3; pos.source = 1; pos.timing = TimingClass(1); pos.sigmaTUs = 1500.f;
    s.positions = { pos };
    s.ballAnchored = true; s.addressBallPx = QPointF(128, 448); s.addressBallSigmaPx = 2.5f; s.synthKappa = 0.375f;
    s.onsetRule = 2; s.phaseRetries = 1; s.phaseSuspect = true; s.p1BallDeltaDeg = 3.5f; s.lenBallRatio = 1.125f;
    s.onsetTUs = T(1500); s.topTUs = -1;   // a found takeaway; no top
    s.handPairFixed = 2; s.handGlitchFixed = 1; s.ballSuspect = true; s.handsRetried = true;
    s.p8Refusal = 2; s.refusedReason = 0;
    s.wedgeObs.push_back({ T(10500), 0.75, ShaftWedgeObsKind::Lead, 2.5f });
    s.clubFilter.ran = true; s.clubFilter.version = 2; s.clubFilter.nEvidence = 40; s.clubFilter.nGated = 2;
    s.clubFilter.nDownweighted = 1; s.clubFilter.nisMean = 1.125; s.clubFilter.residualRmsDeg = 0.75;
    s.clubFilter.rateWobble = 0.0625; s.clubFilter.humpsRaw = 3; s.clubFilter.humpsFiltered = 1;

    a.ball.camera = 3;
    a.ball.frames.push_back({ T(1000), true, QPointF(0.5, 0.75), 0.015625f, 0.875f, 0.25f });
    a.ball.frames.push_back({ T(11500), false, QPointF(), 0.f, 0.f, -1.f });
    a.ball.launchTUs = T(11200);
    a.ball.launchCenter = QPointF(0.5, 0.75);

    ImpactTrack2D &im = a.impact;
    im.camera = 7; im.valid = true; im.width = 512; im.height = 256; im.mmPerPx = 0.25;
    ImpactSample2D is;
    is.t_us = T(10900); is.ballFound = true; is.ball = QPointF(0.5, 0.5); is.ballR = 0.0625f;
    is.clubFound = true; is.head = QPointF(0.25, 0.5); is.headSeen = true; is.atEdge = true;
    is.shaftA = QPointF(0.125, 0.25); is.shaftB = QPointF(0.25, 0.375);
    is.outline = { QPointF(0.125, 0.25), QPointF(0.25, 0.5), QPointF(0.375, 0.25) };
    ImpactSample2D ib; ib.t_us = T(11000);   // nothing seen
    im.frames = { is, ib };
    im.ballLeaveTUs = T(11100);
    im.ballRest = QPointF(0.5, 0.5); im.ballRestR = 0.0625f;
    im.pathValid = true;
    im.path.push_back({ T(10900), QPointF(0.25, 0.5), QPointF(0.25, 0.375), 0.5 });
    im.arcA = 0.5; im.arcB = -0.25; im.arcC = 0.125; im.thetaAtX0 = 0.75; im.thetaSlopePerX = -0.5;
    im.ribbon = { QPointF(0.25, 0.5), QPointF(0.375, 0.5) };
    im.pathHalfWidth = 0.03125; im.headAlongPx = 12.5; im.headAcrossPx = 4.5; im.headOffsetAssumed = false;
    im.headEvidenceFrames = 4; im.hoselFitFrames = 6; im.hoselDropped = 1; im.hoselResidualRmsPx = 0.75;

    a.dtlRuler.ran = true; a.dtlRuler.cmPerPx = 0.25; a.dtlRuler.source = QStringLiteral("dtlBall");
    a.dtlRuler.toward = 1.0; a.dtlRuler.scaleRatio = 1.5; a.dtlRuler.refused = QStringLiteral("shoulders");
    return a;
}

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
