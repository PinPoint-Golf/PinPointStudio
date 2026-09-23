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


// Standalone tests for the down-the-line posture producer (src/Analysis/dtl_posture.h).
// One synthetic golfer seen from down the line, ball image-right: hips that TURN (the two hip
// joints swap depth, the midpoint stays) and then THRUST 30 px, a trunk that loses 9° of bend,
// a trail knee that straightens at the top, and a lead wrist that rises 400 px up the inside and
// comes down `loopPx` outside that path. Qt-only, no fixture.
//
//   cmake --build build/tests --target dtl_posture_test
//   ctest --test-dir build/tests -R dtl_posture_test --output-on-failure

#include "../dtl_posture.h"

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

constexpr int kW = 512, kH = 1024;
constexpr int64_t kAddr = 500000, kTop = 1300000, kP5 = 1450000, kImpact = 1550000, kEnd = 1900000;

static std::vector<PhaseEvent> phases()
{
    auto ev = [](Phase p, int64_t t) { PhaseEvent e; e.phase = p; e.t_us = t; e.conf = 1.f; return e; };
    return { ev(Phase::Address, kAddr), ev(Phase::Top, kTop), ev(Phase::ArmParallelDown, kP5),
             ev(Phase::Impact, kImpact) };
}
static double smooth(double t, double a, double b)
{ const double u = std::clamp((t - a) / (b - a), 0.0, 1.0); return u * u * (3 - 2 * u); }

// mirror = the same golfer facing image-LEFT.
static PoseTrack2D makePose(bool mirror, double thrustPx = 30.0, float toeConf = 0.9f, double loopPx = 60.0)
{
    PoseTrack2D p;
    auto X = [mirror](double x) { return mirror ? kW - x : x; };
    for (int64_t t = 0; t <= kEnd; t += 5000) {
        PoseFrame2D f;
        f.t_us = t;
        f.conf.fill(0.9f);
        const double thrust = thrustPx * smooth(double(t), kTop, kImpact);
        const double turn   = 25.0 * std::sin(M_PI * smooth(double(t), kAddr, kImpact));   // hips swap depth
        const double bend   = (33.0 - 9.0 * smooth(double(t), kP5, kImpact)) * M_PI / 180.0;
        const double hipX = 200 + thrust, hipY = 560;
        auto set = [&](int k, double x, double y) { f.kp[size_t(k)] = QPointF(X(x) / kW, y / kH); };
        set(11, hipX - turn, hipY); set(12, hipX + turn, hipY);
        const double shX = hipX + 250 * std::sin(bend), shY = hipY - 250 * std::cos(bend);
        set(5, shX - 0.5 * turn, shY); set(6, shX + 0.5 * turn, shY);
        set(0, shX + 30, shY - 60);
        // trail (right) knee: 18° at address, straight at the top, 30° at impact
        const double trailFlex = (t < kTop ? 18.0 * (1 - smooth(double(t), kAddr, kTop))
                                           : 30.0 * smooth(double(t), kTop, kImpact)) * M_PI / 180.0;
        const double leadFlex = 20.0 * M_PI / 180.0;
        auto leg = [&](int hip, int knee, int ank, double flex) {
            const QPointF h(f.kp[size_t(hip)].x() * kW, hipY);
            const double hx = mirror ? kW - h.x() : h.x();
            const double kx = hx + 110 * std::sin(flex * 0.5), ky = hipY + 110 * std::cos(flex * 0.5);
            set(knee, kx, ky);
            set(ank, kx - 110 * std::sin(flex * 0.5), ky + 110 * std::cos(flex * 0.5));
        };
        leg(11, 13, 15, leadFlex); leg(12, 14, 16, trailFlex);
        set(17, 260, 800); set(20, 262, 800); set(19, 170, 800); set(22, 172, 800);   // toes ball-side of heels
        f.conf[17] = f.conf[20] = toeConf;
        // lead wrist: up a straight line from (300,620) to (230,220), then down the same heights
        // `loopPx` toward the ball, converging only in the last third before impact.
        auto lineX = [](double y) { return 300.0 - 70.0 * (620.0 - y) / 400.0; };
        double wy, wx;
        if (t <= kTop) { wy = 620.0 - 400.0 * smooth(double(t), kAddr, kTop); wx = lineX(wy); }
        else {
            const double v = smooth(double(t), kTop, kImpact);
            wy = 220.0 + 400.0 * v;
            wx = lineX(wy) + loopPx * std::min(1.0, 3.0 * (1.0 - v));
        }
        set(9, wx, wy);
        p.frames.push_back(f);
    }
    return p;
}
static PoseTrack2D makeFo()
{
    PoseTrack2D p;
    for (int64_t t = 0; t <= kEnd; t += 5000) {
        PoseFrame2D f; f.t_us = t; f.conf.fill(0.9f);
        f.kp[5] = QPointF(0.40, 0.30); f.kp[6] = QPointF(0.52, 0.30);          // 153.6 px shoulder width @1280
        f.kp[0] = QPointF(0.46, 0.22); f.kp[15] = QPointF(0.42, 0.80); f.kp[16] = QPointF(0.50, 0.80);
        p.frames.push_back(f);
    }
    return p;
}
static const MetricSeries *find(const DtlPostureResult &r, const char *key)
{
    for (const MetricSeries &m : r.series) if (m.key == QLatin1String(key)) return &m;
    return nullptr;
}
static double atPhase(const MetricSeries *m, Phase p)
{
    if (m) for (const PhaseSample &s : m->phaseSamples) if (s.phase == p) return s.value;
    return std::nan("");
}

int main()
{
    std::printf("dtl_posture_test\n");
    const std::vector<PhaseEvent> ph = phases();
    const PoseTrack2D fo = makeFo();
    DtlPostureConfig cfg;

    auto inputs = [&](const PoseTrack2D &d, bool mirror) {
        DtlPostureInputs in;
        in.poseDtl = &d; in.dtlW = kW; in.dtlH = kH;
        in.poseFo = &fo; in.foW = 1280; in.foH = 1024;
        in.phases = &ph; in.leadIsLeft = true;
        in.ballFound = true; in.ballBright = true; in.ballRadiusPx = 7.5;
        in.ballX = mirror ? kW - 470.0 : 470.0; in.ballY = 810;
        return in;
    };

    // §1 the ball image-right
    {
        const PoseTrack2D d = makePose(false);
        const DtlPostureResult r = buildDtlPosture(inputs(d, false), cfg);
        const double cmpp = 4.267 / 15.0;
        check(r.valid && r.toward > 0 && r.ruler == QLatin1String("dtlBall") && near(r.cmPerPx, cmpp, 1e-9),
              "§1 valid, ball image-right, ruler = the DTL ball");
        const MetricSeries *th = find(r, "pelvisThrust");
        check(th && near(atPhase(th, Phase::Impact), 30.0 * cmpp, 0.05), "§1 thrust at impact = 30 px in cm");
        check(th && near(atPhase(th, Phase::Top), 0.0, 0.05), "§1 a TURN is not a thrust: 0 at the top with the hips 25 px apart in depth");
        const MetricSeries *b = find(r, "spineForwardBend");
        check(b && near(atPhase(b, Phase::Address), 33.0, 0.1) && near(atPhase(b, Phase::Impact), 24.0, 0.1),
              "§1 forward bend 33° at address, 24° at impact");
        const MetricSeries *kt = find(r, "trailKneeFlexion"), *kl = find(r, "leadKneeFlexion");
        check(kt && near(atPhase(kt, Phase::Address), 18.0, 0.2) && near(atPhase(kt, Phase::Top), 0.0, 0.2)
              && near(atPhase(kt, Phase::Impact), 30.0, 0.2), "§1 trail knee 18° → 0° → 30°");
        check(kl && near(atPhase(kl, Phase::Address), 20.0, 0.2), "§1 lead knee 20°");
        // Past impact the thrust series is marked invalid, not reduced.
        bool tailMasked = th && !th->valid.empty();
        if (tailMasked) for (size_t i = 0; i < th->t_us.size(); ++i)
            if (th->t_us[i] > kImpact + 10000 && th->valid[i]) tailMasked = false;
        check(tailMasked, "§1 thrust is masked past impact");
        const MetricSeries *gap = find(r, "ballBodyDistance"), *bal = find(r, "balanceHeelToe");
        const double ratio = r.scaleRatio;                         // DTL px per face-on px
        check(gap && ratio > 0 && near(atPhase(gap, Phase::Address), 100.0 * (470.0 - 262.0) / (153.6 * ratio), 0.2),
              "§1 ball reach = ball to the ball-most toe, over face-on's shoulder width carried across");
        check(bal && atPhase(bal, Phase::Address) > 0 && atPhase(bal, Phase::Address) < 120, "§1 balance proxy emitted");
        check(near(atPhase(find(r, "handPathLoop"), Phase::Top), 15.0, 0.3),
              "§1 hands down 60 px outside a 400 px rise ⇒ loop +15 %");
    }

    // §1b the loop's sign is the hands' side of the backswing path, and needs no ruler.
    {
        const PoseTrack2D in_ = makePose(false, 30.0, 0.9f, -40.0);
        DtlPostureInputs in = inputs(in_, false);
        in.ballBright = false;                              // no ruler at all
        check(near(atPhase(find(buildDtlPosture(in, cfg), "handPathLoop"), Phase::Top), -10.0, 0.3),
              "§1b hands down 40 px INSIDE ⇒ loop −10 %, with no ruler");
        const PoseTrack2D m = makePose(true, 30.0, 0.9f, -40.0);
        check(near(atPhase(find(buildDtlPosture(inputs(m, true), cfg), "handPathLoop"), Phase::Top), -10.0, 0.3),
              "§1b mirrored: the same −10 %");
    }

    // §2 the same golfer facing image-LEFT reads the same numbers: the sign comes from the feet.
    {
        const PoseTrack2D d = makePose(true);
        const DtlPostureResult r = buildDtlPosture(inputs(d, true), cfg);
        check(r.valid && r.toward < 0, "§2 mirrored: ball image-left");
        check(near(atPhase(find(r, "pelvisThrust"), Phase::Impact), 30.0 * 4.267 / 15.0, 0.05), "§2 mirrored thrust identical");
        check(near(atPhase(find(r, "spineForwardBend"), Phase::Address), 33.0, 0.1), "§2 mirrored bend identical");
    }

    // §3 refusals
    {
        const PoseTrack2D d = makePose(false);
        DtlPostureInputs in = inputs(d, false);
        in.ballX = 40.0;                                   // ball behind the golfer's heels
        check(!buildDtlPosture(in, cfg).valid, "§3 ball and feet disagree ⇒ refuse everything");
        const PoseTrack2D blind = makePose(false, 30.0, 0.05f);
        check(!buildDtlPosture(inputs(blind, false), cfg).valid, "§3 toes unseen at address ⇒ refuse (no sign)");
        DtlPostureConfig off; off.enabled = false;
        check(!buildDtlPosture(inputs(d, false), off).valid, "§3 disabled ⇒ nothing");
    }

    // §4 the ruler: a shadow-cue ball is not one; face-on's is carried across; none ⇒ no thrust,
    // and the angles survive.
    {
        const PoseTrack2D d = makePose(false);
        DtlPostureInputs in = inputs(d, false);
        in.ballBright = false; in.foMmPerPx = 3.0;
        const DtlPostureResult r = buildDtlPosture(in, cfg);
        check(r.ruler == QLatin1String("faceOnBall") && near(r.cmPerPx, 0.3 / r.scaleRatio, 1e-9), "§4 shadow ball ⇒ face-on ruler / ratio");
        in.foMmPerPx = 4.5;                                // implies ~72 cm of shoulders: not a ruler
        const DtlPostureResult bad = buildDtlPosture(in, cfg);
        check(bad.valid && bad.ruler == QLatin1String("none") && !bad.rulerRefused.isEmpty() && !find(bad, "pelvisThrust"),
              "§4 a ruler that measures the golfer implausibly is dropped, and says so");
        in.foMmPerPx = 0.0;
        const DtlPostureResult n = buildDtlPosture(in, cfg);
        check(n.valid && !find(n, "pelvisThrust") && find(n, "spineForwardBend") && find(n, "trailKneeFlexion"),
              "§4 no ruler ⇒ no pelvisThrust, the angles still produced");
    }

    std::printf(g_fail ? "FAILED (%d)\n" : "ALL PASS\n", g_fail);
    return g_fail ? 1 : 0;
}
