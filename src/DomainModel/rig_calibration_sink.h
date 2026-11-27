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

// RigCalibrationSink: THE RIG'S CALIBRATION SINK, as a contract (docs/design/rig_design.md §4.8, "The sink"; §6 rule 3;
// rig_design_addendum.md decision 4, ruled 25 Nov 2026; information_model.md R-RIG-3, R-RIG-4). The Rig (rig.h)
// implements it; the composition root hands it to ImuManager, which hands it to each sensor's live object as it makes
// it (ImuDeviceBase::setCalibrationSink, src/Gui/imu/imu_device.h). One of the Domain model's contract headers (lint
// A9's hubContracts()): it declares only an interface, and includes only the Vocabulary's Rig area, never rig.h (lint
// A10), Gui, Buffer, src/IMU or app_settings.h.
//
// WHO CALLS IT: only the device objects, ImuInstance and HmInstance, which are adapters (R-RIG-4; decision 4: "the
// device object is the only caller of the sink"). The Rig's record follows the live sensor's alignment, whatever changed
// it: the device object commits a record whenever its anatomical alignment (A and M) is set or changes, and clears it
// whenever that alignment ends (a clear of it, the device object's teardown, a HackMotion leaving "calibrated"). A
// worn sensor's record is in the rig state and its digest, held for the run of the app: never saved to rig.json and
// never handed back to a sensor (rig_design.md §4.2).
//
// WHAT EACH CALL DOES (the Rig's side; rig_commands.h states the versioned rule it follows):
//   - commit: the record becomes its subject's record of its kind. Only a sensor's `sensorAlignment` record, on a
//     PlacementKey, is taken in Stage C; any other record changes nothing (a ppWarn says so). A commit that changes the
//     state makes one new rig state and one RigChanged; one equal to what is held changes nothing and emits nothing.
//   - clear: the subject's record of that kind goes. A clear of nothing changes nothing and emits nothing.
//   - Neither writes rig.json when the only change is a sensor's alignment (the file never holds one); a change whose
//     save failed earlier is still saved again at the next call (P4).
//   - The caller has already set (or cleared) its own state: a RigChanged listener reads the device as it now is.

#include "../Vocabulary/Rig/calibration_record.h"
#include "../Vocabulary/Rig/device_key.h"

namespace pinpoint {

class RigCalibrationSink
{
public:
    virtual ~RigCalibrationSink() = default;

    // The record a device object now holds, with its subject (a sensor's PlacementKey: a Witmotion's bare id, or one
    // lane of a HackMotion), kind, method, parameters, uncertainty and time observed.
    virtual void commit(const rig::CalibrationRecord &record) = 0;
    // The device object's alignment ended: the record of `kind` on `subject` goes. Keyed by the PlacementKey the record
    // carries, since a sensor's records are held per placement and a HackMotion has two.
    virtual void clear(const rig::PlacementKey &subject, rig::CalibrationKind kind) = 0;
};

} // namespace pinpoint
