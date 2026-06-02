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

#include "platform.h"
#include <atomic>
#include <chrono>
#include <cstdint>

#if defined(PINPOINT_PLATFORM_APPLE)
  #include <condition_variable>
  #include <mutex>
#endif

namespace pinpoint {

// Platform-abstracted wait-with-timeout on an atomic uint64_t.
// Linux/Android: spin + sleep loop (std::atomic::wait has no timeout in C++20).
// Windows:       WaitOnAddress / WakeByAddressAll.
// Apple:         std::condition_variable (accepted degradation — no futex).
class WaitFlag {
public:
    // Block up to 'timeout' for value_ to differ from 'expected'.
    // Returns the current value. May return 'expected' on spurious wakeup or timeout.
    uint64_t waitFor(uint64_t expected,
                     std::chrono::microseconds timeout) noexcept;

    // Block indefinitely until value_ differs from 'expected' and a notifyAll()
    // arrives. No timeout, no polling. COLD PATH ONLY (the pause gate) — never
    // call on the capture/merge hot path. Unlike waitFor() this parks the thread
    // (futex / WaitOnAddress / condition_variable) instead of spinning.
    void     wait(uint64_t expected) noexcept;

    void     store(uint64_t v) noexcept;
    uint64_t load()            const noexcept;
    void     notifyAll()       noexcept;

private:
    alignas(64) std::atomic<uint64_t> value_{0};

#if defined(PINPOINT_PLATFORM_APPLE)
    // The untimed wait() cannot spin forever, so Apple needs a real condition
    // variable. waiters_ keeps notifyAll() ~free on the hot path: with nobody
    // parked in wait() it is a single atomic load + branch (no mutex), so the
    // spin-only behaviour of source_published_ / index_wait_ is preserved.
    std::atomic<uint32_t>           waiters_{0};
    mutable std::mutex              mtx_;
    mutable std::condition_variable cv_;
#endif
};

} // namespace pinpoint
