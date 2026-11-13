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


// Standalone tests for the down-the-line posture producer (src/Body/dtl_posture.h).
// One synthetic golfer seen from down the line, ball image-right: hips that TURN (the two hip
// joints swap depth, the midpoint stays) and then THRUST 30 px, a trunk that loses 9° of bend,
// a trail knee that straightens at the top, and a lead wrist that rises 400 px up the inside and
// comes down `loopPx` outside that path. Qt-only, no fixture. §9 (plane stack D2): the arm lift at the
// top, the lead upper arm against the shoulder line, both facings and both hands.
//
//   cmake --build build/tests --target dtl_posture_test
//   ctest --test-dir build/tests -R dtl_posture_test --output-on-failure

#include "../../Body/dtl_posture.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

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
static PoseTrack2D makePose(bool mirror, double thrustPx = 30.0, float toeConf = 0.9f, double loopPx = 60.0,
                            double elbowBehindPx = 0.0, float elbowConf = 0.9f)
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
        // trail (right) wrist beside the lead one; trail elbow 100 px below it and `elbowBehindPx`
        // AWAY from the ball (image-left here), so the forearm reads atan(behind / 100) throughout.
        set(10, wx + 10, wy);
        set(8, wx + 10 - elbowBehindPx, wy + 100);
        f.conf[8] = elbowConf;
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
// The address arms (posture_metrics_design.md §3.1): over the address window, both wrists put on a
// line from the shoulder midpoint `hangDeg` off the vertical, + toward the ball, 280 px long, with the
// given confidences. Everything else in the frame is left alone.
static void setArmHang(PoseTrack2D &p, bool mirror, double hangDeg, float lWrConf = 0.9f, float rWrConf = 0.9f)
{
    const double tw = mirror ? -1.0 : 1.0, h = hangDeg * M_PI / 180.0;
    for (PoseFrame2D &f : p.frames) {
        if (std::llabs(f.t_us - kAddr) > 200000) continue;
        const double sx = 0.5 * (f.kp[5].x() + f.kp[6].x()) * kW, sy = 0.5 * (f.kp[5].y() + f.kp[6].y()) * kH;
        const QPointF w((sx + tw * 280.0 * std::sin(h)) / kW, (sy + 280.0 * std::cos(h)) / kH);
        f.kp[9] = w; f.kp[10] = w;
        f.conf[9] = lWrConf; f.conf[10] = rWrConf;
    }
}
// The head (posture_metrics_design.md §3.1), on every frame: both ears 70 px up the trunk line from
// the shoulder midpoint and the nose 40 px from them, `pitchDeg` past square to the trunk toward the
// feet (+ = chin down). Built in a ball-forward, y-up frame and turned into the image by the facing.
static void setHead(PoseTrack2D &p, bool mirror, double pitchDeg)
{
    const double tw = mirror ? -1.0 : 1.0, pr = pitchDeg * M_PI / 180.0;
    for (PoseFrame2D &f : p.frames) {
        const double sx = 0.5 * (f.kp[5].x() + f.kp[6].x()) * kW, sy = 0.5 * (f.kp[5].y() + f.kp[6].y()) * kH;
        const double hx = 0.5 * (f.kp[11].x() + f.kp[12].x()) * kW, hy = 0.5 * (f.kp[11].y() + f.kp[12].y()) * kH;
        const double beta = std::atan2(tw * (sx - hx), -(sy - hy));     // trunk lean toward the ball
        const double ux = tw * std::sin(beta), uy = -std::cos(beta);     // up the trunk, image coords
        const double ex = tw * std::cos(beta + pr), ey = std::sin(beta + pr);   // ear → nose, image coords
        const QPointF ear((sx + 70.0 * ux) / kW, (sy + 70.0 * uy) / kH);
        f.kp[3] = ear; f.kp[4] = ear;
        f.kp[0] = QPointF((sx + 70.0 * ux + 40.0 * ex) / kW, (sy + 70.0 * uy + 40.0 * ey) / kH);
    }
}
// §9 THE ARM LIFT AT THE TOP (plane stack D2): on every frame within ±20 ms of the top, the lead shoulder
// at (250, 300), the trail shoulder 100 px from it along a line rising `tiltDeg` (image up = −y), the lead
// ELBOW 80 px from the lead shoulder at `liftDeg` above that line, and the lead wrist 70 px on from the
// elbow with the forearm folded `foldDeg` further up — a bent lead arm, which a shoulder → wrist line
// would read as lifted. A left-hander's lead side is the right-hand keypoints (6, 8, 10).
static void setArmAtTop(PoseTrack2D &p, bool mirror, bool leftHanded, double tiltDeg, double liftDeg,
                        double foldDeg = 0.0, float trailConf = 0.9f)
{
    const int lS = leftHanded ? 6 : 5, tS = leftHanded ? 5 : 6, lE = leftHanded ? 8 : 7, lW = leftHanded ? 10 : 9;
    const auto X = [mirror](double x) { return mirror ? kW - x : x; };
    const double a = tiltDeg * M_PI / 180.0, b = (tiltDeg + liftDeg) * M_PI / 180.0,
                 c = (tiltDeg + liftDeg + foldDeg) * M_PI / 180.0;
    const double Lx = 250.0, Ly = 300.0, Ex = Lx + 80.0 * std::cos(b), Ey = Ly - 80.0 * std::sin(b);
    for (PoseFrame2D &f : p.frames) {
        if (std::llabs(f.t_us - kTop) > 20000) continue;
        f.kp[size_t(lS)] = QPointF(X(Lx) / kW, Ly / kH);
        f.kp[size_t(tS)] = QPointF(X(Lx + 100.0 * std::cos(a)) / kW, (Ly - 100.0 * std::sin(a)) / kH);
        f.kp[size_t(lE)] = QPointF(X(Ex) / kW, Ey / kH);
        f.kp[size_t(lW)] = QPointF(X(Ex + 70.0 * std::cos(c)) / kW, (Ey - 70.0 * std::sin(c)) / kH);
        f.conf[size_t(tS)] = trailConf;
    }
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
        in.clubLengthM = 0.72; in.clubLengthSigmaM = 0.005;   // 72 cm: the DTL ball ruler puts the ball 71.6 cm from the hands
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
        // The wrists sit at (305, 620) at address and the ball at (470, 810): the club is the ruler.
        const double lever = std::hypot(470.0 - 305.0, 810.0 - 620.0), cmPerPxClub = 72.0 / lever;
        check(gap && near(atPhase(gap, Phase::Address), (470.0 - 262.0) * cmPerPxClub, 0.01),
              "§1 ball distance = ball to the ball-most toe, in cm with the club as the ruler");
        const MetricSeries *ex = find(r, "ballReachExcess");
        check(ex && near(atPhase(ex, Phase::Address), (305.0 - 262.0) * cmPerPxClub, 0.01),
              "§1 beyond the club's reach = the distance less the club along the wrist→ball line = the hands past the toes");
        check(ex && ex->phaseSamples.front().sigma
                  && near(*ex->phaseSamples.front().sigma, atPhase(ex, Phase::Address) * 0.005 / 0.72, 1e-6),
              "§1 …with the club length's σ carried");
        check(bal && atPhase(bal, Phase::Address) > 0 && atPhase(bal, Phase::Address) < 120, "§1 balance proxy emitted");
        check(near(atPhase(find(r, "handPathLoop"), Phase::Top), 15.0, 0.3),
              "§1 hands down 60 px outside a 400 px rise ⇒ loop +15 %");
    }

    // §1c which way the trail elbow points: 0° under the hands, + behind the golfer, the sign from
    // the feet (so a mirrored golfer reads the same), and an unseen elbow is no reading.
    {
        const PoseTrack2D under = makePose(false);
        check(near(atPhase(find(buildDtlPosture(inputs(under, false), cfg), "trailForearmAngle"), Phase::Top), 0.0, 0.01),
              "§1c elbow under the hands ⇒ 0°");
        const PoseTrack2D behind = makePose(false, 30.0, 0.9f, 60.0, 100.0);
        check(near(atPhase(find(buildDtlPosture(inputs(behind, false), cfg), "trailForearmAngle"), Phase::Top), 45.0, 0.01),
              "§1c elbow 100 px behind, 100 px below ⇒ +45°");
        const PoseTrack2D tucked = makePose(false, 30.0, 0.9f, 60.0, -36.4);
        check(near(atPhase(find(buildDtlPosture(inputs(tucked, false), cfg), "trailForearmAngle"), Phase::Top),
                   -std::atan(0.364) * 180.0 / M_PI, 0.01), "§1c elbow toward the ball ⇒ negative");
        const PoseTrack2D mirrored = makePose(true, 30.0, 0.9f, 60.0, 100.0);
        check(near(atPhase(find(buildDtlPosture(inputs(mirrored, true), cfg), "trailForearmAngle"), Phase::Top), 45.0, 0.01),
              "§1c mirrored golfer ⇒ the same +45°");
        const PoseTrack2D unseen = makePose(false, 30.0, 0.9f, 60.0, 100.0, 0.05f);
        const DtlPostureResult r = buildDtlPosture(inputs(unseen, false), cfg);
        check(r.valid && find(r, "trailForearmAngle") == nullptr, "§1c elbow unseen ⇒ no reading, the rest still valid");
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

    // §5 arm hang at address (posture_metrics_design.md §3.1): an angle, so no ruler; + toward the ball
    // by the feet; one wrist is enough, neither is no reading.
    {
        PoseTrack2D d = makePose(false);
        setArmHang(d, false, 8.0);
        const DtlPostureResult r = buildDtlPosture(inputs(d, false), cfg);
        const MetricSeries *a = find(r, "armHang");
        check(a && a->unit == QStringLiteral("°") && a->phaseSamples.size() == 1
                  && a->phaseSamples.front().phase == Phase::Address && a->t_us.empty(),
              "§5 armHang is an address scalar, in degrees");
        check(near(atPhase(a, Phase::Address), 8.0, 0.01), "§5 hands 8° toward the ball ⇒ +8° (reaching)");
        PoseTrack2D m = makePose(true);
        setArmHang(m, true, 8.0);
        check(near(atPhase(find(buildDtlPosture(inputs(m, true), cfg), "armHang"), Phase::Address), 8.0, 0.01),
              "§5 mirrored golfer ⇒ the same +8°");
        PoseTrack2D t = makePose(false);
        setArmHang(t, false, -5.0);
        DtlPostureInputs noRuler = inputs(t, false);
        noRuler.ballBright = false;                               // and no ruler anywhere
        check(near(atPhase(find(buildDtlPosture(noRuler, cfg), "armHang"), Phase::Address), -5.0, 0.01),
              "§5 hands tucked 5° under the body ⇒ −5°, with no ruler");
        PoseTrack2D one = makePose(false);
        setArmHang(one, false, 8.0, 0.05f, 0.9f);                 // the lead wrist hidden
        check(near(atPhase(find(buildDtlPosture(inputs(one, false), cfg), "armHang"), Phase::Address), 8.0, 0.01),
              "§5 one wrist hidden ⇒ the other stands for the midpoint");
        PoseTrack2D none = makePose(false);
        setArmHang(none, false, 8.0, 0.05f, 0.05f);               // neither wrist confident at address
        const DtlPostureResult rn = buildDtlPosture(inputs(none, false), cfg);
        check(rn.valid && find(rn, "armHang") == nullptr && find(rn, "spineForwardBend") != nullptr,
              "§5 neither wrist confident ⇒ the frame is refused and there is no armHang; the rest still valid");
    }

    // §6 head pitch at address: the ear → nose line against square-to-the-trunk, + chin down, measured
    // against the TRUNK so the 33° of forward bend in this fixture does not move it.
    {
        PoseTrack2D d = makePose(false);
        setHead(d, false, 0.0);
        const DtlPostureResult r0 = buildDtlPosture(inputs(d, false), cfg);
        const MetricSeries *h = find(r0, "headPitch");
        check(h && h->unit == QStringLiteral("°") && h->phaseSamples.size() == 1
                  && h->phaseSamples.front().phase == Phase::Address,
              "§6 headPitch is an address scalar, in degrees");
        check(near(atPhase(h, Phase::Address), 0.0, 0.01), "§6 head square to a 33° trunk ⇒ 0°");
        PoseTrack2D down = makePose(false);
        setHead(down, false, 20.0);
        check(near(atPhase(find(buildDtlPosture(inputs(down, false), cfg), "headPitch"), Phase::Address), 20.0, 0.01),
              "§6 chin 20° down toward the chest ⇒ +20°");
        PoseTrack2D up = makePose(false);
        setHead(up, false, -15.0);
        check(near(atPhase(find(buildDtlPosture(inputs(up, false), cfg), "headPitch"), Phase::Address), -15.0, 0.01),
              "§6 chin 15° up ⇒ −15°");
        PoseTrack2D m = makePose(true);
        setHead(m, true, 20.0);
        check(near(atPhase(find(buildDtlPosture(inputs(m, true), cfg), "headPitch"), Phase::Address), 20.0, 0.01),
              "§6 mirrored golfer ⇒ the same +20°");
        PoseTrack2D blind = makePose(false);
        setHead(blind, false, 20.0);
        for (PoseFrame2D &f : blind.frames) { f.conf[3] = 0.05f; f.conf[4] = 0.05f; }
        const DtlPostureResult rb = buildDtlPosture(inputs(blind, false), cfg);
        check(rb.valid && find(rb, "headPitch") == nullptr, "§6 no ear seen ⇒ no headPitch, the rest still valid");
    }

    // §7 head rise (posture_metrics_design.md §4.5): the EAR MIDPOINT's travel up from address, in cm.
    // The two ears part as the head turns — one up, one down by the same amount — and that is not a
    // rise; the midpoint climbing is. No ruler ⇒ no headRise, as for the thrust.
    {
        auto setEars = [](PoseTrack2D &p, bool mirror, double risePx, double turnPx) {
            for (PoseFrame2D &f : p.frames) {
                const double u = smooth(double(f.t_us), kAddr, kTop);
                const double x = mirror ? kW - 250.0 : 250.0, y = 300.0 - risePx * u;
                f.kp[3] = QPointF(x / kW, (y - turnPx * u) / kH);
                f.kp[4] = QPointF(x / kW, (y + turnPx * u) / kH);
            }
        };
        const double cmpp = 4.267 / 15.0;
        PoseTrack2D d = makePose(false);
        setEars(d, false, 20.0, 0.0);
        const DtlPostureResult r = buildDtlPosture(inputs(d, false), cfg);
        const MetricSeries *h = find(r, "headRise");
        check(h && h->unit == QStringLiteral("cm") && near(atPhase(h, Phase::Address), 0.0, 0.01)
                  && near(atPhase(h, Phase::Top), 20.0 * cmpp, 0.05),
              "§7 ears 20 px higher at the top ⇒ headRise 0 at address, 20 px in cm at the top");
        PoseTrack2D t = makePose(false);
        setEars(t, false, 0.0, 15.0);
        check(near(atPhase(find(buildDtlPosture(inputs(t, false), cfg), "headRise"), Phase::Top), 0.0, 0.01),
              "§7 a head TURN that parts the ears 30 px is not a rise: 0 at the top");
        PoseTrack2D m = makePose(true);
        setEars(m, true, 20.0, 0.0);
        check(near(atPhase(find(buildDtlPosture(inputs(m, true), cfg), "headRise"), Phase::Top), 20.0 * cmpp, 0.05),
              "§7 mirrored golfer ⇒ the same rise");
        DtlPostureInputs in = inputs(d, false);
        in.ballBright = false; in.foMmPerPx = 0.0;
        const DtlPostureResult n = buildDtlPosture(in, cfg);
        bool covered = false;
        for (const GateRecord &g : n.gates)
            if (g.gate == QLatin1String("posture.ruler") && g.covers.contains(QStringLiteral("headRise"))) covered = true;
        check(n.valid && !find(n, "headRise") && covered, "§7 no ruler ⇒ no headRise, and the ruler gate says so");
    }

    // §8 the ball the club reaches (ball_reach_by_club.md): the same reading facing either way, and a
    // ball the club cannot have been soled behind — straight under the hands, or two club lengths
    // away by the ball's own ruler — refused with its reason, as is a swing with no club length.
    {
        const PoseTrack2D d = makePose(false), m = makePose(true);
        const double a = atPhase(find(buildDtlPosture(inputs(d, false), cfg), "ballReachExcess"), Phase::Address);
        const double b = atPhase(find(buildDtlPosture(inputs(m, true), cfg), "ballReachExcess"), Phase::Address);
        check(a > 0 && near(a, b, 1e-6), "§8 mirrored golfer ⇒ the same distance beyond the club's reach");
        auto refusedBy = [&](const DtlPostureInputs &in, const char *code) {
            const DtlPostureResult r = buildDtlPosture(in, cfg);
            bool gate = false;
            for (const GateRecord &g : r.gates)
                if (g.gate == QLatin1String("posture.ballReach") && g.subject == QLatin1String("ballReachExcess")
                    && g.covers.contains(QStringLiteral("ballBodyDistance"))
                    && g.evidence.value(QStringLiteral("code")).toString() == QLatin1String(code)) gate = true;
            return r.valid && gate && !find(r, "ballReachExcess") && !find(r, "ballBodyDistance")
                && find(r, "armHang");
        };
        DtlPostureInputs shadow = inputs(d, false);
        shadow.ballX = 305.0;                                     // the hands' shadow, 90° under them
        check(refusedBy(shadow, "ballAngle"), "§8 a ball straight under the hands is refused (ballAngle), the rest kept");
        DtlPostureInputs shortClub = inputs(d, false);
        shortClub.clubLengthM = 0.40;                             // the ball ruler: 71.6 cm from the hands
        check(refusedBy(shortClub, "ballLength"), "§8 a ball 1.8 club lengths from the hands is refused (ballLength)");
        DtlPostureInputs noClub = inputs(d, false);
        noClub.clubLengthM = 0.0;
        check(refusedBy(noClub, "noClub"), "§8 no club length ⇒ no ball distance (noClub)");
    }

    // §9 THE ARM LIFT AT THE TOP (plane stack D2, 13 Nov 2026): the lead UPPER ARM (shoulder → ELBOW) against
    // the shoulder line, + above it. A known angle reads exactly, for a golfer facing either way and for a
    // left-hander; a bent forearm does not move it; σ carries the trail shoulder's confidence; a top with
    // no trail shoulder is refused with its record.
    {
        const auto liftOf = [&](const PoseTrack2D &d, bool mirror, bool leftHanded) {
            DtlPostureInputs in = inputs(d, mirror);
            in.leadIsLeft = !leftHanded;
            const DtlPostureResult r = buildDtlPosture(in, cfg);
            return std::make_pair(r, atPhase(find(r, "armLiftTop"), Phase::Top));
        };
        for (const bool mirror : { false, true })
            for (const bool lefty : { false, true }) {
                char lab[160];
                const char *who = lefty ? (mirror ? "left-hander facing left" : "left-hander facing right")
                                        : (mirror ? "right-hander facing left" : "right-hander facing right");
                PoseTrack2D up = makePose(mirror);
                setArmAtTop(up, mirror, lefty, 25.0, 12.0, /*fold*/ 35.0);
                const auto [r, v] = liftOf(up, mirror, lefty);
                std::snprintf(lab, sizeof lab, "§9 %s: the elbow 12° above a 25° shoulder line reads +12 (got %.4f)", who, v);
                check(near(v, 12.0, 1e-6), lab);
                PoseTrack2D dn = makePose(mirror);
                setArmAtTop(dn, mirror, lefty, 25.0, -8.0);
                const double w = liftOf(dn, mirror, lefty).second;
                std::snprintf(lab, sizeof lab, "§9 %s: the elbow 8° under it reads −8 (got %.4f)", who, w);
                check(near(w, -8.0, 1e-6), lab);
                const MetricSeries *m = find(r, "armLiftTop");
                check(m && m->routeId == QStringLiteral("dtl") && m->phaseSamples.size() == 1
                          && m->phaseSamples.front().phase == Phase::Top && m->phaseSamples.front().t_us == kTop
                          && m->t_us.empty() && m->unit == QStringLiteral("°"),
                      "§9 …one reading, at the ladder's Top, route dtl, no curve");
            }
        // A bent lead arm is not a lift: the forearm folded 35° up puts the WRIST 24° above the shoulder line
        // (shoulder → wrist), yet the reading is the upper arm's 12°.
        {
            const double b = 37.0 * M_PI / 180.0, c = 72.0 * M_PI / 180.0, a = 25.0 * M_PI / 180.0;
            const double wx = 80.0 * std::cos(b) + 70.0 * std::cos(c), wy = 80.0 * std::sin(b) + 70.0 * std::sin(c);
            const double toWrist = std::atan2(wy, wx) * 180.0 / M_PI - a * 180.0 / M_PI;
            PoseTrack2D d = makePose(false);
            setArmAtTop(d, false, false, 25.0, 12.0, 35.0);
            const double v = liftOf(d, false, false).second;
            char lab[160];
            std::snprintf(lab, sizeof lab, "§9 a bent lead arm (wrist %.1f° above the line) still reads the upper arm's 12 (got %.3f)",
                          toWrist, v);
            check(toWrist > 20.0 && near(v, 12.0, 1e-6), lab);
        }
        // σ: the smoother's σ (2 px on every keypoint) × max(1, 0.8 / conf) — the trail shoulder at 0.5 carries
        // 3.2 px — through the angle, the lead shoulder in both lines.
        {
            PoseTrack2D d = makePose(false);
            setArmAtTop(d, false, false, 25.0, 12.0, 0.0, /*trailConf*/ 0.5f);
            d.smoothed = d.frames;
            d.smoothedAux.resize(d.frames.size());
            for (PoseKpAux &x : d.smoothedAux) x.sigma.fill(2.0f);
            const DtlPostureResult r = buildDtlPosture(inputs(d, false), cfg);
            const MetricSeries *m = find(r, "armLiftTop");
            const double sT = 2.0 * 0.8 / 0.5, sE = 2.0, sL = 2.0, ls = 100.0, la = 80.0, th = 12.0 * M_PI / 180.0;
            const double want = std::sqrt(sT * sT / (ls * ls) + sE * sE / (la * la)
                                          + sL * sL * (1.0 / (ls * ls) + 1.0 / (la * la) - 2.0 * std::cos(th) / (ls * la)))
                              * 180.0 / M_PI;
            const double got = m && m->phaseSamples.front().sigma ? *m->phaseSamples.front().sigma : -1.0;
            char lab[160];
            std::snprintf(lab, sizeof lab, "§9 σ = the three keypoints' σ through the angle, the trail shoulder's × 0.8/0.5 (%.4f°, got %.4f°)",
                          want, got);
            check(m && near(got, want, 1e-4) && m->sigma && near(double(*m->sigma), want, 1e-4)
                      && m->phaseSamples.front().sigmaKind == uint8_t(SigmaKind::Propagated), lab);
            const MetricSeries *u = find(buildDtlPosture(inputs(makePose(false), false), cfg), "armLiftTop");
            check(u && !u->phaseSamples.front().sigma, "§9 …and no σ on a track with no smoother σ (unknown, not small)");
        }
        // The trail shoulder half hidden on every frame of the top (0.2 < confMin): no reading, refused.
        {
            PoseTrack2D d = makePose(false);
            setArmAtTop(d, false, false, 25.0, 12.0, 0.0, /*trailConf*/ 0.2f);
            const DtlPostureResult r = buildDtlPosture(inputs(d, false), cfg);
            const GateRecord *g = nullptr;
            for (const GateRecord &x : r.gates) if (x.gate == QLatin1String("posture.armLift")) g = &x;
            check(r.valid && !find(r, "armLiftTop") && g && g->subject == QLatin1String("armLiftTop")
                      && g->effect == GateEffect::Refused
                      && g->evidence.value(QStringLiteral("code")).toString() == QLatin1String("keypoints")
                      && g->evidence.value(QStringLiteral("frames")).toInt() == 7,
                  "§9 no trail shoulder at the top ⇒ no arm lift, posture.armLift (keypoints, 7 frames looked at)");
            check(find(r, "handPathLoop") && find(r, "trailKneeFlexion"), "§9 …and the other readings stand");
            const DtlPostureResult ok = buildDtlPosture(inputs(makePose(false), false), cfg);
            check(std::none_of(ok.gates.begin(), ok.gates.end(),
                               [](const GateRecord &x) { return x.gate == QLatin1String("posture.armLift"); }),
                  "§9 …and none where it reads");
        }
        // An address refusal covers it, as it covers every reading here.
        {
            const PoseTrack2D d = makePose(false, 30.0, /*toeConf*/ 0.1f);
            const DtlPostureResult r = buildDtlPosture(inputs(d, false), cfg);
            bool covered = false;
            for (const GateRecord &g : r.gates)
                if (g.gate == QLatin1String("posture.address") && g.covers.contains(QStringLiteral("armLiftTop"))) covered = true;
            check(!r.valid && covered, "§9 the address refusal covers armLiftTop");
        }
    }

    std::printf(g_fail ? "FAILED (%d)\n" : "ALL PASS\n", g_fail);
    return g_fail ? 1 : 0;
}
