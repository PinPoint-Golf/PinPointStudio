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

// THE SENSOR MANAGER'S LINK TO THE RIG (information_model.md OBJ-RIG, CLS-RIG, SET-09; docs/design/rig_design.md §4.1,
// §4.3; rig_design_addendum.md decisions 1 and 8). Since the sensors' cut-over (platform refactor C-6b.1) the Rig is the
// one writer of each sensor's role (imu/roles before it), alias (imu/alias) and enabled-on-this-machine flag
// (imu/excluded). ImuManager holds no copy of its own in the settings: it is handed the Rig's two parts as a typed value
// (pinpoint::rig::RigParts) and is told of every change to either, and it asks for a change through the Rig's commands
// (RigSensorCommands for a role; setAlias and setEnabledOnThisMachine, which the contract declares on RigCameraCommands
// and which take any device's key), never rig.h (lint A10). What it does with a sensor's facts lives here, as plain
// functions over values, so each rule has a test that needs no sensor (imu_rig_link_test):
//
//   - the hardware facts a claim needs, as the table the Rig is handed (today's ImuManager::deviceFacts);
//   - a sensor's alias and the disabled list, read from the two parts by their own spellings;
//   - what the manager does when an enabled flag changes: decision 8's row, today's behaviour;
//   - the manager's writers as requests: each turns what Settings or the set-up asked for into one Rig command;
//   - the role a sensor's alignment record names, which the manager hands each sensor object with the Rig's sink;
//   - the two automatic role writers (a HackMotion's bare pin migrated, a found HackMotion taking the arm), which decide
//     by today's rules (imu_roles::migrateBareHackMotion, imu_roles::claimArmForUnassignedHackMotion) and write the map
//     they decided through claimRole, only when claimRole gives exactly that map (requestHackMotionMap, below).
//
// Adapters ring (src/Gui/imu), context Acquisition. Includes the Vocabulary's Rig area, the Rig's commands' contract and
// the role rule (src/IMU/imu_role_map.h) only.

#include "../../DomainModel/rig_commands.h"
#include "../../IMU/imu_role_map.h"
#include "../../Vocabulary/Rig/rig_state.h"
#include "../../Vocabulary/Rig/sensor_facts.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace pp_imurig {

using pinpoint::rig::DeviceKey;
using pinpoint::rig::RigParts;
using pinpoint::rig::SensorFacts;
using pinpoint::rig::SensorFactsTable;

// ── What the manager reads from the two parts ──

// One sensor the enumerator lists, as the manager knows it: its id, whether this session has it switched on, whether
// it is a HackMotion. Listed means present: the enumerator never forgets a device registered in this process.
struct ListedSensor {
    QString id;
    bool    sessionEnabled = true;
    bool    hackMotion     = false;
};
// The hardware facts a claim needs, for every listed sensor (today's ImuManager::deviceFacts: present, switched on for
// the session, a HackMotion). A sensor the enumerator does not list reads as SensorFactsTable::unknownDevice().
SensorFactsTable factsTable(const QList<ListedSensor> &listed);

// A sensor's alias, by the alias map's own spelling "<description>|<deviceId>" (decision 9: never by key equality, so
// two descriptions of one id are two entries); "" when it has none.
QString aliasOf(const RigParts &parts, const QString &aliasKey);
// The sensors' aliases as today's imu/alias map, keyed by that spelling: what imu_roles::sensorDisplayName reads.
QVariantMap aliasMapOf(const RigParts &parts);
// The bare device ids of the sensors not enabled on this machine (today's imu/excluded), in the Rig's order.
QStringList disabledOf(const RigParts &parts);

// ── What the manager does when a sensor's enabled flag changes (RigDeviceLabelChanged; decision 8's row) ──
//
// Today's diff of the machine-wide list against the last one seen: each newly disabled sensor is switched off for the
// session (which disconnects it), each re-enabled one switched on; a sensor whose flag did not change keeps the
// session's own choice.
struct EnabledDiff {
    QStringList switchOff;
    QStringList switchOn;
};
EnabledDiff enabledDiff(const QStringList &disabledBefore, const QStringList &disabledNow);

// ── The manager's writers, as requests (each is one Rig command; the manager reacts to the Rig's events) ──

// The Sensors step's (and Settings') choice of a mount: "" releases every claim the device holds; any other text is
// claimed as its placement role, refused or displacing as today's rule decides (claimRole). A text that is not a
// placement role is the rule's caller error, as today.
pinpoint::rig::RoleClaim requestRole(pinpoint::RigSensorCommands &rig, const QString &deviceId, const QString &role,
                                     const SensorFactsTable &facts);
// The alias field: trimmed; an empty one removes the alias (the next time the sensor is found the default is seeded
// again). `aliasKey` is the alias map's spelling, "<description>|<deviceId>" (the device list's imuKey).
void requestAlias(pinpoint::RigCameraCommands &rig, const QString &aliasKey, const QString &typed);
// A newly found sensor with no alias gets the default, its key's parts with every '|' a space
// (imu_roles::seededAlias). Returns whether it was asked for.
bool requestAliasSeed(pinpoint::RigCameraCommands &rig, const RigParts &parts, const DeviceKey &sensor);
// The Enable switch: a machine-wide preference that seeds each session. A sensor's flag is keyed by its bare id.
void requestEnabled(pinpoint::RigCameraCommands &rig, const QString &deviceId, bool enabled);

// ── The role a sensor's alignment record names (C-6b.2; rig_design_addendum.md decision 4) ──
//
// The role the placement holds in the rig state's roles (today's imu/roles map, rolesMapOf), by the placement key's own
// spelling: what ImuManager hands each sensor object with the Rig's calibration sink, so the record names the role it
// was solved for. Unknown when the placement holds none (a HackMotion still on its old bare pin holds none per lane).
pinpoint::analysis::SegmentRole roleOfPlacement(const QVariantMap &roles, const pinpoint::rig::PlacementKey &placement);

// ── The two automatic role writers (RR8, RR9: today's rules decide, claimRole writes) ──
//
// A HackMotion's bare pin migrated to its two lanes, or a found HackMotion taking the lead arm, writes a map today's
// pure rule decided (imu_roles::migrateBareHackMotion, imu_roles::claimArmForUnassignedHackMotion): the device's own
// keys replaced by its two lanes (lower arm: lead forearm; palm: lead hand), and EVERY OTHER CLAIM LEFT STANDING, a
// parked or absent sensor's included. claimRole, handed the real facts, would displace an absent sensor's claim on
// either arm role (a Witmotion not yet found when the HackMotion is would lose its mount), and would refuse a migration
// today's rule makes over a live double claim. So the write hands claimRole the table sparingFacts makes: every other
// claimant of the two arm roles reads as present and switched off for the session (a parked set-up, which claimRole
// neither refuses nor displaces), the device itself as the real table has it. With that table claimRole replaces the
// device's keys by the pair and leaves every other entry as it was.
SensorFactsTable sparingFacts(const QVariantMap &roles, const QString &deviceId, const SensorFactsTable &actual);
// Writes `wanted` (the map today's rule decided from `roles`, which is the rig state's) through one claimRole of the lead
// forearm by `deviceId`, ONLY when claimRole over `roles` with the sparing table gives exactly `wanted`; otherwise it
// asks for nothing and returns false (a map no claim can make, such as a bare pin dropped beside a lane holding another
// role: today's DroppedBare over a half or hand-edited pair). True when the Rig took the claim.
bool requestHackMotionMap(pinpoint::RigSensorCommands &rig, const QVariantMap &roles, const QString &deviceId,
                          const QVariantMap &wanted, const SensorFactsTable &actual);

} // namespace pp_imurig
