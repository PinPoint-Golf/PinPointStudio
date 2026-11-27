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

// imu_rig_link_test: the sensor manager's link to the Rig (src/Gui/imu/imu_rig_link.*; rig_design.md §4.1, §4.3;
// rig_design_addendum.md decisions 1 and 8) and app.rig's sensor half (src/Gui/app/rig_presenter.*), the rules
// ImuManager follows once the Rig writes the sensors' set-up (platform refactor C-6b.1), tested without a sensor.
// IsolatedStores is made first (#187): a temporary settings file and PINPOINT_RIG_DIR; the Rig runs over an in-memory
// repository, or over its real store in the temporary folder (I6, I7).
//
//   I1  the hardware facts a claim needs, as the Rig's table: a listed sensor is present, with this session's switch and
//       its vendor; an unlisted one reads as SensorFactsTable::unknownDevice (ImuManager::deviceFacts' answer)
//   I2  a sensor's alias and the disabled list, read from the two parts by their own spellings: two descriptions of
//       one id are two aliases; a camera's alias or flag is not a sensor's
//   I3  a sensor's enabled flag changed (decision 8's row): newly disabled ones off for the session, re-enabled ones on,
//       an unchanged one keeps the session's own choice
//   I4  the writers as requests, through the real Rig: a claim, refused by a present enabled holder (named, the role
//       named, for a HackMotion possibly the hand), "" releasing, a text that is no placement role a caller error; L1
//       PINNED: a claim displaces an absent holder's claim, both lanes of an absent HackMotion; the alias trimmed, an
//       empty one removed; the alias seeded only when none; the Enable switch; each answer equal to today's rule's
//   I5  the two automatic writers (RR8, RR9), through the real Rig: for every case of today's rules, the Rig's roles
//       after the write equal the map the rule decided; a found HackMotion taking the arm leaves an absent Witmotion's
//       claim standing (the real facts would have displaced it: why the sparing table exists); L2 PINNED: DroppedBare
//       drops only the bare pin; a map no claim can make (a bare pin beside a half pair) is not written at all;
//       E3: a "" on a HackMotion's bare id stops nothing, today or after (the migration drops it before the arm claim)
//   I6  app.rig's sensor half over the real Rig: the roles' values, the placement pairs, the disabled ids, the alias
//       spellings and an alias; the Enable switch; each notifier once per change of its own kind, never for a camera's
//       change or an unchanged claim (A2, A3 re-pointed from imu_role_map_test: a trunk-only change notifies the
//       placement view's notifier; an unchanged write notifies nothing); placementPairs read-only (A4)
//   I7  A1, A5 re-pointed from imu_role_map_test (their subject, AppSettings, holds no sensor key now): a machine where
//       the imu/placement move never ran, through the Rig's own copy at launch: the roles and the derived view as the
//       move gives them, imu/placement untouched, and NOTHING written to settings (neither imu/roles nor the marker,
//       which AppSettings used to write: the Rig's copy is the one reader); everything released, then a relaunch:
//       empty, no copy again

#include "../../Core/tests/isolated_stores.h"

#include "../app/rig_presenter.h"
#include "../imu/imu_rig_link.h"
#include "../../Core/pp_settings.h"
#include "../../DomainModel/rig.h"
#include "../../IMU/imu_role_map.h"
#include "../../Stores/rig_store.h"

#include <QCoreApplication>
#include <QFile>
#include <QSettings>
#include <QMetaProperty>

#include <cstdio>

using namespace pinpoint;
using namespace pinpoint::rig;
using namespace pp_imurig;
namespace ir = pinpoint::imu_roles;

namespace {

int  g_fail = 0;
void check(bool c, const QString &label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", qPrintable(label));
    if (!c) ++g_fail;
}
QString dump(const QVariantMap &m)
{
    QStringList out;
    for (auto it = m.cbegin(); it != m.cend(); ++it) out << it.key() + "=" + it.value().toString();
    return "{" + out.join(", ") + "}";
}

class MemoryRepository final : public RigRepository
{
public:
    RigLoad load() override { return {}; }
    bool    save(const RigId &, const RigParts &) override { ++saves; return true; }
    int     saves = 0;
};

const QString kHm  = QStringLiteral("{767a6a67-14b5-1705-bb58-9ebea6e89014}");
const QString kHmL = kHm + QStringLiteral("#lowerArm");
const QString kHmP = kHm + QStringLiteral("#palm");

// An empty Rig over memory.
struct TestRig {
    MemoryRepository repo;
    Rig              rig{ repo };
    TestRig() { rig.load(); }
};

// A repository whose load hands the Rig a state holding `roles` as its sensors (the state a copy from settings gives).
class SeededRepository final : public RigRepository
{
public:
    explicit SeededRepository(const QVariantMap &roles)
    {
        for (auto it = roles.cbegin(); it != roles.cend(); ++it) {
            SensorSetup x;
            x.key  = PlacementKey::fromPersisted(it.key());
            x.role = analysis::segmentRoleForPlacementRole(it.value().toString());
            m_parts.state.sensors.append(x);
        }
    }
    RigLoad load() override { RigLoad l; l.parts = m_parts; return l; }
    bool    save(const RigId &, const RigParts &) override { ++saves; return true; }
    int     saves = 0;

private:
    RigParts m_parts;
};
QVariantMap rolesOf(const Rig &rig) { return ir::rolesMapOf(rig.parts().state); }

// Today's facts function over a table, as ImuManager::deviceFacts answers.
ir::DeviceFactsFn fnOf(const SensorFactsTable &t)
{
    return [t](const QString &id) { return t.factsFor(id); };
}

} // namespace

int main(int argc, char **argv)
{
    pinpoint::test::IsolatedStores stores;
    QCoreApplication app(argc, argv);
    std::printf("imu_rig_link_test — the temporary directory: %s\n", qPrintable(stores.path()));

    std::printf("\nI1 the hardware facts, as the Rig's table\n");
    {
        const SensorFactsTable t = factsTable({ { "W1", true, false }, { "W2", false, false }, { kHm, true, true } });
        check(t.factsFor("W1") == SensorFacts{ true, true, false } && t.factsFor("W2") == SensorFacts{ true, false, false }
                  && t.factsFor(kHm) == SensorFacts{ true, true, true },
              "I1a a listed sensor is present, with this session's switch and its vendor");
        check(t.factsFor("GONE") == SensorFactsTable::unknownDevice() && !t.factsFor("GONE").present,
              "I1b a sensor the enumerator does not list is not present (today's deviceFacts)");
    }

    std::printf("\nI2 a sensor's alias and the disabled list, by their own spellings\n");
    {
        RigParts p;
        const DeviceKey w1a = DeviceKey::sensor("WT901BLE68", "W1"), w1b = DeviceKey::sensor("WT901BLE67", "W1");
        const DeviceKey cam = DeviceKey::camera("Blackfly", "123", "usb-1");
        p.other.aliases = { { w1a, "Hip" }, { w1b, "Old hip" }, { cam, "Face on" } };
        p.other.enabledOnThisMachine = { { DeviceKey::fromPersistedId(QStringLiteral("W2")), false }, { DeviceKey::fromPersistedId(QStringLiteral("W3")), true },
                                         { cam, false } };
        check(aliasOf(p, "WT901BLE68|W1") == "Hip" && aliasOf(p, "WT901BLE67|W1") == "Old hip" && aliasOf(p, "|W1").isEmpty(),
              "I2a an alias by its spelling: two descriptions of one id are two aliases (decision 9)");
        check(aliasMapOf(p) == QVariantMap{ { "WT901BLE68|W1", "Hip" }, { "WT901BLE67|W1", "Old hip" } },
              "I2b the aliases as today's imu/alias map: a camera's is not a sensor's");
        check(disabledOf(p) == QStringList{ "W2" }, "I2c the disabled sensors by bare id: an enabled one and a camera left out");
    }

    std::printf("\nI3 a sensor's enabled flag changed (decision 8)\n");
    {
        const EnabledDiff d = enabledDiff({ "W1", "W2" }, { "W2", "W3" });
        check(d.switchOff == QStringList{ "W3" } && d.switchOn == QStringList{ "W1" },
              "I3a newly disabled: off for the session; re-enabled: on; W2, unchanged, keeps the session's own choice");
        const EnabledDiff none = enabledDiff({ "W1" }, { "W1" });
        check(none.switchOff.isEmpty() && none.switchOn.isEmpty(), "I3b no change: nothing");
    }

    std::printf("\nI4 the writers as requests, through the real Rig\n");
    {
        TestRig t;
        Rig &rig = t.rig;
        const SensorFactsTable facts = factsTable({ { "W1", true, false }, { "W2", true, false }, { kHm, true, true } });
        int events = 0;
        QObject::connect(&rig, &Rig::rigChanged, &rig, [&] { ++events; });
        const RoleClaim a = requestRole(rig, "W1", "leadForearm", facts);
        check(a.ok && rolesOf(rig) == QVariantMap{ { "W1", "leadForearm" } } && events == 1,
              "I4a a claim: one command, one new state, one RigChanged");
        // A HackMotion asking for the arm while W1, present and enabled, holds the forearm: refused, as today.
        const QVariantMap before = rolesOf(rig);
        const ir::ClaimResult today = ir::claimRole(before, kHm, "leadForearm", fnOf(facts));
        const RoleClaim r = requestRole(rig, kHm, "leadForearm", facts);
        check(!r.ok && r.holder.persistedId() == "W1" && analysis::placementRoleName(r.refusedRole) == "leadForearm"
                  && !today.ok && today.holderId == "W1" && today.refusedRole == "leadForearm"
                  && rolesOf(rig) == before && events == 1,
              "I4b refused: the holder and the role named, as today's rule names them; nothing changed, no event");
        requestRole(rig, "W2", "leadHand", facts);
        // W1 now absent (not listed): the HackMotion is refused only by W2 on the hand.
        const SensorFactsTable w1gone = factsTable({ { "W2", true, false }, { kHm, true, true } });
        const RoleClaim rp = requestRole(rig, kHm, "leadForearm", w1gone);
        check(!rp.ok && rp.holder.persistedId() == "W2" && rp.refusedRole == analysis::SegmentRole::LeadHand,
              "I4c a HackMotion refused on its palm's role: the hand named (the cable's half that collided)");
        const RoleClaim rel = requestRole(rig, "W2", "", facts);
        check(rel.ok && rolesOf(rig) == QVariantMap{ { "W1", "leadForearm" } }, "I4d \"\" releases every claim the device holds");
        const QVariantMap beforeL1 = rolesOf(rig);
        const ir::ClaimResult todayL1 = ir::claimRole(beforeL1, kHm, "leadForearm", fnOf(w1gone));
        const RoleClaim l1 = requestRole(rig, kHm, "leadForearm", w1gone);
        QStringList displaced;
        for (const PlacementKey &k : l1.displaced) displaced << k.persisted();
        check(l1.ok && displaced == QStringList{ "W1" } && todayL1.displaced == QStringList{ "W1" }
                  && rolesOf(rig) == todayL1.roles && rolesOf(rig) == QVariantMap{ { kHmL, "leadForearm" }, { kHmP, "leadHand" } },
              "I4e L1 pinned: a claim displaces an ABSENT holder's claim, as today's rule does (W1 not listed)");
        // And both lanes of an absent HackMotion go when a Witmotion claims one of its roles.
        const SensorFactsTable hmGone = factsTable({ { "W1", true, false } });
        const ir::ClaimResult todayL1b = ir::claimRole(rolesOf(rig), "W1", "leadHand", fnOf(hmGone));
        const RoleClaim l1b = requestRole(rig, "W1", "leadHand", hmGone);
        check(l1b.ok && l1b.displaced.size() == 2 && rolesOf(rig) == todayL1b.roles && rolesOf(rig) == QVariantMap{ { "W1", "leadHand" } },
              "I4f L1 pinned: both lanes of an absent HackMotion go together");
        const QVariantMap keep = rolesOf(rig);
        const RoleClaim bad = requestRole(rig, "W2", "trailForearm", facts);
        const RoleClaim hmBad = requestRole(rig, kHm, "pelvis", facts);
        check(!bad.ok && !bad.error.isEmpty() && !hmBad.ok && !hmBad.error.isEmpty() && rolesOf(rig) == keep,
              "I4g a text that is no placement role, or a HackMotion asked for the trunk: a caller error, nothing changed");

        // The alias and the Enable switch (RigCameraCommands declares them; any device's key).
        int labels = 0;
        QObject::connect(&rig, &Rig::rigDeviceLabelChanged, &rig, [&] { ++labels; });
        requestAlias(rig, "WT901BLE68|W1", "  Hip sensor  ");
        check(aliasOf(rig.parts(), "WT901BLE68|W1") == "Hip sensor" && labels == 1, "I4h the alias trimmed, one label event");
        requestAlias(rig, "WT901BLE68|W1", "   ");
        check(aliasOf(rig.parts(), "WT901BLE68|W1").isEmpty() && labels == 2, "I4i an empty alias removes it");
        const DeviceKey w3 = DeviceKey::sensor("WT901BLE68", "W3");
        const bool seeded = requestAliasSeed(rig, rig.parts(), w3);
        const bool again  = requestAliasSeed(rig, rig.parts(), w3);
        check(seeded && !again && aliasOf(rig.parts(), "WT901BLE68|W3") == "WT901BLE68 W3" && labels == 3,
              "I4j a found sensor with no alias gets its key with the '|' a space, once");
        requestEnabled(rig, "W3", false);
        check(disabledOf(rig.parts()) == QStringList{ "W3" } && labels == 4, "I4k the Enable switch: W3 off on this machine");
        requestEnabled(rig, "W3", true);
        check(disabledOf(rig.parts()).isEmpty() && labels == 5, "I4l ... and on again (enabled is the default, not stored)");
    }

    std::printf("\nI5 the two automatic writers, through the real Rig\n");
    {
        // A case: the roles the Rig holds, the facts, the HackMotion. Today's rule decides; the Rig must then hold
        // exactly the map it decided.
        struct Case {
            const char *label;
            QVariantMap roles;
            SensorFactsTable facts;
        };
        const SensorFactsTable hmOnly = factsTable({ { kHm, true, true } });
        const SensorFactsTable hmAndW = factsTable({ { kHm, true, true }, { "W1", true, false } });
        const SensorFactsTable hmAndWoff = factsTable({ { kHm, true, true }, { "W1", false, false } });

        // Each case on a fresh Rig whose state holds `roles`; today's rule decides over the Rig's roles; the Rig must then
        // hold exactly the map the rule decided (or the roles as they were, when the rule writes nothing).
        auto run = [&](const Case &c, bool rr9) {
            SeededRepository repo(c.roles);
            Rig rig(repo);
            rig.load();
            const QVariantMap before = rolesOf(rig);
            QVariantMap decided = before;
            bool writes = false;
            if (rr9) {
                const ir::HmArmClaim today = ir::claimArmForUnassignedHackMotion(before, kHm, fnOf(c.facts));
                writes = today.outcome == ir::HmArmClaim::Claimed;
                if (writes) decided = today.roles;
            } else {
                const ir::HmMigration today = ir::migrateBareHackMotion(before, kHm, fnOf(c.facts));
                writes = today.outcome == ir::HmMigration::Migrated || today.outcome == ir::HmMigration::DroppedBare;
                if (writes) decided = today.roles;
            }
            const bool wrote = writes && requestHackMotionMap(rig, before, kHm, decided, c.facts);
            const QVariantMap after = rolesOf(rig);
            const bool same = before == c.roles && after == decided && wrote == writes;
            if (!same) std::printf("    rule %s\n     Rig %s\n", qPrintable(dump(decided)), qPrintable(dump(after)));
            check(same, QStringLiteral("I5 %1 %2: the Rig holds today's map").arg(rr9 ? "RR9" : "RR8", QLatin1String(c.label)));
        };

        std::printf("  RR9: a found HackMotion with no mount takes the lead arm\n");
        for (const Case &c : QList<Case>{
                 { "nothing held", {}, hmOnly },
                 { "an ABSENT Witmotion holds the forearm (found first: its claim stands)", { { "W1", "leadForearm" } }, hmOnly },
                 { "an absent Witmotion holds the hand", { { "W1", "leadHand" } }, hmOnly },
                 { "a PARKED Witmotion (present, switched off) holds the forearm", { { "W1", "leadForearm" } }, hmAndWoff },
                 { "a live Witmotion holds the forearm (Held: nothing written)", { { "W1", "leadForearm" } }, hmAndW },
                 { "trunk sensors only", { { "P", "pelvis" }, { "T", "thorax" } }, hmOnly },
                 { "the HackMotion already holds its pair (NoChange)", { { kHmL, "leadForearm" }, { kHmP, "leadHand" } }, hmOnly },
             })
            run(c, true);
        {
            // Why the sparing table: the same write with the real facts would have displaced the absent Witmotion.
            const QVariantMap roles{ { "W1", "leadForearm" } };
            const ir::ClaimResult honest = ir::claimRole(roles, kHm, "leadForearm", fnOf(hmOnly));
            const ir::HmArmClaim today = ir::claimArmForUnassignedHackMotion(roles, kHm, fnOf(hmOnly));
            check(today.outcome == ir::HmArmClaim::Claimed && today.roles.value("W1") == "leadForearm" && honest.ok
                      && !honest.roles.contains("W1"),
                  "I5a a claim with the real facts would drop the absent Witmotion's forearm, which today's arm claim keeps");
        }

        std::printf("  RR8: a HackMotion's bare pin, when it is found\n");
        for (const Case &c : QList<Case>{
                 { "Migrated: the bare pin on the forearm becomes the pair", { { kHm, "leadForearm" } }, hmOnly },
                 { "Migrated beside a live Witmotion on the forearm (today: both claims stand)",
                   { { kHm, "leadForearm" }, { "W1", "leadForearm" } }, hmAndW },
                 { "Migrated beside an absent Witmotion on the forearm", { { kHm, "leadForearm" }, { "W1", "leadForearm" } }, hmOnly },
                 { "L2 DroppedBare: the pair exists, the bare pin goes",
                   { { kHm, "leadForearm" }, { kHmL, "leadForearm" }, { kHmP, "leadHand" } }, hmOnly },
                 { "Unmappable: a bare pin on the trunk stays (nothing written)", { { kHm, "pelvis" } }, hmOnly },
                 { "Blocked: an absent Witmotion holds the hand (nothing written)", { { kHm, "leadForearm" }, { "W1", "leadHand" } }, hmOnly },
                 { "NoChange: no bare pin", { { kHmL, "leadForearm" }, { kHmP, "leadHand" } }, hmOnly },
             })
            run(c, false);
        {
            // A map no claim can make: a bare pin beside HALF a pair (a hand edit). Today's DroppedBare drops the pin and
            // keeps the half; a claim would write the whole pair. Nothing is asked, so nothing is lost.
            SeededRepository repo({ { kHm, "leadForearm" }, { kHmL, "leadForearm" } });
            Rig rig(repo);
            rig.load();
            const QVariantMap before = rolesOf(rig);
            const ir::HmMigration today = ir::migrateBareHackMotion(before, kHm, fnOf(hmOnly));
            const int saves = repo.saves;
            const bool wrote = requestHackMotionMap(rig, before, kHm, today.roles, hmOnly);
            check(today.outcome == ir::HmMigration::DroppedBare && !wrote && rolesOf(rig) == before && repo.saves == saves,
                  "I5b a bare pin beside half a pair: no claim can drop the pin alone, so nothing is asked (the edge)");
        }
        {
            // E3: a "" on a HackMotion's bare id. Today the settings map holds it: the migration (run first whenever the
            // HackMotion becomes known) drops it as DroppedBare, then the arm claim takes the pair. After the cut-over
            // the Rig's state cannot hold it (the copy keeps it verbatim outside the state): the migration sees nothing,
            // and the arm claim takes the pair. The same roles either way.
            const QVariantMap settingsMap{ { kHm, QString() }, { "P", "pelvis" } };
            const ir::HmMigration m = ir::migrateBareHackMotion(settingsMap, kHm, fnOf(hmOnly));
            const ir::HmArmClaim todayClaim = ir::claimArmForUnassignedHackMotion(m.roles, kHm, fnOf(hmOnly));
            QVariantMap stateMap = settingsMap;
            stateMap.remove(kHm);   // what the Rig's state holds of it
            const ir::HmMigration m2 = ir::migrateBareHackMotion(stateMap, kHm, fnOf(hmOnly));
            const ir::HmArmClaim afterClaim = ir::claimArmForUnassignedHackMotion(m2.roles, kHm, fnOf(hmOnly));
            check(m.outcome == ir::HmMigration::DroppedBare && todayClaim.outcome == ir::HmArmClaim::Claimed
                      && m2.outcome == ir::HmMigration::NoChange && afterClaim.outcome == ir::HmArmClaim::Claimed
                      && todayClaim.roles == afterClaim.roles,
                  "I5c E3: a \"\" on the HackMotion's bare id stops no arm claim, today (dropped first) or after (not held)");
        }
    }

    std::printf("\nI6 app.rig's sensor half\n");
    {
        TestRig t;
        Rig &rig = t.rig;
        RigPresenter view(rig);
        int roles = 0, disabled = 0, aliases = 0, cameras = 0;
        QObject::connect(&view, &RigPresenter::sensorRolesChanged, &view, [&] { ++roles; });
        QObject::connect(&view, &RigPresenter::sensorsDisabledChanged, &view, [&] { ++disabled; });
        QObject::connect(&view, &RigPresenter::sensorAliasesChanged, &view, [&] { ++aliases; });
        QObject::connect(&view, &RigPresenter::camerasChanged, &view, [&] { ++cameras; });
        const SensorFactsTable none;
        requestRole(rig, "W2", "leadHand", none);
        requestRole(rig, "W1", "leadForearm", none);
        requestRole(rig, "P", "pelvis", none);
        check(roles == 3 && view.sensorRoles() == QStringList{ "pelvis", "leadForearm", "leadHand" },
              "I6a the roles each placement holds, in key order (Object.values of today's map); one notifier per change");
        check(view.placementPairs() == QStringList{ "W1", "A", "W2", "B" },
              "I6b the derived placement view as key, letter pairs: the arm only");
        requestRole(rig, "P", "thorax", none);
        check(roles == 4 && view.placementPairs() == QStringList{ "W1", "A", "W2", "B" },
              "I6c (A2) a trunk-only change still notifies the placement view's notifier");
        requestRole(rig, "P", "thorax", none);
        check(roles == 4, "I6d (A3) an unchanged claim notifies nothing");
        const QMetaProperty pairs = view.metaObject()->property(view.metaObject()->indexOfProperty("placementPairs"));
        check(pairs.isValid() && !pairs.isWritable() && pairs.hasNotifySignal()
                  && QString::fromLatin1(pairs.notifySignal().name()) == "sensorRolesChanged",
              "I6e (A4) placementPairs is read-only and notifies with the roles");
        const int camerasBefore = cameras;
        rig.assignPerspective(DeviceKey::camera("Blackfly", "123", "usb-1"), 1);
        check(roles == 4 && cameras == camerasBefore + 1, "I6f a camera's change notifies the cameras, never the sensors' roles");
        view.setSensorEnabledOnThisMachine("W2", false);
        check(view.sensorsDisabled() == QStringList{ "W2" } && disabled == 1 && aliases == 0 && roles == 4,
              "I6g the Enable switch: one Rig command, its own notifier only");
        view.setSensorEnabledOnThisMachine("W2", false);
        view.setSensorEnabledOnThisMachine(QString(), false);
        check(disabled == 1, "I6h an unchanged flag, or no id: nothing");
        rig.setAlias(DeviceKey::sensor("WT901BLE68", "W1"), "Hip");
        check(view.sensorAliasKeys() == QStringList{ "WT901BLE68|W1" } && view.sensorAliasOf("WT901BLE68|W1") == "Hip"
                  && view.sensorAliasOf("|W1").isEmpty() && aliases == 1 && disabled == 1,
              "I6i an alias: by its spelling, its own notifier");
        rig.setEnabledOnThisMachine(DeviceKey::camera("Blackfly", "123", "usb-1"), false);
        check(disabled == 1 && view.sensorsDisabled() == QStringList{ "W2" }, "I6j a camera's flag is not a sensor's");
    }

    std::printf("\nI7 the copy at launch (A1, A5 re-pointed)\n");
    {
        const QString folder = stores.path() + "/i7/rig";
        const QVariantMap legacy{ { "W1", "A" }, { "W2", "B" }, { "W4", "D" } };
        {
            QSettings s = ppSettings();
            s.clear();
            s.setValue("imu/placement", legacy);
            s.sync();
        }
        const QString ini = ppSettings().fileName();
        auto readAll = [&ini] { QFile f(ini); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray(); };
        const QByteArray before = readAll();
        {
            RigStore store(folder, { RigStore::Group::Sensors });
            Rig      rig(store);
            rig.load();
            RigPresenter view(rig);
            check(rolesOf(rig) == QVariantMap{ { "W1", "leadForearm" } , { "W2", "leadHand" } }
                      && view.placementPairs() == QStringList{ "W1", "A", "W2", "B" },
                  "I7a (A1) the first load moves imu/placement: the roles and the derived view (D has no role)");
            check(ppSettings().value("imu/placement").toMap() == legacy && !ppSettings().contains("imu/roles")
                      && !ppSettings().contains("imu/rolesMigrated") && readAll() == before,
                  "I7b (A1, changed) imu/placement untouched, and NOTHING written: neither imu/roles nor the marker");
            requestRole(rig, "W1", "", SensorFactsTable());
            requestRole(rig, "W2", "", SensorFactsTable());
            check(rolesOf(rig).isEmpty(), "I7c the coach unassigns everything");
        }
        {
            RigStore store(folder, { RigStore::Group::Sensors });
            Rig      rig(store);
            rig.load();
            RigPresenter view(rig);
            check(rolesOf(rig).isEmpty() && view.placementPairs().isEmpty() && view.sensorRoles().isEmpty(),
                  "I7d (A5) empty after unassigning survives a relaunch: no copy again, the derived view empty");
            check(readAll() == before, "I7e ... and still nothing written to settings");
        }
    }

    std::printf(g_fail ? "\nFAILURES: %d\n" : "\nALL PASS\n", g_fail);
    return g_fail ? 1 : 0;
}
