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

// A switchable, bounded copy of one IMU lane's samples, taken on the I/O thread outside the
// EventBuffer — for the trunk ceremony recorder (trunk_imu_sequence_grf_design.md §6.7, §16.2
// W2.1c). The EventBuffer writes only while a session is capturing; the ceremony runs in session
// setup, before it, so it cannot be read from there.
//
// Producer: the device's I/O-thread data path (ImuInstance's quaternionUpdated lambda; the wG3's
// HmSessionWorker::writeSample), one push per sample, with the SAME host stamp and the SAME
// ImuSample the ring would get. `active` is the lock-free fast path — a tap that is off costs one
// relaxed load per sample — and is re-checked under the mutex so nothing lands after stop().
// Consumer: the GUI thread, which swaps the pending samples out (take) and keeps them itself.
//
// Bounded: at most `cap` samples are accepted between start() and stop(); the rest are counted in
// `overCap` and dropped, so a ceremony left running (setup closed mid-step) cannot grow without
// limit. Qt Core only; no device, no clock.

#include <QMutex>
#include <QMutexLocker>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "../Buffer/imu_sample.h"

namespace pinpoint {

struct ImuSampleTap
{
    std::atomic<bool> active { false };

    QMutex                 mutex;
    std::vector<int64_t>   tUs;          // guarded by mutex — pending, not yet taken
    std::vector<ImuSample> rows;         // guarded by mutex — parallel to tUs
    size_t                 cap      = 0; // guarded by mutex — the most start() lets in
    size_t                 accepted = 0; // guarded by mutex — accepted since start()
    uint64_t               overCap  = 0; // guarded by mutex — refused because the cap was reached

    void start(size_t maxSamples)
    {
        QMutexLocker lock(&mutex);
        tUs.clear(); rows.clear();
        cap = maxSamples; accepted = 0; overCap = 0;
        active.store(true, std::memory_order_release);
    }

    void stop()
    {
        QMutexLocker lock(&mutex);
        active.store(false, std::memory_order_release);
    }

    bool isActive() const { return active.load(std::memory_order_acquire); }

    // I/O thread. A short mutex and a push_back — no disk, no signal.
    void push(int64_t t, const ImuSample &s)
    {
        if (!active.load(std::memory_order_acquire))
            return;
        QMutexLocker lock(&mutex);
        if (!active.load(std::memory_order_relaxed))
            return;
        if (accepted >= cap) {
            ++overCap;
            return;
        }
        ++accepted;
        tUs.push_back(t);
        rows.push_back(s);
    }

    // GUI thread: move every pending sample out (appended to the caller's vectors) — a swap
    // under the lock, so the producer is never held for a copy. Returns overCap so far.
    uint64_t take(std::vector<int64_t> &tOut, std::vector<ImuSample> &rOut)
    {
        std::vector<int64_t>   t;
        std::vector<ImuSample> r;
        uint64_t over = 0;
        {
            QMutexLocker lock(&mutex);
            t.swap(tUs);
            r.swap(rows);
            over = overCap;
        }
        if (tOut.empty()) {
            tOut.swap(t);
            rOut.swap(r);
        } else {
            tOut.insert(tOut.end(), t.begin(), t.end());
            rOut.insert(rOut.end(), r.begin(), r.end());
        }
        return over;
    }
};

} // namespace pinpoint
