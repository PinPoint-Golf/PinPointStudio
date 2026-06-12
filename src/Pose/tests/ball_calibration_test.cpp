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

// Tests for the calibration round bookkeeping (ball_calibration_logic.h —
// design §9 test 5, the protocol-level part) and profile persistence
// (ball_calibration_store.h — design §9 test 7): round append/re-derive,
// consecutive-clean pass rule, exhaustion, save/load round-trip, schema
// version rejection, corrupt-file rejection.

#include "../ball_calibration_logic.h"
#include "../ball_calibration_store.h"

#include <cstdio>
#include <opencv2/imgproc.hpp>

using namespace pinpoint::ballcal;

static int g_fail = 0;

#define CHECK(label, cond)                                                    \
    do {                                                                      \
        const bool ok = (cond);                                               \
        std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", label);              \
        if (!ok) ++g_fail;                                                    \
    } while (0)

#define CHECK_NEAR(label, got, want, tol)                                     \
    do {                                                                      \
        const double g = (double)(got), w = (double)(want), t = (double)(tol);\
        const bool ok = std::abs(g - w) <= t;                                 \
        std::printf("  [%s] %-44s got %.4f  want %.4f±%.4f\n",                \
                    ok ? "PASS" : "FAIL", label, g, w, t);                    \
        if (!ok) ++g_fail;                                                    \
    } while (0)

// ── CalibSession (round bookkeeping) ────────────────────────────────────────

static void testSession()
{
    std::printf("calibration session bookkeeping:\n");

    CalibSession s;
    s.seed({0.80, 0.85, 0.90}, {0.20, 0.25, 0.30});
    CHECK("seed derives a passing threshold", s.current.pass);
    CHECK("not passed before any rounds", !s.passed());

    // Round 1 clean.
    s.addRound(true, true, {0.22, 0.28}, {0.82, 0.88});
    CHECK("1 clean round: not yet passed", !s.passed() && s.cleanRounds == 1);

    // Round 2 clean → passed.
    s.addRound(true, true, {0.21}, {0.84});
    CHECK("2 clean rounds: passed", s.passed());

    // A failed round resets the consecutive count but keeps the evidence.
    CalibSession f;
    f.seed({0.80, 0.85}, {0.20});
    f.addRound(true, true, {0.22}, {0.83});
    CHECK("clean round counted", f.cleanRounds == 1);
    f.addRound(false, true, {0.78}, {0.81});   // false positive while removed!
    CHECK("failed round resets clean count", f.cleanRounds == 0);
    CHECK("failed round's empty score moved maxEmpty",
          f.current.margin < 0.10);
    CHECK("threshold no longer passes", !f.current.pass);

    // Exhaustion after kMaxFailedRounds.
    f.addRound(false, true, {0.79}, {0.80});
    f.addRound(true, false, {}, {});
    CHECK("exhausted after 3 failed rounds", f.exhausted());

    // Robustness meter mapping.
    CalibSession r;
    r.seed({0.90}, {0.50});
    CHECK_NEAR("robustness 0.4 margin → 1.0", r.robustness(), 1.0, 1e-9);
    r.seed({0.70}, {0.50});
    CHECK_NEAR("robustness 0.2 margin → 0.5", r.robustness(), 0.5, 1e-9);
    r.seed({0.50}, {0.55});
    CHECK_NEAR("robustness negative margin → 0", r.robustness(), 0.0, 1e-9);
}

// ── Profile persistence ─────────────────────────────────────────────────────

static BallCalProfile syntheticProfile()
{
    BallCalProfile p;
    cv::RNG rng(7);
    for (int i = 0; i < 6; ++i) {
        cv::Mat f(120, 160, CV_8UC3);
        rng.fill(f, cv::RNG::NORMAL, 80, 3);
        p.background.accumulate(f);
    }
    p.background.finalize();

    p.ball.valid       = true;
    p.ball.radiusPx    = 21.5;
    p.ball.radiusSigma = 3.2;
    p.ball.calibCenter = {80.f, 60.f};
    p.ball.colourMean  = {180.f, 182.f, 179.f};
    p.ball.colourCovInv = cv::Matx33f::eye();
    p.ball.template8u  = cv::Mat(65, 65, CV_8U, cv::Scalar(150));
    p.ball.minContrast = 42.0;

    p.theta = 0.55; p.margin = 0.31;
    p.roiX = 0.25; p.roiY = 0.30; p.roiW = 0.40; p.roiH = 0.35;
    p.roiPxW = 160; p.roiPxH = 120;
    p.calibratedAtMs = 1781234567890LL;
    p.valid = true;
    return p;
}

static void testStore()
{
    std::printf("profile persistence:\n");

    const std::string path = "/tmp/pp_ballcal_test_profile.yml.gz";
    const BallCalProfile in = syntheticProfile();

    CHECK("save succeeds", saveProfile(path, in));

    BallCalProfile out;
    CHECK("load succeeds", loadProfile(path, out));
    CHECK("loaded valid", out.valid);
    CHECK_NEAR("theta round-trips", out.theta, in.theta, 1e-9);
    CHECK_NEAR("margin round-trips", out.margin, in.margin, 1e-9);
    CHECK_NEAR("radius round-trips", out.ball.radiusPx, in.ball.radiusPx, 1e-9);
    CHECK_NEAR("roi round-trips", out.roiW, in.roiW, 1e-9);
    CHECK("epoch ms round-trips (int64)", out.calibratedAtMs == in.calibratedAtMs);
    CHECK("background mats round-trip",
          out.background.meanGray.size() == in.background.meanGray.size()
          && cv::norm(out.background.meanGray, in.background.meanGray, cv::NORM_INF) < 1e-5
          && cv::norm(out.background.sigma, in.background.sigma, cv::NORM_INF) < 1e-5);
    CHECK("template round-trips",
          cv::norm(out.ball.template8u, in.ball.template8u, cv::NORM_INF) == 0);
    CHECK_NEAR("colour mean round-trips", out.ball.colourMean[0], 180.0, 1e-4);

    // The loaded profile must drive the real detector path identically.
    cv::Mat probe(120, 160, CV_8UC3, cv::Scalar(80, 80, 80));
    const Detection dIn  = detect(probe, in.background, in.ball, in.theta);
    const Detection dOut = detect(probe, out.background, out.ball, out.theta);
    CHECK("loaded profile detects identically", dIn.found == dOut.found
          && std::abs(dIn.score - dOut.score) < 1e-5);

    // Invalid profile refuses to save.
    BallCalProfile invalid;
    CHECK("invalid profile refuses save", !saveProfile(path + ".x", invalid));

    // Schema version mismatch rejected.
    {
        cv::FileStorage fs(path, cv::FileStorage::WRITE);
        fs << "schemaVersion" << 999 << "theta" << 0.5;
    }
    BallCalProfile rej;
    CHECK("future schema rejected", !loadProfile(path, rej));

    // Corrupt / missing file rejected.
    CHECK("missing file rejected", !loadProfile("/tmp/pp_ballcal_nope.yml.gz", rej));

    std::remove(path.c_str());
}

int main()
{
    testSession();
    testStore();

    std::printf("%s (%d failure%s)\n", g_fail == 0 ? "ALL PASS" : "FAILURES",
                g_fail, g_fail == 1 ? "" : "s");
    return g_fail == 0 ? 0 : 1;
}
