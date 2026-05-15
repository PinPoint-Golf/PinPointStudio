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
#include "source_stats.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace pinpoint {

class SourceRing {
public:
    SourceRing(SourceId id, size_t slot_count, size_t slot_max_bytes,
               std::atomic<bool>* capturing = nullptr);
    ~SourceRing() = default;

    SourceRing(const SourceRing&)            = delete;
    SourceRing& operator=(const SourceRing&) = delete;
    SourceRing(SourceRing&&)                 = delete;
    SourceRing& operator=(SourceRing&&)      = delete;

    // --- Producer side (single capture thread) ---

    struct WriteSlot {
        std::byte* data{nullptr};
        size_t     capacity{0};
        uint64_t   sequence{0};
        uint32_t*  bytes_written{nullptr};  // producer writes actual size here before publish
        int64_t*   timestamp_us{nullptr};   // producer writes timestamp here before publish
        bool       valid{false};            // false when buffer is paused
    };

    WriteSlot acquireWriteSlot() noexcept;
    WriteSlot getSlotByIndex(size_t slot_idx) noexcept;

    void publish(uint64_t sequence) noexcept;

    // --- Consumer side (zero-copy) ---

    struct ReadHandle {
        const std::byte* data{nullptr};
        size_t           bytes{0};
        int64_t          timestamp_us{0};
        uint64_t         generation_snapshot{0};

        bool validate(const SourceRing& ring) const noexcept;

        // Copy up to len bytes from the slot payload into dst.
        // Intentionally racy (seqlock design) — validate() detects corruption afterwards.
        // Named function so TSAN suppression can target it precisely.
        void copyBytesRacy(void* dst, size_t len) const noexcept;

    private:
        friend class SourceRing;
        const SourceRing* ring_{nullptr};
        size_t            slot_idx_{0};
    };

    ReadHandle acquireReadHandle(uint64_t sequence) const noexcept;
    uint64_t   latestSequence() const noexcept;
    bool       peekTimestamp(uint64_t sequence, int64_t& out) const noexcept;

    // --- Observability ---
    const SourceStats& stats()    const noexcept { return stats_; }
    SourceId           id()       const noexcept { return source_id_; }

    // --- DMA registration ---
    std::vector<std::byte*> getSlotPointers() const;
    size_t slotCapacity() const noexcept { return slot_max_bytes_; }
    size_t slotCount()    const noexcept { return slot_count_; }
    size_t slotStride()   const noexcept { return slot_stride_; }

private:
    struct alignas(64) SlotHeader {
        std::atomic<uint64_t> generation{0};  // even = stable, odd = being written
        int64_t  timestamp_us{0};
        uint32_t bytes_written{0};
        uint8_t  _pad[64
                      - sizeof(std::atomic<uint64_t>)
                      - sizeof(int64_t)
                      - sizeof(uint32_t)]{};
    };
    static_assert(sizeof(SlotHeader) == 64, "SlotHeader must be exactly 64 bytes");

    struct AlignedDeleter {
        void operator()(std::byte* p) const noexcept {
#ifdef _MSC_VER
            _aligned_free(p);
#else
            std::free(p);
#endif
        }
    };

    SlotHeader& slotHeaderAt(size_t slot_idx) noexcept {
        return *reinterpret_cast<SlotHeader*>(storage_.get() + slot_idx * slot_stride_);
    }

    const SlotHeader& slotHeaderAt(size_t slot_idx) const noexcept {
        return *reinterpret_cast<const SlotHeader*>(storage_.get() + slot_idx * slot_stride_);
    }

    std::byte* payloadAt(size_t slot_idx) noexcept {
        return storage_.get() + slot_idx * slot_stride_ + sizeof(SlotHeader);
    }

    const std::byte* payloadAt(size_t slot_idx) const noexcept {
        return storage_.get() + slot_idx * slot_stride_ + sizeof(SlotHeader);
    }

    SourceId source_id_;
    size_t   slot_count_;
    size_t   slot_mask_;
    size_t   slot_max_bytes_;
    size_t   slot_stride_;

    std::unique_ptr<std::byte[], AlignedDeleter> storage_;

    alignas(64) std::atomic<uint64_t> write_seq_{0};
    SourceStats stats_;

    std::atomic<bool>* capturing_{nullptr};
};

} // namespace pinpoint
