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

// rig_commands_test: the Rig's camera commands (src/DomainModel/rig.*, rig_commands.h; docs/design/rig_design.md §4.1,
// §4.2; rig_design_addendum.md decisions 1, 8 and 9), its sensor commands, and its calibration sink
// (rig_calibration_sink.h; decision 4). Most cases run over an in-memory repository that counts saves
// and can fail; K8, K9, K17, K22 and K23 run over the real store in a temporary folder. IsolatedStores is made first (#187).
//
//   K1  each versioned command makes a new state: one save, then one RigChanged with the new RigRef
//   K2  Impact is exclusive across three cameras, through the commands; the camera that loses it becomes None
//   K3  putting a camera back as it was gives the earlier RigStateId back, from a camera first seen by a command
//   K4  a command that leaves the digest unchanged saves nothing and emits nothing
//   K5  the box and the fixed flag are held side by side: a box is kept while its camera is not fixed
//   K6  an alias or an enabled flag: no RigChanged, exactly one RigDeviceLabelChanged (the key, and which fact),
//       after the save; none for a no-op
//   K7  the alias is looked up by its own spelling: two descriptions of one sensor id are two entries (decision 9)
//   K8  a perspective code this build does not know survives a save and a load
//   K9  a failed save still changes the state in memory (and its event is emitted); the next command saves again
//   K10 a key of the wrong kind changes nothing
//   K11 the Rig remembers a change whose save failed and saves it again at quit (P4), the launch's own save too;
//       nothing is saved at quit when nothing is unsaved, nor over a file this build must not write
//   K12 learnClubLength (SET-05, C-7): saved, and no event of either kind, the RigStateId unchanged; the members given
//       replace the entry's and every other member stays (the settings writer's read, set, write back); the entry
//       found by the key's spelling, so two keys that spell the same are one entry; nothing learned saves nothing; a
//       null key is nothing; a failed save is remembered (P4); through the real store, rig.json holds it under
//       today's spelling, byte for byte, and a reload finds it
//   K13 the two set-up assumptions the analysis makes, as `assumed` extrinsics records the Rig derives (RP-8; C-7b): a
//       face-on camera fixed in place is square (yaw 0, sigma 0: AD-7); a down-the-line camera with no measured
//       extrinsics has yaw 0 with the uncalibrated yaw sigma the analysis reads (tuned::dtlCamera, never a copy), fixed
//       or not; any other camera, and a face-on camera not fixed, has none. The record is in the state and its digest,
//       comes and goes with the fact that triggers it (one save, one RigChanged), and has no time observed, so putting a
//       camera back gives its earlier id. At the launch it is derived again, saved once with no event (a stale record
//       replaced; a camera left with nothing but a stale record is no entry), and a second launch saves nothing; a
//       read-only file is derived in memory only. The seam: a measured extrinsics record replaces the assumed one; a
//       record of another kind does not
//
// The sensor commands (claimRole, releaseRole; RigSensorCommands, C-6a.2; invariant 2, one claim per role):
//   K14 a table over today's rule's cases: refused by a present, switched-on holder (a Witmotion's role; a HackMotion
//       refused on the hand); displacing an absent holder (a Witmotion; both lanes of an absent HackMotion: L1); a
//       present but switched-off holder, where both claims stand; a HackMotion holding two roles; a Witmotion moving; a
//       claim that changes nothing; release (a Witmotion; both lanes of a HackMotion); and the caller errors (a null key,
//       a camera's key, Unknown, a role with no settings name, a HackMotion asked for another role). For each, the
//       Rig's answer and its state equal today's imu_roles::claimRole on the same map and facts, and each case's own
//       outcome is asserted as written; a change saves once, then one RigChanged; anything else saves and emits nothing
//   K15 the empty entry: claiming a role then releasing it gives back the earlier RigStateId; an entry read with no
//       role and no record is no entry at the next command; an alignment record stays with its placement through a
//       release and a new claim (an entry with a record and no role is kept)
//   K16 a sensor's alias and enabled flag: setAlias / setEnabledOnThisMachine with a sensor key emit one
//       RigDeviceLabelChanged and no RigChanged; the alias by description|deviceId, the enabled flag by the bare id
//   K17 what claimRole decides over: the rig state's roles only. An entry the store keeps outside the state (a sensor
//       entry of a role this build does not know, on the claiming device's own placement; one with no placement key,
//       holding the role claimed) is neither replaced nor displaced, as today's rule over the whole map would; no role
//       is lost, and the entry is written back as read (the EDGE in NOTES-C-6a.2)
//   K18 the facts table: a device it does not hold reads as today's facts function answers for an unlisted id
//   K19 the placement view (today's imuPlacement): derived from the roles, the arm roles as A, B, C, the trunk left out
//
// The calibration sink (commit, clear; RigCalibrationSink, C-6b.2; rig_design_addendum.md decision 4):
//   K20 a commit: the placement holds the record beside its role, one new state, one RigChanged, nothing saved; an equal
//       commit and a clear of nothing emit nothing; a clear gives the earlier RigStateId back; a commit on a placement
//       with no entry makes a record-only entry, which a claim or its own release keeps (Step 11's E9) and its clear
//       drops; a role released keeps the record, naming the role it was solved for (decision 4.6); a camera's record,
//       or a sensor record of another kind, changes nothing
//   K21 P4 through the sink: a role whose save failed is written again by the next commit; with nothing unsaved, none
//   K22 through the real store: commits and clears leave rig.json's bytes unchanged; a save while records are held
//       writes the roles only (no alignment record, no record-only entry); after a relaunch the Rig holds no record
//   K23 Step 11's E2: an entry rig.json holds with no role and nothing else is not in the state, so the next no-op
//       command saves and emits nothing, and the next save drops it; one with a member this build does not know is
//       kept as read (P2)
//   K24 the evidence pointer (attachEvidence; rig_design_addendum.md decision 2; C-14, #380): set in place on the record
//       observed at that time only, with no new state, no event and no save; refused for any other time, sensor or a
//       null key; kept by an equal commit, gone with a new calibration

#include "../../Core/tests/isolated_stores.h"

#include "../rig.h"
#include "../../Core/PpMessageLog.h"
#include "../../IMU/imu_role_map.h"   // today's rule, which K14 compares the Rig with
#include "../../Stores/rig_store.h"
#include "../../Vocabulary/Rig/camera_assumptions.h"   // the yaw sigma the analysis reads (K13)

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <QTimeZone>

#include <algorithm>
#include <cstdio>
#include <functional>
#include <optional>

using namespace pinpoint;
using namespace pinpoint::rig;

namespace {

int  g_fail = 0;
void check(bool c, const QString &label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", qPrintable(label));
    if (!c) ++g_fail;
}

// An in-memory repository: nothing stored, every save counted (and kept), optionally failing.
class MemoryRepository final : public RigRepository
{
public:
    RigLoad load() override { return loaded; }
    bool    save(const RigId &id, const RigParts &parts) override
    {
        ++saves;
        if (fail) return false;
        savedId    = id;
        savedParts = parts;
        return true;
    }
    int      saves = 0;
    bool     fail  = false;
    RigLoad  loaded;   // what load() hands the Rig: nothing stored, unless a case says otherwise
    RigId    savedId;
    RigParts savedParts;
};

const int kNone = recording::perspectiveCode(recording::Perspective::None);
const int kFaceOn = recording::perspectiveCode(recording::Perspective::FaceOn);
const int kDtl = recording::perspectiveCode(recording::Perspective::DownTheLine);
const int kImpact = recording::perspectiveCode(recording::Perspective::Impact);

const DeviceKey camA = DeviceKey::camera("Blackfly S BFS-U3-16S2M", "24157001", "usb-1");
const DeviceKey camB = DeviceKey::camera("Blackfly S BFS-U3-16S2M", "24157002", "usb-2");
const DeviceKey camC = DeviceKey::camera("FaceTime HD Camera", "", "0x1420000005ac8600");

// Every event the Rig emits, in order, with the save count when it came.
struct Events {
    explicit Events(Rig &rig, const MemoryRepository *repo = nullptr)
    {
        QObject::connect(&rig, &Rig::rigChanged, &rig, [this, repo](const RigRef &r) {
            changed << r;
            savesAtChanged << (repo ? repo->saves : -1);
        });
        QObject::connect(&rig, &Rig::rigDeviceLabelChanged, &rig, [this, repo](const DeviceKey &k, RigDeviceLabel w) {
            labels << qMakePair(k, w);
            savesAtLabel << (repo ? repo->saves : -1);
        });
    }
    void clear() { changed.clear(); labels.clear(); savesAtChanged.clear(); savesAtLabel.clear(); }
    QList<RigRef>                           changed;
    QList<QPair<DeviceKey, RigDeviceLabel>> labels;
    QList<int>                              savesAtChanged, savesAtLabel;
};

const CameraSetup *cameraOf(const Rig &rig, const DeviceKey &k)
{
    for (const CameraSetup &c : rig.parts().state.cameras)
        if (c.key() == k) return &c;
    return nullptr;
}
int codeOf(const Rig &rig, const DeviceKey &k)
{
    const CameraSetup *c = cameraOf(rig, k);
    return c ? c->perspectiveCode() : kNone;
}

// The two assumed records (K13), written out here from their statements, not from the Rig's code (the Solution
// Architect's rulings 2 and 4, 27 Nov 2026): AD-7's square face-on camera (yaw, pitch and roll 0, a sigma of 0 on each:
// no orientation sigma), and the down-the-line camera's yaw, pitch and roll 0 with only the yaw's sigma, the one the
// analysis reads (rig::kDtlYawSigmaUncalDeg); pitch and roll claim none. Neither has a time observed: it
// is an assumption, not an observation.
bool sameValues(const NamedValues &a, const NamedValues &b) { return a.values() == b.values(); }
const NamedValues kLevelAlongTheLine{ { "yawDeg", 0.0 }, { "pitchDeg", 0.0 }, { "rollDeg", 0.0 } };
bool isAssumed(const CalibrationRecord &r, const DeviceKey &camera, const NamedValues &sigmas)
{
    const auto *subject = std::get_if<DeviceKey>(&r.subject);
    const auto *params  = std::get_if<NamedValues>(&r.parameters);
    return subject && *subject == camera && r.kind == CalibrationKind::Extrinsics && r.method == CalibrationMethod::Assumed
           && params && sameValues(*params, kLevelAlongTheLine) && r.uncertainty.isKnown()
           && sameValues(r.uncertainty.values(), sigmas) && !r.observedAtUtc.isValid() && !r.evidence;
}
bool isSquare(const CalibrationRecord &r, const DeviceKey &camera)
{
    return isAssumed(r, camera, NamedValues{ { "yawDeg", 0.0 }, { "pitchDeg", 0.0 }, { "rollDeg", 0.0 } });
}
bool isDtlYaw(const CalibrationRecord &r, const DeviceKey &camera)
{
    return isAssumed(r, camera, NamedValues{ { "yawDeg", pinpoint::rig::kDtlYawSigmaUncalDeg } });
}
CalibrationRecord squareRecord(const DeviceKey &camera)
{
    return CalibrationRecord::forCamera(camera, CalibrationKind::Extrinsics, CalibrationMethod::Assumed, kLevelAlongTheLine,
                                        Uncertainty::sigmas(NamedValues{ { "yawDeg", 0.0 }, { "pitchDeg", 0.0 }, { "rollDeg", 0.0 } }),
                                        QDateTime());
}
// `s` with `camera`'s records replaced by `records`.
RigState withRecords(RigState s, const DeviceKey &camera, const QList<CalibrationRecord> &records)
{
    for (CameraSetup &c : s.cameras)
        if (c.key() == camera) c.calibrations = records;
    return s;
}

// ── The sensors (K14–K19) ──
//
// The roles as imu/roles holds them, turned into the rig state's sensors and back by the test's own code (never the
// Rig's: K14 compares the Rig's conversion with these).
QList<SensorSetup> sensorsOfRoles(const QVariantMap &roles)
{
    QList<SensorSetup> out;
    for (auto it = roles.cbegin(); it != roles.cend(); ++it) {
        SensorSetup x;
        x.key  = PlacementKey::fromPersisted(it.key());
        x.role = analysis::segmentRoleForPlacementRole(it.value().toString());
        out.append(x);
    }
    return out;
}
QVariantMap rolesOfSensors(const QList<SensorSetup> &sensors)
{
    QVariantMap out;
    for (const SensorSetup &x : sensors)
        if (x.role != analysis::SegmentRole::Unknown) out.insert(x.key.persisted(), analysis::placementRoleName(x.role));
    return out;
}
const SensorSetup *sensorOf(const Rig &rig, const PlacementKey &k)
{
    for (const SensorSetup &x : rig.parts().state.sensors)
        if (x.key == k) return &x;
    return nullptr;
}

} // namespace

int main(int argc, char **argv)
{
    pinpoint::test::IsolatedStores stores;
    QCoreApplication app(argc, argv);
    std::printf("rig_commands_test — the temporary directory: %s\n", qPrintable(stores.path()));

    std::printf("\nK1 each versioned command makes a new state, saved, then one RigChanged\n");
    {
        MemoryRepository repo;
        Rig              rig(repo);
        rig.load();
        Events ev(rig, &repo);
        const int savesAtLoad = repo.saves;
        struct Step { const char *name; std::function<void()> run; std::function<bool()> holds; };
        const Step steps[] = {
            { "assignPerspective", [&] { rig.assignPerspective(camA, kFaceOn); }, [&] { return codeOf(rig, camA) == kFaceOn; } },
            { "setMirrored",       [&] { rig.setMirrored(camA, true); },          [&] { return cameraOf(rig, camA) && cameraOf(rig, camA)->mirrored; } },
            { "setFixedInPlace",   [&] { rig.setFixedInPlace(camA, true); },      [&] { return cameraOf(rig, camA) && cameraOf(rig, camA)->fixedInPlace; } },
            { "setHittingArea",    [&] { rig.setHittingArea(camA, QRectF(0.25, 0.5, 0.125, 0.25)); },
              [&] { return cameraOf(rig, camA) && cameraOf(rig, camA)->hittingArea == QRectF(0.25, 0.5, 0.125, 0.25); } },
            { "clearHittingArea",  [&] { rig.clearHittingArea(camA); },           [&] { return cameraOf(rig, camA) && !cameraOf(rig, camA)->hittingArea; } },
        };
        int n = 0;
        for (const Step &st : steps) {
            const RigStateId before = rig.stateId();
            const QDateTime  since  = rig.parts().state.sinceUtc;
            ev.clear();
            st.run();
            ++n;
            check(st.holds() && rig.stateId() != before && rig.stateId() == rigStateIdOf(rig.parts().state)
                      && rig.parts().state.sinceUtc.isValid() && rig.parts().state.sinceUtc >= since,
                  QStringLiteral("K1 %1: the new state is in force, with its own id and sinceUtc").arg(st.name));
            check(repo.saves == savesAtLoad + n && repo.savedParts.state.cameras.size() == rig.parts().state.cameras.size()
                      && rigStateIdOf(repo.savedParts.state) == rig.stateId(),
                  QStringLiteral("K1 %1: saved once, the whole new state").arg(st.name));
            check(ev.changed.size() == 1 && ev.changed.first() == rig.ref() && ev.changed.first().rig() == rig.id()
                      && ev.savesAtChanged.first() == repo.saves && ev.labels.isEmpty(),
                  QStringLiteral("K1 %1: one RigChanged, after the save, carrying the new RigRef").arg(st.name));
        }
    }

    std::printf("\nK2 Impact is exclusive, across three cameras\n");
    {
        MemoryRepository repo;
        Rig              rig(repo);
        rig.load();
        rig.assignPerspective(camA, kImpact);
        rig.assignPerspective(camB, kDtl);
        rig.assignPerspective(camC, kFaceOn);
        rig.setMirrored(camA, true);   // so A keeps an entry when it loses Impact
        Events ev(rig, &repo);
        rig.assignPerspective(camB, kImpact);
        check(codeOf(rig, camB) == kImpact && codeOf(rig, camA) == kNone && codeOf(rig, camC) == kFaceOn,
              "K2a B takes Impact: A, which held it, becomes None; C keeps face-on");
        check(cameraOf(rig, camA) && cameraOf(rig, camA)->mirrored, "K2b A keeps its other facts (mirrored)");
        check(ev.changed.size() == 1, "K2c one RigChanged for the one command");
        rig.assignPerspective(camC, kImpact);
        int holders = 0;
        for (const CameraSetup &c : rig.parts().state.cameras) holders += c.perspectiveCode() == kImpact ? 1 : 0;
        check(codeOf(rig, camC) == kImpact && codeOf(rig, camB) == kNone && holders == 1,
              "K2d the third camera takes Impact: exactly one holder");
        check(!cameraOf(rig, camB), "K2e B, now all defaults, has no entry (the same as no entry)");
        rig.assignPerspective(camA, kDtl);
        check(codeOf(rig, camC) == kImpact && codeOf(rig, camA) == kDtl, "K2f a non-Impact perspective takes nothing from anyone");
    }

    std::printf("\nK3 putting a camera back gives the earlier id back\n");
    {
        MemoryRepository repo;
        Rig              rig(repo);
        rig.load();
        const RigStateId empty = rig.stateId();
        rig.assignPerspective(camA, kFaceOn);   // a camera first seen by a command
        const RigStateId faceOn = rig.stateId();
        rig.setMirrored(camA, true);
        check(rig.stateId() != faceOn, "K3a mirrored makes another state");
        rig.setMirrored(camA, false);
        check(rig.stateId() == faceOn, "K3b mirrored back: the face-on state's id again");
        rig.assignPerspective(camA, kNone);
        check(rig.stateId() == empty && rig.parts().state.cameras.isEmpty(),
              "K3c perspective back to None: the empty rig's id, and no camera entry");
    }

    std::printf("\nK4 a command that changes nothing\n");
    {
        MemoryRepository repo;
        Rig              rig(repo);
        rig.load();
        rig.assignPerspective(camA, kFaceOn);
        rig.setHittingArea(camA, QRectF(0.1, 0.2, 0.3, 0.4));
        Events ev(rig, &repo);
        const int saves = repo.saves;
        rig.assignPerspective(camA, kFaceOn);
        rig.setMirrored(camA, false);
        rig.setFixedInPlace(camA, false);
        rig.setHittingArea(camA, QRectF(0.1, 0.2, 0.3, 0.4));
        rig.clearHittingArea(camB);
        rig.setMirrored(camB, false);   // an unknown camera set to its default: still no entry
        check(ev.changed.isEmpty() && ev.labels.isEmpty(), "K4a no event");
        check(repo.saves == saves, "K4b nothing saved");
        check(!cameraOf(rig, camB), "K4c no entry made for a camera left at its defaults");
    }

    std::printf("\nK5 the box and the fixed flag side by side\n");
    {
        MemoryRepository repo;
        Rig              rig(repo);
        rig.load();
        rig.setHittingArea(camA, QRectF(0.4, 0.6, 0.2, 0.1));
        check(cameraOf(rig, camA) && !cameraOf(rig, camA)->fixedInPlace && cameraOf(rig, camA)->hittingArea,
              "K5a a box is held for a camera that is not fixed");
        rig.setFixedInPlace(camA, true);
        const RigStateId fixedWithBox = rig.stateId();
        rig.setFixedInPlace(camA, false);
        check(cameraOf(rig, camA) && cameraOf(rig, camA)->hittingArea == QRectF(0.4, 0.6, 0.2, 0.1), "K5b un-fixing keeps the box");
        check(rig.stateId() != fixedWithBox, "K5c the flag is in the digest");
        rig.setFixedInPlace(camA, true);
        check(rig.stateId() == fixedWithBox, "K5d re-fixing brings the same state back, box and all");
    }

    std::printf("\nK6 an alias or an enabled flag\n");
    {
        MemoryRepository repo;
        Rig              rig(repo);
        rig.load();
        rig.assignPerspective(camA, kFaceOn);
        const RigStateId state = rig.stateId();
        Events ev(rig, &repo);
        int saves = repo.saves;
        rig.setAlias(camA, "Face on");
        check(ev.changed.isEmpty() && rig.stateId() == state, "K6a setAlias: no new state, no RigChanged");
        check(ev.labels.size() == 1 && ev.labels.first().first == camA && ev.labels.first().second == RigDeviceLabel::Alias
                  && repo.saves == saves + 1 && ev.savesAtLabel.first() == repo.saves
                  && repo.savedParts.other.aliases.size() == 1 && repo.savedParts.other.aliases.first().second == "Face on",
              "K6b exactly one RigDeviceLabelChanged (the camera, Alias), after the save");
        ev.clear(); saves = repo.saves;
        rig.setAlias(camA, "Face on");
        check(ev.labels.isEmpty() && repo.saves == saves, "K6c the same alias again: nothing saved, nothing emitted");
        rig.setAlias(camA, "");
        check(ev.labels.size() == 1 && rig.parts().other.aliases.isEmpty() && repo.saves == saves + 1,
              "K6d an empty alias removes it: one event");
        ev.clear(); saves = repo.saves;
        rig.setAlias(camB, "");
        check(ev.labels.isEmpty() && repo.saves == saves, "K6e removing an alias that is not there: nothing");
        rig.setEnabledOnThisMachine(camB, false);
        check(ev.changed.isEmpty() && rig.stateId() == state && ev.labels.size() == 1 && ev.labels.first().first == camB
                  && ev.labels.first().second == RigDeviceLabel::EnabledOnThisMachine && repo.saves == saves + 1
                  && ev.savesAtLabel.first() == repo.saves,
              "K6f disabling: no RigChanged, one RigDeviceLabelChanged (the camera, EnabledOnThisMachine), after the save");
        ev.clear(); saves = repo.saves;
        rig.setEnabledOnThisMachine(camB, false);
        rig.setEnabledOnThisMachine(camC, true);
        check(ev.labels.isEmpty() && repo.saves == saves, "K6g no change (disabled again; enabled, the default): nothing");
        rig.setEnabledOnThisMachine(camB, true);
        check(ev.labels.size() == 1 && rig.parts().other.enabledOnThisMachine.isEmpty(),
              "K6h re-enabling: one event, and the default is not stored");
    }

    std::printf("\nK7 the alias by its own spelling (decision 9)\n");
    {
        MemoryRepository repo;
        Rig              rig(repo);
        rig.load();
        const DeviceKey one = DeviceKey::sensor("WT901BLE68", "{767a6a67-0000-4000-8000-000000000002}");
        const DeviceKey two = DeviceKey::sensor("WT901BLE67", "{767a6a67-0000-4000-8000-000000000002}");
        rig.setAlias(one, "Pelvis");
        rig.setAlias(two, "Chest");
        check(one == two && rig.parts().other.aliases.size() == 2,
              "K7 two descriptions of one sensor id (equal keys) keep two aliases, as imu/alias does");
    }

    std::printf("\nK8 an unknown perspective code survives a save and a load\n");
    {
        const QString f = stores.path() + "/k8/rig";
        RigId id;
        {
            RigStore store(f);
            Rig      rig(store);
            rig.load();
            rig.assignPerspective(camA, 7);   // a newer build's sixth perspective
            id = rig.id();
        }
        RigStore store(f);
        Rig      rig(store);
        rig.load();
        check(rig.id() == id && codeOf(rig, camA) == 7
                  && cameraOf(rig, camA) && cameraOf(rig, camA)->perspective() == recording::Perspective::None,
              "K8 the code 7 is read back as 7 (and reads as None)");
    }

    std::printf("\nK9 a failed save\n");
    {
        MemoryRepository repo;
        Rig              rig(repo);
        rig.load();
        Events ev(rig, &repo);
        repo.fail = true;
        rig.assignPerspective(camA, kDtl);
        rig.setAlias(camA, "Down the line");
        check(codeOf(rig, camA) == kDtl && rig.parts().other.aliases.size() == 1 && repo.saves == 3,
              "K9a both changes stand in memory, each having tried its save");
        check(ev.changed.size() == 1 && ev.changed.first() == rig.ref() && ev.labels.size() == 1,
              "K9b each still emits its event (the state in force is the new one)");
        repo.fail = false;
        rig.setMirrored(camA, true);
        check(repo.savedParts.state.cameras.size() == 1 && repo.savedParts.state.cameras.first().perspectiveCode() == kDtl
                  && repo.savedParts.other.aliases.size() == 1 && repo.savedParts.state.cameras.first().mirrored,
              "K9c the next command saves the whole rig, the earlier changes with it");

        // The same through the real store: a folder that cannot be made, then made.
        const QString blocker = stores.path() + "/k9-blocked";
        {
            QFile b(blocker);
            if (b.open(QIODevice::WriteOnly)) b.write("in the way");
        }
        int seq = 0;
        PpMessageLog::instance()->fetchSince(seq);
        RigStore store(blocker + "/rig");
        Rig      real(store);
        real.load();
        real.assignPerspective(camA, kFaceOn);
        QStringList warned;
        for (const auto &e : PpMessageLog::instance()->fetchSince(seq)) warned << e.message;
        check(codeOf(real, camA) == kFaceOn && warned.join('\n').contains("[RigStore] not saved"),
              "K9d over the store: the change stands and the failed save is logged");
        QFile::remove(blocker);
        real.setMirrored(camA, true);
        RigStore again(blocker + "/rig");
        const RigLoad back = again.load();
        check(back.id == real.id() && back.parts.state.cameras.size() == 1
                  && back.parts.state.cameras.first().perspectiveCode() == kFaceOn && back.parts.state.cameras.first().mirrored,
              "K9e once the folder can be made, the next command writes the whole rig, with its RigId");
    }

    std::printf("\nK10 a key of the wrong kind\n");
    {
        MemoryRepository repo;
        Rig              rig(repo);
        rig.load();
        Events ev(rig, &repo);
        const int saves = repo.saves;
        const DeviceKey sensor = DeviceKey::sensor("WT901BLE68", "{767a6a67-0000-4000-8000-000000000003}");
        rig.assignPerspective(sensor, kFaceOn);
        rig.setMirrored(DeviceKey(), true);
        rig.setHittingArea(sensor, QRectF(0, 0, 1, 1));
        rig.setAlias(DeviceKey(), "x");
        rig.setEnabledOnThisMachine(DeviceKey(), false);
        check(ev.changed.isEmpty() && ev.labels.isEmpty() && repo.saves == saves && rig.parts().state.cameras.isEmpty(),
              "K10 a sensor's key given a camera command, or a null key: nothing");
    }

    std::printf("\nK11 a change whose save failed, saved again at quit (P4)\n");
    {
        MemoryRepository repo;
        Rig              rig(repo);
        rig.load();
        check(!rig.hasUnsavedChange(), "K11a after a launch that saved: nothing unsaved");
        int saves = repo.saves;
        rig.saveIfUnsaved();
        check(repo.saves == saves, "K11b nothing unsaved: the quit saves nothing");
        repo.fail = true;
        rig.assignPerspective(camA, kDtl);
        check(rig.hasUnsavedChange(), "K11c a failed save is remembered");
        repo.fail = false;
        saves = repo.saves;
        rig.saveIfUnsaved();
        check(repo.saves == saves + 1 && repo.savedParts.state.cameras.size() == 1
                  && repo.savedParts.state.cameras.first().perspectiveCode() == kDtl && !rig.hasUnsavedChange(),
              "K11d at quit the change is saved again, whole");
        repo.fail = true;
        rig.setAlias(camA, "Down the line");
        repo.fail = false;
        rig.setMirrored(camA, true);
        check(!rig.hasUnsavedChange() && repo.savedParts.other.aliases.size() == 1,
              "K11e the next command's save also clears it, as before");
    }
    {
        MemoryRepository repo;
        repo.fail = true;
        Rig rig(repo);
        rig.load();   // a new RigId, whose save fails
        check(rig.hasUnsavedChange(), "K11f the launch's own save failed: remembered");
        repo.fail = false;
        rig.saveIfUnsaved();
        check(repo.savedId == rig.id() && !rig.hasUnsavedChange(), "K11g and saved at quit, with the RigId");
    }
    {
        MemoryRepository repo;
        repo.loaded.source = RigLoad::Source::ReadOnly;
        repo.loaded.id     = RigId::create();
        repo.fail          = true;   // as the store refuses every save over a file this build must not write
        Rig rig(repo);
        rig.load();
        rig.setMirrored(camA, true);
        const int saves = repo.saves;
        rig.saveIfUnsaved();
        check(!rig.hasUnsavedChange() && repo.saves == saves,
              "K11h a read-only file: a refused save is not an unsaved change, and the quit tries nothing");
    }

    std::printf("\nK12 the learned club-length prior (learnClubLength)\n");
    {
        const AthleteId athlete = AthleteId::fromString(u"5b0c7f1e-2a3d-4c5b-8e6f-7a8b9c0d1e2f");
        const ClubLengthPriorKey key(athlete, QStringLiteral("7i"), camA);
        MemoryRepository repo;
        repo.loaded.id = RigId::create();
        repo.loaded.source = RigLoad::Source::File;
        // An entry stored before, with a member the writer does not set.
        repo.loaded.parts.other.clubLengthPriors.append(
            { key, QVariantMap{ { "emaPx", 500.0 }, { "n", 5 }, { "note", "kept" } } });
        Rig rig(repo);
        rig.load();
        Events ev(rig, &repo);
        const RigStateId state = rig.stateId();
        int saves = repo.saves;
        rig.learnClubLength(key, QVariantMap{ { "emaPx", 512.5 }, { "n", 6 } });
        const QVariantMap e = clubLengthPriorOf(rig.parts().other, key);
        check(repo.saves == saves + 1 && clubLengthPriorOf(repo.savedParts.other, key).value("n").toInt() == 6,
              "K12a saved once, with the learned value");
        check(ev.changed.isEmpty() && ev.labels.isEmpty() && rig.stateId() == state,
              "K12b no RigChanged, no RigDeviceLabelChanged, the RigStateId unchanged");
        check(e.value("emaPx").toDouble() == 512.5 && e.value("n").toInt() == 6 && e.value("note").toString() == "kept"
                  && e.size() == 3 && rig.parts().other.clubLengthPriors.size() == 1,
              "K12c the members given replace the entry's; every other member stays");
        saves = repo.saves;
        rig.learnClubLength(key, QVariantMap{ { "emaPx", 512.5 }, { "n", 6 } });
        rig.learnClubLength(ClubLengthPriorKey(), QVariantMap{ { "n", 1 } });
        rig.learnClubLength(ClubLengthPriorKey(athlete, "7i", DeviceKey()), QVariantMap{ { "n", 1 } });
        check(repo.saves == saves && rig.parts().other.clubLengthPriors.size() == 1 && ev.changed.isEmpty() && ev.labels.isEmpty(),
              "K12d nothing learned that the entry did not hold, or a null key: nothing saved, nothing emitted");
        // Two keys whose parts differ but which spell the same: a '|' in the camera's description, or in the club.
        const DeviceKey pipeCam = DeviceKey::camera("Rig|Cam A", "SN 9", "d");
        const DeviceKey plainCam = DeviceKey::camera("Cam A", "SN 9", "d");
        const ClubLengthPriorKey k1(athlete, QStringLiteral("5i"), pipeCam), k2(athlete, QStringLiteral("5i|Rig"), plainCam);
        check(k1 != k2 && k1.persisted() == k2.persisted(), "K12e (two keys, unequal, one spelling)");
        rig.learnClubLength(k1, QVariantMap{ { "n", 1 } });
        rig.learnClubLength(k2, QVariantMap{ { "n", 2 } });
        check(rig.parts().other.clubLengthPriors.size() == 2 && clubLengthPriorOf(rig.parts().other, k1).value("n").toInt() == 2,
              "K12f the entry is found by its spelling, as the reader finds it: one entry, not two");
        repo.fail = true;
        rig.learnClubLength(key, QVariantMap{ { "n", 7 } });
        check(rig.hasUnsavedChange() && clubLengthPriorOf(rig.parts().other, key).value("n").toInt() == 7,
              "K12g a failed save: the prior stands in memory and is remembered as unsaved (P4)");
        repo.fail = false;
        rig.saveIfUnsaved();
        check(!rig.hasUnsavedChange() && clubLengthPriorOf(repo.savedParts.other, key).value("n").toInt() == 7,
              "K12h and saved at quit");
    }
    {
        // Through the real store: rig.json holds the prior under today's spelling (capture_context.cpp's expression on
        // 8273d1cf, before C-1c: athlete uuid + '|' + club + '|' + description + '|' + serial-or-id).
        const QString folder = stores.path() + "/k12/rig";
        const QString athlete = QStringLiteral("5b0c7f1e-2a3d-4c5b-8e6f-7a8b9c0d1e2f");
        const QString todays  = athlete + "|7i|" + "Blackfly S BFS-U3-16S2M|24157001";
        const ClubLengthPriorKey key(AthleteId::fromString(athlete), QStringLiteral("7i"), camA);
        {
            RigStore store(folder);
            Rig      rig(store);
            rig.load();
            rig.learnClubLength(key, QVariantMap{ { "emaPx", 512.25 }, { "varPx", 3.5 }, { "n", 6 }, { "lengthMm", 940 } });
        }
        QFile f(folder + "/rig.json");
        const QByteArray bytes = f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
        const QJsonArray priors = QJsonDocument::fromJson(bytes).object().value("other").toObject().value("clubLengthPriors").toArray();
        const QJsonObject p = priors.isEmpty() ? QJsonObject() : priors.first().toObject();
        check(priors.size() == 1
                  && (p.value("athlete").toString() + '|' + p.value("club").toString() + '|' + p.value("faceOnCamera").toString()).toUtf8()
                         == todays.toUtf8(),
              "K12i rig.json holds the prior under today's spelling, byte for byte");
        RigStore store(folder);
        Rig      rig(store);
        rig.load();
        const QVariantMap e = clubLengthPriorOf(rig.parts().other, key);
        check(e.value("emaPx").toDouble() == 512.25 && e.value("varPx").toDouble() == 3.5 && e.value("n").toInt() == 6
                  && e.value("lengthMm").toInt() == 940,
              "K12j a reload finds it by the typed key, every member as learned");
    }

    std::printf("\nK13 the two set-up assumptions, as recorded `assumed` extrinsics records\n");
    {
        MemoryRepository repo;
        Rig              rig(repo);
        rig.load();
        Events ev(rig, &repo);
        rig.assignPerspective(camA, kFaceOn);
        const RigStateId unfixed = rig.stateId();
        check(cameraOf(rig, camA) && cameraOf(rig, camA)->calibrations.isEmpty(),
              "K13a a face-on camera that is not fixed in place has no assumed record");
        ev.clear();
        const int saves = repo.saves;
        rig.setFixedInPlace(camA, true);
        check(cameraOf(rig, camA) && cameraOf(rig, camA)->calibrations.size() == 1
                  && isSquare(cameraOf(rig, camA)->calibrations.first(), camA),
              "K13b fixed in place: one extrinsics record, method assumed, square and level (yaw, pitch, roll 0, each sigma 0), no time observed");
        check(rig.stateId() != unfixed && rig.stateId() == rigStateIdOf(rig.parts().state)
                  && rig.stateId() == rigStateIdOf(withRecords(rig.parts().state, camA, { squareRecord(camA) }))
                  && rig.stateId() != rigStateIdOf(withRecords(rig.parts().state, camA, {})),
              "K13c the record is in the rig state and its digest: the id is the state's with it, not without it");
        check(repo.saves == saves + 1 && ev.changed.size() == 1 && ev.changed.first() == rig.ref()
                  && rigStateIdOf(repo.savedParts.state) == rig.stateId(),
              "K13d one command: one save holding the record, then one RigChanged");
        const RigStateId fixedId = rig.stateId();
        rig.setFixedInPlace(camA, false);
        check(cameraOf(rig, camA) && cameraOf(rig, camA)->calibrations.isEmpty() && rig.stateId() == unfixed,
              "K13e fixed in place cleared: the record goes, and the earlier id comes back");
        rig.setFixedInPlace(camA, true);
        check(rig.stateId() == fixedId, "K13f fixed again: the same record and the same id (nothing in it is a clock)");

        rig.assignPerspective(camB, kDtl);
        check(cameraOf(rig, camB) && cameraOf(rig, camB)->calibrations.size() == 1
                  && isDtlYaw(cameraOf(rig, camB)->calibrations.first(), camB),
              "K13g a down-the-line camera with no measured extrinsics: one assumed record, yaw, pitch, roll 0, with the "
              "uncalibrated yaw sigma (rig::kDtlYawSigmaUncalDeg) and no sigma for pitch or roll");
        rig.setFixedInPlace(camB, true);
        check(cameraOf(rig, camB) && cameraOf(rig, camB)->calibrations.size() == 1
                  && isDtlYaw(cameraOf(rig, camB)->calibrations.first(), camB),
              "K13h a down-the-line camera fixed in place keeps the down-the-line record (the analysis gives it the yaw "
              "sigma), and only that one");
        rig.assignPerspective(camA, kDtl);
        check(cameraOf(rig, camA) && cameraOf(rig, camA)->calibrations.size() == 1
                  && isDtlYaw(cameraOf(rig, camA)->calibrations.first(), camA),
              "K13i a fixed face-on camera made down-the-line: its square record is replaced by the down-the-line one");
        rig.assignPerspective(camA, kFaceOn);
        check(cameraOf(rig, camA) && cameraOf(rig, camA)->calibrations.size() == 1
                  && isSquare(cameraOf(rig, camA)->calibrations.first(), camA),
              "K13j and back to face-on: square again");

        bool none = true;
        rig.setFixedInPlace(camC, true);   // fixed, perspective None
        none = none && cameraOf(rig, camC) && cameraOf(rig, camC)->calibrations.isEmpty();
        rig.assignPerspective(camC, kImpact);
        none = none && cameraOf(rig, camC) && cameraOf(rig, camC)->calibrations.isEmpty();
        rig.assignPerspective(camC, recording::perspectiveCode(recording::Perspective::Other));
        none = none && cameraOf(rig, camC) && cameraOf(rig, camC)->calibrations.isEmpty();
        rig.assignPerspective(camC, 7);   // a code this build does not know
        none = none && cameraOf(rig, camC) && cameraOf(rig, camC)->calibrations.isEmpty();
        check(none, "K13k a camera neither applies to (None, Impact, Other, an unknown code; fixed in place) has no record");
        rig.assignPerspective(camB, kNone);
        rig.setFixedInPlace(camB, false);
        check(!cameraOf(rig, camB), "K13l a down-the-line camera put back to None and not fixed: no record, and no entry");
    }
    {
        // At the launch: a rig stored before the records existed, or by a build whose records differ. Derived once,
        // saved once, silently; a second launch of what was saved saves nothing.
        MemoryRepository repo;
        repo.loaded.id     = RigId::create();
        repo.loaded.source = RigLoad::Source::File;
        CameraSetup a(camA);
        a.setPerspective(recording::Perspective::FaceOn);
        a.fixedInPlace = true;
        CameraSetup b(camB);
        b.setPerspective(recording::Perspective::DownTheLine);
        b.calibrations.append(CalibrationRecord::forCamera(camB, CalibrationKind::Extrinsics, CalibrationMethod::Assumed,
                                                           NamedValues{ { "yawDeg", 0.0 } },
                                                           Uncertainty::sigmas(NamedValues{ { "yawDeg", 5.0 } }),
                                                           QDateTime::currentDateTimeUtc()));   // a stale record
        CameraSetup c(camC);   // every fact at its default but a stale assumed record (a carried-over copy)
        c.calibrations.append(squareRecord(camC));
        repo.loaded.parts.state.cameras = { a, b, c };
        Rig rig(repo);
        Events ev(rig, &repo);
        rig.load();
        check(cameraOf(rig, camA) && cameraOf(rig, camA)->calibrations.size() == 1
                  && isSquare(cameraOf(rig, camA)->calibrations.first(), camA)
                  && cameraOf(rig, camB) && cameraOf(rig, camB)->calibrations.size() == 1
                  && isDtlYaw(cameraOf(rig, camB)->calibrations.first(), camB),
              "K13m at the launch: the fixed face-on camera gains its record, and the down-the-line camera's stale one "
              "is replaced by today's");
        check(!cameraOf(rig, camC), "K13n a camera that held nothing but an assumed record that no longer applies is no entry");
        check(repo.saves == 1 && rigStateIdOf(repo.savedParts.state) == rig.stateId() && ev.changed.isEmpty()
                  && ev.labels.isEmpty() && !rig.hasUnsavedChange(),
              "K13o the derived state is saved once at the launch, with no event");
        MemoryRepository again;
        again.loaded.id     = rig.id();
        again.loaded.source = RigLoad::Source::File;
        again.loaded.parts  = repo.savedParts;
        Rig second(again);
        second.load();
        check(again.saves == 0 && second.stateId() == rig.stateId(), "K13p the next launch derives the same: nothing saved");
    }
    {
        // A file this build must not write: derived in memory, never saved, never "unsaved".
        MemoryRepository repo;
        repo.loaded.id     = RigId::create();
        repo.loaded.source = RigLoad::Source::ReadOnly;
        repo.fail          = true;
        CameraSetup b(camB);
        b.setPerspective(recording::Perspective::DownTheLine);
        repo.loaded.parts.state.cameras = { b };
        Rig rig(repo);
        rig.load();
        check(cameraOf(rig, camB) && cameraOf(rig, camB)->calibrations.size() == 1 && repo.saves == 0
                  && !rig.hasUnsavedChange(),
              "K13q a read-only file: the record is derived in memory, and nothing is saved or left unsaved");
    }
    {
        // The seam: a measured extrinsics record (none is made in Stage C) is the camera's; no assumed one beside it.
        MemoryRepository repo;
        repo.loaded.id     = RigId::create();
        repo.loaded.source = RigLoad::Source::File;
        const CalibrationRecord solvedA = CalibrationRecord::forCamera(
            camA, CalibrationKind::Extrinsics, CalibrationMethod::Solved, NamedValues{ { "yawDeg", 1.25 } },
            Uncertainty::sigmas(NamedValues{ { "yawDeg", 0.5 } }), QDateTime());
        const CalibrationRecord solvedB = CalibrationRecord::forCamera(
            camB, CalibrationKind::Extrinsics, CalibrationMethod::Solved, NamedValues{ { "yawDeg", -3.5 } },
            Uncertainty::sigmas(NamedValues{ { "yawDeg", 1.0 } }), QDateTime());
        const CalibrationRecord focusB = CalibrationRecord::forCamera(camB, CalibrationKind::Focus, CalibrationMethod::UserMeasured,
                                                                      {}, Uncertainty::notKnown(), QDateTime());
        CameraSetup a(camA);
        a.setPerspective(recording::Perspective::FaceOn);
        a.fixedInPlace = true;
        a.calibrations = { solvedA, squareRecord(camA) };
        CameraSetup b(camB);
        b.setPerspective(recording::Perspective::DownTheLine);
        b.calibrations = { solvedB };
        CameraSetup c(camC);
        c.setPerspective(recording::Perspective::DownTheLine);
        c.calibrations = { focusB };   // a record of another kind is not a measured extrinsics
        c.calibrations.first().subject = camC;
        repo.loaded.parts.state.cameras = { a, b, c };
        Rig rig(repo);
        rig.load();
        const CameraSetup *ra = cameraOf(rig, camA), *rb = cameraOf(rig, camB), *rc = cameraOf(rig, camC);
        check(ra && ra->calibrations.size() == 1 && ra->calibrations.first().method == CalibrationMethod::Solved
                  && rb && rb->calibrations.size() == 1 && rb->calibrations.first().method == CalibrationMethod::Solved,
              "K13r a measured extrinsics record replaces the assumed one: a face-on and a down-the-line camera keep "
              "only theirs");
        check(rc && rc->calibrations.size() == 2
                  && std::any_of(rc->calibrations.begin(), rc->calibrations.end(),
                                 [](const CalibrationRecord &r) { return r.kind == CalibrationKind::Focus; })
                  && std::any_of(rc->calibrations.begin(), rc->calibrations.end(),
                                 [](const CalibrationRecord &r) { return isDtlYaw(r, camC); }),
              "K13s a record of another kind (focus) does not: the down-the-line camera keeps it and gains its assumed one");
    }


    // ── The sensor commands (C-6a.2) ──────────────────────────────────────────────────────────────────────────────
    using analysis::SegmentRole;
    // The devices: Witmotions W1–W3 and a HackMotion H1, present and switched on unless a case says otherwise; GONE and
    // OLD are not present (OLD was a HackMotion: its two lanes). Each is handed in as a description and a device id, the
    // way the enumerator names it; the description is not part of the claim.
    const QString kH1 = QStringLiteral("{0d7a1c2e-0000-4000-8000-0000000000a1}");
    auto sensor = [](const QString &id) { return DeviceKey::sensor(QStringLiteral("WT901BLE68"), id); };
    const DeviceKey h1 = DeviceKey::sensor(QStringLiteral("HackMotion wG3"), kH1);
    const QString h1Lower = kH1 + "#lowerArm", h1Palm = kH1 + "#palm";
    using FactsMap = QMap<QString, SensorFacts>;
    const SensorFacts on{ true, true, false }, parked{ true, false, false }, hmOn{ true, true, true };
    const FactsMap everyone{ { "W1", on }, { "W2", on }, { "W3", on }, { kH1, hmOn } };

    std::printf("\nK14 claimRole and releaseRole against today's rule, case by case\n");
    {
        struct Case {
            const char *name;
            QVariantMap roles;           // the roles before, as imu/roles holds them
            FactsMap    facts;
            DeviceKey   who;
            std::optional<SegmentRole> role;   // none: releaseRole
            // What today's rule answers, as the case is written (not computed):
            bool        ok;
            QVariantMap after;           // the roles after (the same as before unless ok)
            QString     refusedRole, holder;
            QStringList displaced;
            bool        error;
        };
        FactsMap w1Parked = everyone;
        w1Parked["W1"]    = parked;
        const QList<Case> cases = {
            { "refused: the role is held by another present, switched-on Witmotion", { { "W1", "pelvis" } }, everyone,
              sensor("W2"), SegmentRole::Pelvis, false, { { "W1", "pelvis" } }, "pelvis", "W1", {}, false },
            { "refused: a HackMotion on the arm, the hand held by a present, switched-on Witmotion", { { "W2", "leadHand" } },
              everyone, h1, SegmentRole::LeadForearm, false, { { "W2", "leadHand" } }, "leadHand", "W2", {}, false },
            { "displaced: an absent Witmotion's claim (L1)", { { "GONE", "leadForearm" }, { "W3", "leadHand" } }, everyone,
              sensor("W2"), SegmentRole::LeadForearm, true, { { "W2", "leadForearm" }, { "W3", "leadHand" } }, "", "",
              { "GONE" }, false },
            { "displaced: both lanes of an absent HackMotion (L1)", { { "OLD#lowerArm", "leadForearm" }, { "OLD#palm", "leadHand" } },
              everyone, sensor("W1"), SegmentRole::LeadHand, true, { { "W1", "leadHand" } }, "", "",
              { "OLD#palm", "OLD#lowerArm" }, false },
            { "a present but switched-off holder: both claims stand", { { "W1", "pelvis" } }, w1Parked, sensor("W2"),
              SegmentRole::Pelvis, true, { { "W1", "pelvis" }, { "W2", "pelvis" } }, "", "", {}, false },
            { "a HackMotion holds two roles, its lower arm and its palm", { { "W1", "pelvis" } }, everyone, h1,
              SegmentRole::LeadForearm, true, { { "W1", "pelvis" }, { h1Lower, "leadForearm" }, { h1Palm, "leadHand" } }, "", "",
              {}, false },
            { "a Witmotion moving from the hand to the forearm keeps one claim", { { "W1", "leadHand" } }, everyone,
              sensor("W1"), SegmentRole::LeadForearm, true, { { "W1", "leadForearm" } }, "", "", {}, false },
            { "a claim that changes nothing", { { "W1", "pelvis" } }, everyone, sensor("W1"), SegmentRole::Pelvis, true,
              { { "W1", "pelvis" } }, "", "", {}, false },
            { "release: a Witmotion's one claim", { { "W1", "pelvis" }, { "W2", "thorax" } }, everyone, sensor("W1"),
              std::nullopt, true, { { "W2", "thorax" } }, "", "", {}, false },
            { "release: both lanes of a HackMotion", { { h1Lower, "leadForearm" }, { h1Palm, "leadHand" }, { "W1", "pelvis" } },
              everyone, h1, std::nullopt, true, { { "W1", "pelvis" } }, "", "", {}, false },
            { "release: a device that holds nothing", { { "W1", "pelvis" } }, everyone, sensor("W3"), std::nullopt, true,
              { { "W1", "pelvis" } }, "", "", {}, false },
            { "caller error: a null key", { { "W1", "pelvis" } }, everyone, DeviceKey(), SegmentRole::Thorax, false,
              { { "W1", "pelvis" } }, "", "", {}, true },
            { "caller error: a camera's key", { { "W1", "pelvis" } }, everyone, camA, SegmentRole::Thorax, false,
              { { "W1", "pelvis" } }, "", "", {}, true },
            { "caller error: Unknown is no role, and never a release", { { "W1", "pelvis" } }, everyone, sensor("W1"),
              SegmentRole::Unknown, false, { { "W1", "pelvis" } }, "", "", {}, true },
            { "caller error: a role with no settings name (T12)", { { "W1", "pelvis" } }, everyone, sensor("W1"),
              SegmentRole::T12, false, { { "W1", "pelvis" } }, "", "", {}, true },
            { "caller error: a HackMotion asked for the hand", {}, everyone, h1, SegmentRole::LeadHand, false, {}, "", "", {},
              true },
            { "caller error: a HackMotion asked for the pelvis", {}, everyone, h1, SegmentRole::Pelvis, false, {}, "", "", {},
              true },
        };
        int matched = 0;
        for (const Case &c : cases) {
            // Today: the rule over the map, with the facts the app's function would give (an unlisted id: not present).
            const FactsMap facts = c.facts;
            const imu_roles::DeviceFactsFn todaysFacts = [facts](const QString &id) {
                return facts.value(id, SensorFacts{ false, true, false });
            };
            // A role is handed to today's rule by its settings name; one with none (Unknown, T12) by its own name, which
            // is no placement role (today's caller never hands it "" for a role: "" is a release).
            const QString todaysRole = !c.role ? QString()
                                       : analysis::placementRoleName(*c.role).isEmpty() ? analysis::segmentRoleName(*c.role)
                                                                                        : analysis::placementRoleName(*c.role);
            const imu_roles::ClaimResult today = imu_roles::claimRole(c.roles, c.who.persistedId(), todaysRole, todaysFacts);

            // The Rig, holding the same roles in its state, handed the same facts as a table.
            MemoryRepository repo;
            repo.loaded.id                  = RigId::create();
            repo.loaded.source              = RigLoad::Source::File;
            repo.loaded.parts.state.sensors = sensorsOfRoles(c.roles);
            Rig rig(repo);
            rig.load();
            SensorFactsTable table;
            for (auto it = c.facts.cbegin(); it != c.facts.cend(); ++it) table.set(sensor(it.key()), it.value());
            Events ev(rig, &repo);
            const int        saves  = repo.saves;
            const RigStateId before = rig.stateId();
            const RoleClaim  got    = c.role ? rig.claimRole(c.who, *c.role, table) : rig.releaseRole(c.who);
            const QVariantMap after = rolesOfSensors(rig.parts().state.sensors);
            QStringList displaced;
            for (const PlacementKey &k : got.displaced) displaced << k.persisted();

            const bool sameAsToday = got.ok == today.ok && got.error == today.error
                                     && analysis::placementRoleName(got.refusedRole) == today.refusedRole
                                     && got.holder.persistedId() == today.holderId && displaced == today.displaced
                                     && after == today.roles && imu_roles::rolesMapOf(rig.parts().state) == today.roles;
            const bool asWritten = got.ok == c.ok && after == c.after && analysis::placementRoleName(got.refusedRole) == c.refusedRole
                                   && got.holder.persistedId() == c.holder && displaced == c.displaced
                                   && got.error.isEmpty() == !c.error && (got.holder.isNull() || got.holder.isSensor());
            const bool changed = after != c.roles;
            const bool events  = changed ? repo.saves == saves + 1 && ev.changed.size() == 1 && ev.savesAtChanged.first() == repo.saves
                                               && ev.changed.first() == rig.ref() && rig.stateId() != before && ev.labels.isEmpty()
                                         : repo.saves == saves && ev.changed.isEmpty() && ev.labels.isEmpty() && rig.stateId() == before;
            matched += sameAsToday ? 1 : 0;
            check(sameAsToday && asWritten && events,
                  QStringLiteral("K14 %1: as today's rule (%2), as written, %3")
                      .arg(QLatin1String(c.name), today.ok ? QStringLiteral("ok") : today.error.isEmpty() ? QStringLiteral("refused")
                                                                                                            : QStringLiteral("an error"),
                           changed ? QStringLiteral("one save then one RigChanged") : QStringLiteral("nothing saved or emitted")));
        }
        std::printf("  the Rig matched today's rule on %d of %d cases\n", matched, int(cases.size()));
        check(matched == cases.size() && cases.size() == 17, "K14z every case of the table matched today's rule (17 of 17)");
    }

    std::printf("\nK15 the empty entry\n");
    {
        MemoryRepository repo;
        Rig              rig(repo);
        rig.load();
        rig.assignPerspective(camA, kFaceOn);
        const RigStateId earlier = rig.stateId();
        SensorFactsTable table;
        table.set(sensor("W1"), on);
        rig.claimRole(sensor("W1"), SegmentRole::Pelvis, table);
        check(rig.stateId() != earlier && rig.parts().state.sensors.size() == 1, "K15a a claim makes a new state");
        rig.releaseRole(sensor("W1"));
        check(rig.stateId() == earlier && rig.parts().state.sensors.isEmpty(),
              "K15b claiming then releasing gives back the earlier RigStateId (no entry is left)");

        // An entry read with no role and no record (a file's "role": "") is no entry at the next command.
        MemoryRepository read;
        read.loaded.id     = RigId::create();
        read.loaded.source = RigLoad::Source::File;
        SensorSetup idle;
        idle.key = PlacementKey(sensor("W9"));
        read.loaded.parts.state.sensors = { idle };
        Rig fromFile(read);
        fromFile.load();
        check(fromFile.parts().state.sensors.size() == 1, "K15c the load keeps the entry as read");
        fromFile.claimRole(sensor("W1"), SegmentRole::Thorax, table);
        RigState expected;
        expected.sensors = sensorsOfRoles({ { "W1", "thorax" } });
        check(!sensorOf(fromFile, PlacementKey(sensor("W9"))) && fromFile.stateId() == rigStateIdOf(expected),
              "K15d the next command drops it: the state is the one with W1's claim alone");

        // An alignment record stays with its placement (rig_design.md §4.2: held for the run).
        MemoryRepository held;
        held.loaded.id     = RigId::create();
        held.loaded.source = RigLoad::Source::File;
        SensorSetup worn;
        worn.key         = PlacementKey(sensor("W1"));
        worn.role        = SegmentRole::Pelvis;
        worn.calibration = CalibrationRecord::forSensor(worn.key, CalibrationMethod::Solved, SensorAlignmentParameters{},
                                                        Uncertainty::notKnown(), QDateTime::fromMSecsSinceEpoch(1764230400000, QTimeZone::UTC));
        held.loaded.parts.state.sensors = { worn };
        Rig withRecord(held);
        withRecord.load();
        withRecord.releaseRole(sensor("W1"));
        const SensorSetup *w = sensorOf(withRecord, worn.key);
        check(w && w->role == SegmentRole::Unknown && w->calibration && w->calibration->observedAtUtc == worn.calibration->observedAtUtc,
              "K15e released, a placement holding an alignment record stays, with no role and its record");
        withRecord.claimRole(sensor("W1"), SegmentRole::Thorax, table);
        w = sensorOf(withRecord, worn.key);
        check(w && w->role == SegmentRole::Thorax && w->calibration && withRecord.parts().state.sensors.size() == 1,
              "K15f claimed again, it takes the role and keeps its record");
        withRecord.claimRole(sensor("W2"), SegmentRole::Pelvis, table);
        w = sensorOf(withRecord, worn.key);
        check(w && w->calibration && w->role == SegmentRole::Thorax, "K15g another device's claim leaves the record where it is");
    }

    std::printf("\nK16 a sensor's alias and enabled flag\n");
    {
        MemoryRepository repo;
        Rig              rig(repo);
        rig.load();
        const RigStateId state = rig.stateId();
        Events ev(rig, &repo);
        const DeviceKey described = DeviceKey::sensor("WT901BLE68", "W1");
        rig.setAlias(described, "Pelvis");
        check(ev.changed.isEmpty() && rig.stateId() == state && ev.labels.size() == 1 && ev.labels.first().first == described
                  && ev.labels.first().second == RigDeviceLabel::Alias && repo.savedParts.other.aliases.size() == 1
                  && repo.savedParts.other.aliases.first().first.persistedWithDescription() == "WT901BLE68|W1",
              "K16a a sensor's alias: one RigDeviceLabelChanged, no RigChanged, held under description|deviceId");
        ev.clear();
        rig.setAlias(DeviceKey::fromPersistedId(u"W1"), "Chest");   // the same id with no description: another spelling
        check(rig.parts().other.aliases.size() == 2 && ev.labels.size() == 1,
              "K16b the alias is found by its spelling, not by key equality: a bare id is another entry, as in imu/alias");
        ev.clear();
        rig.setEnabledOnThisMachine(described, false);
        check(ev.changed.isEmpty() && ev.labels.size() == 1 && ev.labels.first().second == RigDeviceLabel::EnabledOnThisMachine
                  && rig.parts().other.enabledOnThisMachine.size() == 1 && rig.stateId() == state,
              "K16c a sensor switched off on this machine: one RigDeviceLabelChanged, no RigChanged");
        ev.clear();
        rig.setEnabledOnThisMachine(DeviceKey::fromPersistedId(u"W1"), true);
        check(ev.labels.size() == 1 && rig.parts().other.enabledOnThisMachine.isEmpty(),
              "K16d the enabled flag is the bare id's (imu/excluded's spelling): switched on again by a key with no description");
    }

    std::printf("\nK17 what claimRole decides over: the rig state's roles only\n");
    {
        // A rig.json holding, beside W2's readable claim, two sensor entries this build cannot read: a role it does not
        // know on W1's own placement, and an entry with no placement key holding the pelvis.
        const QString f = stores.path() + "/k17/rig";
        const QJsonObject unknownRole{ { "placement", "W1" }, { "role", "trailForearm" }, { "note", "a later build's" } };
        const QJsonObject noKey{ { "placement", "" }, { "role", "pelvis" } };
        const QJsonObject file{ { "schema", "pinpoint.rig/1" }, { "rigId", RigId::create().toString() },
                                { "state", QJsonObject{ { "cameras", QJsonArray() },
                                                        { "sensors", QJsonArray{ unknownRole, noKey,
                                                                                 QJsonObject{ { "placement", "W2" }, { "role", "thorax" } } } } } },
                                { "other", QJsonObject() } };
        QDir().mkpath(f);
        {
            QFile out(f + "/rig.json");
            if (out.open(QIODevice::WriteOnly)) out.write(QJsonDocument(file).toJson());
        }
        RigStore store(f);
        Rig      rig(store);
        rig.load();
        SensorFactsTable table;
        for (const QString &id : { "W1", "W2", "W3" }) table.set(sensor(id), on);
        // Today's rule, over the whole map as imu/roles would hold these entries.
        const QVariantMap whole{ { "W1", "trailForearm" }, { "", "pelvis" }, { "W2", "thorax" } };
        const imu_roles::DeviceFactsFn fn = [&](const QString &id) { return table.factsFor(id); };
        const imu_roles::ClaimResult own = imu_roles::claimRole(whole, "W1", "leadHand", fn);
        const RoleClaim gotOwn = rig.claimRole(sensor("W1"), SegmentRole::LeadHand, table);
        check(gotOwn.ok && own.ok && imu_roles::rolesMapOf(rig.parts().state) == QVariantMap{ { "W1", "leadHand" }, { "W2", "thorax" } }
                  && own.roles.value("W1") == "leadHand" && !own.roles.values().contains(QVariant("trailForearm")),
              "K17a W1 claims the hand: the Rig holds it, as today; today's rule replaced W1's unknown role, the Rig cannot see it");
        const imu_roles::ClaimResult pel = imu_roles::claimRole(own.roles, "W3", "pelvis", fn);
        const RoleClaim gotPel = rig.claimRole(sensor("W3"), SegmentRole::Pelvis, table);
        check(gotPel.ok && pel.ok && pel.displaced == QStringList{ "" } && gotPel.displaced.isEmpty()
                  && imu_roles::rolesMapOf(rig.parts().state).value("W3") == "pelvis",
              "K17b W3 claims the pelvis: today's rule would displace the entry with no key; the Rig cannot see it, and the "
              "claim stands");
        RigStore again(f);
        const RigLoad back = again.load();
        const QJsonArray written = QJsonDocument::fromJson([&] { QFile in(f + "/rig.json"); return in.open(QIODevice::ReadOnly) ? in.readAll() : QByteArray(); }())
                                       .object().value("state").toObject().value("sensors").toArray();
        check(written.contains(unknownRole) && written.contains(noKey) && rolesOfSensors(back.parts.state.sensors)
                                                                              == QVariantMap{ { "W1", "leadHand" }, { "W2", "thorax" }, { "W3", "pelvis" } },
              "K17c no role is lost: every claim is read back, and both entries this build cannot read are written back as read");
    }

    std::printf("\nK18 the facts table\n");
    {
        SensorFactsTable table;
        table.set(sensor("W1"), parked);
        table.set(camA, on);   // not a sensor: ignored
        check(table.factsFor("W1") == parked && table.size() == 1 && !table.holds(camA.id()),
              "K18a a sensor's facts by its bare id; a camera's key is not held");
        check(table.factsFor("NOT-LISTED") == SensorFacts{ false, true, false } && SensorFactsTable::unknownDevice() == SensorFacts{ false, true, false },
              "K18b a device it does not hold: not present, not a HackMotion, switched on (ImuManager::deviceFacts for an "
              "id the enumerator does not list and the session did not exclude)");
    }

    std::printf("\nK19 the placement view\n");
    {
        RigState s;
        s.sensors = sensorsOfRoles({ { "W1", "pelvis" }, { "W2", "thorax" }, { "W3", "leadUpperArm" }, { h1Lower, "leadForearm" },
                                     { h1Palm, "leadHand" } });
        const QVariantMap view = imu_roles::placementViewOf(s);
        check(view == QVariantMap{ { "W3", "C" }, { h1Lower, "A" }, { h1Palm, "B" } },
              "K19 the arm roles as A, B, C under the same keys, the trunk roles left out (derived, never stored)");
    }

    // ── The calibration sink (C-6b.2; rig_calibration_sink.h; rig_design_addendum.md decision 4) ──────────────────────
    // A Witmotion's alignment record, as a device object builds it: A, M, the two mount checks, a role, a time observed.
    const auto alignment = [](const QString &id, float ax, qint64 ms, SegmentRole role = SegmentRole::Pelvis) {
        SensorAlignmentParameters p;
        p.role      = role;
        p.alignment = SensorAlignment{ QQuaternion(1.f, ax, 0.f, 0.f).normalized(), QQuaternion(0.5f, 0.5f, 0.5f, 0.5f), 3.0, 4.0 };
        return CalibrationRecord::forSensor(PlacementKey(DeviceKey::fromPersistedId(id)), CalibrationMethod::Solved, p,
                                            Uncertainty::notKnown(), QDateTime::fromMSecsSinceEpoch(ms, QTimeZone::UTC));
    };
    const auto readBytes = [](const QString &path) {
        QFile in(path);
        return in.open(QIODevice::ReadOnly) ? in.readAll() : QByteArray();
    };

    std::printf("\nK20 the calibration sink: a commit or a clear is one new state and one RigChanged, and saves nothing\n");
    {
        MemoryRepository repo;
        Rig              rig(repo);
        rig.load();
        SensorFactsTable table;
        for (const QString &id : { "W1", "W2", "W9" }) table.set(sensor(id), on);
        rig.claimRole(sensor("W1"), SegmentRole::Pelvis, table);
        const RigStateId roleOnly = rig.stateId();
        const int savesBefore = repo.saves;
        Events ev(rig, &repo);
        RigCalibrationSink &sink = rig;   // the contract the device objects are handed
        const CalibrationRecord r1 = alignment("W1", 0.1f, 1764230400000);
        sink.commit(r1);
        const SensorSetup *w1 = sensorOf(rig, PlacementKey(sensor("W1")));
        check(ev.changed.size() == 1 && ev.changed.first() == rig.ref() && rig.stateId() != roleOnly && w1 && w1->calibration
                  && rigStateIdOf(RigState{ {}, { SensorSetup{ w1->key, SegmentRole::Pelvis, r1 } }, {} }) == rig.stateId()
                  && w1->role == SegmentRole::Pelvis && ev.labels.isEmpty(),
              "K20a a commit: the placement holds the record beside its role, one new state, one RigChanged with its RigRef");
        check(repo.saves == savesBefore, "K20b the commit wrote nothing to the repository (an alignment is never saved)");
        ev.clear();
        sink.commit(r1);
        check(ev.changed.isEmpty() && repo.saves == savesBefore, "K20c a commit equal to what is held: no event, no save");
        const CalibrationRecord r2 = alignment("W1", 0.2f, 1764230401000);
        sink.commit(r2);
        w1 = sensorOf(rig, PlacementKey(sensor("W1")));
        check(ev.changed.size() == 1 && w1 && w1->calibration && w1->calibration->observedAtUtc == r2.observedAtUtc
                  && repo.saves == savesBefore,
              "K20d a changed record replaces the held one: one RigChanged, no save");
        ev.clear();
        sink.clear(PlacementKey(sensor("W1")), CalibrationKind::SensorAlignment);
        w1 = sensorOf(rig, PlacementKey(sensor("W1")));
        check(ev.changed.size() == 1 && w1 && !w1->calibration && w1->role == SegmentRole::Pelvis && rig.stateId() == roleOnly
                  && repo.saves == savesBefore,
              "K20e a clear: the record goes, the role stays, the earlier RigStateId is back, one RigChanged, no save");
        ev.clear();
        sink.clear(PlacementKey(sensor("W1")), CalibrationKind::SensorAlignment);
        sink.clear(PlacementKey(sensor("W2")), CalibrationKind::SensorAlignment);
        check(ev.changed.isEmpty() && rig.stateId() == roleOnly, "K20f a clear of nothing: no event");

        // The record-only entry (Step 11's E9 carried): a sensor with no role holds a record, and keeps it.
        sink.commit(alignment("W9", 0.3f, 1764230402000, SegmentRole::Unknown));
        const SensorSetup *w9 = sensorOf(rig, PlacementKey(sensor("W9")));
        check(ev.changed.size() == 1 && w9 && w9->role == SegmentRole::Unknown && w9->calibration,
              "K20g a commit on a placement with no entry makes one, with no role, holding the record");
        rig.claimRole(sensor("W2"), SegmentRole::Thorax, table);
        rig.releaseRole(sensor("W9"));
        w9 = sensorOf(rig, PlacementKey(sensor("W9")));
        check(w9 && w9->calibration && w9->role == SegmentRole::Unknown,
              "K20h an entry with a record and no role is not empty: another claim and its own release leave it");
        ev.clear();
        sink.clear(PlacementKey(sensor("W9")), CalibrationKind::SensorAlignment);
        check(!sensorOf(rig, PlacementKey(sensor("W9"))) && ev.changed.size() == 1,
              "K20i its record cleared, the entry is empty and is no entry");

        // A role released leaves the record where it is (decision 4.6): the alignment is still applied.
        sink.commit(alignment("W1", 0.4f, 1764230403000));
        rig.releaseRole(sensor("W1"));
        w1 = sensorOf(rig, PlacementKey(sensor("W1")));
        const auto *solvedFor = w1 && w1->calibration ? std::get_if<SensorAlignmentParameters>(&w1->calibration->parameters) : nullptr;
        check(w1 && w1->role == SegmentRole::Unknown && solvedFor && solvedFor->role == SegmentRole::Pelvis,
              "K20j a role released: the placement keeps its record, which names the role it was solved for (decision 4.6)");

        // Only a sensor's alignment is taken (Stage C).
        ev.clear();
        const RigStateId held = rig.stateId();
        sink.commit(CalibrationRecord::forCamera(camA, CalibrationKind::Extrinsics, CalibrationMethod::Solved,
                                                 NamedValues{ { "yawDeg", 1.0 } }, Uncertainty::notKnown(), QDateTime()));
        CalibrationRecord wrongKind = alignment("W2", 0.5f, 1764230404000);
        wrongKind.kind = CalibrationKind::Extrinsics;
        sink.commit(wrongKind);
        check(ev.changed.isEmpty() && rig.stateId() == held, "K20k a camera's record, or a sensor record of another kind, changes nothing");
    }

    std::printf("\nK21 a change whose save failed is saved again at the next commit (P4)\n");
    {
        MemoryRepository repo;
        Rig              rig(repo);
        rig.load();
        SensorFactsTable table;
        table.set(sensor("W1"), on);
        repo.fail = true;
        rig.claimRole(sensor("W1"), SegmentRole::Pelvis, table);   // the role's save fails
        repo.fail = false;
        const int saves = repo.saves;
        rig.commit(alignment("W1", 0.1f, 1764230400000));
        check(repo.saves == saves + 1 && !rig.hasUnsavedChange() && rolesOfSensors(repo.savedParts.state.sensors)
                                                                        == QVariantMap{ { "W1", "pelvis" } },
              "K21 the role's failed save is written again by the next commit; nothing is unsaved after it");
        const int after = repo.saves;
        rig.commit(alignment("W1", 0.2f, 1764230401000));
        check(repo.saves == after, "K21b with nothing unsaved, the next commit writes nothing");
    }

    std::printf("\nK22 rig.json through the real store: no alignment, no write for one, none after a relaunch\n");
    {
        const QString f = stores.path() + "/k22/rig";
        QDir().mkpath(f);
        SensorFactsTable table;
        for (const QString &id : { "W1", "W2", "W9" }) table.set(sensor(id), on);
        RigStateId rolesOnly;
        {
            RigStore store(f);
            Rig      rig(store);
            rig.load();
            rig.claimRole(sensor("W1"), SegmentRole::Pelvis, table);
            rolesOnly = rig.stateId();
            const QByteArray before = readBytes(f + "/rig.json");
            Events ev(rig);
            rig.commit(alignment("W1", 0.1f, 1764230400000));
            rig.commit(alignment("W9", 0.2f, 1764230401000, SegmentRole::Unknown));   // a record-only entry
            rig.clear(PlacementKey(sensor("W1")), CalibrationKind::SensorAlignment);
            rig.commit(alignment("W1", 0.3f, 1764230402000));
            check(ev.changed.size() == 4 && !before.isEmpty() && readBytes(f + "/rig.json") == before,
                  "K22a four commits and clears, four RigChanged: rig.json's bytes are unchanged (it was not written)");
            rig.claimRole(sensor("W2"), SegmentRole::Thorax, table);   // a real save while two records are held
            const QByteArray saved = readBytes(f + "/rig.json");
            const QJsonArray sensors = QJsonDocument::fromJson(saved).object().value("state").toObject().value("sensors").toArray();
            QStringList placements;
            for (const QJsonValue &v : sensors) placements << v.toObject().value("placement").toString();
            check(saved != before && placements == QStringList{ "W1", "W2" } && !saved.contains("sensorAlignment")
                      && !saved.contains("calibration\"") && !saved.contains("\"W9\""),
                  "K22b a role saved while records are held: rig.json holds the roles only, no alignment record (0), and "
                  "no entry for the record-only placement");
        }
        {
            RigStore store(f);
            Rig      rig(store);
            Events   ev(rig);
            rig.load();
            int records = 0;
            for (const SensorSetup &x : rig.parts().state.sensors) records += x.calibration ? 1 : 0;
            check(records == 0 && rolesOfSensors(rig.parts().state.sensors) == QVariantMap{ { "W1", "pelvis" }, { "W2", "thorax" } }
                      && rig.parts().state.sensors.size() == 2 && ev.changed.isEmpty(),
                  "K22c after a relaunch the Rig holds no alignment record, only the roles; the load emits nothing");
            (void)rolesOnly;
        }
    }

    std::printf("\nK23 an empty sensor entry in rig.json (Step 11's E2): not in the state, and no save for nothing\n");
    {
        const QString f = stores.path() + "/k23/rig";
        QDir().mkpath(f);
        const QJsonObject empty{ { "placement", "W9" }, { "role", "" } };
        const QJsonObject emptyWithMore{ { "placement", "W8" }, { "role", "" }, { "note", "a later build's" } };
        const QJsonObject file{ { "schema", "pinpoint.rig/1" }, { "rigId", RigId::create().toString() },
                                { "state", QJsonObject{ { "cameras", QJsonArray() },
                                                        { "sensors", QJsonArray{ empty, emptyWithMore,
                                                                                 QJsonObject{ { "placement", "W1" }, { "role", "pelvis" } } } } } },
                                { "other", QJsonObject() } };
        {
            QFile out(f + "/rig.json");
            if (out.open(QIODevice::WriteOnly)) out.write(QJsonDocument(file).toJson());
        }
        const QByteArray asWritten = readBytes(f + "/rig.json");
        RigStore store(f);
        Rig      rig(store);
        rig.load();
        Events ev(rig);
        check(!sensorOf(rig, PlacementKey(sensor("W9"))) && !sensorOf(rig, PlacementKey(sensor("W8")))
                  && rig.parts().state.sensors.size() == 1,
              "K23a the load holds neither entry with no role: the state is W1's role alone");
        rig.releaseRole(sensor("W2"));   // a no-op command: W2 holds nothing
        check(ev.changed.isEmpty() && readBytes(f + "/rig.json") == asWritten,
              "K23b the next no-op command saves nothing and emits nothing (the E2 pin: it saved once before)");
        SensorFactsTable table;
        table.set(sensor("W2"), on);
        rig.claimRole(sensor("W2"), SegmentRole::Thorax, table);
        const QJsonArray written = QJsonDocument::fromJson(readBytes(f + "/rig.json")).object().value("state").toObject()
                                       .value("sensors").toArray();
        check(written.contains(emptyWithMore) && !written.contains(empty) && written.size() == 3,
              "K23c at the next save the empty entry is gone and the one holding a member this build does not know is "
              "written back as read (P2)");
    }

    std::printf("\nK24 the evidence pointer (attachEvidence; addendum decision 2; C-14, #380): set in place, no new state\n");
    {
        MemoryRepository repo;
        Rig              rig(repo);
        rig.load();
        RigCalibrationSink &sink = rig;
        const CalibrationRecord r1 = alignment("W1", 0.1f, 1764230400000);
        sink.commit(r1);
        const RigStateId held = rig.stateId();
        const QDateTime  since = rig.parts().state.sinceUtc;
        const int savesBefore = repo.saves;
        Events ev(rig, &repo);
        const SessionId session = SessionId::create();
        const CalibrationEvidence e{ session, QStringLiteral("trunk_ceremony.json") };
        check(rig.attachEvidence(PlacementKey(sensor("W1")), r1.observedAtUtc, e),
              "K24a the record observed at that time takes the pointer");
        const SensorSetup *w1 = sensorOf(rig, PlacementKey(sensor("W1")));
        check(w1 && w1->calibration && w1->calibration->evidence && w1->calibration->evidence->sessionId == session
                  && w1->calibration->evidence->fileName == QLatin1String("trunk_ceremony.json"),
              "K24b it names the session and the file");
        check(rig.stateId() == held && rig.parts().state.sinceUtc == since && ev.changed.isEmpty() && ev.labels.isEmpty()
                  && repo.saves == savesBefore,
              "K24c the rig state's id and its time are unchanged; no RigChanged, no label event, nothing saved");
        check(!rig.attachEvidence(PlacementKey(sensor("W1")), r1.observedAtUtc.addMSecs(1), e)
                  && !rig.attachEvidence(PlacementKey(sensor("W9")), r1.observedAtUtc, e)
                  && !rig.attachEvidence(PlacementKey(), r1.observedAtUtc, e) && !rig.attachEvidence(PlacementKey(sensor("W1")), {}, e),
              "K24d another time, a sensor with no record, a null key or no time: refused, nothing changed");
        sink.commit(r1);
        w1 = sensorOf(rig, PlacementKey(sensor("W1")));
        check(w1 && w1->calibration && w1->calibration->evidence && ev.changed.isEmpty(),
              "K24e a commit equal to what is held keeps the pointer (the same calibration)");
        sink.commit(alignment("W1", 0.2f, 1764230401000));
        w1 = sensorOf(rig, PlacementKey(sensor("W1")));
        check(w1 && w1->calibration && !w1->calibration->evidence && ev.changed.size() == 1,
              "K24f a new calibration replaces the record, and its pointer with it");
    }

    std::printf(g_fail ? "\nFAILURES: %d\n" : "\nALL PASS\n", g_fail);
    return g_fail ? 1 : 0;
}
