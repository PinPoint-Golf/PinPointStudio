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

// Rig: THE RIG, one per machine, at the application level (information_model.md §2.3 OBJ-RIG, CLS-RIG; lifecycle
// model LC-RIG; docs/design/rig_design.md §4.1). It holds the machine's capture set-up in two parts side by side
// (rig::RigParts): the rig state, versioned, whose digest is its RigStateId; and the Rig's other facts, not versioned.
// It is their one writer, and the one writer of rig.json, through RigRepository (rig_repository.h). It changes them only
// by its commands (rig_commands.h, the contract its callers are handed: the camera commands, C-5a.1, which the camera
// manager and app.rig call since C-5b; learnClubLength, which the capture service calls since C-7; the sensor commands,
// claimRole and releaseRole, C-6a.2, which ImuManager calls since the sensors' cut-over, C-6b.1, and app.rig's sensor
// members read). It also takes each worn sensor's alignment record, through its calibration sink
// (rig_calibration_sink.h, since C-6b.2), which the sensors' live objects call.
//
// THE SENSORS' ALIGNMENT RECORDS (rig_design.md §4.2, §4.8; rig_design_addendum.md decision 4). The record follows the
// live sensor's alignment: a device object commits one whenever its A and M are set or changed, and clears it whenever
// that alignment ends. Each commit or clear that changes the state is one new rig state and one RigChanged, by the
// versioned rule (rig_commands.h); one that changes nothing emits nothing. A record is in the state and its digest and
// is never saved: a change to the alignment records alone writes nothing to rig.json, and the state loaded at launch
// holds none. A placement keeps its record through a role released or moved (decision 4.6), so an entry with a record
// and no role is kept; it is never written to rig.json (its role is the only fact the file would hold of it).
//
// THE SENSORS' ROLES (invariant 2, one claim per role; rig_design.md §4.1, §4.3). The rig state holds each sensor
// placement's role. A claim is decided by today's rule, imu_roles::claimRole, over the roles as today's imu/roles map
// (imu_roles::rolesMapOf: the state's sensors that hold a role, each under its placement key's own spelling), with the hardware
// facts the caller hands in; the map it answers becomes the state again, each placement keeping its alignment record.
// Entries the Rig's store keeps outside the state (P2: a sensor entry this build cannot read; the settings entries the
// copy could not type) are not in that map, so a claim neither displaces nor replaces them; they are written back as
// read. The placement view (imu_roles::placementViewOf: today's imuPlacement) is derived from the roles by
// imu_roles::placementFromRoles, never stored.
//
// THE ASSUMED RECORDS (RP-8; rig_design.md §4.3, §4.8; information_model.md §2.3, "Calibration"). The two set-up
// assumptions the analysis makes are held as `extrinsics` records with method `assumed`, which the Rig derives from
// each camera's set-up whenever that changes (by a command, and at load()): a face-on camera fixed in place is square
// and level (yaw, pitch, roll 0, no orientation sigma: AD-7); a down-the-line camera with no measured extrinsics is
// level along the target line, its yaw uncertain by 9° (rig::kDtlYawSigmaUncalDeg, which the analysis reads from the
// same header). What each says is Vocabulary/Rig/camera_assumptions.h's. They are in the rig state and its digest, and saved to rig.json with the
// camera's other records. The analysis never reads the records; it reads the same header's yaw sigmas, unchanged (RP-9).
//
// LIFECYCLE. The composition root makes it, with its store, on the GUI thread, and calls load() once. It has no
// states and lives for the application's lifetime. On a machine with nothing stored it starts empty, with a new
// RigId, and saves that once. The state loaded at launch, with its assumed records made again, is the state in force,
// and no event is emitted for it; when making them again changed it, it is saved once. A camera entry is dropped at the
// load only when it held nothing but an assumed record that no longer applies; any other entry stays as read.
//
// THREAD AND FAILURE. The owner thread only (the GUI thread in the app). A save that fails is logged with ppWarn by
// the store; the state in memory stands, the Rig remembers it is unsaved, and the next command, or the quit
// (saveIfUnsaved), writes it again. Nothing is refused because the disk failed.
//
// WHO INCLUDES THIS HEADER: only the composition root (src/Gui/main.cpp, src/App), app.rig (src/Gui/app/rig_presenter)
// and the Rig's store (lint A10; rig_design.md §6 rule 8). It includes only the Vocabulary's Rig and Identity areas,
// Platform, its repository, its commands' contract and its calibration sink's, and (rig.cpp only) the pure role rule src/IMU/imu_role_map.h, of
// the Rules ring (§6 rule 4, ruled by the Solution Architect, 27 Nov 2026); never Gui, Buffer, any other file of src/IMU
// or app_settings.h (§6 rule 5).

#include "rig_calibration_sink.h"
#include "rig_commands.h"
#include "rig_repository.h"
#include "../Vocabulary/Identity/identity.h"
#include "../Vocabulary/Rig/rig_state.h"

#include <QObject>
#include <QVariantMap>

namespace pinpoint {

class Rig : public QObject,
            public RigCameraCommands,
            public RigSensorCommands,
            public RigLearningCommands,
            public RigCalibrationSink
{
    Q_OBJECT
public:
    explicit Rig(RigRepository &repository, QObject *parent = nullptr);

    // Once, at launch: the stored rig, or an empty one with a new RigId (saved), its assumed records made again from
    // the set-up (saved when that changed it). Emits nothing.
    void load();

    RigId                id() const { return m_id; }
    RigStateId           stateId() const { return m_stateId; }
    RigRef               ref() const { return RigRef(m_id, m_stateId); }
    const rig::RigParts &parts() const { return m_parts; }

    // A change whose save failed is remembered (the launch's own save too): the next command saves the whole rig again,
    // and so does saveIfUnsaved(), which the composition root calls at quit (lifecycle model RIG-T4; the Solution
    // Architect's ruling P4, 27 Nov 2026). A file this build must not write (RigLoad::ReadOnly) is never "unsaved": its
    // refusal is not a failure to retry. No notice is shown: that would be a design-system change.
    bool hasUnsavedChange() const { return m_unsaved; }
    void saveIfUnsaved();

    // ── The camera commands (RigCameraCommands; rig_commands.h says what each does) ──
    void assignPerspective(const rig::DeviceKey &camera, int perspectiveCode) override;
    void setMirrored(const rig::DeviceKey &camera, bool mirrored) override;
    void setFixedInPlace(const rig::DeviceKey &camera, bool fixedInPlace) override;
    void setHittingArea(const rig::DeviceKey &camera, const QRectF &box) override;
    void clearHittingArea(const rig::DeviceKey &camera) override;
    void setAlias(const rig::DeviceKey &device, const QString &alias) override;
    void setEnabledOnThisMachine(const rig::DeviceKey &device, bool enabled) override;

    // ── The sensor commands (RigSensorCommands; rig_commands.h says what each does) ──
    rig::RoleClaim claimRole(const rig::DeviceKey &sensor, analysis::SegmentRole role,
                             const rig::SensorFactsTable &facts) override;
    rig::RoleClaim releaseRole(const rig::DeviceKey &sensor) override;

    // The derived placement view, today's imuPlacement (the arm roles as the legacy slot letters A, B, C under the same
    // keys; the trunk roles left out): imu_roles::placementViewOf over the rig state (src/IMU/imu_role_map.h, where the
    // state as today's imu/roles map, imu_roles::rolesMapOf, lives too, since C-6b.1: ImuManager reads both without
    // this header). Read-only, never stored.
    QVariantMap placementView() const;

    // ── The learned prior (RigLearningCommands; rig_commands.h): saved, no event ──
    void learnClubLength(const rig::ClubLengthPriorKey &key, const QVariantMap &learned) override;

    // ── A sensor's alignment record (RigCalibrationSink; rig_calibration_sink.h says what each does) ──
    // A sensorAlignment record on a PlacementKey becomes that placement's record (the placement's entry made, with no
    // role, when it has none); any other record changes nothing. The clear removes the placement's record of `kind`.
    // Versioned, never saved: no rig.json write when only an alignment changed.
    void commitCalibration(const rig::CalibrationRecord &record);
    void clearCalibration(const rig::PlacementKey &subject, rig::CalibrationKind kind);
    void commit(const rig::CalibrationRecord &record) override { commitCalibration(record); }
    void clear(const rig::PlacementKey &subject, rig::CalibrationKind kind) override { clearCalibration(subject, kind); }

signals:
    // EVT-15: the versioned part changed. Once per command, after the save; never for the state loaded at launch.
    void rigChanged(const pinpoint::RigRef &ref);
    // The unversioned part changed for one device: its alias or its enabled flag (decision 8's second event,
    // RigDeviceLabelChanged). Once per command, after the save. Recomputes nothing; never for a load.
    void rigDeviceLabelChanged(const pinpoint::rig::DeviceKey &device, pinpoint::RigDeviceLabel which);

private:
    bool save();
    // A versioned command: `change` edits a copy of the state; a new digest makes it the state in force. It is saved
    // when what rig.json holds of it changed (the state without its sensors' alignment records), or when an earlier
    // save failed (P4); RigChanged follows either way.
    template <typename Change> void changeState(Change change);
    // One claim through today's rule: `roleName` is the role's settings name, "" to release.
    rig::RoleClaim claim(const rig::DeviceKey &sensor, const QString &roleName, const rig::SensorFactsTable &facts);

    RigRepository &m_repo;
    bool           m_readOnly = false;   // the file loaded is one this build must not write
    bool           m_unsaved  = false;   // the last save failed, and nothing has saved since
    RigId          m_id;
    RigStateId     m_stateId;
    rig::RigParts  m_parts;
};

} // namespace pinpoint
