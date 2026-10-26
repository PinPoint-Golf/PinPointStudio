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

// The trunk ceremony file round trip (trunk_imu_sequence_grf_design.md §6.7, §16.2 W2.1c): a
// synthetic ceremony is solved and checked "live" — on the samples that had arrived when the live
// call ran, as ImuInstance::calibrateTrunk and TrunkCeremonyRecorder::checkTurn see them — then
// written as pinpoint.trunk_ceremony/1, read back, and replayed. The replay must give the live
// A and M and the live turn records exactly: that is what lets swinglab --trunk-calib-from and the
// unit tests stand in for the golfer.

#include <cmath>
#include <cstdio>
#include <tuple>
#include <vector>

#include "trunk_ceremony_file.h"

namespace tcf = trunk_ceremony_file;
using imu_calibration::TrunkSensor;
using imu_calibration::TrunkTurnDirection;
using imu_calibration::TrunkTurnRecord;

static int g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}

namespace {

constexpr int     kRateHz  = 200;
constexpr double  kPeriodS = 1.0 / 198.6;           // the W0 device clock, 0.7 % slow (§17.4)
constexpr int64_t kT0Us    = 5'000'000'000;         // an arbitrary steady-clock origin
constexpr double  kPi      = 3.14159265358979323846;

const QVector3D X(1, 0, 0), Y(0, 1, 0), Z(0, 0, 1);

// Bursts of 8 arrivals, 0.3 ms apart, each burst landing when its last sample was taken (§17.4).
int64_t arrivalUs(int i, int64_t offsetUs)
{
    const int burst = i / 8;
    return kT0Us + offsetUs + int64_t(std::llround((burst + 1) * 8 * kPeriodS * 1e6)) + (i % 8) * 300;
}

// The golfer's truth over the ceremony: still 0–3.5 s (stand tall), turn away 3.5–11.5 s, still,
// turn through 12–20 s, still to 21 s. A turn is peak·sin²(2πu) about vertical — away and back twice.
QQuaternion truthAt(double tS, float peakDeg, bool leftHanded)
{
    const auto turn = [&](double t0, double t1, int sign) {
        const double u = (tS - t0) / (t1 - t0), s = std::sin(2.0 * kPi * u);
        return QQuaternion::fromAxisAndAngle(Z, float(sign * (leftHanded ? -1 : 1) * peakDeg * s * s));
    };
    if (tS >= 3.5 && tS < 11.5) return turn(3.5, 11.5, -1);
    if (tS >= 12.0 && tS < 20.0) return turn(12.0, 20.0, +1);
    return QQuaternion();
}

// One Witmotion lane: the fused q_raw = W·q_true·M⁻¹ for the filter's world W (imu_calibration_test
// §H), the accelerometer's gravity reaction M·ẑ, and a small gyro bias.
tcf::Lane witmotionLane(const QString &role, const QString &serial, const QQuaternion &mount,
                        float peakDeg, int64_t offsetUs)
{
    const QQuaternion W = QQuaternion::fromAxisAndAngle(Z, 37);
    tcf::Lane L;
    L.role = role;
    L.serial = serial;
    L.deviceId = QStringLiteral("dev-") + serial;
    L.instrument = tcf::instrumentWitmotion();
    L.configuredRateHz = kRateHz;
    L.measuredRateHz = 198.6;
    const QVector3D f = mount.rotatedVector(Z) * 1.004f;
    const int n = int(21.0 / kPeriodS);
    for (int i = 0; i < n; ++i) {
        const double tS = (i + 1) * kPeriodS;
        const QQuaternion q = (W * truthAt(tS, peakDeg, false) * mount.conjugated()).normalized();
        L.tUs.push_back(arrivalUs(i, offsetUs));
        L.rows.push_back(pinpoint::makeImuSample(f.x(), f.y(), f.z(), 0.11f, -0.05f, 0.02f,
                                                 q.scalar(), q.x(), q.y(), q.z()));
    }
    return L;
}

tcf::Lane hackmotionLane()
{
    tcf::Lane L;
    L.role = QStringLiteral("leadForearm");
    L.serial = QStringLiteral("AA:BB#lowerArm");
    L.deviceId = QStringLiteral("AA:BB");
    L.instrument = tcf::instrumentHackMotion();
    L.measuredRateHz = 50.0;
    for (int i = 0; i < 1050; ++i) {
        const QQuaternion q = QQuaternion::fromAxisAndAngle(Y, float(i) * 0.37f);
        L.tUs.push_back(kT0Us + 20'000 * (i + 1));
        L.rows.push_back(pinpoint::makeImuSample(0.001f * i, -0.3f, 1e-7f, 123.456f, -0.0f, 3.0e-5f,
                                                 q.scalar(), q.x(), q.y(), q.z()));
    }
    return L;
}

// What a live call saw: the lane as it stood at tNowUs (every sample that had arrived).
tcf::Ceremony asOf(const tcf::Ceremony &c, int64_t tNowUs)
{
    tcf::Ceremony out = c;
    for (tcf::Lane &L : out.lanes) {
        size_t lo = 0, hi = 0;
        tcf::spanOf(L, INT64_MIN, tNowUs, &lo, &hi);
        L.tUs.resize(hi);
        L.rows.resize(hi);
    }
    return out;
}

// ImuInstance::calibrateTrunk's map, as the live call builds it.
QVariantMap liveStandTall(const tcf::Ceremony &c, const QString &role, int64_t tEndUs,
                          trunk_stand_tall::LiveSolve *solveOut)
{
    const tcf::Ceremony seen = asOf(c, tEndUs);
    const tcf::Lane &L = *tcf::laneByRole(seen, role);
    const trunk_stand_tall::LiveSolve s = trunk_stand_tall::solveLatest(
        L.tUs, L.rows, tEndUs, L.configuredRateHz,
        role == QLatin1String("pelvis") ? TrunkSensor::Pelvis : TrunkSensor::Thorax);
    QVariantMap m = trunk_stand_tall::toVariantMap(s);
    m.insert(QStringLiteral("role"), role);
    m.insert(QStringLiteral("serial"), L.serial);
    m.insert(QStringLiteral("applied"), s.alignment.valid);
    m.insert(QStringLiteral("windowEndUs"), qint64(tEndUs));
    if (s.alignment.valid) m.insert(QStringLiteral("calibratedAt"), QStringLiteral("2026-10-26T10:00:00.000Z"));
    if (solveOut) *solveOut = s;
    return m;
}

// TrunkCeremonyRecorder::checkTurn, minus the device: the step's start to now, clamped to each
// lane's last sample, through the given A/M.
QVariantMap liveTurn(const tcf::Ceremony &c, const tcf::Step &step, int64_t tNowUs, TrunkTurnDirection d,
                     const QQuaternion &aP, const QQuaternion &mP, const QQuaternion &aT, const QQuaternion &mT,
                     TrunkTurnRecord *recOut)
{
    const tcf::Ceremony seen = asOf(c, tNowUs);
    tcf::TurnInputs in;
    in.direction = d;
    in.leftHanded = false;
    const auto fill = [&](tcf::TurnSensorInput &si, const QString &role, const QQuaternion &A, const QQuaternion &M) {
        const tcf::Lane &L = *tcf::laneByRole(seen, role);
        si.serial = L.serial;
        si.windowStartUs = step.startUs;
        si.windowEndUs = std::min(tNowUs, L.tUs.back());
        si.A = A;
        si.M = M;
    };
    fill(in.pelvis, QStringLiteral("pelvis"), aP, mP);
    fill(in.thorax, QStringLiteral("thorax"), aT, mT);
    *recOut = tcf::runTurn(seen, in);
    return tcf::turnRecordToMap(*recOut, in);
}

bool quatSame(const QQuaternion &a, const QQuaternion &b)
{
    return a.scalar() == b.scalar() && a.x() == b.x() && a.y() == b.y() && a.z() == b.z();
}

bool sensorSame(const imu_calibration::TrunkTurnSensorRecord &a, const imu_calibration::TrunkTurnSensorRecord &b)
{
    return a.samples == b.samples && a.rangeDeg == b.rangeDeg && a.turnDeg == b.turnDeg
           && a.siTurnDeg == b.siTurnDeg && a.axisFromVerticalDeg == b.axisFromVerticalDeg
           && a.rangeOk == b.rangeOk && a.senseOk == b.senseOk && a.axisOk == b.axisOk && a.failure == b.failure;
}

bool turnSame(const TrunkTurnRecord &a, const TrunkTurnRecord &b)
{
    return a.valid == b.valid && a.failure == b.failure && a.expectedSign == b.expectedSign
           && a.chestOverHips == b.chestOverHips && a.orderOk == b.orderOk && a.wrongWay == b.wrongWay
           && a.cause == b.cause
           && sensorSame(a.pelvis, b.pelvis) && sensorSame(a.thorax, b.thorax);
}

int64_t at(double s) { return kT0Us + int64_t(std::llround(s * 1e6)); }

} // namespace

int main()
{
    std::printf("=== trunk ceremony file: write, read, replay ===\n");

    const QQuaternion mNom   = QQuaternion::fromAxisAndAngle(X, -90);   // USB down, label out
    const QQuaternion mHip   = (mNom * QQuaternion::fromAxisAndAngle(X, 12)).normalized();
    const QQuaternion mChest = (mNom * QQuaternion::fromAxisAndAngle(X, -20) * QQuaternion::fromAxisAndAngle(Y, 8)).normalized();

    tcf::Ceremony c;
    c.createdUtc = QStringLiteral("2026-10-26T09:59:00.000Z");
    c.appVersion = QStringLiteral("test");
    c.handedness = QStringLiteral("right");
    c.beganUs = kT0Us;
    c.lanes.push_back(witmotionLane(QStringLiteral("pelvis"), QStringLiteral("WT-HIP"), mHip, 45.0f, 0));
    c.lanes.push_back(witmotionLane(QStringLiteral("thorax"), QStringLiteral("WT-CHEST"), mChest, 90.0f, 1'100));
    c.lanes.push_back(hackmotionLane());

    // ── The live ceremony ──
    c.steps.push_back({ tcf::stepArmRoutine(), at(0.0), at(0.2), {} });
    // Stand tall: the hold bar fills at 3.2 s, both sensors solved then.
    tcf::Step st{ tcf::stepStandTall(), at(0.5), at(3.3), {} };
    trunk_stand_tall::LiveSolve sp, sc;
    st.result.insert(QStringLiteral("pelvis"), liveStandTall(c, QStringLiteral("pelvis"), at(3.2), &sp));
    st.result.insert(QStringLiteral("thorax"), liveStandTall(c, QStringLiteral("thorax"), at(3.2) + 700, &sc));
    c.steps.push_back(st);
    check(sp.alignment.valid && sc.alignment.valid, "live stand tall: both sensors solve");

    // The turns, checked at the end of each step through the stand-tall A/M.
    TrunkTurnRecord liveAway, liveThrough;
    tcf::Step away{ tcf::stepTurnAway(), at(3.5), -1, {} };
    away.result = liveTurn(c, away, at(11.5), TrunkTurnDirection::Away, sp.alignment.A, sp.alignment.M,
                           sc.alignment.A, sc.alignment.M, &liveAway);
    away.endUs = at(11.52);
    c.steps.push_back(away);
    tcf::Step through{ tcf::stepTurnThrough(), at(12.0), -1, {} };
    through.result = liveTurn(c, through, at(20.0), TrunkTurnDirection::Through, sp.alignment.A, sp.alignment.M,
                              sc.alignment.A, sc.alignment.M, &liveThrough);
    through.endUs = at(20.01);
    c.steps.push_back(through);
    check(liveAway.valid && liveThrough.valid, "live turns: away and through pass");
    std::printf("         away: chest %.2f°, hips %.2f° · through: chest %.2f°, hips %.2f°\n",
                double(liveAway.thorax.rangeDeg), double(liveAway.pelvis.rangeDeg),
                double(liveThrough.thorax.rangeDeg), double(liveThrough.pelvis.rangeDeg));

    // A stand-tall repeat that FAILS (its window runs 1.5 s past the last sample: too few samples).
    // Live, calibrateTrunk would apply nothing — the first solve stays in force — so must the replay.
    tcf::Step st2{ tcf::stepStandTall(), at(20.2), at(22.6), {} };
    trunk_stand_tall::LiveSolve sp2;
    st2.result.insert(QStringLiteral("pelvis"), liveStandTall(c, QStringLiteral("pelvis"), at(22.5), &sp2));
    c.steps.push_back(st2);
    check(!sp2.alignment.valid, "the repeated stand tall fails (too few samples)");
    c.steps.push_back({ QStringLiteral("unended"), at(22.7), -1, {} });
    c.endedUs = at(23.0);
    c.endedUtc = QStringLiteral("2026-10-26T10:00:23.000Z");

    // ── Write, read ──
    std::printf("\n-- round trip --\n");
    const QByteArray bytes = tcf::toJson(c);
    std::printf("         %lld bytes for %zu + %zu + %zu samples\n", (long long)bytes.size(),
                c.lanes[0].tUs.size(), c.lanes[1].tUs.size(), c.lanes[2].tUs.size());
    tcf::Ceremony r;
    QString err;
    check(tcf::fromJson(bytes, &r, &err), "parses");
    if (!err.isEmpty()) std::printf("         error: %s\n", qPrintable(err));
    check(r.lanes.size() == 3 && r.steps.size() == c.steps.size(), "lane and step counts");
    check(r.handedness == c.handedness && r.appVersion == c.appVersion && r.beganUs == c.beganUs
              && r.endedUs == c.endedUs && r.createdUtc == c.createdUtc, "header");
    // Equal as IEEE values, field by field: a −0 reads back +0 (trunk_ceremony_file.h), so the
    // wG3 lane's −0 gyro is the one field that is not bit-for-bit.
    const auto rowsEqual = [](const tcf::Lane &a, const tcf::Lane &b, size_t *where) {
        for (size_t k = 0; k < a.rows.size(); ++k) {
            const pinpoint::ImuSample &x = a.rows[k], &y = b.rows[k];
            if (!(x.accel_x == y.accel_x && x.accel_y == y.accel_y && x.accel_z == y.accel_z
                  && x.gyro_x == y.gyro_x && x.gyro_y == y.gyro_y && x.gyro_z == y.gyro_z
                  && x.quat_w == y.quat_w && x.quat_x == y.quat_x && x.quat_y == y.quat_y && x.quat_z == y.quat_z)) {
                *where = k;
                return false;
            }
        }
        return true;
    };
    bool lanesSame = r.lanes.size() == c.lanes.size();
    for (size_t i = 0; lanesSame && i < c.lanes.size(); ++i) {
        const tcf::Lane &a = c.lanes[i], &b = r.lanes[i];
        size_t where = 0;
        lanesSame = a.role == b.role && a.serial == b.serial && a.deviceId == b.deviceId
                    && a.instrument == b.instrument && a.configuredRateHz == b.configuredRateHz
                    && a.measuredRateHz == b.measuredRateHz && a.tUs == b.tUs && a.rows.size() == b.rows.size()
                    && rowsEqual(a, b, &where);
        if (!lanesSame)
            std::printf("         lane %zu (%s) differs (sample %zu)\n", i, qPrintable(a.serial), where);
    }
    check(lanesSame, "every lane exact (stamps, accel, gyro, quat), the wG3 lane included");
    bool stepsSame = true;
    for (size_t i = 0; i < c.steps.size(); ++i)
        stepsSame = stepsSame && r.steps[i].name == c.steps[i].name && r.steps[i].startUs == c.steps[i].startUs
                    && r.steps[i].endUs == c.steps[i].endUs;
    check(stepsSame, "step names and boundaries (an unended step stays −1)");

    // ── The stand-tall replay = the live solve ──
    std::printf("\n-- stand tall replayed --\n");
    for (const auto &[role, sensor, live] : { std::tuple{ QStringLiteral("pelvis"), TrunkSensor::Pelvis, &sp },
                                              std::tuple{ QStringLiteral("thorax"), TrunkSensor::Thorax, &sc } }) {
        const tcf::StandTallReplay rp = tcf::replayStandTall(r, *tcf::laneByRole(r, role), sensor);
        char lbl[96];
        std::snprintf(lbl, sizeof lbl, "%s: replays the first (valid) stand tall, not the failed repeat", qPrintable(role));
        check(rp.found && rp.stepIndex == 1, lbl);
        std::snprintf(lbl, sizeof lbl, "%s: valid, same samples (%d)", qPrintable(role), live->slice.w.samplesReceived);
        check(rp.solve.alignment.valid && rp.solve.slice.w.samplesReceived == live->slice.w.samplesReceived, lbl);
        std::snprintf(lbl, sizeof lbl, "%s: A identical to the live A", qPrintable(role));
        check(quatSame(rp.solve.alignment.A, live->alignment.A), lbl);
        std::snprintf(lbl, sizeof lbl, "%s: M identical to the live M", qPrintable(role));
        check(quatSame(rp.solve.alignment.M, live->alignment.M), lbl);
        std::snprintf(lbl, sizeof lbl, "%s: and the live record's alignA/mountM read back", qPrintable(role));
        const QVariantMap rec = r.steps[1].result.value(role).toMap();
        check(qFuzzyCompare(tcf::quatFromList(rec.value(QStringLiteral("alignA"))), live->alignment.A)
                  && qFuzzyCompare(tcf::quatFromList(rec.value(QStringLiteral("mountM"))), live->alignment.M), lbl);
    }
    {   // With only the failed repeat in the file, the replay is that repeat — and says it failed.
        tcf::Ceremony onlyBad = r;
        onlyBad.steps.erase(onlyBad.steps.begin() + 1);
        const tcf::StandTallReplay rp = tcf::replayStandTall(onlyBad, *tcf::laneByRole(onlyBad, QStringLiteral("pelvis")),
                                                             TrunkSensor::Pelvis);
        check(rp.found && !rp.solve.alignment.valid
                  && rp.solve.alignment.record.failure == sp2.alignment.record.failure,
              "no valid stand tall: the last one is replayed, failure and all");
        const tcf::StandTallReplay none = tcf::replayStandTall(onlyBad, *tcf::laneByRole(onlyBad, QStringLiteral("thorax")),
                                                               TrunkSensor::Thorax);
        check(none.found && none.windowEndUs == onlyBad.steps[3].endUs,
              "a step without the role's record replays to the step's end");
    }

    // ── The turn replay = the live check ──
    std::printf("\n-- turns replayed --\n");
    for (const auto &[idx, live, name] : { std::tuple{ size_t(2), &liveAway, "away" },
                                          std::tuple{ size_t(3), &liveThrough, "through" } }) {
        TrunkTurnRecord rt;
        char lbl[96];
        std::snprintf(lbl, sizeof lbl, "%s: replays through the recorded A/M and windows", name);
        check(tcf::replayTurn(r, r.steps[idx], &rt), lbl);
        std::snprintf(lbl, sizeof lbl, "%s: the record is the live record, field for field", name);
        check(turnSame(rt, *live), lbl);
        const tcf::StandTallReplay rp = tcf::replayStandTall(r, *tcf::laneByRole(r, QStringLiteral("pelvis")), TrunkSensor::Pelvis);
        const tcf::StandTallReplay rc = tcf::replayStandTall(r, *tcf::laneByRole(r, QStringLiteral("thorax")), TrunkSensor::Thorax);
        TrunkTurnRecord rt2;
        tcf::replayTurn(r, r.steps[idx], &rt2, &rp.solve.alignment.A, &rp.solve.alignment.M,
                        &rc.solve.alignment.A, &rc.solve.alignment.M);
        std::snprintf(lbl, sizeof lbl, "%s: and through the replayed stand-tall A/M too", name);
        check(turnSame(rt2, *live), lbl);
    }
    {   // The map W2.2 reads: the keys are there, and the file carries them back.
        const QVariantMap m = r.steps[2].result;
        bool keys = true;
        for (const char *k : { "valid", "failure", "direction", "leftHanded", "expectedSign", "chestOverHips",
                               "orderOk", "wrongWay", "cause", "pelvis", "thorax" })
            keys = keys && m.contains(QLatin1String(k));
        for (const char *k : { "samples", "rangeDeg", "turnDeg", "siTurnDeg", "axisFromVerticalDeg", "rangeOk",
                               "senseOk", "axisOk", "failure", "serial", "windowStartUs", "windowEndUs",
                               "alignA", "mountM" })
            keys = keys && m.value(QStringLiteral("thorax")).toMap().contains(QLatin1String(k));
        check(keys, "turn result: every documented key survives the file");
        check(m.value(QStringLiteral("direction")).toString() == QLatin1String("away")
                  && m.value(QStringLiteral("expectedSign")).toInt() == -1
                  && m.value(QStringLiteral("valid")).toBool(), "turn result: away, sign −1, valid");
    }

    // ── What the reader refuses ──
    std::printf("\n-- refused --\n");
    {
        tcf::Ceremony x;
        QByteArray wrong = bytes;
        wrong.replace("pinpoint.trunk_ceremony/1", "pinpoint.trunk_ceremony/9");
        check(!tcf::fromJson(wrong, &x, &err) && err.contains(QLatin1String("format")), "another format version");
        check(!tcf::fromJson(QByteArray("{\"format\":"), &x, &err), "truncated JSON");
        tcf::Ceremony bad = c;
        bad.lanes.resize(1);
        QByteArray b = tcf::toJson(bad);
        b.replace(",\"gyro\":[[", ",\"gyro\":[[0,0,0],[");   // one gyro triple too many
        check(!tcf::fromJson(b, &x, &err) && err.contains(QLatin1String("lengths")), "a lane whose arrays disagree");
    }

    std::printf(g_fail ? "\nFAILED (%d)\n" : "\nOK\n", g_fail);
    return g_fail ? 1 : 0;
}
