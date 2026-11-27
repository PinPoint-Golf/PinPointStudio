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

// The two functions of the role map that return a word the golfer reads (a sensor's display name), kept in Gui when
// the role rules moved to src/IMU/imu_role_map.h (rig_design.md §5, §6 rule 4): the Rules ring holds no golfer-facing
// label. Text unchanged from the role map.
#include "../../IMU/imu_role_map.h"

#include <QRegularExpression>   // stripDeviceIds; no longer reached through imu_role_map.h (C-6a.2)

namespace pinpoint::imu_roles {

// The name to show for a sensor, from what is REMEMBERED — it may be switched off and so absent
// from the device list. ⚠ NEVER A RAW DEVICE ID: on macOS a BLE id is a "{…}" UUID, and the
// golfer was shown "Lead forearm + hand — {767a6a67-…} not found". In order:
//   1. the alias the coach gave it. An alias equal to "<description> <id>" is the one
//      ImuManager SEEDS for every newly seen device (deviceAdded), not a name anyone gave;
//   2. its description — `presentDescription` when the enumerator lists it, else the description
//      half of its alias key (remembered from when it was last seen);
//   3. "HackMotion wG3" when the roles map holds one of its unit keys ("<id>#lowerArm"/"#palm");
//   4. "sensor".
// Any UUID-shaped token (the form a raw BLE id takes) is stripped from 1 and 2 before they are
// used.
inline QString stripDeviceIds(QString s)
{
    static const QRegularExpression uuid(QStringLiteral(
        R"(\{?[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}\}?)"));
    s.remove(uuid);
    return s.simplified();
}
inline QString sensorDisplayName(const QString &deviceId, const QString &presentDescription,
                                 const QVariantMap &aliasMap, const QVariantMap &roles)
{
    QString desc = presentDescription, alias;
    if (!desc.isEmpty()) {
        alias = aliasMap.value(aliasKey(desc, deviceId)).toString();
    } else {
        const QString tail = aliasKey(QString(), deviceId);           // "|<id>"
        for (auto it = aliasMap.cbegin(); it != aliasMap.cend(); ++it)
            if (it.key().endsWith(tail)) {
                desc  = it.key().left(it.key().size() - tail.size());
                alias = it.value().toString();
                break;
            }
    }
    if (alias == desc + QStringLiteral(" ") + deviceId) alias.clear();   // the seeded default
    alias = stripDeviceIds(alias);
    if (!alias.isEmpty()) return alias;
    desc = stripDeviceIds(desc);
    if (!desc.isEmpty()) return desc;
    if (roles.contains(deviceId + QStringLiteral("#lowerArm")) || roles.contains(deviceId + QStringLiteral("#palm")))
        return QStringLiteral("HackMotion wG3");
    return QStringLiteral("sensor");
}

}   // namespace pinpoint::imu_roles
