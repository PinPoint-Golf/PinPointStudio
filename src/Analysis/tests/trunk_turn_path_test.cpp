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

// The trunk turn check end to end, from the device's bytes (26 Oct regression).
//
// On real sensors every turn-away attempt read "range 0.0 deg turn 0.0 S-I 0.0 axis 90.0" on both
// trunk sensors while the golfer turned ~90°. The live fusion filter (ESKF, selected in Settings)
// had diverged to NaN while the sensors were handled before stand tall, and latched there; the
// stand-tall gates read accel and gyro only, so they passed and APPLIED a NaN A; every turn read
// through it collapsed to the defaults. The existing tests could not see it: they hand the turn
// check a ready-made quaternion series.
//
// This test drives the path the app runs, with nothing pre-built:
//   synthetic 0x55 0x61 combined frames (accel g, gyro °/s, as the WT901BLE67 sends them)
//   → WT9011DCL_Base::receiveData → ImuBase::fuseRawImu (the real Madgwick / ESKF)
//   → quaternionUpdated → the ImuInstance data lambda's tap push (stamp + makeImuSample)
//   → ImuSampleTap::take into a trunk_ceremony_file::Lane
//   → trunk_stand_tall::solveLatest (calibrateTrunk's solve) for A and M
//   → trunk_ceremony_file::runTurn (TrunkCeremonyRecorder::checkTurn's check)
// for two sensors worn on the hips and chest, handled roughly first (the ESKF's divergence), then
// stood tall, then turned away twice. Run once per filter; the ESKF must never diverge (no
// re-seed by fuseRawImu's safety net) and must read the turns within 2° of Madgwick (#104).
// Then the arm strap (arm_swing_synth.h): calibration and a 1800 °/s, several-g swing through the
// same driver path, ESKF against Madgwick and the truth.

#include "wt9011dcl_ble.h"
#include "arm_swing_synth.h"
#include "imu_sample_tap.h"
#include "trunk_ceremony_file.h"
#include "trunk_stand_tall.h"

#include <QByteArray>
#include <QQuaternion>
#include <QVector3D>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>

namespace tcf = trunk_ceremony_file;

static int g_fail = 0;

static void check(const char *label, bool ok, const char *fmt = "", double v = 0.0)
{
    std::printf("  [%s] %s", ok ? "PASS" : "FAIL", label);
    if (*fmt) { std::printf("  "); std::printf(fmt, v); }
    std::printf("\n");
    if (!ok) ++g_fail;
}

class TestBle : public WT9011DCL_BLE {
public:
    using WT9011DCL_Base::receiveData;   // protected frame parser → public
};

// One 20-byte combined frame: 0x55 0x61, accel ×3 (±16 g), gyro ×3 (±2000 °/s), angle ×3 (unused
// by the fusion, left 0), each int16 LE.
static QByteArray combinedFrame(const QVector3D &accG, const QVector3D &gyroDps)
{
    QByteArray f(20, char(0));
    f[0] = char(0x55); f[1] = char(0x61);
    const auto put = [&](int off, double v, double fullScale) {
        const long r = std::lround(v / fullScale * 32768.0);
        const qint16 s = qint16(std::clamp(r, -32768L, 32767L));
        f[off]     = char(s & 0xFF);
        f[off + 1] = char((s >> 8) & 0xFF);
    };
    put(2, accG.x(), 16.0);  put(4, accG.y(), 16.0);  put(6, accG.z(), 16.0);
    put(8, gyroDps.x(), 2000.0); put(10, gyroDps.y(), 2000.0); put(12, gyroDps.z(), 2000.0);
    return f;
}

static constexpr int     kRateHz = 200;
static constexpr int64_t kDtUs   = 1000000 / kRateHz;
static constexpr double  kDeg    = 3.14159265358979323846 / 180.0;

// One simulated sensor: its true sensor→world orientation (world +Z up), the driver it feeds and
// the ceremony tap that driver's data path pushes into.
struct Sensor {
    QString                                 serial;
    QQuaternion                             R;            // truth, sensor → world
    std::unique_ptr<TestBle>                imu;
    std::shared_ptr<pinpoint::ImuSampleTap> tap;
    int                                     nonFiniteEmitted = 0;
};

static int64_t g_nowUs = 0;   // the host clock the data path stamps with (EventBuffer::nowMicros live)

// The world-frame angular velocity w (rad/s) and linear acceleration aLin (g) for one sample:
// integrate the truth, synthesise what the device would measure, and feed it through the driver.
static void step(Sensor &s, const QVector3D &wWorldRad, const QVector3D &aLinWorldG)
{
    const double dt = double(kDtUs) / 1e6;
    const float ang = float(double(wWorldRad.length()) * dt / kDeg);
    if (ang > 0.0f)
        s.R = (QQuaternion::fromAxisAndAngle(wWorldRad.normalized(), ang) * s.R).normalized();
    const QQuaternion inv = s.R.conjugated();
    const QVector3D accBody  = inv.rotatedVector(QVector3D(0.0f, 0.0f, 1.0f) + aLinWorldG);   // specific force
    const QVector3D gyroBody = inv.rotatedVector(wWorldRad) / float(kDeg);                      // °/s
    s.imu->receiveData(combinedFrame(accBody, gyroBody));
}

static QQuaternion wornMount(float leanDeg)
{
    // USB up (sensor +Y = world up), label out (sensor +Z = world +Y, anterior), so sensor +X =
    // world −X; then a forward lean about the golfer's medio-lateral axis.
    const QQuaternion upright = QQuaternion::fromAxes(QVector3D(-1, 0, 0), QVector3D(0, 0, 1), QVector3D(0, 1, 0));
    return (QQuaternion::fromAxisAndAngle(1, 0, 0, leanDeg) * upright).normalized();
}

struct FilterRun {
    imu_calibration::TrunkTurnRecord rec;
    quint64                          divergences[2] = { 0, 0 };
};

static FilterRun runFilter(OrientationFilterType filter)
{
    std::printf("\n-- filter %s --\n", toString(filter));
    Sensor sens[2];   // [0] pelvis, [1] thorax
    const float leanDeg[2] = { 13.0f, 26.0f };   // the long-axis angles the 26 Oct stand tall logged
    const float turnDeg[2] = { 45.0f, 90.0f };   // design §6.7: hips about 45°, chest about 90° away
    for (int k = 0; k < 2; ++k) {
        Sensor &s = sens[k];
        s.serial = k == 0 ? QStringLiteral("{pelvis-0000}") : QStringLiteral("{thorax-0000}");
        s.R      = QQuaternion::fromAxisAndAngle(0, 0, 1, 30.0f * float(k));   // lying flat on a desk
        s.imu    = std::make_unique<TestBle>();
        s.imu->setNominalSampleRateHz(kRateHz);
        s.imu->setOrientationFilter(filter);
        s.tap    = std::make_shared<pinpoint::ImuSampleTap>();
        // ImuInstance's data lambda (imu_instance.cpp): the same stamp and makeImuSample, pushed
        // to the ceremony tap while it records.
        QObject::connect(s.imu.get(), &WT9011DCL_Base::quaternionUpdated, s.imu.get(),
                         [&s](const WT9011DCL_Base::QuaternionData &q) {
                             if (!(std::isfinite(q.w) && std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z)))
                                 ++s.nonFiniteEmitted;
                             const auto a = s.imu->accelData();
                             const auto g = s.imu->gyroData();
                             if (s.tap->isActive())
                                 s.tap->push(g_nowUs, pinpoint::makeImuSample(a.x, a.y, a.z, g.x, g.y, g.z,
                                                                              q.w, q.x, q.y, q.z));
                         },
                         Qt::DirectConnection);
        s.tap->start(120'000);   // the recorder's kMaxSamplesPerLane
    }

    const auto run = [&](double seconds, auto &&motion) {
        const int n = int(std::lround(seconds * kRateHz));
        for (int i = 0; i < n; ++i) {
            g_nowUs += kDtUs;
            const double t = double(i) / kRateHz;
            for (int k = 0; k < 2; ++k) {
                QVector3D w, a;
                motion(k, t, &w, &a);
                step(sens[k], w, a);
            }
        }
    };
    const auto still = [](int, double, QVector3D *w, QVector3D *a) { *w = QVector3D(); *a = QVector3D(); };

    g_nowUs = 1'000'000'000;
    const int64_t beganUs = g_nowUs;
    run(1.0, still);                                                   // on the desk: the filter seeds
    run(4.0, [](int k, double t, QVector3D *w, QVector3D *a) {         // handled roughly, strapped on
        const double s = 1.0 + 0.2 * k;
        *w = QVector3D(float(900 * std::sin(13 * t * s)), float(600 * std::sin(7 * t)), float(800 * std::cos(11 * t))) * float(kDeg);
        *a = QVector3D(float(3 * std::sin(17 * t)), float(3 * std::cos(9 * t * s)), float(2 * std::sin(5 * t)));
    });
    // Onto the body: a smooth rotation, over 2 s, from wherever the handling left it to the worn
    // mount (constant world-frame rate, so the gyro the device reports is exactly the rotation).
    QVector3D onto[2];
    for (int k = 0; k < 2; ++k) {
        const QQuaternion d = (wornMount(leanDeg[k]) * sens[k].R.conjugated()).normalized();   // world-frame delta
        float ax, ay, az, angDeg;
        d.getAxisAndAngle(&ax, &ay, &az, &angDeg);
        onto[k] = QVector3D(ax, ay, az).normalized() * float(angDeg * kDeg / 2.0);
    }
    run(2.0, [&](int k, double, QVector3D *w, QVector3D *a) { *w = onto[k]; *a = QVector3D(); });
    run(1.0, still);                                                   // settle

    // ── Stand tall: 3 s upright and still; calibrateTrunk's 2 s window ends at the step's end ──
    tcf::Step stand;
    stand.name    = tcf::stepStandTall();
    stand.startUs = g_nowUs;
    run(3.0, still);
    stand.endUs = g_nowUs;

    // ── Turn away (right-hander: about −Z), twice, the step opening a second before ──
    tcf::Step turn;
    turn.name    = tcf::stepTurnAway();
    turn.startUs = g_nowUs;
    run(1.0, still);
    run(5.0, [&](int k, double t, QVector3D *w, QVector3D *a) {
        // two out-and-back turns, 2.5 s each: θ(t) = −peak·sin²(πt/2.5)
        const double peak = turnDeg[k] * kDeg, P = 2.5;
        const double dTheta = -peak * 3.14159265358979323846 / P * std::sin(2 * 3.14159265358979323846 * t / P);
        *w = QVector3D(0, 0, float(dTheta));
        *a = QVector3D(float(0.05 * std::sin(3 * t)), float(0.05 * std::cos(4 * t)), 0.0f);   // the body sways a little
    });
    run(1.0, still);
    turn.endUs = g_nowUs;

    // ── The ceremony, as TrunkCeremonyRecorder holds it ──
    tcf::Ceremony c;
    c.beganUs = beganUs;
    const QString roles[2] = { QStringLiteral("pelvis"), QStringLiteral("thorax") };
    for (int k = 0; k < 2; ++k) {
        sens[k].tap->stop();
        tcf::Lane L;
        L.role             = roles[k];
        L.serial           = sens[k].serial;
        L.instrument       = tcf::instrumentWitmotion();
        L.configuredRateHz = kRateHz;
        L.overCap          = sens[k].tap->take(L.tUs, L.rows);
        c.lanes.push_back(std::move(L));
    }
    c.steps = { stand, turn };

    for (int k = 0; k < 2; ++k)
        std::printf("  %s: %zu samples, %d non-finite quaternions emitted, %llu filter divergence(s)\n",
                    qPrintable(roles[k]), c.lanes[size_t(k)].tUs.size(), sens[k].nonFiniteEmitted,
                    static_cast<unsigned long long>(sens[k].imu->fusionDivergences()));
    FilterRun out;
    for (int k = 0; k < 2; ++k) {
        out.divergences[k] = sens[k].imu->fusionDivergences();
        check(k == 0 ? "pelvis: the fusion never emitted a non-finite quaternion"
                     : "thorax: the fusion never emitted a non-finite quaternion",
              sens[k].nonFiniteEmitted == 0, "(%g emitted)", double(sens[k].nonFiniteEmitted));
        // Issue #104: the re-seed in fuseRawImu is a safety net, not the fix. The filter itself
        // must never diverge, or the heading restarts mid-turn.
        check(k == 0 ? "pelvis: the filter never diverged (no re-seed)" : "thorax: the filter never diverged (no re-seed)",
              out.divergences[k] == 0, "(%g re-seeds)", double(out.divergences[k]));
    }

    // ── Stand tall (ImuInstance::calibrateTrunk) ──
    tcf::TurnInputs in;
    in.direction  = imu_calibration::TrunkTurnDirection::Away;
    in.leftHanded = false;
    tcf::TurnSensorInput *inputs[2] = { &in.pelvis, &in.thorax };
    for (int k = 0; k < 2; ++k) {
        const tcf::Lane &L = c.lanes[size_t(k)];
        const auto sensor = k == 0 ? imu_calibration::TrunkSensor::Pelvis : imu_calibration::TrunkSensor::Thorax;
        const trunk_stand_tall::LiveSolve s = trunk_stand_tall::solveLatest(L.tUs, L.rows, stand.endUs, kRateHz, sensor);
        std::printf("  %s stand tall: %s %s (long axis %.1f deg, face %.1f deg)\n", qPrintable(roles[k]),
                    s.alignment.valid ? "VALID" : "INVALID", qPrintable(s.alignment.record.failure),
                    double(s.alignment.record.longAxisFromUpDeg), double(s.alignment.record.faceFromHorizontalDeg));
        check(k == 0 ? "pelvis: stand tall valid" : "thorax: stand tall valid", s.alignment.valid);
        // TrunkCeremonyRecorder::checkTurn: the step's window, no later than the lane's last sample,
        // read through the A/M the instance holds (the solve's, applied only when valid).
        tcf::TurnSensorInput &si = *inputs[k];
        si.serial        = L.serial;
        si.windowStartUs = turn.startUs;
        si.windowEndUs   = std::min(turn.endUs, L.tUs.empty() ? turn.endUs : L.tUs.back());
        si.A             = s.alignment.A;
        si.M             = s.alignment.M;
    }

    // ── The window, as checkTurn now logs it ──
    for (int k = 0; k < 2; ++k) {
        const tcf::TurnWindowStats st = tcf::turnWindowStats(c, *inputs[k]);
        std::printf("  %s window: %zu samples, first +%.3f s last +%.3f s, spread %.1f deg, non-finite %zu, A/M %s\n",
                    qPrintable(roles[k]), st.samples, double(st.firstUs - inputs[k]->windowStartUs) / 1e6,
                    double(st.lastUs - inputs[k]->windowStartUs) / 1e6, st.spreadDeg, st.nonFinite,
                    st.alignFinite ? "finite" : "NON-FINITE");
        const size_t expect = size_t(7 * kRateHz);
        check(k == 0 ? "pelvis: the window holds the step's samples, once each" : "thorax: the window holds the step's samples, once each",
              st.samples >= expect - 2 && st.samples <= expect + 1, "(%g)", double(st.samples));
        check(k == 0 ? "pelvis: the raw quaternion moved through the turn" : "thorax: the raw quaternion moved through the turn",
              std::abs(st.spreadDeg - turnDeg[k]) < 10.0, "(spread %.1f deg)", st.spreadDeg);
        check(k == 0 ? "pelvis: no non-finite sample, A/M finite" : "thorax: no non-finite sample, A/M finite",
              st.nonFinite == 0 && st.alignFinite);
    }

    // ── The turn check ──
    const imu_calibration::TrunkTurnRecord rec = tcf::runTurn(c, in);
    std::printf("  turn away: %s %s — hips range %.1f turn %.1f S-I %.1f axis %.1f; chest range %.1f turn %.1f S-I %.1f axis %.1f\n",
                rec.valid ? "VALID" : "INVALID", qPrintable(rec.failure),
                double(rec.pelvis.rangeDeg), double(rec.pelvis.turnDeg), double(rec.pelvis.siTurnDeg), double(rec.pelvis.axisFromVerticalDeg),
                double(rec.thorax.rangeDeg), double(rec.thorax.turnDeg), double(rec.thorax.siTurnDeg), double(rec.thorax.axisFromVerticalDeg));
    check("turn away valid", rec.valid);
    check("hips turned about 45 deg away (−)", std::abs(rec.pelvis.turnDeg + 45.0f) < 8.0f, "(%.1f)", double(rec.pelvis.turnDeg));
    check("chest turned about 90 deg away (−)", std::abs(rec.thorax.turnDeg + 90.0f) < 8.0f, "(%.1f)", double(rec.thorax.turnDeg));
    check("hips axis near vertical", rec.pelvis.axisFromVerticalDeg < 10.0f, "(%.1f)", double(rec.pelvis.axisFromVerticalDeg));
    check("chest axis near vertical", rec.thorax.axisFromVerticalDeg < 10.0f, "(%.1f)", double(rec.thorax.axisFromVerticalDeg));
    out.rec = rec;
    return out;
}

// ── The arm strap, watch-style (issue #104's arm regression check) ─────────────────────────────
// arm_swing_synth.h's sequence — arm hanging still, the calibration's reference window, a forearm
// twist and an elbow flex for the functional axes, then a swing whose forearm roll peaks at
// 1800 °/s with several g on the accelerometer, then a held finish — through the real driver and
// fuseRawImu, then the arm calibration solve (solveSegment, as ImuInstance::setFunctionalCalibration)
// and q_anat = A·q·M through the swing, against the same composition of the truth.
struct ArmRun {
    std::vector<QQuaternion> anat, anatTruth;
    std::vector<arm_synth::Phase> phase;
    quint64 divergences = 0;
    int     nonFinite = 0;
    imu_calibration::Alignment al;
};

static double quatAngleDeg(const QQuaternion &a, const QQuaternion &b)
{
    const double d = std::min(1.0, std::abs(double(QQuaternion::dotProduct(a.normalized(), b.normalized()))));
    return 2.0 * std::acos(d) / kDeg;
}

static ArmRun runArm(OrientationFilterType filter, const std::vector<arm_synth::Sample> &S)
{
    std::printf("\n-- arm strap, filter %s --\n", toString(filter));
    ArmRun r;
    TestBle imu;
    imu.setNominalSampleRateHz(arm_synth::kRateHz);
    imu.setOrientationFilter(filter);
    std::vector<QQuaternion> q;
    QObject::connect(&imu, &WT9011DCL_Base::quaternionUpdated, &imu,
                     [&](const WT9011DCL_Base::QuaternionData &d) {
                         if (!(std::isfinite(d.w) && std::isfinite(d.x) && std::isfinite(d.y) && std::isfinite(d.z)))
                             ++r.nonFinite;
                         q.emplace_back(d.w, d.x, d.y, d.z);
                     },
                     Qt::DirectConnection);
    for (const arm_synth::Sample &s : S)
        imu.receiveData(combinedFrame(s.accG, s.gyroDps));
    r.divergences = imu.fusionDivergences();
    check("arm: one fused orientation per frame", q.size() == S.size(), "(%g)", double(q.size()));
    if (q.size() != S.size()) return r;

    // The solve: the reference is the last sample of the still window, gravity its mean reading,
    // the long and flexion axes the dominant gyro axes of the twist and the flex.
    size_t refEnd = 0;
    QVector3D accSum;
    std::vector<QVector3D> twist, flex;
    for (size_t i = 0; i < S.size(); ++i) {
        if (S[i].phase == arm_synth::Ref)   { refEnd = i; accSum += S[i].accG; }
        if (S[i].phase == arm_synth::Twist) twist.push_back(S[i].gyroDps);
        if (S[i].phase == arm_synth::Flex)  flex.push_back(S[i].gyroDps);
    }
    r.al = imu_calibration::solveSegment(q[refEnd], -accSum.normalized(),
                                         arm_synth::principalAxis(twist), arm_synth::principalAxis(flex));
    const QQuaternion Atruth = (S[refEnd].truth * r.al.M).conjugated().normalized();
    for (size_t i = 0; i < S.size(); ++i) {
        r.anat.push_back(imu_calibration::toAnatomical(r.al.A, q[i], r.al.M));
        r.anatTruth.push_back(imu_calibration::toAnatomical(Atruth, S[i].truth, r.al.M));
        r.phase.push_back(S[i].phase);
    }
    std::printf("  %d non-finite emitted, %llu divergence(s); M %.3f deg from nominalArmMount, axes %.1f deg apart\n",
                r.nonFinite, static_cast<unsigned long long>(r.divergences),
                quatAngleDeg(r.al.M, imu_calibration::nominalArmMount()), double(r.al.axisAngleDeg));
    check("arm: never a non-finite orientation", r.nonFinite == 0, "(%g)", double(r.nonFinite));
    check("arm: the filter never diverged (no re-seed)", r.divergences == 0, "(%g)", double(r.divergences));
    check("arm: the solve is valid and lands on the strap's nominal mount (< 1 deg)",
          r.al.valid && quatAngleDeg(r.al.M, imu_calibration::nominalArmMount()) < 1.0,
          "(%.3f deg)", quatAngleDeg(r.al.M, imu_calibration::nominalArmMount()));
    return r;
}

static void armRegression()
{
    const std::vector<arm_synth::Sample> S = arm_synth::generate();
    double peak[3] = { 0, 0, 0 }, aMax = 0;
    for (const auto &s : S) {
        for (int k = 0; k < 3; ++k) peak[k] = std::max(peak[k], double(std::abs(s.gyroDps[k])));
        aMax = std::max(aMax, double(s.accG.length()));
    }
    std::printf("\n=== arm strap sequence: gyro peak per sensor axis X %.0f Y %.0f Z %.0f deg/s, |a| peak %.2f g ===\n",
                peak[0], peak[1], peak[2], aMax);
    check("arm sequence: every gyro axis inside the device's +-2000 deg/s", peak[0] < 2000 && peak[1] < 2000 && peak[2] < 2000);
    check("arm sequence: the swing reads several g", aMax > 4.0, "(%.2f g)", aMax);

    const ArmRun m = runArm(OrientationFilterType::Madgwick, S);
    const ArmRun e = runArm(OrientationFilterType::Eskf, S);
    if (m.anat.size() != S.size() || e.anat.size() != S.size()) return;

    double errM = 0, errE = 0, dEM = 0;
    for (size_t i = 0; i < S.size(); ++i) {
        if (S[i].phase != arm_synth::Swing && S[i].phase != arm_synth::Finish) continue;
        errM = std::max(errM, quatAngleDeg(m.anat[i], m.anatTruth[i]));
        errE = std::max(errE, quatAngleDeg(e.anat[i], e.anatTruth[i]));
        dEM  = std::max(dEM,  quatAngleDeg(e.anat[i], m.anat[i]));
    }
    const double endM = quatAngleDeg(m.anat.back(), m.anatTruth.back());
    const double endE = quatAngleDeg(e.anat.back(), e.anatTruth.back());
    std::printf("  q_anat vs truth through swing + finish: Madgwick max %.2f deg (end %.2f), ESKF max %.2f deg (end %.2f); "
                "ESKF vs Madgwick max %.2f deg\n", errM, endM, errE, endE, dEM);
    std::printf("  calibration: M ESKF vs Madgwick %.3f deg\n", quatAngleDeg(e.al.M, m.al.M));
    check("arm: ESKF and Madgwick solve the same mount (< 0.1 deg)", quatAngleDeg(e.al.M, m.al.M) < 0.1);
    // The ESKF is held off the accelerometer through the swing (its gyro integration is
    // Madgwick's to 0.5° on this sequence), so it tracks the truth at least as well.
    check("arm: ESKF q_anat within 3 deg of the truth through the swing", errE < 3.0, "(%.2f)", errE);
    check("arm: ESKF no worse than Madgwick through the swing", errE <= errM + 0.1, "(ESKF %.2f deg)", errE);
    check("arm: ESKF back on the truth at the held finish (< 0.5 deg)", endE < 0.5, "(%.2f)", endE);
    check("arm: ESKF and Madgwick agree through the swing within 5 deg", dEM < 5.0, "(%.2f)", dEM);
}

// A NaN orientation in a stand-tall window is never a valid solve (the 26 Oct A that passed
// every accel/gyro gate). Built from a still, upright window whose quaternions are NaN.
static void standTallRefusesNonFinite()
{
    std::printf("\n-- stand tall refuses a non-finite orientation --\n");
    std::vector<int64_t> tUs;
    std::vector<pinpoint::ImuSample> rows;
    const QQuaternion R = wornMount(13.0f);
    const QVector3D a = R.conjugated().rotatedVector(QVector3D(0, 0, 1));
    for (int i = 0; i < 3 * kRateHz; ++i) {
        tUs.push_back(1'000'000 + int64_t(i) * kDtUs);
        rows.push_back(pinpoint::makeImuSample(a.x(), a.y(), a.z(), 0, 0, 0, NAN, NAN, NAN, NAN));
    }
    const auto s = trunk_stand_tall::solveLatest(tUs, rows, tUs.back(), kRateHz, imu_calibration::TrunkSensor::Pelvis);
    std::printf("  %s %s\n", s.alignment.valid ? "VALID" : "INVALID", qPrintable(s.alignment.record.failure));
    check("NaN quaternions: stand tall invalid", !s.alignment.valid);
    check("NaN quaternions: the failure says why", s.alignment.record.failure.contains(QStringLiteral("orientation")));

    for (auto &r : rows) { r.quat_w = R.scalar(); r.quat_x = R.x(); r.quat_y = R.y(); r.quat_z = R.z(); }
    const auto ok = trunk_stand_tall::solveLatest(tUs, rows, tUs.back(), kRateHz, imu_calibration::TrunkSensor::Pelvis);
    check("the same window with its quaternions: stand tall valid", ok.alignment.valid);
}

int main()
{
    std::printf("=== trunk turn check from the device's bytes (tap → lane → stand tall → turn) ===\n");
    const FilterRun m = runFilter(OrientationFilterType::Madgwick);
    const FilterRun e = runFilter(OrientationFilterType::Eskf);
    std::printf("\n-- ESKF against Madgwick (issue #104) --\n");
    check("hips: ESKF turn within 2 deg of Madgwick's", std::abs(e.rec.pelvis.turnDeg - m.rec.pelvis.turnDeg) <= 2.0f,
          "(%.2f)", double(e.rec.pelvis.turnDeg - m.rec.pelvis.turnDeg));
    check("chest: ESKF turn within 2 deg of Madgwick's", std::abs(e.rec.thorax.turnDeg - m.rec.thorax.turnDeg) <= 2.0f,
          "(%.2f)", double(e.rec.thorax.turnDeg - m.rec.thorax.turnDeg));
    check("hips: ESKF range within 2 deg of Madgwick's", std::abs(e.rec.pelvis.rangeDeg - m.rec.pelvis.rangeDeg) <= 2.0f,
          "(%.2f)", double(e.rec.pelvis.rangeDeg - m.rec.pelvis.rangeDeg));
    check("chest: ESKF range within 2 deg of Madgwick's", std::abs(e.rec.thorax.rangeDeg - m.rec.thorax.rangeDeg) <= 2.0f,
          "(%.2f)", double(e.rec.thorax.rangeDeg - m.rec.thorax.rangeDeg));
    armRegression();
    standTallRefusesNonFinite();
    std::printf("\n=== %s (%d assert failures) ===\n", g_fail ? "FAILURES PRESENT" : "ALL ASSERTS PASS", g_fail);
    return g_fail ? 1 : 0;
}
