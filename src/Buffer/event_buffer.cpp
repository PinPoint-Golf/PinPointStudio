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

#include "event_buffer.h"
#include "platform.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <climits>
#include <stdexcept>
#include <thread>

#if defined(PINPOINT_PLATFORM_WINDOWS)
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
  #include <timeapi.h>
  #pragma comment(lib, "winmm.lib")
#endif

namespace pinpoint {

// ---------------------------------------------------------------------------
// Internal MergerState — not exposed in header
// ---------------------------------------------------------------------------

struct PendingEvent {
    SourceId source_id    = kInvalidSourceId;
    uint64_t source_seq   = 0;
    int64_t  timestamp_us = 0;
    bool     valid        = false;
};

class MergerState {
public:
    static constexpr size_t MAX = EventBuffer::MAX_SOURCES;

    std::array<PendingEvent, MAX> heads{};
    std::array<int64_t, MAX>      last_emitted_ts{};

    MergerState() { last_emitted_ts.fill(INT64_MIN); }

    // Linear scan — faster than a heap for N <= 16 (cache-hot, no allocation).
    int findMinValidIndex() const noexcept {
        int    best    = -1;
        int64_t min_ts = INT64_MAX;
        for (int i = 0; i < static_cast<int>(MAX); ++i) {
            if (heads[i].valid && heads[i].timestamp_us < min_ts) {
                best   = i;
                min_ts = heads[i].timestamp_us;
            }
        }
        return best;
    }

    void setHead(int idx, PendingEvent ev) noexcept { heads[idx] = ev; }
    void clearHead(int idx)               noexcept { heads[idx].valid = false; }

    // Clamps non-monotonic timestamps; records the violation via the ring.
    void enforceMonotonicity(int idx, PendingEvent& ev, SourceRing& ring) noexcept {
        if (ev.timestamp_us <= last_emitted_ts[idx]) {
            ring.recordMonotonicityViolation();
            ev.timestamp_us = last_emitted_ts[idx] + 1;
        }
        last_emitted_ts[idx] = ev.timestamp_us;
    }
};

// ---------------------------------------------------------------------------
// EventBuffer
// ---------------------------------------------------------------------------

EventBuffer::EventBuffer(EventBufferConfig cfg)
    : config_(cfg)
    , index_(cfg.timeline_index_capacity)
{
}

EventBuffer::~EventBuffer() {
    if (state_.load(std::memory_order_acquire) != BufferState::Idle)
        stop();
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

SourceId EventBuffer::registerSource(SourceDescriptor desc) {
    assert(state_.load(std::memory_order_acquire) == BufferState::Idle
           && "registerSource must be called before start()");

    if (source_count_ >= MAX_SOURCES)
        throw std::runtime_error("EventBuffer: MAX_SOURCES exceeded");

    SourceId id = static_cast<SourceId>(source_count_);
    desc.id = id;

    size_t slot_count = desc.computeSlotCount();
    size_t slot_bytes = desc.computeSlotBytes();

    auto slot  = std::make_unique<SourceSlot>();
    slot->desc = std::move(desc);
    slot->ring = std::make_unique<SourceRing>(id, slot_count, slot_bytes,
                                              &capturing_);
    sources_[source_count_++] = std::move(slot);
    return id;
}

// ---------------------------------------------------------------------------
// Producer API
// ---------------------------------------------------------------------------

int EventBuffer::findSlotIndex(SourceId id) const noexcept {
    // Source IDs are assigned sequentially from 0 == their index.
    if (id < source_count_ && sources_[id]) return static_cast<int>(id);
    return -1;
}

SourceRing::WriteSlot EventBuffer::acquireWriteSlot(SourceId id) noexcept {
    int idx = findSlotIndex(id);
    if (idx < 0) return {};
    return sources_[idx]->ring->acquireWriteSlot();
}

SourceRing::WriteSlot EventBuffer::getSlotByIndex(SourceId id,
                                                   size_t slot_idx) noexcept {
    int idx = findSlotIndex(id);
    if (idx < 0) return {};
    return sources_[idx]->ring->getSlotByIndex(slot_idx);
}

void EventBuffer::publish(SourceId id, uint64_t sequence) noexcept {
    int idx = findSlotIndex(id);
    if (idx < 0) return;
    sources_[idx]->ring->publish(sequence);
}

std::vector<std::byte*> EventBuffer::getSlotPointers(SourceId id) const {
    int idx = findSlotIndex(id);
    if (idx < 0) return {};
    return sources_[idx]->ring->getSlotPointers();
}

size_t EventBuffer::getSlotCapacity(SourceId id) const {
    int idx = findSlotIndex(id);
    return (idx < 0) ? 0 : sources_[idx]->ring->slotCapacity();
}

size_t EventBuffer::getSlotCount(SourceId id) const {
    int idx = findSlotIndex(id);
    return (idx < 0) ? 0 : sources_[idx]->ring->slotCount();
}

// ---------------------------------------------------------------------------
// Consumer API
// ---------------------------------------------------------------------------

EventBuffer::Subscription EventBuffer::subscribe() {
    return Subscription(this,
                        index_.writeSequence(),
                        sub_gen_.load(std::memory_order_acquire));
}

SourceRing::ReadHandle EventBuffer::acquireReadHandle(SourceId id,
                                                       uint64_t source_seq) const noexcept {
    int idx = findSlotIndex(id);
    if (idx < 0) return {};
    return sources_[idx]->ring->acquireReadHandle(source_seq);
}

const FormatDescriptor& EventBuffer::formatOf(SourceId id) const {
    int idx = findSlotIndex(id);
    if (idx < 0) throw std::out_of_range("EventBuffer: unknown SourceId");
    return sources_[idx]->desc.format;
}

std::vector<IndexEntry> EventBuffer::snapshot(int64_t t_start_us,
                                               int64_t t_end_us) {
    return index_.snapshot(t_start_us, t_end_us);
}

// ---------------------------------------------------------------------------
// Observability
// ---------------------------------------------------------------------------

const SourceStats& EventBuffer::statsFor(SourceId id) const {
    int idx = findSlotIndex(id);
    if (idx < 0) throw std::out_of_range("EventBuffer: unknown SourceId");
    return sources_[idx]->ring->stats();
}

std::vector<SourceId> EventBuffer::stalledSources() const {
    // TODO Phase 3: return sources whose stalled flag is set
    return {};
}

BufferState EventBuffer::state() const noexcept {
    return state_.load(std::memory_order_acquire);
}

bool EventBuffer::isCapturing() const noexcept {
    return capturing_.load(std::memory_order_acquire);
}

int64_t EventBuffer::nowMicros() noexcept {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

// ---------------------------------------------------------------------------
// SwingWindow
// ---------------------------------------------------------------------------

SwingWindow EventBuffer::captureSwingWindow(int64_t t_start_us,
                                             int64_t t_end_us) {
    assert(state_.load(std::memory_order_acquire) == BufferState::Paused
           && "captureSwingWindow requires Paused state");
    auto entries = index_.snapshot(t_start_us, t_end_us);
    return SwingWindow(this, std::move(entries), t_start_us, t_end_us);
}

SwingWindow EventBuffer::captureSwingWindow(
    std::chrono::milliseconds trailing_duration) {
    int64_t end_us   = nowMicros();
    int64_t start_us = end_us - trailing_duration.count() * 1000LL;
    return captureSwingWindow(start_us, end_us);
}

// ---------------------------------------------------------------------------
// Merger loop
// ---------------------------------------------------------------------------

void EventBuffer::mergerLoop() {
#if defined(PINPOINT_PLATFORM_WINDOWS)
    timeBeginPeriod(1);
#endif

    MergerState state;

    // Drain each source ring into merger heads.
    // Returns true if any new event was pulled.
    auto drainSources = [&]() -> bool {
        bool any = false;
        for (size_t i = 0; i < source_count_; ++i) {
            auto& slot = *sources_[i];
            if (state.heads[i].valid) continue; // head already filled

            uint64_t write_seq = slot.ring->writeSequence();
            if (write_seq == 0) continue; // nothing written yet

            uint64_t& next = slot.next_seq;
            while (next < write_seq) {
                int64_t ts;
                if (slot.ring->peekTimestamp(next, ts)) {
                    state.setHead(static_cast<int>(i),
                                  PendingEvent{slot.desc.id, next, ts, true});
                    next++;
                    any = true;
                    break;
                } else {
                    // Overrun: skip to current write head.
                    next = write_seq;
                    break;
                }
            }
        }
        return any;
    };

    // Safe-to-emit threshold: min(latest producer ts across all sources) minus
    // the reorder window. Uses ring stats to see past the current head.
    auto computeSafe = [&]() -> int64_t {
        int64_t min_ts = INT64_MAX;
        bool    any    = false;
        for (size_t i = 0; i < source_count_; ++i) {
            int64_t ts = sources_[i]->ring->stats()
                             .last_write_timestamp_us.load(std::memory_order_relaxed);
            if (ts == 0) continue; // source never written
            any    = true;
            min_ts = std::min(min_ts, ts);
        }
        return any ? (min_ts - static_cast<int64_t>(config_.reorder_window_us))
                   : INT64_MIN;
    };

    // Emit all heads with timestamp <= safe_until into the timeline index.
    // Returns true if anything was emitted.
    auto emitReady = [&](int64_t safe_until) -> bool {
        bool emitted = false;
        while (true) {
            int idx = state.findMinValidIndex();
            if (idx < 0) break;
            PendingEvent& ev = state.heads[idx];
            if (ev.timestamp_us > safe_until) break;

            state.enforceMonotonicity(idx, ev, *sources_[idx]->ring);

            IndexEntry entry{};
            entry.timestamp_us    = ev.timestamp_us;
            entry.source_id       = ev.source_id;
            entry.source_sequence = ev.source_seq;
            uint64_t seq = index_.append(entry);
            index_wait_.store(seq);
            index_wait_.notifyAll();

            state.clearHead(idx);
            emitted = true;
            drainSources(); // refill after consuming a head
        }
        return emitted;
    };

    // Drain and emit everything — used during pause().
    auto drainAll = [&] {
        drainSources();
        while (true) {
            int idx = state.findMinValidIndex();
            if (idx < 0) break;
            PendingEvent& ev = state.heads[idx];
            state.enforceMonotonicity(idx, ev, *sources_[idx]->ring);

            IndexEntry entry{};
            entry.timestamp_us    = ev.timestamp_us;
            entry.source_id       = ev.source_id;
            entry.source_sequence = ev.source_seq;
            uint64_t seq = index_.append(entry);
            index_wait_.store(seq);
            index_wait_.notifyAll();

            state.clearHead(idx);
            drainSources();
        }
    };

    while (running_.load(std::memory_order_acquire)) {

        // --- Draining path (pause() signalled) ---
        if (draining_.load(std::memory_order_acquire)) {
            drainAll();
            drained_.store(true, std::memory_order_release);

            // Block until resumed or stopped.
            while (draining_.load(std::memory_order_acquire)
                   && running_.load(std::memory_order_acquire)) {
                index_wait_.waitFor(index_wait_.load(),
                                    std::chrono::microseconds(1000));
            }

            // Fresh capture session — reset merger state.
            state = MergerState{};
            continue;
        }

        // --- Normal path ---
        bool any_progress  = drainSources();
        int64_t safe_until = computeSafe();
        any_progress      |= emitReady(safe_until);

        // TODO Phase 3: watchdogTick()

        if (any_progress) {
            for (uint32_t i = 0; i < config_.merger_spin_iterations; ++i)
                PINPOINT_CPU_PAUSE();
        } else {
            index_wait_.waitFor(index_wait_.load(),
                                std::chrono::microseconds(config_.merger_cold_timeout_us));
        }
    }

#if defined(PINPOINT_PLATFORM_WINDOWS)
    timeEndPeriod(1);
#endif
}

void EventBuffer::watchdogTick() {
    // TODO Phase 3: implement liveness watchdog
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void EventBuffer::start() {
    assert(state_.load(std::memory_order_acquire) == BufferState::Idle
           && "start() requires Idle state");
    if (state_.load(std::memory_order_acquire) != BufferState::Idle) return;

    draining_.store(false, std::memory_order_relaxed);
    drained_.store(false, std::memory_order_relaxed);
    capturing_.store(true, std::memory_order_release);
    running_.store(true, std::memory_order_release);
    state_.store(BufferState::Capturing, std::memory_order_release);

    merger_thread_ = std::thread([this] { mergerLoop(); });
}

void EventBuffer::pause() {
    assert(state_.load(std::memory_order_acquire) == BufferState::Capturing
           && "pause() requires Capturing state");
    if (state_.load(std::memory_order_acquire) != BufferState::Capturing) return;

    capturing_.store(false, std::memory_order_release);

    draining_.store(true, std::memory_order_release);
    index_wait_.notifyAll(); // wake merger if sleeping

    auto deadline = std::chrono::steady_clock::now()
                    + std::chrono::milliseconds(config_.pause_drain_timeout_ms);
    while (!drained_.load(std::memory_order_acquire)) {
        if (std::chrono::steady_clock::now() >= deadline) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    state_.store(BufferState::Paused, std::memory_order_release);
}

void EventBuffer::resume() {
    assert(state_.load(std::memory_order_acquire) == BufferState::Paused
           && "resume() requires Paused state");
    if (state_.load(std::memory_order_acquire) != BufferState::Paused) return;

    if (config_.resume_clear_rings) {
        for (size_t i = 0; i < source_count_; ++i) {
            sources_[i]->ring->reset();
            sources_[i]->next_seq = 0;
        }
        index_.reset();
    }

    // Invalidate all live Subscription instances.
    sub_gen_.fetch_add(1, std::memory_order_release);

    draining_.store(false, std::memory_order_release);
    drained_.store(false, std::memory_order_relaxed);
    capturing_.store(true, std::memory_order_release);

    // Wake the merger so it exits the draining-wait block.
    index_wait_.store(0);
    index_wait_.notifyAll();

    state_.store(BufferState::Capturing, std::memory_order_release);
}

void EventBuffer::stop() {
    if (state_.load(std::memory_order_acquire) == BufferState::Capturing)
        pause();

    running_.store(false, std::memory_order_release);
    index_wait_.notifyAll();

    if (merger_thread_.joinable())
        merger_thread_.join();

    state_.store(BufferState::Idle, std::memory_order_release);
}

// ---------------------------------------------------------------------------
// Subscription
// ---------------------------------------------------------------------------

EventBuffer::Subscription::Subscription(EventBuffer* buf,
                                         uint64_t start_seq,
                                         uint64_t gen)
    : buffer_(buf), next_seq_(start_seq), gen_(gen)
{}

void EventBuffer::Subscription::resetToLatest() {
    next_seq_ = buffer_->index_.writeSequence();
}

bool EventBuffer::Subscription::tryNext(IndexEntry& out) {
    // Generation check: timeline was reset by resume().
    uint64_t cur_gen = buffer_->sub_gen_.load(std::memory_order_acquire);
    if (cur_gen != gen_) {
        gen_      = cur_gen;
        next_seq_ = 0;
        return false;
    }

    if (buffer_->index_.tryRead(next_seq_, out)) {
        next_seq_++;
        return true;
    }

    // Overrun: write_seq_ has lapped next_seq_ by more than index capacity.
    uint64_t ws = buffer_->index_.writeSequence();
    if (ws > next_seq_ && (ws - next_seq_) > buffer_->index_.capacity()) {
        overruns_ += (ws - next_seq_);
        next_seq_  = ws;
    }

    return false;
}

bool EventBuffer::Subscription::waitNext(IndexEntry& out,
                                          std::chrono::microseconds timeout) {
    if (tryNext(out)) return true;
    uint64_t expected = buffer_->index_wait_.load();
    buffer_->index_wait_.waitFor(expected, timeout);
    return tryNext(out);
}

} // namespace pinpoint
