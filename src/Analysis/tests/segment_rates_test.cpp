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
//   §9  THE PAIRED ROUTE (faceOn+dtl). The SAME swing is rendered a second time through a
//       down-the-line camera: a different pixel scale, its own clock 3 ms out of phase at a
//       different frame rate, a shoulder line that TILTS 30° out of horizontal through the
//       downswing, and an inter-view angle of 80° rather than 90°. The pair must then place the
//       trunk nodes on the truth where the face-on span rung can only BOUND them, must survive
//       the tilt that a 2-D span distance absorbs, must say "not before impact" rather than
//       placing an edge peak when the trunk peaks after the ball, must NOT reproduce the unsigned
//       separation's fake peak at square (gate G1 of
//       docs/research/data/kinematic_sequence/pair_span_turn_20260920.md), must mirror for a
//       left-hander, and must fall through to face-on — leaving §2 exactly as it was — whenever
//       the second view is missing, dark, unconfident, too small or inconsistent.

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
constexpr int    kLSh = 5, kRSh = 6, kLWr = 9, kRWr = 10, kLHip = 11, kRHip = 12,
                 kLAnk = 15, kRAnk = 16;

// ── §9's second camera ─────────────────────────────────────────────────────────────────────────
// The angle between the two camera axes is NOT 90°: the measured rigs read 75–84°, the
// down-the-line camera sitting behind the ball rather than behind the hands. The pixel scale is
// not 1 either, and neither is the clock: 144 fps offset 3 ms against the face-on 120.
constexpr double  kGammaDeg     = 80.0;
constexpr double  kDtlScale     = 1.35;          // s_D / s_F
constexpr int     kWD = 640, kHD = 800;
constexpr int64_t kDtlOffsetUs  = 3000;
constexpr int64_t kDtlStepUs    = 6944;
constexpr double  kVertExtentPx = 400.0;         // ankle mid → shoulder mid, face-on pixels
constexpr double  kHipSpanPx    = 200.0, kShSpanPx = 320.0;

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

// The shoulder line's TILT out of horizontal: flat until the Top, then smoothly to `tiltDeg` at
// impact and held. Zero everywhere unless a fixture asks for it, so §2–§8 are untouched. This is
// the thing a 2-D span distance cannot separate from turn and the ratio of two signed horizontal
// separations divides out.
static double tiltAt(double s, double tiltDeg)
{
    if (tiltDeg == 0.0) return 0.0;
    const double topS = kTopUs * 1e-6, impS = kImpactUs * 1e-6;
    if (s <= topS) return 0.0;
    const double u = std::min(1.0, (s - topS) / (impS - topS));
    return tiltDeg * (u * u * (3.0 - 2.0 * u));
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
// `signedSpans` drops the fabs on the imaged cosine, so a line turned past 90° to the camera
// images with its keypoints genuinely the other way round. §2–§8 keep the fabs and are therefore
// bit-identical; on every §9 fixture but the past-90° one the two are the same number anyway,
// because the turn never exceeds 90° there.
static PoseTrack2D makePose(bool leadIsLeft, double pelvisStartClosedDeg = 45.0, double addrOpenDeg = 0.0,
                            const Bump &pelvisBump = kPelvis, double shoulderTiltDeg = 0.0,
                            bool signedSpans = false, double thoraxStartClosedDeg = 90.0,
                            const Bump &thoraxBump = kThorax)
{
    PoseTrack2D pose;
    const auto proj = [&](double a) { return signedSpans ? std::cos(a) : std::fabs(std::cos(a)); };
    for (int64_t t = 0; t <= kFinishUs; t += 8333) {
        const double s = t * 1e-6;
        PoseFrame2D f;
        f.t_us = t;
        const double hip = closedAngleDeg(pelvisBump, pelvisStartClosedDeg, s, addrOpenDeg) * kD2R;
        const double sh  = closedAngleDeg(thoraxBump, thoraxStartClosedDeg, s, addrOpenDeg) * kD2R;
        const double tau = tiltAt(s, shoulderTiltDeg) * kD2R;
        const double hs = 0.5 * kHipSpanPx * proj(hip) / kW;
        const double ss = 0.5 * kShSpanPx * std::cos(tau) * proj(sh) / kW;
        const double sy = 0.5 * kShSpanPx * std::sin(tau) / kH;     // 0 unless the fixture tilts
        f.kp[kLHip] = QPointF(0.5 - hs, 0.55); f.conf[kLHip] = 0.9f;
        f.kp[kRHip] = QPointF(0.5 + hs, 0.55); f.conf[kRHip] = 0.9f;
        f.kp[kLSh]  = QPointF(0.5 - ss, 0.30 - sy); f.conf[kLSh]  = 0.9f;
        f.kp[kRSh]  = QPointF(0.5 + ss, 0.30 + sy); f.conf[kRSh]  = 0.9f;
        // The ankles are here only so the pair route can read the body's VERTICAL extent, which
        // is how it fixes the two views' pixel-scale ratio with no calibration (§9). No other
        // route reads them, so §2–§8 do not move.
        f.kp[kLAnk] = QPointF(0.48, 0.30 + kVertExtentPx / kH); f.conf[kLAnk] = 0.9f;
        f.kp[kRAnk] = QPointF(0.52, 0.30 + kVertExtentPx / kH); f.conf[kRAnk] = 0.9f;
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

// THE SAME SWING THROUGH THE DOWN-THE-LINE CAMERA. Its image-x axis is the face-on one turned
// kGammaDeg about the vertical, so for a line of length L tilted τ and turned ψ (closed-positive)
//
//     face-on          d_fo  = s_F · L · cos τ · cos ψ
//     down-the-line    d_dtl = s_D · L · cos τ · cos(ψ − γ)
//
// — the ideal complement s_D·L·cos τ·sin ψ only when γ is exactly 90°. Both cameras are level, so
// both see the same vertical extent up to their scales, which is what fixes s_D/s_F.
//
// Keypoint ORDER matches makePose (left at the smaller x), so the route's own orientation bits
// have to find the signs; a left-hander reads the same frames with lead and trail swapped.
// `unsignedSeparation` renders |d_dtl| instead — the gate-G1 control.
static PoseTrack2D makeDtlPose(double pelvisStartClosedDeg = 45.0, double addrOpenDeg = 0.0,
                               const Bump &pelvisBump = kPelvis, double shoulderTiltDeg = 0.0,
                               bool unsignedSeparation = false, double scale = kDtlScale,
                               float conf = 0.9f, double vertExtentScale = 1.0,
                               double thoraxStartClosedDeg = 90.0, const Bump &thoraxBump = kThorax)
{
    PoseTrack2D pose;
    const double g = kGammaDeg * kD2R;
    for (int64_t t = kDtlOffsetUs; t <= kFinishUs; t += kDtlStepUs) {
        const double s = t * 1e-6;
        PoseFrame2D f;
        f.t_us = t;
        const double hip = closedAngleDeg(pelvisBump, pelvisStartClosedDeg, s, addrOpenDeg) * kD2R;
        const double sh  = closedAngleDeg(thoraxBump, thoraxStartClosedDeg, s, addrOpenDeg) * kD2R;
        const double tau = tiltAt(s, shoulderTiltDeg) * kD2R;
        double hd = scale * kHipSpanPx * std::cos(hip - g);
        double sd = scale * kShSpanPx * std::cos(tau) * std::cos(sh - g);
        if (unsignedSeparation) { hd = std::fabs(hd); sd = std::fabs(sd); }
        const double sy = 0.5 * scale * kShSpanPx * std::sin(tau) / kHD;
        f.kp[kLHip] = QPointF(0.5 - 0.5 * hd / kWD, 0.55); f.conf[kLHip] = conf;
        f.kp[kRHip] = QPointF(0.5 + 0.5 * hd / kWD, 0.55); f.conf[kRHip] = conf;
        f.kp[kLSh]  = QPointF(0.5 - 0.5 * sd / kWD, 0.30 - sy); f.conf[kLSh] = conf;
        f.kp[kRSh]  = QPointF(0.5 + 0.5 * sd / kWD, 0.30 + sy); f.conf[kRSh] = conf;
        const double vy = scale * kVertExtentPx * vertExtentScale / kHD;
        f.kp[kLAnk] = QPointF(0.48, 0.30 + vy); f.conf[kLAnk] = conf;
        f.kp[kRAnk] = QPointF(0.52, 0.30 + vy); f.conf[kRAnk] = conf;
        pose.frames.push_back(f);
    }
    return pose;
}

// PLANT A LEFT/RIGHT RELABEL. What a pose model does with the golfer's back to the lens: it swaps
// the two keypoints outright, so the separation changes sign with its MAGNITUDE INTACT. `alternate`
// swaps every other frame instead — the flutter the guard must refuse to read either way.
static void plantSwap(PoseTrack2D &pose, int a, int b, int64_t fromUs, int64_t toUs,
                      bool alternate = false)
{
    // BOTH TIERS. The route reads raw to DETECT and smoothed to CONSUME, so a fixture that plants
    // into one of them is testing nothing — that is a real trap and it caught this test first.
    for (std::vector<PoseFrame2D> *v : { &pose.frames, &pose.smoothed }) {
        int k = 0;
        for (PoseFrame2D &f : *v) {
            if (f.t_us < fromUs || f.t_us > toUs) continue;
            if (alternate && (k++ % 2)) continue;
            std::swap(f.kp[size_t(a)], f.kp[size_t(b)]);
            std::swap(f.conf[size_t(a)], f.conf[size_t(b)]);
        }
    }
}

// PLANT A BURST OF KEYPOINT NONSENSE. Not a relabel — the magnitude is wrong too, alternating by
// ±100 px frame to frame, which is what the corpus's face-on shoulders do through their own
// square-up (−121, +12, −108, −26, +96 px in 27 ms). No parity rule reaches it; only a bound on
// how fast a rigid line can change its separation does.
static void plantBurst(PoseTrack2D &pose, int a, int b, int64_t fromUs, int64_t toUs, double px)
{
    for (std::vector<PoseFrame2D> *v : { &pose.frames, &pose.smoothed }) {
        int k = 0;
        for (PoseFrame2D &f : *v) {
            if (f.t_us < fromUs || f.t_us > toUs) continue;
            const double e = ((k++ % 2) ? -px : px) * 0.5 / kW;
            f.kp[size_t(a)] = QPointF(f.kp[size_t(a)].x() - e, f.kp[size_t(a)].y());
            f.kp[size_t(b)] = QPointF(f.kp[size_t(b)].x() + e, f.kp[size_t(b)].y());
        }
    }
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
        // …and IS the derivative. The bound above held for a gain with its sine and cosine terms
        // swapped, which is what shipped until 2026-09-21: inside [k, 1/k] at every angle and wrong
        // at all but one of them.
        bool deriv = true;
        for (double psi = -3.0; psi <= 3.0; psi += 0.07) {
            const double h = 1e-6, nu = kNodeDeg * kD2R;
            const double num = (deprojectPlaneAngle(psi + h, nu, kRatio) - deprojectPlaneAngle(psi - h, nu, kRatio)) / (2 * h);
            if (!near(deprojectGain(psi, nu, kRatio), num, 1e-5)) deriv = false;
        }
        CHECK("§1 the gain is dα/dψ of the de-projection, checked numerically", deriv);
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
        // (e) A pelvis that never reaches the edge of sight — 15° closed at the top — was blind
        //     from the transition on. "No earlier than the transition" is the whole domain, so no
        //     bound is claimed: the node reads not in sight.
        const SegmentRatesResult never = run(15.0, 0.0);
        const KsNode *np = nodeOf(never, SeqSegment::Pelvis);
        CHECK("§8e never in sight: not placed and NO bound", np && !np->placed && !np->bounded());
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

    // ── §9 the paired face-on + down-the-line route ───────────────────────────────────────────
    {
        const ShaftTrack2D shaft = makeShaft();
        // One helper: build the inputs for a fixture, with or without the second camera.
        struct Fix { PoseTrack2D fo, dtl; };
        const auto build = [&](double pelvisStart, const Bump &pb, double tiltDeg, bool leadIsLeft,
                               bool unsignedDtl = false, double scale = kDtlScale, float conf = 0.9f,
                               double vertScale = 1.0, double thoraxStart = 90.0,
                               const Bump &tb = kThorax) {
            Fix fx;
            fx.fo  = makePose(leadIsLeft, pelvisStart, 0.0, pb, tiltDeg, /*signedSpans*/ true,
                              thoraxStart, tb);
            fx.dtl = makeDtlPose(pelvisStart, 0.0, pb, tiltDeg, unsignedDtl, scale, conf, vertScale,
                                 thoraxStart, tb);
            // A smoothed tier identical to the raw one, so the source SELECTION is exercised
            // without moving a single number: a view with no relabel must consume "smoothed".
            fx.fo.smoothed  = fx.fo.frames;
            fx.dtl.smoothed = fx.dtl.frames;
            return fx;
        };
        // The SHIPPED default leaves the thorax unplaced (pp_tuned_constants.h says why, on the
        // corpus's evidence). Everything in §9 that is about the geometry rather than about that
        // gate opens it explicitly, so the geometry stays pinned if the gate is ever reopened.
        SegmentRatesConfig cfgT = cfg;
        cfgT.pairTrunkThoraxPlacement = true;
        const auto runPair = [&](const Fix &fx, bool leadIsLeft, const SegmentRatesConfig &c,
                                 bool withDtl) {
            SegmentRatesInputs in;
            in.pose = &fx.fo; in.frameW = kW; in.frameH = kH; in.leadIsLeft = leadIsLeft;
            if (withDtl) { in.poseDtl = &fx.dtl; in.dtlFrameW = kWD; in.dtlFrameH = kHD; }
            in.shaft = &shaft; in.phases = &ph; in.impactUs = kImpactUs;
            return buildSegmentRates(in, c);
        };
        const auto show = [](const char *tag, const SegmentRatesResult &r) {
            for (SeqSegment sg : { SeqSegment::Pelvis, SeqSegment::Thorax }) {
                const KsNode *n = r.sequence.node(sg);
                if (!n) { std::printf("    %s %-6s (no node)\n", tag, seqSegmentKey(sg)); continue; }
                std::printf("    %s %-6s %-8s route %-10s t=%.1f ms before impact ±%.1f peak %.0f"
                            " bound≥%.1f\n", tag, seqSegmentKey(sg),
                            n->placed ? "placed" : "UNPLACED", qPrintable(n->routeId),
                            n->beforeImpactMs, n->tSigmaMs, n->peakDps, n->peakNoEarlierThanMs);
            }
        };

        // (a) The pair fires and places BOTH trunk nodes on the truth — including the 45°-start
        //     pelvis whose peak sits inside the face-on blind band, which §2 and §8a can only
        //     bound. That is the whole case for the rung.
        const Fix base = build(45.0, kPelvis, 0.0, true);
        const SegmentRatesResult pair = runPair(base, true, cfgT, true);
        show("§9a", pair);
        const KsNode *pp = nodeOf(pair, SeqSegment::Pelvis), *pt = nodeOf(pair, SeqSegment::Thorax);
        CHECK("§9a pair: pelvis and thorax both take the faceOn+dtl route, Estimated",
              pp && pt && pp->routeId == QLatin1String("faceOn+dtl")
              && pt->routeId == QLatin1String("faceOn+dtl")
              && !pair.pelvis.direct && !pair.thorax.direct);
        CHECK("§9a pair: the arm and club still come off the face-on camera",
              pair.leadArm.routeId == QLatin1String("faceOn")
              && pair.club.routeId == QLatin1String("faceOnClub"));
        CHECK("§9a pair: the 45°-start pelvis — bounded by face-on — is PLACED within 8 ms",
              pp && pp->placed && msFromTruth(pp, kPelvis) <= 8.0);
        CHECK("§9a pair: the thorax is placed within 8 ms", pt && pt->placed && msFromTruth(pt, kThorax) <= 8.0);
        CHECK("§9a pair: no bound on a placed node", pp && pt && !pp->bounded() && !pt->bounded());
        CHECK("§9a pair: both trunk peaks are opening-POSITIVE", pp && pt && pp->peakDps > 0.0 && pt->peakDps > 0.0);
        CHECK("§9a pair: no relabel in either view ⇒ BOTH legs consume the smoothed track",
              pair.pair.pelvis.srcFo == QLatin1String("smoothed")
              && pair.pair.pelvis.srcDtl == QLatin1String("smoothed")
              && pair.pair.pelvis.nSwapsFo == 0 && pair.pair.pelvis.nRateLimitedFo == 0);
        CHECK("§9a pair: the diagnostics report the scale it measured and a consistent pairing",
              pair.pair.attempted && pair.pair.refusal.isEmpty()
              && near(pair.pair.rVertical, kDtlScale, 0.02)
              && pair.pair.pelvis.corrAbs >= cfg.pairMinCorr
              && pair.pair.thorax.corrAbs >= cfg.pairMinCorr && pair.pair.pelvis.nPaired > 10);
        std::printf("    §9a diag: rVert %.3f (%.0f/%.0f px, n%d)  pelvis corr %.2f closure %.3f/%.3f"
                    " rEll %.3f sgn %d/%d  thorax corr %.2f closure %.3f/%.3f\n",
                    pair.pair.rVertical, pair.pair.extentFoPx, pair.pair.extentDtlPx,
                    pair.pair.addrSamples, pair.pair.pelvis.corrAbs, pair.pair.pelvis.closureP50,
                    pair.pair.pelvis.closureP90, pair.pair.pelvis.rEllipse, pair.pair.pelvis.signFo,
                    pair.pair.pelvis.signDtl, pair.pair.thorax.corrAbs, pair.pair.thorax.closureP50,
                    pair.pair.thorax.closureP90);

        // (b) THE TILT. The shoulder line leaves horizontal by 30° through the downswing. A 2-D
        //     span distance cannot separate that from turn — the span stops shrinking — while the
        //     ratio of the two signed horizontal separations divides cos τ out exactly.
        //     ⚠ WHAT THIS ACTUALLY SHOWED, recorded rather than engineered away: on this fixture
        //     the tilt moves the span route's thorax MAGNITUDE (629 against the pair's 596 and a
        //     truth of 727 before the γ bias) but not its peak TIME, which lands on the truth as
        //     well as the pair's does. The reference width absorbs a slowly-varying inflation of
        //     the span, and the argmax survives it. So the assertion is on the pair — the claim
        //     the rung has to earn — and the control is PRINTED, not asserted against.
        const Fix tilted = build(45.0, kPelvis, 30.0, true);
        const SegmentRatesResult tPair = runPair(tilted, true, cfgT, true);
        const SegmentRatesResult tSpan = runPair(tilted, true, cfgT, false);   // the control
        show("§9b pair", tPair);
        show("§9b span", tSpan);
        const KsNode *tp = nodeOf(tPair, SeqSegment::Thorax), *ts = nodeOf(tSpan, SeqSegment::Thorax);
        std::printf("    §9b thorax: pair %.1f ms off the truth (placed=%d); the 2-D-distance "
                    "control %.1f ms off (placed=%d)\n",
                    tp ? msFromTruth(tp, kThorax) : -1.0, tp ? int(tp->placed) : 0,
                    ts ? msFromTruth(ts, kThorax) : -1.0, ts ? int(ts->placed) : 0);
        CHECK("§9b tilted shoulders: the pair thorax is still placed within 8 ms of the truth",
              tp && tp->placed && tp->routeId == QLatin1String("faceOn+dtl")
              && msFromTruth(tp, kThorax) <= 8.0);

        // (c) A TRUNK THAT PEAKS AFTER THE BALL. The extremum inside [transition, impact] sits at
        //     impact with the rate still climbing into it. The honest output is not a node at the
        //     edge: it is "did not peak before impact".
        const Bump late { 480.0, 1.030, 0.050 };
        const Fix rising = build(45.0, late, 0.0, true);
        const SegmentRatesResult rPair = runPair(rising, true, cfgT, true);
        show("§9c", rPair);
        const KsNode *rp = nodeOf(rPair, SeqSegment::Pelvis);
        CHECK("§9c still rising at impact: the pelvis node is UNPLACED", rp && !rp->placed);
        CHECK("§9c still rising at impact: peakNoEarlierThanMs == 0 — 'not before impact'",
              rp && rp->peakNoEarlierThanMs == 0.0);
        CHECK("§9c still rising at impact: no LATER bound is claimed",
              rp && !std::isfinite(rp->peakNoLaterThanMs));

        // (d) GATE G1 — THE OBSERVABLE MUST BE SIGNED. A fixture whose pelvis squares up well
        //     before impact: from 10° closed, the line crosses square ~48 ms before its rate
        //     peaks. Rendered UNSIGNED, the down-the-line separation has a V at square and the
        //     angle a kink, and the derivative reports a peak there. The production path must not.
        const double squareS = [&] {
            double best = 0.0, bestAbs = 1e9;
            for (double u = kTopUs * 1e-6; u <= kImpactUs * 1e-6; u += 0.0005) {
                const double a = std::fabs(closedAngleDeg(kPelvis, 10.0, u));
                if (a < bestAbs) { bestAbs = a; best = u; }
            }
            return best;
        }();
        const Fix g1signed   = build(10.0, kPelvis, 0.0, true, /*unsignedDtl*/ false);
        const Fix g1unsigned = build(10.0, kPelvis, 0.0, true, /*unsignedDtl*/ true);
        const SegmentRatesResult gS = runPair(g1signed, true, cfg, true);
        const SegmentRatesResult gU = runPair(g1unsigned, true, cfg, true);
        const KsNode *gsp = nodeOf(gS, SeqSegment::Pelvis), *gup = nodeOf(gU, SeqSegment::Pelvis);
        const double sqMs = squareS * 1e3;
        std::printf("    §9d square-up at %.1f ms, truth peak at %.1f ms; signed node %.1f ms"
                    " (placed=%d), UNSIGNED control %.1f ms (placed=%d)\n",
                    sqMs, kPelvis.tPeakS * 1e3,
                    gsp ? double(gsp->tPeakUs) * 1e-3 : -1.0, gsp ? int(gsp->placed) : 0,
                    gup ? double(gup->tPeakUs) * 1e-3 : -1.0, gup ? int(gup->placed) : 0);
        // ⚠ WHAT THIS ASSERTED BEFORE K1d, and why it no longer can. Up to K1c the UNSIGNED
        //   control PLACED a node at 866.6 ms — 2 ms from the square-up — which is the artefact
        //   gate G1 is named for, and the assertion was that the control shows it while the signed
        //   production path does not. K1d's guards now refuse the unsigned fixture outright
        //   (printed below, with its bound), so the control can no longer DEMONSTRATE the
        //   artefact. That is the guards working and it is recorded rather than manufactured: the
        //   assertion that matters — the signed path does not put its node at square, and does put
        //   it on the truth — is untouched and is what G1 actually asked for.
        std::printf("    §9d unsigned control now: placed=%d bound≥%.1f σt %.1f ms\n",
                    gup ? int(gup->placed) : 0, gup ? gup->peakNoEarlierThanMs : -1.0,
                    gup ? gup->tSigmaMs : -1.0);
        CHECK("§9d G1: the unsigned observable never yields a PLACED node at square",
              !gup || !gup->placed || std::fabs(double(gup->tPeakUs) * 1e-3 - sqMs) > 10.0);
        CHECK("§9d G1: the SIGNED production path does not put its node at square",
              gsp && std::fabs(double(gsp->tPeakUs) * 1e-3 - sqMs) > 10.0);
        CHECK("§9d G1: the signed path places on the truth instead",
              gsp && gsp->placed && msFromTruth(gsp, kPelvis) <= 8.0);

        // (e) HANDEDNESS. The same frames read with lead and trail swapped: the route's own
        //     orientation bits must mirror, so the left-hander's trunk reads opening-positive and
        //     places at the same instant.
        const SegmentRatesResult lh = runPair(build(45.0, kPelvis, 0.0, false), false, cfgT, true);
        show("§9e", lh);
        const KsNode *lp = nodeOf(lh, SeqSegment::Pelvis), *lt = nodeOf(lh, SeqSegment::Thorax);
        CHECK("§9e left-hander: the pair mirrors — trunk opening-POSITIVE and placed on the truth",
              lp && lt && lp->placed && lt->placed && lp->peakDps > 0.0 && lt->peakDps > 0.0
              && msFromTruth(lp, kPelvis) <= 8.0 && msFromTruth(lt, kThorax) <= 8.0);
        CHECK("§9e left-hander: the two orientation bits came out opposite to the right-hander's",
              lh.pair.pelvis.signFo == -pair.pair.pelvis.signFo
              && lh.pair.pelvis.signDtl == -pair.pair.pelvis.signDtl);

        // (f) NO SECOND CAMERA, AND THE DARK SWITCH. §2 must be exactly what it was.
        const SegmentRatesResult noDtl = runPair(base, true, cfg, false);
        SegmentRatesConfig off = cfg;
        off.pairTrunkEnabled = false;
        const SegmentRatesResult dark = runPair(base, true, off, true);
        bool sameAsToday = noDtl.sequence.nodes.size() == dark.sequence.nodes.size();
        for (size_t i = 0; sameAsToday && i < dark.sequence.nodes.size(); ++i) {
            const KsNode &a = noDtl.sequence.nodes[i], &b = dark.sequence.nodes[i];
            sameAsToday = a.segment == b.segment && a.routeId == b.routeId && a.placed == b.placed
                       && a.tPeakUs == b.tPeakUs && a.peakDps == b.peakDps;
        }
        CHECK("§9f no down-the-line pose: the trunk is back on the face-on span rung",
              noDtl.pelvis.routeId == QLatin1String("faceOn")
              && noDtl.thorax.routeId == QLatin1String("faceOn")
              && noDtl.club.routeId == QLatin1String("faceOnClub") && !noDtl.pair.attempted);
        CHECK("§9f sequence.pairTrunk.enabled=false reproduces that exactly", sameAsToday);
        CHECK("§9f …and the pair's own placement switch is NOT faceOnTrunkPlacement", [&] {
            SegmentRatesConfig noPlace = cfg;
            noPlace.pairTrunkPlacement = false;
            const SegmentRatesResult r = runPair(base, true, noPlace, true);
            const KsNode *a = nodeOf(r, SeqSegment::Pelvis);
            SegmentRatesConfig noSpan = cfg;
            noSpan.faceOnTrunkPlacement = false;      // the SPAN rung's gate — must not reach here
            const SegmentRatesResult r2 = runPair(base, true, noSpan, true);
            const KsNode *b = nodeOf(r2, SeqSegment::Pelvis);
            return a && b && !a->placed && b->placed;
        }());

        // (j) THE SHIPPED DEFAULT, and the fall-through that protects the reader. With the thorax
        //     ring switched off this fixture's thorax has no bound either (its rate peaks inside
        //     the domain and falls away), so the pair would be telling the reader LESS than the
        //     span rung does — and the route withdraws rather than do that. The pelvis is
        //     untouched by that switch.
        {
            const SegmentRatesResult ship = runPair(base, true, cfg, true);
            const KsNode *sp = nodeOf(ship, SeqSegment::Pelvis), *stn = nodeOf(ship, SeqSegment::Thorax);
            CHECK("§9j shipped default: a thorax with neither ring nor bound falls through to faceOn",
                  ship.thorax.produced() && ship.thorax.routeId == QLatin1String("faceOn")
                  && stn && stn->routeId == QLatin1String("faceOn"));
            CHECK("§9j …and the pelvis still places on the pair exactly as with the gate open",
                  sp && pp && sp->placed && sp->tPeakUs == pp->tPeakUs
                  && sp->routeId == QLatin1String("faceOn+dtl"));
        }

        // (ii) A BURST OF KEYPOINT NONSENSE on the face-on shoulders near the Top: ±100 px frame
        //      to frame against a 98 px rigid-body bound. The samples go; what is left still
        //      answers the question.
        {
            // A thorax that peaks after the ball AND actually turns on the way there. A narrower
            // bump leaves it 65–90° closed all through the domain, where the down-the-line
            // separation is almost constant, and the pairing gate refuses it for that reason
            // rather than for the one under test — which is what the first cut of this fixture
            // did, and is worth naming so nobody re-introduces it.
            const Bump lateT { 727.0, 1.030, 0.090 };
            Fix risingT = build(45.0, kPelvis, 0.0, true, false, kDtlScale, 0.9f, 1.0, 90.0, lateT);
            plantBurst(risingT.fo, kLSh, kRSh, 699000, 734000, 100.0);
            const SegmentRatesResult rr = runPair(risingT, true, cfgT, true);
            const KsNode *rt = nodeOf(rr, SeqSegment::Thorax);
            std::printf("    §9ii-burst rising: rateLimited %d, invalidFrac %.3f, thorax %s"
                        " bound≥%.1f peak %.0f route %s\n",
                        rr.pair.thorax.nRateLimitedFo, rr.pair.thorax.invalidFrac,
                        rt ? (rt->placed ? "placed" : "UNPLACED") : "none",
                        rt ? rt->peakNoEarlierThanMs : -1.0, rt ? rt->peakDps : 0.0,
                        rt ? qPrintable(rt->routeId) : "-");
            // The burst is not a relabel, so nothing is flipped and the leg is still read from
            // the smoothed tier — the rate limit is what has to catch it, and does.
            CHECK("§9ii burst: the rate limit fired on the smoothed face-on leg",
                  rr.pair.thorax.nRateLimitedFo > 0
                  && rr.pair.thorax.srcFo == QLatin1String("smoothed"));
            CHECK("§9ii burst + peaks after impact: unplaced, bounded 'not before impact'",
                  rt && !rt->placed && rt->peakNoEarlierThanMs == 0.0
                  && rt->routeId == QLatin1String("faceOn+dtl"));
            CHECK("§9ii burst: no spike node survives it",
                  rt && std::fabs(rt->peakDps) < 1.5 * kThorax.peakDps);

            Fix clearT = build(45.0, kPelvis, 0.0, true);   // thorax peaks 75 ms before impact
            plantBurst(clearT.fo, kLSh, kRSh, 699000, 734000, 100.0);
            const SegmentRatesResult cr = runPair(clearT, true, cfgT, true);
            const KsNode *ct2 = nodeOf(cr, SeqSegment::Thorax);
            std::printf("    §9ii-burst clear: rateLimited %d, invalidFrac %.3f, thorax %s %.1f ms"
                        " (truth 75.0) peak %.0f\n", cr.pair.thorax.nRateLimitedFo,
                        cr.pair.thorax.invalidFrac, ct2 ? (ct2->placed ? "placed" : "UNPLACED") : "none",
                        ct2 ? ct2->beforeImpactMs : -1.0, ct2 ? ct2->peakDps : 0.0);
            CHECK("§9ii burst clear of the peak: the thorax still places within 8 ms of the truth",
                  ct2 && ct2->placed && msFromTruth(ct2, kThorax) <= 8.0);
        }

        // (iii) THE PLACEMENT KNOBS GATE THE RING, NEVER THE BOUND.
        {
            SegmentRatesConfig noRing = cfg;
            noRing.pairTrunkPlacement = false;
            const SegmentRatesResult r = runPair(rising, true, noRing, true);
            const KsNode *n = nodeOf(r, SeqSegment::Pelvis);
            CHECK("§9iii placement off: the pelvis bound is still emitted on the pair route",
                  n && !n->placed && n->peakNoEarlierThanMs == 0.0
                  && n->routeId == QLatin1String("faceOn+dtl"));
        }

        // (iv) NEITHER A RING NOR A BOUND ⇒ the span rung runs instead, for that segment on that
        //      swing. The reader never gets less than today because the pair was tried.
        {
            SegmentRatesConfig strict = cfgT;
            strict.maxPlaceSigmaMs = 0.0;          // nothing can be placed, and nothing is bounded
            const SegmentRatesResult r = runPair(base, true, strict, true);
            CHECK("§9iv a pair channel with neither falls through to the face-on span rung",
                  r.pelvis.routeId == QLatin1String("faceOn")
                  && r.thorax.routeId == QLatin1String("faceOn")
                  && r.pelvis.produced() && r.thorax.produced());
        }

        // (g) The refusals, each falling through to face-on rather than to nothing.
        const auto fellThrough = [&](const SegmentRatesResult &r) {
            return r.pelvis.produced() && r.pelvis.routeId == QLatin1String("faceOn")
                && r.thorax.produced() && r.thorax.routeId == QLatin1String("faceOn");
        };
        {
            const SegmentRatesResult lowConf =
                runPair(build(45.0, kPelvis, 0.0, true, false, kDtlScale, 0.1f), true, cfg, true);
            CHECK("§9g down-the-line keypoints below the confidence gate ⇒ face-on",
                  fellThrough(lowConf));
            // The golfer imaged 6× smaller down the line: 67 px of vertical extent is not a scale.
            const SegmentRatesResult tiny =
                runPair(build(45.0, kPelvis, 0.0, true, false, 1.0 / 6.0, 0.9f), true, cfg, true);
            CHECK("§9g a vertical extent below the floor ⇒ face-on, with the reason recorded",
                  fellThrough(tiny) && !tiny.pair.refusal.isEmpty());
            std::printf("    §9g scale refusal: %s\n", qPrintable(tiny.pair.refusal));
            // A down-the-line stream that is not this swing: the separations hold still.
            Fix wrong = base;
            for (PoseFrame2D &f : wrong.dtl.frames) {
                f.kp[kLHip] = QPointF(0.45, 0.55); f.kp[kRHip] = QPointF(0.55, 0.55);
                f.kp[kLSh]  = QPointF(0.44, 0.30); f.kp[kRSh]  = QPointF(0.56, 0.30);
            }
            wrong.dtl.smoothed = wrong.dtl.frames;
            const SegmentRatesResult mismatched = runPair(wrong, true, cfg, true);
            CHECK("§9g a down-the-line separation that does not track the turn ⇒ face-on",
                  fellThrough(mismatched));
            std::printf("    §9g corr refusal: %s\n", qPrintable(mismatched.pair.pelvis.refusal));
        }

        // (i) A PLANTED RELABEL. The face-on shoulder keypoints are swapped outright over a 60 ms
        //     run before the Top — a sign change with the magnitude intact, which is what a pose
        //     model does with the golfer's back to the lens. Two flips (in and out of the run) are
        //     undone and the node must not move.
        {
            Fix planted = build(45.0, kPelvis, 0.0, true, false, kDtlScale, 0.9f, 1.0,
                                /*thoraxStart*/ 45.0);
            const SegmentRatesResult clean = runPair(planted, true, cfgT, true);
            plantSwap(planted.fo, kLSh, kRSh, 600000, 660000);
            const SegmentRatesResult swapped = runPair(planted, true, cfgT, true);
            const KsNode *c = nodeOf(clean, SeqSegment::Thorax), *w = nodeOf(swapped, SeqSegment::Thorax);
            std::printf("    §9i planted relabel: clean %.1f ms peak %.0f (swaps %d) → swapped %.1f ms"
                        " peak %.0f (swaps %d, dropped %d)\n",
                        c ? c->beforeImpactMs : -1.0, c ? c->peakDps : 0.0, clean.pair.thorax.nSwapsFo,
                        w ? w->beforeImpactMs : -1.0, w ? w->peakDps : 0.0, swapped.pair.thorax.nSwapsFo,
                        swapped.pair.thorax.nSwapFramesDropped);
            CHECK("§9i planted relabel: exactly two flips are counted in the face-on view",
                  swapped.pair.thorax.nSwapsFo == 2 && clean.pair.thorax.nSwapsFo == 0);
            CHECK("§9i planted relabel: the thorax node is unmoved within 4 ms",
                  c && w && std::llabs(c->tPeakUs - w->tPeakUs) <= 4000 && c->placed == w->placed);
            CHECK("§9i planted relabel: and the peak magnitude is unmoved within 2 %",
                  c && w && near(c->peakDps, w->peakDps, 0.02 * std::fabs(c->peakDps)));
        }

        // (ii) A GENUINE PAST-90° TURN. The shoulders start 100° closed, so the face-on separation
        //      really does cross zero — through a COLLAPSE, as kinematics requires. Nothing may be
        //      flagged, ψ must run continuously through 90°, and the node must still land.
        {
            const Fix past90 = build(45.0, kPelvis, 0.0, true, false, kDtlScale, 0.9f, 1.0,
                                     /*thoraxStart*/ 100.0);
            const SegmentRatesResult r = runPair(past90, true, cfgT, true);
            const KsNode *n = nodeOf(r, SeqSegment::Thorax);
            std::printf("    §9ii past-90°: thorax %s %.1f ms (truth %.1f) peak %.0f, swaps %d,"
                        " dropped %d\n", n && n->placed ? "placed" : "UNPLACED",
                        n ? n->beforeImpactMs : -1.0, (kImpactUs * 1e-6 - kThorax.tPeakS) * 1e3,
                        n ? n->peakDps : 0.0, r.pair.thorax.nSwapsFo, r.pair.thorax.nSwapFramesDropped);
            CHECK("§9ii a real crossing of square is NOT read as a relabel",
                  r.pair.thorax.nSwapsFo == 0 && r.pair.thorax.nSwapFramesDropped == 0);
            // 10 ms, not §9a's 8: γ = 80° reparametrises ψ smoothly, and the argmax of the
            //  measured rate moves with the LOCAL gain g(ψ) = sin γ / (1 + cos γ · cos(2ψ − γ)).
            //  A shoulder line 100° closed at the top peaks at a different ψ from one 90° closed,
            //  where g' is larger, and the shift comes out at one 8.33 ms frame rather than a
            //  fraction of one. It is the level bias doing what §5.2 says it does and it is
            //  reported (the printf above carries the number), not a tolerance tuned to pass.
            CHECK("§9ii …and the thorax node still places within 10 ms of the truth",
                  n && n->placed && msFromTruth(n, kThorax) <= 10.0);
            CHECK("§9ii …with a credible peak (the angle ran through 90°, it did not step)",
                  n && std::fabs(n->peakDps) < 1.5 * kThorax.peakDps);
        }

        // (iii) RAPID ALTERNATION. Labels flapping every other frame for 120 ms: not a golfer, and
        //       not something to correct. Those frames go, and whatever is left must not report a
        //       club-sized thorax.
        {
            Fix flutter = build(45.0, kPelvis, 0.0, true, false, kDtlScale, 0.9f, 1.0,
                                /*thoraxStart*/ 45.0);
            plantSwap(flutter.fo, kLSh, kRSh, 780000, 900000, /*alternate*/ true);
            const SegmentRatesResult r = runPair(flutter, true, cfgT, true);
            const KsNode *n = nodeOf(r, SeqSegment::Thorax);
            std::printf("    §9iii flutter: swaps %d, dropped %d, thorax %s %.1f ms peak %.0f (%s)\n",
                        r.pair.thorax.nSwapsFo, r.pair.thorax.nSwapFramesDropped,
                        n ? (n->placed ? "placed" : "UNPLACED") : "none",
                        n ? n->beforeImpactMs : -1.0, n ? n->peakDps : 0.0,
                        qPrintable(r.pair.thorax.refusal.isEmpty() ? r.thorax.routeId
                                                                   : r.pair.thorax.refusal));
            CHECK("§9iii flutter: the alternating frames are dropped, not trusted",
                  r.pair.thorax.nSwapFramesDropped > 0);
            CHECK("§9iii flutter: no club-sized thorax spike survives",
                  !n || std::fabs(n->peakDps) < 1.5 * kThorax.peakDps);
        }

        // (h) Determinism.
        {
            const SegmentRatesResult a = runPair(base, true, cfgT, true);
            const SegmentRatesResult b = runPair(base, true, cfgT, true);
            bool same = a.sequence.nodes.size() == b.sequence.nodes.size()
                     && a.pelvis.series.value.size() == b.pelvis.series.value.size();
            for (size_t i = 0; same && i < a.sequence.nodes.size(); ++i)
                same = a.sequence.nodes[i].tPeakUs == b.sequence.nodes[i].tPeakUs
                    && a.sequence.nodes[i].peakDps == b.sequence.nodes[i].peakDps
                    && a.sequence.nodes[i].tSigmaMs == b.sequence.nodes[i].tSigmaMs;
            for (size_t i = 0; same && i < a.pelvis.series.value.size(); ++i)
                same = a.pelvis.series.value[i] == b.pelvis.series.value[i];
            CHECK("§9h two runs of the same inputs are bit-identical", same);
        }
    }

    // ── §10 the club through the FUSED two-camera plane ───────────────────────────────────────
    // The face-on track's own ellipse plane is an inference, and on 07-04 it wandered. Give the
    // fixture a WRONG ellipse (0.97 @ 40°, against the true 0.87 @ 10°): the club's peak rate
    // reads wrong through it, and right again through the fused plane — which moves the club
    // and its route id, and nothing else.
    {
        const PoseTrack2D pose = makePose(true);
        ShaftTrack2D shaft = makeShaft();
        shaft.plane.measured.ratioDown   = 0.97;
        shaft.plane.measured.nodeDownDeg = 40.0;
        SegmentRatesInputs in;
        in.pose = &pose; in.frameW = kW; in.frameH = kH; in.leadIsLeft = true;
        in.shaft = &shaft; in.phases = &ph; in.impactUs = kImpactUs;
        const SegmentRatesResult wrong = buildSegmentRates(in, cfg);
        in.fusedClubPlane.have    = true;
        in.fusedClubPlane.ratio   = kRatio;
        in.fusedClubPlane.nodeRad = kNodeDeg * kD2R;
        const SegmentRatesResult fused = buildSegmentRates(in, cfg);
        const KsNode *cw = nodeOf(wrong, SeqSegment::Club), *cf = nodeOf(fused, SeqSegment::Club);
        CHECK("§10 fused plane: the club route reads faceOn+dtl, the ellipse route faceOnClub",
              fused.club.routeId == QLatin1String("faceOn+dtl") && wrong.club.routeId == QLatin1String("faceOnClub")
              && cf && cf->routeId == QLatin1String("faceOn+dtl"));
        CHECK("§10 fused plane: club peak within 8 % of 2254 °/s",
              cf && near(cf->peakDps, kClub.peakDps, 0.08 * kClub.peakDps));
        CHECK("§10 fused plane: and nearer the truth than through the wrong ellipse",
              cf && cw && std::fabs(cf->peakDps - kClub.peakDps) < std::fabs(cw->peakDps - kClub.peakDps));
        bool armSame = fused.leadArm.series.value.size() == wrong.leadArm.series.value.size();
        for (size_t i = 0; armSame && i < fused.leadArm.series.value.size(); ++i)
            armSame = fused.leadArm.series.value[i] == wrong.leadArm.series.value[i];
        CHECK("§10 fused plane: the lead arm is not moved onto it", armSame);
        in.fusedClubPlane.ratio = 0.1;     // below planeRatioFloor (0.20): not a plane to de-project through
        CHECK("§10 a fused ratio below the floor falls back to the ellipse",
              buildSegmentRates(in, cfg).club.routeId == QLatin1String("faceOnClub"));
    }

    std::printf(g_fail ? "FAILED (%d)\n" : "OK\n", g_fail);
    return g_fail ? 1 : 0;
}
