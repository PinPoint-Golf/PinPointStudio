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

#include "../Vocabulary/Analysis/pose_timing_types.h"   // PoseTiming (vocabulary since refactor 7.3)

#include <QElapsedTimer>
#include <QJsonObject>
#include <mutex>

namespace pinpoint::pose {

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
