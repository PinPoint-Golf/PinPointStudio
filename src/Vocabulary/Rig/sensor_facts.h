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

// What a claim of a sensor role needs to know about the hardware, and what it answers (docs/design/rig_design.md §4.1,
// invariant 2; information_model.md OBJ-RIG). Vocabulary ring, context Acquisition. Header-only; it includes only
// Vocabulary/Common, the device keys and Qt value types (rig_design.md §6 rule 1).
//
// ONE CLAIM PER ROLE needs three facts about a device that only the live device side knows: whether the device is
// present (listed by the enumerator), whether this session has it switched on, and whether it is a HackMotion. The Rig
// never asks a device (§4.1): the caller hands it these facts as one value, a SensorFactsTable, and the Rig hands the
// role rule (src/IMU/imu_role_map.h, imu_roles::claimRole) a lookup over it. imu_roles::DeviceFacts is an alias of
// SensorFacts, so the role rule and its callers read the same type as before it moved here.
//
// THE DEFAULT FOR A DEVICE THE TABLE DOES NOT HOLD is what the app's facts function answers today for an id the
// enumerator does not list (ImuManager::deviceFacts): not present, not a HackMotion, and switched on for the session,
// since it is not in the session's exclusion list. No role rule reads `sessionEnabled` of a device that is not present,
// so this default and an all-false one decide alike; it is today's, so nothing can differ.

#include "device_key.h"
#include "../Common/segment_role.h"

#include <QHash>
#include <QList>
#include <QString>

namespace pinpoint::rig {

// The facts the role rule needs about one device (moved unchanged from imu_roles::DeviceFacts). `present` means the
// enumerator lists it; it does not require a connection. `sessionEnabled` is this session's switch, not the machine's.
struct SensorFacts {
    bool present        = false;
    bool sessionEnabled = false;
    bool hackMotion     = false;

    friend bool operator==(const SensorFacts &, const SensorFacts &) = default;
};

// The facts of the devices the caller knows, by bare device id (a sensor DeviceKey's persistedId()).
class SensorFactsTable
{
public:
    // What a device the table does not hold reads as: today's answer for a device the enumerator does not list.
    static constexpr SensorFacts unknownDevice() { return SensorFacts{ false, true, false }; }

    // A sensor's facts; a key that is not a sensor's is ignored.
    void set(const DeviceKey &sensor, SensorFacts facts)
    {
        if (sensor.isSensor() && !sensor.id().isEmpty()) m_byId.insert(sensor.id(), facts);
    }

    // The facts of the device with this bare id, or unknownDevice().
    SensorFacts factsFor(const QString &deviceId) const { return m_byId.value(deviceId, unknownDevice()); }

    bool holds(const QString &deviceId) const { return m_byId.contains(deviceId); }
    int  size() const { return int(m_byId.size()); }

private:
    QHash<QString, SensorFacts> m_byId;
};

// What a claim of a role answered (the Rig's claimRole and releaseRole; rig_commands.h): today's
// imu_roles::ClaimResult, typed, without the map (the Rig holds the result in its state).
struct RoleClaim {
    bool                  ok = false;
    analysis::SegmentRole refusedRole = analysis::SegmentRole::Unknown;   // the role that was held (for a HackMotion
                                                                          // claiming the arm, possibly LeadHand)
    DeviceKey             holder;      // who holds it: a sensor key by its bare device id; null unless refused
    QList<PlacementKey>   displaced;   // the claims removed from absent devices (both lanes of an absent HackMotion)
    QString               error;       // a caller error (no sensor, a role that is not a placement role, a HackMotion
                                       // asked for anything but the lead forearm); the state unchanged
};

} // namespace pinpoint::rig
