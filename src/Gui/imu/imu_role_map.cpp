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

#include "imu_role_map.h"

#include "../../IMU/hm_unit_id.h"

#include <QPair>

namespace pinpoint::imu_roles {

namespace {

namespace hu = pinpoint::hm_unit_id;

QString lowerArmKey(const QString &deviceId) { return hu::unitIdFor(deviceId, hu::kLowerArm); }
QString palmKey(const QString &deviceId)     { return hu::unitIdFor(deviceId, hu::kPalm); }

}   // namespace

bool parseUnitKey(const QString &key, QString *deviceId, int *unit)
{
    // ⚠ NOT RESPELLED HERE. hm_unit_id owns the spelling and regenerates-and-compares, so
    // a suffix this build does not recognise stays a bare key rather than being guessed at.
    return hu::parse(key, deviceId, unit);
}

QString ownerOfKey(const QString &key)
{
    QString devId;
    return parseUnitKey(key, &devId, nullptr) ? devId : key;
}

RoleResolution resolveRole(const QVariantMap &roles, const QString &role, const DeviceFactsFn &facts)
{
    RoleResolution r;
    if (role.isEmpty()) return r;   // "" means unassigned; it is never a role to look up

    // ⚠ A LIVE, ENABLED DEVICE BEATS EVERYTHING ELSE. The map accretes claims from
    // sensors that no longer exist, and it deliberately KEEPS claims from sensors that
    // exist but are switched off this session — that pair of claims is how a coach flips
    // between a HackMotion and a Witmotion on the same roles without re-assigning
    // anything. So resolution is a ladder, not a lookup (see the header for the rungs).
    // Without it, a dead sensor's key that happens to sort first takes the role from a
    // connected one, and the only symptom is a readout that silently never appears.
    QString firstKey;     // rung 3 — any claimant
    QString presentKey;   // rung 2 — present but disabled this session
    for (auto it = roles.cbegin(); it != roles.cend(); ++it) {
        if (it.value().toString() != role) continue;
        if (firstKey.isEmpty()) firstKey = it.key();
        const DeviceFacts f = facts ? facts(ownerOfKey(it.key())) : DeviceFacts{};
        if (!f.present) continue;
        if (!f.sessionEnabled) {
            if (presentKey.isEmpty()) presentKey = it.key();
        } else {
            r.enabledKeys.append(it.key());
        }
    }
    r.key = !r.enabledKeys.isEmpty() ? r.enabledKeys.first()
          : !presentKey.isEmpty()    ? presentKey
          :                            firstKey;
    return r;
}

QString roleHolder(const QVariantMap &roles, const QString &role, const DeviceFactsFn &facts)
{
    const RoleResolution r = resolveRole(roles, role, facts);
    return r.enabledKeys.isEmpty() ? QString() : ownerOfKey(r.enabledKeys.first());
}

QStringList rolesInSession(const QVariantMap &roles, const DeviceFactsFn &facts)
{
    QStringList out;
    for (const QString &role : placementRoleOrder())
        if (!roleHolder(roles, role, facts).isEmpty()) out.append(role);
    return out;
}

QString roleForDevice(const QVariantMap &roles, const QString &deviceId)
{
    if (deviceId.isEmpty()) return {};
    // A wG3 answers for the lower-arm unit: that is the role it was ASSIGNED, and the palm
    // unit's role is a consequence of the cable rather than a second assignment. The bare
    // key comes next (a Witmotion, or a wG3 still on Phase A's pin), the palm key last so a
    // half-pair still names the device's role rather than reading as unassigned.
    const QString lower = lowerArmKey(deviceId);
    if (roles.contains(lower)) return roles.value(lower).toString();
    if (roles.contains(deviceId)) return roles.value(deviceId).toString();
    const QString palm = palmKey(deviceId);
    if (roles.contains(palm)) return roles.value(palm).toString();
    return {};
}

ClaimResult claimRole(const QVariantMap &roles, const QString &deviceId, const QString &role,
                      const DeviceFactsFn &facts)
{
    ClaimResult r;
    r.roles = roles;
    if (deviceId.isEmpty()) {
        r.error = QStringLiteral("no device id");
        return r;
    }

    const QString lower = lowerArmKey(deviceId);
    const QString palm  = palmKey(deviceId);

    if (role.isEmpty()) {
        // Every key the device can own, including Phase A's bare pin on a wG3.
        r.roles.remove(deviceId);
        r.roles.remove(lower);
        r.roles.remove(palm);
        r.ok = true;
        return r;
    }
    if (!isPlacementRole(role)) {
        r.error = QStringLiteral("\"%1\" is not a placement role").arg(role);
        return r;
    }

    const DeviceFacts self = facts ? facts(deviceId) : DeviceFacts{};

    // key → role this claim writes.
    QList<QPair<QString, QString>> targets;
    if (self.hackMotion) {
        // ⚠ A HACKMOTION'S ASSIGNMENT IS NOT A CHOICE OF ONE ROLE. One wG3 is a single BLE
        // peripheral carrying TWO sensor units on a cable, and the cable fixes which unit
        // sits on which segment: block 0 on the lower arm, block 1 on the palm. A settings
        // map that allowed any other pairing would yield a wrist angle that is exactly
        // MIRRORED and passes every plausibility check. So "leadForearm" is the only
        // non-empty role it accepts, and anything else is a caller error, never coerced.
        if (role != leadForearm()) {
            r.error = QStringLiteral("a HackMotion fills leadForearm (lower arm) and leadHand"
                                     " (palm) together, fixed by its cable; \"%1\" is not an"
                                     " assignment that exists for it").arg(role);
            return r;
        }
        targets = { { lower, leadForearm() }, { palm, leadHand() } };
    } else {
        targets = { { deviceId, role } };
    }

    // ⚠ ONE CLAIM PER ROLE, AND A LIVE HOLDER WINS. A second present, enabled device on a
    // held role is refused rather than double-claimed: a double claim hands the answer to
    // the ladder's key order — deterministic, but arbitrary as an ANSWER, with the losing
    // sensor never mentioned. This is also what keeps two instruments off one arm (memory
    // note no-dual-instrument-wear): a wG3 holding the forearm and hand blocks a Witmotion
    // from both, and vice versa. Roles are disjoint across the trunk and the arm, so a wG3
    // and trunk Witmotions never meet here (trunk_imu_design.md §6.8).
    for (const auto &t : targets) {
        for (auto it = roles.cbegin(); it != roles.cend(); ++it) {
            if (it.value().toString() != t.second) continue;
            const QString owner = ownerOfKey(it.key());
            if (owner == deviceId) continue;
            const DeviceFacts f = facts ? facts(owner) : DeviceFacts{};
            if (f.present && f.sessionEnabled) {
                r.refusedRole = t.second;
                r.holderId    = owner;
                return r;
            }
        }
    }

    // An explicit assignment is AUTHORITATIVE over stale claims: a discarded sensor's key
    // still naming the role has no row in the settings panel and nothing else ever prunes
    // it, so if assignment cannot displace it the role is locked with no UI path out.
    //
    // ⚠ A PRESENT OWNER'S CLAIM IS SPARED, EVEN A DISABLED ONE — that is a PARKED setup,
    // not a stale one, and deleting it would make every HackMotion ↔ Witmotion flip cost a
    // re-assignment. ⚠ Displacing one of a wG3's unit keys strips BOTH: a half-assigned
    // pair reads as a unit that was never strapped, and no consumer is written for that.
    QStringList doomed;
    for (const auto &t : targets) {
        for (auto it = roles.cbegin(); it != roles.cend(); ++it) {
            if (it.value().toString() != t.second) continue;
            QString devId;
            const bool unitKey = parseUnitKey(it.key(), &devId, nullptr);
            const QString owner = unitKey ? devId : it.key();
            if (owner == deviceId) continue;
            const DeviceFacts f = facts ? facts(owner) : DeviceFacts{};
            if (f.present) continue;
            doomed.append(it.key());
            if (unitKey)
                for (const QString &partner : { lowerArmKey(devId), palmKey(devId) })
                    if (roles.contains(partner)) doomed.append(partner);
        }
    }
    doomed.removeDuplicates();
    for (const QString &k : doomed) r.roles.remove(k);
    r.displaced = doomed;

    // The device's own previous claims go first, so a Witmotion moving from leadHand to
    // leadForearm does not keep both, and a wG3's bare pin does not outlive its unit keys.
    r.roles.remove(deviceId);
    r.roles.remove(lower);
    r.roles.remove(palm);
    for (const auto &t : targets) r.roles.insert(t.first, t.second);
    r.ok = true;
    return r;
}

HmMigration migrateBareHackMotion(const QVariantMap &roles, const QString &deviceId,
                                  const DeviceFactsFn &facts)
{
    HmMigration m;
    m.roles = roles;

    const QString lower = lowerArmKey(deviceId);
    const QString palm  = palmKey(deviceId);

    if (!roles.contains(deviceId)) {
        // Nothing keyed by the bare id. ⚠ "Already unit-keyed", "never assigned" and "a
        // coach deliberately cleared it" all land here and all are left alone: this
        // migrates, it never assigns — a migration that helpfully filled the arm roles
        // would silently undo a clearing on every restart.
        return m;
    }

    const QString bare = roles.value(deviceId).toString();
    if (roles.contains(lower) || roles.contains(palm) || bare.isEmpty()) {
        // The unit keys are the authority the moment they exist, and an empty value means
        // unassigned; either way the bare entry carries nothing worth keeping. This is
        // what makes a second run a no-op.
        m.roles.remove(deviceId);
        m.outcome = HmMigration::DroppedBare;
        return m;
    }

    if (bare != leadForearm()) {
        // ⚠ NOT REINTERPRETED. Phase A pinned a wG3 to slot A only; any other value came
        // from an older build or a hand edit, and no other pair of segments is one this
        // hardware can be in. Mapping it anyway is how a mirrored wrist angle ships.
        m.outcome  = HmMigration::Unmappable;
        m.bareRole = bare;
        return m;
    }

    // ⚠ THE PIN CLAIMED ONE ROLE; THE MIGRATION CLAIMS TWO, so it can collide on the
    // palm's even though the forearm is already this device's. Writing over another
    // sensor's leadHand would leave it claimed twice; refusing leaves the bare entry,
    // which the resolver reports as unresolvable — a dead end the coach can see and fix.
    const RoleResolution hand = resolveRole(roles, leadHand(), facts);
    const QString holder = hand.key.isEmpty() ? QString() : ownerOfKey(hand.key);
    if (!holder.isEmpty() && holder != deviceId) {
        m.outcome  = HmMigration::Blocked;
        m.holderId = holder;
        return m;
    }

    m.roles.insert(lower, leadForearm());
    m.roles.insert(palm,  leadHand());
    m.roles.remove(deviceId);
    m.outcome = HmMigration::Migrated;
    return m;
}

}   // namespace pinpoint::imu_roles
