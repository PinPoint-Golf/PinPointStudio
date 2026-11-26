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

// A calibration record: the RESULT of a calibration, a source fact owned by the Rig, held in the rig state
// (information_model.md §2.3, OBJ-RIG "Calibration"; docs/design/rig_design.md §4.8, "The typed result"). The
// procedure that makes one belongs to Acquisition (deferred to #291); this is only the value it returns.
// Vocabulary ring, context Acquisition. Header-only, Qt value types only (rig_design.md §6 rule 1).
//
//   subject        the camera's DeviceKey, or the sensor's PlacementKey
//   kind           intrinsics | extrinsics | focus | sensorAlignment
//   method         factory | per_frame | solved | user_measured | estimated_online | assumed | deviceHeld
//   parameters     typed per kind: a sensorAlignment holds the role, A, M, the two mount checks and the trunk solve's
//                  provenance (the facts ImuSegmentBinding records for a sensor today, no more); a camera kind holds
//                  its parameters by name (#74 names them; Stage C writes only `extrinsics` with method `assumed`)
//   uncertainty    MANDATORY: "not known" is a stated value (Uncertainty::notKnown()), never an omission. The type has
//                  no default, so a record cannot be made without saying it
//   observedAtUtc  when the calibration was observed
//   evidence       OPTIONAL: where the raw run is kept (a session id and a file name). It is a pointer set after the
//                  record is committed (C-14), so it is OUTSIDE the digest: setting it makes no new rig state. A
//                  reader treats a missing file as "no evidence kept"
//
// The persisted spellings of the kinds and methods are the design's, and are what the canonical bytes write
// (rig_state.h): never reorder or respell one. `deviceHeld` is the HackMotion: its calibration lives in the device,
// so its record says the device reported "calibrated", and when. Its A and M are the host's fixed frame for that unit
// (A = identity, M = the unit's mount), as each shot's `streams[].device` records them; they are not a solved alignment
// (rig_design_addendum.md decision 4).

#include "device_key.h"
#include "../Common/segment_role.h"
#include "../Common/trunk_calib_provenance.h"
#include "../Identity/identity.h"

#include <QDateTime>
#include <QList>
#include <QPair>
#include <QQuaternion>
#include <QString>

#include <algorithm>
#include <initializer_list>
#include <optional>
#include <utility>
#include <variant>

namespace pinpoint::rig {

enum class CalibrationKind { Intrinsics, Extrinsics, Focus, SensorAlignment };
enum class CalibrationMethod { Factory, PerFrame, Solved, UserMeasured, EstimatedOnline, Assumed, DeviceHeld };

inline QString calibrationKindName(CalibrationKind k)
{
    switch (k) {
    case CalibrationKind::Intrinsics:      return QStringLiteral("intrinsics");
    case CalibrationKind::Extrinsics:      return QStringLiteral("extrinsics");
    case CalibrationKind::Focus:           return QStringLiteral("focus");
    case CalibrationKind::SensorAlignment: return QStringLiteral("sensorAlignment");
    }
    return {};
}
inline QString calibrationMethodName(CalibrationMethod m)
{
    switch (m) {
    case CalibrationMethod::Factory:         return QStringLiteral("factory");
    case CalibrationMethod::PerFrame:        return QStringLiteral("per_frame");
    case CalibrationMethod::Solved:          return QStringLiteral("solved");
    case CalibrationMethod::UserMeasured:    return QStringLiteral("user_measured");
    case CalibrationMethod::EstimatedOnline: return QStringLiteral("estimated_online");
    case CalibrationMethod::Assumed:         return QStringLiteral("assumed");
    case CalibrationMethod::DeviceHeld:      return QStringLiteral("deviceHeld");
    }
    return {};
}

// Named numbers, kept sorted by name so two equal sets are equal however they were built. A camera kind's parameters
// and every kind's σ values are held this way.
class NamedValues
{
public:
    NamedValues() = default;
    NamedValues(std::initializer_list<std::pair<QString, double>> values)
    {
        for (const auto &v : values) set(v.first, v.second);
    }
    void set(const QString &name, double value)
    {
        auto it = std::lower_bound(m_values.begin(), m_values.end(), name,
                                   [](const QPair<QString, double> &e, const QString &n) { return e.first < n; });
        if (it != m_values.end() && it->first == name) it->second = value;
        else m_values.insert(it, qMakePair(name, value));
    }
    const QList<QPair<QString, double>> &values() const { return m_values; }
    bool isEmpty() const { return m_values.isEmpty(); }

private:
    QList<QPair<QString, double>> m_values;
};

// The uncertainty of a record: stated, always. Either "not known", or σ values by name (degrees, metres, ... as the
// kind defines). No default constructor: a record says which.
class Uncertainty
{
public:
    static Uncertainty notKnown() { return Uncertainty(false, {}); }
    static Uncertainty sigmas(NamedValues values) { return Uncertainty(true, std::move(values)); }

    bool               isKnown() const { return m_known; }
    const NamedValues &values() const { return m_values; }

private:
    Uncertainty(bool known, NamedValues values) : m_known(known), m_values(std::move(values)) {}
    bool        m_known = false;
    NamedValues m_values;
};

// A worn sensor's alignment: A and M with the two mount checks (ImuSegmentBinding's alignA, mountM,
// mountDeviationDeg, mountGravityErrorDeg). A HackMotion's device-held record holds A = identity and M = the unit's
// mount (addendum decision 4).
struct SensorAlignment {
    QQuaternion alignA;   // fusion-world -> anatomical-world (A)
    QQuaternion mountM;   // anatomical-body -> sensor-body (M)
    double      mountDeviationDeg    = 0.0;
    double      mountGravityErrorDeg = 0.0;
};
struct SensorAlignmentParameters {
    analysis::SegmentRole          role = analysis::SegmentRole::Unknown;
    std::optional<SensorAlignment> alignment;    // none only when no A and M are known; deviceHeld: identity A, the mount M
    TrunkCalibProvenance           trunkSolve;   // present only when A and M came from the trunk stand-tall solve
};

// Where a calibration's raw run is kept.
struct CalibrationEvidence {
    SessionId sessionId;
    QString   fileName;
};

struct CalibrationRecord {
    std::variant<DeviceKey, PlacementKey>                  subject;   // a camera's DeviceKey, a sensor's PlacementKey
    CalibrationKind                                        kind   = CalibrationKind::Extrinsics;
    CalibrationMethod                                      method = CalibrationMethod::Assumed;
    std::variant<NamedValues, SensorAlignmentParameters>   parameters;   // SensorAlignmentParameters iff sensorAlignment
    Uncertainty                                            uncertainty;  // mandatory
    QDateTime                                              observedAtUtc;
    std::optional<CalibrationEvidence>                     evidence;     // outside the digest

    // A camera's record of kind intrinsics, extrinsics or focus.
    static CalibrationRecord forCamera(const DeviceKey &camera, CalibrationKind kind, CalibrationMethod method,
                                       NamedValues parameters, Uncertainty uncertainty, const QDateTime &observedAtUtc)
    {
        return { camera, kind, method, std::move(parameters), std::move(uncertainty), observedAtUtc, std::nullopt };
    }
    // A sensor's alignment.
    static CalibrationRecord forSensor(const PlacementKey &sensor, CalibrationMethod method,
                                       SensorAlignmentParameters parameters, Uncertainty uncertainty,
                                       const QDateTime &observedAtUtc)
    {
        return { sensor, CalibrationKind::SensorAlignment, method, std::move(parameters), std::move(uncertainty),
                 observedAtUtc, std::nullopt };
    }
};

} // namespace pinpoint::rig
