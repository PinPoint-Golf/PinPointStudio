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

#include "eskf_orientation_filter.h"

#include "../Core/pp_tuned_constants.h"

#include <algorithm>
#include <cmath>

// Vendored ESKF (third_party/imu_ekf, on the include path). Pulls in Eigen via
// the compat shim <Eigen.h>; ESKF.h includes the template impl (ESKF.cpp) at its
// foot, so ESKF<float> is fully instantiated in this translation unit only.
#include <ESKF.h>

// THE LIBRARY'S CONVENTIONS, as read from third_party/imu_ekf (issue #104):
//   * qref_ is the NOMINAL attitude, a Hamilton body→world quaternion: predict() integrates
//     q̇ = ½ q ⊗ ω with ω in the body frame (the library's operator* is the reversed Hamilton
//     product, l*r = r ⊗ l, so (ω*q) there is q ⊗ ω), and toRotationMatrix() returns R(q)ᵀ,
//     world→body.
//   * Its world frame is z-DOWN (NED-like): correctAcc() predicts the accelerometer as
//     R(q)ᵀ·(0, 0, −9.81), the specific force of a sensor at rest when +Z points down, and
//     initWithAcc(0, 0, +1) seeds 180° about X. The app's world is z-UP (gravity reaction
//     along +Z, docs/reference/imu_axis_reference.md §3; MadgwickFilter). Same body frame.
//   * x_(9..11) is the attitude ERROR, local (q_true = q ⊗ δq): correctAcc() linearises with
//     H = [R(q)ᵀ·v]×, which is the body-frame-error Jacobian. reset() is the injection step:
//     q ← q ⊗ (1, δθ/2), normalised, δθ ← 0. It must close EVERY cycle. Without it the
//     accelerometer never reached qref_ (the published attitude was pure gyro integration),
//     the error state grew without bound, predict()'s attitude Jacobian ([δθ]×, ~0 only
//     while δθ is) multiplied the attitude covariance by |δθ|² each step once |δθ| > 1 rad,
//     and the filter overflowed to NaN — the 26 Oct divergence. (The covariance reset's
//     G ≈ I for the small δθ one cycle leaves, so the library's reset is complete as is.)
//   * correctAcc() has two branches: |a| within 0.7 m/s² of 9.81 corrects tilt; otherwise it
//     estimates a linear acceleration jointly with tilt, using cross-covariances predict()
//     never propagates. That branch is never entered here: the wrapper hands the library a
//     unit-g vector (below).
namespace {
// The vendored correctGyr() converts its inputs from degrees to radians
// internally (it multiplies by DEG_TO_RAD). IOrientationFilter::update() takes
// gyro in rad/s (the Madgwick convention), so undo that here.
constexpr float kRadToDeg = 57.295779513082320876798154814105f;

// App (z-up) ← library (z-down): q_app = Rx(180°) ⊗ q_lib, with Rx(180°) written (0, −1, 0, 0)
// so a level sensor reads +identity (w = +1), as Madgwick's does. Exact; the body frame is
// untouched, only the world is turned over.
struct Q { float w, x, y, z; };
Q libToApp(const Q &l) { return { l.x, -l.w, l.z, -l.y }; }

// The library's own tilt gain per accelerometer correction, from its fixed tuning (ESKF.cpp
// init(): attitude Q 0.025 rad²/s; accelerometer R diag 0.0142, 0.0165, 0.0100 g² × 9.81, mean
// here). predict() rebuilds the attitude variance as dt·Q every step (its attitude Jacobian is
// [δθ]×, zero once the error is injected), so the gain is a fixed k = dt·Q·g²/(dt·Q·g² + R):
// 0.08 at 200 Hz, a 60 ms time constant. That is the accelerometer believed almost outright.
float libraryTiltGain(float dt)
{
    constexpr float kQ = 0.025f, kG2 = 9.81f * 9.81f;
    constexpr float kR = (0.0141615383f + 0.0164647549f + 0.0100094303f) / 3.0f * 9.81f;
    const float a = dt * kQ * kG2;
    return a / (a + kR);
}

// 0..1, full at or under lo, none at or over hi, linear between.
float ramp(float v, float lo, float hi)
{
    if (std::isnan(v)) return 0.0f;
    if (v <= lo)       return 1.0f;
    if (v >= hi)       return 0.0f;
    return (hi - v) / (hi - lo);
}
} // namespace

EskfOrientationFilter::EskfOrientationFilter()
    : m_eskf(std::make_unique<IMU_EKF::ESKF<float>>())
{
    reset();
}

EskfOrientationFilter::~EskfOrientationFilter() = default;

void EskfOrientationFilter::cacheAttitude()
{
    const IMU_EKF::Quaternion<float> q = m_eskf->getAttitude();
    const Q a = libToApp({ q[IMU_EKF::w], q[IMU_EKF::v1], q[IMU_EKF::v2], q[IMU_EKF::v3] });
    m_w = a.w;
    m_x = a.x;
    m_y = a.y;
    m_z = a.z;
}

void EskfOrientationFilter::reset()
{
    // Full re-initialisation. (The library's own reset() is the error-state injection
    // step, NOT a filter reset.) Its init() leaves the LIBRARY's identity, which is upside
    // down in the app's world; seeding from a level reading lands on the app's identity, so
    // the cached attitude and the library agree.
    m_eskf->initWithAcc(0.0f, 0.0f, 1.0f);
    m_initialized = false;
    m_trust = 1.0f;
    cacheAttitude();
}

void EskfOrientationFilter::initFromAccel(float ax, float ay, float az)
{
    const float n = std::sqrt(ax * ax + ay * ay + az * az);
    if (!(n > 1e-6f) || !std::isfinite(n)) {
        reset();
        m_initialized = true;
        return;
    }
    const float ux = ax / n, uy = ay / n, uz = az / n;

    // NOT the library's initWithAcc(): its roll/pitch (AN3461) disagree with its own
    // measurement model off the vertical — it predicts gravity at (−x, −y, z) of the reading,
    // e.g. 160° wrong for a sensor on its side (exact only flat or upside down). So start
    // from the one pose it gets exactly — level, the app's identity (or upside down, 180°
    // about X, when the reading points down) — and walk the tilt onto the reading with the
    // filter's own correction + injection. Each step turns about the fixed axis up × reading,
    // so the walk ends on the shortest arc: the same seed as MadgwickFilter::initFromAccel
    // (exactly, for readings within ~154° of up).
    const bool fromBelow = uz < -0.9f;
    m_eskf->initWithAcc(0.0f, 0.0f, fromBelow ? -1.0f : 1.0f);
    constexpr float kSeedCos = 0.99999998f;   // within ~0.01°
    for (int k = 0; k < 60; ++k) {
        cacheAttitude();
        const float px = 2.0f * (m_x * m_z - m_w * m_y);
        const float py = 2.0f * (m_y * m_z + m_w * m_x);
        const float pz = 1.0f - 2.0f * (m_x * m_x + m_y * m_y);
        if (px * ux + py * uy + pz * uz >= kSeedCos) break;
        m_eskf->predict(1.0f);   // rate state is 0: no rotation, attitude variance → Q, so the step is near full
        m_eskf->correctAcc(ux, uy, uz);
        m_eskf->reset();
    }
    m_initialized = true;
    m_trust = 1.0f;
    cacheAttitude();
}

bool EskfOrientationFilter::setOrientation(float /*w*/, float /*x*/, float /*y*/, float /*z*/)
{
    // The vendored ESKF holds its reference quaternion (qref_) and nominal state
    // (x_) privately and exposes no attitude setter; the library is kept verbatim
    // (third_party/imu_ekf), so an EXACT warm-start to an arbitrary quaternion is
    // not available. Return false so offline re-fusion falls back to re-seeding
    // within the window (initFromAccel on the address still segment) and reports
    // the result as approximate.
    return false;
}

void EskfOrientationFilter::update(float ax, float ay, float az,
                                   float gx, float gy, float gz, float dt)
{
    namespace tf = pinpoint::tuned::filter;

    // 1. The gyro measures the rate state; correctGyr expects deg/s (it converts to rad/s
    //    internally). Folded in BEFORE predict so this sample's rate drives this step, as
    //    Madgwick's does.
    m_eskf->correctGyr(gx * kRadToDeg, gy * kRadToDeg, gz * kRadToDeg);

    // 2. Propagate the nominal attitude and the covariance over dt.
    m_eskf->predict(dt);

    // 3. Gravity. How far the accelerometer may be believed to read gravity alone:
    //    |‖a‖ − 1 g| and the rotation rate (a proxy for the centripetal and tangential
    //    acceleration a norm test cannot see) both small. Trust drops at once and recovers
    //    over kEskfTrustRecoverS, so a sample that happens to read 1 g mid-motion (a swing
    //    passing through 1 g) is not believed. This, with the tilt time constant below, is the
    //    ESKF's counterpart of Madgwick's bounded gain. The library's R is fixed, so a weight w
    //    scales the innovation instead: the library is handed the direction w of the way from
    //    its prediction to the reading (for the small angles of one step, the same as scaling
    //    its gain by w).
    const float an = std::sqrt(ax * ax + ay * ay + az * az);
    const float gn = std::sqrt(gx * gx + gy * gy + gz * gz);
    const float wNow = std::isfinite(an) && an > 1e-6f
        ? ramp(std::abs(an - 1.0f), tf::kEskfAccelFullTrustG, tf::kEskfAccelNoTrustG)
              * ramp(gn, tf::kEskfGyroFullTrustRadps, tf::kEskfGyroNoTrustRadps)
        : 0.0f;
    const float climb = dt > 0.0f ? dt / tf::kEskfTrustRecoverS : 0.0f;
    m_trust = std::min(wNow, m_trust + climb);
    // Even fully trusted, the tilt follows gravity with time constant kEskfTiltTauS, not the
    // library's 60 ms: a sideways acceleration barely changes ‖a‖ (0.3 g sideways reads
    // 1.04 g) yet turns the reading 17°, and no norm test can see it.
    const float kLib = dt > 0.0f ? libraryTiltGain(dt) : 0.0f;
    const float wCap = kLib > 0.0f ? std::min(1.0f, (1.0f - std::exp(-dt / tf::kEskfTiltTauS)) / kLib) : 0.0f;
    const float w    = m_trust * wCap;

    if (w > 0.0f) {
        const float ux = ax / an, uy = ay / an, uz = az / an;   // measured up, body frame
        float vx = ux, vy = uy, vz = uz;
        if (w < 1.0f) {
            // Predicted up in the body frame: R(q_app)ᵀ·(0, 0, 1).
            cacheAttitude();
            const float px = 2.0f * (m_x * m_z - m_w * m_y);
            const float py = 2.0f * (m_y * m_z + m_w * m_x);
            const float pz = 1.0f - 2.0f * (m_x * m_x + m_y * m_y);
            vx = px + w * (ux - px);
            vy = py + w * (uy - py);
            vz = pz + w * (uz - pz);
            const float vn = std::sqrt(vx * vx + vy * vy + vz * vz);
            vx /= vn; vy /= vn; vz /= vn;
        }
        // A unit vector in g: the library's quasi-static (tilt) branch, always. The body
        // frame is common to both worlds, so the reading goes in as measured.
        m_eskf->correctAcc(vx, vy, vz);
    }

    // 4. Inject the error state into the nominal attitude and zero it — every cycle.
    m_eskf->reset();

    // (Future: m_eskf->correctMag(...) once a magnetometer is wired in.)
    cacheAttitude();
}
