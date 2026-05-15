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

#include "swing_window.h"
#include "event_buffer.h"

#include <algorithm>
#include <cstring>

namespace pinpoint {

SwingWindow::SwingWindow(const EventBuffer* buffer,
                         std::vector<IndexEntry> entries,
                         int64_t start_us,
                         int64_t end_us)
    : buffer_(buffer)
    , entries_(std::move(entries))
    , start_us_(start_us)
    , end_us_(end_us)
{}

SwingWindow::SwingWindow(SwingWindow&& o) noexcept
    : buffer_(o.buffer_)
    , entries_(std::move(o.entries_))
    , start_us_(o.start_us_)
    , end_us_(o.end_us_)
{
    o.buffer_ = nullptr;
}

SwingWindow& SwingWindow::operator=(SwingWindow&& o) noexcept {
    if (this != &o) {
        buffer_   = o.buffer_;
        entries_  = std::move(o.entries_);
        start_us_ = o.start_us_;
        end_us_   = o.end_us_;
        o.buffer_ = nullptr;
    }
    return *this;
}

std::vector<IndexEntry> SwingWindow::entriesFor(SourceId id) const {
    std::vector<IndexEntry> out;
    for (const auto& e : entries_) {
        if (e.source_id == id) out.push_back(e);
    }
    return out;
}

size_t SwingWindow::frameCount(SourceId camera_id) const noexcept {
    size_t n = 0;
    for (const auto& e : entries_)
        if (e.source_id == camera_id) ++n;
    return n;
}

size_t SwingWindow::imuSampleCount(SourceId imu_id) const noexcept {
    return frameCount(imu_id); // same logic — count by source
}

SourceRing::ReadHandle SwingWindow::payloadOf(const IndexEntry& e) const noexcept {
    if (!buffer_) return {};
    return buffer_->acquireReadHandle(e.source_id, e.source_sequence);
}

const FormatDescriptor& SwingWindow::formatOf(SourceId id) const noexcept {
    return buffer_->formatOf(id);
}

bool SwingWindow::interpolateImu(SourceId imu_id, int64_t target_us,
                                  std::byte* out, size_t out_bytes) const noexcept {
    if (!out || out_bytes == 0) return false;

    // Find the nearest entry before and after target_us for this source.
    const IndexEntry* prev = nullptr;
    const IndexEntry* next = nullptr;

    for (const auto& e : entries_) {
        if (e.source_id != imu_id) continue;
        if (e.timestamp_us <= target_us) {
            if (!prev || e.timestamp_us > prev->timestamp_us) prev = &e;
        } else {
            if (!next || e.timestamp_us < next->timestamp_us) next = &e;
        }
    }

    if (!prev || !next) return false;

    SourceRing::ReadHandle prev_h = payloadOf(*prev);
    SourceRing::ReadHandle next_h = payloadOf(*next);

    if (!prev_h.data || !next_h.data) return false;
    if (prev_h.bytes != out_bytes || next_h.bytes != out_bytes) return false;

    int64_t denom = next->timestamp_us - prev->timestamp_us;
    if (denom == 0) {
        std::memcpy(out, prev_h.data, out_bytes);
        return true;
    }

    // Byte-level linear interpolation. Correct for packed float32 fields only.
    double t = static_cast<double>(target_us - prev->timestamp_us)
             / static_cast<double>(denom);

    const auto* p = reinterpret_cast<const unsigned char*>(prev_h.data);
    const auto* n = reinterpret_cast<const unsigned char*>(next_h.data);
    auto*       o = reinterpret_cast<unsigned char*>(out);

    for (size_t i = 0; i < out_bytes; ++i)
        o[i] = static_cast<unsigned char>(
            static_cast<double>(p[i]) + t * (static_cast<double>(n[i])
                                             - static_cast<double>(p[i])));

    return prev_h.validate(*buffer_->sources_[imu_id]->ring)
        && next_h.validate(*buffer_->sources_[imu_id]->ring);
}

} // namespace pinpoint
