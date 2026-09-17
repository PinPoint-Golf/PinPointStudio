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

#include "recorded_products.h"

#include <QJsonArray>
#include <QJsonValue>

#include <algorithm>

namespace pinpoint::analysis {

namespace {

QPointF pointPx(const QJsonValue &v, double w, double h)
{
    const QJsonArray a = v.toArray();
    if (a.size() < 2) return QPointF();
    return QPointF(a[0].toDouble() * w, a[1].toDouble() * h);
}

// One sample of `samples` / `predicted` / `synth`. The three arrays share a shape; the
// keys a tier never writes (lineConf on synth, headConf on predicted) default exactly as
// the in-memory struct does.
ShaftSample2D sampleFrom(const QJsonObject &o, double w, double h)
{
    ShaftSample2D s;
    s.t_us         = int64_t(o.value(QStringLiteral("t_us")).toDouble());
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
    return s;
}

ShaftPlaneChannel planeChannelFrom(const QJsonObject &o)
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
    return c;
}

} // namespace

ShaftTrack2D shaftTrackFromAnalysisJson(const QJsonObject &club, pinpoint::SourceId camera)
{
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

    for (const QJsonValue &v : samples) t.samples.push_back(sampleFrom(v.toObject(), w, h));
    for (const QJsonValue &v : club.value(QStringLiteral("predicted")).toArray())
        t.predicted.push_back(sampleFrom(v.toObject(), w, h));
    for (const QJsonValue &v : club.value(QStringLiteral("synth")).toArray())
        t.synth.push_back(sampleFrom(v.toObject(), w, h));

    for (const QJsonValue &v : club.value(QStringLiteral("positions")).toArray()) {
        const QJsonObject o = v.toObject();
        ShaftPosition pos;
        pos.p             = o.value(QStringLiteral("p")).toInt();
        pos.t_us          = int64_t(o.value(QStringLiteral("t_us")).toDouble());
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
        t.positions.push_back(pos);
    }
    return t;
}

std::optional<Segmentation> segmentationFromAnalysisJson(const QJsonObject &analysis)
{
    const QJsonArray phases = analysis.value(QStringLiteral("phases")).toArray();
    if (phases.isEmpty()) return std::nullopt;

    Segmentation seg;
    for (const QJsonValue &v : phases) {
        const QJsonObject o = v.toObject();
        PhaseEvent e;
        e.phase      = Phase(o.value(QStringLiteral("phase")).toInt());
        e.t_us       = int64_t(o.value(QStringLiteral("t_us")).toDouble());
        e.conf       = float(o.value(QStringLiteral("conf")).toDouble(1.0));
        e.provenance = SegmentRole(o.value(QStringLiteral("segment")).toInt());
        // `timing` is written only on a fusion-arbitrated ladder; absent ⇒ Measured, which is
        // what every pre-fusion producer effectively claimed (swing_doc.cpp).
        e.timing     = TimingClass(o.value(QStringLiteral("timing")).toInt(int(TimingClass::Measured)));
        seg.events.push_back(e);
    }

    const QJsonObject s = analysis.value(QStringLiteral("segmentation")).toObject();
    if (!s.isEmpty()) {
        seg.swingStartUs = int64_t(s.value(QStringLiteral("swingStartUs")).toDouble());
        seg.swingEndUs   = int64_t(s.value(QStringLiteral("swingEndUs")).toDouble());
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
    } else {
        // No bounds block (a swing whose span never resolved): the events are still a ladder,
        // and `conf` must be positive for SegResolve to adopt it — take the weakest anchor's.
        float c = 1.f;
        for (const PhaseEvent &e : seg.events) c = std::min(c, e.conf);
        seg.conf = c;
    }
    return seg;
}

} // namespace pinpoint::analysis
