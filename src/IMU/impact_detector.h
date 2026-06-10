/*
 * Copyright (c) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#pragma once

#include <array>
#include <cmath>
#include <cstdint>

// IMU impact detector — pure math, header-only, no Qt, no allocation in
// push(). Unit-tested standalone in src/IMU/tests without BLE or the app
// build (the live hook lives in ImuInstance).
//
// A shot candidate is an accelerometer *jerk peak* — a local maximum above an
// adaptive threshold reached with a minimum rise rate — that is preceded by a
// *swing-energy signature* (sustained gyro magnitude in a rolling window: a
// downswing rotates fast into impact, a mat tap or address knock does not) at
// a plausible club orientation, with a refractory so the follow-through
// cannot double-fire. All windows are expressed in milliseconds and evaluated
// against sample timestamps, so 100↔200 Hz rate changes do not alter
// behaviour.
//
// Timestamps are host-arrival (EventBuffer::nowMicros domain); the BLE chain
// delays them by a roughly constant latency the caller passes in, so
// est_t_us = peak_t_us − bleLatencyUs estimates the true impact instant
// (P4 auto-calibrates the constant).
namespace pinpoint {

struct ImpactDetectorConfig {
    // Accel jerk-peak gates (magnitudes in g, gravity included).
    float accelFloorG   = 4.0f;    // absolute minimum peak height
    float accelAdaptive = 3.0f;    // peak must also exceed this × slow EMA of |a|
    float jerkMinGps    = 100.0f;  // minimum rise rate into the peak (g/s) —
                                   // rejects slow swells that crest above the
                                   // threshold without a strike's sharp edge

    // Swing-energy gate: rolling window ending at the peak.
    float gyroWindowMs   = 400.0f;
    float gyroPeakMinDps = 300.0f; // max |gyro| in window (downswing rotation)
    float gyroMeanMinDps = 80.0f;  // mean |gyro| in window (sustained, not a knock)

    // Club-orientation gate — the weakest-evidenced gate, kept permissive and
    // disableable while tuning.
    bool  orientationGate = true;
    float clubVerticalMin = 0.35f; // |dot(shaft axis in world, vertical)|

    float refractoryMs = 200.0f;

    // Host-arrival → true-impact correction (not hard-coded; P4 calibrates).
    int64_t bleLatencyUs = 0;

    // Sensitivity scale applied to every accel/gyro threshold:
    // >1 = less sensitive (Low), <1 = more sensitive (High).
    float thresholdScale = 1.0f;
};

struct ImpactSample {
    float   accelMag     = 0.0f;  // |accel| in g (rotation-invariant)
    float   gyroMag      = 0.0f;  // |gyro| in °/s
    float   clubVertical = 0.0f;  // |dot(shaft axis_world, up)| in [0,1]
    int64_t t_us         = 0;     // host-arrival steady_clock µs
};

struct ImpactResult {
    bool    impact     = false;
    int64_t est_t_us   = 0;       // peak_t_us − bleLatencyUs
    float   confidence = 0.0f;    // from swing energy — never from peak g,
                                  // which clips at the ±16 g full scale
};

class ImpactDetector {
public:
    ImpactDetector() = default;
    explicit ImpactDetector(const ImpactDetectorConfig &cfg) : m_cfg(cfg) {}

    ImpactDetectorConfig       &config()       { return m_cfg; }
    const ImpactDetectorConfig &config() const { return m_cfg; }

    void reset()
    {
        m_head = 0;
        m_size = 0;
        m_emaAccel  = 1.0f;   // resting |a| = gravity
        m_hasImpact = false;
    }

    // Feed one sample (monotonic t_us); returns Impact at most once per
    // refractory. The decision is delayed by exactly one sample — a local
    // maximum needs one sample of lookahead.
    ImpactResult push(const ImpactSample &s)
    {
        ImpactResult res;

        // Slow EMA of |a| = adaptive noise floor (τ ≈ 1 s — a one-sample
        // spike barely moves it; dt from timestamps keeps it rate-blind).
        if (m_size > 0) {
            const float dtS = static_cast<float>(s.t_us - at(0).t_us) * 1e-6f;
            if (dtS > 0.0f && dtS < 0.5f) {
                const float alpha = 1.0f - std::exp(-dtS / kEmaTauS);
                m_emaAccel += alpha * (s.accelMag - m_emaAccel);
            }
        }

        append(s);
        if (m_size < 3)
            return res;

        const ImpactSample &cur  = at(0);   // newest — lookahead sample
        const ImpactSample &peak = at(1);   // candidate
        const ImpactSample &prev = at(2);

        // Local max: strict rise in, level-or-fall out ('>=' so a clipped
        // ±16 g plateau still fires on its first sample).
        if (!(peak.accelMag > prev.accelMag && peak.accelMag >= cur.accelMag))
            return res;

        const float scale     = m_cfg.thresholdScale;
        const float threshold = std::max(m_cfg.accelFloorG * scale,
                                         m_cfg.accelAdaptive * m_emaAccel * scale);
        if (peak.accelMag < threshold)
            return res;

        // Jerk gate — a strike is a sharp edge, not a swell cresting.
        const float riseDtS = static_cast<float>(peak.t_us - prev.t_us) * 1e-6f;
        if (riseDtS <= 0.0f)
            return res;
        if ((peak.accelMag - prev.accelMag) / riseDtS < m_cfg.jerkMinGps * scale)
            return res;

        // Refractory — one shot per strike, follow-through taps suppressed.
        if (m_hasImpact &&
            peak.t_us - m_lastImpactTUs <
                static_cast<int64_t>(m_cfg.refractoryMs * 1000.0f))
            return res;

        // Swing-energy gate over the window ending at the peak (inclusive).
        const int64_t windowUs = static_cast<int64_t>(m_cfg.gyroWindowMs * 1000.0f);
        float   gyroPeak = 0.0f, gyroSum = 0.0f;
        int     n        = 0;
        int64_t oldestT  = peak.t_us;
        for (int i = 1; i < m_size; ++i) {
            const ImpactSample &h = at(i);
            if (peak.t_us - h.t_us > windowUs)
                break;
            gyroPeak = std::max(gyroPeak, h.gyroMag);
            gyroSum += h.gyroMag;
            ++n;
            oldestT = h.t_us;
        }
        // Require at least half the window of history — never fires on the
        // first samples after connect.
        if (n < 2 || peak.t_us - oldestT < windowUs / 2)
            return res;
        const float gyroPeakMin = m_cfg.gyroPeakMinDps * scale;
        if (gyroPeak < gyroPeakMin ||
            gyroSum / static_cast<float>(n) < m_cfg.gyroMeanMinDps * scale)
            return res;

        if (m_cfg.orientationGate && peak.clubVertical < m_cfg.clubVerticalMin)
            return res;

        m_hasImpact     = true;
        m_lastImpactTUs = peak.t_us;
        res.impact      = true;
        res.est_t_us    = peak.t_us - m_cfg.bleLatencyUs;
        // 0.5 at the swing-energy threshold → 1.0 at twice it.
        res.confidence  = std::min(1.0f, gyroPeak / (2.0f * gyroPeakMin));
        return res;
    }

private:
    static constexpr int   kCapacity = 512;   // > 2.5 s at 200 Hz
    static constexpr float kEmaTauS  = 1.0f;

    // at(0) = newest sample.
    const ImpactSample &at(int back) const
    {
        return m_ring[static_cast<size_t>((m_head - 1 - back + 2 * kCapacity) % kCapacity)];
    }
    void append(const ImpactSample &s)
    {
        m_ring[static_cast<size_t>(m_head)] = s;
        m_head = (m_head + 1) % kCapacity;
        if (m_size < kCapacity)
            ++m_size;
    }

    ImpactDetectorConfig m_cfg;
    std::array<ImpactSample, kCapacity> m_ring {};
    int     m_head = 0;
    int     m_size = 0;
    float   m_emaAccel  = 1.0f;
    bool    m_hasImpact = false;
    int64_t m_lastImpactTUs = 0;
};

} // namespace pinpoint
