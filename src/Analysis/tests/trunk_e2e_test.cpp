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

// The END-TO-END trunk test (trunk_imu_sequence_grf_design.md §14.1) — the test whose absence let
// the frame defect (design §5.1) survive: every trunk consumer read world Z as vertical while the
// limb solve handed them a Y-down world, and each consumer's own test built its q_anat by hand in
// the frame that consumer assumed, so all of them passed.
//
// Here nothing is built in the consumer's frame. A synthetic body stands tall, bends to address and
// swings; two sensors ride it at ARBITRARY mounts (the nominal "USB down, label out" plus a tilt of up
// to 20° and, in §c, a mount yaw δ about the spine); the fusion world has an arbitrary heading and
// is Madgwick's (gravity seed → identity) or ESKF's (180° about X). From that we generate what the
// device and the filter would hand us — raw sensor→world quaternions, sensor-frame gyro from the
// quaternion derivative, gravity-up accel — and then run the REAL chain:
//
//   solveTrunkSegment on the stand-tall window → A, M → q_anat = toAnatomical(A, q_raw, M) →
//   SegmentStreams (gyro = M⁻¹·gyro_sensor) → body_rotation, segment_rates, phase_segmenter and the
//   skeleton3d IMU term.
//
//   §a  the solve: q_anat is the identity at stand tall and IS the true segment orientation in the
//       stance frame; forward bend and turn read back the truth; the ESKF world gives the same q_anat.
//   §b  the consumers recover the motion: turn at the top, the S-I peak value and instant and the
//       pelvis-before-thorax order, the transition from the world-Z pelvis rate, and the skeleton3d
//       term's residual (≈ 0 for the true pose, growing for a pose turned away; a TILTED world —
//       the defect — costs at least the tilt).
//   §c  design §6.2's δ table, measured on the chain: S-I rates and peak times unchanged by δ = 10°;
//       turn error atan(tan δ·cos θ) "changing only as θ changes" under the table's own model (a
//       turn about world vertical) — and what it is when the trunk turns about its own bent spine;
//       obliquity at address asin(sin θ·sin δ); forward bend untouched.
//   §d  a left-hander's mirrored swing: the same lead-relative rates and turn magnitudes.
//   §e  a fusion-yaw drift between stand tall and the swing changes nothing measured from address.
//   §f  the gates on chain-generated windows, and the one mis-mount stand tall cannot see.
//   §g  design defect 2: the IMU's signed turns (pelvisRotationSigned, thoraxRotationSigned) read the
//       truth with the lead-relative sign, mirror for a left-hander, and carry the P6→P7 rate
//       hip_stall's measure (m_pelvisRotRateP6P7) reads — on a hip sensor alone too.
//   §h  design §8.4, the signed chart: pelvis, chest and a wG3 forearm, through segment_rates, cross
//       zero once at the top with the same polarity (− going back, + coming down), peak positive in
//       order, both hands, every mount above; and a left-hander analysed as right-handed is caught.
//   §i  design §6.3 / W2.1d, the face-in cue: a sensor worn label-in reads its address bend backward,
//       and body_rotation puts a DOWNGRADED record (body.pelvisFaceIn / body.thoraxFaceIn) on that
//       sensor's readings only — kept and shown, never refused — on every swing's own address.
//
// Own main()/CHECK, no fixture. The skeleton3d term is evaluated in the test, in its own types
// (skeleton3d_rig.h), exactly as skeleton3d_fit.cpp writes it — the fit itself needs cameras.

#include "../../IMU/imu_calibration.h"
#include "../../IMU/trunk_angles.h"
#include "../../IMU/imu_vision_fuser.h"
#include "../../IMU/hm_frame.h"
#include "../../Body/body_rotation.h"
#include "../../Kinematics/segment_rates.h"
#include "../../Timeline/phase_segmenter.h"
#include "../../Timeline/phase_signals.h"
#include "../../Skeleton3d/skeleton3d_rig.h"

#include <QQuaternion>
#include <QVector3D>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>

using namespace pinpoint::analysis;
namespace ps = pinpoint::analysis::phase_signals;
namespace sk = pinpoint::skeleton3d;
using SQ = sk::Q;
using SV = sk::V3;

static int g_fail = 0;

#define CHECK(label, cond)                                        \
    do {                                                          \
        const bool ok_ = (cond);                                  \
        std::printf("  [%s] %s\n", ok_ ? "PASS" : "FAIL", label); \
        if (!ok_) ++g_fail;                                       \
    } while (0)

#define CHECK_NEAR(label, got, want, tol)                                                   \
    do {                                                                                    \
        const double g_ = (got), w_ = (want);                                               \
        const bool ok_ = std::fabs(g_ - w_) <= (tol);                                       \
        std::printf("  [%s] %-62s got %10.4f  want %10.4f (tol %g)\n", ok_ ? "PASS" : "FAIL", \
                    label, g_, w_, double(tol));                                            \
        if (!ok_) ++g_fail;                                                                 \
    } while (0)

namespace {

constexpr double kE2ePi = 3.14159265358979323846;
constexpr double kD2R   = kE2ePi / 180.0;
constexpr double kR2D   = 180.0 / kE2ePi;

// ── The timeline (seconds), 200 Hz ─────────────────────────────────────────────────────────────
constexpr int64_t kDtUs       = 5000;
constexpr double  kEndS       = 5.0;
constexpr double  kStandEndS  = 2.0;    // stand tall: upright and still, [0, 2) s
constexpr double  kBendEndS   = 2.6;    // bending to the address posture, [2, 2.6) s
constexpr double  kAddressS   = 3.0;    // address = takeaway
constexpr double  kTopS       = 3.8;    // the arms' top
constexpr double  kImpactS    = 4.1;
constexpr double  kFollowEndS = 4.5;

int64_t us(double s) { return int64_t(std::llround(s * 1e6)); }
size_t  idxAt(double s) { return size_t(std::llround(s * 1e6 / double(kDtUs))); }

double smooth(double u)  { return u <= 0 ? 0.0 : u >= 1 ? 1.0 : u * u * (3.0 - 2.0 * u); }
double smoothD(double u) { return (u <= 0 || u >= 1) ? 0.0 : 6.0 * u * (1.0 - u); }

SQ rx(double deg) { return SQ::axisAngle({ 1, 0, 0 }, deg * kD2R); }
SQ ry(double deg) { return SQ::axisAngle({ 0, 1, 0 }, deg * kD2R); }
SQ rz(double deg) { return SQ::axisAngle({ 0, 0, 1 }, deg * kD2R); }

QQuaternion toQt(const SQ &q) { return QQuaternion(float(q.w), float(q.x), float(q.y), float(q.z)); }
SQ fromQt(const QQuaternion &q) { return SQ { q.scalar(), q.x(), q.y(), q.z() }.normalized(); }
QVector3D toQt(const SV &v) { return QVector3D(float(v.x), float(v.y), float(v.z)); }

// Rotation vector (rad) of q, sign-fixed to the short way round.
SV rotvec(const SQ &qIn)
{
    const SQ q = qIn.w < 0 ? SQ { -qIn.w, -qIn.x, -qIn.y, -qIn.z } : qIn;
    const SV v { q.x, q.y, q.z };
    const double s = v.norm();
    if (s < 1e-15) return SV {};
    return v * (2.0 * std::atan2(s, q.w) / s);
}

// Angle (deg) between two orientations.
double angleDeg(const SQ &a, const SQ &b) { return (a.conj() * b).normalized().angle() * kR2D; }
double angleDeg(const QQuaternion &a, const QQuaternion &b) { return angleDeg(fromQt(a), fromQt(b)); }

// ── The body ───────────────────────────────────────────────────────────────────────────────────
//
// One trunk segment's motion, right-handed: a spine rotation ψ(t) (opening positive, about +Z) —
// a smoothstep close through the backswing, then a tanh opening whose RATE peaks at peakS — on a
// forward bend θ(t). The S-I rate is ψ'(t) exactly, whatever θ does (ω_body = ψ' ẑ − θ' Rz(−ψ) x̂).
struct TrunkMotion {
    double bendDeg;       // forward bend at address
    double bendTopDeg;    // forward bend at the top (§c's table model only; = bendDeg otherwise)
    double closeDeg;      // spine rotation closed at the end of the backswing
    double backEndS;      // the backswing's smoothstep ends here
    double openDeg;       // the downswing's tanh opening, total
    double peakS;         // its rate peaks here
    double widthS;        // tanh width

    double psiDeg(double t) const
    {
        if (t <= kAddressS) return 0.0;
        double p = -closeDeg * smooth((t - kAddressS) / (backEndS - kAddressS));
        p += 0.5 * openDeg * (std::tanh((t - peakS) / widthS) - std::tanh((kAddressS - peakS) / widthS));
        return p;
    }
    double psiRateDps(double t) const
    {
        if (t <= kAddressS) return 0.0;
        const double c = std::cosh((t - peakS) / widthS);
        return -closeDeg * smoothD((t - kAddressS) / (backEndS - kAddressS)) / (backEndS - kAddressS)
             + 0.5 * openDeg / widthS / (c * c);
    }
    double peakRateDps() const { return 0.5 * openDeg / widthS; }
    double bendAt(double t) const
    {
        if (t < kStandEndS) return 0.0;
        if (t < kAddressS) return bendDeg * smooth((t - kStandEndS) / (kBendEndS - kStandEndS));
        return bendDeg + (bendTopDeg - bendDeg) * smooth((t - kAddressS) / (kTopS - kAddressS));
    }
};

// Pelvis: reverses ~3.707 s, S-I peak 556 °/s at 3.98 s. Thorax: reverses ~3.744 s, peak 800 °/s at
// 4.01 s. Both before impact at 4.1 s; the pelvis first.
TrunkMotion pelvisMotion(double bend = 20.0) { return { bend, bend, 45.0, 3.72, 100.0, 3.98, 0.09 }; }
TrunkMotion thoraxMotion(double bend = 40.0) { return { bend, bend, 92.0, 3.79, 160.0, 4.01, 0.10 }; }

enum class TurnAxis { Spine, WorldVertical };

// How a sensor sits on its segment: M⁻¹ = Rz(δ)·Rx(pitch)·Ry(roll)·extra·M_nom⁻¹, the extra being a
// mis-mount (face-in = Rz(180°), USB up = Ry(180°)). M_nom = Rx(−90°): segment +Z = sensor +Y
// (pointing away from the USB end, which is down), +Y = sensor −Z (label out), +X = sensor +X.
struct Mount {
    double pitchDeg = 0, rollDeg = 0, yawDeg = 0;
    SQ     extra;
    SQ mountInv() const { return (rz(yawDeg) * rx(pitchDeg) * ry(rollDeg) * extra * rx(-90).conj()).normalized(); }
};

struct Rig {
    TrunkMotion pel = pelvisMotion(), thx = thoraxMotion();
    Mount  pelMount { 15.0, 8.0, 0.0, {} }, thxMount { 20.0, -5.0, 0.0, {} };
    TurnAxis axis = TurnAxis::Spine;
    bool   leftHanded = false;
    bool   eskf = false;
    double yaw0Deg  = 57.0;    // the fusion world's arbitrary heading
    double driftDeg = 0.0;     // fusion yaw drift, ramped in over [2, 2.6) s — after stand tall
    // Stand-tall window doctoring for §f.
    QVector3D extraGyroDps;
    double    sampleFraction = 1.0;
};

SQ trueOrientation(const Rig &rig, const TrunkMotion &m, double t)
{
    const double psi = (rig.leftHanded ? -1.0 : 1.0) * m.psiDeg(t);
    return rig.axis == TurnAxis::Spine ? (rx(-m.bendAt(t)) * rz(psi)).normalized()
                                       : (rz(psi) * rx(-m.bendAt(t))).normalized();
}

// Body-frame rate (°/s) by a central difference of the analytic orientation.
SV bodyRateDps(const std::function<SQ(double)> &q, double t)
{
    constexpr double h = 0.0005;
    return rotvec((q(t - h).conj() * q(t + h)).normalized()) * (kR2D / (2.0 * h));
}

SQ fusionWorld(const Rig &rig, double t)
{
    const double drift = rig.driftDeg * smooth((t - kStandEndS) / (kBendEndS - kStandEndS));
    const SQ w = rz(rig.yaw0Deg + drift);
    return rig.eskf ? (rx(180) * w).normalized() : w;
}

// One sensor's raw device + filter output, and the chain run on it.
struct SensorRun {
    std::vector<SQ>        qTrue;     // the truth, stance frame
    std::vector<QVector3D> gyroS, accelS;
    std::vector<QQuaternion> qRaw;
    imu_calibration::StandTallWindow window;
    imu_calibration::TrunkAlignment  al;
    SegmentStream stream;
};

SensorRun runSensor(const Rig &rig, const TrunkMotion &m, const Mount &mount, SegmentRole role,
                    const std::vector<int64_t> &grid)
{
    SensorRun r;
    const SQ Minv = mount.mountInv(), M = Minv.conj();
    const auto qt = [&](double t) { return trueOrientation(rig, m, t); };
    for (int64_t tu : grid) {
        const double t = double(tu) * 1e-6;
        const SQ q = qt(t);
        r.qTrue.push_back(q);
        r.qRaw.push_back(toQt((fusionWorld(rig, t) * q * Minv).normalized()));
        r.gyroS.push_back(toQt(M.rotate(bodyRateDps(qt, t))));
        r.accelS.push_back(toQt(M.rotate(q.conj().rotate({ 0, 0, 1 }))));   // gravity-up, M·(q⁻¹·ẑ)
    }

    // The stand-tall window, summarised as the ceremony would.
    QVector3D fSum, gSum;
    double qs[4] = { 0, 0, 0, 0 };
    int n = 0;
    for (size_t i = 0; i < grid.size() && double(grid[i]) * 1e-6 < kStandEndS; ++i, ++n) {
        fSum += r.accelS[i];
        gSum += r.gyroS[i];
        const QQuaternion &q = r.qRaw[i];
        const double sgn = (n == 0 || q.scalar() * qs[0] + q.x() * qs[1] + q.y() * qs[2] + q.z() * qs[3] >= 0) ? 1.0 : -1.0;
        qs[0] += sgn * q.scalar(); qs[1] += sgn * q.x(); qs[2] += sgn * q.y(); qs[3] += sgn * q.z();
    }
    r.window.meanAccelG  = fSum / float(n);
    r.window.meanGyroDps = gSum / float(n) + rig.extraGyroDps;
    r.window.meanQuat    = QQuaternion(float(qs[0]), float(qs[1]), float(qs[2]), float(qs[3])).normalized();
    r.window.samplesExpected = n;
    r.window.samplesReceived = int(std::llround(rig.sampleFraction * n));
    r.al = imu_calibration::solveTrunkSegment(
        r.window, role == SegmentRole::Pelvis ? imu_calibration::TrunkSensor::Pelvis
                                              : imu_calibration::TrunkSensor::Thorax);

    r.stream.role = role;
    const QQuaternion mInvSolved = r.al.M.conjugated();
    for (size_t i = 0; i < grid.size(); ++i) {
        r.stream.qAnat.push_back(imu_calibration::toAnatomical(r.al.A, r.qRaw[i], r.al.M));
        r.stream.gyroDps.push_back(mInvSolved.rotatedVector(r.gyroS[i]));
        r.stream.accelG.push_back(mInvSolved.rotatedVector(r.accelS[i]));
    }
    return r;
}

// The lead arm for the phase segmenter (its Top and the downswing it votes on): the
// phase_segmenter_test rig on this timeline — one swing angle about world X from the hanging base.
double armPhiDeg(double t)
{
    if (t < kAddressS) return 0.0;
    if (t < kTopS) return -120.0 * smooth((t - kAddressS) / (kTopS - kAddressS));
    if (t < kImpactS) { const double u = (t - kTopS) / (kImpactS - kTopS); return -120.0 + 140.0 * u * u; }
    if (t < kFollowEndS) return 20.0 + 70.0 * smooth((t - kImpactS) / (kFollowEndS - kImpactS));
    return 90.0;
}
SegmentStream armStream(SegmentRole role, double scale, const std::vector<int64_t> &grid)
{
    SegmentStream s;
    s.role = role;
    const auto q = [&](double t) { return (rx(scale * armPhiDeg(t)) * rx(-80)).normalized(); };
    for (int64_t tu : grid) {
        const double t = double(tu) * 1e-6;
        s.qAnat.push_back(toQt(q(t)));
        s.gyroDps.push_back(toQt(bodyRateDps(q, t)));
        s.accelG.push_back(toQt(q(t).conj().rotate({ 0, 0, 1 })));
    }
    return s;
}

struct Run {
    Rig rig;
    std::vector<int64_t> grid;
    SensorRun pel, thx;
    FusedStreams fs;
    double transitionS = 0;            // the truth: the pelvis spine rotation's reversal
    std::vector<PhaseEvent> phases;
};

Run build(const Rig &rig)
{
    Run r;
    r.rig = rig;
    for (int64_t t = 0; t <= us(kEndS); t += kDtUs) r.grid.push_back(t);
    r.pel = runSensor(rig, rig.pel, rig.pelMount, SegmentRole::Pelvis, r.grid);
    r.thx = runSensor(rig, rig.thx, rig.thxMount, SegmentRole::Thorax, r.grid);
    r.fs.timeGrid = r.grid;
    r.fs.segments = { armStream(SegmentRole::LeadHand, 1.0, r.grid),
                      armStream(SegmentRole::LeadForearm, 0.95, r.grid), r.pel.stream, r.thx.stream };
    // The transition truth: ψ' crosses zero, negative → positive, before the top.
    double lo = 3.5, hi = kTopS;
    for (int it = 0; it < 80; ++it) {
        const double mid = 0.5 * (lo + hi);
        (rig.pel.psiRateDps(mid) < 0.0 ? lo : hi) = mid;
    }
    r.transitionS = 0.5 * (lo + hi);
    r.phases = { { Phase::Address, us(kAddressS), 1.f }, { Phase::Top, us(kTopS), 1.f },
                 { Phase::Transition, us(r.transitionS), 1.f }, { Phase::Impact, us(kImpactS), 1.f },
                 { Phase::Finish, us(kFollowEndS), 1.f } };
    return r;
}

SegmentRatesResult rates(const Run &r)
{
    SegmentRatesInputs in;
    in.streams = &r.fs;
    in.leadIsLeft = !r.rig.leftHanded;   // handedness != 2
    in.phases = &r.phases;
    in.impactUs = us(kImpactS);
    return buildSegmentRates(in, SegmentRatesConfig {});
}

BodyRotationResult rotation(const Run &r)
{
    return trackBodyRotation(PoseTrack2D {}, r.fs, 1000, 1000, !r.rig.leftHanded, r.phases);
}

double turnAt(const RotationChannel &c, double s)
{
    for (size_t i = 0; i < c.turn.t_us.size(); ++i)
        if (c.turn.t_us[i] == us(s)) return c.turn.value[i];
    return std::nan("");
}

// Turn from address (deg, signed) by the D13 projection, on a q_anat stream or the truth.
double turnFromAddrDeg(const QQuaternion &q, const QQuaternion &qAddr)
{
    return trunk_angles::turnFromRad(q, qAddr) * kR2D;
}
double turnFromAddrDeg(const SQ &q, const SQ &qAddr) { return turnFromAddrDeg(toQt(q), toQt(qAddr)); }

// The skeleton3d IMU term (skeleton3d_fit.cpp, "── IMUs ──"): E = Qi⁻¹·Rz(ψ)·R_joint·M, its
// residual the rotation angle of E. ψ is the fit's free heading; here the best single ψ over the
// frames (a coarse scan, then a fine one), the RMS angle returned in degrees.
double imuTermRmsDeg(const std::vector<SQ> &Qi, const std::vector<SQ> &R, const SQ &M, double *bestPsiDeg = nullptr)
{
    const auto rms = [&](double psiDeg) {
        const SQ Rz = rz(psiDeg);
        double s = 0;
        for (size_t i = 0; i < Qi.size(); ++i) {
            const double a = (Qi[i].conj() * Rz * R[i] * M).normalized().angle();
            s += a * a;
        }
        return std::sqrt(s / double(Qi.size())) * kR2D;
    };
    double best = 0, bestV = 1e9;
    for (double p = -180; p < 180; p += 0.5) { const double v = rms(p); if (v < bestV) { bestV = v; best = p; } }
    const double c = best;
    for (double p = c - 0.5; p <= c + 0.5; p += 0.001) { const double v = rms(p); if (v < bestV) { bestV = v; best = p; } }
    if (bestPsiDeg) *bestPsiDeg = best;
    return bestV;
}

} // namespace

int main()
{
    std::printf("=== trunk end-to-end: stand tall → solve → q_anat → every trunk consumer ===\n");

    const Rig base {};
    const Run R = build(base);
    const size_t iAddr = idxAt(kAddressS), iTop = idxAt(kTopS);
    std::printf("  truth: pelvis transition %.4f s; top turn (D13) pelvis %.2f°, thorax %.2f° "
                "(spine rotation %.2f°, %.2f°)\n", R.transitionS,
                std::fabs(turnFromAddrDeg(R.pel.qTrue[iTop], R.pel.qTrue[iAddr])),
                std::fabs(turnFromAddrDeg(R.thx.qTrue[iTop], R.thx.qTrue[iAddr])),
                base.pel.psiDeg(kTopS), base.thx.psiDeg(kTopS));

    // ── §a the solve ──────────────────────────────────────────────────────────────────────────
    std::printf("-- a. the solve --\n");
    {
        CHECK("a: both stand-tall solves valid", R.pel.al.valid && R.thx.al.valid);
        double standMax = 0, trueMax = 0;
        for (size_t i = 0; i < R.grid.size(); ++i) {
            const SegmentStream *ss[2] = { &R.pel.stream, &R.thx.stream };
            const SensorRun *sr[2] = { &R.pel, &R.thx };
            for (int k = 0; k < 2; ++k) {
                const double d = angleDeg(ss[k]->qAnat[i], toQt(sr[k]->qTrue[i]));
                trueMax = std::max(trueMax, d);
                if (double(R.grid[i]) * 1e-6 < kStandEndS)
                    standMax = std::max(standMax, angleDeg(ss[k]->qAnat[i], QQuaternion()));
            }
        }
        CHECK_NEAR("a: q_anat at stand tall is the identity (max angle, deg)", standMax, 0.0, 0.01);
        CHECK_NEAR("a: q_anat IS the true segment orientation, every sample (deg)", trueMax, 0.0, 0.01);
        CHECK_NEAR("a: pelvis forward bend at address (deg)",
                   trunk_angles::forwardBendRad(R.pel.stream.qAnat[iAddr]) * kR2D, 20.0, 0.5);
        CHECK_NEAR("a: thorax forward bend at address (deg)",
                   trunk_angles::forwardBendRad(R.thx.stream.qAnat[iAddr]) * kR2D, 40.0, 0.5);
        CHECK_NEAR("a: pelvis turn at the top reads the truth (deg)",
                   turnFromAddrDeg(R.pel.stream.qAnat[iTop], R.pel.stream.qAnat[iAddr]),
                   turnFromAddrDeg(R.pel.qTrue[iTop], R.pel.qTrue[iAddr]), 0.5);
        CHECK_NEAR("a: thorax turn at the top reads the truth (deg)",
                   turnFromAddrDeg(R.thx.stream.qAnat[iTop], R.thx.stream.qAnat[iAddr]),
                   turnFromAddrDeg(R.thx.qTrue[iTop], R.thx.qTrue[iAddr]), 0.5);
        CHECK_NEAR("a: the S-I rate is the segment gyro's z (pelvis at its peak, °/s)",
                   trunk_angles::siRateDps(R.pel.stream.gyroDps[idxAt(base.pel.peakS)]),
                   base.pel.psiRateDps(base.pel.peakS), 0.5);

        Rig e = base;
        e.eskf = true;
        const Run E = build(e);
        double dMax = 0;
        for (size_t i = 0; i < R.grid.size(); ++i) {
            dMax = std::max(dMax, angleDeg(E.pel.stream.qAnat[i], R.pel.stream.qAnat[i]));
            dMax = std::max(dMax, angleDeg(E.thx.stream.qAnat[i], R.thx.stream.qAnat[i]));
        }
        CHECK("a: ESKF world: both solves valid", E.pel.al.valid && E.thx.al.valid);
        CHECK_NEAR("a: ESKF world (Rx 180°) gives the same q_anat (max angle, deg)", dMax, 0.0, 0.01);
        CHECK("a: …and a different A (the 180° is absorbed by the solve, not assumed)",
              angleDeg(E.pel.al.A, R.pel.al.A) > 170.0);
    }

    // ── §b the consumers ──────────────────────────────────────────────────────────────────────
    std::printf("-- b. the consumers --\n");
    const BodyRotationResult rot = rotation(R);
    const SegmentRatesResult sr = rates(R);
    const Segmentation seg = PhaseSegmenter::segment(R.fs, us(kImpactS));
    {
        // body_rotation's IMU fill.
        CHECK("b: body_rotation: both segments from the IMU",
              rot.pelvis.tier == RotationTier::Imu && rot.thorax.tier == RotationTier::Imu);
        CHECK_NEAR("b: body_rotation pelvis turn at the top (deg)", turnAt(rot.pelvis, kTopS),
                   std::fabs(turnFromAddrDeg(R.pel.qTrue[iTop], R.pel.qTrue[iAddr])), 0.5);
        CHECK_NEAR("b: body_rotation thorax turn at the top (deg)", turnAt(rot.thorax, kTopS),
                   std::fabs(turnFromAddrDeg(R.thx.qTrue[iTop], R.thx.qTrue[iAddr])), 0.5);

        // segment_rates' IMU trunk rate: the S-I rate, its peak and its instant.
        const KsNode *p = sr.sequence.node(SeqSegment::Pelvis), *t = sr.sequence.node(SeqSegment::Thorax);
        CHECK("b: segment_rates: pelvisImu / thoraxImu routes",
              sr.pelvis.routeId == QLatin1String("pelvisImu") && sr.thorax.routeId == QLatin1String("thoraxImu"));
        CHECK("b: both trunk nodes placed", p && p->placed && t && t->placed);
        if (p && t) {
            std::printf("    pelvis node %.1f °/s at %.4f s (truth %.1f at %.4f); thorax %.1f at %.4f (truth %.1f at %.4f)\n",
                        p->peakDps, p->tPeakUs * 1e-6, base.pel.peakRateDps(), base.pel.peakS,
                        t->peakDps, t->tPeakUs * 1e-6, base.thx.peakRateDps(), base.thx.peakS);
            // The node's peak VALUE is the reducer's windowed mean (placePeak, angular_rate.h), not the
            // raw sample, so a sharp synthetic peak reads ~2 % low (NR-09's smoothed-peak attenuation);
            // the INSTANT is exact and is checked to one sample below. 3 % holds the value honest.
            CHECK_NEAR("b: pelvis S-I peak (°/s), 3 % (windowed mean)", p->peakDps, base.pel.peakRateDps(), 0.03 * base.pel.peakRateDps());
            CHECK_NEAR("b: thorax S-I peak (°/s), 3 % (windowed mean)", t->peakDps, base.thx.peakRateDps(), 0.03 * base.thx.peakRateDps());
            CHECK_NEAR("b: pelvis peak instant (ms), one sample", p->tPeakUs * 1e-3, base.pel.peakS * 1e3, 5.0);
            CHECK_NEAR("b: thorax peak instant (ms), one sample", t->tPeakUs * 1e-3, base.thx.peakS * 1e3, 5.0);
            CHECK("b: pelvis peaks before thorax", p->tPeakUs < t->tPeakUs);
        }

        // phase_segmenter's trunk read: the pelvis turn rate about world Z through q_anat.
        const std::vector<double> wz = ps::signedRate(ps::worldGyro(R.pel.stream), QVector3D(0, 0, 1));
        double mean = 0;
        for (size_t i = iTop; i <= idxAt(kImpactS); ++i) mean += wz[i];
        mean /= double(idxAt(kImpactS) - iTop + 1);
        CHECK("b: world-Z pelvis rate is opening-positive Top→Impact (right-hander)", mean > 10.0);
        CHECK("b: …negative 30 ms before the truth transition, positive 30 ms after",
              wz[idxAt(R.transitionS - 0.03)] < 0.0 && wz[idxAt(R.transitionS + 0.03)] > 0.0);
        CHECK_NEAR("b: world-Z rate = S-I rate × cos(bend) at the pelvis peak (°/s)", wz[idxAt(base.pel.peakS)],
                   base.pel.psiRateDps(base.pel.peakS) * std::cos(20.0 * kD2R), 0.5);
        const PhaseEvent *tr = seg.eventFor(Phase::Transition);
        CHECK("b: phase_segmenter emits a Transition", tr != nullptr);
        if (tr) CHECK_NEAR("b: phase_segmenter Transition at the truth (s)", tr->t_us * 1e-6, R.transitionS, 0.02);
        const PhaseEvent *top = seg.eventFor(Phase::Top);
        if (top) std::printf("    segmenter Top %.4f s (arms' truth %.4f)\n", top->t_us * 1e-6, kTopS);

        // The skeleton3d IMU term, hasMount = true (set here, in the test). The fit's world sits at a
        // 30° heading from the stance frame and its joint frame is the anatomical one through a rig
        // mount; for the TRUE pose E is the identity at ψ = 30°.
        const SQ mRig[2] = { SQ::axisAngle(SV { 0.3, 1, -0.4 }, 70 * kD2R),
                             SQ::axisAngle(SV { 0.3, 1, -0.4 }, 110 * kD2R) };
        const SensorRun *srn[2] = { &R.pel, &R.thx };
        const char *nm[2] = { "pelvis", "thorax" };
        for (int k = 0; k < 2; ++k) {
            std::vector<SQ> Qi, Rtrue, Rturned, Qtilted;
            for (size_t i = iAddr; i <= idxAt(kFollowEndS); i += 2) {
                const double ts = double(R.grid[i]) * 1e-6;
                Qi.push_back(fromQt(srn[k]->stream.qAnat[i]));
                Rtrue.push_back((rz(-30) * srn[k]->qTrue[i] * mRig[k].conj()).normalized());
                // Turned 20° further toward the top (and held): a pose the IMU did not see.
                const double away = 20.0 * smooth((ts - kAddressS) / (kTopS - kAddressS));
                Rturned.push_back((rz(-30) * rz(away) * srn[k]->qTrue[i] * mRig[k].conj()).normalized());
                // The limb-solve defect: an IMU world tilted 30° off vertical.
                Qtilted.push_back((rx(30) * Qi.back()).normalized());
            }
            double psi = 0;
            const double eTrue = imuTermRmsDeg(Qi, Rtrue, mRig[k], &psi);
            const double eTurn = imuTermRmsDeg(Qi, Rturned, mRig[k]);
            const double eTilt = imuTermRmsDeg(Qtilted, Rtrue, mRig[k]);
            std::printf("    skeleton3d %s: true %.4f° (ψ %.3f°), turned 20° %.2f°, tilted world %.2f°\n",
                        nm[k], eTrue, psi, eTurn, eTilt);
            char lbl[96];
            std::snprintf(lbl, sizeof lbl, "b: skeleton3d %s IMU term: true pose residual ~0 (RMS deg)", nm[k]);
            CHECK_NEAR(lbl, eTrue, 0.0, 0.01);
            std::snprintf(lbl, sizeof lbl, "b: skeleton3d %s: ψ solves to the 30° heading", nm[k]);
            CHECK_NEAR(lbl, psi, 30.0, 0.01);
            std::snprintf(lbl, sizeof lbl, "b: skeleton3d %s: a pose turned 20° away is seen (RMS > 5°)", nm[k]);
            CHECK(lbl, eTurn > 5.0);
            std::snprintf(lbl, sizeof lbl, "b: skeleton3d %s: a 30°-tilted world costs ≥ 29° (heading-only ψ)", nm[k]);
            CHECK(lbl, eTilt >= 29.0);
        }
    }

    // ── §c the δ table (design §6.2) ──────────────────────────────────────────────────────────
    std::printf("-- c. the δ table --\n");
    {
        Rig d = base;
        d.pelMount.yawDeg = 10.0;
        d.thxMount.yawDeg = 10.0;
        const Run D = build(d);
        CHECK("c: δ = 10°: both solves valid (stand tall cannot see δ)", D.pel.al.valid && D.thx.al.valid);

        // S-I rates and peak times: no sensitivity.
        const SegmentRatesResult sd = rates(D);
        const SegmentRateChannel *c0[2] = { &sr.pelvis, &sr.thorax }, *c1[2] = { &sd.pelvis, &sd.thorax };
        const SeqSegment segs[2] = { SeqSegment::Pelvis, SeqSegment::Thorax };
        for (int k = 0; k < 2; ++k) {
            const KsNode *n0 = sr.sequence.node(segs[k]), *n1 = sd.sequence.node(segs[k]);
            double dMax = 0, peak = 1;
            if (n0) peak = std::fabs(n0->peakDps);
            const std::vector<double> &v0 = c0[k]->series.value, &v1 = c1[k]->series.value;
            for (size_t i = 0; i < std::min(v0.size(), v1.size()); ++i) dMax = std::max(dMax, std::fabs(v0[i] - v1[i]));
            char lbl[96];
            std::snprintf(lbl, sizeof lbl, "c: %s S-I series unchanged by δ (max |Δ| / peak)", k ? "thorax" : "pelvis");
            CHECK_NEAR(lbl, v0.size() == v1.size() && !v0.empty() ? dMax / peak : 1.0, 0.0, 1e-3);
            std::snprintf(lbl, sizeof lbl, "c: %s peak value unchanged (relative)", k ? "thorax" : "pelvis");
            CHECK_NEAR(lbl, n0 && n1 ? std::fabs(n1->peakDps - n0->peakDps) / peak : 1.0, 0.0, 1e-3);
            std::snprintf(lbl, sizeof lbl, "c: %s peak instant unchanged (samples)", k ? "thorax" : "pelvis");
            CHECK_NEAR(lbl, n0 && n1 ? std::fabs(double(n1->tPeakUs - n0->tPeakUs)) / double(kDtUs) : 99.0, 0.0, 1.0);
        }

        // Turn — what δ does to it is a conjugation, q_anat = Rz(−δ)·q_true·Rz(δ) (the solve takes
        // anterior δ off). Measured on the chain against that, every swing sample:
        //  (i) the table's own model, a turn about WORLD vertical with the bend changing: the error
        //      is Δ atan(tan δ·cos θ) — "changing only as θ changes", bounded ~1°;
        //  (ii) a turn about the bent SPINE: the error grows with the turn itself.
        for (int model = 0; model < 2; ++model) {
            Rig m = d;
            Rig m0 = base;
            if (model == 0) {
                m.axis = m0.axis = TurnAxis::WorldVertical;
                m.pel.bendTopDeg = m0.pel.bendTopDeg = 15.0;      // rising 5° through the backswing
                m.thx.bendTopDeg = m0.thx.bendTopDeg = 30.0;      // rising 10°
            }
            const Run Mr = build(m), M0 = build(m0);
            const SensorRun *a[2] = { &Mr.pel, &Mr.thx }, *b[2] = { &M0.pel, &M0.thx };
            const TrunkMotion *tm[2] = { &m.pel, &m.thx };
            for (int k = 0; k < 2; ++k) {
                double errMax = 0, devMax = 0, predMax = 0;
                const SQ rzd = rz(10.0), rzdInv = rz(-10.0);
                for (size_t i = iAddr; i <= idxAt(kFollowEndS); ++i) {
                    const double read  = turnFromAddrDeg(a[k]->stream.qAnat[i], a[k]->stream.qAnat[iAddr]);
                    const double truth = turnFromAddrDeg(b[k]->qTrue[i], b[k]->qTrue[iAddr]);
                    const SQ cq = (rzdInv * b[k]->qTrue[i] * rzd).normalized(), cqa = (rzdInv * b[k]->qTrue[iAddr] * rzd).normalized();
                    const double conj = turnFromAddrDeg(cq, cqa);
                    errMax = std::max(errMax, std::fabs(read - truth));
                    devMax = std::max(devMax, std::fabs(read - conj));
                    if (model == 0) {
                        const double th = tm[k]->bendAt(double(R.grid[i]) * 1e-6) * kD2R, th0 = tm[k]->bendDeg * kD2R;
                        const double pred = (std::atan(std::tan(10 * kD2R) * std::cos(th)) - std::atan(std::tan(10 * kD2R) * std::cos(th0))) * kR2D;
                        predMax = std::max(predMax, std::fabs((read - truth) - pred));
                    }
                }
                const char *segName = k ? "thorax" : "pelvis";
                std::printf("    %s, turn about %s: max turn error over the swing %.3f°\n",
                            segName, model == 0 ? "world vertical" : "the bent spine", errMax);
                char lbl[110];
                std::snprintf(lbl, sizeof lbl, "c: %s %s: the chain is Rz(−δ)·q·Rz(δ) (max deg)", segName, model == 0 ? "world-vertical" : "spine-axis");
                CHECK_NEAR(lbl, devMax, 0.0, 0.01);
                if (model == 0) {
                    std::snprintf(lbl, sizeof lbl, "c: %s world-vertical: error = Δatan(tan δ·cos θ) (max deg)", segName);
                    CHECK_NEAR(lbl, predMax, 0.0, 0.01);
                    std::snprintf(lbl, sizeof lbl, "c: %s world-vertical: turn error bounded ≤ 2°", segName);
                    CHECK(lbl, errMax <= 2.0);
                }
            }
        }

        // Obliquity at address: asin(sin θ·sin δ) — 4.2° at θ = 25°, 6.4° at θ = 40°. Forward bend:
        // the long axis does not move under a rotation about itself, so no error at all.
        Rig d25 = d;
        d25.pel = pelvisMotion(25.0);
        const Run D25 = build(d25);
        const double ob25 = std::fabs(trunk_angles::obliquityRad(D25.pel.stream.qAnat[iAddr])) * kR2D;
        const double ob40 = std::fabs(trunk_angles::obliquityRad(D.thx.stream.qAnat[iAddr])) * kR2D;
        std::printf("    obliquity at address: θ 25° %.3f° (asin(sin θ sin δ) = %.3f), θ 40° %.3f° (%.3f)\n", ob25,
                    std::asin(std::sin(25 * kD2R) * std::sin(10 * kD2R)) * kR2D, ob40,
                    std::asin(std::sin(40 * kD2R) * std::sin(10 * kD2R)) * kR2D);
        CHECK_NEAR("c: obliquity error at address, θ = 25°, δ = 10° (deg)", ob25, 4.2, 0.3);
        CHECK_NEAR("c: obliquity error at address, θ = 40°, δ = 10° (deg)", ob40, 6.4, 0.3);
        CHECK_NEAR("c: forward bend at address, θ = 40°, δ = 10° (deg)",
                   trunk_angles::forwardBendRad(D.thx.stream.qAnat[iAddr]) * kR2D, 40.0, 0.5);
        CHECK_NEAR("c: forward bend at address, θ = 25°, δ = 10° (deg)",
                   trunk_angles::forwardBendRad(D25.pel.stream.qAnat[iAddr]) * kR2D, 25.0, 0.5);
    }

    // ── §d the left-hander ────────────────────────────────────────────────────────────────────
    std::printf("-- d. left-hander --\n");
    {
        Rig l = base;
        l.leftHanded = true;
        const Run L = build(l);
        const SegmentRatesResult sl = rates(L);
        const BodyRotationResult rl = rotation(L);
        const SeqSegment segs[2] = { SeqSegment::Pelvis, SeqSegment::Thorax };
        for (int k = 0; k < 2; ++k) {
            const KsNode *n0 = sr.sequence.node(segs[k]), *n1 = sl.sequence.node(segs[k]);
            char lbl[96];
            std::snprintf(lbl, sizeof lbl, "d: %s node placed, opening-positive", k ? "thorax" : "pelvis");
            CHECK(lbl, n1 && n1->placed && n1->peakDps > 0.0);
            std::snprintf(lbl, sizeof lbl, "d: %s peak = the right-hander's (relative)", k ? "thorax" : "pelvis");
            CHECK_NEAR(lbl, n0 && n1 ? std::fabs(n1->peakDps - n0->peakDps) / std::fabs(n0->peakDps) : 1.0, 0.0, 1e-3);
            std::snprintf(lbl, sizeof lbl, "d: %s peak instant = the right-hander's (samples)", k ? "thorax" : "pelvis");
            CHECK_NEAR(lbl, n0 && n1 ? std::fabs(double(n1->tPeakUs - n0->tPeakUs)) / double(kDtUs) : 99.0, 0.0, 1.0);
        }
        CHECK_NEAR("d: pelvis turn at the top = the right-hander's (deg)", turnAt(rl.pelvis, kTopS), turnAt(rot.pelvis, kTopS), 0.01);
        CHECK_NEAR("d: thorax turn at the top = the right-hander's (deg)", turnAt(rl.thorax, kTopS), turnAt(rot.thorax, kTopS), 0.01);
        const std::vector<double> wz = ps::signedRate(ps::worldGyro(L.pel.stream), QVector3D(0, 0, 1));
        CHECK("d: the raw world-Z pelvis rate is NEGATIVE in the downswing (opening is −Z for a left-hander)",
              wz[idxAt(base.pel.peakS)] < -100.0);
        const Segmentation segL = PhaseSegmenter::segment(L.fs, us(kImpactS));
        const PhaseEvent *t0 = seg.eventFor(Phase::Transition), *t1 = segL.eventFor(Phase::Transition);
        CHECK("d: phase_segmenter finds the left-hander's Transition", t1 != nullptr);
        if (t0 && t1) CHECK_NEAR("d: …at the right-hander's instant (ms)", t1->t_us * 1e-3, t0->t_us * 1e-3, 1.0);
    }

    // ── §e per-swing heading reset ────────────────────────────────────────────────────────────
    std::printf("-- e. fusion yaw drift between stand tall and the swing --\n");
    for (int eskf = 0; eskf < 2; ++eskf) {
        Rig dr = base;
        dr.driftDeg = 17.0;
        dr.eskf = eskf != 0;
        const Run Dr = build(dr);
        const char *w = eskf ? "ESKF" : "Madgwick";
        char lbl[110];
        std::snprintf(lbl, sizeof lbl, "e: %s: the drift is really in q_anat (address heading moved, deg)", w);
        CHECK_NEAR(lbl, std::fabs(turnFromAddrDeg(Dr.pel.stream.qAnat[iAddr], R.pel.stream.qAnat[iAddr])), 17.0, 0.01);
        const BodyRotationResult rd = rotation(Dr);
        std::snprintf(lbl, sizeof lbl, "e: %s: pelvis turn at the top unchanged (deg)", w);
        CHECK_NEAR(lbl, turnAt(rd.pelvis, kTopS), turnAt(rot.pelvis, kTopS), 0.01);
        std::snprintf(lbl, sizeof lbl, "e: %s: thorax turn at the top unchanged (deg)", w);
        CHECK_NEAR(lbl, turnAt(rd.thorax, kTopS), turnAt(rot.thorax, kTopS), 0.01);
        const SegmentRatesResult sdr = rates(Dr);
        const KsNode *n0 = sr.sequence.node(SeqSegment::Thorax), *n1 = sdr.sequence.node(SeqSegment::Thorax);
        std::snprintf(lbl, sizeof lbl, "e: %s: thorax S-I peak unchanged (relative)", w);
        CHECK_NEAR(lbl, n0 && n1 ? std::fabs(n1->peakDps - n0->peakDps) / std::fabs(n0->peakDps) : 1.0, 0.0, 1e-3);
    }

    // ── §f the gates, on chain-generated windows ─────────────────────────────────────────────
    std::printf("-- f. the gates --\n");
    {
        Rig up = base;
        up.pelMount.extra = ry(180);            // USB end UP, label still out
        const Run U = build(up);
        CHECK("f: USB-up hip sensor rejected", !U.pel.al.valid);
        CHECK("f: …\"Hip sensor: turn it USB-down, label out\"",
              U.pel.al.record.failure == QStringLiteral("Hip sensor: turn it USB-down, label out"));
        CHECK("f: …and the chest sensor beside it still solves", U.thx.al.valid);

        Rig mv = base;
        mv.extraGyroDps = QVector3D(0.f, 2.f, 0.f);
        const Run Mv = build(mv);
        CHECK("f: a moving stand tall (2 °/s) rejected, chest sensor named",
              !Mv.thx.al.valid && Mv.thx.al.record.failure == QStringLiteral("Chest sensor moved — hold still and try again"));

        Rig sh = base;
        sh.sampleFraction = 0.80;
        const Run Sh = build(sh);
        CHECK("f: 80 % of the samples rejected, hip sensor named",
              !Sh.pel.al.valid && Sh.pel.al.record.failure == QStringLiteral("Hip sensor dropped samples — move closer or check the battery"));

        // FACE-IN: label against the back, USB still down. Gravity cannot tell it from the right mount
        // (they differ by a rotation about up), so stand tall solves it — 180° about the spine.
        // Turn and the S-I rate are a rotation about that same axis and read CORRECTLY; the
        // sagittal and lateral readings flip: the address forward bend reads as a backward bend.
        // The turn-away step (design §6.3 step 2, "away on both") cannot see it either — an upright
        // turn is about the spine. The bend sign at the first address is what can.
        Rig fi = base;
        fi.thxMount.extra = rz(180);
        const Run Fi = build(fi);
        CHECK("f: face-in chest sensor: stand tall cannot see it (solves, valid)", Fi.thx.al.valid);
        CHECK_NEAR("f: face-in: the turn at the top still reads right (deg)",
                   turnFromAddrDeg(Fi.thx.stream.qAnat[iTop], Fi.thx.stream.qAnat[iAddr]),
                   turnFromAddrDeg(R.thx.qTrue[iTop], R.thx.qTrue[iAddr]), 0.01);
        CHECK_NEAR("f: face-in: the S-I rate at the thorax peak still reads right (°/s)",
                   trunk_angles::siRateDps(Fi.thx.stream.gyroDps[idxAt(base.thx.peakS)]),
                   trunk_angles::siRateDps(R.thx.stream.gyroDps[idxAt(base.thx.peakS)]), 0.01);
        CHECK_NEAR("f: face-in: the address forward bend reads BACKWARD (deg)",
                   trunk_angles::forwardBendRad(Fi.thx.stream.qAnat[iAddr]) * kR2D, -40.0, 0.01);
    }

    // ── §g the signed turn and hip_stall's reading on a worn swing (design defect 2) ──────────
    // BodyRotationTriangulated steps aside for every segment an IMU measured, so on a worn swing
    // the signed pelvis turn m_pelvisRotRateP6P7 (→ sig_hipStall → hip_stall) reads has to come from
    // the IMU route. The measure is a rate: the curve's ±kWindowHalfUs median at P7 less that at P6,
    // over the time between (measure_sample.h) — reproduced here on the emitted series.
    std::printf("-- g. signed turn and the hip_stall rate (defect 2) --\n");
    {
        constexpr double kDeliveryS = 4.05;   // P6, the shaft parallel coming down
        const auto ladderWithP6 = [](const Run &run) {
            std::vector<PhaseEvent> ph = run.phases;
            ph.push_back({ Phase::Delivery, us(kDeliveryS), 1.f });
            return ph;
        };
        const auto findKey = [](const std::vector<MetricSeries> &all, const char *key) -> const MetricSeries * {
            for (const MetricSeries &m : all)
                if (m.key == QLatin1String(key)) return &m;
            return nullptr;
        };
        const auto medianAt = [](const MetricSeries *m, double s) {
            std::vector<double> v;
            if (m)
                for (size_t i = 0; i < m->t_us.size(); ++i)
                    if (std::llabs(m->t_us[i] - us(s)) <= pinpoint::tuned::sampler::kWindowHalfUs)
                        v.push_back(m->value[i]);
            std::sort(v.begin(), v.end());
            return v.empty() ? std::nan("") : v[v.size() / 2];
        };
        const auto rateP6P7 = [&](const MetricSeries *m) {
            return (medianAt(m, kImpactS) - medianAt(m, kDeliveryS)) / (kImpactS - kDeliveryS);
        };
        // The truth, signed and lead-relative: D13's turn of the TRUE orientation from address; the
        // right-hander's opening is +Z, so it stands as it is.
        const auto truthTurn = [&](const SensorRun &sr, double s) {
            return turnFromAddrDeg(sr.qTrue[idxAt(s)], sr.qTrue[iAddr]);
        };

        const std::vector<PhaseEvent> ph = ladderWithP6(R);
        const std::vector<MetricSeries> series = buildBodyRotationSeries(rot, ph);
        const MetricSeries *ps = findKey(series, "pelvisRotationSigned");
        const MetricSeries *ts = findKey(series, "thoraxRotationSigned");
        CHECK("g: both signed series emitted, from the IMU tier",
              ps && ts && rot.pelvisSigned.tier == RotationTier::Imu && rot.thoraxSigned.tier == RotationTier::Imu);
        CHECK_NEAR("g: signed pelvis at the top = the truth (deg)", turnAt(rot.pelvisSigned, kTopS),
                   truthTurn(R.pel, kTopS), 0.5);
        CHECK_NEAR("g: signed thorax at the top = the truth (deg)", turnAt(rot.thoraxSigned, kTopS),
                   truthTurn(R.thx, kTopS), 0.5);
        CHECK("g: closed (negative) at the top, open (positive) at impact — pelvis",
              turnAt(rot.pelvisSigned, kTopS) < -20.0 && turnAt(rot.pelvisSigned, kImpactS) > 10.0);
        CHECK("g: …and thorax", turnAt(rot.thoraxSigned, kTopS) < -40.0 && turnAt(rot.thoraxSigned, kImpactS) > 0.0);
        CHECK_NEAR("g: |signed| = the magnitude series at impact (deg)", std::fabs(turnAt(rot.pelvisSigned, kImpactS)),
                   turnAt(rot.pelvis, kImpactS), 1e-6);

        const double rate = rateP6P7(ps);
        const double truthRate = (truthTurn(R.pel, kImpactS) - truthTurn(R.pel, kDeliveryS)) / (kImpactS - kDeliveryS);
        std::printf("    m_pelvisRotRateP6P7 on the IMU series %.1f °/s (truth %.1f)\n", rate, truthRate);
        CHECK("g: m_pelvisRotRateP6P7 is assessable (both ends read)", std::isfinite(rate));
        CHECK_NEAR("g: …and reads the pelvis still opening into impact, as the truth (°/s)", rate, truthRate, 1.0);
        CHECK("g: …positive — a pelvis turning through square is not a stall", rate > 100.0);

        // A left-hander's mirrored swing: the same lead-relative numbers (the mirror is the producer's).
        Rig l = base;
        l.leftHanded = true;
        const Run L = build(l);
        const BodyRotationResult rl = rotation(L);
        const std::vector<MetricSeries> lSeries = buildBodyRotationSeries(rl, ladderWithP6(L));
        CHECK_NEAR("g: left-hander: signed pelvis at the top = the right-hander's (deg)",
                   turnAt(rl.pelvisSigned, kTopS), turnAt(rot.pelvisSigned, kTopS), 0.01);
        CHECK_NEAR("g: left-hander: signed thorax at impact = the right-hander's (deg)",
                   turnAt(rl.thoraxSigned, kImpactS), turnAt(rot.thoraxSigned, kImpactS), 0.01);
        CHECK_NEAR("g: left-hander: the P6→P7 rate = the right-hander's (°/s)",
                   rateP6P7(findKey(lSeries, "pelvisRotationSigned")), rate, 0.01);

        // One sensor at a time: the hip sensor alone still feeds hip_stall; the chest alone gives
        // its own signed twin and no pelvis one.
        for (int k = 0; k < 2; ++k) {
            FusedStreams one;
            one.timeGrid = R.grid;
            one.segments = { k == 0 ? R.pel.stream : R.thx.stream };
            const std::vector<MetricSeries> s1 =
                buildBodyRotationSeries(trackBodyRotation(PoseTrack2D {}, one, 1000, 1000, true, ph), ph);
            if (k == 0) {
                CHECK("g: hip sensor alone: pelvisRotationSigned, no thoraxRotationSigned",
                      findKey(s1, "pelvisRotationSigned") && !findKey(s1, "thoraxRotationSigned"));
                CHECK_NEAR("g: hip sensor alone: the same P6→P7 rate (°/s)",
                           rateP6P7(findKey(s1, "pelvisRotationSigned")), rate, 0.01);
            } else {
                CHECK("g: chest sensor alone: thoraxRotationSigned, no pelvisRotationSigned",
                      findKey(s1, "thoraxRotationSigned") && !findKey(s1, "pelvisRotationSigned"));
            }
        }
    }

    // ── §h the signed chart: trunk and wG3 arm cross zero together at the top (design §8.4) ───
    // Since b699d505 the sequence curves are SIGNED: the trunk S-I rate natively (axialFromImu, lead-
    // relative by `leadIsLeft`), the arm IMU against its own swing plane (signAgainstPlane, a reversal
    // only where the arm slows — 93c1e4c8) and then turned over as a whole so the downswing reads +
    // (orientDownswingPositive). Two independent sign rules meet on one chart, so a trunk curve upside
    // down against the arm would read as a sequence fault. The convention (pinpoint_sign_conventions.md,
    // "Turn RATES are signed"; kSignedArmClub): opening / towards the target is POSITIVE — every curve
    // negative going back, crossing zero once near the top, one positive hump coming down.
    //
    // THE ARM IS A wG3 FOREARM, composed as imu_vision_fuser.cpp composes a HackMotion lane: the device
    // streams WORLD→BODY in its own frame and its gyro in its own frame; the fuser conjugates the quat
    // and calls toAnatomical(A = identity, q_hm*, M = hm_frame::mountM()) — the selected candidate — and
    // rotates the gyro by M⁻¹; role LeadForearm (hm_binding_recon.h, the lower-arm unit), hackMotion set.
    // In segment_rates the wG3 is `armImu` by the fallback (no LeadUpperArm is worn with it) and takes
    // swingFromImu's GYRO branch: ω less its roll about e_y, signed against the carried plane normal.
    // Nothing there dispatches on hackMotion, so a Witmotion forearm takes the same lines; what differs
    // is upstream (the conjugate, the frame constant), which is why the lane is built the wG3's way.
    //
    // The forearm swings about an inclined, world-fixed plane normal while it rolls (supinates going
    // back, pronates 90° coming down) — the roll moves the swing vector around the segment frame, so
    // the normal has to be CARRIED for the sign to hold. Synthetic timing (truth, printed below): the
    // pelvis reverses 91 ms before the arms' top (3.80 s), the chest 37 ms before, the forearm 13 ms
    // after; peaks pelvis 3.98 s < chest 4.01 s < forearm 4.05 s, impact 4.10 s.
    std::printf("-- h. the signed chart: pelvis, chest and wG3 forearm cross zero together (§8.4) --\n");
    {
        // The forearm's swing angle about its plane normal: the TrunkMotion form (smoothstep closing
        // through the backswing, a tanh opening whose rate peaks at 833 °/s, 50 ms before impact).
        const TrunkMotion armSwing { 0.0, 0.0, 120.0, 3.83, 150.0, 4.05, 0.09 };
        const auto armRollDeg = [](double t) {
            return 50.0 * smooth((t - kAddressS) / (kTopS - kAddressS)) - 90.0 * smooth((t - kTopS) / (kFollowEndS - kTopS));
        };
        const SQ plane   = (rz(-35) * rx(55)).normalized();   // an inclined swing plane, arbitrary heading
        const SQ hmWorld = rz(123);                           // the wG3's own world heading
        // A left-hander's swing: the mirror across the stance's Y–Z plane (rotation axis (x, −y, −z)).
        const auto mirror = [](const SQ &q) { return SQ { q.w, q.x, -q.y, -q.z }; };
        const auto wg3Forearm = [&](const std::vector<int64_t> &grid, bool leftHanded) {
            const std::function<SQ(double)> qt = [&](double t) {
                const SQ q = (plane * rx(armSwing.psiDeg(t)) * ry(armRollDeg(t))).normalized();
                return leftHanded ? mirror(q) : q;
            };
            const QQuaternion M = pinpoint::hm_frame::mountM(), Minv = M.conjugated();
            SegmentStream s;
            s.role       = SegmentRole::LeadForearm;
            s.hackMotion = true;
            for (int64_t tu : grid) {
                const double t = double(tu) * 1e-6;
                // What the device sends: q_hm (world→body), so that q_hm*·M = W·q_true; gyro_s = M·ω_body.
                const QQuaternion qHm   = (toQt(hmWorld) * toQt(qt(t)) * Minv).normalized().conjugated();
                const QVector3D   gyroS = M.rotatedVector(toQt(bodyRateDps(qt, t)));
                // …and the fuser's composition of a HackMotion lane, line for line.
                s.qAnat.push_back(imu_calibration::toAnatomical(QQuaternion(), qHm.conjugated(), M));
                s.gyroDps.push_back(Minv.rotatedVector(gyroS));
                s.accelG.push_back(QVector3D());   // gravity-removed on a wG3; segment_rates does not read it
            }
            return s;
        };
        // Where ψ' (φ' for the arm) crosses zero, negative → positive, before the downswing's peak.
        const auto truthCross = [](const TrunkMotion &m) {
            double lo = 3.5, hi = 3.95;
            for (int it = 0; it < 80; ++it) {
                const double mid = 0.5 * (lo + hi);
                (m.psiRateDps(mid) < 0.0 ? lo : hi) = mid;
            }
            return 0.5 * (lo + hi);
        };
        // The emitted curve's zero crossings from 100 ms after address to impact (it is exactly 0 before
        // the takeaway): how many, the first one's instant (linear between the bracketing samples), and
        // the sign either side of it.
        struct Crossing { int count = 0; double tS = std::nan(""); int before = 0, after = 0; };
        const auto crossingOf = [](const MetricSeries &m) {
            Crossing c;
            int lastSign = 0;
            size_t lastI = 0;
            for (size_t i = 0; i < m.t_us.size() && i < m.value.size(); ++i) {
                const double ts = double(m.t_us[i]) * 1e-6;
                if (ts < kAddressS + 0.1 || ts > kImpactS || m.value[i] == 0.0) continue;
                const int sgn = m.value[i] > 0.0 ? 1 : -1;
                if (lastSign != 0 && sgn != lastSign) {
                    if (++c.count == 1) {
                        const double v0 = m.value[lastI], v1 = m.value[i];
                        const double t0 = double(m.t_us[lastI]) * 1e-6, t1 = ts;
                        c.tS     = t0 + (t1 - t0) * (-v0) / (v1 - v0);
                        c.before = lastSign;
                        c.after  = sgn;
                    }
                }
                lastSign = sgn;
                lastI    = i;
            }
            return c;
        };
        const auto ratesFor = [](const Run &run, const FusedStreams &fs, bool leadIsLeft) {
            SegmentRatesInputs in;
            in.streams    = &fs;
            in.leadIsLeft = leadIsLeft;
            in.phases     = &run.phases;
            in.impactUs   = us(kImpactS);
            return buildSegmentRates(in, SegmentRatesConfig {});
        };
        const auto streamsFor = [&](const Run &run) {
            FusedStreams fs;
            fs.timeGrid = run.grid;
            fs.segments = { run.pel.stream, run.thx.stream, wg3Forearm(run.grid, run.rig.leftHanded) };
            return fs;
        };

        const double tP = truthCross(base.pel), tT = truthCross(base.thx), tA = truthCross(armSwing);
        std::printf("    truth: zero crossings pelvis %.4f s, chest %.4f s, forearm %.4f s (arms' top %.4f); "
                    "peaks %.3f / %.3f / %.3f s, forearm %.0f °/s\n",
                    tP, tT, tA, kTopS, base.pel.peakS, base.thx.peakS, armSwing.peakS, armSwing.peakRateDps());

        // THE TOLERANCES. The trunk's crossing is read off a symmetric smoothing of a rate that passes
        // through zero cleanly: one sample (5 ms). The arm's sign is HELD until the rate along the plane
        // normal clears 15 × gyroNoiseDps (30 °/s) on the far side, so its curve reads −|ω⊥| a little past
        // the true reversal and then steps to +30 °/s, and the 25 ms smoothing window spreads that step:
        // one derivative window (25 ms). TOGETHER: the synthetic truth spreads the three crossings over
        // tA − tP (the hips lead the transition); allowed that plus the arm's 25 ms.
        const double tolTrunkS = 0.005, tolArmS = 0.025;
        const double spreadMaxS = (tA - tP) + tolArmS;

        struct MountCase { const char *name; std::function<void(Rig &)> set; };
        const MountCase mounts[] = {
            { "tilted mounts", [](Rig &) {} },
            { "δ = 10°", [](Rig &r) { r.pelMount.yawDeg = 10.0; r.thxMount.yawDeg = 10.0; } },
            { "ESKF + 17° drift", [](Rig &r) { r.eskf = true; r.driftDeg = 17.0; } },
            { "face-in chest", [](Rig &r) { r.thxMount.extra = rz(180); } },
        };
        SegmentRatesResult rhNominal, lhNominal;
        for (int lh = 0; lh < 2; ++lh) {
            for (const MountCase &mc : mounts) {
                Rig rg = base;
                mc.set(rg);
                rg.leftHanded = lh != 0;
                const Run run = build(rg);
                const FusedStreams fs = streamsFor(run);
                const SegmentRatesResult res = ratesFor(run, fs, !rg.leftHanded);
                if (&mc == &mounts[0]) (lh ? lhNominal : rhNominal) = res;

                const Crossing cp = crossingOf(res.pelvis.series), ct = crossingOf(res.thorax.series),
                               ca = crossingOf(res.leadArm.series);
                const KsNode *np = res.sequence.node(SeqSegment::Pelvis), *nt = res.sequence.node(SeqSegment::Thorax),
                             *na = res.sequence.node(SeqSegment::LeadArm);
                const char *hand = lh ? "LH" : "RH";
                const auto pol = [](const Crossing &c) { return c.before < 0 && c.after > 0 ? "−→+" : c.before > 0 && c.after < 0 ? "+→−" : "?"; };
                std::printf("    %s %-17s crossings pelvis %.4f %s, chest %.4f %s, forearm %.4f %s s (×%d/%d/%d); "
                            "spread %.0f ms; peaks %.3f < %.3f < %.3f s (%.0f / %.0f / %.0f °/s)\n",
                            hand, mc.name, cp.tS, pol(cp), ct.tS, pol(ct), ca.tS, pol(ca), cp.count, ct.count, ca.count,
                            (std::max({ cp.tS, ct.tS, ca.tS }) - std::min({ cp.tS, ct.tS, ca.tS })) * 1e3,
                            np ? np->tPeakUs * 1e-6 : 0.0, nt ? nt->tPeakUs * 1e-6 : 0.0, na ? na->tPeakUs * 1e-6 : 0.0,
                            np ? np->peakDps : 0.0, nt ? nt->peakDps : 0.0, na ? na->peakDps : 0.0);

                char lbl[140];
                std::snprintf(lbl, sizeof lbl, "h: %s %s: routes pelvisImu / thoraxImu / leadArmImus (the wG3 forearm)", hand, mc.name);
                CHECK(lbl, run.pel.al.valid && run.thx.al.valid && res.pelvis.routeId == QLatin1String("pelvisImu")
                               && res.thorax.routeId == QLatin1String("thoraxImu")
                               && res.leadArm.routeId == QLatin1String("leadArmImus"));
                std::snprintf(lbl, sizeof lbl, "h: %s %s: each curve crosses zero ONCE, address+100 ms → impact", hand, mc.name);
                CHECK(lbl, cp.count == 1 && ct.count == 1 && ca.count == 1);
                std::snprintf(lbl, sizeof lbl, "h: %s %s: crossings at the truth (trunk ±5 ms, forearm ±25 ms)", hand, mc.name);
                CHECK(lbl, std::fabs(cp.tS - tP) <= tolTrunkS && std::fabs(ct.tS - tT) <= tolTrunkS
                               && std::fabs(ca.tS - tA) <= tolArmS);
                std::snprintf(lbl, sizeof lbl, "h: %s %s: together at the top (spread ≤ %.0f ms), pelvis ≤ chest ≤ forearm",
                              hand, mc.name, spreadMaxS * 1e3);
                CHECK(lbl, std::max({ cp.tS, ct.tS, ca.tS }) - std::min({ cp.tS, ct.tS, ca.tS }) <= spreadMaxS
                               && cp.tS <= ct.tS && ct.tS <= ca.tS);
                std::snprintf(lbl, sizeof lbl, "h: %s %s: one polarity — negative going back, positive coming down", hand, mc.name);
                CHECK(lbl, cp.before < 0 && cp.after > 0 && ct.before < 0 && ct.after > 0 && ca.before < 0 && ca.after > 0);
                std::snprintf(lbl, sizeof lbl, "h: %s %s: peaks placed, positive, pelvis < chest < forearm", hand, mc.name);
                CHECK(lbl, np && nt && na && np->placed && nt->placed && na->placed && np->peakDps > 0.0
                               && nt->peakDps > 0.0 && na->peakDps > 0.0 && np->tPeakUs < nt->tPeakUs
                               && nt->tPeakUs < na->tPeakUs);
            }
        }
        // The forearm is handedness-free by construction (its sign is the swing's own); the mirror must
        // not move it. Nominal mounts, every sample.
        {
            const std::vector<double> &a = rhNominal.leadArm.series.value, &b = lhNominal.leadArm.series.value;
            double dMax = a.size() == b.size() && !a.empty() ? 0.0 : 1e9;
            for (size_t i = 0; i < std::min(a.size(), b.size()); ++i) dMax = std::max(dMax, std::fabs(a[i] - b[i]));
            CHECK_NEAR("h: the left-hander's forearm curve = the right-hander's (max |Δ| °/s)", dMax, 0.0, 0.01);
            const KsNode *n0 = rhNominal.sequence.node(SeqSegment::LeadArm), *n1 = lhNominal.sequence.node(SeqSegment::LeadArm);
            CHECK_NEAR("h: …forearm peak, 3 % of the truth's 833 °/s (windowed mean)", n0 ? n0->peakDps : 0.0,
                       armSwing.peakRateDps(), 0.03 * armSwing.peakRateDps());
            CHECK_NEAR("h: …forearm peak instant (ms), one sample", n1 ? n1->tPeakUs * 1e-3 : 0.0, armSwing.peakS * 1e3, 5.0);
        }

        // THE FAILURE THIS CHECK EXISTS FOR. A left-hander analysed as right-handed (`leadIsLeft` wrong:
        // a profile or session handedness slip). The trunk's sign comes from `leadIsLeft`, the arm's from
        // its own swing, so only the trunk turns over: pelvis and chest read + going back and − coming
        // down while the forearm still reads − → +. A chart with all four nodes judged from that would
        // be a sequence fault the golfer never made; the polarity check above sees it.
        {
            Rig rg = base;
            rg.leftHanded = true;
            const Run run = build(rg);
            const FusedStreams fs = streamsFor(run);
            const SegmentRatesResult res = ratesFor(run, fs, /*leadIsLeft (wrong)*/ true);
            const Crossing cp = crossingOf(res.pelvis.series), ct = crossingOf(res.thorax.series),
                           ca = crossingOf(res.leadArm.series);
            const KsNode *np = res.sequence.node(SeqSegment::Pelvis), *nt = res.sequence.node(SeqSegment::Thorax);
            std::printf("    LH analysed as RH: pelvis %.4f %s, chest %.4f %s, forearm %.4f %s; trunk nodes %s %.0f / %s %.0f °/s\n",
                        cp.tS, cp.before > 0 ? "+→−" : "−→+", ct.tS, ct.before > 0 ? "+→−" : "−→+",
                        ca.tS, ca.before > 0 ? "+→−" : "−→+",
                        np && np->placed ? "placed" : "unplaced", np ? np->peakDps : 0.0,
                        nt && nt->placed ? "placed" : "unplaced", nt ? nt->peakDps : 0.0);
            CHECK("h: LH analysed as RH: the trunk turns over (+ going back, − coming down)…",
                  cp.before > 0 && cp.after < 0 && ct.before > 0 && ct.after < 0);
            CHECK("h: …while the forearm does not (− → +), so the polarity check fails it",
                  ca.before < 0 && ca.after > 0
                      && !(cp.before < 0 && cp.after > 0 && ct.before < 0 && ct.after > 0 && ca.before < 0 && ca.after > 0));
        }
    }

    // ── §i the face-in cue at address (design §6.3, W2.1d) ───────────────────────────────────
    // §f showed stand tall and the turns cannot see a sensor worn label-in; the address bend's sign
    // can. Each swing checks its own address (a per-swing analysis cannot see the session's first).
    std::printf("-- i. the face-in cue at address --\n");
    {
        const auto faceIn = [](const BodyRotationResult &res) {
            std::vector<const GateRecord *> out;
            for (const GateRecord &g : res.gates)
                if (g.gate == QLatin1String("body.pelvisFaceIn") || g.gate == QLatin1String("body.thoraxFaceIn"))
                    out.push_back(&g);
            return out;
        };
        const auto hasSeries = [](const std::vector<MetricSeries> &all, const char *key) {
            for (const MetricSeries &m : all)
                if (m.key == QLatin1String(key)) return true;
            return false;
        };

        // The right mount: forward bends read, no cue.
        CHECK_NEAR("i: right mount: hip address bend read (deg)", rot.pelvisAddressBendDeg, 20.0, 0.5);
        CHECK_NEAR("i: right mount: chest address bend read (deg)", rot.thoraxAddressBendDeg, 40.0, 0.5);
        CHECK("i: right mount: no face-in record", faceIn(rot).empty());

        // A face-in CHEST at a 40° address: one record, on the chest only, downgraded.
        Rig fc = base;
        fc.thxMount.extra = rz(180);
        const Run Fc = build(fc);
        const BodyRotationResult rfc = rotation(Fc);
        const std::vector<const GateRecord *> gc = faceIn(rfc);
        CHECK_NEAR("i: face-in chest: its address bend reads backward (deg)", rfc.thoraxAddressBendDeg, -40.0, 0.5);
        CHECK("i: face-in chest: exactly one record, body.thoraxFaceIn on thoraxRotation",
              gc.size() == 1 && gc[0]->gate == QLatin1String("body.thoraxFaceIn")
                  && gc[0]->subject == QLatin1String("thoraxRotation"));
        CHECK("i: …DOWNGRADED (shown with its warning), not refused or withheld",
              gc.size() == 1 && gc[0]->effect == GateEffect::Downgraded);
        CHECK("i: …covers the chest's signed turn and the separation, nothing of the hips or the KS",
              gc.size() == 1 && gc[0]->covers.contains(QStringLiteral("thoraxRotationSigned"))
                  && gc[0]->covers.contains(QStringLiteral("xFactor")) && gc[0]->covers.contains(QStringLiteral("xFactorStretch"))
                  && !gc[0]->covers.contains(QStringLiteral("pelvisRotation")) && !gc[0]->covers.contains(QStringLiteral("pelvisRotationSigned"))
                  && !gc[0]->covers.contains(QStringLiteral("ks.thorax")));
        CHECK_NEAR("i: …its evidence carries the bend it compared (deg)",
                   gc.size() == 1 ? gc[0]->evidence.value(QStringLiteral("bendDeg")).toDouble() : 0.0, -40.0, 0.5);
        CHECK_NEAR("i: …and the limit (deg)",
                   gc.size() == 1 ? gc[0]->evidence.value(QStringLiteral("limitDeg")).toDouble() : 0.0, -5.0, 1e-9);
        CHECK_NEAR("i: face-in chest: the hip sensor beside it still reads forward (deg)", rfc.pelvisAddressBendDeg, 20.0, 0.5);
        // Shown, never hidden: every reading is still emitted, and the turn is still the truth.
        const std::vector<MetricSeries> sfc = buildBodyRotationSeries(rfc, Fc.phases);
        CHECK("i: face-in chest: every reading still emitted (turn, signed, separation)",
              hasSeries(sfc, "thoraxRotation") && hasSeries(sfc, "thoraxRotationSigned")
                  && hasSeries(sfc, "xFactor") && hasSeries(sfc, "xFactorStretch"));
        CHECK_NEAR("i: face-in chest: its turn at the top = the right mount's (deg)",
                   turnAt(rfc.thorax, kTopS), turnAt(rot.thorax, kTopS), 0.01);
        // The stage's gate list carries it, with no refusal beside it.
        const std::vector<GateRecord> stage = bodyRotationGates(rfc, Fc.fs, false, false);
        bool stageHas = false, stageRefuses = false;
        for (const GateRecord &g : stage) {
            stageHas = stageHas || g.gate == QLatin1String("body.thoraxFaceIn");
            stageRefuses = stageRefuses || g.effect == GateEffect::Refused;
        }
        CHECK("i: face-in chest: bodyRotationGates hands the record on, refusing nothing", stageHas && !stageRefuses);

        // A face-in PELVIS: the hips only.
        Rig fp = base;
        fp.pelMount.extra = rz(180);
        const Run Fp = build(fp);
        const BodyRotationResult rfp = rotation(Fp);
        const std::vector<const GateRecord *> gp = faceIn(rfp);
        CHECK_NEAR("i: face-in pelvis: its address bend reads backward (deg)", rfp.pelvisAddressBendDeg, -20.0, 0.5);
        CHECK("i: face-in pelvis: exactly one record, body.pelvisFaceIn on pelvisRotation, downgraded",
              gp.size() == 1 && gp[0]->gate == QLatin1String("body.pelvisFaceIn")
                  && gp[0]->subject == QLatin1String("pelvisRotation") && gp[0]->effect == GateEffect::Downgraded);
        CHECK("i: …covering the hips' signed turn and the separation, not the chest's",
              gp.size() == 1 && gp[0]->covers.contains(QStringLiteral("pelvisRotationSigned"))
                  && gp[0]->covers.contains(QStringLiteral("xFactor"))
                  && !gp[0]->covers.contains(QStringLiteral("thoraxRotationSigned")));

        // Both worn label-in: one record each.
        Rig fb = base;
        fb.pelMount.extra = rz(180);
        fb.thxMount.extra = rz(180);
        const BodyRotationResult rfb = rotation(build(fb));   // named: faceIn() points into it
        const std::vector<const GateRecord *> gb = faceIn(rfb);
        CHECK("i: both face-in: one record per sensor",
              gb.size() == 2 && gb[0]->gate == QLatin1String("body.pelvisFaceIn") && gb[1]->gate == QLatin1String("body.thoraxFaceIn"));

        // A left-hander: the bend is sagittal, so the cue is handedness-free.
        Rig lc = base;
        lc.leftHanded = true;
        const BodyRotationResult rlc = rotation(build(lc));
        CHECK("i: left-hander, right mount: no face-in record", faceIn(rlc).empty());
        lc.thxMount.extra = rz(180);
        const BodyRotationResult rlf = rotation(build(lc));
        const std::vector<const GateRecord *> gl = faceIn(rlf);
        CHECK_NEAR("i: left-hander, face-in chest: its address bend reads backward (deg)", rlf.thoraxAddressBendDeg, -40.0, 0.5);
        CHECK("i: left-hander, face-in chest: body.thoraxFaceIn only, downgraded",
              gl.size() == 1 && gl[0]->gate == QLatin1String("body.thoraxFaceIn") && gl[0]->effect == GateEffect::Downgraded);

        // The threshold: a near-upright address on the right mount does not fire; a shallow 10° hip
        // bend worn label-in does.
        Rig up = base;
        up.pel = pelvisMotion(3.0);
        const BodyRotationResult rup = rotation(build(up));
        CHECK_NEAR("i: near-upright hips (3°), right mount: bend read (deg)", rup.pelvisAddressBendDeg, 3.0, 0.5);
        CHECK("i: …no face-in record", faceIn(rup).empty());
        up.pel = pelvisMotion(10.0);
        up.pelMount.extra = rz(180);
        const BodyRotationResult rsh = rotation(build(up));
        const std::vector<const GateRecord *> gs = faceIn(rsh);
        CHECK("i: shallow hips (10°) worn label-in: body.pelvisFaceIn fires",
              gs.size() == 1 && gs[0]->gate == QLatin1String("body.pelvisFaceIn"));

        // No Address on the ladder: the reference falls back to the first sample (stand tall here),
        // which says nothing about a mount, so nothing is read and nothing fires.
        Run noAddr = Fc;
        noAddr.phases.erase(std::remove_if(noAddr.phases.begin(), noAddr.phases.end(),
                                           [](const PhaseEvent &e) { return e.phase == Phase::Address; }),
                            noAddr.phases.end());
        const BodyRotationResult rna = rotation(noAddr);
        CHECK("i: no Address on the ladder: no bend read, no record (face-in chest)",
              std::isnan(rna.thoraxAddressBendDeg) && faceIn(rna).empty());
    }

    std::printf("\n=== %s (%d failures) ===\n", g_fail ? "FAILURES PRESENT" : "ALL PASS", g_fail);
    return g_fail ? 1 : 0;
}
