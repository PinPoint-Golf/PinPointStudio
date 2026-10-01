/*
 * PinPoint Studio — tests for the down-the-line shaft lie
 *
 * Copyright (C) 2026 Mark Liversedge
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 */

// Standalone tests for the down-the-line shaft lie producer (src/Analysis/dtl_shaft_lie.h).
// One synthetic DTL club track at 120 fps: 60° at address, a Held gap, 90° near P3, 66° at
// impact (steeper by 6°), and a mirrored twin facing the other way that must read the same.
//
//   cmake --build build/tests --target dtl_shaft_lie_test
//   ctest --test-dir build/tests -R dtl_shaft_lie_test --output-on-failure

#include "../dtl_shaft_lie.h"

#include <cmath>
#include <cstdio>

using namespace pinpoint::analysis;

static int g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}
static bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

constexpr int64_t kFrame = 8333;                       // 120 fps
constexpr int64_t kAddr = 400000, kTop = 1200000, kImpact = 1500000, kEnd = 1800000;

static std::vector<PhaseEvent> phases(bool withImpact = true)
{
    auto ev = [](Phase p, int64_t t) { PhaseEvent e; e.phase = p; e.t_us = t; e.conf = 1.f; return e; };
    std::vector<PhaseEvent> v{ ev(Phase::Address, kAddr), ev(Phase::Top, kTop) };
    if (withImpact) v.push_back(ev(Phase::Impact, kImpact));
    return v;
}

// The shaft's angle to the ground through the swing, degrees; `mirror` flips which way the
// head points along the image's x so the fold has to undo it.
static double lieAt(int64_t t)
{
    if (t < kAddr + 150000) return 60.0;                         // the address hold
    if (t < kTop)           return 60.0 + 30.0 * double(t - kAddr - 150000) / double(kTop - kAddr - 150000);
    if (t < kImpact)        return 90.0 - 24.0 * double(t - kTop) / double(kImpact - kTop);   // → 66 at impact
    return 66.0;
}

static DtlShaftTrack2D makeTrack(bool mirror, int64_t gapFrom = -1, int64_t gapTo = -1,
                                 bool dropImpactBand = false, int64_t clockOffsetUs = 0)
{
    DtlShaftTrack2D tr;
    tr.valid = true;
    tr.clockOffsetUs = clockOffsetUs;
    for (int64_t t = 0; t <= kEnd; t += kFrame) {
        DtlSample s;
        s.t_us = t + clockOffsetUs;
        const double lie = lieAt(t) * M_PI / 180.0;
        // grip→head: down the image (y positive) and toward the ball, image-right or image-left.
        const double dx = std::cos(lie) * (mirror ? -1.0 : 1.0), dy = std::sin(lie);
        s.thetaRad = std::atan2(dy, dx);
        s.tier = DtlTier::Band;
        if (gapFrom >= 0 && t >= gapFrom && t <= gapTo) s.tier = DtlTier::Held;   // finite θ, not measured
        if (dropImpactBand && t >= kImpact - 60000) s.tier = DtlTier::Unseen;
        tr.samples.push_back(s);
    }
    return tr;
}

static double phaseValue(const MetricSeries &m, Phase p, bool *found)
{
    for (const PhaseSample &ps : m.phaseSamples) if (ps.phase == p) { *found = true; return ps.value; }
    *found = false;
    return 0.0;
}

int main()
{
    std::printf("=== the fold ===\n");
    check(near(dtlShaftLieDeg(60.0 * M_PI / 180.0), 60.0, 1e-9), "60° reads 60°");
    check(near(dtlShaftLieDeg(120.0 * M_PI / 180.0), 60.0, 1e-9), "120° (the mirrored golfer) reads 60°");
    check(near(dtlShaftLieDeg(-60.0 * M_PI / 180.0), 60.0, 1e-9), "−60° (head→grip) reads 60°");
    check(near(dtlShaftLieDeg(M_PI), 0.0, 1e-9), "flat along the ground reads 0°");
    check(near(dtlShaftLieDeg(M_PI / 2), 90.0, 1e-9), "straight up reads 90°");

    std::printf("=== the two readings and the delta ===\n");
    {
        const DtlShaftLieResult r = buildDtlShaftLie(makeTrack(false), phases());
        check(r.valid, "a measured track produces the series");
        check(r.series.key == QLatin1String("shaftLie"), "key is shaftLie");
        bool fa = false, fi = false;
        const double a = phaseValue(r.series, Phase::Address, &fa), i = phaseValue(r.series, Phase::Impact, &fi);
        check(fa && near(a, 60.0, 0.05), "address reads 60°");
        check(fi && near(i, 66.0, 0.3), "impact reads 66°");
        check(fi && fa && i - a > 5.5, "Δ impact − address is +6: steeper is positive");
        check(r.series.t_us.size() == r.series.value.size() && r.series.valid.size() == r.series.t_us.size(),
              "curve, values and the valid mask are parallel");
        check(r.nMeasured == int(r.series.t_us.size()), "every frame measured ⇒ nMeasured is the frame count");
        check(r.addressSnapUs >= 0 && r.addressSnapUs <= kFrame / 2 + 1, "address reading sits within half a frame of P1");
    }
    {
        // Facing the other way: the same numbers, no ball-side convention needed.
        const DtlShaftLieResult r = buildDtlShaftLie(makeTrack(true), phases());
        bool fa = false, fi = false;
        const double a = phaseValue(r.series, Phase::Address, &fa), i = phaseValue(r.series, Phase::Impact, &fi);
        check(fa && fi && near(a, 60.0, 0.05) && near(i, 66.0, 0.3), "the mirrored golfer reads the same two numbers");
    }

    std::printf("=== only measured tiers are readings ===\n");
    {
        // A Held run over the address instant: finite θ there, but no reading, and the curve is
        // bridged and flagged across it.
        const DtlShaftLieResult r = buildDtlShaftLie(makeTrack(false, kAddr - 30000, kAddr + 30000), phases());
        bool fa = false, fi = false;
        phaseValue(r.series, Phase::Address, &fa);
        phaseValue(r.series, Phase::Impact, &fi);
        check(r.valid && !fa, "a Held run over P1 leaves the address reading absent");
        check(fi, "…and the impact reading alone is still published");
        int bridged = 0;
        for (uint8_t v : r.series.valid) bridged += v == 0 ? 1 : 0;
        check(bridged >= 7 && bridged <= 8, "the Held frames are flagged valid 0");
        check(r.nMeasured == int(r.series.t_us.size()) - bridged, "nMeasured excludes them");
        for (size_t k = 0; k < r.series.value.size(); ++k)
            if (!std::isfinite(r.series.value[k])) { check(false, "no NaN in the curve"); break; }
    }
    {
        // No band at impact (the DTL ball was not found): no impact reading, nothing invented
        // from the nearest band that does exist.
        const DtlShaftLieResult r = buildDtlShaftLie(makeTrack(false, -1, -1, true), phases());
        bool fi = false;
        phaseValue(r.series, Phase::Impact, &fi);
        check(r.valid && !fi, "a missing impact band gives no impact reading");
        check(!std::isfinite(r.impactDeg) && r.impactSnapUs < 0, "…and the result says so");
    }
    {
        // Address only on the ladder: the address reading, and no impact.
        const DtlShaftLieResult r = buildDtlShaftLie(makeTrack(false), phases(false));
        bool fa = false, fi = false;
        phaseValue(r.series, Phase::Address, &fa);
        phaseValue(r.series, Phase::Impact, &fi);
        check(r.valid && fa && !fi, "no Impact on the ladder ⇒ address reading only");
    }

    std::printf("=== clocks and refusals ===\n");
    {
        // The DTL clock runs 250 ms ahead: the readings land on the face-on instants.
        const DtlShaftLieResult r = buildDtlShaftLie(makeTrack(false, -1, -1, false, 250000), phases());
        bool fa = false, fi = false;
        const double a = phaseValue(r.series, Phase::Address, &fa), i = phaseValue(r.series, Phase::Impact, &fi);
        check(fa && fi && near(a, 60.0, 0.05) && near(i, 66.0, 0.3), "clockOffsetUs is taken off before reading");
        check(!r.series.t_us.empty() && r.series.t_us.front() == 0, "the curve is on the face-on clock");
    }
    {
        DtlShaftTrack2D empty;
        check(!buildDtlShaftLie(empty, phases()).valid, "an invalid track is refused");
        DtlShaftTrack2D unseen = makeTrack(false);
        for (DtlSample &s : unseen.samples) s.tier = DtlTier::Unseen;
        check(!buildDtlShaftLie(unseen, phases()).valid, "a track with no measured frame is refused");
        std::vector<PhaseEvent> noAddr{ phases()[1], phases()[2] };
        check(!buildDtlShaftLie(makeTrack(false), noAddr).valid, "no Address on the ladder is refused");
    }

    std::printf("=== %s ===\n", g_fail == 0 ? "ALL PASS" : "FAILURES");
    return g_fail ? 1 : 0;
}
