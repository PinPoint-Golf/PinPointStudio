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

#include "imu_rig_link.h"

namespace pp_imurig {

namespace ir = pinpoint::imu_roles;

SensorFactsTable factsTable(const QList<ListedSensor> &listed)
{
    SensorFactsTable t;
    for (const ListedSensor &s : listed)
        t.set(DeviceKey::fromPersistedId(s.id), SensorFacts{ true, s.sessionEnabled, s.hackMotion });
    return t;
}

QString aliasOf(const RigParts &parts, const QString &aliasKey)
{
    for (const auto &a : parts.other.aliases)
        if (a.first.isSensor() && a.first.persistedWithDescription() == aliasKey) return a.second;
    return {};
}

QVariantMap aliasMapOf(const RigParts &parts)
{
    QVariantMap out;
    for (const auto &a : parts.other.aliases)
        if (a.first.isSensor()) out.insert(a.first.persistedWithDescription(), a.second);
    return out;
}

QStringList disabledOf(const RigParts &parts)
{
    QStringList out;
    for (const auto &e : parts.other.enabledOnThisMachine)
        if (e.first.isSensor() && !e.second && !out.contains(e.first.persistedId())) out << e.first.persistedId();
    return out;
}

EnabledDiff enabledDiff(const QStringList &disabledBefore, const QStringList &disabledNow)
{
    EnabledDiff d;
    for (const QString &id : disabledNow)
        if (!disabledBefore.contains(id)) d.switchOff << id;
    for (const QString &id : disabledBefore)
        if (!disabledNow.contains(id)) d.switchOn << id;
    return d;
}

pinpoint::rig::RoleClaim requestRole(pinpoint::RigSensorCommands &rig, const QString &deviceId, const QString &role,
                                     const SensorFactsTable &facts)
{
    const DeviceKey sensor = DeviceKey::fromPersistedId(deviceId);
    if (role.isEmpty()) return rig.releaseRole(sensor);
    // A text that names no placement role reaches the rule as Unknown, which it refuses as "not a placement role".
    return rig.claimRole(sensor, pinpoint::analysis::segmentRoleForPlacementRole(role), facts);
}

void requestAlias(pinpoint::RigCameraCommands &rig, const QString &aliasKey, const QString &typed)
{
    rig.setAlias(DeviceKey::fromPersistedWithDescription(aliasKey), typed.trimmed());
}

bool requestAliasSeed(pinpoint::RigCameraCommands &rig, const RigParts &parts, const DeviceKey &sensor)
{
    if (!sensor.isSensor() || !aliasOf(parts, sensor.persistedWithDescription()).isEmpty()) return false;
    rig.setAlias(sensor, ir::seededAlias(sensor));
    return true;
}

void requestEnabled(pinpoint::RigCameraCommands &rig, const QString &deviceId, bool enabled)
{
    rig.setEnabledOnThisMachine(DeviceKey::fromPersistedId(deviceId), enabled);
}

pinpoint::analysis::SegmentRole roleOfPlacement(const QVariantMap &roles, const pinpoint::rig::PlacementKey &placement)
{
    return pinpoint::analysis::segmentRoleForPlacementRole(roles.value(placement.persisted()).toString());
}

SensorFactsTable sparingFacts(const QVariantMap &roles, const QString &deviceId, const SensorFactsTable &actual)
{
    SensorFactsTable t;
    t.set(DeviceKey::fromPersistedId(deviceId), actual.factsFor(deviceId));
    for (auto it = roles.cbegin(); it != roles.cend(); ++it) {
        const QString role = it.value().toString();
        if (role != ir::leadForearm() && role != ir::leadHand()) continue;
        const QString owner = ir::ownerOfKey(it.key());
        if (owner == deviceId) continue;
        t.set(DeviceKey::fromPersistedId(owner), SensorFacts{ true, false, actual.factsFor(owner).hackMotion });
    }
    return t;
}

bool requestHackMotionMap(pinpoint::RigSensorCommands &rig, const QVariantMap &roles, const QString &deviceId,
                          const QVariantMap &wanted, const SensorFactsTable &actual)
{
    const SensorFactsTable spared = sparingFacts(roles, deviceId, actual);
    const ir::DeviceFactsFn fn = [&spared](const QString &id) { return spared.factsFor(id); };
    const ir::ClaimResult would = ir::claimRole(roles, deviceId, ir::leadForearm(), fn);
    if (!would.ok || would.roles != wanted) return false;
    return rig.claimRole(DeviceKey::fromPersistedId(deviceId), pinpoint::analysis::SegmentRole::LeadForearm, spared).ok;
}

} // namespace pp_imurig
