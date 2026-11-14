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

// THE ONE SERIALISER of three `analysis` blocks — `club` (the face-on shaft track: samples,
// predicted, synth, lengths, positions, plane, diag), `phases[]` and `segmentation` — each a
// writer and its typed reader side by side (refactor step 1.3a; information_model.md §8 "One
// serialiser per analysis block"). swing_doc_analysis.cpp composes the document from the writers;
// version-gated re-analysis (analysis_versions.h, swing_reanalyzer.cpp) rebuilds the RECORDED
// products from the readers, so a reused track is exactly the one that was written.
//
// Why the readers exist: the pose track has had one since the gate existed, the ball track too.
// The shaft track and the phase ladder did not, so every re-analysis — including a metrics-only
// one — re-ran the tracker on whatever frames the swing directory still held. For a swing with no
// raw sidecar that is the mp4, and on 2026-09-17 a sweep that only wanted to add four metric
// series replaced nine good live tracks with coasting ones because a compressed, dim club is not
// the club the live tracker saw.
//
// EXACT INVERSES, field for field; a field the writer omits (predicted / synth / positions when
// empty, lineConf when unmeasured, timing on an unfused ladder, fusion when empty) comes back as
// its in-memory default. TIMES: every writer and reader takes `t0Us` and goes through the one
// rule windowTimeUs (swing_analysis.h); 0 = as written (a document with `analysis.timeBase`), the
// document's clock.t0_us = a legacy document read once (analysisReadOriginUs, swing_doc.h).
//
// What a rebuilt shaft track does NOT carry: `addressPhaseFrame` and `onsetFloorFrame` (never
// persisted; both read −1 = dark). The Layer C synth tier is read as recorded, but the Shaft stage
// re-synthesises it from the samples on reuse (shaft_track_assembly.h resynthesizeLayerC) so the
// visualisation follows the current rule rather than the one in force when the swing was captured.
//
// Header-only (step 1.3a) so every target that writes a document links the pair without a source
// list entry of its own.

#include "swing_analysis.h"   // ShaftTrack2D, Segmentation, windowTimeUs
#include "uncertainty_json.h" // the σ keys of a sample

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

#include <algorithm>
#include <optional>

namespace pinpoint::analysis {

namespace recorded_detail {

inline QPointF pointPx(const QJsonValue &v, double w, double h)
{
    const QJsonArray a = v.toArray();
    if (a.size() < 2) return QPointF();
    return QPointF(a[0].toDouble() * w, a[1].toDouble() * h);
}

inline int64_t timeAt(const QJsonObject &o, const char *key, int64_t t0Us)
{
    return windowTimeUs(int64_t(o.value(QString::fromLatin1(key)).toDouble()), t0Us);
}

// One sample of `samples` / `predicted` / `synth`. The three arrays share a shape; the
// keys a tier never writes (lineConf on synth, headConf on predicted) default exactly as
// the in-memory struct does.
inline ShaftSample2D sampleFrom(const QJsonObject &o, double w, double h, int64_t t0Us)
{
    ShaftSample2D s;
    s.t_us         = timeAt(o, "t_us", t0Us);
    s.gripPx       = pointPx(o.value(QStringLiteral("grip")), w, h);
    s.headPx       = pointPx(o.value(QStringLiteral("head")), w, h);
    s.thetaRad     = o.value(QStringLiteral("theta")).toDouble();
    s.thetaDotRadS = o.value(QStringLiteral("thetaDot")).toDouble();
    s.visibleLenPx = o.value(QStringLiteral("lenPx")).toDouble();
    s.conf         = float(o.value(QStringLiteral("conf")).toDouble());
    s.flags        = uint16_t(o.value(QStringLiteral("flags")).toInt());
    s.headConf     = float(o.value(QStringLiteral("headConf")).toDouble(-1.0));
    s.headSigmaPx  = float(o.value(QStringLiteral("headSigma")).toDouble(-1.0));
    s.lineConf     = float(o.value(QStringLiteral("lineConf")).toDouble(-1.0));
    readShaftSigma(o, s);   // uncertainty design §7 — absent keys read as "not assessed"
    return s;
}

inline ShaftPlaneChannel planeChannelFrom(const QJsonObject &o)
{
    ShaftPlaneChannel c;
    c.fitted           = o.value(QStringLiteral("fitted")).toBool();
    c.iotaBackDeg      = o.value(QStringLiteral("iotaBackDeg")).toDouble();
    c.iotaDownDeg      = o.value(QStringLiteral("iotaDownDeg")).toDouble();
    c.deltaDeg         = o.value(QStringLiteral("deltaDeg")).toDouble();
    c.nodeBackDeg      = o.value(QStringLiteral("nodeBackDeg")).toDouble();
    c.nodeDownDeg      = o.value(QStringLiteral("nodeDownDeg")).toDouble();
    c.nBack            = o.value(QStringLiteral("nBack")).toInt();
    c.nDown            = o.value(QStringLiteral("nDown")).toInt();
    c.conicResidBack   = o.value(QStringLiteral("conicResidBack")).toDouble(-1.0);
    c.conicResidDown   = o.value(QStringLiteral("conicResidDown")).toDouble(-1.0);
    c.ratioBack        = o.value(QStringLiteral("ratioBack")).toDouble(-1.0);
    c.ratioDown        = o.value(QStringLiteral("ratioDown")).toDouble(-1.0);
    c.splitHalfBackDeg = o.value(QStringLiteral("splitHalfBackDeg")).toDouble();
    c.splitHalfDownDeg = o.value(QStringLiteral("splitHalfDownDeg")).toDouble();
    c.anchorsBack      = o.value(QStringLiteral("anchorsBack")).toInt();
    c.anchorsDown      = o.value(QStringLiteral("anchorsDown")).toInt();
    c.anchorConfMin    = float(o.value(QStringLiteral("anchorConfMin")).toDouble());
    c.rejectBack       = o.value(QStringLiteral("rejectBack")).toInt();
    c.rejectDown       = o.value(QStringLiteral("rejectDown")).toInt();
    c.sigmaIotaBackDeg = o.value(QStringLiteral("sigmaIotaBackDeg")).toDouble(-1.0);
    c.sigmaIotaDownDeg = o.value(QStringLiteral("sigmaIotaDownDeg")).toDouble(-1.0);
    c.sigmaDeltaDeg    = o.value(QStringLiteral("sigmaDeltaDeg")).toDouble(-1.0);
    c.pNeedle          = o.value(QStringLiteral("pNeedle")).toDouble(-1.0);
    return c;
}

} // namespace recorded_detail

// ── analysis.club ─────────────────────────────────────────────────────────────────────────────

// Whether the face-on track is written at all: a VALID track and a REFUSED one (valid=false +
// `refused`, kept for the lab); a track that merely failed the coverage gate, or has no samples or
// frame size to normalise by, is omitted. Consumers gate on `valid`, so a refused track draws nothing.
inline bool shaftTrackIsWritten(const ShaftTrack2D &t)
{
    return (t.valid || t.refusedReason) && !t.samples.empty() && t.frameWidth > 0 && t.frameHeight > 0;
}

// The `club` block. pose2d keypoints are already normalized 0..1 frame coords; club grip/head
// are normalized here by the camera dims so every consumer (replay overlay, reload) is
// resolution-independent. Call only when shaftTrackIsWritten().
inline QJsonObject shaftTrackToAnalysisJson(const ShaftTrack2D &t, int64_t t0Us)
{
    const auto rel = [t0Us](int64_t v) -> qint64 { return qint64(windowTimeUs(v, t0Us)); };
    const double iw = 1.0 / t.frameWidth, ih = 1.0 / t.frameHeight;
    QJsonArray samples;
    for (const ShaftSample2D &s : t.samples) {
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
        insertShaftSigma(so, s);   // uncertainty design §7 — only when assessed
        samples.append(so);
    }
    // R7 dual output (additive): the pure-model predicted series + its
    // agreement with the prior-free vision measurement. Same normalized shape
    // as `samples`; SwingLab consumes it for residual analysis / β̂ calibration.
    QJsonArray predicted;
    for (const ShaftSample2D &s : t.predicted)
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
    for (const ShaftSample2D &s : t.synth) {
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
        insertShaftSigma(so, s);
        synth.append(so);
    }
    // Multi-estimator club-length fusion (club_length_fusion.h) — identical
    // shape in both parity writers (shot_processor.cpp toLengthsDetail is the
    // live-detail twin). Always written, even on abstain (nEstimators==0,
    // fusedPx<0): readers treat <0 as absent.
    const ClubLengthEstimate &l = t.lengths;
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
    for (const ShaftPosition &p : t.positions) {
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
    auto planeChan = [](const ShaftPlaneChannel &c) {
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
    auto planeChanSig = [&planeChan](const ShaftPlaneChannel &c) {
        QJsonObject o = planeChan(c);
        if (c.sigmaIotaBackDeg >= 0.0) o.insert(QStringLiteral("sigmaIotaBackDeg"), c.sigmaIotaBackDeg);
        if (c.sigmaIotaDownDeg >= 0.0) o.insert(QStringLiteral("sigmaIotaDownDeg"), c.sigmaIotaDownDeg);
        if (c.sigmaDeltaDeg    >= 0.0) o.insert(QStringLiteral("sigmaDeltaDeg"),    c.sigmaDeltaDeg);
        if (c.pNeedle          >= 0.0) o.insert(QStringLiteral("pNeedle"),          c.pNeedle);
        return o;
    };
    const QJsonObject plane{
        { QStringLiteral("valid"),    t.plane.valid },
        { QStringLiteral("channel"),  t.plane.channel },
        { QStringLiteral("measured"), planeChanSig(t.plane.measured) },
        { QStringLiteral("synth"),    planeChanSig(t.plane.synth) } };
    QJsonObject clubObj{
        { QStringLiteral("camera"),        int(t.camera) },
        { QStringLiteral("valid"),         t.valid },
        { QStringLiteral("coverage"),      double(t.coverage) },
        { QStringLiteral("imuVisionCorr"), double(t.imuVisionCorr) },
        { QStringLiteral("modelVisionResidualDeg"), double(t.modelVisionResidualDeg) },
        // v3.4 (design §9.4): measured club length in px (grip-to-ball at
        // address) — a scale floor. -1 = unmeasured (no ball anchor).
        { QStringLiteral("measuredClubLenPx"), double(t.measuredClubLenPx) },
        { QStringLiteral("frameWidth"),    t.frameWidth },
        { QStringLiteral("frameHeight"),   t.frameHeight },
        { QStringLiteral("lengths"),       lengths },
        { QStringLiteral("plane"),         plane },
        { QStringLiteral("samples"),       samples },
        { QStringLiteral("predicted"),     predicted } };
    if (!positions.isEmpty()) clubObj.insert(QStringLiteral("positions"), positions);
    if (!synth.isEmpty())     clubObj.insert(QStringLiteral("synth"), synth);
    if (t.ballAnchored)
        clubObj.insert(QStringLiteral("addressBall"),
                       QJsonArray{ t.addressBallPx.x() * iw, t.addressBallPx.y() * ih });
    if (t.addressBallSigmaPx >= 0.f)
        clubObj.insert(QStringLiteral("addressBallSigmaPx"), double(t.addressBallSigmaPx));
    if (t.synthKappa >= 0.f)
        clubObj.insert(QStringLiteral("synthKappa"), double(t.synthKappa));
    // Robustness self-checks (2026-10-01, swing_analysis.h ShaftTrack2D): how the
    // phase model found its takeaway, whether it was suspect, and the two witness
    // checks. `refused` names the reason when the track was refused (valid=false).
    clubObj.insert(QStringLiteral("diag"), QJsonObject{
        { QStringLiteral("onsetRule"),      t.onsetRule },
        { QStringLiteral("phaseRetries"),   t.phaseRetries },
        { QStringLiteral("phaseSuspect"),   t.phaseSuspect },
        { QStringLiteral("p1BallDeltaDeg"), double(t.p1BallDeltaDeg) },
        { QStringLiteral("lenBallRatio"),   double(t.lenBallRatio) },
        { QStringLiteral("onsetTUs"),       t.onsetTUs >= 0 ? rel(t.onsetTUs) : -1.0 },
        { QStringLiteral("topTUs"),         t.topTUs   >= 0 ? rel(t.topTUs)   : -1.0 },
        { QStringLiteral("handPairFixed"),  t.handPairFixed },
        { QStringLiteral("handGlitchFixed"), t.handGlitchFixed },
        { QStringLiteral("ballSuspect"),    t.ballSuspect },
        { QStringLiteral("handsRetried"),   t.handsRetried } });
    // Why the tracker found no P8 (plane stack D6), only when it found none: a reused track keeps it.
    if (t.p8Refusal) {
        QJsonObject dg = clubObj.value(QStringLiteral("diag")).toObject();
        dg.insert(QStringLiteral("p8Refusal"), QString::fromLatin1(shaftP8RefusalName(t.p8Refusal)));
        clubObj.insert(QStringLiteral("diag"), dg);
    }
    if (t.refusedReason)
        clubObj.insert(QStringLiteral("refused"),
                       QString::fromLatin1(shaftRefusedReasonName(t.refusedReason)));
    // The blurred frames' timed edge readings (ShaftWedgeObs) as [t_us, theta, kind, sigmaDeg],
    // kind 0 trail / 1 mid / 2 lead. Written only when non-empty.
    if (!t.wedgeObs.empty()) {
        QJsonArray wo;
        for (const ShaftWedgeObs &w : t.wedgeObs)
            wo.append(QJsonArray{ rel(w.t_us), w.thetaRad, int(w.kind), double(w.sigmaDeg) });
        clubObj.insert(QStringLiteral("wedgeObs"), wo);
    }
    // The club rate filter's diagnostic (club_rate_filter.h) — developer only, written only when
    // the filter ran, so a filter-off document is byte-identical. shot_processor.cpp mirrors it.
    if (t.clubFilter.ran) {
        const ClubFilterStats &f = t.clubFilter;
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
    return clubObj;
}

// `club` is `analysis.club`. Returns a track with `samples` empty (and `valid` false)
// when the block is absent, has no samples, or carries no frame size to de-normalise by.
// Lossless since step 1.3a: `diag.onsetTUs` / `diag.topTUs` and the club rate filter's `filter`
// diagnostic are read back too (before, a reused track wrote them back as −1 / absent). Nothing
// downstream reads those three — they are the record, and a reused track now keeps it.
inline ShaftTrack2D shaftTrackFromAnalysisJson(const QJsonObject &club, pinpoint::SourceId camera,
                                               int64_t t0Us = 0)
{
    using namespace recorded_detail;
    ShaftTrack2D t;
    if (club.isEmpty()) return t;
    const int w = club.value(QStringLiteral("frameWidth")).toInt();
    const int h = club.value(QStringLiteral("frameHeight")).toInt();
    const QJsonArray samples = club.value(QStringLiteral("samples")).toArray();
    if (w <= 0 || h <= 0 || samples.isEmpty()) return t;

    t.camera                 = camera;
    t.frameWidth             = w;
    t.frameHeight            = h;
    t.valid                  = club.value(QStringLiteral("valid")).toBool();
    t.coverage               = float(club.value(QStringLiteral("coverage")).toDouble());
    t.imuVisionCorr          = float(club.value(QStringLiteral("imuVisionCorr")).toDouble());
    t.modelVisionResidualDeg = float(club.value(QStringLiteral("modelVisionResidualDeg")).toDouble(-1.0));
    t.measuredClubLenPx      = float(club.value(QStringLiteral("measuredClubLenPx")).toDouble(-1.0));

    const QJsonObject l = club.value(QStringLiteral("lengths")).toObject();
    if (!l.isEmpty()) {
        ClubLengthEstimate &e = t.lengths;
        e.ballPx           = l.value(QStringLiteral("ballPx")).toDouble(-1.0);
        e.bandPx           = l.value(QStringLiteral("bandPx")).toDouble(-1.0);
        e.headPx           = l.value(QStringLiteral("headP95Px")).toDouble(-1.0);
        e.posePx           = l.value(QStringLiteral("posePx")).toDouble(-1.0);
        e.priorPx          = l.value(QStringLiteral("priorPx")).toDouble(-1.0);
        e.fusedPx          = l.value(QStringLiteral("fusedPx")).toDouble(-1.0);
        e.fusedSigmaPx     = l.value(QStringLiteral("fusedSigmaPx")).toDouble(-1.0);
        e.fusedConf        = l.value(QStringLiteral("fusedConf")).toDouble();
        e.fusedInstantPx   = l.value(QStringLiteral("fusedInstantPx")).toDouble(-1.0);
        e.fusedInstantConf = l.value(QStringLiteral("fusedInstantConf")).toDouble();
        e.ladderRung       = l.value(QStringLiteral("ladderRung")).toInt();
        e.ladderLenPx      = l.value(QStringLiteral("ladderLenPx")).toDouble();
        e.nEstimators      = l.value(QStringLiteral("nEstimators")).toInt();
        e.priorN           = l.value(QStringLiteral("priorN")).toInt();
        e.headMeasN        = l.value(QStringLiteral("headMeasN")).toInt();
    }

    const QJsonObject p = club.value(QStringLiteral("plane")).toObject();
    if (!p.isEmpty()) {
        t.plane.valid    = p.value(QStringLiteral("valid")).toBool();
        t.plane.channel  = p.value(QStringLiteral("channel")).toInt(-1);
        t.plane.measured = planeChannelFrom(p.value(QStringLiteral("measured")).toObject());
        t.plane.synth    = planeChannelFrom(p.value(QStringLiteral("synth")).toObject());
    }

    for (const QJsonValue &v : samples) t.samples.push_back(sampleFrom(v.toObject(), w, h, t0Us));
    for (const QJsonValue &v : club.value(QStringLiteral("predicted")).toArray())
        t.predicted.push_back(sampleFrom(v.toObject(), w, h, t0Us));
    for (const QJsonValue &v : club.value(QStringLiteral("synth")).toArray())
        t.synth.push_back(sampleFrom(v.toObject(), w, h, t0Us));
    if (club.contains(QStringLiteral("addressBall"))) {
        t.addressBallPx = pointPx(club.value(QStringLiteral("addressBall")), w, h);
        t.ballAnchored  = true;
    }
    t.addressBallSigmaPx = float(club.value(QStringLiteral("addressBallSigmaPx")).toDouble(-1.0));
    t.synthKappa         = float(club.value(QStringLiteral("synthKappa")).toDouble(-1.0));
    // Robustness self-checks (2026-10-01): the diag block and the refusal reason
    // round-trip so a reused track keeps its verdict (valid is already false).
    const QJsonObject dg = club.value(QStringLiteral("diag")).toObject();
    if (!dg.isEmpty()) {
        t.onsetRule      = dg.value(QStringLiteral("onsetRule")).toInt();
        t.phaseRetries   = dg.value(QStringLiteral("phaseRetries")).toInt();
        t.phaseSuspect   = dg.value(QStringLiteral("phaseSuspect")).toBool();
        t.p1BallDeltaDeg = float(dg.value(QStringLiteral("p1BallDeltaDeg")).toDouble(-1.0));
        t.lenBallRatio   = float(dg.value(QStringLiteral("lenBallRatio")).toDouble(-1.0));
        t.handPairFixed   = dg.value(QStringLiteral("handPairFixed")).toInt();
        t.handGlitchFixed = dg.value(QStringLiteral("handGlitchFixed")).toInt();
        t.ballSuspect     = dg.value(QStringLiteral("ballSuspect")).toBool();
        t.handsRetried    = dg.value(QStringLiteral("handsRetried")).toBool();
        t.p8Refusal       = shaftP8RefusalFromName(dg.value(QStringLiteral("p8Refusal")).toString());   // D6
        // onsetTUs / topTUs: −1 = not found, written as −1 and kept as −1.
        const double onset = dg.value(QStringLiteral("onsetTUs")).toDouble(-1.0);
        const double top   = dg.value(QStringLiteral("topTUs")).toDouble(-1.0);
        if (onset >= 0) t.onsetTUs = windowTimeUs(int64_t(onset), t0Us);
        if (top   >= 0) t.topTUs   = windowTimeUs(int64_t(top), t0Us);
    }
    t.refusedReason = shaftRefusedReasonFromName(club.value(QStringLiteral("refused")).toString());
    const QJsonObject f = club.value(QStringLiteral("filter")).toObject();
    if (!f.isEmpty()) {
        ClubFilterStats &cf = t.clubFilter;
        cf.ran            = true;
        cf.version        = f.value(QStringLiteral("version")).toInt(cf.version);
        cf.nEvidence      = f.value(QStringLiteral("nEvidence")).toInt();
        cf.nGated         = f.value(QStringLiteral("nGated")).toInt();
        cf.nDownweighted  = f.value(QStringLiteral("nDownweighted")).toInt();
        cf.nisMean        = f.value(QStringLiteral("nisMean")).toDouble(-1.0);
        cf.residualRmsDeg = f.value(QStringLiteral("residualRmsDeg")).toDouble(-1.0);
        cf.rateWobble     = f.value(QStringLiteral("rateWobble")).toDouble(-1.0);
        cf.humpsRaw       = f.value(QStringLiteral("humpsRaw")).toInt(-1);
        cf.humpsFiltered  = f.value(QStringLiteral("humpsFiltered")).toInt(-1);
    }
    for (const QJsonValue &v : club.value(QStringLiteral("wedgeObs")).toArray()) {
        const QJsonArray a = v.toArray();
        if (a.size() < 4) continue;
        t.wedgeObs.push_back({ windowTimeUs(int64_t(a.at(0).toDouble()), t0Us), a.at(1).toDouble(),
                               ShaftWedgeObsKind(std::clamp(a.at(2).toInt(), 0, 2)),
                               float(a.at(3).toDouble()) });
    }

    for (const QJsonValue &v : club.value(QStringLiteral("positions")).toArray()) {
        const QJsonObject o = v.toObject();
        ShaftPosition pos;
        pos.p             = o.value(QStringLiteral("p")).toInt();
        pos.t_us          = timeAt(o, "t_us", t0Us);
        pos.gripPx        = pointPx(o.value(QStringLiteral("grip")), w, h);
        pos.headPx        = pointPx(o.value(QStringLiteral("head")), w, h);
        pos.thetaRad      = o.value(QStringLiteral("theta")).toDouble();
        pos.lenPx         = o.value(QStringLiteral("lenPx")).toDouble(-1.0);
        pos.conf          = float(o.value(QStringLiteral("conf")).toDouble());
        pos.sigmaThetaDeg = float(o.value(QStringLiteral("sigmaThetaDeg")).toDouble(-1.0));
        pos.sigmaLenPx    = float(o.value(QStringLiteral("sigmaLenPx")).toDouble(-1.0));
        pos.stackN        = o.value(QStringLiteral("stackN")).toInt();
        pos.source        = uint8_t(o.value(QStringLiteral("source")).toInt());
        pos.timing        = TimingClass(o.value(QStringLiteral("timing")).toInt(int(TimingClass::Measured)));
        pos.sigmaTUs      = float(o.value(QStringLiteral("sigmaTUs")).toDouble(-1.0));
        t.positions.push_back(pos);
    }
    return t;
}

// ── analysis.phases ───────────────────────────────────────────────────────────────────────────

// `phases[]`. `timing` (TimingClass — how the instant was OBTAINED, orthogonal to conf) is written
// ONLY on a fusion-arbitrated ladder (`fused`: segmentation.version >= 5). Every producer stamps
// the field in memory regardless, but emitting it unconditionally would put a new key on every
// phase of every swing and break the refine.fusion=false parity baseline the corpus gate rests on
// (timeline-fusion.md §8 gate 2). Absent ⇒ Measured, which is what the pre-fusion producers all
// effectively claimed.
inline QJsonArray phasesToJson(const std::vector<PhaseEvent> &phases, bool fused, int64_t t0Us)
{
    QJsonArray out;
    for (const PhaseEvent &e : phases) {
        QJsonObject po{ { QStringLiteral("phase"),   int(e.phase) },
                        { QStringLiteral("t_us"),    qint64(windowTimeUs(e.t_us, t0Us)) },
                        { QStringLiteral("conf"),    e.conf },
                        { QStringLiteral("segment"), int(e.provenance) } };
        if (fused)
            po.insert(QStringLiteral("timing"), int(e.timing));
        out.append(po);
    }
    return out;
}

inline std::vector<PhaseEvent> phasesFromJson(const QJsonArray &phases, int64_t t0Us = 0)
{
    std::vector<PhaseEvent> out;
    for (const QJsonValue &v : phases) {
        const QJsonObject o = v.toObject();
        PhaseEvent e;
        e.phase      = Phase(o.value(QStringLiteral("phase")).toInt());
        e.t_us       = recorded_detail::timeAt(o, "t_us", t0Us);
        e.conf       = float(o.value(QStringLiteral("conf")).toDouble(1.0));
        e.provenance = SegmentRole(o.value(QStringLiteral("segment")).toInt());
        // `timing` is written only on a fusion-arbitrated ladder; absent ⇒ Measured (phasesToJson).
        e.timing     = TimingClass(o.value(QStringLiteral("timing")).toInt(int(TimingClass::Measured)));
        out.push_back(e);
    }
    return out;
}

// ── analysis.segmentation ─────────────────────────────────────────────────────────────────────

// Whether the bounds block is written (v3 G2, design A.7): only when the swing span resolved.
// Missing block on reload = full-window bounds.
inline bool segmentationIsWritten(const Segmentation &s) { return s.swingEndUs > s.swingStartUs; }

// The `segmentation` block: the swing bounds + ladder meta. The ladder's events are `phases[]`.
inline QJsonObject segmentationToJson(const Segmentation &s, int64_t t0Us)
{
    QJsonObject seg{
        { QStringLiteral("swingStartUs"), qint64(windowTimeUs(s.swingStartUs, t0Us)) },
        { QStringLiteral("swingEndUs"),   qint64(windowTimeUs(s.swingEndUs, t0Us)) },
        { QStringLiteral("conf"),    double(s.conf) },
        { QStringLiteral("version"), s.version } };
    // Additive fusion audit trail (timeline_fusion.h): one entry per
    // arbitrated P-slot, RETENTIONS INCLUDED — the club-vs-IMU delta on a
    // slot that did NOT flip is exactly the calibration data the V2 σ path
    // needs (timeline-fusion.md §5, §9.2), and discarding it would repeat the
    // mistake this design exists to fix. Present only when a fusion pass
    // emitted something, so a fusion-dark or all-abstain run writes nothing.
    // `phase` is the Phase enum int (as phases[].phase), NOT the coaching
    // P-index; winner/loser are SegmentRole; reason is FusionReason. dtUs is a delta.
    if (!s.fusion.empty()) {
        QJsonArray fusion;
        for (const FusionDecision &d : s.fusion)
            fusion.append(QJsonObject{
                { QStringLiteral("phase"),  int(d.phase) },
                { QStringLiteral("winner"), int(d.winner) },
                { QStringLiteral("loser"),  int(d.loser) },
                { QStringLiteral("dtUs"),   qint64(d.deltaUs) },
                { QStringLiteral("reason"), int(d.reason) } });
        seg.insert(QStringLiteral("fusion"), fusion);
    }
    return seg;
}

// The typed reader of the `segmentation` block alone: bounds, conf, version and the fusion record.
// The ladder's events are `phases[]` (phasesFromJson); the caller puts them in `events`.
inline Segmentation segmentationFromJson(const QJsonObject &s, int64_t t0Us = 0)
{
    Segmentation seg;
    if (s.isEmpty()) return seg;
    seg.swingStartUs = recorded_detail::timeAt(s, "swingStartUs", t0Us);
    seg.swingEndUs   = recorded_detail::timeAt(s, "swingEndUs", t0Us);
    seg.conf         = float(s.value(QStringLiteral("conf")).toDouble());
    seg.version      = s.value(QStringLiteral("version")).toInt(seg.version);
    for (const QJsonValue &fv : s.value(QStringLiteral("fusion")).toArray()) {
        const QJsonObject o = fv.toObject();
        FusionDecision d;
        d.phase   = Phase(o.value(QStringLiteral("phase")).toInt());
        d.winner  = SegmentRole(o.value(QStringLiteral("winner")).toInt());
        d.loser   = SegmentRole(o.value(QStringLiteral("loser")).toInt());
        d.deltaUs = int64_t(o.value(QStringLiteral("dtUs")).toDouble());
        d.reason  = FusionReason(o.value(QStringLiteral("reason")).toInt());
        seg.fusion.push_back(d);
    }
    return seg;
}

// The RECORDED LADDER for version-gated reuse. `analysis` is the whole `analysis` object: the
// ladder is `phases[]` plus the `segmentation` bounds / conf / version / fusion record. nullopt
// when there are no phases.
inline std::optional<Segmentation> segmentationFromAnalysisJson(const QJsonObject &analysis, int64_t t0Us = 0)
{
    const QJsonArray phases = analysis.value(QStringLiteral("phases")).toArray();
    if (phases.isEmpty()) return std::nullopt;

    const QJsonObject s = analysis.value(QStringLiteral("segmentation")).toObject();
    Segmentation seg = segmentationFromJson(s, t0Us);
    seg.events = phasesFromJson(phases, t0Us);
    if (s.isEmpty()) {
        // No bounds block (a swing whose span never resolved): the events are still a ladder,
        // and `conf` must be positive for SegResolve to adopt it — take the weakest anchor's.
        float c = 1.f;
        for (const PhaseEvent &e : seg.events) c = std::min(c, e.conf);
        seg.conf = c;
    }
    return seg;
}

} // namespace pinpoint::analysis
