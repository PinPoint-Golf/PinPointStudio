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

// Role-keyed IMU placement (session_wizard_refactor_design.md §4.13) — the pure logic
// ImuManager and AppSettings delegate to (imu_role_map.{h,cpp}), and AppSettings' own
// half of it against a scratch settings file.
//
// ⚠ THE ORACLES ARE THE CONTRACT, NOT THE CODE. The design says: the Wrist map migrates
// A/B/C to leadForearm/leadHand/leadUpperArm and drops D; the migration runs once, so a
// coach who unassigns everything is not re-assigned on the next launch; imu/placement is
// never written again; the ladder is present+enabled > present+disabled > any; one claim
// per role, with a present enabled holder refusing and an absent one displaced; a wG3
// holds the forearm AND the hand; trunk roles and a wG3 coexist (trunk_imu_design §6.8).
// Each case below is one of those sentences.
//
// ImuManager itself is not linked (it needs the BLE and device stack). What it adds on
// top of these functions is the device-facts callback, the live instances and logging.

#include "app/app_settings.h"
#include "imu/imu_role_map.h"

#include <QCoreApplication>
#include <QMetaProperty>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <cstdio>

using namespace pinpoint::imu_roles;

static int g_fail = 0;
static int g_run  = 0;

// Quiet on success: a FAIL line names the case, the summary counts them.
static void check(const QString &label, bool ok)
{
    ++g_run;
    if (!ok) {
        std::printf("  [FAIL] %s\n", qPrintable(label));
        ++g_fail;
    }
}

static QString show(const QVariantMap &m)
{
    QStringList parts;
    for (auto it = m.cbegin(); it != m.cend(); ++it)
        parts << it.key() + QLatin1Char('=') + it.value().toString();
    return QLatin1Char('{') + parts.join(QStringLiteral(", ")) + QLatin1Char('}');
}

static void checkMap(const QString &label, const QVariantMap &got, const QVariantMap &want)
{
    check(QStringLiteral("%1 (got %2, want %3)").arg(label, show(got), show(want)), got == want);
}

static void checkStr(const QString &label, const QString &got, const QString &want)
{
    check(QStringLiteral("%1 (got \"%2\", want \"%3\")").arg(label, got, want), got == want);
}

static void checkList(const QString &label, const QStringList &got, const QStringList &want)
{
    check(QStringLiteral("%1 (got [%2], want [%3])")
              .arg(label, got.join(QLatin1Char(',')), want.join(QLatin1Char(','))),
          got == want);
}

// ── A device table standing in for DeviceEnumerator + the session exclusion list ──

struct Table {
    QMap<QString, DeviceFacts> devs;
    Table &witmotion(const QString &id, bool enabled = true)
    {
        devs[id] = DeviceFacts{ true, enabled, false };
        return *this;
    }
    Table &hackmotion(const QString &id, bool enabled = true)
    {
        devs[id] = DeviceFacts{ true, enabled, true };
        return *this;
    }
    // Absent: unknown to the enumerator. Its vendor is unknowable, as in the app.
    DeviceFactsFn fn() const
    {
        const auto copy = devs;
        return [copy](const QString &id) { return copy.value(id, DeviceFacts{}); };
    }
};

static const QString kHm  = QStringLiteral("HM1");
static const QString kHmL = QStringLiteral("HM1#lowerArm");
static const QString kHmP = QStringLiteral("HM1#palm");

// ── 1. Names and the legacy letters ───────────────────────────────────────────────

static void testNames()
{
    checkList(QStringLiteral("N1 the five role names, in the fixed order"), placementRoleOrder(),
              { "pelvis", "thorax", "leadUpperArm", "leadForearm", "leadHand" });
    check(QStringLiteral("N2 a letter is not a role"), !isPlacementRole(QStringLiteral("A")));
    check(QStringLiteral("N3 a swing-file role name is not a placement role"),
          !isPlacementRole(QStringLiteral("LeadHand")));
    checkStr(QStringLiteral("N4 A → leadForearm"), roleForLegacySlot("A"), "leadForearm");
    checkStr(QStringLiteral("N5 B → leadHand"),    roleForLegacySlot("B"), "leadHand");
    checkStr(QStringLiteral("N6 C → leadUpperArm"), roleForLegacySlot("C"), "leadUpperArm");
    checkStr(QStringLiteral("N7 D has no role"),    roleForLegacySlot("D"), "");
    checkStr(QStringLiteral("N8 a trunk role has no letter"), legacySlotForRole("pelvis"), "");
    // segment_role.h: the persisted swing-file names are untouched, and both directions agree.
    using pinpoint::analysis::SegmentRole;
    checkStr(QStringLiteral("N9 segmentRoleName unchanged"),
             pinpoint::analysis::segmentRoleName(SegmentRole::LeadHand), "LeadHand");
    check(QStringLiteral("N10 placement name ↔ SegmentRole round trip"),
          pinpoint::analysis::segmentRoleForPlacementRole(
              pinpoint::analysis::placementRoleName(SegmentRole::Thorax)) == SegmentRole::Thorax);
    check(QStringLiteral("N11 T12 has no placement name"),
          pinpoint::analysis::placementRoleName(SegmentRole::T12).isEmpty());
    QString dev;
    int unit = -1;
    check(QStringLiteral("N12 a unit key parses"),
          parseUnitKey(kHmP, &dev, &unit) && dev == kHm && unit == 1);
    check(QStringLiteral("N13 a bare key does not"), !parseUnitKey(kHm, &dev, &unit));
    checkStr(QStringLiteral("N14 owner of a unit key"), ownerOfKey(kHmL), kHm);
}

// ── 2. The migration imu/placement → imu/roles ───────────────────────────────────

static void testMigration()
{
    // A typical two-sensor Wrist setup.
    {
        const RolesLoad r = loadRoles(false, {}, { { "W1", "A" }, { "W2", "B" } });
        check(QStringLiteral("M1 never migrated → migrates"), r.migrated);
        checkMap(QStringLiteral("M1 two Witmotions"), r.roles,
                 { { "W1", "leadForearm" }, { "W2", "leadHand" } });
        check(QStringLiteral("M1 one log line per entry"), r.log.size() == 2);
    }
    // Three, with the upper arm.
    {
        const RolesLoad r = loadRoles(false, {}, { { "W1", "A" }, { "W2", "B" }, { "W3", "C" } });
        checkMap(QStringLiteral("M2 three Witmotions"), r.roles,
                 { { "W1", "leadForearm" }, { "W2", "leadHand" }, { "W3", "leadUpperArm" } });
    }
    // A wG3 already on unit keys: carried across verbatim.
    {
        const RolesLoad r = loadRoles(false, {}, { { kHmL, "A" }, { kHmP, "B" } });
        checkMap(QStringLiteral("M3 wG3 unit keys"), r.roles,
                 { { kHmL, "leadForearm" }, { kHmP, "leadHand" } });
    }
    // A wG3 still on Phase A's bare pin: carried as the bare key, then split once the
    // enumerator says it is a HackMotion.
    {
        const RolesLoad r = loadRoles(false, {}, { { kHm, "A" } });
        checkMap(QStringLiteral("M4a bare wG3 pin carried"), r.roles, { { kHm, "leadForearm" } });
        const DeviceFactsFn f = Table().hackmotion(kHm).fn();
        const HmMigration m = migrateBareHackMotion(r.roles, kHm, f);
        check(QStringLiteral("M4b bare wG3 pin split"), m.outcome == HmMigration::Migrated);
        checkMap(QStringLiteral("M4b both unit keys"), m.roles,
                 { { kHmL, "leadForearm" }, { kHmP, "leadHand" } });
        const HmMigration again = migrateBareHackMotion(m.roles, kHm, f);
        check(QStringLiteral("M4c the split twice is a no-op"),
              again.outcome == HmMigration::NoChange && again.roles == m.roles);
    }
    // D and "other" have no role.
    {
        const RolesLoad r = loadRoles(false, {}, { { "W1", "A" }, { "W4", "D" }, { "W5", "other" } });
        checkMap(QStringLiteral("M5 D and other dropped"), r.roles, { { "W1", "leadForearm" } });
        check(QStringLiteral("M5 the drops are logged too"), r.log.size() == 3);
    }
    // Run twice: the second run reads what the first stored, and is the same.
    {
        const QVariantMap legacy{ { "W1", "A" }, { "W2", "B" } };
        const RolesLoad first  = loadRoles(false, {}, legacy);
        const RolesLoad second = loadRoles(true, first.roles, legacy);
        check(QStringLiteral("M6 second run does not migrate"), !second.migrated);
        checkMap(QStringLiteral("M6 second run = first"), second.roles, first.roles);
        checkMap(QStringLiteral("M6 a re-run of the migration itself is the same"),
                 loadRoles(false, {}, legacy).roles, first.roles);
    }
    // Empty after the coach unassigned everything: the marker, not the map, decides.
    {
        const RolesLoad r = loadRoles(true, {}, { { "W1", "A" }, { "W2", "B" } });
        check(QStringLiteral("M7 empty-after-unassign does not re-migrate"), !r.migrated);
        checkMap(QStringLiteral("M7 roles stay empty"), r.roles, {});
    }
    // HackMotion migration edge cases.
    {
        const DeviceFactsFn f = Table().hackmotion(kHm).witmotion("W2").fn();
        const HmMigration blocked =
            migrateBareHackMotion({ { kHm, "leadForearm" }, { "W2", "leadHand" } }, kHm, f);
        check(QStringLiteral("M8 bare wG3 blocked by a leadHand holder"),
              blocked.outcome == HmMigration::Blocked && blocked.holderId == "W2");
        const HmMigration odd = migrateBareHackMotion({ { kHm, "leadHand" } }, kHm, f);
        check(QStringLiteral("M9 a bare wG3 on another role is not reinterpreted"),
              odd.outcome == HmMigration::Unmappable && odd.roles.value(kHm) == "leadHand");
        const HmMigration dropped = migrateBareHackMotion(
            { { kHm, "leadForearm" }, { kHmL, "leadForearm" }, { kHmP, "leadHand" } }, kHm, f);
        check(QStringLiteral("M10 a bare key beside unit keys is dropped"),
              dropped.outcome == HmMigration::DroppedBare && !dropped.roles.contains(kHm)
                  && dropped.roles.size() == 2);
    }
}

// ── 3. The derived legacy view ───────────────────────────────────────────────────

static void testDerivedView()
{
    for (const QVariantMap &legacy : {
             QVariantMap{ { "W1", "A" }, { "W2", "B" } },
             QVariantMap{ { "W1", "A" }, { "W2", "B" }, { "W3", "C" } },
             QVariantMap{ { kHmL, "A" }, { kHmP, "B" }, { "W3", "C" } } }) {
        checkMap(QStringLiteral("V1 derived imuPlacement = the legacy map (arm-only)"),
                 placementFromRoles(loadRoles(false, {}, legacy).roles), legacy);
    }
    checkMap(QStringLiteral("V2 trunk roles produce no letter"),
             placementFromRoles({ { "W1", "pelvis" }, { "W2", "thorax" }, { "W3", "leadUpperArm" } }),
             { { "W3", "C" } });
    // (V3/V4 checked the legacy WRITE translation, rolesWithLegacyPlacement; the write path
    // went at Stage 5c — imuPlacement is read-only.)
}

// ── 3b. A sensor's name, never a raw id (Stage 5d) ──────────────────────────────

static void testDisplayNames()
{
    const QString id = QStringLiteral("{767a6a67-14b5-1705-bb58-9ebea6e89014}");
    const QString hmKey = aliasKey(QStringLiteral("HackMotion wG3"), id);
    checkStr(QStringLiteral("DN0 the alias key is description|id"), hmKey,
             QStringLiteral("HackMotion wG3|") + id);
    // 1. An alias the coach gave — found for an ABSENT sensor by its key's id half.
    checkStr(QStringLiteral("DN1 absent: the coach's alias"),
             sensorDisplayName(id, QString(), { { hmKey, QStringLiteral("Lead arm wG3") } }, {}),
             QStringLiteral("Lead arm wG3"));
    // The seeded alias ("<description> <id>") is not a name: the description stands in.
    checkStr(QStringLiteral("DN2 absent: the seeded alias falls back to the description"),
             sensorDisplayName(id, QString(), { { hmKey, QStringLiteral("HackMotion wG3 ") + id } }, {}),
             QStringLiteral("HackMotion wG3"));
    // 3. No alias remembered, but the roles hold its unit keys: the vendor's name.
    checkStr(QStringLiteral("DN3 absent: a wG3 unit key names the vendor"),
             sensorDisplayName(id, QString(), {}, { { id + QStringLiteral("#lowerArm"), QStringLiteral("leadForearm") },
                                                     { id + QStringLiteral("#palm"), QStringLiteral("leadHand") } }),
             QStringLiteral("HackMotion wG3"));
    // 4. Nothing remembered.
    checkStr(QStringLiteral("DN4 absent: nothing remembered → \"sensor\""),
             sensorDisplayName(id, QString(), {}, { { id, QStringLiteral("leadForearm") } }), QStringLiteral("sensor"));
    // Present: the alias under its exact key, else the description; ids stripped either way.
    checkStr(QStringLiteral("DN5 present: the description when the alias is the seeded one"),
             sensorDisplayName(id, QStringLiteral("WT901BLE68"),
                               { { aliasKey(QStringLiteral("WT901BLE68"), id), QStringLiteral("WT901BLE68 ") + id } }, {}),
             QStringLiteral("WT901BLE68"));
    checkStr(QStringLiteral("DN6 an id inside an alias is stripped"),
             sensorDisplayName(id, QStringLiteral("WT901BLE68"),
                               { { aliasKey(QStringLiteral("WT901BLE68"), id), QStringLiteral("Hand ") + id } }, {}),
             QStringLiteral("Hand"));
}

// ── 4. The owner ladder ─────────────────────────────────────────────────────────

static void testLadder()
{
    // X absent, Y present but disabled, Z present and enabled — all on the forearm.
    const QVariantMap roles{ { "X", "leadForearm" }, { "Y", "leadForearm" }, { "Z", "leadForearm" } };
    const DeviceFactsFn all = Table().witmotion("Y", false).witmotion("Z").fn();
    checkStr(QStringLiteral("L1 present+enabled wins"), resolveRole(roles, "leadForearm", all).key, "Z");
    checkStr(QStringLiteral("L1 roleHolder names it"), roleHolder(roles, "leadForearm", all), "Z");

    const DeviceFactsFn noZ = Table().witmotion("Y", false).fn();
    checkStr(QStringLiteral("L2 then present+disabled"), resolveRole(roles, "leadForearm", noZ).key, "Y");
    checkStr(QStringLiteral("L2 a disabled holder is not the session holder"),
             roleHolder(roles, "leadForearm", noZ), "");

    checkStr(QStringLiteral("L3 then any claimant"),
             resolveRole(roles, "leadForearm", Table().fn()).key, "X");
    checkStr(QStringLiteral("L4 an unheld role resolves to nothing"),
             resolveRole(roles, "pelvis", all).key, "");
    checkStr(QStringLiteral("L5 the empty role resolves to nothing"),
             resolveRole(roles, "", all).key, "");

    // Two enabled, present claimants: deterministic by key order, and reported.
    const RoleResolution two = resolveRole({ { "B2", "leadHand" }, { "A1", "leadHand" } }, "leadHand",
                                           Table().witmotion("A1").witmotion("B2").fn());
    check(QStringLiteral("L6 a double claim is reported (both keys) and key order decides"),
          two.enabledKeys == QStringList{ "A1", "B2" } && two.key == "A1");

    // A wG3 unit key resolves through its owner's facts.
    const QVariantMap hm{ { kHmL, "leadForearm" }, { kHmP, "leadHand" } };
    checkStr(QStringLiteral("L7 a wG3 holds the hand through its palm key"),
             resolveRole(hm, "leadHand", Table().hackmotion(kHm).fn()).key, kHmP);
}

// ── 5. One claim per role ────────────────────────────────────────────────────────

static void testClaims()
{
    // Refusal by a present, enabled holder, with the holder named.
    {
        const QVariantMap roles{ { "W1", "leadForearm" } };
        const ClaimResult c = claimRole(roles, "W2", "leadForearm",
                                        Table().witmotion("W1").witmotion("W2").fn());
        check(QStringLiteral("C1 refused when a present enabled device holds the role"), !c.ok);
        checkStr(QStringLiteral("C1 the holder is named"), c.holderId, "W1");
        checkStr(QStringLiteral("C1 the role is named"), c.refusedRole, "leadForearm");
        checkMap(QStringLiteral("C1 nothing changed"), c.roles, roles);
    }
    // An absent holder is displaced.
    {
        const ClaimResult c = claimRole({ { "GONE", "leadForearm" }, { "W3", "leadHand" } }, "W2",
                                        "leadForearm", Table().witmotion("W2").witmotion("W3").fn());
        check(QStringLiteral("C2 an absent holder is displaced"), c.ok);
        checkMap(QStringLiteral("C2 the new map"), c.roles,
                 { { "W2", "leadForearm" }, { "W3", "leadHand" } });
        checkList(QStringLiteral("C2 the displaced key is reported"), c.displaced, { "GONE" });
    }
    // Displacing an absent wG3 strips both of its unit keys.
    {
        const ClaimResult c = claimRole({ { "OLD#lowerArm", "leadForearm" }, { "OLD#palm", "leadHand" } },
                                        "W2", "leadHand", Table().witmotion("W2").fn());
        checkMap(QStringLiteral("C3 an absent wG3 loses both units"), c.roles, { { "W2", "leadHand" } });
    }
    // A present but DISABLED holder is a parked setup: both claims stand.
    {
        const ClaimResult c = claimRole({ { kHmL, "leadForearm" }, { kHmP, "leadHand" } }, "W1",
                                        "leadForearm", Table().hackmotion(kHm, false).witmotion("W1").fn());
        check(QStringLiteral("C4 a disabled holder does not refuse"), c.ok);
        check(QStringLiteral("C4 and keeps its parked claim"),
              c.roles.value(kHmL) == "leadForearm" && c.roles.value("W1") == "leadForearm");
    }
    // A wG3 takes both arm roles, and drops its bare pin.
    {
        const ClaimResult c = claimRole({ { kHm, "leadForearm" } }, kHm, "leadForearm",
                                        Table().hackmotion(kHm).fn());
        check(QStringLiteral("C5 a wG3 claims leadForearm"), c.ok);
        checkMap(QStringLiteral("C5 …and holds both arm roles"), c.roles,
                 { { kHmL, "leadForearm" }, { kHmP, "leadHand" } });
        checkStr(QStringLiteral("C5 roleForDevice answers leadForearm"), roleForDevice(c.roles, kHm),
                 "leadForearm");
        const ClaimResult bad = claimRole({}, kHm, "leadHand", Table().hackmotion(kHm).fn());
        check(QStringLiteral("C6 a wG3 cannot be put on leadHand alone"), !bad.ok && !bad.error.isEmpty());
        const ClaimResult trunk = claimRole({}, kHm, "pelvis", Table().hackmotion(kHm).fn());
        check(QStringLiteral("C6 …nor on the trunk"), !trunk.ok && !trunk.error.isEmpty());
    }
    // A wG3 holding the arm blocks a Witmotion from the hand.
    {
        const QVariantMap roles{ { kHmL, "leadForearm" }, { kHmP, "leadHand" } };
        const ClaimResult c = claimRole(roles, "W1", "leadHand",
                                        Table().hackmotion(kHm).witmotion("W1").fn());
        check(QStringLiteral("C7 wG3 blocks a Witmotion from leadHand"), !c.ok && c.holderId == kHm
                                                                              && c.refusedRole == "leadHand");
    }
    // A Witmotion on the forearm blocks the wG3; one on the hand blocks it on the palm half.
    {
        const ClaimResult fore = claimRole({ { "W1", "leadForearm" } }, kHm, "leadForearm",
                                           Table().hackmotion(kHm).witmotion("W1").fn());
        check(QStringLiteral("C8 Witmotion on leadForearm blocks the wG3"),
              !fore.ok && fore.holderId == "W1" && fore.refusedRole == "leadForearm");
        const ClaimResult hand = claimRole({ { "W2", "leadHand" } }, kHm, "leadForearm",
                                           Table().hackmotion(kHm).witmotion("W2").fn());
        check(QStringLiteral("C9 Witmotion on leadHand blocks the wG3 (palm half named)"),
              !hand.ok && hand.holderId == "W2" && hand.refusedRole == "leadHand");
    }
    // Trunk roles coexist with a wG3 (trunk_imu_design §6.8: roles disjoint, allowed).
    {
        const DeviceFactsFn f = Table().hackmotion(kHm).witmotion("P").witmotion("T").fn();
        QVariantMap roles;
        ClaimResult c = claimRole(roles, kHm, "leadForearm", f);
        roles = c.roles;
        c = claimRole(roles, "P", "pelvis", f);
        check(QStringLiteral("C10 pelvis beside a wG3"), c.ok);
        roles = c.roles;
        c = claimRole(roles, "T", "thorax", f);
        check(QStringLiteral("C10 thorax beside a wG3"), c.ok);
        roles = c.roles;
        checkList(QStringLiteral("C10 rolesInSession, in the fixed order"), rolesInSession(roles, f),
                  { "pelvis", "thorax", "leadForearm", "leadHand" });
    }
    // Moving, re-claiming, unassigning.
    {
        const DeviceFactsFn f = Table().witmotion("W1").fn();
        checkMap(QStringLiteral("C11 a Witmotion moving roles keeps one"),
                 claimRole({ { "W1", "leadHand" } }, "W1", "leadForearm", f).roles,
                 { { "W1", "leadForearm" } });
        check(QStringLiteral("C12 re-claiming one's own role is fine"),
              claimRole({ { "W1", "leadForearm" } }, "W1", "leadForearm", f).ok);
        checkMap(QStringLiteral("C13 \"\" unassigns a wG3 entirely"),
                 claimRole({ { kHmL, "leadForearm" }, { kHmP, "leadHand" }, { kHm, "leadForearm" },
                             { "W1", "pelvis" } }, kHm, "", f).roles,
                 { { "W1", "pelvis" } });
        check(QStringLiteral("C14 an unknown role is a caller error"),
              !claimRole({}, "W1", "D", f).ok);
        checkStr(QStringLiteral("C15 an unassigned device has no role"), roleForDevice({}, "W1"), "");
    }
}

// ── 6. rolesInSession ────────────────────────────────────────────────────────────

static void testRolesInSession()
{
    const QVariantMap roles{ { "W3", "leadUpperArm" }, { "W1", "leadForearm" }, { "W2", "leadHand" },
                             { "T", "thorax" }, { "P", "pelvis" }, { "GONE", "pelvis" } };
    checkList(QStringLiteral("S1 fixed order, whatever the key order"),
              rolesInSession(roles, Table().witmotion("W1").witmotion("W2").witmotion("W3")
                                        .witmotion("T").witmotion("P").fn()),
              { "pelvis", "thorax", "leadUpperArm", "leadForearm", "leadHand" });
    checkList(QStringLiteral("S2 disabled and absent holders are not in session"),
              rolesInSession(roles, Table().witmotion("W1").witmotion("W2").witmotion("W3", false)
                                        .witmotion("T", false).fn()),
              { "leadForearm", "leadHand" });
    checkList(QStringLiteral("S3 nothing assigned → empty"), rolesInSession({}, Table().fn()), {});
}

// ── 7. AppSettings: the marker, the stored keys, the derived view, the signals ─────

static void testAppSettings()
{
    QTemporaryDir scratch;
    // ⚠ BEFORE the first AppSettings: its constructor reads (and the migration writes) the
    // settings file, and the default path is the user's real one.
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, scratch.path());

    const QVariantMap legacy{ { "W1", "A" }, { "W2", "B" }, { "W4", "D" } };
    ppSettings().setValue(QStringLiteral("imu/placement"), legacy);

    {
        AppSettings s;
        checkMap(QStringLiteral("A1 first load migrates imu/placement"), s.imuRoles(),
                 { { "W1", "leadForearm" }, { "W2", "leadHand" } });
        checkMap(QStringLiteral("A1 derived imuPlacement"), s.imuPlacement(),
                 { { "W1", "A" }, { "W2", "B" } });
        check(QStringLiteral("A1 the marker is written"),
              ppSettings().value(QStringLiteral("imu/rolesMigrated")).toBool());
        checkMap(QStringLiteral("A1 imu/placement on disk is untouched"),
                 ppSettings().value(QStringLiteral("imu/placement")).toMap(), legacy);

        QSignalSpy roles(&s, &AppSettings::imuRolesChanged);
        QSignalSpy placement(&s, &AppSettings::imuPlacementChanged);
        s.setImuRoles({ { "W1", "leadForearm" }, { "W2", "leadHand" }, { "P", "pelvis" } });
        check(QStringLiteral("A2 a trunk-only change still notifies imuPlacement"),
              roles.count() == 1 && placement.count() == 1);
        s.setImuRoles(s.imuRoles());
        check(QStringLiteral("A3 an unchanged write notifies nothing"),
              roles.count() == 1 && placement.count() == 1);

        // A4: imuPlacement is READ-ONLY (Stage 5c): its property has no WRITE, and a roles
        // write moves the derived view without touching imu/placement on disk.
        const int pi = s.metaObject()->indexOfProperty("imuPlacement");
        check(QStringLiteral("A4 imuPlacement is a read-only property"),
              pi >= 0 && !s.metaObject()->property(pi).isWritable());
        s.setImuRoles({ { "P", "pelvis" }, { "W2", "leadForearm" } });
        checkMap(QStringLiteral("A4 the derived view follows the roles"), s.imuPlacement(),
                 { { "W2", "A" } });
        checkMap(QStringLiteral("A4 imu/placement on disk is still untouched"),
                 ppSettings().value(QStringLiteral("imu/placement")).toMap(), legacy);

        s.setImuRoles({});   // the coach unassigns everything
    }
    {
        AppSettings again;
        checkMap(QStringLiteral("A5 empty-after-unassign survives a restart (no re-migration)"),
                 again.imuRoles(), {});
        checkMap(QStringLiteral("A5 and the derived view is empty"), again.imuPlacement(), {});
    }
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    std::printf("imu_role_map_test — role-keyed placement (design §4.13)\n");
    testNames();
    testMigration();
    testDerivedView();
    testDisplayNames();
    testLadder();
    testClaims();
    testRolesInSession();
    testAppSettings();
    std::printf("%d checks, %d failed\n", g_run, g_fail);
    return g_fail == 0 ? 0 : 1;
}
