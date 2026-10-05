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

#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <functional>

#include "../../Analysis/segment_role.h"

// ---------------------------------------------------------------------------
// Role-keyed IMU placement — the pure logic (session_wizard_refactor_design.md §4.13)
// ---------------------------------------------------------------------------
//
// `imu/roles` maps a PLACEMENT KEY to a ROLE NAME:
//   Witmotion   "<deviceId>"           → "pelvis" | "thorax" | "leadUpperArm" | "leadForearm" | "leadHand"
//   HackMotion  "<deviceId>#lowerArm"  → "leadForearm"
//               "<deviceId>#palm"      → "leadHand"
// (unit-key spelling owned by src/IMU/hm_unit_id.h).
//
// Everything here is a function over that QVariantMap plus, where the answer depends on
// the hardware, a DeviceFactsFn the caller supplies. ImuManager supplies the real one
// (DeviceEnumerator + the session exclusion list); imu_role_map_test supplies a table. So
// the owner ladder, the one-claim-per-role rule and both migrations are tested without
// a BLE stack, and the app runs the same code the test ran.
//
// TWO HALVES, ON PURPOSE. The functions in this header are INLINE because AppSettings
// calls them, and AppSettings is compiled into four targets (the app, both QML suites and
// the update-policy suite) that would otherwise all have to learn a new .cpp. They need
// nothing but QtCore. The device-fact half (imu_role_map.cpp) is called by ImuManager
// only, and it alone needs the HackMotion unit-key parser.
namespace pinpoint::imu_roles {

// ── Names ──────────────────────────────────────────────────────────────────────────

inline QString pelvis()       { return analysis::placementRoleName(analysis::SegmentRole::Pelvis); }
inline QString thorax()       { return analysis::placementRoleName(analysis::SegmentRole::Thorax); }
inline QString leadUpperArm() { return analysis::placementRoleName(analysis::SegmentRole::LeadUpperArm); }
inline QString leadForearm()  { return analysis::placementRoleName(analysis::SegmentRole::LeadForearm); }
inline QString leadHand()     { return analysis::placementRoleName(analysis::SegmentRole::LeadHand); }

// The fixed order every role list is reported in: trunk first, then down the lead arm.
// rolesInSession uses it, so a QML list bound to it never reorders as sensors come and go.
inline QStringList placementRoleOrder()
{
    return { pelvis(), thorax(), leadUpperArm(), leadForearm(), leadHand() };
}

inline bool isPlacementRole(const QString &role)
{
    return analysis::segmentRoleForPlacementRole(role) != analysis::SegmentRole::Unknown;
}

// ── The legacy slot letters (the …ForSlot shims, until Stage 8) ─────────────────────
//
// Through the WRIST map, the only one slot letters ever had: A forearm, B hand, C upper
// arm. "D", "other" and "" have no role and answer "". Trunk roles have no letter.
// ── A sensor's name, as the golfer may read it (Stage 5d) ────────────────────────────
//
// The alias map (AppSettings::imuAlias) is keyed "<description>|<deviceId>" — this is that
// key, the ONE spelling; ImuManager builds and reads it only through here.
inline QString aliasKey(const QString &description, const QString &deviceId)
{
    return description + QStringLiteral("|") + deviceId;
}

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

inline QString roleForLegacySlot(const QString &slot)
{
    if (slot == QLatin1String("A")) return leadForearm();
    if (slot == QLatin1String("B")) return leadHand();
    if (slot == QLatin1String("C")) return leadUpperArm();
    return {};
}

inline QString legacySlotForRole(const QString &role)
{
    if (role == leadForearm())  return QStringLiteral("A");
    if (role == leadHand())     return QStringLiteral("B");
    if (role == leadUpperArm()) return QStringLiteral("C");
    return {};
}

// The derived, read-only `imuPlacement` view: the arm roles as letters under the same
// keys, trunk roles omitted. Its one reader labels OLD swings (which carry no role) on the
// review screen (PpDataViewer.qml → SwingDataSource, segmentRoleForSlot).
inline QVariantMap placementFromRoles(const QVariantMap &roles)
{
    QVariantMap out;
    for (auto it = roles.cbegin(); it != roles.cend(); ++it) {
        const QString slot = legacySlotForRole(it.value().toString());
        if (!slot.isEmpty()) out.insert(it.key(), slot);
    }
    return out;
}

// ── The one-time migration imu/placement → imu/roles ────────────────────────────────

struct RolesLoad {
    QVariantMap roles;
    bool        migrated = false;   // true → the caller persists `roles` and the marker
    QStringList log;                // one line per legacy entry, for ppInfo
};

// ⚠ THE MARKER, NOT THE MAP, DECIDES. "Never migrated" and "migrated, then the coach
// unassigned everything" both leave an empty roles map; keying the migration on emptiness
// would resurrect every assignment the coach had cleared on the next launch. So
// `alreadyMigrated` is a separate persisted flag, and once it is set the stored roles are
// returned exactly as they are — empty included.
//
// The legacy keys are carried across VERBATIM: a Witmotion's bare id, a wG3's unit keys.
// A wG3 still on Phase A's bare-id pin ("<id>" → "A") arrives here as "<id>" →
// "leadForearm" and is split into its two unit keys by ImuManager when the device is
// enumerated (migrateBareHackMotion below) — only the enumerator knows a bare id is a
// HackMotion. `imu/placement` itself is never written: reverting the build must find it
// exactly as it was.
inline RolesLoad loadRoles(bool alreadyMigrated, const QVariantMap &storedRoles,
                           const QVariantMap &legacyPlacement)
{
    RolesLoad r;
    if (alreadyMigrated) {
        r.roles = storedRoles;
        return r;
    }
    r.migrated = true;
    for (auto it = legacyPlacement.cbegin(); it != legacyPlacement.cend(); ++it) {
        const QString slot = it.value().toString();
        const QString role = roleForLegacySlot(slot);
        if (!role.isEmpty()) {
            r.roles.insert(it.key(), role);
            r.log << QStringLiteral("%1: slot %2 → role %3").arg(it.key(), slot, role);
        } else if (!slot.isEmpty()) {
            r.log << QStringLiteral("%1: slot %2 has no role and is dropped").arg(it.key(), slot);
        }
    }
    return r;
}

// ── The device-fact half (imu_role_map.cpp) ────────────────────────────────────────

// What the resolver needs to know about a device id. `present` means the enumerator can
// see it (the same test the slot resolver has always used — a device row exists and its
// state is inspectable); it does NOT require a connection.
struct DeviceFacts {
    bool present        = false;
    bool sessionEnabled = false;
    bool hackMotion     = false;
};
using DeviceFactsFn = std::function<DeviceFacts(const QString &deviceId)>;

// The device id a key belongs to: the key itself for a Witmotion (or an unmigrated bare
// wG3 pin), the part before the unit suffix for a wG3 unit key.
QString ownerOfKey(const QString &key);

// Splits a wG3 unit key. False for a bare key. `unit` is hm_unit_id's int
// (kLowerArm / kPalm).
bool parseUnitKey(const QString &key, QString *deviceId, int *unit);

// The owner ladder for one role:
//   1. present AND session-enabled — the sensor actually in play;
//   2. present but disabled — a parked setup;
//   3. any claimant at all — placement is read before the first scan completes.
// Within a rung, QVariantMap key order. `enabledKeys` lists every rung-1 claimant so the
// caller can warn (once) about a real double claim.
struct RoleResolution {
    QString     key;           // the winning placement key, "" when the role is unheld
    QStringList enabledKeys;   // every present + enabled claimant, in key order
};
RoleResolution resolveRole(const QVariantMap &roles, const QString &role, const DeviceFactsFn &facts);

// Device id of the present, session-enabled holder of `role`, else "".
QString roleHolder(const QVariantMap &roles, const QString &role, const DeviceFactsFn &facts);

// Roles with a present, session-enabled holder, in placementRoleOrder().
QStringList rolesInSession(const QVariantMap &roles, const DeviceFactsFn &facts);

// The role a DEVICE was assigned. A wG3 answers for its lower-arm unit ("leadForearm"),
// its palm unit's role following from the cable. No ladder: this is what the device was
// given, which is what a binding records.
QString roleForDevice(const QVariantMap &roles, const QString &deviceId);

// One claim per role.
//
//   role ""                        unassign: every key the device owns is removed.
//   a HackMotion                   "leadForearm" only, and it then holds leadForearm
//                                  (lower-arm unit) AND leadHand (palm unit) — the cable
//                                  fixes which unit is on which segment.
//   held by another device that is PRESENT and SESSION-ENABLED
//                                  REFUSED; `refusedRole` / `holderId` name the conflict.
//   held by an ABSENT device       that claim is displaced (both unit keys of an absent
//                                  wG3); `displaced` lists the keys removed.
//   held by a present, DISABLED device
//                                  both claims stand — a parked setup, arbitrated by the
//                                  ladder's enabled-first rung.
struct ClaimResult {
    bool        ok = false;
    QVariantMap roles;          // the new map when ok, the input unchanged otherwise
    QString     refusedRole;    // the role that was held (for a wG3, possibly "leadHand")
    QString     holderId;       // who holds it
    QStringList displaced;      // keys removed from absent devices
    QString     error;          // a caller error (unknown role, a wG3 asked for another role)
};
ClaimResult claimRole(const QVariantMap &roles, const QString &deviceId, const QString &role,
                      const DeviceFactsFn &facts);

// Phase A's interim bare-id pin on a wG3, split into its two unit keys. Run when the
// device becomes known to the enumerator. Idempotent.
struct HmMigration {
    enum Outcome {
        NoChange,      // nothing keyed by the bare id
        DroppedBare,   // unit keys already exist, or the bare value was empty: bare key removed
        Migrated,      // bare "leadForearm" → lowerArm leadForearm + palm leadHand
        Unmappable,    // bare value is some other role: left in place, unresolved
        Blocked,       // leadHand is held by another device: left in place
    };
    Outcome     outcome = NoChange;
    QVariantMap roles;
    QString     bareRole;   // for Unmappable
    QString     holderId;   // for Blocked
};
HmMigration migrateBareHackMotion(const QVariantMap &roles, const QString &deviceId,
                                  const DeviceFactsFn &facts);

}   // namespace pinpoint::imu_roles
