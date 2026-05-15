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

#if defined(PINPOINT_PLATFORM_WINDOWS)
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
  #include <synchapi.h>
#endif

namespace pinpoint {

// ---- Apple: condition_variable path ----------------------------------------

#if defined(PINPOINT_PLATFORM_APPLE)

uint64_t WaitFlag::waitFor(uint64_t expected,
                           std::chrono::microseconds timeout) noexcept {
    std::unique_lock lock(mtx_);
    cv_.wait_for(lock, timeout, [&] {
        return value_.load(std::memory_order_acquire) != expected;
    });
    return value_.load(std::memory_order_acquire);
}

void WaitFlag::store(uint64_t v) noexcept {
    { std::lock_guard lock(mtx_); value_.store(v, std::memory_order_release); }
    cv_.notify_all();
}

void WaitFlag::notifyAll() noexcept {
    cv_.notify_all();
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
