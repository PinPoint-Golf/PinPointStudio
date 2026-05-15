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
#include <cstddef>
#include <cstdint>

namespace pinpoint {

struct EventBufferConfig {
    uint32_t reorder_window_us        = 5000;   // 5ms reorder window
    uint32_t watchdog_interval_ms     = 10;
    uint32_t stall_threshold_mult     = 5;
    uint32_t timeline_index_capacity  = 8192;   // must be power of 2
    uint32_t merger_spin_iterations   = 64;
    // On macOS/iOS, this is the yield budget per merger iteration rather than
    // a blocking sleep duration. Lower values reduce idle CPU at the cost of
    // slightly higher minimum latency during truly idle periods.
    uint32_t merger_cold_timeout_us   = 500;
    uint32_t pause_drain_timeout_ms   = 20;
    bool     resume_clear_rings       = true;
    bool     cpu_affinity_enabled     = false;
};

} // namespace pinpoint
