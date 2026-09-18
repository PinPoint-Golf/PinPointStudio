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

// Standalone test for segment_rates.{h,cpp} — the four angular-speed series and the kinematic
// sequence, by three routes against ONE synthetic downswing (kinematic_sequence_design.md §9
// stage 0).
//
// THE MODEL. Four segments fire as Gaussian rate bumps in the downswing — pelvis, thorax, arm,
// club — with known peak instants and amplitudes. From those the test RENDERS what each sensor
// would see: an IMU's orientation + gyro streams on a 200 Hz grid; a face-on camera's hip and
// shoulder spans (the cosine of the turn) and the lead arm's image angle through the swing-plane
// ellipse at 120 fps; the club tracker's shaft angle through the same ellipse at 240 Hz. Every
// route then runs through the one producer.
//
// WHAT IS PINNED:
//   §1  the de-projection geometry round-trips and its gain is bounded in [k, 1/k];
//   §2  face-on only: all four channels produced and Estimated; the ARM and CLUB nodes land within
//       a few ms of the truth; the pelvis rate is opening-POSITIVE at its peak;
//   §3  IMU only: all four Direct; every node within a few ms; the verdict is proximal→distal;
//   §4  THE COMMON MEASURE: the arm and club nodes from §2 and §3 agree to a few ms;
//   §5  handedness: a left-hander's pelvis reads opening-positive too, and the mirrored arm places
//       at the same instant;
//   §6  a node whose σ exceeds the placement threshold is emitted UNRESOLVED, and the verdict says
//       so — the thing the whole design hangs on;
//   §7  the domain mask and the refusal cases (no Impact, no inputs);
//   §8  THE SIGHTED BAND (design §12.4): a trunk peak that falls where the span is near square is
//       emitted as a BOUND that contains the truth, never as a node; the same peak moved into
//       sight is placed; and an address set open by 15° moves neither, because the reference is
//       the square-up span and not the address span.

#include "../segment_rates.h"
#include "../kinematic_sequence_json.h"

#include <QJsonArray>
#include <QJsonObject>

#include <QQuaternion>
#include <QVector3D>

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

constexpr double kPi  = 3.14159265358979323846;
constexpr double kD2R = kPi / 180.0;
constexpr int    kW = 1000, kH = 1000;
constexpr int    kLSh = 5, kRSh = 6, kLWr = 9, kRWr = 10, kLHip = 11, kRHip = 12;

// ── The synthetic downswing ────────────────────────────────────────────────────────────────────

// Address is SQUARE (the face-on route's reference window is the first 250 ms after Address);
// the backswing turns the body lines closed between 300 and 700 ms; the downswing bumps fire after
// the Top at 700 ms; impact at 1000 ms.
constexpr int64_t kAddressUs = 0, kTopUs = 700000, kTransitionUs = 720000, kImpactUs = 1000000,
                  kFinishUs = 1150000;
constexpr double  kBackswingStartS = 0.30;

struct Bump { double peakDps, tPeakS, widthS; };
// Peaks before impact: pelvis 87 ms, thorax 75, arm 60, club 0 (at the ball). Widths chosen so
// the total turn is a plausible downswing (pelvis ~60°, thorax ~90°).
constexpr Bump kPelvis { 480.0,  0.913, 0.050 };
constexpr Bump kThorax { 727.0,  0.925, 0.050 };
constexpr Bump kArm    { 980.0,  0.940, 0.040 };
constexpr Bump kClub   { 2254.0, 1.000, 0.045 };

static double rateAt(const Bump &b, double s)
{
    const double x = (s - b.tPeakS) / b.widthS;
    return b.peakDps * std::exp(-0.5 * x * x);
}
// Integrated turn from the top (degrees), by fine trapezoid.
static double turnFromTop(const Bump &b, double s)
{
    double acc = 0.0;
    const double dt = 0.0005;
    for (double u = kTopUs * 1e-6; u < s; u += dt) acc += 0.5 * (rateAt(b, u) + rateAt(b, u + dt)) * dt;
    return acc;
}

// Closed-positive turn of a body line at s: square at address, closing smoothly through the
// backswing to `startClosedDeg` at the top, then opening through square in the downswing.
// `addrOpenDeg` sets the golfer OPEN at address by that much (a negative closed angle), which the
// backswing then closes from — the corpus's 15–19° address offset (design §12.4).
static double closedAngleDeg(const Bump &b, double startClosedDeg, double s, double addrOpenDeg = 0.0)
{
    const double topS = kTopUs * 1e-6;
    if (s <= kBackswingStartS) return -addrOpenDeg;
    if (s <= topS) {
        const double u = (s - kBackswingStartS) / (topS - kBackswingStartS);
        return -addrOpenDeg + (startClosedDeg + addrOpenDeg) * (u * u * (3.0 - 2.0 * u));   // smoothstep
    }
    return startClosedDeg - turnFromTop(b, s);
}

// The swing plane as the face-on camera images it.
constexpr double kRatio = 0.87, kNodeDeg = 10.0;
// Forward projection: in-plane α → image ψ. tan(ψ − ν) = k tan α.
static double imageAngle(double alphaRad)
{
    const double nu = kNodeDeg * kD2R;
    const double phi = std::atan2(kRatio * std::sin(alphaRad), std::cos(alphaRad));
    // Keep the sheet continuous with α (the tracker unwraps its own θ).
    const double sheet = std::round((alphaRad - phi) / (2.0 * kPi)) * 2.0 * kPi;
    return phi + sheet + nu;
}

static std::vector<PhaseEvent> phases()
{
    return { { Phase::Address, kAddressUs, 1.f }, { Phase::Top, kTopUs, 1.f },
             { Phase::Transition, kTransitionUs, 1.f }, { Phase::Impact, kImpactUs, 1.f },
             { Phase::Finish, kFinishUs, 1.f } };
}

// Face-on pose at 120 fps. Hip span 200 px, shoulder span 320 px at address. The arm's in-plane
// angle integrates the arm bump from −120° (near the top) downward.
static PoseTrack2D makePose(bool leadIsLeft, double pelvisStartClosedDeg = 45.0, double addrOpenDeg = 0.0,
                            const Bump &pelvisBump = kPelvis)
{
    PoseTrack2D pose;
    for (int64_t t = 0; t <= kFinishUs; t += 8333) {
        const double s = t * 1e-6;
        PoseFrame2D f;
        f.t_us = t;
        const double hip = closedAngleDeg(pelvisBump, pelvisStartClosedDeg, s, addrOpenDeg) * kD2R;
        const double sh  = closedAngleDeg(kThorax, 90.0, s, addrOpenDeg) * kD2R;
        const double hs = 0.5 * 200.0 * std::fabs(std::cos(hip)) / kW;
        const double ss = 0.5 * 320.0 * std::fabs(std::cos(sh))  / kW;
        f.kp[kLHip] = QPointF(0.5 - hs, 0.55); f.conf[kLHip] = 0.9f;
        f.kp[kRHip] = QPointF(0.5 + hs, 0.55); f.conf[kRHip] = 0.9f;
        f.kp[kLSh]  = QPointF(0.5 - ss, 0.30); f.conf[kLSh]  = 0.9f;
        f.kp[kRSh]  = QPointF(0.5 + ss, 0.30); f.conf[kRSh]  = 0.9f;
        // Lead arm: shoulder fixed, wrist on the imaged arc.
        const double alpha = (-120.0 + turnFromTop(kArm, s)) * kD2R;
        const double psi   = imageAngle(alpha);
        const int sIdx = leadIsLeft ? kLSh : kRSh, wIdx = leadIsLeft ? kLWr : kRWr;
        const QPointF shoulder = f.kp[sIdx];
        f.kp[wIdx] = QPointF(shoulder.x() + 600.0 * std::cos(psi) / kW,
                             shoulder.y() + 600.0 * std::sin(psi) / kH);
        f.conf[wIdx] = 0.9f;
        pose.frames.push_back(f);
    }
    return pose;
}

// The club track at 240 Hz (synth tier), with the downswing conic already fitted.
static ShaftTrack2D makeShaft()
{
    ShaftTrack2D s;
    s.valid = true;
    s.frameWidth = kW; s.frameHeight = kH;
    for (int64_t t = 0; t <= kFinishUs; t += 4167) {
        const double sec   = t * 1e-6;
        const double alpha = (-200.0 + turnFromTop(kClub, sec)) * kD2R;
        ShaftSample2D e;
        e.t_us     = t;
        e.thetaRad = imageAngle(alpha);
        e.conf     = 0.9f;
        e.gripPx   = QPointF(500, 500);
        e.headPx   = QPointF(500 + 400 * std::cos(e.thetaRad), 500 + 400 * std::sin(e.thetaRad));
        s.synth.push_back(e);
        s.samples.push_back(e);
    }
    s.plane.valid = true;
    s.plane.channel = 0;
    s.plane.measured.fitted = true;
    s.plane.measured.ratioDown = kRatio;
    s.plane.measured.nodeDownDeg = kNodeDeg;
    ShaftPosition p7; p7.p = 7; p7.t_us = kImpactUs; p7.conf = 0.9f;
    s.positions.push_back(p7);
    return s;
}

// IMU streams on a 200 Hz grid. Pelvis / thorax: yaw about world Z, opening = +Z for a
// right-hander (negative for a left-hander); gyro in the anatomical frame is the same vector since
// a rotation about Z leaves Z alone. Arm / club: the long axis (anatomical +Y) swung about world X.
static FusedStreams makeStreams(bool leadIsLeft, bool withGyro = true)
{
    FusedStreams fs;
    for (int64_t t = 0; t <= kFinishUs; t += 5000) fs.timeGrid.push_back(t);
    const double sgn = leadIsLeft ? 1.0 : -1.0;
    const auto yawStream = [&](SegmentRole role, const Bump &b, double startClosedDeg) {
        SegmentStream s;
        s.role = role;
        for (int64_t t : fs.timeGrid) {
            const double sec = t * 1e-6;
            const double yawDeg = -sgn * closedAngleDeg(b, startClosedDeg, sec);   // opening ⇒ yaw rising
            s.qAnat.push_back(QQuaternion::fromAxisAndAngle(0.f, 0.f, 1.f, float(yawDeg)));
            if (withGyro) {
                const double rate = (sec > kTopUs * 1e-6) ? rateAt(b, sec) : 0.0;
                s.gyroDps.push_back(QVector3D(0.f, 0.f, float(sgn * rate)));
                s.accelG.push_back(QVector3D(0.f, 0.f, 1.f));
            }
        }
        return s;
    };
    const auto swingStream = [&](SegmentRole role, const Bump &b) {
        SegmentStream s;
        s.role = role;
        for (int64_t t : fs.timeGrid) {
            const double sec = t * 1e-6;
            const double angDeg = turnFromTop(b, sec);
            s.qAnat.push_back(QQuaternion::fromAxisAndAngle(1.f, 0.f, 0.f, float(angDeg)));
            if (withGyro) {
                const double rate = (sec > kTopUs * 1e-6) ? rateAt(b, sec) : 0.0;
                s.gyroDps.push_back(QVector3D(float(rate), 0.f, 0.f));
                s.accelG.push_back(QVector3D(0.f, 0.f, 1.f));
            }
        }
        return s;
    };
    fs.segments.push_back(yawStream(SegmentRole::Pelvis, kPelvis, 45.0));
    fs.segments.push_back(yawStream(SegmentRole::Thorax, kThorax, 90.0));
    fs.segments.push_back(swingStream(SegmentRole::LeadForearm, kArm));
    fs.segments.push_back(swingStream(SegmentRole::Club, kClub));
    return fs;
}

static const KsNode *nodeOf(const SegmentRatesResult &r, SeqSegment s) { return r.sequence.node(s); }
static double msFromTruth(const KsNode *n, const Bump &b)
{
    return n ? std::fabs(double(n->tPeakUs) * 1e-3 - b.tPeakS * 1e3) : 1e9;
}

int main()
{
    std::printf("segment_rates_test\n");
    const std::vector<PhaseEvent> ph = phases();
    const SegmentRatesConfig cfg;

    // ── §1 de-projection geometry ─────────────────────────────────────────────────────────────
    {
        bool round = true, gain = true;
        for (double a = -3.0; a <= 3.0; a += 0.05) {
            const double psi = imageAngle(a);
            if (!near(deprojectPlaneAngle(psi, kNodeDeg * kD2R, kRatio), a, 1e-9)) round = false;
            const double g = deprojectGain(psi, kNodeDeg * kD2R, kRatio);
            if (g < kRatio - 1e-9 || g > 1.0 / kRatio + 1e-9) gain = false;
        }
        CHECK("§1 image angle → in-plane angle round-trips through the ellipse", round);
        CHECK("§1 the de-projection gain stays inside [k, 1/k]", gain);
        CHECK("§1 k = 1 is the identity (in the plane's own frame, ν removed)", near(deprojectPlaneAngle(0.7, 0.2, 1.0), 0.5, 1e-12));
    }

    // ── §2 face-on only ───────────────────────────────────────────────────────────────────────
    SegmentRatesResult faceOn;
    {
        const PoseTrack2D pose = makePose(true);
        const ShaftTrack2D shaft = makeShaft();
        SegmentRatesInputs in;
        in.pose = &pose; in.frameW = kW; in.frameH = kH; in.leadIsLeft = true;
        in.shaft = &shaft; in.phases = &ph; in.impactUs = kImpactUs;
        faceOn = buildSegmentRates(in, cfg);
        CHECK("§2 face-on: result valid", faceOn.valid);
        CHECK("§2 face-on: all four channels produced",
              faceOn.pelvis.produced() && faceOn.thorax.produced() && faceOn.leadArm.produced() && faceOn.club.produced());
        CHECK("§2 face-on: every channel Estimated, routes faceOn / faceOnClub",
              !faceOn.pelvis.direct && !faceOn.thorax.direct && !faceOn.leadArm.direct && !faceOn.club.direct
              && faceOn.pelvis.routeId == QLatin1String("faceOn") && faceOn.club.routeId == QLatin1String("faceOnClub"));
        const KsNode *arm = nodeOf(faceOn, SeqSegment::LeadArm), *club = nodeOf(faceOn, SeqSegment::Club);
        const KsNode *pel = nodeOf(faceOn, SeqSegment::Pelvis), *tho = nodeOf(faceOn, SeqSegment::Thorax);
        CHECK("§2 face-on: arm node placed within 6 ms of the truth",  arm && arm->placed && msFromTruth(arm, kArm) <= 6.0);
        CHECK("§2 face-on: club node placed within 6 ms of the truth", club && club->placed && msFromTruth(club, kClub) <= 6.0);
        CHECK("§2 face-on: arm peak within 8 % of 980 °/s (de-projected magnitude)",
              arm && near(arm->peakDps, kArm.peakDps, 0.08 * kArm.peakDps));
        CHECK("§2 face-on: club peak within 8 % of 2254 °/s", club && near(club->peakDps, kClub.peakDps, 0.08 * kClub.peakDps));
        CHECK("§2 face-on: pelvis node produced and its peak is opening-POSITIVE", pel && pel->peakDps > 0.0);
        CHECK("§2 face-on: thorax node produced and its peak is opening-POSITIVE", tho && tho->peakDps > 0.0);
        // This pelvis starts 45° closed and peaks 30° into a 60° turn: 15° closed, INSIDE the
        // 20° blind band. The route must not claim it — it bounds it (§8 pins the bound). The
        // thorax starts 90° closed and peaks 45° in: in sight, and placed when its σ allows.
        CHECK("§2 face-on: the pelvis peak falls in the blind band — NOT placed, bounded",
              pel && !pel->placed && std::isfinite(pel->peakNoEarlierThanMs));
        CHECK("§2 face-on: the thorax peak is in sight and placed within 12 ms of the truth",
              tho && tho->placed && msFromTruth(tho, kThorax) <= 12.0);
        {
            // The route-level switch still closes everything.
            SegmentRatesConfig closed = cfg;
            closed.faceOnTrunkPlacement = false;
            const SegmentRatesResult c = buildSegmentRates(in, closed);
            const KsNode *cp = nodeOf(c, SeqSegment::Pelvis), *ct = nodeOf(c, SeqSegment::Thorax);
            CHECK("§2 face-on: faceOnTrunkPlacement=false leaves both trunk nodes unplaced",
                  cp && ct && !cp->placed && !ct->placed);
        }
        CHECK("§2 face-on: series carry a σ and the °/s unit",
              faceOn.pelvis.series.sigma.has_value() && faceOn.club.series.unit == QStringLiteral("°/s"));
        for (const KsNode &n : faceOn.sequence.nodes)
            std::printf("    faceOn %-8s %s  t=%.1f ms before impact ±%.1f  peak %.0f ±%.0f\n",
                        seqSegmentKey(n.segment), n.placed ? "placed  " : "UNPLACED", n.beforeImpactMs,
                        n.tSigmaMs, n.peakDps, n.peakSigmaDps);
        std::printf("    faceOn verdict %s (%s)\n", qPrintable(faceOn.sequence.verdict),
                    qPrintable(faceOn.sequence.routeSummary));
    }

    // ── §3 IMU only ───────────────────────────────────────────────────────────────────────────
    SegmentRatesResult imu;
    {
        const FusedStreams fs = makeStreams(true);
        SegmentRatesInputs in;
        in.streams = &fs; in.leadIsLeft = true; in.phases = &ph; in.impactUs = kImpactUs;
        imu = buildSegmentRates(in, cfg);
        CHECK("§3 IMU: all four channels produced and Direct",
              imu.pelvis.produced() && imu.thorax.produced() && imu.leadArm.produced() && imu.club.produced()
              && imu.pelvis.direct && imu.thorax.direct && imu.leadArm.direct && imu.club.direct);
        CHECK("§3 IMU: routes pelvisImu / thoraxImu / leadArmImus / clubSensorFused",
              imu.pelvis.routeId == QLatin1String("pelvisImu") && imu.thorax.routeId == QLatin1String("thoraxImu")
              && imu.leadArm.routeId == QLatin1String("leadArmImus") && imu.club.routeId == QLatin1String("clubSensorFused"));
        bool allClose = true, allPlaced = true;
        const Bump *truth[4] = { &kPelvis, &kThorax, &kArm, &kClub };
        for (int i = 0; i < 4; ++i) {
            const KsNode *n = nodeOf(imu, SeqSegment(i));
            if (!n || !n->placed) allPlaced = false;
            if (msFromTruth(n, *truth[i]) > 5.0) allClose = false;
        }
        CHECK("§3 IMU: every node placed", allPlaced);
        CHECK("§3 IMU: every node within 5 ms of the truth", allClose);
        CHECK("§3 IMU: pelvis peak within 5 % of 480 °/s, opening-positive",
              nodeOf(imu, SeqSegment::Pelvis) && near(nodeOf(imu, SeqSegment::Pelvis)->peakDps, 480.0, 24.0));
        CHECK("§3 IMU: verdict proximalToDistal, order resolved, routeSummary direct",
              imu.sequence.verdict == QLatin1String("proximalToDistal") && imu.sequence.orderResolved
              && imu.sequence.routeSummary == QLatin1String("direct"));
        CHECK("§3 IMU: pelvis decelerates before impact", imu.sequence.pelvisDecelerates == 1);
        CHECK("§3 IMU: gaps are 12 / 15 / 60 ms within 5",
              imu.sequence.gapsMs.size() == 3 && near(imu.sequence.gapsMs[0], 12.0, 5.0)
              && near(imu.sequence.gapsMs[1], 15.0, 5.0) && near(imu.sequence.gapsMs[2], 60.0, 5.0));
        for (const KsNode &n : imu.sequence.nodes)
            std::printf("    imu    %-8s %s  t=%.1f ms before impact ±%.1f  peak %.0f ±%.0f\n",
                        seqSegmentKey(n.segment), n.placed ? "placed  " : "UNPLACED", n.beforeImpactMs,
                        n.tSigmaMs, n.peakDps, n.peakSigmaDps);
    }

    // ── §4 the common measure: routes agree ───────────────────────────────────────────────────
    {
        const KsNode *a1 = nodeOf(faceOn, SeqSegment::LeadArm), *a2 = nodeOf(imu, SeqSegment::LeadArm);
        const KsNode *c1 = nodeOf(faceOn, SeqSegment::Club),    *c2 = nodeOf(imu, SeqSegment::Club);
        CHECK("§4 arm node: face-on and IMU agree within 6 ms",
              a1 && a2 && std::llabs(a1->tPeakUs - a2->tPeakUs) <= 6000);
        CHECK("§4 club node: face-on and IMU agree within 6 ms",
              c1 && c2 && std::llabs(c1->tPeakUs - c2->tPeakUs) <= 6000);
        CHECK("§4 arm peak: face-on within 10 % of the IMU's",
              a1 && a2 && near(a1->peakDps, a2->peakDps, 0.10 * a2->peakDps));
    }

    // ── §5 handedness ─────────────────────────────────────────────────────────────────────────
    {
        const FusedStreams fs = makeStreams(false);
        SegmentRatesInputs in;
        in.streams = &fs; in.leadIsLeft = false; in.phases = &ph; in.impactUs = kImpactUs;
        const SegmentRatesResult lh = buildSegmentRates(in, cfg);
        const KsNode *p = nodeOf(lh, SeqSegment::Pelvis);
        CHECK("§5 left-hander IMU: pelvis peak opening-POSITIVE (mirrored by the producer)",
              p && p->placed && p->peakDps > 400.0);
        const PoseTrack2D poseL = makePose(false);
        const ShaftTrack2D shaft = makeShaft();
        SegmentRatesInputs fin;
        fin.pose = &poseL; fin.frameW = kW; fin.frameH = kH; fin.leadIsLeft = false;
        fin.shaft = &shaft; fin.phases = &ph; fin.impactUs = kImpactUs;
        const SegmentRatesResult lf = buildSegmentRates(fin, cfg);
        const KsNode *a = nodeOf(lf, SeqSegment::LeadArm);
        CHECK("§5 left-hander face-on: the arm is read off the RIGHT side and places at the same instant",
              a && a->placed && msFromTruth(a, kArm) <= 6.0);
    }

    // ── §6 unresolved is a state, not a failure ───────────────────────────────────────────────
    {
        SegmentRatesConfig strict = cfg;
        strict.maxPlaceSigmaMs = 0.0;          // nothing can be placed
        const FusedStreams fs = makeStreams(true);
        SegmentRatesInputs in;
        in.streams = &fs; in.leadIsLeft = true; in.phases = &ph; in.impactUs = kImpactUs;
        const SegmentRatesResult r = buildSegmentRates(in, strict);
        bool noneP = true;
        for (const KsNode &n : r.sequence.nodes) if (n.placed) noneP = false;
        CHECK("§6 σ threshold at zero: every node emitted, none placed", r.sequence.nodes.size() == 4 && noneP);
        CHECK("§6 …and the verdict is unresolved with an empty order",
              r.sequence.verdict == QLatin1String("unresolved") && r.sequence.order.empty());
        CHECK("§6 …and the series are still produced (the curve is shown, the node is not claimed)",
              r.pelvis.produced() && r.club.produced());
        // The gap test: two nodes 12 ms apart cannot be ordered under a 20 ms σ.
        SegmentRatesConfig wide = cfg;
        wide.sigmaK = 100.0;                   // every gap inside its σ
        const SegmentRatesResult w = buildSegmentRates(in, wide);
        CHECK("§6 gaps inside k·σ: nodes placed but the order is NOT declared",
              w.sequence.order.size() == 4 && !w.sequence.orderResolved
              && w.sequence.verdict == QLatin1String("unresolved"));
    }

    // ── §7 domain and refusals ────────────────────────────────────────────────────────────────
    {
        const MetricSeries &m = imu.pelvis.series;
        bool maskedOutside = !m.valid.empty();
        for (size_t i = 0; i < m.t_us.size() && maskedOutside; ++i) {
            const bool inDom = m.t_us[i] >= kTransitionUs && m.t_us[i] <= kImpactUs;
            if (inDom != (m.valid[i] != 0)) maskedOutside = false;
        }
        CHECK("§7 the series is valid exactly on Transition→Impact", maskedOutside);
        bool clubMasked = !imu.club.series.valid.empty();
        CHECK("§7 the club series carries a mask too (ends at the P7 knot / impact)", clubMasked);

        std::vector<PhaseEvent> noImpact = { { Phase::Address, kAddressUs, 1.f }, { Phase::Top, kTopUs, 1.f } };
        const FusedStreams fs = makeStreams(true);
        SegmentRatesInputs in;
        in.streams = &fs; in.leadIsLeft = true; in.phases = &noImpact; in.impactUs = -1;
        CHECK("§7 no Impact anywhere: refused", !buildSegmentRates(in, cfg).valid);
        SegmentRatesInputs none;
        none.phases = &ph; none.impactUs = kImpactUs;
        CHECK("§7 no inputs at all: refused", !buildSegmentRates(none, cfg).valid);
        {
            const PoseTrack2D pose = makePose(true);
            const ShaftTrack2D shaft = makeShaft();
            SegmentRatesInputs bad;
            bad.pose = &pose; bad.frameW = kW; bad.frameH = kH; bad.leadIsLeft = true;
            bad.shaft = &shaft; bad.phases = &ph; bad.impactUs = kImpactUs;
            bad.clubheadSpeedImpactMph = 20.0;
            const SegmentRatesResult r = buildSegmentRates(bad, cfg);
            const KsNode *c = nodeOf(r, SeqSegment::Club);
            CHECK("§7 a track whose clubhead speed at impact is not credible produces the curve but no club node",
                  r.club.produced() && c && !c->placed);
        }
        CHECK("§7 segmentRateSeries lists the produced channels only",
              segmentRateSeries(faceOn).size() == 4 && segmentRateSeries(SegmentRatesResult{}).empty());
    }

    // ── §8 the sighted band ───────────────────────────────────────────────────────────────────
    {
        const ShaftTrack2D shaft = makeShaft();
        // A pelvis that squares up: the reference is only observable when the line comes to
        // square somewhere in the swing, so (b) and (c) use a wider bump — the same 480 °/s peak at
        // the same instant, 84° of total turn from 80° closed, square 30 ms after impact, 20°
        // closed (the edge of sight) 47 ms before impact, the peak 40 ms before that and falling
        // at the edge. The wider hump has less curvature, so its timing σ (~65 ms) is above the
        // 40 ms threshold; the σ model is what the §9 capture calibrates, and this section
        // widens the threshold to test the sighted-band logic rather than the σ model.
        const Bump wide { 480.0, 0.913, 0.070 };
        SegmentRatesConfig cfg8 = cfg;
        cfg8.maxPlaceSigmaMs = 100.0;
        const auto run = [&](double pelvisStart, double addrOpen, const Bump &b = kPelvis) {
            PoseTrack2D pose = makePose(true, pelvisStart, addrOpen, b);
            SegmentRatesInputs in;
            in.pose = &pose; in.frameW = kW; in.frameH = kH; in.leadIsLeft = true;
            in.shaft = &shaft; in.phases = &ph; in.impactUs = kImpactUs;
            return buildSegmentRates(in, cfg8);
        };
        // (a) The bound contains the truth. The 45°-start pelvis crosses 20° closed 25° into its
        //     turn, ~97 ms before impact; the true peak (87 ms) is later than that, so "no earlier
        //     than ~97 ms" holds it, and the bound is where the view went blind, not where the
        //     rate happened to be highest in sight.
        const SegmentRatesResult blind = run(45.0, 0.0);
        const KsNode *bp = nodeOf(blind, SeqSegment::Pelvis);
        CHECK("§8a blind-band pelvis: not placed", bp && !bp->placed);
        CHECK("§8a blind-band pelvis: the bound holds the truth (87 ms ≤ bound)",
              bp && std::isfinite(bp->peakNoEarlierThanMs) && bp->peakNoEarlierThanMs >= 87.0);
        CHECK("§8a blind-band pelvis: the bound is the edge of sight (~97 ms), within 15 ms",
              bp && near(bp->peakNoEarlierThanMs, 97.0, 15.0));
        CHECK("§8a blind-band pelvis: no upper bound is claimed", bp && !std::isfinite(bp->peakNoLaterThanMs));
        if (bp) std::printf("    §8a pelvis bound: no earlier than %.1f ms before impact (σ_t %.1f)\n",
                            bp->peakNoEarlierThanMs, bp->tSigmaMs);
        // (b) The wide bump from 80° closed peaks 38° closed — in sight — and is placed on the truth.
        const SegmentRatesResult seen = run(80.0, 0.0, wide);
        const KsNode *sp = nodeOf(seen, SeqSegment::Pelvis);
        if (sp) std::printf("    §8b pelvis: placed=%d %.1f ms ±%.1f peak %.0f bound≤%.1f\n",
                            int(sp->placed), sp->beforeImpactMs, sp->tSigmaMs, sp->peakDps, sp->peakNoEarlierThanMs);
        CHECK("§8b sighted pelvis: placed", sp && sp->placed);
        CHECK("§8b sighted pelvis: within 12 ms of the truth", sp && msFromTruth(sp, wide) <= 12.0);
        CHECK("§8b sighted pelvis: no bound attached", sp && !sp->bounded());
        // (c) Address set 15° open: the address span is cos 15° of square. With an address
        //     reference this read as 15° of turn at address and clamped the whole downswing; with
        //     the square-up reference nothing moves.
        const SegmentRatesResult open = run(80.0, 15.0, wide);
        const KsNode *op = nodeOf(open, SeqSegment::Pelvis), *ot = nodeOf(open, SeqSegment::Thorax);
        const KsNode *st = nodeOf(seen, SeqSegment::Thorax);
        if (op) std::printf("    §8c open-address pelvis: placed=%d %.1f ms ±%.1f peak %.0f bound≤%.1f ≥%.1f\n",
                            int(op->placed), op->beforeImpactMs, op->tSigmaMs, op->peakDps,
                            op->peakNoEarlierThanMs, op->peakNoLaterThanMs);
        CHECK("§8c open address: the sighted pelvis node still places, on the same instant within 6 ms",
              op && sp && op->placed && std::llabs(op->tPeakUs - sp->tPeakUs) <= 6000);
        CHECK("§8c open address: the pelvis peak amplitude moves by less than 10 %",
              op && sp && near(op->peakDps, sp->peakDps, 0.10 * sp->peakDps));
        CHECK("§8c open address: the thorax node is unmoved within 6 ms",
              ot && st && ot->placed && st->placed && std::llabs(ot->tPeakUs - st->tPeakUs) <= 6000);
        const SegmentRatesResult openBlind = run(45.0, 15.0);
        const KsNode *obp = nodeOf(openBlind, SeqSegment::Pelvis);
        CHECK("§8c open address: the blind-band bound is unmoved within 10 ms",
              obp && bp && std::isfinite(obp->peakNoEarlierThanMs)
              && near(obp->peakNoEarlierThanMs, bp->peakNoEarlierThanMs, 10.0));
        // (d) The bound is serialised, and only when it exists.
        const QJsonObject j = kinematicSequenceToJson(blind.sequence, [](int64_t t) { return qint64(t); });
        bool boundOut = false, noBoundOnPlaced = true;
        for (const QJsonValue &v : j.value(QStringLiteral("nodes")).toArray()) {
            const QJsonObject n = v.toObject();
            const QString seg = n.value(QStringLiteral("segment")).toString();
            if (seg == QLatin1String("pelvis") && n.contains(QStringLiteral("peakNoEarlierThanMs"))) boundOut = true;
            if (seg == QLatin1String("club") && n.contains(QStringLiteral("peakNoEarlierThanMs"))) noBoundOnPlaced = false;
        }
        CHECK("§8d JSON carries peakNoEarlierThanMs on the bounded node and not on the placed one",
              boundOut && noBoundOnPlaced);
    }

    std::printf(g_fail ? "FAILED (%d)\n" : "OK\n", g_fail);
    return g_fail ? 1 : 0;
}
