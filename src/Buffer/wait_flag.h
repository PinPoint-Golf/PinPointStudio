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

    void     store(uint64_t v) noexcept;
    uint64_t load()            const noexcept;
    void     notifyAll()       noexcept;

private:
    alignas(64) std::atomic<uint64_t> value_{0};

#if defined(PINPOINT_PLATFORM_APPLE)
    mutable std::mutex              mtx_;
    mutable std::condition_variable cv_;
#endif
};

} // namespace pinpoint
