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
// manager and app.rig call since C-5b; learnClubLength, which the capture service calls since C-7).
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
// Platform, its repository and its commands' contract (§6 rule 4), never Gui, Buffer or app_settings.h (§6 rule 5).

#include "rig_commands.h"
#include "rig_repository.h"
#include "../Vocabulary/Identity/identity.h"
#include "../Vocabulary/Rig/rig_state.h"

#include <QObject>

namespace pinpoint {

class Rig : public QObject, public RigCameraCommands, public RigLearningCommands
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

    // ── The learned prior (RigLearningCommands; rig_commands.h): saved, no event ──
    void learnClubLength(const rig::ClubLengthPriorKey &key, const QVariantMap &learned) override;

signals:
    // EVT-15: the versioned part changed. Once per command, after the save; never for the state loaded at launch.
    void rigChanged(const pinpoint::RigRef &ref);
    // The unversioned part changed for one device: its alias or its enabled flag (decision 8's second event,
    // RigDeviceLabelChanged). Once per command, after the save. Recomputes nothing; never for a load.
    void rigDeviceLabelChanged(const pinpoint::rig::DeviceKey &device, pinpoint::RigDeviceLabel which);

private:
    bool save();
    // A versioned command: `change` edits a copy of the state; a new digest makes it the state in force.
    template <typename Change> void changeState(Change change);

    RigRepository &m_repo;
    bool           m_readOnly = false;   // the file loaded is one this build must not write
    bool           m_unsaved  = false;   // the last save failed, and nothing has saved since
    RigId          m_id;
    RigStateId     m_stateId;
    rig::RigParts  m_parts;
};

} // namespace pinpoint
