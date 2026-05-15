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

#include "types.h"
#include "source_ring.h"
#include "format_descriptor.h"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace pinpoint {

class EventBuffer;

// Frozen, zero-copy, bounded view of a time range within a paused EventBuffer.
// Only valid while the buffer remains in Paused state. Payload access after
// resume() is undefined — destroy the window before calling resume().
class SwingWindow {
public:
    SwingWindow(const SwingWindow&)            = delete;
    SwingWindow& operator=(const SwingWindow&) = delete;

    SwingWindow(SwingWindow&&) noexcept;
    SwingWindow& operator=(SwingWindow&&) noexcept;

    ~SwingWindow() = default;

    int64_t startTimestampUs() const noexcept { return start_us_; }
    int64_t endTimestampUs()   const noexcept { return end_us_; }
    std::chrono::microseconds duration() const noexcept {
        return std::chrono::microseconds(end_us_ - start_us_);
    }

    std::span<const IndexEntry> entries() const noexcept {
        return {entries_.data(), entries_.size()};
    }

    std::vector<IndexEntry> entriesFor(SourceId id) const;

    size_t frameCount(SourceId camera_id) const noexcept;
    size_t imuSampleCount(SourceId imu_id) const noexcept;

    // Zero-copy payload access. Valid for the window's lifetime (buffer is frozen).
    // Returns a handle with data=nullptr if the entry is not in this window.
    SourceRing::ReadHandle payloadOf(const IndexEntry& e) const noexcept;

    const FormatDescriptor& formatOf(SourceId id) const noexcept;

    // Linear interpolation between the two nearest IMU samples for imu_id.
    // Byte-level interpolation — correct for packed float32 fields only.
    // Returns false if insufficient data exists or out_bytes mismatches packet size.
    bool interpolateImu(SourceId imu_id, int64_t target_us,
                        std::byte* out, size_t out_bytes) const noexcept;

private:
    friend class EventBuffer;

    SwingWindow(const EventBuffer* buffer,
                std::vector<IndexEntry> entries,
                int64_t start_us,
                int64_t end_us);

    const EventBuffer*      buffer_;
    std::vector<IndexEntry> entries_;
    int64_t                 start_us_;
    int64_t                 end_us_;
};

} // namespace pinpoint
