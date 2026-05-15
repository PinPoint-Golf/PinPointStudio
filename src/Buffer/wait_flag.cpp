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

#include "wait_flag.h"
#include <thread>

#if defined(PINPOINT_PLATFORM_APPLE)
  #include <sched.h>
#endif

#if defined(PINPOINT_PLATFORM_WINDOWS)
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #include <windows.h>
  #include <synchapi.h>
  #pragma comment(lib, "Synchronization.lib")
#endif

namespace pinpoint {

// ---- Apple: spin + sched_yield path ----------------------------------------
// cv_.wait_for() on macOS maps to pthread_cond_timedwait which is subject to
// Darwin timer coalescing: a 500µs request routinely sleeps 5–10ms. Instead,
// spin briefly then yield cooperatively — sched_yield() costs 1–20µs and does
// not engage the Darwin timer subsystem.

#if defined(PINPOINT_PLATFORM_APPLE)

uint64_t WaitFlag::waitFor(uint64_t expected,
                           std::chrono::microseconds timeout) noexcept {
    uint64_t current = value_.load(std::memory_order_acquire);
    if (current != expected) return current;

    auto deadline = std::chrono::steady_clock::now() + timeout;

    // Phase 1: tight spin — catches events that arrive within ~2µs
    for (int i = 0; i < 32; ++i) {
        PINPOINT_CPU_PAUSE();
        current = value_.load(std::memory_order_acquire);
        if (current != expected) return current;
    }

    // Phase 2: yield loop — cooperative, avoids Darwin timer coalescing
    while (std::chrono::steady_clock::now() < deadline) {
        sched_yield();
        current = value_.load(std::memory_order_acquire);
        if (current != expected) return current;
    }

    return value_.load(std::memory_order_acquire);
}

void WaitFlag::store(uint64_t v) noexcept {
    value_.store(v, std::memory_order_release);
}

void WaitFlag::notifyAll() noexcept {
    // Spin loop polls value_ directly — no condition variable to notify.
}

// ---- Windows: WaitOnAddress / WakeByAddressAll -----------------------------

#elif defined(PINPOINT_PLATFORM_WINDOWS)

uint64_t WaitFlag::waitFor(uint64_t expected,
                           std::chrono::microseconds timeout) noexcept {
    uint64_t current = value_.load(std::memory_order_acquire);
    if (current != expected) return current;

    DWORD ms = static_cast<DWORD>(
        std::max<int64_t>(1, timeout.count() / 1000));
    WaitOnAddress(
        reinterpret_cast<volatile void*>(&value_),
        &expected, sizeof(expected), ms);
    return value_.load(std::memory_order_acquire);
}

void WaitFlag::store(uint64_t v) noexcept {
    value_.store(v, std::memory_order_release);
}

void WaitFlag::notifyAll() noexcept {
    WakeByAddressAll(reinterpret_cast<void*>(&value_));
}

// ---- Linux/Android/POSIX: spin + sleep loop --------------------------------
// std::atomic::wait has no timeout in C++20 (P2643 targets C++26).
// We use a brief spin followed by short sleeps with a deadline check.

#else

uint64_t WaitFlag::waitFor(uint64_t expected,
                           std::chrono::microseconds timeout) noexcept {
    uint64_t current = value_.load(std::memory_order_acquire);
    if (current != expected) return current;

    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true) {
        for (int i = 0; i < 16; ++i) {
            current = value_.load(std::memory_order_acquire);
            if (current != expected) return current;
            PINPOINT_CPU_PAUSE();
        }
        if (std::chrono::steady_clock::now() >= deadline) break;
        std::this_thread::sleep_for(std::chrono::microseconds(50));
        current = value_.load(std::memory_order_acquire);
        if (current != expected) return current;
    }
    return value_.load(std::memory_order_acquire);
}

void WaitFlag::store(uint64_t v) noexcept {
    value_.store(v, std::memory_order_release);
}

void WaitFlag::notifyAll() noexcept {
    value_.notify_all(); // wakes threads in std::atomic::wait if used elsewhere
}

#endif

// ---- load: same on all platforms -------------------------------------------

uint64_t WaitFlag::load() const noexcept {
    return value_.load(std::memory_order_acquire);
}

} // namespace pinpoint
