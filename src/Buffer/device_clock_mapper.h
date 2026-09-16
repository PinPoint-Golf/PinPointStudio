/*
 * Copyright (C) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */
#pragma once

// device_clock_mapper.h — a camera's own frame clock, mapped onto EventBuffer::nowMicros().
//
// ⚠ WHY THIS EXISTS (2026-09-15). Every local camera frame used to be stamped with the host clock
// when CameraInstance's handler ran — after the USB transfer, the driver, a copy and a queued Qt hop.
// Each frame was late by a variable few milliseconds: the 592 fps impact clip's intervals ran
// 1.2–2.2 ms against a true 1.69 ms while the ball moved an even 38–39 px per frame. The camera knows
// exactly when it exposed each frame; this class carries that instant onto the host clock
// (event_buffer_design.md §9).
//
// ── The estimator ──────────────────────────────────────────────────────────────────────────────
// A port of libwrist's device→host fit (libwrist/src/wr_fit.h, wr_clock.c), with the device
// TIMESTAMP in place of a sample index:
//
//  • FIT THE LOWER ENVELOPE, NOT LEAST SQUARES. Arrival delay is one-sided — a frame can reach the
//    host late, never early — so least squares biases the line by the MEAN delay. The highest line
//    under every (device µs, arrival µs) observation is the maximum-likelihood answer; its optimum
//    lies on an edge of the lower convex hull, so the fit is an incremental hull plus one edge scan.
//  • FIT THE RATE TOO. A camera crystal is tens of ppm off the host's; pinning the rate at 1.0 makes
//    arrival − device drift monotonically, the envelope minimum collapses onto one end of the run,
//    and the fit reports small residuals while being wrong. That is flagged as DEGENERATE.
//  • ROLL THE WINDOW, POOL THE RATE. A crystal's rate wanders with temperature over an hour, so the
//    hull is restarted every `windowUs` of device time; the new window starts on the pooled rate and
//    fits its own once it has `minSpanUs` of baseline (below that a few ms of jitter makes a wild
//    slope).
//
// ── Two ways to know the offset ────────────────────────────────────────────────────────────────
//  • ENVELOPE ONLY: the mapped instant is the envelope line, i.e. the device instant plus the
//    camera's MINIMUM transfer latency. Jitter and drift are gone; a constant few ms remain, and the
//    stats say the constant is unknown.
//  • LATCH: the backend brackets a camera-side TimestampLatch between two host reads
//    (seedLatch()). Once the envelope has a baseline, the latch fixes that constant: the envelope's
//    height at the latch instant minus the latch's host time IS the minimum latency, which is then
//    subtracted from the envelope for every frame. The envelope keeps tracking drift; the latch
//    only calibrates the one number the envelope cannot see.
//
// ── What a consumer can rely on ────────────────────────────────────────────────────────────────
//  • map() returns strictly increasing µs, never later than the frame's own arrival.
//  • Corrections SLEW at ≤ maxSlewUsPerS once warm — a stamp never steps backwards, because
//    EventBuffer clamps a backwards stamp and counts a monotonicity violation.
//  • A device counter that goes backwards (a camera restart) starts a new stream on the pooled rate
//    and the carried latency.
//
// Thread-safety: map() is called on one capture thread; stats() may be called from any thread.

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace pinpoint {

enum class ClockMapMethod : uint8_t {
    None,       // nothing observed yet
    Envelope,   // lower-envelope line; the minimum transfer latency is NOT removed
    Latch,      // envelope for drift, a TimestampLatch bracket for the constant
    DevicePts,  // the backend supplied an instant already on the host clock (AVFoundation PTS)
};

const char *clockMapMethodName(ClockMapMethod m);

struct DeviceClockStats {
    ClockMapMethod method = ClockMapMethod::None;
    bool    haveFit       = false;
    bool    shortBaseline = true;    // the current window's rate is the seeded one, not its own
    bool    degenerate    = false;   // the fit rests on two nearly adjacent frames
    bool    implausible   = false;   // the fitted rate was outside ±kMaxPlausiblePpm and was refused
    double  skewPpm       = 0.0;     // (host µs per device µs − 1) × 1e6
    double  fitSpanS      = 0.0;     // baseline of the current window
    int64_t latchRttUs    = -1;      // the bracket's round trip; -1 = no latch
    int64_t minLatencyUs  = -1;      // envelope − latch: the camera's fastest delivery; -1 = unknown
    int64_t residualP50Us = 0;       // arrival − envelope, recent frames
    int64_t residualP99Us = 0;
    int64_t lagP50Us      = 0;       // arrival − mapped instant, recent frames
    int64_t lagP99Us      = 0;
    int64_t lagMaxUs      = 0;
    int64_t frames        = 0;       // frames mapped since reset()
    int64_t frameIdGaps   = 0;       // frames the device's own counter says never arrived
    int64_t streams       = 0;       // device-counter restarts + 1
    int64_t clampedToArrival = 0;    // mapped instants that came out after arrival and were clamped
    int64_t clampedMonotonic = 0;    // mapped instants held back to stay strictly increasing
};

class DeviceClockMapper {
public:
    struct Config {
        int64_t minSpanUs      = 2'000'000;    // below this the window's rate is seeded, not fitted
        int64_t windowUs       = 120'000'000;  // hull restarts after this much device time
        double  maxSlewUsPerS  = 50.0;         // once warm, corrections move at most this fast
        int64_t warmupUs       = 2'000'000;    // before this, the line may jump (still monotonic)
        int64_t resetJumpUs    = 1'000'000;    // device time going back by more than this = restart
        size_t  hullMax        = 256;
        size_t  recentMax      = 4096;         // frames kept for the residual / lag percentiles
    };
    static constexpr double  kMaxPlausiblePpm = 500.0;
    // No camera delivers its FASTEST frame this late; a latch implying so is a broken bracket.
    static constexpr int64_t kMaxPlausibleLatencyUs = 100'000;

    DeviceClockMapper() = default;
    explicit DeviceClockMapper(const Config &cfg) : m_cfg(cfg) {}

    // Forget everything — a new connection.
    void reset();

    // The camera said its clock read `deviceNs` at host instant `hostUs` (the bracket's midpoint),
    // pinned to ±rttUs/2. Call after the first frames or before; the constant is calibrated once the
    // envelope has a baseline.
    void seedLatch(int64_t hostUs, int64_t deviceNs, int64_t rttUs);

    // One frame: its device timestamp, its arrival on the host clock (taken as the first statement
    // after the frame was in hand), and the device's frame counter (-1 if none). Returns the frame's
    // capture instant on the host clock.
    int64_t map(int64_t deviceNs, int64_t arrivalUs, int64_t frameId = -1);

    // A backend whose timestamp is already on the host clock (AVFoundation PTS): no fit, but the same
    // monotonic guarantee and the same lag statistics.
    int64_t mapDirect(int64_t captureUs, int64_t arrivalUs, int64_t frameId = -1);

    DeviceClockStats stats() const;

private:
    struct Pt { int64_t x; int64_t h; };   // device µs, arrival µs — relative to the stream origin

    void beginStream();
    void observe(const Pt &p);
    void hullMakeRoom();
    void refit();
    double envelopeAt(int64_t x) const;    // envelope height at relative device µs x (relative host µs)
    int64_t publish(int64_t absTargetUs, double slope, int64_t x, int64_t arrivalUs);
    void noteFrame(int64_t outUs, int64_t arrivalUs, int64_t frameId, double residualUs);

    Config m_cfg;
    mutable std::mutex m_mutex;

    // Stream origin (absolute device µs, absolute host µs).
    bool    m_haveOrigin = false;
    int64_t m_originX = 0;
    int64_t m_originH = 0;
    int64_t m_lastX = 0;          // relative
    int64_t m_lastAbsX = 0;       // absolute device µs of the last frame
    bool    m_haveLastAbsX = false;

    // Current window.
    std::vector<Pt> m_hull;
    long double m_sumX = 0, m_sumH = 0;
    int64_t m_n = 0;
    int64_t m_firstX = 0;
    int64_t m_windowStartX = 0;

    // Fit.
    bool    m_haveFit = false;
    double  m_slope = 1.0;
    int64_t m_anchorX = 0, m_anchorH = 0;
    bool    m_shortBaseline = true, m_degenerate = false, m_implausible = false;
    double  m_pooledSlope = 1.0, m_pooledWeight = 0.0;

    // Latch.
    bool    m_haveLatch = false;
    int64_t m_latchHostUs = 0, m_latchDeviceUs = 0, m_latchRttUs = -1;
    bool    m_haveMinLatency = false;
    int64_t m_minLatencyUs = 0;

    // Publication.
    bool    m_havePub = false;
    int64_t m_pubAbsX = 0;        // absolute device µs the published line is anchored at
    // Absolute host µs at that device instant — FRACTIONAL. A frame advances the line by slope × period, a
    // non-integer number of µs; rounding it into an integer every frame lost up to 0.5 µs per frame, which
    // outruns a 50 µs/s slew and walked the line a millisecond off in a few seconds.
    long double m_pubAbsH = 0;
    double  m_pubSlope = 1.0;
    int64_t m_warmStartAbsX = 0;
    bool    m_haveLastOut = false;
    int64_t m_lastOut = 0;
    ClockMapMethod m_method = ClockMapMethod::None;

    // Stats.
    int64_t m_frames = 0, m_frameIdGaps = 0, m_streams = 0, m_clampArrival = 0, m_clampMono = 0;
    int64_t m_lastFrameId = -1;
    std::vector<int64_t> m_recentLag, m_recentResid;
    size_t  m_recentHead = 0;
};

} // namespace pinpoint
