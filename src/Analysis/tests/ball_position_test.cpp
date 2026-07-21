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

// Standalone test for ball position at address (src/Analysis/ball_position.{h,cpp}):
// the heel-line projection, the order-independence of the median lock (the property
// ball_anchor's cluster gate was written to get), the ball-diameter px→mm ruler, and
// the refusal contract. Synthetic tracks only, no fixture. Mirrors
// foot_metrics_test.cpp's structure/style.

#include "../ball_position.h"
#include "../../Core/pp_physical_constants.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace pinpoint::analysis;

static int g_fail = 0;

#define CHECK(label, cond)                                        \
    do {                                                          \
        const bool ok = (cond);                                   \
        std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", label);  \
        if (!ok) ++g_fail;                                        \
    } while (0)

static bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

constexpr int kW = 1280, kH = 720;
constexpr int64_t kAddrUs = 5'000'000;

// A stationary ball at (xPx, yPx) over `n` frames straddling address.
static BallTrack2D stillBall(double xPx, double yPx, int n = 10, double radiusPx = 9.5)
{
    BallTrack2D b;
    for (int i = 0; i < n; ++i) {
        BallSample2D s;
        s.t_us       = kAddrUs + int64_t(i - n / 2) * 10'000;   // ±50 ms — inside the window
        s.found      = true;
        s.center     = QPointF(xPx / kW, yPx / kH);
        s.radiusNorm = float(radiusPx / kW);
        s.conf       = 0.9f;
        b.frames.push_back(s);
    }
    return b;
}

int main()
{
    std::printf("=== ball position ===\n");

    // Feet 500 px apart, horizontal, lead on the left (as a right-hander appears
    // face-on). Ball a quarter of the way along = 25 %.
    const QPointF lead(400.0, 600.0), trail(900.0, 600.0);

    // ------------------------------------------------------- the projection
    {
        const BallPositionResult r =
            computeBallPosition(stillBall(525.0, 640.0), lead, trail, kAddrUs, kW, kH);
        CHECK("valid on a clean still ball", r.valid);
        CHECK("frac == 0.25",                near(r.fracOfStance, 0.25, 1e-9));
        CHECK("centre recovered",            near(r.addressBallPx.x(), 525.0, 1e-6));
        CHECK("all samples accepted",        r.samples == 10);

        // The projection is onto the heel LINE, so the ball's perpendicular
        // offset (it sits on the ground, below the heels) must not affect it.
        const BallPositionResult high =
            computeBallPosition(stillBall(525.0, 500.0), lead, trail, kAddrUs, kW, kH);
        CHECK("perpendicular offset does not change frac",
              near(high.fracOfStance, 0.25, 1e-9));

        // Midpoint and the two endpoints.
        CHECK("ball at lead heel ⇒ 0",
              near(computeBallPosition(stillBall(400.0, 640.0), lead, trail, kAddrUs, kW, kH)
                       .fracOfStance, 0.0, 1e-9));
        CHECK("ball at trail heel ⇒ 1",
              near(computeBallPosition(stillBall(900.0, 640.0), lead, trail, kAddrUs, kW, kH)
                       .fracOfStance, 1.0, 1e-9));
        CHECK("ball at stance centre ⇒ 0.5",
              near(computeBallPosition(stillBall(650.0, 640.0), lead, trail, kAddrUs, kW, kH)
                       .fracOfStance, 0.5, 1e-9));

        // Forward of the lead heel is a REAL driver setup, not an error — the
        // value must come back negative, unclamped.
        const BallPositionResult fwd =
            computeBallPosition(stillBall(350.0, 640.0), lead, trail, kAddrUs, kW, kH);
        CHECK("forward of the lead heel ⇒ negative, unclamped",
              fwd.valid && near(fwd.fracOfStance, -0.1, 1e-9));
    }

    // A tilted heel line (camera not perfectly square) still projects correctly.
    {
        const QPointF tLead(400.0, 620.0), tTrail(900.0, 580.0);
        BallTrack2D b = stillBall(650.0, 600.0);   // exactly the midpoint of that line
        const BallPositionResult r = computeBallPosition(b, tLead, tTrail, kAddrUs, kW, kH);
        CHECK("tilted heel line ⇒ midpoint is still 0.5", near(r.fracOfStance, 0.5, 1e-9));
    }

    // ------------------------------------------------ mis-lock / order independence
    {
        // Three detector warm-up mis-locks FIRST (on the clubhead, well above the
        // ball), then seven good frames. A first-accepted chain gate would lock
        // onto the mis-locks and reject every good sample; the component-wise
        // median must not. This is the 2026-07-04 corpus failure in miniature.
        BallTrack2D b;
        const auto push = [&](int64_t t, double x, double y) {
            BallSample2D s;
            s.t_us = t; s.found = true;
            s.center = QPointF(x / kW, y / kH);
            s.radiusNorm = float(9.5 / kW); s.conf = 0.9f;
            b.frames.push_back(s);
        };
        for (int i = 0; i < 3; ++i) push(kAddrUs - 50'000 + i * 10'000, 525.0, 450.0);  // mis-lock
        for (int i = 0; i < 7; ++i) push(kAddrUs - 20'000 + i * 10'000, 525.0, 640.0);  // truth

        const BallPositionResult r = computeBallPosition(b, lead, trail, kAddrUs, kW, kH);
        CHECK("median survives leading mis-locks", r.valid);
        CHECK("mis-locks excluded from the cluster", r.samples == 7);
        CHECK("centre is the truth cluster", near(r.addressBallPx.y(), 640.0, 1e-6));
    }

    // ------------------------------------------------------------- the ruler
    {
        // mmPerPx = 42.67 / (2 · r). At r = 9.5 px that is ~2.246 mm/px, which
        // makes the 500 px stance ~1123 mm — implausibly wide, which is exactly
        // the point of the "±1 px of radius is ~10 % of scale" warning.
        const BallPositionResult r =
            computeBallPosition(stillBall(525.0, 640.0, 10, /*radiusPx*/ 9.5), lead, trail,
                                kAddrUs, kW, kH);
        const double expect = pinpoint::physical::kGolfBallDiameterMm / (2.0 * 9.5);
        // Tolerance is 1e-6, not exact: radiusNorm is a FLOAT and 9.5/1280 is not
        // exactly representable in binary, so the normalize/de-normalize round
        // trip loses ~1.2e-7 px (~3e-8 mm/px here). That is six orders of
        // magnitude below the ruler's real error — a ±1 px radius error is ~10 %
        // of scale — so tightening this would be testing IEEE-754, not the ruler.
        CHECK("ruler matches the ball-diameter formula", near(r.mmPerPx, expect, 1e-6));

        // A bigger ball on screen ⇒ finer scale.
        const BallPositionResult big =
            computeBallPosition(stillBall(525.0, 640.0, 10, 19.0), lead, trail, kAddrUs, kW, kH);
        CHECK("larger radius ⇒ smaller mmPerPx", big.mmPerPx < r.mmPerPx);

        // No radius reported ⇒ no ruler, but the POSITION still resolves. The two
        // outputs are deliberately independent.
        BallTrack2D noR = stillBall(525.0, 640.0);
        for (BallSample2D &s : noR.frames) s.radiusNorm = 0.f;
        const BallPositionResult nr = computeBallPosition(noR, lead, trail, kAddrUs, kW, kH);
        CHECK("no radius ⇒ ruler unresolved", nr.mmPerPx <= 0.0);
        CHECK("no radius ⇒ position still valid", nr.valid);

        // Degenerate heels ⇒ no position, but the ruler still resolves (the case
        // foot_metrics relies on to put stance width in mm when feet are shaky).
        const BallPositionResult deg =
            computeBallPosition(stillBall(525.0, 640.0), lead, lead, kAddrUs, kW, kH);
        CHECK("degenerate heel pair ⇒ invalid position", !deg.valid);
        CHECK("degenerate heel pair ⇒ ruler survives", deg.mmPerPx > 0.0);
    }

    // ------------------------------------------------------------ refusal gates
    {
        BallPositionConfig off;
        off.enabled = false;
        CHECK("disabled ⇒ invalid",
              !computeBallPosition(stillBall(525.0, 640.0), lead, trail, kAddrUs, kW, kH, off).valid);

        CHECK("empty ball track ⇒ invalid",
              !computeBallPosition(BallTrack2D{}, lead, trail, kAddrUs, kW, kH).valid);

        CHECK("too few samples ⇒ invalid",
              !computeBallPosition(stillBall(525.0, 640.0, /*n*/ 2), lead, trail,
                                   kAddrUs, kW, kH).valid);

        // `found == false` frames carry a timestamp but no lock — they must not count.
        BallTrack2D lost = stillBall(525.0, 640.0);
        for (BallSample2D &s : lost.frames) s.found = false;
        CHECK("no found samples ⇒ invalid",
              !computeBallPosition(lost, lead, trail, kAddrUs, kW, kH).valid);

        // Implausible position ⇒ detector failure, refuse rather than publish.
        CHECK("ball far outside the stance ⇒ invalid",
              !computeBallPosition(stillBall(1200.0, 640.0), lead, trail, kAddrUs, kW, kH).valid);

        CHECK("degenerate frame dims ⇒ invalid",
              !computeBallPosition(stillBall(525.0, 640.0), lead, trail, kAddrUs, 0, 0).valid);

        // Samples outside the address window are ignored.
        BallTrack2D late = stillBall(525.0, 640.0);
        for (BallSample2D &s : late.frames) s.t_us += 5'000'000;   // 5 s after address
        CHECK("samples outside the address window ⇒ invalid",
              !computeBallPosition(late, lead, trail, kAddrUs, kW, kH).valid);
    }

    // -------------------------------------------------- no-Address fallback path
    {
        // With no Address event (seg.conf == 0), fall back to everything before
        // launch — the ball is stationary until it is struck, so that whole span
        // is one address measurement.
        BallTrack2D b = stillBall(525.0, 640.0);
        b.launchTUs = kAddrUs + 1'000'000;
        const BallPositionResult r = computeBallPosition(b, lead, trail, /*addressUs*/ -1, kW, kH);
        CHECK("addressUs < 0 falls back to pre-launch samples", r.valid);
        CHECK("fallback frac is still 0.25", near(r.fracOfStance, 0.25, 1e-9));

        // Post-launch samples (the ball in flight) must be excluded by that fallback.
        BallTrack2D flying = b;
        for (int i = 0; i < 20; ++i) {
            BallSample2D s;
            s.t_us = b.launchTUs + int64_t(i + 1) * 10'000;
            s.found = true;
            s.center = QPointF((525.0 + i * 30.0) / kW, 300.0 / kH);
            s.radiusNorm = float(9.5 / kW); s.conf = 0.9f;
            flying.frames.push_back(s);
        }
        const BallPositionResult fr =
            computeBallPosition(flying, lead, trail, /*addressUs*/ -1, kW, kH);
        CHECK("in-flight samples excluded by the launch bound",
              fr.valid && near(fr.fracOfStance, 0.25, 1e-9));
    }

    // ---------------------------------------------------------- tuning plumbing
    {
        QVariantMap ov;
        ov[QStringLiteral("ballpos.enabled")]      = false;
        ov[QStringLiteral("ballpos.addrWindowUs")] = qint64(500000);
        ov[QStringLiteral("ballpos.minSamples")]   = 7;
        ov[QStringLiteral("ballpos.maxJumpPx")]    = 12.5;
        ov[QStringLiteral("ballpos.fracLo")]       = -0.25;
        ov[QStringLiteral("ballpos.fracHi")]       = 1.25;
        const BallPositionConfig c = BallPositionConfig::fromOverrides(ov);
        CHECK("ballpos.enabled override",      c.enabled == false);
        CHECK("ballpos.addrWindowUs override", c.addrWindowUs == 500000);
        CHECK("ballpos.minSamples override",   c.minSamples == 7);
        CHECK("ballpos.maxJumpPx override",    near(c.maxJumpPx, 12.5, 1e-12));
        CHECK("ballpos.fracLo override",       near(c.fracLo, -0.25, 1e-12));
        CHECK("ballpos.fracHi override",       near(c.fracHi, 1.25, 1e-12));
    }

    std::printf(g_fail ? "=== FAILED (%d) ===\n" : "=== OK ===\n", g_fail);
    return g_fail ? 1 : 0;
}
