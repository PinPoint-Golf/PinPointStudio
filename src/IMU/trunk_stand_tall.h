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

#pragma once

// The stand-tall window, summarised ONE way for the live ceremony and the offline replay
// (trunk_imu_sequence_grf_design.md §6.3 step 1, §16.2 W2.1a; imu_frame_contract.md §5 "Trunk
// segments"). ImuInstance::calibrateTrunk() takes the LAST 2 s of its own samples; swinglab
// --trunk-calib-from takes the STILLEST 2 s of a recorded lane. Where the window falls differs
// by design (the live hold bar has just filled; a recording has to be searched), but the
// summary of the chosen samples is summarise() in both, so the two cannot drift.
//
// Pure: no QObject, no device, no clock — the caller passes "now". Tested by
// src/IMU/tests/trunk_stand_tall_test.cpp.

#include <QQuaternion>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QVector3D>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "../Buffer/imu_sample.h"
#include "imu_calibration.h"
#include "trunk_calib_provenance.h"

namespace trunk_stand_tall {

// The hold the golfer is asked for (§6.3 step 1, "2 s; the hold bar fills").
inline constexpr double kSpanS = 2.0;

// Mean accel, mean gyro and the mean quaternion of rows [lo, hi) — each quaternion sign-aligned
// to the running sum (q and −q are one rotation) and the sum normalised.
//
// samplesExpected is span × the NOMINAL (configured) rate, never the fitted one, so a window
// that lost samples fails the solve's sample gate rather than passing on fewer. ⚠ The device
// clock runs about 0.7 % slow (W0, §17.4: 198.6 Hz at a configured 200), so a healthy 2 s window
// holds ~397 of the 400 expected — 0.993, well inside kTrunkMinSampleFraction (0.95). A window
// cut by host arrival time can also lose up to one BLE burst (8 samples) at an edge: ~0.97,
// still inside. Nothing here corrects for either; the gate's margin is what tolerates them.
inline imu_calibration::StandTallWindow summarise(const std::vector<pinpoint::ImuSample> &rows,
                                                  size_t lo, size_t hi, double spanS, int nominalRateHz)
{
    imu_calibration::StandTallWindow w;
    w.samplesExpected = int(std::lround(spanS * double(nominalRateHz > 0 ? nominalRateHz : 0)));
    if (hi > rows.size()) hi = rows.size();
    if (lo >= hi) {
        w.meanQuat = QQuaternion();
        return w;   // nothing received: the sample gate fails, and the solve says so
    }
    double f[3] = { 0, 0, 0 }, g[3] = { 0, 0, 0 }, q[4] = { 0, 0, 0, 0 };
    int cnt = 0;
    for (size_t k = lo; k < hi; ++k, ++cnt) {
        const pinpoint::ImuSample &s = rows[k];
        f[0] += s.accel_x; f[1] += s.accel_y; f[2] += s.accel_z;
        g[0] += s.gyro_x;  g[1] += s.gyro_y;  g[2] += s.gyro_z;
        const double dot = s.quat_w * q[0] + s.quat_x * q[1] + s.quat_y * q[2] + s.quat_z * q[3];
        const double sgn = (cnt == 0 || dot >= 0.0) ? 1.0 : -1.0;
        q[0] += sgn * s.quat_w; q[1] += sgn * s.quat_x; q[2] += sgn * s.quat_y; q[3] += sgn * s.quat_z;
    }
    w.meanAccelG  = QVector3D(float(f[0] / cnt), float(f[1] / cnt), float(f[2] / cnt));
    w.meanGyroDps = QVector3D(float(g[0] / cnt), float(g[1] / cnt), float(g[2] / cnt));
    w.meanQuat    = QQuaternion(float(q[0]), float(q[1]), float(q[2]), float(q[3])).normalized();
    w.samplesReceived = cnt;
    return w;
}

// Where a window fell, and what it summarised to.
struct Slice {
    bool   shorterThanSpan = false;   // the samples do not reach back the whole span
    size_t lo = 0, hi = 0;            // sample range [lo, hi)
    imu_calibration::StandTallWindow w;
};

// The live window: every sample whose arrival time lies in (tEndUs − span, tEndUs]. tEndUs is
// "now", not the last sample, so a sensor that stopped streaming a while ago holds nothing in
// the window and fails the sample gate instead of being calibrated from stale data. tUs is
// ascending (the arrival order) and parallel to rows.
inline Slice latest(const std::vector<int64_t> &tUs, const std::vector<pinpoint::ImuSample> &rows,
                    int64_t tEndUs, double spanS, int nominalRateHz)
{
    Slice out;
    const size_t n = std::min(tUs.size(), rows.size());
    const int64_t spanUs = int64_t(std::llround(spanS * 1e6));
    const int64_t t0 = tEndUs - spanUs;
    size_t lo = n, hi = n;
    while (hi > 0 && tUs[hi - 1] > tEndUs) --hi;
    lo = hi;
    while (lo > 0 && tUs[lo - 1] > t0) --lo;
    out.lo = lo;
    out.hi = hi;
    const int64_t dtUs = nominalRateHz > 0 ? int64_t(1000000 / nominalRateHz) : 5000;
    out.shorterThanSpan = (lo == hi) || (lo == 0 && tUs[0] > t0 + dtUs);
    out.w = summarise(rows, lo, hi, spanS, nominalRateHz);
    return out;
}

// One live solve: the latest window, through solveTrunkSegment. The caller applies alignment.A
// and alignment.M ONLY when alignment.valid (§16.2 W2.1a).
struct LiveSolve {
    Slice                           slice;
    imu_calibration::TrunkAlignment alignment;
};

inline LiveSolve solveLatest(const std::vector<int64_t> &tUs, const std::vector<pinpoint::ImuSample> &rows,
                             int64_t tEndUs, int nominalRateHz, imu_calibration::TrunkSensor sensor)
{
    LiveSolve s;
    s.slice     = latest(tUs, rows, tEndUs, kSpanS, nominalRateHz);
    s.alignment = imu_calibration::solveTrunkSegment(s.slice.w, sensor);
    // The gates above read accel and gyro only; the window's mean quaternion sets A's heading. A
    // non-finite orientation (a diverged fusion filter — ImuBase::fuseRawImu now re-seeds it, but
    // a recorded window may still hold one) gives a NaN A that every gate passes, and applied it
    // reads every later turn as 0° (26 Oct). Never valid.
    const auto finite = [](const QQuaternion &q) {
        return std::isfinite(q.scalar()) && std::isfinite(q.x()) && std::isfinite(q.y()) && std::isfinite(q.z());
    };
    if (!finite(s.alignment.A) || !finite(s.alignment.M)) {
        s.alignment.valid = false;
        if (s.alignment.record.failure.isEmpty())
            s.alignment.record.failure = (sensor == imu_calibration::TrunkSensor::Pelvis ? QStringLiteral("Hip sensor")
                                                                                          : QStringLiteral("Chest sensor"))
                                         + QStringLiteral("'s orientation reading is invalid — hold still and try again");
    }
    return s;
}

// The binding's record of a solve (trunk_calib_provenance.h) — present only for a valid one,
// because only a valid one is ever applied.
inline pinpoint::TrunkCalibProvenance provenanceFrom(const imu_calibration::TrunkAlignment &a)
{
    pinpoint::TrunkCalibProvenance p;
    if (!a.valid)
        return p;
    const imu_calibration::TrunkSolveRecord &r = a.record;
    p.present               = true;
    p.deltaTier             = r.deltaTier;
    p.sigmaDeltaDeg         = double(r.sigmaDeltaDeg);
    p.sampleFraction        = double(r.sampleFraction);
    p.maxAbsGyroDps         = double(r.maxAbsGyroDps);
    p.gravityErrPct         = double(r.gravityErrPct);
    p.longAxisFromUpDeg     = double(r.longAxisFromUpDeg);
    p.faceFromHorizontalDeg = double(r.faceFromHorizontalDeg);
    return p;
}

inline QVariantList quatList(const QQuaternion &q)
{
    return QVariantList{ double(q.scalar()), double(q.x()), double(q.y()), double(q.z()) };
}

// The record as ImuInstance::calibrateTrunk returns it to QML. The key names are the contract
// the trunk routine (W2.2) reads — see imu_instance.h, where they are listed. "role",
// "deviceId", "applied" and "calibratedAt" are the instance's to add.
inline QVariantMap toVariantMap(const LiveSolve &s)
{
    const imu_calibration::TrunkSolveRecord &r = s.alignment.record;
    QVariantMap m;
    m.insert(QStringLiteral("valid"),                 s.alignment.valid);
    m.insert(QStringLiteral("failure"),               r.failure);
    m.insert(QStringLiteral("samplesOk"),             r.samplesOk);
    m.insert(QStringLiteral("stillOk"),               r.stillOk);
    m.insert(QStringLiteral("gravityOk"),             r.gravityOk);
    m.insert(QStringLiteral("longAxisOk"),            r.longAxisOk);
    m.insert(QStringLiteral("faceOk"),                r.faceOk);
    m.insert(QStringLiteral("sampleFraction"),        double(r.sampleFraction));
    m.insert(QStringLiteral("maxAbsGyroDps"),         double(r.maxAbsGyroDps));
    m.insert(QStringLiteral("gravityErrPct"),         double(r.gravityErrPct));
    m.insert(QStringLiteral("longAxisFromUpDeg"),     double(r.longAxisFromUpDeg));
    m.insert(QStringLiteral("faceFromHorizontalDeg"), double(r.faceFromHorizontalDeg));
    m.insert(QStringLiteral("deltaTier"),             r.deltaTier);
    m.insert(QStringLiteral("sigmaDeltaDeg"),         double(r.sigmaDeltaDeg));
    m.insert(QStringLiteral("samplesReceived"),       s.slice.w.samplesReceived);
    m.insert(QStringLiteral("samplesExpected"),       s.slice.w.samplesExpected);
    m.insert(QStringLiteral("windowS"),               kSpanS);
    m.insert(QStringLiteral("shorterThanSpan"),       s.slice.shorterThanSpan);
    // What the solve produced, applied or not ("a rejected record still says what it would have
    // been", imu_calibration.h). [w, x, y, z].
    m.insert(QStringLiteral("alignA"),                quatList(s.alignment.A));
    m.insert(QStringLiteral("mountM"),                quatList(s.alignment.M));
    return m;
}

} // namespace trunk_stand_tall
