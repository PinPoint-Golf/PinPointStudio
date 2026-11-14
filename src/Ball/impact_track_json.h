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

// THE ONE SERIALISER of `analysis.impact` (the impact camera's track, impact_camera_design.md §7)
// — the writer and its typed reader side by side, beside the producer (ImpactRunner). Refactor
// step 1.3a (information_model.md §8 "One serialiser per analysis block"). The document
// (swing_doc_analysis.cpp) and the live detail (pinpoint::impactTrackJson, swing_doc.h) write
// through impactTrackToAnalysisJson, so the overlay reads one shape from both surfaces.
//
// Coordinates are normalised to the CLIP frame. TIMES go through windowTimeUs (swing_analysis.h):
// `t0Us` 0 = as written. The path's `kind` is always "synth" and is not read back.

#include "../Analysis/swing_analysis.h"

#include <QJsonArray>
#include <QJsonObject>

namespace pinpoint::analysis {

// Written only for a valid track (the caller skips an invalid one).
inline QJsonObject impactTrackToAnalysisJson(const ImpactTrack2D &t, int64_t t0Us)
{
    const auto rel = [t0Us](int64_t v) -> qint64 { return qint64(windowTimeUs(v, t0Us)); };
    QJsonArray samples;
    for (const ImpactSample2D &s : t.frames) {
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
        for (const ImpactPathPoint &p : t.path)
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

// The typed reader: every field the writer emits comes back; a key the writer omits (no ball, no
// club, no path) reads as the in-memory default.
inline ImpactTrack2D impactTrackFromAnalysisJson(const QJsonObject &o, int64_t t0Us = 0)
{
    const auto at = [t0Us](const QJsonValue &v) { return windowTimeUs(int64_t(v.toDouble()), t0Us); };
    const auto pt = [](const QJsonObject &j, const char *x, const char *y) {
        return QPointF(j.value(QString::fromLatin1(x)).toDouble(), j.value(QString::fromLatin1(y)).toDouble());
    };
    ImpactTrack2D t;
    if (o.isEmpty()) return t;
    t.camera  = pinpoint::SourceId(o.value(QStringLiteral("camera")).toInt(int(pinpoint::kInvalidSourceId)));
    t.valid   = o.value(QStringLiteral("valid")).toBool();
    t.width   = o.value(QStringLiteral("width")).toInt();
    t.height  = o.value(QStringLiteral("height")).toInt();
    t.mmPerPx = o.value(QStringLiteral("mmPerPx")).toDouble();
    for (const QJsonValue &v : o.value(QStringLiteral("samples")).toArray()) {
        const QJsonObject so = v.toObject();
        ImpactSample2D s;
        s.t_us      = at(so.value(QStringLiteral("t_us")));
        s.ballFound = so.contains(QStringLiteral("bx"));
        if (s.ballFound) {
            s.ball  = pt(so, "bx", "by");
            s.ballR = float(so.value(QStringLiteral("br")).toDouble());
        }
        s.clubFound = so.contains(QStringLiteral("hx"));
        if (s.clubFound) {
            s.head     = pt(so, "hx", "hy");
            s.headSeen = so.value(QStringLiteral("hs")).toBool();
            s.atEdge   = so.value(QStringLiteral("edge")).toBool();
            const QJsonArray poly = so.value(QStringLiteral("poly")).toArray();
            for (int i = 0; i + 1 < poly.size(); i += 2)
                s.outline.emplace_back(poly[i].toDouble(), poly[i + 1].toDouble());
            s.shaftA = pt(so, "sax", "say");
            s.shaftB = pt(so, "sbx", "sby");
        }
        t.frames.push_back(std::move(s));
    }
    if (o.contains(QStringLiteral("ballLeaveTUs")))
        t.ballLeaveTUs = at(o.value(QStringLiteral("ballLeaveTUs")));
    const QJsonObject rest = o.value(QStringLiteral("ballRest")).toObject();
    if (!rest.isEmpty()) {
        t.ballRest  = pt(rest, "x", "y");
        t.ballRestR = float(rest.value(QStringLiteral("r")).toDouble());
    }
    const QJsonObject path = o.value(QStringLiteral("path")).toObject();
    t.pathValid = !path.isEmpty();
    if (t.pathValid) {
        for (const QJsonValue &v : path.value(QStringLiteral("points")).toArray()) {
            const QJsonObject p = v.toObject();
            ImpactPathPoint ip;
            ip.t_us     = at(p.value(QStringLiteral("t_us")));
            ip.head     = pt(p, "x", "y");
            ip.hosel    = pt(p, "hx", "hy");
            ip.thetaRad = p.value(QStringLiteral("th")).toDouble();
            t.path.push_back(ip);
        }
        for (const QJsonValue &v : path.value(QStringLiteral("ribbon")).toArray())
            t.ribbon.push_back(pt(v.toObject(), "x", "y"));
        const QJsonObject arc = path.value(QStringLiteral("arc")).toObject();
        t.arcA           = arc.value(QStringLiteral("a")).toDouble();
        t.arcB           = arc.value(QStringLiteral("b")).toDouble();
        t.arcC           = arc.value(QStringLiteral("c")).toDouble();
        t.thetaAtX0      = arc.value(QStringLiteral("thetaAtX0")).toDouble();
        t.thetaSlopePerX = arc.value(QStringLiteral("thetaSlopePerX")).toDouble();
        t.pathHalfWidth      = path.value(QStringLiteral("halfWidth")).toDouble();
        t.headAlongPx        = path.value(QStringLiteral("headAlongPx")).toDouble();
        t.headAcrossPx       = path.value(QStringLiteral("headAcrossPx")).toDouble();
        t.headOffsetAssumed  = path.value(QStringLiteral("headAssumed")).toBool(true);
        t.headEvidenceFrames = path.value(QStringLiteral("headEvidence")).toInt();
        t.hoselFitFrames     = path.value(QStringLiteral("hoselFit")).toInt();
        t.hoselDropped       = path.value(QStringLiteral("hoselDropped")).toInt();
        t.hoselResidualRmsPx = path.value(QStringLiteral("hoselResRmsPx")).toDouble();
    }
    return t;
}

} // namespace pinpoint::analysis
