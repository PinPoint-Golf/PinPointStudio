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

// Where an offline pose pass spent its time, per camera: a plain value (copyable, +=,
// toJson/fromJson) the shot's analysis record holds (AnalysisTimings::poseFaceOn and poseDtl,
// swing_analysis.h), so it sits in the vocabulary. The accumulator and the scoped timer a run writes
// it through stay in Pose/pose_timing.h, which includes this. Qt Core only. Moved out of
// pose_timing.h unchanged (refactor 7.3, A-11).

#include <QJsonObject>
#include <QtGlobal>

namespace pinpoint::pose {

struct PoseTiming {
    double sessionBuildMs  = 0.0;   // ORT session construction (model load + EP init/compile)
    double sessionWaitMs   = 0.0;   // waited for the cached session — another run holding it,
                                    // or the launch warm-up still compiling it (CoreML MLProgram:
                                    // ≈20 s Release, 53 s Debug, 97 s cold); a first shot or
                                    // re-analysis right after launch shows it here, not in run
    double decodeMs        = 0.0;   // payload → BGR (demosaic / MP4 frame), summed over frames
    double preprocessMs    = 0.0;   // resize → RGB → float → normalise → NCHW, summed
    double runMs           = 0.0;   // ORT Session::Run(), summed
    double heatmapDecodeMs = 0.0;   // argmax/DARK decode of the 133 heatmaps, summed
    double totalMs         = 0.0;   // PoseRunner::run() wall time (the producer overlaps
                                    // the consumer, so the parts can sum past it)
    int    frames          = 0;     // frames that went through Run() successfully

    PoseTiming &operator+=(const PoseTiming &o)
    {
        sessionBuildMs  += o.sessionBuildMs;
        sessionWaitMs   += o.sessionWaitMs;
        decodeMs        += o.decodeMs;
        preprocessMs    += o.preprocessMs;
        runMs           += o.runMs;
        heatmapDecodeMs += o.heatmapDecodeMs;
        totalMs         += o.totalMs;
        frames          += o.frames;
        return *this;
    }
    friend PoseTiming operator+(PoseTiming a, const PoseTiming &b) { return a += b; }

    bool measured() const { return totalMs > 0.0 || frames > 0; }

    // Run() ms per inferred frame — the number steps 1–3 move. 0 with no frames.
    double runMsPerFrame() const { return frames > 0 ? runMs / frames : 0.0; }

    // Milliseconds rounded to 0.1 ms: enough to see a 1 ms/frame change on a
    // 200-frame pass, without a 15-digit tail in every swing document.
    static double r1(double v) { return double(qRound64(v * 10.0)) / 10.0; }

    QJsonObject toJson() const
    {
        return QJsonObject{
            { QStringLiteral("sessionBuildMs"),  r1(sessionBuildMs) },
            { QStringLiteral("sessionWaitMs"),   r1(sessionWaitMs) },
            { QStringLiteral("decodeMs"),        r1(decodeMs) },
            { QStringLiteral("preprocessMs"),    r1(preprocessMs) },
            { QStringLiteral("runMs"),           r1(runMs) },
            { QStringLiteral("heatmapDecodeMs"), r1(heatmapDecodeMs) },
            { QStringLiteral("totalMs"),         r1(totalMs) },
            { QStringLiteral("frames"),          frames } };
    }
    static PoseTiming fromJson(const QJsonObject &o)
    {
        PoseTiming t;
        t.sessionBuildMs  = o.value(QStringLiteral("sessionBuildMs")).toDouble();
        t.sessionWaitMs   = o.value(QStringLiteral("sessionWaitMs")).toDouble();
        t.decodeMs        = o.value(QStringLiteral("decodeMs")).toDouble();
        t.preprocessMs    = o.value(QStringLiteral("preprocessMs")).toDouble();
        t.runMs           = o.value(QStringLiteral("runMs")).toDouble();
        t.heatmapDecodeMs = o.value(QStringLiteral("heatmapDecodeMs")).toDouble();
        t.totalMs         = o.value(QStringLiteral("totalMs")).toDouble();
        t.frames          = o.value(QStringLiteral("frames")).toInt();
        return t;
    }
};

} // namespace pinpoint::pose
