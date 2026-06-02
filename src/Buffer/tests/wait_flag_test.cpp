/*
 * Copyright (C) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include "wait_flag.h"
#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <future>
#include <thread>
#include <vector>

using namespace pinpoint;
using namespace std::chrono_literals;

// ---------------------------------------------------------------------------
// Basic store / load
// ---------------------------------------------------------------------------

TEST(WaitFlag, StoreLoad) {
    WaitFlag f;
    EXPECT_EQ(f.load(), 0u);
    f.store(42);
    EXPECT_EQ(f.load(), 42u);
}

// ---------------------------------------------------------------------------
// waitFor returns immediately when value already differs
// ---------------------------------------------------------------------------

TEST(WaitFlag, ReturnsImmediatelyWhenDiffers) {
    WaitFlag f;
    f.store(5);

    auto t0 = std::chrono::steady_clock::now();
    uint64_t result = f.waitFor(0, 100ms);
    auto elapsed = std::chrono::steady_clock::now() - t0;

    EXPECT_EQ(result, 5u);
    EXPECT_LT(elapsed, 30ms); // generous: ±20ms for macOS scheduler jitter
}

// ---------------------------------------------------------------------------
// waitFor times out correctly
// ---------------------------------------------------------------------------

TEST(WaitFlag, TimesOut) {
    WaitFlag f;
    f.store(0);

    auto t0 = std::chrono::steady_clock::now();
    uint64_t result = f.waitFor(0, 80ms);
    auto elapsed = std::chrono::steady_clock::now() - t0;

    EXPECT_EQ(result, 0u);
    EXPECT_GE(elapsed, 60ms); // 80ms nominal - 20ms tolerance
    EXPECT_LT(elapsed, 200ms);
}

// ---------------------------------------------------------------------------
// notifyAll wakes a waiting thread
// ---------------------------------------------------------------------------

TEST(WaitFlag, NotifyAllWakes) {
    WaitFlag f;
    f.store(0);

    std::atomic<uint64_t> seen{0};

    std::thread waiter([&] {
        uint64_t v = f.waitFor(0, 5s);
        seen.store(v, std::memory_order_relaxed);
    });

    std::this_thread::sleep_for(20ms);
    f.store(1);
    f.notifyAll();

    waiter.join();

    EXPECT_EQ(seen.load(), 1u);
}

// ---------------------------------------------------------------------------
// Multiple waiters all wake
// ---------------------------------------------------------------------------

TEST(WaitFlag, MultipleWaiters) {
    WaitFlag f;
    f.store(0);

    constexpr int THREADS = 4;
    std::atomic<int> woke{0};
    std::vector<std::thread> threads;

    for (int i = 0; i < THREADS; ++i) {
        threads.emplace_back([&] {
            f.waitFor(0, 2s);
            woke.fetch_add(1, std::memory_order_relaxed);
        });
    }

    std::this_thread::sleep_for(30ms);
    f.store(99);
    f.notifyAll();

    for (auto& t : threads) t.join();

    EXPECT_EQ(woke.load(), THREADS);
}

// ===========================================================================
// Untimed wait() — the pause-gate primitive.
//
// wait() is UNTIMED: a broken impl HANGS rather than fails. Every test below
// runs the waiter on std::async and asserts fut.wait_for(deadline) == ready, so
// a lost wakeup surfaces as a test failure (and timeout) instead of wedging
// ctest forever.
// ===========================================================================

// wait() returns immediately when value_ already differs from expected.
TEST(WaitFlag, WaitReturnsImmediatelyWhenDiffers) {
    WaitFlag f;
    f.store(5);

    auto fut = std::async(std::launch::async, [&] { f.wait(0); });
    EXPECT_EQ(fut.wait_for(1s), std::future_status::ready);
}

// wait() parks, then wakes on store()+notifyAll().
TEST(WaitFlag, WaitWakesOnNotify) {
    WaitFlag f;
    f.store(0);

    auto fut = std::async(std::launch::async, [&] { f.wait(0); });

    std::this_thread::sleep_for(20ms); // let the waiter park
    f.store(1);
    f.notifyAll();

    EXPECT_EQ(fut.wait_for(2s), std::future_status::ready);
}

// THE key test for the Apple seq_cst/waiters_ handshake: store()+notifyAll()
// racing the waiter as it parks must never be lost. On Linux this exercises the
// futex path (TSAN-clean by construction); built under macOS TSAN it stresses
// the condition_variable handshake where a botched ordering would hang.
TEST(WaitFlag, WaitNoLostWakeupUnderRace) {
    constexpr int kIters = 20000;
    for (int i = 0; i < kIters; ++i) {
        WaitFlag f; // fresh: value_ starts at 0
        std::atomic<bool> armed{false};

        auto fut = std::async(std::launch::async, [&] {
            armed.store(true, std::memory_order_release);
            f.wait(0); // race: may park just as the notify below lands
        });

        while (!armed.load(std::memory_order_acquire)) { /* spin to the park */ }
        f.store(1);
        f.notifyAll();

        ASSERT_EQ(fut.wait_for(2s), std::future_status::ready)
            << "lost wakeup at iteration " << i;
    }
}
