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

// Shaft track assembly (ShaftTracker S2) — synthetic-sequence tests:
// ŝ_hand calibration (incl. mirrored chirality), Viterbi clutter rejection
// (with and without the IMU channel), IMU-bridged dropouts through the fast
// downswing, vision-only coasting, wrap/unwrap continuity across ±180°,
// RTS lag at peak angular velocity, and the 60% coverage validity gate.

#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

#include <QQuaternion>

#include "../shaft_track_assembly.h"

using namespace pinpoint::analysis;

static int g_fail = 0;

#define CHECK(label, cond)                                                       \
    do { bool ok = (cond);                                                       \
         std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", label);                \
         if (!ok) ++g_fail; } while (0)

#define CHECK_NEAR(label, got, want, tol)                                        \
    do { double g = (got), w = (want); bool ok = std::abs(g - w) <= (tol);       \
         std::printf("  [%s] %-44s got %9.3f  want %9.3f (tol %.3f)\n",          \
                     ok ? "PASS" : "FAIL", label, g, w, double(tol));            \
         if (!ok) ++g_fail; } while (0)

#define CHECK_LT(label, got, bound)                                              \
    do { double g = (got), b = (bound); bool ok = g < b;                         \
         std::printf("  [%s] %-44s got %9.3f  <    %9.3f\n",                     \
                     ok ? "PASS" : "FAIL", label, g, b);                         \
         if (!ok) ++g_fail; } while (0)

static constexpr double kPi  = 3.14159265358979323846;
static constexpr double kDeg = kPi / 180.0;

static double wrapNear(double a)
{
    a = std::fmod(a + kPi, 2.0 * kPi);
    if (a <= 0.0) a += 2.0 * kPi;
    return a - kPi;
}

// ---------------------------------------------------------------------------
// Synthetic swing: C¹ cosine-eased image-angle profile (degrees), 60 fps.
// Address hold → 200° backswing (crosses +180° ⇒ exercises wrap) → −300°
// downswing in 0.25 s (peak ω ≈ 33 rad/s) → settle.

static double ease(double u) { return 0.5 * (1.0 - std::cos(kPi * u)); }

static double thetaTrueDeg(double t)
{
    const double th0 = 150.0;
    if (t < 0.3)  return th0;
    if (t < 1.1)  return th0 + 200.0 * ease((t - 0.3) / 0.8);
    if (t < 1.35) return th0 + 200.0 - 300.0 * ease((t - 1.1) / 0.25);
    return th0 - 100.0 + 60.0 * ease(std::min((t - 1.35) / 0.45, 1.0));
}

static double omegaTrueRadS(double t)   // central difference
{
    const double h = 1e-4;
    return (thetaTrueDeg(t + h) - thetaTrueDeg(t - h)) * kDeg / (2.0 * h);
}

struct Synth {
    std::vector<ShaftFrameObs> obs;
    std::vector<double> trueThetaRad;       // unwrapped (continuous) truth
    std::vector<double> trueOmega;
    int64_t t0 = 1'000'000;
};

struct SynthOptions {
    bool   imu            = true;
    double imuOffsetDeg   = 23.0;    // δ_true: IMU-world ↔ image yaw misalignment
    bool   mirrored       = false;   // image angle = −φ + δ (chirality flip)
    int    clutterFirst   = -1, clutterLast = -2;   // frames with a STRONGER false ridge
    int    dropFirst      = -1, dropLast = -2;      // frames with no candidates at all
    int    dropEvery      = 0;       // additionally drop every Nth frame (gate test)
};

static Synth makeSynth(const SynthOptions &so)
{
    std::mt19937 rng(42);
    std::normal_distribution<double>       noise(0.0, 1.0);
    std::uniform_real_distribution<double> uni(0.0, 1.0);

    // Hand-frame gauge: q(t) = R_y(φ)·R_fix with R_fix arbitrary; the fit's
    // (ŝ, δ, sign) only ever needs to reproduce the projected angle.
    const QQuaternion rFix = QQuaternion::fromAxisAndAngle(0.4f, 0.2f, 0.88f, 37.0f);

    Synth s;
    double unwrapped = 0.0;
    bool   first     = true;
    for (int i = 0; i < 110; ++i) {
        const double t  = i / 60.0;
        const double td = thetaTrueDeg(t);
        const double sgn = so.mirrored ? -1.0 : 1.0;
        const double thImg = wrapNear(sgn * (td - so.imuOffsetDeg) * kDeg
                                      + so.imuOffsetDeg * kDeg);
        // Continuous truth for assertions.
        if (first) { unwrapped = thImg; first = false; }
        else         unwrapped += wrapNear(thImg - unwrapped);
        s.trueThetaRad.push_back(unwrapped);
        s.trueOmega.push_back(sgn * omegaTrueRadS(t));

        ShaftFrameObs o;
        o.t_us   = s.t0 + int64_t(t * 1e6 + 0.5);
        o.gripPx = cv::Point2f(300.f, 400.f);
        if (so.imu) {
            // φ = θ_raw such that image angle = sign·φ + δ.
            const double phi = td - so.imuOffsetDeg;
            o.qHand = QQuaternion::fromAxisAndAngle(0.f, 1.f, 0.f, float(phi)) * rFix;
            o.qHandValid = true;
        }

        const bool dropped = (i >= so.dropFirst && i <= so.dropLast)
                          || (so.dropEvery > 0 && (i % so.dropEvery) == 0);
        if (!dropped) {
            const double om   = std::abs(s.trueOmega.back());
            const bool wedge  = om > 15.0;
            const double sig  = wedge ? 1.5 * kDeg : 0.6 * kDeg;

            ShaftCandidate c;
            c.thetaRad      = float(wrapNear(thImg + sig * noise(rng)));
            c.sigmaThetaRad = float(wedge ? om * 0.004 / 2.0 : 0.5 * kDeg);
            c.visibleLenPx  = float(290.0 + 8.0 * noise(rng));
            c.score         = float(0.75 + 0.2 * uni(rng));
            c.wedge         = wedge;
            c.headPx        = o.gripPx + cv::Point2f(float(c.visibleLenPx * std::cos(c.thetaRad)),
                                                     float(c.visibleLenPx * std::sin(c.thetaRad)));
            o.candidates.push_back(c);

            if (i >= so.clutterFirst && i <= so.clutterLast) {
                ShaftCandidate cl = c;            // stronger false ridge, +25° off
                cl.thetaRad = float(wrapNear(double(c.thetaRad) + 25.0 * kDeg));
                cl.score    = c.score * 1.3f;
                cl.wedge    = false;
                o.candidates.insert(o.candidates.begin(), cl);   // sorted desc by score
            } else {
                ShaftCandidate weak = c;          // background clutter, never plausible
                weak.thetaRad = float(wrapNear(uni(rng) * 2.0 * kPi - kPi));
                weak.score    = c.score * float(0.2 + 0.2 * uni(rng));
                weak.visibleLenPx = float(60.0 + 40.0 * uni(rng));
                o.candidates.push_back(weak);
            }
        }
        s.obs.push_back(std::move(o));
    }
    return s;
}

// Align an unwrapped track to the unwrapped truth (both are continuous; the
// assembly's absolute origin may differ by k·2π).
static double alignOffset(double sample0, double truth0)
{
    return std::round((sample0 - truth0) / (2.0 * kPi)) * 2.0 * kPi;
}

// ---------------------------------------------------------------------------

int main()
{
    // ===== 1. IMU run: calibration, clutter rejection, downswing bridge =====
    std::printf("== IMU-fused run ==\n");
    {
        SynthOptions so;
        so.clutterFirst = 40; so.clutterLast = 44;    // mid-backswing, stronger ridge
        so.dropFirst    = 66; so.dropLast    = 73;    // 8 frames inside the downswing
        const Synth s = makeSynth(so);

        AssemblyConfig cfg;
        const ShaftInHandFit fit = ShaftTrackAssembly::calibrateShaftInHand(s.obs, cfg);
        CHECK("s_hand fit converged", fit.ok);
        CHECK_NEAR("fit sign", fit.sign, 1.0, 0.1);
        CHECK_LT("fit residual (deg)", fit.residualRad / kDeg, 2.0);

        // The channel must reproduce the projected angle everywhere — that is
        // the gauge-invariant statement (ŝ itself has a one-parameter family).
        double chanRms = 0.0;
        int    chanN   = 0;
        for (size_t i = 0; i < s.obs.size(); ++i) {
            if (!s.obs[i].qHandValid) continue;
            const double pred = ShaftTrackAssembly::predictTheta(fit, s.obs[i].qHand);
            const double err  = wrapNear(pred - wrapNear(s.trueThetaRad[i]));
            chanRms += err * err;
            ++chanN;
        }
        chanRms = std::sqrt(chanRms / chanN) / kDeg;
        CHECK_LT("IMU channel RMS vs truth (deg)", chanRms, 1.5);

        const std::vector<double> chan = ShaftTrackAssembly::imuThetaChannel(s.obs, fit);
        const std::vector<int> sel = ShaftTrackAssembly::associate(s.obs, chan, cfg);
        bool clutterRejected = true;
        for (int i = so.clutterFirst; i <= so.clutterLast; ++i) {
            if (sel[size_t(i)] < 0) { clutterRejected = false; break; }
            const double th = s.obs[size_t(i)].candidates[size_t(sel[size_t(i)])].thetaRad;
            if (std::abs(wrapNear(th - wrapNear(s.trueThetaRad[size_t(i)]))) > 5.0 * kDeg)
                clutterRejected = false;
        }
        CHECK("Viterbi rejects stronger clutter ridge (IMU)", clutterRejected);

        const ShaftTrack2D track = ShaftTrackAssembly::assemble(
            s.obs, s.obs.front().t_us, s.obs.back().t_us, cfg);
        CHECK("track valid", track.valid);
        CHECK_LT("coverage shortfall", 1.0 - track.coverage, 0.05);
        CHECK("imuVisionCorr > 0.9", track.imuVisionCorr > 0.9f);

        const double off = alignOffset(track.samples.front().thetaRad, s.trueThetaRad.front());
        double rms = 0.0, dropMax = 0.0, peakErr = 0.0, maxStep = 0.0;
        int    peakIdx = 0;
        bool   bridged = true;
        for (double om : s.trueOmega) (void)om;
        for (size_t k = 0; k < track.samples.size(); ++k) {
            const size_t i = k;   // samples start at frame 0 here (measured at 0)
            const double err = track.samples[k].thetaRad - off - s.trueThetaRad[i];
            rms += err * err;
            if (std::abs(s.trueOmega[i]) > std::abs(s.trueOmega[size_t(peakIdx)]))
                peakIdx = int(i);
            if (int(i) >= so.dropFirst && int(i) <= so.dropLast) {
                dropMax = std::max(dropMax, std::abs(err));
                if (!(track.samples[k].flags & ShaftImuBridged))
                    bridged = false;
            }
            if (k > 0)
                maxStep = std::max(maxStep, std::abs(track.samples[k].thetaRad
                                                     - track.samples[k - 1].thetaRad));
        }
        rms = std::sqrt(rms / track.samples.size()) / kDeg;
        peakErr = std::abs(track.samples[size_t(peakIdx)].thetaRad - off
                           - s.trueThetaRad[size_t(peakIdx)]) / kDeg;
        CHECK_LT("smoothed RMS error (deg)", rms, 1.5);
        CHECK_LT("max error across IMU-bridged dropout (deg)", dropMax / kDeg, 4.0);
        CHECK("dropout frames flagged ImuBridged", bridged);
        CHECK_LT("error at peak angular velocity (deg) [RTS lag]", peakErr, 2.0);
        CHECK_LT("max per-frame step (deg) [unwrap continuity]", maxStep / kDeg, 40.0);

        // The unwrapped sweep from top to post-impact must be ≈ −300°, which
        // only a correctly unwrapped track can show.
        const double sweep = (track.samples[81].thetaRad - track.samples[66].thetaRad) / kDeg;
        CHECK_NEAR("unwrapped downswing sweep (deg)", sweep,
                   s.trueThetaRad[81] / kDeg - s.trueThetaRad[66] / kDeg, 6.0);
    }

    // ===== 2. Mirrored chirality =====
    std::printf("== Mirrored camera ==\n");
    {
        SynthOptions so;
        so.mirrored = true;
        const Synth s = makeSynth(so);
        AssemblyConfig cfg;
        const ShaftInHandFit fit = ShaftTrackAssembly::calibrateShaftInHand(s.obs, cfg);
        CHECK("mirrored fit converged", fit.ok);
        CHECK_NEAR("mirrored fit sign", fit.sign, -1.0, 0.1);
        double rms = 0.0; int n = 0;
        for (size_t i = 0; i < s.obs.size(); ++i) {
            const double pred = ShaftTrackAssembly::predictTheta(fit, s.obs[i].qHand);
            const double err  = wrapNear(pred - wrapNear(s.trueThetaRad[i]));
            rms += err * err; ++n;
        }
        CHECK_LT("mirrored channel RMS (deg)", std::sqrt(rms / n) / kDeg, 1.5);
    }

    // ===== 3. Vision-only: coasting + trend-based clutter rejection =====
    std::printf("== Vision-only run ==\n");
    {
        SynthOptions so;
        so.imu = false;
        so.clutterFirst = 30; so.clutterLast = 34;    // slow backswing
        so.dropFirst    = 24; so.dropLast    = 27;    // 4 frames, slow phase
        const Synth s = makeSynth(so);
        AssemblyConfig cfg;

        const ShaftInHandFit fit = ShaftTrackAssembly::calibrateShaftInHand(s.obs, cfg);
        CHECK("no IMU -> no fit", !fit.ok);

        const std::vector<double> chan = ShaftTrackAssembly::imuThetaChannel(s.obs, fit);
        const std::vector<int> sel = ShaftTrackAssembly::associate(s.obs, chan, cfg);
        bool clutterRejected = true;
        for (int i = so.clutterFirst; i <= so.clutterLast; ++i) {
            if (sel[size_t(i)] < 0) continue;   // missing is acceptable rejection
            const double th = s.obs[size_t(i)].candidates[size_t(sel[size_t(i)])].thetaRad;
            if (std::abs(wrapNear(th - wrapNear(s.trueThetaRad[size_t(i)]))) > 5.0 * kDeg)
                clutterRejected = false;
        }
        CHECK("Viterbi rejects clutter on velocity trend alone", clutterRejected);

        const ShaftTrack2D track = ShaftTrackAssembly::assemble(
            s.obs, s.obs.front().t_us, s.obs.back().t_us, cfg);
        CHECK("vision-only track valid", track.valid);
        CHECK_NEAR("imuVisionCorr without IMU", track.imuVisionCorr, 0.0, 1e-6);

        const double off = alignOffset(track.samples.front().thetaRad, s.trueThetaRad.front());
        double rms = 0.0, dropMax = 0.0;
        bool coasted = true;
        for (size_t k = 0; k < track.samples.size(); ++k) {
            const double err = track.samples[k].thetaRad - off - s.trueThetaRad[k];
            rms += err * err;
            if (int(k) >= so.dropFirst && int(k) <= so.dropLast) {
                dropMax = std::max(dropMax, std::abs(err));
                if (!(track.samples[k].flags & ShaftCoasted))
                    coasted = false;
            }
        }
        rms = std::sqrt(rms / track.samples.size()) / kDeg;
        CHECK_LT("vision-only smoothed RMS (deg)", rms, 2.0);
        CHECK_LT("coasted dropout max error (deg)", dropMax / kDeg, 6.0);
        CHECK("dropout frames flagged Coasted", coasted);
    }

    // ===== 4. Coverage validity gate =====
    std::printf("== Coverage gate ==\n");
    {
        SynthOptions so;
        so.imu = false;
        so.dropEvery = 2;          // every other frame unmeasured, no IMU bridge
        const Synth s = makeSynth(so);
        const ShaftTrack2D track = ShaftTrackAssembly::assemble(
            s.obs, s.obs.front().t_us, s.obs.back().t_us, AssemblyConfig{});
        CHECK("≈50% coverage without IMU fails the 60% gate", !track.valid);
        CHECK_LT("gate coverage", double(track.coverage), 0.60);
    }

    // ===== 5. IMU-independence guarantee (clean vision-only baseline) =========
    // The shaft tracker is used in production with IMUs on the SPINE (no club /
    // lead-hand sensor), so it MUST yield a full, valid track from vision alone.
    // This is the explicit, named invariant (test 3 stresses clutter/dropouts;
    // this asserts the clean no-IMU case + that the IMU channel is truly inert —
    // a guard against any future change that hard-couples the tracker to an IMU).
    std::printf("== Vision-only baseline (IMU-independence) ==\n");
    {
        SynthOptions so;
        so.imu = false;                 // no qHand on ANY frame; no clutter, no drops
        const Synth s = makeSynth(so);
        AssemblyConfig cfg;

        const ShaftInHandFit fit = ShaftTrackAssembly::calibrateShaftInHand(s.obs, cfg);
        CHECK("no IMU -> no s_hand fit", !fit.ok);

        const ShaftTrack2D track = ShaftTrackAssembly::assemble(
            s.obs, s.obs.front().t_us, s.obs.back().t_us, cfg);
        CHECK("vision-only track valid (no IMU)", track.valid);
        CHECK_LT("coverage meets the gate without IMU", cfg.coverageMin - double(track.coverage), 1e-6);
        CHECK_LT("near-full coverage shortfall", 1.0 - double(track.coverage), 0.05);
        CHECK_NEAR("imuVisionCorr == 0 (channel inert, not fabricated)",
                   track.imuVisionCorr, 0.0, 1e-6);

        bool anyImuBridged = false;
        for (const ShaftSample2D &smp : track.samples)
            if (smp.flags & ShaftImuBridged) anyImuBridged = true;
        CHECK("no sample claims ImuBridged (channel never engaged)", !anyImuBridged);

        const double off = alignOffset(track.samples.front().thetaRad, s.trueThetaRad.front());
        double rms = 0.0;
        for (size_t k = 0; k < track.samples.size(); ++k) {
            const double err = track.samples[k].thetaRad - off - s.trueThetaRad[k];
            rms += err * err;
        }
        CHECK_LT("vision-only baseline RMS (deg)", std::sqrt(rms / track.samples.size()) / kDeg, 1.5);
    }

    std::printf("%s (%d failure%s)\n", g_fail == 0 ? "ALL PASS" : "FAILURES",
                g_fail, g_fail == 1 ? "" : "s");
    return g_fail == 0 ? 0 : 1;
}
