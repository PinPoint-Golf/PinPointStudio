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

#include <QQuaternion>
#include <QString>
#include <QVector3D>
#include <algorithm>
#include <cmath>
#include <vector>

#include "../Core/pp_tuned_constants.h"
#include "imu_calibration.h"
#include "trunk_angles.h"

// The trunk ceremony's turn checks (trunk_imu_sequence_grf_design.md §6.3 steps 2 "Turn away" and
// 3 "Turn through"; §16.2 W2.1b). Checks, not solves: nothing here changes A or M.
//
// Kept apart from imu_calibration.h because that header is the A·q·M solve, included by the live
// write path and the fuser; the turn check is ceremony-only, needs a whole step's series and the D13
// turn (trunk_angles.h), and neither belongs in every includer of the solve.
//
// WHAT THE TURNS CAN SEE (design §6.1–§6.3). The step is read through the same A·q·M every consumer
// uses, so it checks the sign of what the analysis will read — the HackMotion lesson: sign is the only
// thing that falsifies a frame. Two turns are read per sensor:
//   * the D13 turn (trunk_angles::turnFromRad): the world heading of q_anat·x̂ — body_rotation's turn
//     and what the live check shows;
//   * the S-I turn: the twist of q_anat about the segment's OWN long axis ẑ — what the KS rate
//     integrates (design D3, gyro_segment·ẑ).
// For a sensor read through its own stand-tall solve both are right whatever the mount: gravity fixes
// the segment's up exactly, and a turn about up commutes with any error about up. So:
//   * FACE-IN PASSES, by design (§6.3 "Face-in cannot be caught by stand tall or the turns"): it is a
//     180° error about the spine, which an upright turn cannot see. The first address catches it.
//   * Mount yaw δ passes: the turn is δ-immune (§6.2).
//   * "Reads the turn backwards" is a sensor whose segment up points DOWN in the solved frame: the S-I
//     turn flips. That is a sensor upside down relative to its stand-tall solve — re-seated or slipped
//     between the steps, an A/M from another session, or an axis sign broken anywhere in the chain. A
//     sensor upside down at stand tall AND the turns is rejected by stand tall (the long-axis gate);
//     its solve would absorb the flip exactly and the turns would read right.
//   * Swapped sensors: the "hip" series turns more than the "chest" series (§6.7: hips about 45°,
//     chest about 90° away).
//   * The axis gate is about the golfer more than the mount: an upright turn is about world +Z for any
//     mount, so a tilted axis is a lean or a bend during the turn (or a loose sensor).
//
// ONE STEP, NOT ONE REPETITION. The step asks for the turn twice; the check reads the whole step. The
// gates are facts about the mount, which does not change between two turns ten seconds apart, so a
// per-repetition verdict adds nothing a whole-step one lacks. Segmenting the step into repetitions
// would add a failure of its own (pauses, a half turn, a third turn) and a gate the design does not
// ask for. Over the whole step the range is the furthest excursion, the axis the angle-weighted mean
// of every sample's rotation, and the sense that of the furthest excursion. The routine repeats the
// whole step on a failure (§6.3: "repeats only that step"), so the second turn is the golfer's margin.
//
// Pure and header-only (QtGui types only), unit-tested in imu_calibration_test §H.
namespace imu_calibration {

enum class TrunkTurnDirection { Away, Through };

// ONE trunk sensor's series over one turn step, as the caller holds it.
//   qRaw: the fused sensor→world quaternion per sample, in time order, from the start of the step
//         (the golfer standing, arms crossed, before turning) to its end. The FIRST sample is the
//         step's reference: the range is measured from it.
//   A, M: this sensor's TrunkAlignment from the stand-tall solve (solveTrunkSegment).
// A caller that already holds q_anat passes it as qRaw with A and M left at the identity.
struct TrunkTurnSeries {
    std::vector<QQuaternion> qRaw;
    QQuaternion A;
    QQuaternion M;
};

// What the step showed on ONE sensor. Angles in degrees.
struct TrunkTurnSensorRecord {
    int   samples             = 0;
    float rangeDeg            = 0.0f;   // |the D13 turn| at its furthest from the step's first sample
    float turnDeg             = 0.0f;   // the D13 turn there, signed: + = about +Z (opening for a right-hander)
    float siTurnDeg           = 0.0f;   // the S-I turn (about the segment's own ẑ, D3) at the same sample, signed alike
    float axisFromVerticalDeg = 90.0f;  // the turn's world axis (angle-weighted mean) from the vertical line
    bool  rangeOk = false;              // ≥ kTrunkTurnChestMinRangeDeg (chest) / kTrunkTurnHipsMinRangeDeg (hips)
    bool  senseOk = false;              // the D13 AND the S-I turn both in the direction asked
    bool  axisOk  = false;              // axisFromVerticalDeg ≤ kTrunkTurnAxisMaxFromVerticalDeg
    QString failure;                    // golfer-facing, names this sensor and the cause; empty when it passed
};

struct TrunkTurnRecord {
    TrunkTurnDirection direction = TrunkTurnDirection::Away;
    bool  leftHanded = false;
    int   expectedSign = 0;             // the sign of the turn about +Z the direction asks for
    TrunkTurnSensorRecord pelvis;       // the hip sensor
    TrunkTurnSensorRecord thorax;       // the chest sensor
    float chestOverHips = 0.0f;         // thorax range / pelvis range (0 when the hips did not move)
    bool  orderOk  = false;             // chestOverHips > kTrunkTurnChestOverHipsMin: not swapped
    bool  wrongWay = false;             // BOTH sensors turned the other way: the golfer (or the hand set), not a mount
    bool  valid    = false;             // every gate on both sensors, and the order
    QString failure;                    // the step's one golfer-facing message; empty when valid
    // Which rule gave `failure` — the routine's branch, so it never parses the words (W2.2):
    //   "" (valid) | "noData" | "tooLittle" | "wrongWay" | "backwards" | "swapped" | "range" | "tilt".
    // "backwards" and "swapped" mean a sensor sits other than its stand-tall solve assumed (re-seated,
    // slipped, or on the other segment): the routine sends the golfer back to stand tall, because a
    // re-seated sensor's A/M no longer holds. Every other cause repeats the turn.
    QString cause;
};

namespace detail {
// Rotation vector (rad) of a unit quaternion, the shorter way round: axis × angle.
inline QVector3D rotationVectorRad(const QQuaternion &qIn)
{
    QQuaternion q = qIn.normalized();
    if (q.scalar() < 0.0f) q = -q;
    const double vx = q.x(), vy = q.y(), vz = q.z();
    const double s = std::sqrt(vx * vx + vy * vy + vz * vz);
    if (s < 1e-12) return QVector3D();
    const double angle = 2.0 * std::atan2(s, double(q.scalar()));
    return QVector3D(float(vx / s * angle), float(vy / s * angle), float(vz / s * angle));
}

// Twist (rad, (−π, π]) of a body-frame rotation about the body's ẑ (swing-twist decomposition).
inline double twistAboutZRad(const QQuaternion &qIn)
{
    const QQuaternion q = qIn.normalized();
    return trunk_angles::wrapPi(2.0 * std::atan2(double(q.z()), double(q.scalar())));
}

inline TrunkTurnSensorRecord readTurnSensor(const TrunkTurnSeries &s, double minRangeDeg)
{
    namespace tc = pinpoint::tuned::calib;
    constexpr double kRadToDeg = 57.29577951308232;

    TrunkTurnSensorRecord r;
    r.samples = int(s.qRaw.size());
    if (r.samples < 2) return r;

    const QQuaternion q0 = toAnatomical(s.A, s.qRaw.front(), s.M);
    const QQuaternion q0inv = q0.conjugated();
    double peakAbs = 0.0, peakTurn = 0.0, peakSi = 0.0;
    double ax = 0.0, ay = 0.0, az = 0.0;
    for (const QQuaternion &qRaw : s.qRaw) {
        const QQuaternion q = toAnatomical(s.A, qRaw, s.M);
        const double turn = trunk_angles::turnFromRad(q, q0);
        if (std::abs(turn) > peakAbs) {
            peakAbs  = std::abs(turn);
            peakTurn = turn;
            peakSi   = twistAboutZRad(q0inv * q);      // body frame: q = q0·B
        }
        const QVector3D rv = rotationVectorRad(q * q0inv);   // world frame: q = R·q0
        ax += rv.x(); ay += rv.y(); az += rv.z();
    }
    r.rangeDeg  = float(peakAbs * kRadToDeg);
    r.turnDeg   = float(peakTurn * kRadToDeg);
    r.siTurnDeg = float(peakSi * kRadToDeg);
    const double an = std::sqrt(ax * ax + ay * ay + az * az);
    if (an > 1e-9)
        r.axisFromVerticalDeg = float(std::acos(std::clamp(std::abs(az) / an, 0.0, 1.0)) * kRadToDeg);
    r.rangeOk = r.rangeDeg >= minRangeDeg;
    r.axisOk  = r.axisFromVerticalDeg <= tc::kTrunkTurnAxisMaxFromVerticalDeg;
    return r;
}
} // namespace detail

// The turn check for one step (away or through) on both trunk sensors. Handedness flips "away":
// a right-hander turns away about −Z and through about +Z (pinpoint_sign_conventions.md "Trunk IMU
// readings": opening is + about +Z for a right-hander); a left-hander the reverse.
//
// The step's message is the first that applies, most specific first:
//   no data → too little turn to judge anything → the golfer turned the other way (both sensors) →
//   a sensor reads it backwards → swapped → the chest's range → the hips' range → a tilted axis.
// "Enough to judge" a sense or the order is the hips' range gate on the sensor concerned: below it the
// heading is not far enough from the start for its sign to mean anything about the mount.
inline TrunkTurnRecord checkTrunkTurn(const TrunkTurnSeries &pelvis, const TrunkTurnSeries &thorax,
                                      TrunkTurnDirection direction, bool leftHanded)
{
    namespace tc = pinpoint::tuned::calib;
    const bool away = direction == TrunkTurnDirection::Away;

    TrunkTurnRecord out;
    out.direction = direction;
    out.leftHanded = leftHanded;
    out.expectedSign = (away ? -1 : 1) * (leftHanded ? -1 : 1);
    out.pelvis = detail::readTurnSensor(pelvis, tc::kTrunkTurnHipsMinRangeDeg);
    out.thorax = detail::readTurnSensor(thorax, tc::kTrunkTurnChestMinRangeDeg);

    const float judgeDeg = tc::kTrunkTurnHipsMinRangeDeg;
    const auto sgn = [](float v) { return v > 0.0f ? 1 : (v < 0.0f ? -1 : 0); };
    const auto judged = [&](const TrunkTurnSensorRecord &r) { return r.samples >= 2 && r.rangeDeg >= judgeDeg; };
    const auto worldRight = [&](const TrunkTurnSensorRecord &r) { return sgn(r.turnDeg) == out.expectedSign; };
    for (TrunkTurnSensorRecord *r : { &out.pelvis, &out.thorax })
        r->senseOk = worldRight(*r) && sgn(r->siTurnDeg) == out.expectedSign;

    out.chestOverHips = out.pelvis.rangeDeg > 1e-3f ? out.thorax.rangeDeg / out.pelvis.rangeDeg : 0.0f;
    out.orderOk = out.pelvis.rangeDeg > 1e-3f ? out.chestOverHips > tc::kTrunkTurnChestOverHipsMin
                                              : out.thorax.rangeDeg > 0.0f;
    out.wrongWay = judged(out.pelvis) && judged(out.thorax)
                   && !worldRight(out.pelvis) && !worldRight(out.thorax);

    // ── Per-sensor messages (name the sensor and the cause, §6.3) ──
    const QString turnFurther = away
        ? QStringLiteral("turn further, as if to the top, and back")
        : QStringLiteral("turn further toward the target, and back");
    const auto sensorFailure = [&](TrunkTurnSensorRecord &r, const QString &who, bool chest) {
        if (r.samples < 2)
            r.failure = who + QStringLiteral(" sent no data — check it is switched on and connected");
        else if (judged(r) && !r.senseOk && !out.wrongWay)
            r.failure = who + QStringLiteral(" reads the turn backwards — check it sits USB-down, label facing out");
        else if (!r.rangeOk)
            r.failure = chest ? QStringLiteral("Chest turned too little — ") + turnFurther
                              : QStringLiteral("Hips turned too little — let them turn with your chest");
        else if (!r.axisOk)
            r.failure = who + QStringLiteral(" turned on a tilt — stand tall and turn level, without leaning");
    };
    sensorFailure(out.pelvis, QStringLiteral("Hip sensor"), false);
    sensorFailure(out.thorax, QStringLiteral("Chest sensor"), true);

    // ── The step's one message ──
    const bool bothBackwards = judged(out.pelvis) && judged(out.thorax) && !out.wrongWay
                               && !out.pelvis.senseOk && !out.thorax.senseOk;
    QString &f = out.failure;
    const auto fail = [&out](const QString &message, const char *cause) {
        out.failure = message;
        out.cause   = QLatin1String(cause);
    };
    if (out.pelvis.samples < 2)
        fail(out.pelvis.failure, "noData");
    else if (out.thorax.samples < 2)
        fail(out.thorax.failure, "noData");
    else if (std::max(out.pelvis.rangeDeg, out.thorax.rangeDeg) < judgeDeg)
        fail(QStringLiteral("Cross your arms and ") + turnFurther, "tooLittle");
    else if (out.wrongWay)
        fail(away ? QStringLiteral("You turned toward the target — turn away from it, as if to the top")
                  : QStringLiteral("You turned away from the target — turn toward it this time"), "wrongWay");
    else if (bothBackwards)
        fail(QStringLiteral("Hip and chest sensors read the turn backwards — check they sit USB-down, label facing out"),
             "backwards");
    else if (judged(out.thorax) && !out.thorax.senseOk)
        fail(out.thorax.failure, "backwards");
    else if (judged(out.pelvis) && !out.pelvis.senseOk)
        fail(out.pelvis.failure, "backwards");
    else if (!out.orderOk)
        fail(QStringLiteral("Hip and chest sensors look swapped"), "swapped");
    else if (!out.thorax.rangeOk)
        fail(out.thorax.failure, "range");
    else if (!out.pelvis.rangeOk)
        fail(out.pelvis.failure, "range");
    else if (!out.pelvis.axisOk && !out.thorax.axisOk)
        fail(QStringLiteral("Hip and chest sensors turned on a tilt — stand tall and turn level, without leaning"), "tilt");
    else if (!out.thorax.axisOk)
        fail(out.thorax.failure, "tilt");
    else if (!out.pelvis.axisOk)
        fail(out.pelvis.failure, "tilt");

    out.valid = f.isEmpty();
    if (out.valid) out.cause.clear();
    return out;
}

} // namespace imu_calibration
