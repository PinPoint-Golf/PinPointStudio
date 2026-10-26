// SPDX-License-Identifier: GPL-3.0-or-later
// ---------------------------------------------------------------------------
// trunk_stand_tall_test — the live trunk stand-tall solve (trunk_imu_sequence_grf_design.md
// §6.3 step 1, §16.2 W2.1a), fed synthetic still samples.
// ---------------------------------------------------------------------------
//
// Run via CTest (src/IMU/tests/CMakeLists.txt):
//   ctest --test-dir build/tests -R trunk_stand_tall --output-on-failure
//
// ImuInstance::calibrateTrunk() is a thin wrapper: it copies its sample ring out and calls
// trunk_stand_tall::solveLatest(), applying A and M only when the result is valid. The instance
// itself cannot be built here (it owns the BLE driver stack), so this drives the pure layer it
// calls with what its ring would hold — host-arrival stamps and raw sensor-frame ImuSamples —
// and checks the window, the verdict, the golfer-facing failure, the QVariantMap keys the trunk
// routine (W2.2) reads, and the binding provenance that is present only for a valid solve.
//
// The sensor is the WT901BLE67 worn as prescribed (design §4.1): USB end down, so sensor +Y
// (which points away from the USB end) up, label (+Z) out.
// At rest the accelerometer reads +1 g along up, so an upright sensor reads f = (0, 1, 0).

#include <QJsonObject>
#include <QQuaternion>
#include <QVector3D>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "trunk_stand_tall.h"

using imu_calibration::TrunkSensor;
using pinpoint::ImuSample;

static int g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}

namespace {

constexpr int     kRateHz = 200;
constexpr int64_t kT0Us   = 1'000'000'000;   // an arbitrary steady-clock origin

// The fused sensor→world quaternion of an upright sensor: sensor +Y onto world +Z (a Madgwick
// world, Z up), i.e. +90° about X. Any yaw would do — the solve takes heading from it.
QQuaternion uprightQuat() { return QQuaternion::fromAxisAndAngle(1.0f, 0.0f, 0.0f, 90.0f); }

struct Lane {
    std::vector<int64_t>   tUs;
    std::vector<ImuSample> rows;
    void add(int64_t t, const QVector3D &f, const QVector3D &gyro, const QQuaternion &q)
    {
        tUs.push_back(t);
        rows.push_back(pinpoint::makeImuSample(f.x(), f.y(), f.z(), gyro.x(), gyro.y(), gyro.z(),
                                               q.scalar(), q.x(), q.y(), q.z()));
    }
    int64_t last() const { return tUs.back(); }
};

// `seconds` of still samples ending at kT0Us + seconds, at a sample period of periodUs.
Lane still(double seconds, const QVector3D &f, double periodUs = 1e6 / kRateHz,
           const QVector3D &gyro = QVector3D(0.1f, -0.05f, 0.02f), const QQuaternion &q = uprightQuat())
{
    Lane L;
    const int n = int(std::floor(seconds * 1e6 / periodUs));
    for (int i = 1; i <= n; ++i)
        L.add(kT0Us + int64_t(std::llround(i * periodUs)), f, gyro, q);
    return L;
}

bool contains(const QString &s, const char *needle) { return s.contains(QLatin1String(needle)); }

void test_upright_still_is_valid()
{
    std::printf("\n-- upright, still, 2 s at 200 Hz --\n");
    const Lane L = still(3.0, QVector3D(0.0f, 1.0f, 0.0f));
    const trunk_stand_tall::LiveSolve s =
        trunk_stand_tall::solveLatest(L.tUs, L.rows, L.last(), kRateHz, TrunkSensor::Pelvis);
    const imu_calibration::TrunkSolveRecord &r = s.alignment.record;
    check(s.slice.w.samplesReceived == 400 && s.slice.w.samplesExpected == 400,
          "the window is the last 2 s: 400 of 400 samples (older ones excluded)");
    check(!s.slice.shorterThanSpan, "3 s of samples reach back the whole span");
    check(s.alignment.valid && r.failure.isEmpty(), "valid, no failure");
    check(r.samplesOk && r.stillOk && r.gravityOk && r.longAxisOk && r.faceOk, "every gate passes");
    check(r.longAxisFromUpDeg < 0.01f && r.faceFromHorizontalDeg < 0.01f, "long axis up, face horizontal");
    const QQuaternion anat = (s.alignment.A * s.slice.w.meanQuat * s.alignment.M).normalized();
    check(std::abs(anat.scalar()) > 0.99999f, "A·q̄·M is the identity at stand tall");

    const pinpoint::TrunkCalibProvenance p = trunk_stand_tall::provenanceFrom(s.alignment);
    check(p.present && p.deltaTier == r.deltaTier && p.sigmaDeltaDeg > 0.0,
          "a valid solve gives the binding its provenance (δ tier, σ_δ)");

    const QVariantMap m = trunk_stand_tall::toVariantMap(s);
    bool keys = true;
    for (const char *k : { "valid", "failure", "samplesOk", "stillOk", "gravityOk", "longAxisOk", "faceOk",
                           "sampleFraction", "maxAbsGyroDps", "gravityErrPct", "longAxisFromUpDeg",
                           "faceFromHorizontalDeg", "deltaTier", "sigmaDeltaDeg", "samplesReceived",
                           "samplesExpected", "windowS", "shorterThanSpan", "alignA", "mountM" })
        if (!m.contains(QString::fromLatin1(k))) {
            std::printf("    missing key %s\n", k);
            keys = false;
        }
    check(keys, "the QVariantMap carries every documented key");
    check(m.value(QStringLiteral("valid")).toBool() && m.value(QStringLiteral("alignA")).toList().size() == 4,
          "valid = true, alignA as [w, x, y, z]");
}

void test_the_slow_device_clock_and_bursts_pass()
{
    std::printf("\n-- 198.6 Hz (W0: the device clock runs 0.7 %% slow) --\n");
    const double periodUs = 1e6 / 198.6;
    {
        const Lane L = still(3.0, QVector3D(0.0f, 1.0f, 0.0f), periodUs);
        const trunk_stand_tall::LiveSolve s =
            trunk_stand_tall::solveLatest(L.tUs, L.rows, L.last(), kRateHz, TrunkSensor::Thorax);
        std::printf("    received %d of %d (%.4f)\n", s.slice.w.samplesReceived, s.slice.w.samplesExpected,
                    double(s.alignment.record.sampleFraction));
        check(s.slice.w.samplesExpected == 400, "expected is span × the CONFIGURED rate (400)");
        check(s.alignment.record.sampleFraction > 0.95f && s.alignment.record.sampleFraction < 1.0f,
              "short of nominal, but inside the 0.95 gate");
        check(s.alignment.valid, "and valid");
    }
    {   // and arriving in bursts of 8 sharing one host stamp (§17.4), the window cut mid-stream
        Lane L;
        for (int i = 1; i <= 800; ++i) {
            const int burstEnd = ((i + 7) / 8) * 8;   // the stamp of the burst's last sample
            L.add(kT0Us + int64_t(std::llround(burstEnd * periodUs)), QVector3D(0.0f, 1.0f, 0.0f),
                  QVector3D(0.1f, 0.0f, 0.0f), uprightQuat());
        }
        bool allValid = true;
        float worst = 1.0f;
        for (int64_t lag = 0; lag < 45'000; lag += 3'000) {   // every phase of the burst cycle
            const trunk_stand_tall::LiveSolve s =
                trunk_stand_tall::solveLatest(L.tUs, L.rows, L.last() + lag, kRateHz, TrunkSensor::Thorax);
            allValid = allValid && s.alignment.valid;
            worst = std::min(worst, s.alignment.record.sampleFraction);
        }
        std::printf("    worst fraction over the burst phases %.3f\n", double(worst));
        check(allValid, "valid at every phase of the burst cycle");
    }
}

void test_tilted_beyond_the_long_axis_gate()
{
    std::printf("\n-- tilted 40° in the face plane (long axis gate 35°) --\n");
    const float a = float(40.0 * M_PI / 180.0);
    const Lane L = still(2.5, QVector3D(std::sin(a), std::cos(a), 0.0f));
    const trunk_stand_tall::LiveSolve s =
        trunk_stand_tall::solveLatest(L.tUs, L.rows, L.last(), kRateHz, TrunkSensor::Pelvis);
    const imu_calibration::TrunkSolveRecord &r = s.alignment.record;
    check(!s.alignment.valid, "invalid");
    check(!r.longAxisOk && r.faceOk, "the long axis fails, the face passes");
    check(std::abs(r.longAxisFromUpDeg - 40.0f) < 0.1f, "long axis reads 40° from up");
    check(contains(r.failure, "Hip sensor") && contains(r.failure, "USB-down"),
          "the failure names the hip sensor and says USB-down");
    check(!trunk_stand_tall::provenanceFrom(s.alignment).present, "no provenance: nothing to apply");
    check(!trunk_stand_tall::toVariantMap(s).value(QStringLiteral("valid")).toBool(), "map says valid = false");
}

void test_upside_down()
{
    std::printf("\n-- upside down (USB end up) --\n");
    const Lane L = still(2.5, QVector3D(0.0f, -1.0f, 0.0f), 1e6 / kRateHz, QVector3D(0.1f, 0.0f, 0.0f),
                         QQuaternion::fromAxisAndAngle(1.0f, 0.0f, 0.0f, -90.0f));
    const trunk_stand_tall::LiveSolve s =
        trunk_stand_tall::solveLatest(L.tUs, L.rows, L.last(), kRateHz, TrunkSensor::Thorax);
    const imu_calibration::TrunkSolveRecord &r = s.alignment.record;
    check(!s.alignment.valid, "invalid");
    check(r.longAxisFromUpDeg > 179.0f, "long axis 180° from up");
    check(contains(r.failure, "Chest sensor") && contains(r.failure, "USB-down"),
          "the failure names the chest sensor and says USB-down");
    check(!trunk_stand_tall::provenanceFrom(s.alignment).present, "no provenance");
}

void test_too_few_samples()
{
    std::printf("\n-- too few samples --\n");
    {   // only 1.5 s since it connected
        const Lane L = still(1.5, QVector3D(0.0f, 1.0f, 0.0f));
        const trunk_stand_tall::LiveSolve s =
            trunk_stand_tall::solveLatest(L.tUs, L.rows, L.last(), kRateHz, TrunkSensor::Pelvis);
        check(s.slice.shorterThanSpan, "1.5 s of samples: shorter than the span");
        check(!s.alignment.valid && !s.alignment.record.samplesOk, "invalid on the sample gate");
        check(contains(s.alignment.record.failure, "dropped samples"), "the failure says dropped samples");
    }
    {   // a dropout: every fourth sample lost inside the hold
        Lane full = still(2.0, QVector3D(0.0f, 1.0f, 0.0f));
        Lane L;
        for (size_t i = 0; i < full.tUs.size(); ++i)
            if (i % 4 != 3) { L.tUs.push_back(full.tUs[i]); L.rows.push_back(full.rows[i]); }
        const trunk_stand_tall::LiveSolve s =
            trunk_stand_tall::solveLatest(L.tUs, L.rows, full.last(), kRateHz, TrunkSensor::Pelvis);
        check(std::abs(s.alignment.record.sampleFraction - 0.75f) < 0.01f, "a 25 % loss reads 0.75");
        check(!s.alignment.valid && contains(s.alignment.record.failure, "dropped samples"),
              "invalid: dropped samples");
    }
    {   // stale: the sensor stopped streaming 10 s ago — nothing in the window
        const Lane L = still(3.0, QVector3D(0.0f, 1.0f, 0.0f));
        const trunk_stand_tall::LiveSolve s =
            trunk_stand_tall::solveLatest(L.tUs, L.rows, L.last() + 10'000'000, kRateHz, TrunkSensor::Pelvis);
        check(s.slice.w.samplesReceived == 0 && !s.alignment.valid,
              "a sensor silent for 10 s is not calibrated from its old samples");
    }
    {   // never streamed at all
        const trunk_stand_tall::LiveSolve s =
            trunk_stand_tall::solveLatest({}, {}, kT0Us, kRateHz, TrunkSensor::Thorax);
        check(!s.alignment.valid && contains(s.alignment.record.failure, "Chest sensor dropped samples"),
              "no samples: invalid, chest sensor dropped samples");
    }
}

void test_moving()
{
    std::printf("\n-- moving --\n");
    {   // swaying through the whole hold
        const Lane L = still(2.5, QVector3D(0.0f, 1.0f, 0.0f), 1e6 / kRateHz, QVector3D(0.0f, 5.0f, 0.0f));
        const trunk_stand_tall::LiveSolve s =
            trunk_stand_tall::solveLatest(L.tUs, L.rows, L.last(), kRateHz, TrunkSensor::Thorax);
        check(!s.alignment.valid && !s.alignment.record.stillOk, "invalid on the still gate");
        check(contains(s.alignment.record.failure, "moved"), "the failure says moved");
    }
    {   // moving BEFORE the last 2 s, still within them: only the last 2 s count
        Lane L;
        for (int i = 1; i <= 600; ++i)
            L.add(kT0Us + i * 5000, QVector3D(0.0f, 1.0f, 0.0f),
                  i <= 200 ? QVector3D(0.0f, 40.0f, 0.0f) : QVector3D(0.1f, 0.0f, 0.0f), uprightQuat());
        const trunk_stand_tall::LiveSolve s =
            trunk_stand_tall::solveLatest(L.tUs, L.rows, L.last(), kRateHz, TrunkSensor::Pelvis);
        check(s.alignment.valid, "movement before the hold is outside the window: valid");
    }
    {   // still, then moving at the end: the end is in the window
        Lane L;
        for (int i = 1; i <= 600; ++i)
            L.add(kT0Us + i * 5000, QVector3D(0.0f, 1.0f, 0.0f),
                  i > 500 ? QVector3D(0.0f, 40.0f, 0.0f) : QVector3D(0.1f, 0.0f, 0.0f), uprightQuat());
        const trunk_stand_tall::LiveSolve s =
            trunk_stand_tall::solveLatest(L.tUs, L.rows, L.last(), kRateHz, TrunkSensor::Pelvis);
        check(!s.alignment.valid && contains(s.alignment.record.failure, "moved"),
              "movement inside the last 2 s: invalid, moved");
    }
}

void test_the_mean_quaternion_is_sign_blind()
{
    std::printf("\n-- q and −q are one rotation --\n");
    Lane L = still(2.0, QVector3D(0.0f, 1.0f, 0.0f));
    for (size_t i = 1; i < L.rows.size(); i += 2) {
        ImuSample &s = L.rows[i];
        s.quat_w = -s.quat_w; s.quat_x = -s.quat_x; s.quat_y = -s.quat_y; s.quat_z = -s.quat_z;
    }
    const imu_calibration::StandTallWindow w =
        trunk_stand_tall::summarise(L.rows, 0, L.rows.size(), trunk_stand_tall::kSpanS, kRateHz);
    check(std::abs(QQuaternion::dotProduct(w.meanQuat, uprightQuat())) > 0.99999f,
          "half the samples negated: the mean is still the rotation");
}

void test_the_binding_provenance_round_trips()
{
    std::printf("\n-- the binding's swing.json keys --\n");
    const Lane L = still(2.5, QVector3D(0.0f, 1.0f, 0.0f));
    const trunk_stand_tall::LiveSolve s =
        trunk_stand_tall::solveLatest(L.tUs, L.rows, L.last(), kRateHz, TrunkSensor::Pelvis);
    const pinpoint::TrunkCalibProvenance p = trunk_stand_tall::provenanceFrom(s.alignment);
    QJsonObject o;
    pinpoint::writeTrunkCalibProvenance(o, p);
    check(o.value(QStringLiteral("calibrationSource")).toString() == QStringLiteral("trunkStandTall"),
          "calibrationSource = trunkStandTall");
    const pinpoint::TrunkCalibProvenance back = pinpoint::readTrunkCalibProvenance(o);
    check(back.present && back.deltaTier == p.deltaTier && back.sigmaDeltaDeg == p.sigmaDeltaDeg
              && std::abs(back.maxAbsGyroDps - p.maxAbsGyroDps) < 1e-9
              && std::abs(back.sampleFraction - p.sampleFraction) < 1e-9,
          "read back: tier, σ_δ and residuals");

    QJsonObject arm;
    pinpoint::writeTrunkCalibProvenance(arm, pinpoint::TrunkCalibProvenance());
    check(arm.isEmpty(), "an arm (not-present) binding gains no keys");
    check(!pinpoint::readTrunkCalibProvenance(QJsonObject{ { QStringLiteral("calibrated"), true } }).present,
          "a binding without the keys (every swing before W2) reads not present");
}

} // namespace

int main()
{
    std::printf("=== trunk stand-tall: the live solve ===\n");

    test_upright_still_is_valid();
    test_the_slow_device_clock_and_bursts_pass();
    test_tilted_beyond_the_long_axis_gate();
    test_upside_down();
    test_too_few_samples();
    test_moving();
    test_the_mean_quaternion_is_sign_blind();
    test_the_binding_provenance_round_trips();

    std::printf(g_fail ? "\nFAILED (%d)\n" : "\nOK\n", g_fail);
    return g_fail ? 1 : 0;
}
