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
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace pinpoint {

// Seqlock SPSC ring of IndexEntry. One writer (merger thread), many readers.
// Capacity must be a power of 2.
class TimelineIndex {
public:
    explicit TimelineIndex(size_t capacity = 8192);
    ~TimelineIndex() = default;

    TimelineIndex(const TimelineIndex&)            = delete;
    TimelineIndex& operator=(const TimelineIndex&) = delete;

    // Append — merger thread only. Returns the global_sequence assigned.
    uint64_t append(const IndexEntry& entry) noexcept;

    // latestSequence: write_seq_ - 1. Returns 0 when nothing written (ambiguous
    // with "seq 0 written" — use writeSequence() to distinguish).
    uint64_t latestSequence() const noexcept;

    // writeSequence: next sequence to be written. 0 means nothing written yet.
    uint64_t writeSequence() const noexcept;

    // capacity of the ring
    size_t capacity() const noexcept { return capacity_; }

    // tryRead: returns true and fills out if global_seq has been written and not
    // yet overrun. Safe to call from any thread concurrently.
    bool tryRead(uint64_t global_seq, IndexEntry& out) const noexcept;

    // snapshot: range query over [t_start_us, t_end_us]. Allocates. Analysis path only.
    std::vector<IndexEntry> snapshot(int64_t t_start_us, int64_t t_end_us) const;

    // reset: zero write_seq_ and all generation counters. Call only when no
    // producer is active (buffer Paused). seq_cst fence at end ensures visibility.
    void reset() noexcept;

private:
    struct alignas(64) Slot {
        std::atomic<uint64_t> generation{0};  // even = stable, odd = being written
        IndexEntry entry{};
        uint8_t _pad[64 - sizeof(std::atomic<uint64_t>) - sizeof(IndexEntry)]{};
    };
    static_assert(sizeof(Slot) == 64, "Slot must be exactly 64 bytes");

    size_t capacity_;
    size_t mask_;
    std::unique_ptr<Slot[]> slots_;

    alignas(64) std::atomic<uint64_t> write_seq_{0};

    Slot&       slotAt(size_t i)       noexcept { return slots_[i]; }
    const Slot& slotAt(size_t i) const noexcept { return slots_[i]; }
};

} // namespace pinpoint
