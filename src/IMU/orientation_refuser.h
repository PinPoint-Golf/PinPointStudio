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

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include "iorientation_filter.h"

// Offline orientation RE-FUSION + parity — the load-bearing capability for
// post-hoc tuning of the orientation filter (phase-adaptive gain + impact
// handling). See docs/validation/pipeline_validation_and_tuning.md §5.3.1 and
// corpus1_collection_protocol.md §0a.
//
// The orientation filter runs only on the live I/O thread (ImuBase::fuseRawImu);
// the analyzer consumes the *stored* fused quaternion. To TUNE the filter we must
// re-run it offline from the recorded raw accel+gyro. The recorded data is
// sufficient (imu_sample_v2 persists raw accel (g) + gyro (deg/s) + the live-fused
// quaternion, with per-sample timestamps and the per-device A/M snapshot), so a
// captured swing is re-fusable — this module is the re-fusion itself.
//
// FIDELITY (must match ImuBase::fuseRawImu exactly or parity fails for reasons
// unrelated to the parameters under test):
//   * dt is the NOMINAL period 1/outputRateHz, NOT per-sample timestamp deltas
//     (BLE delivers bursts, so arrival deltas alias to ~0 within a burst and a
//     gap across it; the live filter integrates with the configured cadence).
//   * the filter takes gyro in RAD/S; ImuSample stores DEG/S, so convert here.
//   * accel stays in g.
//
// WARM-START: the stored quaternion at the window's first sample already encodes
// pre-window history (seed convergence + accumulated yaw drift), which a 5 s
// trailing capture window cannot reconstruct by re-seeding from gravity. So we
// inject that stored quaternion as the filter's initial state (IOrientationFilter
// ::setOrientation). Filters that cannot be set exactly (ESKF) fall back to
// initFromAccel and the early-window samples are flagged approximate.
//
// Header-only and free of Qt/Eigen so it can be unit-tested standalone (the
// concrete filter is injected by reference) and reused by swinglab_run.
namespace pinpoint {

// One recorded IMU sample, exactly as persisted (imu_sample_v2 fields + the
// sample timestamp). gyro is DEG/S as stored; accel is g; quat is the live-fused
// reference (the parity target), wxyz.
struct RefuseSample {
    int64_t t_us = 0;
    float ax = 0.f, ay = 0.f, az = 0.f;          // accel, g (sensor frame)
    float gx = 0.f, gy = 0.f, gz = 0.f;          // gyro, deg/s (sensor frame)
    float qw = 1.f, qx = 0.f, qy = 0.f, qz = 0.f; // stored live-fused quat (reference)
};

struct RefuseConfig {
    float outputRateHz = 200.0f;   // nominal cadence -> dt = 1/rate (matches ImuBase)
    bool  warmStart    = true;     // warm-start from samples[0] stored quat
    // (phase-adaptive / impact-handling knobs extend this struct in a later phase)
};

struct RefuseResult {
    std::vector<std::array<float, 4>> quat;  // re-fused quaternion per sample (wxyz)
    bool   warmStarted = false;              // false -> filter re-seeded (early samples approximate)
    size_t seededAt    = 0;                  // index where update() integration began
};

// Geodesic angle (degrees) between two quaternions, sign-insensitive (q and -q
// are the same rotation). Inputs need not be unit — they are normalised here.
inline double quatAngleDeg(const float *a, const float *b)
{
    const double na = std::sqrt((double)a[0]*a[0] + (double)a[1]*a[1] + (double)a[2]*a[2] + (double)a[3]*a[3]);
    const double nb = std::sqrt((double)b[0]*b[0] + (double)b[1]*b[1] + (double)b[2]*b[2] + (double)b[3]*b[3]);
    if (na < 1e-12 || nb < 1e-12) return 0.0;
    double d = ((double)a[0]*b[0] + (double)a[1]*b[1] + (double)a[2]*b[2] + (double)a[3]*b[3]) / (na * nb);
    d = std::fabs(d);
    if (d > 1.0) d = 1.0;
    return 2.0 * std::acos(d) * 57.29577951308232087;
}

// Re-run an orientation filter over recorded samples, reproducing fuseRawImu
// (nominal dt, gyro deg/s -> rad/s, accel g). Warm-starts from the stored quat at
// samples[0] when the filter supports it; otherwise seeds from samples[0]'s accel
// (gravity) and sets warmStarted=false. The filter is reset() first, so the caller
// can reuse one instance across swings.
inline RefuseResult refuseOrientation(IOrientationFilter &filt,
                                       const std::vector<RefuseSample> &s,
                                       const RefuseConfig &cfg)
{
    RefuseResult out;
    out.quat.resize(s.size());
    if (s.empty()) return out;

    const float dt = (cfg.outputRateHz > 0.f) ? (1.0f / cfg.outputRateHz) : 0.005f;
    constexpr float kDegToRad = 0.01745329251994329577f;

    filt.reset();
    if (cfg.warmStart && filt.setOrientation(s[0].qw, s[0].qx, s[0].qy, s[0].qz)) {
        out.warmStarted = true;
        out.quat[0] = { s[0].qw, s[0].qx, s[0].qy, s[0].qz };   // anchor == stored
    } else {
        // Fallback: seed from the first sample's gravity (loses pre-window yaw).
        filt.initFromAccel(s[0].ax, s[0].ay, s[0].az);
        out.warmStarted = false;
        out.quat[0] = { filt.w(), filt.x(), filt.y(), filt.z() };
    }
    out.seededAt = 0;

    for (size_t i = 1; i < s.size(); ++i) {
        filt.update(s[i].ax, s[i].ay, s[i].az,
                    s[i].gx * kDegToRad, s[i].gy * kDegToRad, s[i].gz * kDegToRad, dt);
        out.quat[i] = { filt.w(), filt.x(), filt.y(), filt.z() };
    }
    return out;
}

struct ParityStats {
    size_t n       = 0;
    double meanDeg = 0.0;
    double rmsDeg  = 0.0;
    double maxDeg  = 0.0;
    double p95Deg  = 0.0;
    size_t maxAt   = 0;   // sample index of the worst disagreement
};

// Per-sample geodesic disagreement between the re-fused trajectory and the stored
// live quaternion — the parity gate. `skip` drops the first N samples from the
// statistics (use it to exclude the unconverged head when warmStarted == false).
inline ParityStats parity(const std::vector<RefuseSample> &s,
                          const RefuseResult &r,
                          size_t skip = 0)
{
    ParityStats st;
    const size_t n = std::min(s.size(), r.quat.size());
    if (skip >= n) return st;

    std::vector<double> errs;
    errs.reserve(n - skip);
    double sum = 0.0, sumsq = 0.0, mx = 0.0;
    size_t mxAt = skip;
    for (size_t i = skip; i < n; ++i) {
        const float stored[4] = { s[i].qw, s[i].qx, s[i].qy, s[i].qz };
        const double e = quatAngleDeg(stored, r.quat[i].data());
        errs.push_back(e);
        sum += e;
        sumsq += e * e;
        if (e > mx) { mx = e; mxAt = i; }
    }
    st.n       = errs.size();
    st.meanDeg = sum / double(st.n);
    st.rmsDeg  = std::sqrt(sumsq / double(st.n));
    st.maxDeg  = mx;
    st.maxAt   = mxAt;
    std::sort(errs.begin(), errs.end());
    st.p95Deg  = errs[size_t(std::floor(0.95 * double(errs.size() - 1)))];
    return st;
}

} // namespace pinpoint
