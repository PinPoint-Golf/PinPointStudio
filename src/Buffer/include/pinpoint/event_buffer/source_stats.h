/*
 * Copyright (C) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#pragma once

#include <atomic>
#include <cstdint>

namespace pinpoint {

struct alignas(64) SourceStats {
    std::atomic<uint64_t> events_written{0};
    std::atomic<uint64_t> events_overwritten{0};
    std::atomic<uint64_t> bytes_written_total{0};
    std::atomic<int64_t>  last_write_timestamp_us{0};
    std::atomic<int64_t>  max_inter_arrival_us{0};
    std::atomic<uint64_t> bounds_violations{0};
    std::atomic<uint64_t> monotonicity_violations{0};

    void updateInterArrival(int64_t timestamp_us) noexcept {
        int64_t prev = last_write_timestamp_us.load(std::memory_order_relaxed);
        if (prev > 0) {
            int64_t delta = timestamp_us - prev;
            if (delta > 0) {
                int64_t cur_max = max_inter_arrival_us.load(std::memory_order_relaxed);
                if (delta > cur_max) {
                    max_inter_arrival_us.store(delta, std::memory_order_relaxed);
                }
            }
        }
        last_write_timestamp_us.store(timestamp_us, std::memory_order_relaxed);
    }
};

static_assert(sizeof(SourceStats) <= 128, "SourceStats must fit in two cache lines");

} // namespace pinpoint
