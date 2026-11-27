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

// THE RIG'S COMMANDS, as a contract (docs/design/rig_design.md §4.1; rig_design_addendum.md decision 1, ruled 25 Nov
// 2026). The Rig (rig.h) implements them; the composition root hands each caller the interface it may call, so a
// caller never includes rig.h (lint A10). One of the Domain model's contract headers (lint A9's hubContracts()): it
// declares only interfaces and value types, and no owner class or service. It includes only the Vocabulary's Rig
// area, Identity and Platform (Qt value types), never rig.h, Gui, Buffer or app_settings.h.
//
// Today: the camera commands, which the app calls since the cameras' cut-over (C-5b): CameraManager is handed
// RigCameraCommands by the composition root (src/Gui/cameras/camera_rig_link.h), and app.rig forwards QML's. The
// learned club-length prior's command (RigLearningCommands), which the capture service calls since C-7: ShotProcessor
// is handed it by the composition root. The sensor commands join in C-6a.2.
//
// WHAT EACH COMMAND DOES (rig_design.md §4.1, §4.2):
//   - The versioned ones (the perspective, mirrored, fixed in place, the hitting-area box) make a new rig state, save
//     it, then emit RigChanged with the new RigRef, once. One that leaves the state's digest unchanged saves nothing
//     and emits nothing.
//   - The unversioned ones (an alias, enabled on this machine) save, then emit RigDeviceLabelChanged once, carrying
//     the device's key and which of the two changed (decision 8). They make no new rig state and no RigChanged. One
//     that changes nothing saves nothing and emits nothing.
//   - learnClubLength (SET-05) is learned state: it saves, and emits nothing at all (no RigChanged, no
//     RigDeviceLabelChanged; the Rig design addendum's decision 8). One that changes nothing saves nothing.
//   - A save that fails is logged by the store; the change stands in memory, its event is emitted, and the Rig
//     remembers it is unsaved: the next command, or the quit, saves the whole rig again (P4). No command is refused
//     because the disk failed.
//   - The assumed extrinsics records (rig.h, "The assumed records") follow the set-up in the same new state: a
//     command that makes a camera face-on and fixed in place, or down the line, gives it its record, and one that
//     ends that takes it away. They are part of the command's one save and one RigChanged, never a second state.
//   - A camera entry whose versioned facts are all their defaults (perspective None, not mirrored, not fixed, no box,
//     no calibration record) is the same as no entry: it is not kept in the rig state. So putting a camera back as it
//     was gives back the earlier RigStateId, whether or not the camera was known before.
//   - A camera command given a key that is not a camera's (a sensor's, or a null key) changes nothing, and so does
//     setAlias or setEnabledOnThisMachine given a null key.

#include "../Vocabulary/Rig/club_length_prior_key.h"
#include "../Vocabulary/Rig/device_key.h"

#include <QRectF>
#include <QString>
#include <QVariantMap>

namespace pinpoint {

// Which of a device's unversioned facts changed (RigDeviceLabelChanged; decision 8).
enum class RigDeviceLabel { Alias, EnabledOnThisMachine };

class RigCameraCommands
{
public:
    virtual ~RigCameraCommands() = default;

    // The camera's perspective, by its stored integer code (recording::Perspective's; a code this build does not
    // know is kept as it is and written back unchanged). Impact is exclusive: assigning it to one camera makes every
    // other camera that held it None, as CameraManager::assignPerspective does today (invariant 1).
    //
    // PRECONDITION (decision 8): a caller that moves a camera to or from Impact writes that role's device settings
    // (the exposure, the rate, the `#impact` crop) first. Those settings are not the Rig's, so the Rig neither checks
    // nor orders them; reconnecting a camera whose Impact-ness changed is the camera manager's reaction (C-5b).
    virtual void assignPerspective(const rig::DeviceKey &camera, int perspectiveCode) = 0;
    virtual void setMirrored(const rig::DeviceKey &camera, bool mirrored) = 0;
    // The box is kept whether or not the camera is fixed in place; it is applied only while it is (§4.1).
    virtual void setFixedInPlace(const rig::DeviceKey &camera, bool fixedInPlace) = 0;
    // The hitting-area box, normalised to the frame (0..1), stored as given. One command at the end of an edit.
    virtual void setHittingArea(const rig::DeviceKey &camera, const QRectF &box) = 0;
    virtual void clearHittingArea(const rig::DeviceKey &camera) = 0;

    // The device's display alias; an empty alias removes it (as today: the next seeding gives the default again).
    // Looked up by the device's own spelling, never by key equality (decision 9).
    virtual void setAlias(const rig::DeviceKey &device, const QString &alias) = 0;
    // Whether the device is enabled on this machine: a machine-wide preference that seeds each session. Enabled is
    // the default and is not stored.
    virtual void setEnabledOnThisMachine(const rig::DeviceKey &device, bool enabled) = 0;
};

// The Rig's learned state (SET-05; information_model.md OBJ-RIG, CLS-RIG): the club-length prior, one entry per
// athlete, club and face-on camera, kept in the Rig's other facts and saved to rig.json under the key's own spelling
// (ClubLengthPriorKey::persisted(), today's analysis/clubLenPrior key, byte for byte).
class RigLearningCommands
{
public:
    virtual ~RigLearningCommands() = default;

    // What a shot learned for `key`, after its live analysis: each member of `learned` replaces the stored entry's
    // member of that name, and every other member of the stored entry stays, as the settings writer before C-7 read
    // the entry, set its fields and wrote it back. The entry is found by the key's spelling, never by key equality, as
    // the reader finds it. A null key, or an entry already holding every member as given, changes nothing and saves
    // nothing. Saved, with no event; never refused because the disk failed.
    virtual void learnClubLength(const rig::ClubLengthPriorKey &key, const QVariantMap &learned) = 0;
};

} // namespace pinpoint
