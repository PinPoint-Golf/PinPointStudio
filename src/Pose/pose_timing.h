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

// Where an offline pose pass spends its time (pose_inference_performance_plan.md
// step 0). Until this split existed the only number was PoseStage's poseMs —
// 1478–1767 ms for 181–215 face-on frames on 5 Oct (8.0 ms a frame end to end),
// with the DTL pass measured and never written — so whether the time went to the
// ORT session build, the Bayer demosaic, the resize/normalise, Run() or the 133
// DARK heatmap decodes was a guess. Every later step (session cache, producer
// pool, batching/fp16, frame budget) is judged by this split on the same swings.
//
// Header-only, Qt Core only: swing_analysis.h carries two PoseTiming values in
// AnalysisTimings, and the swing_doc / analysis test suites link no Pose code.
//
//   PoseTiming       — plain value (copyable, += , toJson/fromJson), one per camera.
//   PoseTimingSink   — the mutex-guarded accumulator a run writes into from its
//                      producer thread(s) (decode, preprocess) and its inference
//                      thread (run, heatmap, frames) at once.
//   PoseScopedTimer  — RAII: adds the scope's elapsed ms to one field of a sink;
//                      a null sink is a no-op (the live path never times).

#include <QElapsedTimer>
#include <QJsonObject>
#include <mutex>

namespace pinpoint::pose {

struct PoseTiming {
    double sessionBuildMs  = 0.0;   // ORT session construction (model load + EP init/compile)
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
        t.decodeMs        = o.value(QStringLiteral("decodeMs")).toDouble();
        t.preprocessMs    = o.value(QStringLiteral("preprocessMs")).toDouble();
        t.runMs           = o.value(QStringLiteral("runMs")).toDouble();
        t.heatmapDecodeMs = o.value(QStringLiteral("heatmapDecodeMs")).toDouble();
        t.totalMs         = o.value(QStringLiteral("totalMs")).toDouble();
        t.frames          = o.value(QStringLiteral("frames")).toInt();
        return t;
    }
};

// One producer (or a pool of them) + one inference thread write here at once.
// A mutex, not atomics: three short critical sections per frame against 8 ms of
// work per frame is noise, and it stays correct when step 2 adds producers.
class PoseTimingSink {
public:
    void add(double PoseTiming::*field, double ms)
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_t.*field += ms;
    }
    void addFrame()
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        ++m_t.frames;
    }
    PoseTiming snapshot() const
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        return m_t;
    }
    void reset()
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_t = PoseTiming{};
    }

private:
    mutable std::mutex m_mutex;
    PoseTiming         m_t;
};

// Adds the enclosing scope's wall time to sink->*field on destruction (or stop()).
class PoseScopedTimer {
public:
    PoseScopedTimer(PoseTimingSink *sink, double PoseTiming::*field)
        : m_sink(sink), m_field(field)
    {
        if (m_sink)
            m_timer.start();
    }
    ~PoseScopedTimer() { stop(); }
    // Bill the time so far and disarm (the destructor then adds nothing) — for a
    // span that ends before its scope does.
    void stop()
    {
        if (m_sink)
            m_sink->add(m_field, double(m_timer.nsecsElapsed()) / 1e6);
        m_sink = nullptr;
    }
    PoseScopedTimer(const PoseScopedTimer &) = delete;
    PoseScopedTimer &operator=(const PoseScopedTimer &) = delete;

private:
    PoseTimingSink        *m_sink;
    double PoseTiming::*   m_field;
    QElapsedTimer          m_timer;
};

} // namespace pinpoint::pose
