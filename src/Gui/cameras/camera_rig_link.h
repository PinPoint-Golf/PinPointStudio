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

// THE CAMERA MANAGER'S LINK TO THE RIG (information_model.md OBJ-RIG, CLS-RIG; docs/design/rig_design.md §4.1;
// rig_design_addendum.md decisions 1 and 8). Since the cameras' cut-over the Rig is the one writer of a camera's
// perspective, mirrored flag, fixed-in-place flag, hitting-area box, alias and enabled flag (SET-09's six camera keys
// before it). CameraManager holds no copy of its own: it is handed the Rig's two parts as a typed value
// (pinpoint::rig::RigParts) and is told of every change to either, and it asks for a change through the Rig's commands
// (RigCameraCommands, rig_commands.h), never rig.h (lint A10). What it does with a camera's facts lives here, as plain
// functions over values, so each rule has a test that needs no camera (camera_rig_link_test):
//
//   - the facts of one camera, read from the two parts by its key (an absent entry is every default);
//   - what a newly connected camera is given (createController's restore, today's: a perspective above None, pose off
//     for Impact, the mirror flag, and the box only while the camera is fixed in place);
//   - what the manager does to its live cameras when the rig state changes: decision 8's table, today's behaviour;
//   - the manager's writers as requests: each turns what Settings or the set-up asked for into one Rig command.
//
// Adapters ring (src/Gui/cameras), context Acquisition. Includes the Vocabulary's Rig area and the Rig's commands'
// contract only.

#include "camera_roi.h"
#include "../../DomainModel/rig_commands.h"
#include "../../Vocabulary/Rig/rig_state.h"

#include <QList>
#include <QPair>
#include <QRectF>
#include <QString>

#include <optional>

namespace pp_camrig {

using pinpoint::rig::DeviceKey;
using pinpoint::rig::RigParts;
using pinpoint::rig::RigState;

// One camera's facts, as the Rig holds them. A camera with no entry has every default: perspective None (0), not
// mirrored, not fixed, no box, no alias, enabled on this machine.
struct CameraFacts {
    int                   perspectiveCode = 0;
    bool                  mirrored        = false;
    bool                  fixedInPlace    = false;
    std::optional<QRectF> hittingArea;
    QString               alias;
    bool                  enabledOnThisMachine = true;
};
CameraFacts factsOf(const RigParts &parts, const DeviceKey &camera);

// What a camera is given when its live instance is made (CameraManager::createController). The perspective only when
// it is above None; pose estimation off for good on the impact camera (a strip of club and ball has no body); the
// mirror flag; and the stored box only while the camera is fixed in place (a moved camera makes it meaningless; the
// Rig keeps it either way). `fixedInPlace` is held on the instance for the freeze's snapshot; it applies nothing.
struct ConnectRestore {
    std::optional<int>    perspectiveCode;
    bool                  poseOff      = false;
    bool                  mirrored     = false;
    bool                  fixedInPlace = false;
    std::optional<QRectF> box;
};
ConnectRestore restoreAtConnect(const RigParts &parts, const DeviceKey &camera);

// A camera as the manager holds it, for the table below: listed, selected, and whether it has a live instance.
struct ManagedCamera {
    DeviceKey key;
    bool      selected = false;
    bool      live     = false;
};

// What the manager does to its cameras when the rig state changes (RigChanged), by comparing the state it held with
// the new one: only what changed, by decision 8's table, which is today's behaviour.
//   - a perspective: set on the live instance (the camera that lost Impact to another is set to None with it); each
//     SELECTED live camera whose Impact-ness changed is reconnected, so its new instance reads its role's crop, rate,
//     exposure and gain (the Rig has saved the state before it says so); and the camera list changes;
//   - mirrored: set on the live instance;
//   - fixed in place: nothing on the camera; never applies or withholds the box. The live instance holds the flag for
//     the freeze's snapshot only;
//   - the hitting-area box: set on (or cleared from) the live instance.
// A camera with no live instance takes everything at its next connect (restoreAtConnect).
struct StateReaction {
    struct Perspective { DeviceKey camera; int code = 0; };
    struct Flag { DeviceKey camera; bool on = false; };
    struct Box { DeviceKey camera; std::optional<QRectF> box; };
    QList<Perspective> perspectives;
    QList<Flag>        mirrored;
    QList<Flag>        fixedInPlace;
    QList<Box>         boxes;
    QList<DeviceKey>   reconnect;   // in the manager's own order
    bool               listChanged = false;
};
StateReaction reactionTo(const RigState &before, const RigState &after, const QList<ManagedCamera> &cameras);

// What the manager does when a camera's enabled-on-this-machine flag changes (RigDeviceLabelChanged): decision 8's row,
// today's behaviour. A listed camera whose own flag already says so: nothing. Otherwise its flag follows; the session's
// list follows too (a camera disabled on this machine is disabled for this session, and one enabled again is enabled
// for it); and a listed camera is deselected when disabled, selected when enabled. The camera list changes.
struct EnabledCamera {
    bool listed   = false;   // the manager lists the camera
    bool excluded = false;   // its own flag, as the manager holds it
    bool selected = false;
};
struct EnabledReaction {
    bool nothing       = true;
    bool excluded      = false;   // the camera's new flag (listed cameras only)
    bool sessionAdd    = false;   // add its key to the session's excluded list
    bool sessionRemove = false;   // take its key off it
    bool select        = false;
    bool deselect      = false;
};
EnabledReaction enabledReaction(const EnabledCamera &camera, bool inSessionExcluded, bool enabledOnThisMachine);

// ── The manager's writers, as requests (each is one Rig command; the reaction above does the rest) ──
//
// None of them is refused for a key that is not a camera's: the Rig ignores such a key (rig_commands.h), and the
// manager only ever passes the keys of cameras it lists.

// The VIEW combo's choice. Impact is exclusive: the Rig makes the camera that held it None.
void requestPerspective(pinpoint::RigCameraCommands &rig, const DeviceKey &camera, int perspectiveCode);
// The Mirrored chip.
void requestMirrored(pinpoint::RigCameraCommands &rig, const DeviceKey &camera, bool mirrored);
// The hitting-area box, as the Ball step, the ball panel or the frame's editor hands it: normalised and clipped to the
// frame; a box with no area is a clear. Returns the box the live instance is set to (empty for a clear).
QRectF requestBox(pinpoint::RigCameraCommands &rig, const DeviceKey &camera, const QRectF &box);
void   requestClearBox(pinpoint::RigCameraCommands &rig, const DeviceKey &camera);
// The alias field: trimmed; an empty one removes the alias (the next launch seeds the default again).
void requestAlias(pinpoint::RigCameraCommands &rig, const DeviceKey &camera, const QString &typed);
// The Enable switch: a machine-wide preference that seeds each session.
void requestExcluded(pinpoint::RigCameraCommands &rig, const DeviceKey &camera, bool excluded);
// At launch, the default alias of each listed camera that has none: its key with every '|' a space
// (pp_camroi::seededAlias). Returns how many were asked for.
int requestAliasSeeds(pinpoint::RigCameraCommands &rig, const RigParts &parts, const QList<DeviceKey> &cameras);

} // namespace pp_camrig
