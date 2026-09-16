/*
 * Copyright (C) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

// device_clock_mapper_test — the camera clock → host clock mapping on synthetic cameras whose truth is
// known: a crystal off by tens of ppm, one-sided transfer delay with jitter and bursts, host stalls,
// counter restarts. The bar each scenario asserts is the one the impact camera needs: intervals good to
// tens of µs where the arrival stamps were good to ±400 µs.

#include "device_clock_mapper.h"

#include <gtest/gtest.h>
#include <cmath>
#include <limits>
#include <random>
#include <vector>

using namespace pinpoint;

namespace {

// A synthetic camera. Device µs advance at the frame period; the host sees the exposure at
// hostOffset + deviceUs × (1 + skewPpm·1e-6) and receives it `latency` later.
struct SynthCamera {
    double  fps          = 592.0;
    double  skewPpm      = 50.0;
    int64_t hostOffsetUs = 7'000'000'000LL;   // host µs when the device counter read 0
    int64_t deviceStartUs = 123'456'789LL;
    double  minLatencyUs = 800.0;
    double  jitterMeanUs = 400.0;             // exponential, one-sided
    std::mt19937_64 rng{ 42 };

    int64_t deviceUs(int64_t frame) const { return deviceStartUs + int64_t(std::llround(frame * 1e6 / fps)); }
    double  truthHostUs(int64_t devUs) const { return double(hostOffsetUs) + double(devUs) * (1.0 + skewPpm * 1e-6); }
    double  latency() { std::exponential_distribution<double> e(1.0 / jitterMeanUs); return minLatencyUs + e(rng); }
};

double sd(const std::vector<double> &v)
{
    double m = 0; for (double x : v) m += x; m /= double(v.size());
    double s = 0; for (double x : v) s += (x - m) * (x - m);
    return std::sqrt(s / double(v.size() - 1));
}

} // namespace

// Envelope only: jitter and drift gone, the minimum latency (constant) remains and is reported unknown.
TEST(DeviceClockMapper, EnvelopeRemovesJitterAndDriftButKeepsTheMinimumLatency)
{
    SynthCamera cam;
    DeviceClockMapper m;
    std::vector<double> err, arrivalIv, mappedIv;
    int64_t prevOut = 0; double prevArr = 0;
    const int64_t frames = int64_t(30 * cam.fps);
    for (int64_t f = 0; f < frames; ++f) {
        const int64_t dev = cam.deviceUs(f);
        const double truth = cam.truthHostUs(dev);
        const int64_t arr = int64_t(std::llround(truth + cam.latency()));
        const int64_t out = m.map(dev * 1000, arr, f);
        // Intervals after the warm-up, where the line is allowed to jump.
        if (f > 0 && dev - cam.deviceStartUs > 3'000'000) {
            mappedIv.push_back(double(out - prevOut)); arrivalIv.push_back(double(arr) - prevArr);
        }
        prevOut = out; prevArr = double(arr);
        if (dev - cam.deviceStartUs > 5'000'000) err.push_back(double(out) - truth);
    }
    const DeviceClockStats s = m.stats();
    EXPECT_EQ(s.method, ClockMapMethod::Envelope);
    EXPECT_EQ(s.minLatencyUs, -1);
    EXPECT_NEAR(s.skewPpm, cam.skewPpm, 2.0);
    EXPECT_FALSE(s.degenerate);
    // Error is the minimum latency, constant to tens of µs.
    double lo = 1e18, hi = -1e18;
    for (double e : err) { lo = std::min(lo, e); hi = std::max(hi, e); }
    EXPECT_NEAR(lo, cam.minLatencyUs, 60.0);
    EXPECT_LT(hi - lo, 60.0);
    EXPECT_GT(sd(arrivalIv), 300.0);   // what the old stamps looked like
    EXPECT_LT(sd(mappedIv), 5.0);      // what the frames get now
}

// Latch: the constant is calibrated out, so the mapped instant is the exposure instant itself.
TEST(DeviceClockMapper, LatchCalibratesTheConstantOut)
{
    SynthCamera cam;
    DeviceClockMapper m;
    // Bracket: host midpoint within ±60 µs of the truth at that device instant.
    const int64_t latchDev = cam.deviceStartUs - 50'000;
    m.seedLatch(int64_t(std::llround(cam.truthHostUs(latchDev) + 40.0)), latchDev * 1000, 120);
    std::vector<double> err;
    for (int64_t f = 0; f < int64_t(30 * cam.fps); ++f) {
        const int64_t dev = cam.deviceUs(f);
        const double truth = cam.truthHostUs(dev);
        const int64_t out = m.map(dev * 1000, int64_t(std::llround(truth + cam.latency())), f);
        if (dev - cam.deviceStartUs > 10'000'000) err.push_back(double(out) - truth);
    }
    const DeviceClockStats s = m.stats();
    EXPECT_EQ(s.method, ClockMapMethod::Latch);
    EXPECT_EQ(s.latchRttUs, 120);
    EXPECT_NEAR(double(s.minLatencyUs), cam.minLatencyUs, 100.0);
    for (double e : err) EXPECT_LT(std::abs(e), 150.0);
}

// A 300 ms host stall delivers a burst of late frames; the envelope must not bend toward them.
TEST(DeviceClockMapper, AHostStallDoesNotBendTheLine)
{
    SynthCamera cam;
    DeviceClockMapper m;
    double worst = 0;
    for (int64_t f = 0; f < int64_t(20 * cam.fps); ++f) {
        const int64_t dev = cam.deviceUs(f);
        const double truth = cam.truthHostUs(dev);
        double lat = cam.latency();
        const double t = double(dev - cam.deviceStartUs) / 1e6;
        if (t > 10.0 && t < 10.3) lat += (10.3 - t) * 1e6;   // held, then flushed at once
        const int64_t out = m.map(dev * 1000, int64_t(std::llround(truth + lat)), f);
        if (t > 5.0) worst = std::max(worst, std::abs(double(out) - truth - cam.minLatencyUs));
    }
    EXPECT_LT(worst, 80.0);
}

// Every output strictly increases and no later than arrival — across window rolls, a crystal whose
// rate wanders, and a latch that disagrees with the envelope by several ms (the correction slews).
TEST(DeviceClockMapper, OutputsAreMonotonicAndSlewAcrossWindows)
{
    SynthCamera cam;
    cam.fps = 150.0;
    DeviceClockMapper::Config cfg;
    cfg.windowUs = 20'000'000;
    DeviceClockMapper m(cfg);
    m.seedLatch(int64_t(cam.truthHostUs(cam.deviceStartUs)) + 4000, cam.deviceStartUs * 1000, 200);  // 4 ms wrong
    int64_t prev = std::numeric_limits<int64_t>::min();
    double prevTruth = 0, worstIvErr = 0;
    for (int64_t f = 0; f < int64_t(120 * cam.fps); ++f) {
        const int64_t dev = cam.deviceUs(f);
        const double t = double(dev - cam.deviceStartUs) / 1e6;
        cam.skewPpm = 50.0 + 0.05 * t;                       // 6 ppm over two minutes
        const double truth = cam.truthHostUs(dev);
        const int64_t arr = int64_t(std::llround(truth + cam.latency()));
        const int64_t out = m.map(dev * 1000, arr, f);
        ASSERT_GT(out, prev);
        ASSERT_LE(out, arr);
        if (f > 0 && t > 3.0) worstIvErr = std::max(worstIvErr, std::abs(double(out - prev) - (truth - prevTruth)));
        prev = out; prevTruth = truth;
    }
    EXPECT_LT(worstIvErr, 60.0);   // no step anywhere, including the 4 ms latch disagreement
}

// A camera restart resets its counter; the mapping continues forward and recovers on the pooled rate.
TEST(DeviceClockMapper, ACounterRestartStartsANewStream)
{
    SynthCamera cam;
    DeviceClockMapper m;
    int64_t prev = std::numeric_limits<int64_t>::min();
    double lastErr = 0;
    for (int64_t f = 0; f < int64_t(10 * cam.fps); ++f) {
        const int64_t dev = cam.deviceUs(f);
        const int64_t out = m.map(dev * 1000, int64_t(std::llround(cam.truthHostUs(dev) + cam.latency())), f);
        ASSERT_GT(out, prev); prev = out;
    }
    // Restart: the counter begins again at 5 s while host time continues; model it by moving the offset.
    const int64_t hostAtRestart = int64_t(cam.truthHostUs(cam.deviceUs(int64_t(10 * cam.fps)))) + 500'000;
    cam.deviceStartUs = 5'000'000;
    cam.hostOffsetUs = hostAtRestart - int64_t(double(cam.deviceStartUs) * (1.0 + cam.skewPpm * 1e-6));
    for (int64_t f = 0; f < int64_t(10 * cam.fps); ++f) {
        const int64_t dev = cam.deviceUs(f);
        const double truth = cam.truthHostUs(dev);
        const int64_t out = m.map(dev * 1000, int64_t(std::llround(truth + cam.latency())), f);
        ASSERT_GT(out, prev); prev = out;
        lastErr = double(out) - truth;
    }
    const DeviceClockStats s = m.stats();
    EXPECT_EQ(s.streams, 2);
    EXPECT_NEAR(lastErr, cam.minLatencyUs, 100.0);
}

TEST(DeviceClockMapper, FrameIdGapsAreCounted)
{
    SynthCamera cam;
    DeviceClockMapper m;
    for (int64_t f = 0; f < 1000; ++f) {
        if (f == 300 || f == 301 || f == 700) continue;   // three frames never arrived
        const int64_t dev = cam.deviceUs(f);
        m.map(dev * 1000, int64_t(std::llround(cam.truthHostUs(dev) + cam.latency())), f);
    }
    EXPECT_EQ(m.stats().frameIdGaps, 3);
}

// A rate no crystal could have (1 %) is refused and flagged, not published.
TEST(DeviceClockMapper, AnImplausibleRateIsRefused)
{
    SynthCamera cam;
    cam.skewPpm = 10'000.0;
    DeviceClockMapper m;
    for (int64_t f = 0; f < int64_t(10 * cam.fps); ++f) {
        const int64_t dev = cam.deviceUs(f);
        m.map(dev * 1000, int64_t(std::llround(cam.truthHostUs(dev) + cam.latency())), f);
    }
    const DeviceClockStats s = m.stats();
    EXPECT_TRUE(s.implausible);
    EXPECT_NEAR(s.skewPpm, 0.0, 1.0);
}

// Already-on-host-clock instants (AVFoundation PTS): passed through, kept monotonic, lag recorded.
TEST(DeviceClockMapper, DirectInstantsKeepTheGuaranteesAndTheLag)
{
    DeviceClockMapper m;
    int64_t prev = std::numeric_limits<int64_t>::min();
    for (int64_t f = 0; f < 600; ++f) {
        const int64_t cap = 1'000'000 + f * 16'667;
        const int64_t arr = cap + 3'000 + (f % 7) * 100;
        const int64_t out = m.mapDirect(f == 300 ? cap - 50'000 : cap, arr,       // one PTS goes backwards
                                        ClockMapMethod::DevicePts, f);
        ASSERT_GT(out, prev); prev = out;
    }
    const DeviceClockStats s = m.stats();
    EXPECT_EQ(s.method, ClockMapMethod::DevicePts);
    EXPECT_EQ(s.clampedMonotonic, 1);
    EXPECT_NEAR(double(s.lagP50Us), 3300.0, 400.0);
}
