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

#include "timeline_index.h"
#include <algorithm>
#include <cassert>
#include <stdexcept>

namespace pinpoint {

TimelineIndex::TimelineIndex(size_t capacity)
    : capacity_(capacity)
    , mask_(capacity - 1)
    , slots_(std::make_unique<Slot[]>(capacity))
{
    if ((capacity & (capacity - 1)) != 0)
        throw std::invalid_argument("TimelineIndex capacity must be a power of 2");
}

uint64_t TimelineIndex::append(const IndexEntry& entry) noexcept {
    // SPSC writer: load seq WITHOUT advancing write_seq_ yet.
    // write_seq_ is advanced AFTER gen=even so any reader that loads write_seq_
    // via acquire is guaranteed to see gen as even and the entry as fully written.
    uint64_t seq      = write_seq_.load(std::memory_order_relaxed);
    size_t   slot_idx = seq & mask_;
    Slot&    slot     = slotAt(slot_idx);

    // Mark in-progress (odd) BEFORE write_seq_ becomes visible to readers.
    uint64_t gen = slot.generation.load(std::memory_order_relaxed);
    slot.generation.store(gen | 1u, std::memory_order_relaxed);

    slot.entry                 = entry;
    slot.entry.global_sequence = seq;

    // Mark stable with release: synchronises with reader's acquire load of gen.
    slot.generation.fetch_add(1, std::memory_order_release);

    // Advance write_seq_ AFTER gen=even, with release.
    // Readers that load write_seq_ >= seq+1 (acquire) are guaranteed to see the
    // slot as fully written with gen=even.
    write_seq_.store(seq + 1, std::memory_order_release);

    return seq;
}

uint64_t TimelineIndex::latestSequence() const noexcept {
    uint64_t ws = write_seq_.load(std::memory_order_acquire);
    return ws == 0 ? 0 : ws - 1;
}

uint64_t TimelineIndex::writeSequence() const noexcept {
    return write_seq_.load(std::memory_order_acquire);
}

bool TimelineIndex::tryRead(uint64_t global_seq, IndexEntry& out) const noexcept {
    uint64_t ws = write_seq_.load(std::memory_order_acquire);
    if (ws <= global_seq) return false; // not yet written

    size_t      slot_idx = global_seq & mask_;
    const Slot& slot     = slotAt(slot_idx);

    uint64_t gen_before = slot.generation.load(std::memory_order_acquire);
    if (gen_before & 1u) return false; // slot being written

    // Overrun check: if write_seq_ has lapped us by more than capacity, the slot
    // has been reused for a different global_seq.
    uint64_t ws2 = write_seq_.load(std::memory_order_acquire);
    if ((ws2 - global_seq) > capacity_) return false;

    // Intentionally racy payload read — TSAN suppression covers this function.
    out = slot.entry;

    uint64_t gen_after = slot.generation.load(std::memory_order_acquire);
    return gen_after == gen_before;
}

std::vector<IndexEntry> TimelineIndex::snapshot(int64_t t_start_us,
                                                 int64_t t_end_us) const {
    uint64_t ws = write_seq_.load(std::memory_order_acquire);
    if (ws == 0) return {};

    // Walk from the oldest slot still in the ring up to the latest.
    uint64_t start_seq = (ws > capacity_) ? (ws - capacity_) : 0;

    std::vector<IndexEntry> result;
    result.reserve(std::min<size_t>(static_cast<size_t>(ws - start_seq), capacity_));

    for (uint64_t seq = start_seq; seq < ws; ++seq) {
        IndexEntry e;
        if (tryRead(seq, e) && e.timestamp_us >= t_start_us
                             && e.timestamp_us <= t_end_us) {
            result.push_back(e);
        }
    }

    std::sort(result.begin(), result.end(),
              [](const IndexEntry& a, const IndexEntry& b) {
                  return a.timestamp_us < b.timestamp_us;
              });
    return result;
}

void TimelineIndex::reset() noexcept {
    write_seq_.store(0, std::memory_order_relaxed);
    for (size_t i = 0; i < capacity_; ++i)
        slotAt(i).generation.store(0, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_seq_cst);
}

} // namespace pinpoint
