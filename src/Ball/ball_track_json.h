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

// THE ONE SERIALISER of `analysis.ball` (the face-on ball track, v3.4 design §9) — the writer and
// its typed reader side by side, beside the producer (BallRunner / the live accumulator). Refactor
// step 1.3a (information_model.md §8 "One serialiser per analysis block"). swing_doc_analysis.cpp
// writes through ballTrackToAnalysisJson; BallRunner::fromAnalysisJson (version-gated reuse) and
// the document's typed read go through ballTrackFromAnalysisJson.
//
// Shape: { camera, valid: true, launchTUs, samples[{t_us, x, y, r, conf, found, [act]}],
// [launch{x, y}] } — normalized [0,1] full-frame centre + radius; found=false marks the
// post-launch gap. TIMES go through windowTimeUs (swing_analysis.h): `t0Us` 0 = as written.

#include "../Analysis/swing_analysis.h"

#include <QJsonArray>
#include <QJsonObject>

namespace pinpoint::analysis {

// Written only when the track has frames (the caller skips an empty one).
inline QJsonObject ballTrackToAnalysisJson(const BallTrack2D &b, int64_t t0Us)
{
    const auto rel = [t0Us](int64_t v) -> qint64 { return qint64(windowTimeUs(v, t0Us)); };
    QJsonArray samples;
    for (const BallSample2D &s : b.frames) {
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
        { QStringLiteral("camera"),    int(b.camera) },
        { QStringLiteral("valid"),     true },
        { QStringLiteral("launchTUs"), rel(b.launchTUs) },
        { QStringLiteral("samples"),   samples } };
    // Additive (2026-09-09): the pre-launch ball centre the impact anchor reads,
    // so a reloaded track carries it (BallRunner::fromAnalysisJson).
    if (b.launchTUs >= 0)
        ballObj.insert(QStringLiteral("launch"), QJsonObject{ { QStringLiteral("x"), b.launchCenter.x() },
                                                              { QStringLiteral("y"), b.launchCenter.y() } });
    return ballObj;
}

// The typed reader. `camera` is the block's own; a caller binding the track to a live source sets it.
inline BallTrack2D ballTrackFromAnalysisJson(const QJsonObject &ball, int64_t t0Us = 0)
{
    BallTrack2D track;
    track.camera = pinpoint::SourceId(ball.value(QStringLiteral("camera")).toInt(int(pinpoint::kInvalidSourceId)));
    const QJsonArray samples = ball.value(QStringLiteral("samples")).toArray();
    track.frames.reserve(size_t(samples.size()));
    for (const QJsonValue &fv : samples) {
        const QJsonObject o = fv.toObject();
        BallSample2D s;
        s.t_us       = windowTimeUs(int64_t(o.value(QStringLiteral("t_us")).toDouble()), t0Us);
        s.found      = o.value(QStringLiteral("found")).toBool();
        s.center     = QPointF(o.value(QStringLiteral("x")).toDouble(), o.value(QStringLiteral("y")).toDouble());
        s.radiusNorm = float(o.value(QStringLiteral("r")).toDouble());
        s.conf       = float(o.value(QStringLiteral("conf")).toDouble());
        s.clubActivity = o.contains(QStringLiteral("act")) ? float(o.value(QStringLiteral("act")).toDouble()) : -1.f;
        track.frames.push_back(s);
    }
    track.launchTUs = ball.contains(QStringLiteral("launchTUs"))
                          ? windowTimeUs(int64_t(ball.value(QStringLiteral("launchTUs")).toDouble()), t0Us) : -1;
    const QJsonObject launch = ball.value(QStringLiteral("launch")).toObject();
    if (!launch.isEmpty())
        track.launchCenter = QPointF(launch.value(QStringLiteral("x")).toDouble(), launch.value(QStringLiteral("y")).toDouble());
    else if (track.launchTUs >= 0) {
        // pre-2026-09-09 records carried no launch centre: the last found sample at or before launch
        for (const BallSample2D &s : track.frames)
            if (s.found && s.t_us <= track.launchTUs) track.launchCenter = s.center;
    }
    return track;
}

} // namespace pinpoint::analysis
