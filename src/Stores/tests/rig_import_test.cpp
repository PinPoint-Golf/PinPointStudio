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

// rig_import_test: the copy of the camera keys (and, since C-7, the club-length prior) from settings into rig.json, a
// group at a time, with a fingerprint that notices an older build (src/Stores/rig_store.*; docs/design/rig_design.md §4.4 "From settings to the file" and "Going back to
// an older build", §9 question 6; rig_design_addendum.md decisions 9 and 10). The settings store is ppSettings() inside
// IsolatedStores (made first, #187): a temporary INI file, never the real one, and never a hand-made QSettings path.
// The four cases of §4.4's table are D1 to D4.
//
//   M1  a machine with keys and no rig: the fixture (two cameras, one with no serial; a perspective code this build does
//       not know; a bare key "FO1" holding a perspective and an alias) copies to the same values; the bare entries are
//       kept verbatim and matched to no camera; one load, no event; the group is recorded as taken, with its fingerprint
//   M2  the settings keys are untouched by the copy, byte for byte, at every launch below
//   M3  no second copy when nothing changed: a fresh settings handle and a fresh store read the same fingerprint
//   D1  the older build runs: it reads the keys exactly as they were at the copy
//   D2  the older build changes a key: the next launch copies the whole group again and says so (the last writer
//       wins); a kept entry changed counts too
//   D3  rig.json deleted: the group is copied again, logged, with a new RigId
//   D4  rig.json written by a newer build: read-only, no copy, never saved over
//   M4  a kept entry is still there after a camera command's save and a reload
//   M5  a value of the wrong shape, or one JSON has no form for, is kept verbatim too: dropped 0
//   M6  a machine with neither keys nor a rig: an empty rig; no group taken, since no camera key exists (P1)
//   M7  switched off: a store the Rig writes no group of copies nothing (a test's store; the app's names the cameras)
//   P1  the camera keys gone from settings after the group was taken (the settings reset or deleted): the Rig is left
//       as it is, no copy and no wipe; a group with one key still copies again when its fingerprint differs
//   P2  a group copied again keeps what the copy cannot type and what the file holds that this build cannot read
//
// The club-length prior's group (SET-05, analysis/clubLenPrior; C-7), written as the settings writer before C-7 wrote it
// (one whole map, by today's open-coded key):
//   L1  a machine with priors and no rig: each prior whose key spells itself again is copied, by its typed key; a key
//       that does not (one '|' too few, an upper-case athlete id) and a value that is not a map are kept verbatim; the
//       group is recorded as taken; no event
//   L2  the fingerprint is the same for the values as set and as parsed from the file; the keys untouched by the copy
//   L3  a prior learned on the shot before the cut-over (the old writer's last read, set, write back) is in the copy
//   L4  a prior learned after the copy, through the Rig, stands at the next launch: settings unchanged, no copy again
//   L5  an older build learns a prior in settings: the whole group is copied again, and says so (the last writer wins)
//   L6  the prior's key gone from settings (P1): the Rig's priors are left as they are
//   L7  a group copied again keeps a prior entry in rig.json this build cannot read (P2)
//   L8  the two groups apart: a camera group copied again leaves the priors, and each group's kept entries stay
//
// The assumed extrinsics records (C-7b), which the Rig derives from the copied set-up:
//   A1  the first copy gives the fixed face-on and the down-the-line camera their records, with no event; a group
//       copied again after an older build's edit moves them with the set-up (a camera put on Impact loses its record);
//       a camera gone from the group, holding only its carried-over record, is no entry, in the Rig or in rig.json
//
// The sensor group (imu/roles, imu/alias, imu/excluded; C-6a.2), written as AppSettings writes it:
//   S1  an un-migrated machine (imu/placement only, no marker): the roles the earlier move would give, by today's
//       loadRoles; the copy writes neither the roles nor the marker; the group taken; no event
//   S2  that machine's AppSettings then makes the move (imu/roles and the marker), after a claim through the Rig: the
//       same fingerprint, so no copy again, and the claim stands
//   S3  the aliases by description|deviceId and the excluded list by bare id, each resolving under both sensor
//       spellings (in the Rig, in rig.json, and through the Rig's commands); an alias key with no '|' and a value that is
//       not a string kept verbatim
//   S4  L4: every imu/roles entry copied verbatim, those the typed keys cannot hold (a slot letter, "other", "", a
//       number, a newer build's role, an empty key) kept in rig.json and saved again verbatim after a command and a
//       reload; a kept entry changed is a change (the fingerprint covers it); all six survive each re-copy (S4e, S6c)
//   S5  P1 for the sensor group: no sensor key, the marker alone, or imu/placement alone on a migrated machine leave the
//       Rig as it is; a group with one key copies again when its fingerprint differs
//   S6  P2: a re-copy keeps a sensor entry in rig.json this build cannot read
//   S7  the groups apart: the sensors copied again leave the cameras' aliases, enabled flags and kept entries
//   S8  imu/calibration and imu/mountOrientation are not copied
//   S9  settings writes by the copy: 0, over every sensor copy above

#include "../../Core/tests/isolated_stores.h"

#include "../rig_store.h"
#include "../../Core/PpMessageLog.h"
#include "../../Core/pp_settings.h"
#include "../../DomainModel/rig.h"
#include "../../IMU/imu_role_map.h"   // today's loadRoles and aliasKey, which S1 and S3 compare with

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRectF>

#include <cstdio>
#include <memory>

using namespace pinpoint;
using namespace pinpoint::rig;

namespace {

int  g_fail = 0;
void check(bool c, const QString &label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", qPrintable(label));
    if (!c) ++g_fail;
}

QByteArray readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}
void writeAll(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(bytes);
}
QJsonObject readJson(const QString &path) { return QJsonDocument::fromJson(readAll(path)).object(); }
// A byte added to the settings file (a blank line), so the next QSettings parses its text again, as a launch does.
void reparse(const QString &ini)
{
    QFile f(ini);
    if (f.open(QIODevice::Append)) f.write("\n");
    f.close();
    ppSettings().sync();
}
QString     messagesSince(int &seq)
{
    QStringList out;
    for (const auto &e : PpMessageLog::instance()->fetchSince(seq)) out << e.severity + ": " + e.message;
    return out.join('\n');
}

const QString kBlackfly = QStringLiteral("Blackfly S BFS-U3-16S2M|24157001");      // a serial
const QString kFaceTime = QStringLiteral("FaceTime HD Camera|0x1420000005ac8600"); // no serial: its device id
const QString kBare     = QStringLiteral("FO1");                                   // the set-up tests' bare key

// The fixture, written as AppSettings writes it (whole maps, one setValue each, through ppSettings()).
void writeFixture()
{
    QSettings s = ppSettings();
    s.setValue("camera/perspective", QVariantMap{ { kBlackfly, 2 }, { kFaceTime, 7 }, { kBare, 1 } });
    s.setValue("camera/isMirrored", QVariantMap{ { kBlackfly, true }, { kFaceTime, false } });
    s.setValue("camera/fixedInPlace", QVariantMap{ { kBlackfly, true }, { kFaceTime, false } });   // FX3: an explicit false
    s.setValue("camera/ballRoi", QVariantMap{ { kBlackfly, QVariantMap{ { "x", 0.3 }, { "y", 1.0 / 3.0 }, { "w", 0.125 }, { "h", 0.2 } } } });
    s.setValue("camera/alias", QVariantMap{ { kBlackfly, "Face on" }, { kBare, "Old fixture camera" } });
    s.setValue("camera/excluded", QStringList{ kFaceTime });
    s.setValue("camera/exposureUs", QVariantMap{ { kBlackfly, 900 } });   // not the Rig's: never read by the copy
    s.sync();
}

struct Launch {
    Launch(const QString &folder, QList<RigStore::Group> groups = { RigStore::Group::Cameras })
        : store(folder, groups), rig(store)
    {
        QObject::connect(&rig, &Rig::rigChanged, &rig, [this] { ++events; });
        QObject::connect(&rig, &Rig::rigDeviceLabelChanged, &rig, [this] { ++events; });
        rig.load();
    }
    RigStore store;
    Rig      rig;
    int      events = 0;
};

const CameraSetup *cameraOf(const Rig &rig, const DeviceKey &k)
{
    for (const CameraSetup &c : rig.parts().state.cameras)
        if (c.key() == k) return &c;
    return nullptr;
}

} // namespace

int main(int argc, char **argv)
{
    pinpoint::test::IsolatedStores stores;
    QCoreApplication app(argc, argv);
    const QString ini = ppSettings().fileName();
    std::printf("rig_import_test — the temporary directory: %s\n  settings: %s\n", qPrintable(stores.path()), qPrintable(ini));
    check(stores.contains(ini), "the settings store is the temporary one");
    int seq = 0;
    messagesSince(seq);

    const DeviceKey blackfly = DeviceKey::fromPersistedCameraKey(kBlackfly);
    const DeviceKey facetime = DeviceKey::camera("FaceTime HD Camera", "", "0x1420000005ac8600");
    const QString   folder   = stores.path() + "/m/rig";

    writeFixture();
    // Every QSettings on one file shares one in-memory copy, which holds the values as they were set. A launch parses
    // the file's text, where a one-entry list is a plain string: force that parse, so every case below reads the
    // keys as a launch does.
    const QString typedFingerprint = RigStore::settingsFingerprint(RigStore::Group::Cameras);
    reparse(ini);
    std::printf("  after a re-parse, camera/excluded is a %s\n", ppSettings().value("camera/excluded").typeName());
    check(ppSettings().value("camera/excluded").typeId() == QMetaType::QString,
          "M0a the settings are read from the file's text (a one-entry list reads back as a string)");
    check(RigStore::settingsFingerprint(RigStore::Group::Cameras) == typedFingerprint,
          "M0b the fingerprint is the same for the values as set and as parsed from the file");
    const QByteArray keys = readAll(ini);
    check(!keys.isEmpty(), "the fixture is in the settings file");

    std::printf("\nM1 a machine with keys and no rig\n");
    RigId firstId;
    {
        Launch l(folder);
        const QString log = messagesSince(seq);
        firstId = l.rig.id();
        const CameraSetup *b = cameraOf(l.rig, blackfly);
        const CameraSetup *f = cameraOf(l.rig, facetime);
        check(l.rig.parts().state.cameras.size() == 2 && b && f, "M1a two cameras, by their keys (one with no serial)");
        check(b && b->perspective() == recording::Perspective::FaceOn && b->mirrored && b->fixedInPlace
                  && b->hittingArea == QRectF(0.3, 1.0 / 3.0, 0.125, 0.2),
              "M1b the first camera: face-on, mirrored, fixed, its box exactly");
        check(f && f->perspectiveCode() == 7 && !f->mirrored && !f->fixedInPlace && !f->hittingArea,
              "M1c the second: the unknown code 7 kept, an explicit false read as false");
        check(l.rig.parts().other.aliases.size() == 1 && l.rig.parts().other.aliases.first().first == blackfly
                  && l.rig.parts().other.aliases.first().second == "Face on"
                  && l.rig.parts().other.enabledOnThisMachine.size() == 1
                  && l.rig.parts().other.enabledOnThisMachine.first().first == facetime
                  && !l.rig.parts().other.enabledOnThisMachine.first().second,
              "M1d the alias, and the excluded camera disabled on this machine");
        const QJsonObject j    = readJson(l.store.filePath());
        const QJsonObject kept = j.value("keptFromSettings").toObject();
        check(kept.value("camera/perspective").toObject().value("entries").toObject().value(kBare).toInt() == 1
                  && kept.value("camera/alias").toObject().value("entries").toObject().value(kBare).toString() == "Old fixture camera"
                  && kept.size() == 2,
              "M1e the bare key's perspective and alias are kept verbatim in rig.json");
        bool bareMatched = false;
        for (const CameraSetup &c : l.rig.parts().state.cameras) bareMatched = bareMatched || c.key().persistedCameraKey().contains(kBare);
        check(!bareMatched, "M1f and matched to no camera");
        check(l.events == 0, "M1g one load: no RigChanged, no RigDeviceLabelChanged");
        check(j.value("groupsTaken").toObject().value("cameras").toObject().value("fingerprint").toString()
                      == RigStore::settingsFingerprint(RigStore::Group::Cameras)
                  && j.value("rigId").toString() == firstId.toString(),
              "M1h rig.json records the group as taken, with the keys' fingerprint");
        check(log.contains("keys are copied from settings"), "M1i the copy is logged");
        check(!j.value("state").toObject().value("cameras").toArray().isEmpty(), "M1j and rig.json holds the cameras");
    }
    check(readAll(ini) == keys, "M2a the settings keys are untouched by the copy, byte for byte");

    std::printf("\nM3 no second copy when nothing changed\n");
    {
        // The same file, compact: a launch that saved again would rewrite it indented.
        const QByteArray compact = QJsonDocument(readJson(folder + "/rig.json")).toJson(QJsonDocument::Compact);
        writeAll(folder + "/rig.json", compact);
        reparse(ini);
        Launch l(folder);
        const QString log = messagesSince(seq);
        check(readAll(folder + "/rig.json") == compact && l.rig.id() == firstId && cameraOf(l.rig, blackfly),
              "M3a a fresh store reads the same fingerprint: nothing copied, nothing written");
        check(!log.contains("no longer match") && !log.contains("copied from settings") && l.events == 0,
              "M3b nothing logged as a copy, no event");
    }
    check(readAll(ini).trimmed() == keys.trimmed(), "M2b the keys are untouched by a launch that copies nothing (but for the blank line M3 added)");
    {
        // An explicit false (FX3) and an absent entry are the same fact: dropping one is no change.
        QSettings s = ppSettings();
        s.setValue("camera/fixedInPlace", QVariantMap{ { kBlackfly, true } });
        s.sync();
        Launch l(folder);
        check(!messagesSince(seq).contains("copied again") && l.events == 0,
              "M3c an explicit false removed (absent reads the same): no second copy");
        s.setValue("camera/fixedInPlace", QVariantMap{ { kBlackfly, true }, { kFaceTime, false } });
        s.sync();
    }

    std::printf("\nD1 the older build runs\n");
    {
        const QSettings s = ppSettings();
        check(s.value("camera/perspective").toMap().value(kBlackfly).toInt() == 2
                  && s.value("camera/perspective").toMap().value(kBare).toInt() == 1
                  && s.value("camera/alias").toMap().value(kBare).toString() == "Old fixture camera"
                  && s.value("camera/excluded").toStringList() == QStringList{ kFaceTime },
              "D1 it reads every key as it was at the copy (they are never written or deleted)");
    }

    std::printf("\nD2 the older build changes a key\n");
    {
        {   // a change the newer build made through the Rig before the downgrade
            Launch l(folder);
            l.rig.setMirrored(facetime, true);
        }
        {   // the older build, through settings: the first camera to down-the-line
            QSettings s = ppSettings();
            QVariantMap p = s.value("camera/perspective").toMap();
            p[kBlackfly] = 1;
            s.setValue("camera/perspective", p);
            s.sync();
        }
        const QByteArray olderKeys = readAll(ini);
        Launch l(folder);
        const QString log = messagesSince(seq);
        check(cameraOf(l.rig, blackfly) && cameraOf(l.rig, blackfly)->perspective() == recording::Perspective::DownTheLine,
              "D2a the changed key is copied again");
        check(log.contains("WARN") && log.contains("an older build") && log.contains("copied again"),
              "D2b one ppWarn line says an older build changed them");
        check(cameraOf(l.rig, facetime) && !cameraOf(l.rig, facetime)->mirrored,
              "D2c the whole group is copied: the newer build's edit before the downgrade is undone (the stated cost)");
        check(l.events == 0 && l.rig.id() == firstId, "D2d part of the load: no event, the same RigId");
        check(readJson(folder + "/rig.json").value("groupsTaken").toObject().value("cameras").toObject().value("fingerprint").toString()
                  == RigStore::settingsFingerprint(RigStore::Group::Cameras),
              "D2e the new fingerprint is recorded");
        check(readAll(ini) == olderKeys, "M2c the copy wrote no key");
        {   // a kept entry changed by the older build counts too
            QSettings s = ppSettings();
            QVariantMap p = s.value("camera/perspective").toMap();
            p[kBare] = 3;
            s.setValue("camera/perspective", p);
            s.sync();
        }
        Launch again(folder);
        const QString log2 = messagesSince(seq);
        check(log2.contains("copied again")
                  && readJson(folder + "/rig.json").value("keptFromSettings").toObject().value("camera/perspective").toObject()
                             .value("entries").toObject().value(kBare).toInt() == 3,
              "D2f a kept entry changed is a change: the group is copied again, the entry kept as it now is");
    }

    std::printf("\nD3 rig.json deleted\n");
    {
        QFile::remove(folder + "/rig.json");
        Launch l(folder);
        const QString log = messagesSince(seq);
        check(!l.rig.id().isNull() && l.rig.id() != firstId, "D3a a new RigId");
        check(cameraOf(l.rig, blackfly) && cameraOf(l.rig, facetime) && QFileInfo::exists(folder + "/rig.json"),
              "D3b the keys are copied again into a new rig.json");
        check(log.contains("no rig.json") && log.contains("copied from settings"), "D3c logged");
    }

    std::printf("\nM4 a kept entry after a camera command and a reload\n");
    {
        {
            Launch l(folder);
            l.rig.assignPerspective(blackfly, recording::perspectiveCode(recording::Perspective::Impact));
        }
        Launch l(folder);
        const QJsonObject kept = readJson(folder + "/rig.json").value("keptFromSettings").toObject();
        check(cameraOf(l.rig, blackfly) && cameraOf(l.rig, blackfly)->perspective() == recording::Perspective::Impact
                  && kept.value("camera/alias").toObject().value("entries").toObject().value(kBare).toString() == "Old fixture camera"
                  && kept.value("camera/perspective").toObject().value("entries").toObject().value(kBare).toInt() == 3
                  && l.rig.parts().state.cameras.size() == 2,
              "M4 the bare entries are still in rig.json after the Rig's own save, and still matched to no camera");
    }

    std::printf("\nD4 rig.json written by a newer build\n");
    {
        const QString f = stores.path() + "/d4/rig";
        const QByteArray newer = R"({"schema":"pinpoint.rig/3","rigId":"6f1c2b6e-0d1e-4c71-9a64-3e2b9d0c5a11","state":{"cameras":[]}})";
        writeAll(f + "/rig.json", newer);
        Launch l(f);
        const QString log = messagesSince(seq);
        check(l.store.isReadOnly() && readAll(f + "/rig.json") == newer, "D4a read-only, never saved over");
        check(l.rig.parts().state.cameras.isEmpty() && !log.contains("copied"), "D4b and nothing copied into it");
    }

    std::printf("\nM5 values of the wrong shape\n");
    {
        const QString f = stores.path() + "/m5/rig";
        QSettings s = ppSettings();
        s.setValue("camera/isMirrored", QVariantMap{ { kBlackfly, "maybe" } });
        s.setValue("camera/ballRoi", QVariantMap{ { kBlackfly, QVariantMap{ { "x", 0.1 }, { "y", 0.1 }, { "w", 0.2 }, { "h", 0.2 }, { "z", 1 } } },
                                                  { kFaceTime, QRectF(0.5, 0.5, 0.1, 0.1) } });
        s.setValue("camera/fixedInPlace", QStringLiteral("not a map"));
        s.sync();
        const QByteArray odd = readAll(ini);
        Launch l(f);
        const QJsonObject kept = readJson(f + "/rig.json").value("keptFromSettings").toObject();
        check(kept.value("camera/isMirrored").toObject().value("entries").toObject().value(kBlackfly).toString() == "maybe",
              "M5a a flag that is not one is kept verbatim");
        check(kept.value("camera/ballRoi").toObject().value("entries").toObject().value(kBlackfly).toObject().value("z").toInt() == 1
                  && kept.value("camera/ballRoi").toObject().value("entries").toObject().value(kFaceTime).toObject().contains("@qvariant"),
              "M5b a box with a field this build does not know, and a value JSON has no form for, are kept");
        check(kept.value("camera/fixedInPlace").toObject().value("whole").toString() == "not a map",
              "M5c a key whose whole value is not a map is kept whole");
        check(cameraOf(l.rig, blackfly) && !cameraOf(l.rig, blackfly)->mirrored && !cameraOf(l.rig, blackfly)->hittingArea,
              "M5d none of them is read as a camera's fact");
        check(readAll(ini) == odd, "M5e the keys are untouched");
        Launch again(f);
        check(readJson(f + "/rig.json").value("keptFromSettings").toObject() == kept && !messagesSince(seq).contains("copied again"),
              "M5f the fingerprint of the kept values is stable: no second copy");
    }

    std::printf("\nM6 a machine with neither keys nor a rig\n");
    {
        QSettings s = ppSettings();
        s.clear();
        s.sync();
        const QString f = stores.path() + "/m6/rig";
        Launch l(f);
        const QJsonObject j = readJson(f + "/rig.json");
        check(!l.rig.id().isNull() && l.rig.parts().state.cameras.isEmpty() && l.rig.parts().other.aliases.isEmpty()
                  && !j.value("groupsTaken").toObject().contains("cameras") && j.value("keptFromSettings").toObject().isEmpty(),
              "M6 an empty rig with a new RigId; no camera key, so no group taken (P1); nothing kept");
    }

    std::printf("\nM8 an emptied list, as the file holds it\n");
    {
        writeFixture();
        QSettings s = ppSettings();
        s.remove("camera/excluded");
        s.sync();
        reparse(ini);
        const QString absent = RigStore::settingsFingerprint(RigStore::Group::Cameras);
        s.setValue("camera/excluded", QStringList{});
        s.sync();
        reparse(ini);
        std::printf("  an emptied camera/excluded reads back as %s\n",
                    ppSettings().value("camera/excluded").isValid() ? ppSettings().value("camera/excluded").typeName() : "an invalid value");
        const QString f = stores.path() + "/m8/rig";
        Launch l(f);
        check(RigStore::settingsFingerprint(RigStore::Group::Cameras) == absent
                  && !readJson(f + "/rig.json").value("keptFromSettings").toObject().contains("camera/excluded")
                  && l.rig.parts().other.enabledOnThisMachine.isEmpty(),
              "M8 an emptied list reads as absent: the same fingerprint, nothing kept, no camera disabled");
    }

    std::printf("\nM7 switched off\n");
    {
        writeFixture();
        const QString f = stores.path() + "/m7/rig";
        Launch l(f, {});
        const QJsonObject j = readJson(f + "/rig.json");
        check(l.rig.parts().state.cameras.isEmpty() && j.value("groupsTaken").toObject().isEmpty()
                  && j.value("keptFromSettings").toObject().isEmpty(),
              "M7 a store that writes no group copies nothing, whatever the keys hold");
    }

    std::printf("\nP1 the camera keys gone from settings\n");
    {
        writeFixture();
        reparse(ini);
        const QString f = stores.path() + "/p1/rig";
        RigStateId taken;
        QJsonObject groupsAtCopy;
        {
            Launch l(f);
            taken = l.rig.stateId();
            groupsAtCopy = readJson(f + "/rig.json").value("groupsTaken").toObject();
            check(cameraOf(l.rig, blackfly) && cameraOf(l.rig, facetime), "P1a the group taken: two cameras");
        }
        messagesSince(seq);
        {
            QSettings s = ppSettings();
            for (const char *k : { "camera/perspective", "camera/isMirrored", "camera/fixedInPlace", "camera/ballRoi",
                                   "camera/alias", "camera/excluded" })
                s.remove(QLatin1String(k));
            s.sync();
        }
        reparse(ini);
        const QByteArray before = readAll(f + "/rig.json");
        {
            Launch l(f);
            const QString log = messagesSince(seq);
            check(l.rig.stateId() == taken && cameraOf(l.rig, blackfly) && cameraOf(l.rig, facetime)
                      && l.rig.parts().other.aliases.size() == 1 && l.rig.parts().other.enabledOnThisMachine.size() == 1,
                  "P1b no camera key in settings: the Rig is left as it is (every camera, alias and enabled flag)");
            check(!log.contains("copied again") && !log.contains("no longer match") && readAll(f + "/rig.json") == before
                      && l.events == 0,
                  "P1c no copy, nothing written, nothing logged as an older build's edit, no event");
        }
        {
            QSettings s = ppSettings();
            s.setValue("camera/alias", QVariantMap{ { kBlackfly, "Renamed by an older build" } });
            s.sync();
        }
        reparse(ini);
        {
            Launch l(f);
            const QString log = messagesSince(seq);
            check(log.contains("copied again") && l.rig.parts().other.aliases.size() == 1
                      && l.rig.parts().other.aliases.first().second == "Renamed by an older build",
                  "P1d one key present and its fingerprint different: the group is copied again, as before");
        }
        QSettings s = ppSettings();
        s.remove("camera/alias");
        s.sync();
    }

    std::printf("\nP2 a group copied again keeps what this build cannot read\n");
    {
        writeFixture();
        reparse(ini);
        const QString f = stores.path() + "/p2/rig";
        { Launch l(f); }
        // What a later build of the same schema could have added: a record of a kind this build does not know, on the
        // first camera, and a camera entry with no key.
        QJsonObject j = readJson(f + "/rig.json");
        QJsonObject state = j.value("state").toObject();
        QJsonArray cams = state.value("cameras").toArray();
        const QJsonObject unknownRecord{ { "subject", QJsonObject{ { "camera", kBlackfly } } }, { "kind", "thermalDrift" },
                                         { "method", "solved" }, { "parameters", QJsonObject{ { "k", 2 } } } };
        const QJsonObject noKey{ { "perspective", 3 }, { "note", "no key" } };
        for (int i = 0; i < cams.size(); ++i) {
            QJsonObject c = cams.at(i).toObject();
            if (c.value("key").toString() != kBlackfly) continue;
            QJsonArray recs = c.value("calibrations").toArray();
            recs.append(unknownRecord);
            c.insert("calibrations", recs);
            cams.replace(i, c);
        }
        cams.append(noKey);
        state.insert("cameras", cams);
        j.insert("state", state);
        writeAll(f + "/rig.json", QJsonDocument(j).toJson());
        {   // the older build changes a key: the group is copied again
            QSettings s = ppSettings();
            QVariantMap p = s.value("camera/perspective").toMap();
            p[kBlackfly] = 1;
            s.setValue("camera/perspective", p);
            s.sync();
        }
        reparse(ini);
        {
            Launch l(f);
            check(messagesSince(seq).contains("copied again") && cameraOf(l.rig, blackfly)
                      && cameraOf(l.rig, blackfly)->perspective() == recording::Perspective::DownTheLine,
                  "P2a the group is copied again");
        }
        const QJsonArray after = readJson(f + "/rig.json").value("state").toObject().value("cameras").toArray();
        bool recordKept = false, entryKept = false;
        for (const QJsonValue &v : after) {
            const QJsonObject c = v.toObject();
            if (c.value("key").toString() == kBlackfly) recordKept = c.value("calibrations").toArray().contains(unknownRecord);
            if (c == noKey) entryKept = true;
        }
        check(recordKept && entryKept, "P2b the record of an unknown kind and the entry with no key are written back as read");
        check(readJson(f + "/rig.json").value("keptFromSettings").toObject().value("camera/alias").toObject()
                  .value("entries").toObject().value(kBare).toString() == "Old fixture camera",
              "P2c and the bare key's entries are still kept from settings");
    }

    std::printf("\nL1 the club-length prior: a machine with priors and no rig\n");
    const QString athlete = QStringLiteral("5b0c7f1e-2a3d-4c5b-8e6f-7a8b9c0d1e2f");
    // SET-05's key, as capture_context.cpp built it on 8273d1cf, before C-1c (athlete uuid | club | camera key).
    auto todaysPriorKey = [](const QString &uuid, const QString &club, const QString &cameraKey) {
        return uuid + QLatin1Char('|') + club + QLatin1Char('|') + cameraKey;
    };
    const QString pA       = todaysPriorKey(athlete, "7i", kBlackfly);
    const QString pNoOne   = todaysPriorKey(QString(), "Driver", kFaceTime);   // no athlete chosen
    const QString pBare    = QStringLiteral("FO1|7i");                          // one '|' too few
    const QString pUpper   = todaysPriorKey(athlete.toUpper(), "7i", kBlackfly);   // never what the reader spells
    const QString pOdd     = todaysPriorKey(athlete, "PW", kBlackfly);          // its value is not a map
    const ClubLengthPriorKey kA(AthleteId::fromString(athlete), "7i", blackfly);
    const ClubLengthPriorKey kNoOne(AthleteId(), "Driver", facetime);
    auto entry = [](int n, const QString &when) {
        return QVariantMap{ { "emaPx", 512.25 + n }, { "varPx", 3.5 }, { "n", n }, { "disagreeRun", 0 }, { "lengthMm", 940 },
                            { "frameW", 1280 }, { "frameH", 1024 }, { "lastUpdatedUtc", when } };
    };
    // The settings writer before C-7 (ShotProcessor through AppSettings::setClubLenPrior): read the whole map, set the
    // shot's entry, write the whole map back.
    auto oldWriterLearns = [&](const QString &key, const QVariantMap &value) {
        QSettings s = ppSettings();
        QVariantMap all = s.value("analysis/clubLenPrior", QVariantMap{}).toMap();
        all[key] = value;
        s.setValue("analysis/clubLenPrior", all);
        s.sync();
    };
    // A log line that holds both (the group's name is quoted by the log stream).
    auto lineHas = [](const QString &log, const char *a, const char *b) {
        for (const QString &line : log.split('\n'))
            if (line.contains(QLatin1String(a)) && line.contains(QLatin1String(b))) return true;
        return false;
    };
    const QList<RigStore::Group> both{ RigStore::Group::Cameras, RigStore::Group::ClubLengthPrior };
    const QString lf = stores.path() + "/l/rig";
    {
        QSettings s = ppSettings();
        s.clear();
        s.setValue("analysis/clubLenPrior",
                   QVariantMap{ { pA, entry(6, "2026-11-20T10:00:00Z") }, { pNoOne, entry(2, "2026-11-19T09:00:00Z") },
                                { pBare, entry(1, "x") }, { pUpper, entry(4, "y") }, { pOdd, QStringLiteral("not a map") } });
        s.sync();
    }
    // L3's case is in the fixture: the shot before the cut-over learned n 7, through the old writer.
    oldWriterLearns(pA, entry(7, "2026-11-27T08:00:00Z"));
    const QString priorAsSet = RigStore::settingsFingerprint(RigStore::Group::ClubLengthPrior);
    reparse(ini);
    const QByteArray priorKeys = readAll(ini);
    messagesSince(seq);
    {
        Launch l(lf, both);
        const QString log = messagesSince(seq);
        const QJsonObject j = readJson(lf + "/rig.json");
        const QVariantMap a = clubLengthPriorOf(l.rig.parts().other, kA);
        const QVariantMap b = clubLengthPriorOf(l.rig.parts().other, kNoOne);
        check(l.rig.parts().other.clubLengthPriors.size() == 2 && !a.isEmpty() && !b.isEmpty()
                  && b.value("n").toInt() == 2 && b.value("emaPx").toDouble() == 514.25,
              "L1a the two priors whose keys spell themselves again are copied, each found by its typed key");
        const QJsonObject kept = j.value("keptFromSettings").toObject().value("analysis/clubLenPrior").toObject()
                                     .value("entries").toObject();
        check(kept.size() == 3 && kept.contains(pBare) && kept.contains(pUpper) && kept.value(pOdd).toString() == "not a map"
                  && kept.value(pUpper).toObject().value("n").toInt() == 4,
              "L1b a key with one '|' too few, an upper-case athlete id and a value that is not a map: kept verbatim");
        check(j.value("groupsTaken").toObject().value("clubLengthPrior").toObject().value("fingerprint").toString()
                      == RigStore::settingsFingerprint(RigStore::Group::ClubLengthPrior),
              "L1c rig.json records the prior's group as taken, with its fingerprint");
        check(l.events == 0 && lineHas(log, "clubLengthPrior", "keys are copied from settings"),
              "L1d one load, no event; the copy is logged");
        check(a.value("n").toInt() == 7 && a.value("emaPx").toDouble() == 519.25
                  && a.value("lastUpdatedUtc").toString() == "2026-11-27T08:00:00Z" && a.value("frameW").toInt() == 1280,
              "L3 the prior learned on the shot before the cut-over is the one copied");
    }
    check(RigStore::settingsFingerprint(RigStore::Group::ClubLengthPrior) == priorAsSet,
          "L2a the prior's fingerprint is the same as set and as parsed from the file");
    check(readAll(ini) == priorKeys, "L2b the copy wrote no settings key");

    std::printf("\nL4 a prior learned after the copy stands\n");
    {
        { Launch l(lf, both); l.rig.learnClubLength(kA, entry(8, "2026-11-27T09:00:00Z")); }
        reparse(ini);
        Launch l(lf, both);
        const QString log = messagesSince(seq);
        check(clubLengthPriorOf(l.rig.parts().other, kA).value("n").toInt() == 8 && !log.contains("copied again")
                  && ppSettings().value("analysis/clubLenPrior").toMap().value(pA).toMap().value("n").toInt() == 7,
              "L4 the next launch: the Rig's n 8 stands, the settings' n 7 is not copied over it");
    }

    std::printf("\nL5 an older build learns a prior in settings\n");
    {
        oldWriterLearns(pA, entry(9, "2026-11-28T10:00:00Z"));
        reparse(ini);
        Launch l(lf, both);
        const QString log = messagesSince(seq);
        check(clubLengthPriorOf(l.rig.parts().other, kA).value("n").toInt() == 9 && log.contains("WARN")
                  && lineHas(log, "clubLengthPrior", "no longer match") && l.events == 0,
              "L5 the whole group is copied again, with one ppWarn line: the older build's n 9 replaces the Rig's n 8");
    }

    std::printf("\nL6 the prior's key gone from settings (P1)\n");
    {
        QSettings s = ppSettings();
        const QVariant saved = s.value("analysis/clubLenPrior");
        s.remove("analysis/clubLenPrior");
        s.sync();
        reparse(ini);
        const QByteArray before = readAll(lf + "/rig.json");
        {
            Launch l(lf, both);
            check(clubLengthPriorOf(l.rig.parts().other, kA).value("n").toInt() == 9
                      && l.rig.parts().other.clubLengthPriors.size() == 2 && readAll(lf + "/rig.json") == before
                      && !messagesSince(seq).contains("copied"),
                  "L6 no prior key in settings: the Rig's priors are left as they are, nothing written");
        }
        s.setValue("analysis/clubLenPrior", saved);
        s.sync();
        reparse(ini);
    }

    std::printf("\nL7 a re-copy keeps a prior entry this build cannot read (P2)\n");
    {
        QJsonObject j = readJson(lf + "/rig.json");
        QJsonObject other = j.value("other").toObject();
        QJsonArray priors = other.value("clubLengthPriors").toArray();
        const QJsonObject unreadable{ { "athlete", athlete }, { "club", "7i" }, { "faceOnCamera", "no camera key" },
                                      { "value", QJsonObject{ { "n", 3 } } } };
        priors.append(unreadable);
        other.insert("clubLengthPriors", priors);
        j.insert("other", other);
        writeAll(lf + "/rig.json", QJsonDocument(j).toJson());
        oldWriterLearns(pNoOne, entry(3, "2026-11-28T11:00:00Z"));
        reparse(ini);
        {
            Launch l(lf, both);
            check(messagesSince(seq).contains("copied again") && clubLengthPriorOf(l.rig.parts().other, kNoOne).value("n").toInt() == 3,
                  "L7a the group is copied again");
        }
        check(readJson(lf + "/rig.json").value("other").toObject().value("clubLengthPriors").toArray().contains(unreadable),
              "L7b the entry this build cannot read is written back as read");
    }

    std::printf("\nL8 the two groups apart\n");
    {
        writeFixture();   // the camera keys, with their bare entries
        reparse(ini);
        { Launch l(lf, both); }
        {
            QSettings s = ppSettings();
            QVariantMap p = s.value("camera/perspective").toMap();
            p[kBlackfly] = 1;
            s.setValue("camera/perspective", p);
            s.sync();
        }
        reparse(ini);
        messagesSince(seq);
        {
            Launch l(lf, both);
            const QString log = messagesSince(seq);
            check(lineHas(log, "cameras", "no longer match") && !lineHas(log, "clubLengthPrior", "keys")
                      && clubLengthPriorOf(l.rig.parts().other, kA).value("n").toInt() == 9,
                  "L8a a camera group copied again leaves the priors as they are");
        }
        const QJsonObject kept = readJson(lf + "/rig.json").value("keptFromSettings").toObject();
        check(kept.contains("analysis/clubLenPrior") && kept.contains("camera/perspective") && kept.contains("camera/alias"),
              "L8b each group's kept entries stay when the other is copied again");
        oldWriterLearns(pA, entry(10, "2026-11-28T12:00:00Z"));
        reparse(ini);
        { Launch l(lf, both); }
        const QJsonObject kept2 = readJson(lf + "/rig.json").value("keptFromSettings").toObject();
        check(kept2.contains("analysis/clubLenPrior") && kept2.contains("camera/perspective") && kept2.contains("camera/alias"),
              "L8c and when the prior's group is copied again, the camera's kept entries stay");
    }

    std::printf("\nA1 the assumed records follow the copy from settings\n");
    {
        const QString af = stores.path() + "/a1/rig";
        auto assumed = [](const CameraSetup *c) {
            int n = 0;
            if (c)
                for (const CalibrationRecord &r : c->calibrations)
                    n += r.kind == CalibrationKind::Extrinsics && r.method == CalibrationMethod::Assumed;
            return n;
        };
        auto setPerspectives = [&](const QVariantMap &p) {
            QSettings s = ppSettings();
            s.setValue("camera/perspective", p);
            s.sync();
            reparse(ini);
        };
        writeFixture();   // the face-on Blackfly fixed in place; the FaceTime camera on an unknown code
        setPerspectives(QVariantMap{ { kBlackfly, 2 }, { kFaceTime, 1 } });
        {
            Launch l(af);
            check(assumed(cameraOf(l.rig, blackfly)) == 1 && assumed(cameraOf(l.rig, facetime)) == 1 && l.events == 0,
                  "A1a the first copy: the fixed face-on camera and the down-the-line camera each hold one assumed record");
        }
        // An older build moves the down-the-line camera to Impact: the group is copied again, and its record goes.
        setPerspectives(QVariantMap{ { kBlackfly, 2 }, { kFaceTime, 4 } });
        {
            Launch l(af);
            check(cameraOf(l.rig, facetime) && assumed(cameraOf(l.rig, facetime)) == 0 && assumed(cameraOf(l.rig, blackfly)) == 1,
                  "A1b copied again with the camera on Impact: its record is gone, the face-on camera's stays");
        }
        // An older build forgets the face-on camera altogether: the copy carries its records over, and with nothing else
        // left, the camera is no entry.
        {
            QSettings s = ppSettings();
            for (const char *k : { "camera/perspective", "camera/isMirrored", "camera/fixedInPlace", "camera/ballRoi", "camera/alias" }) {
                QVariantMap m = s.value(k).toMap();
                m.remove(kBlackfly);
                s.setValue(k, m);
            }
            s.sync();
            reparse(ini);
        }
        {
            Launch l(af);
            check(!cameraOf(l.rig, blackfly) && cameraOf(l.rig, facetime),
                  "A1c a camera gone from the copied group, which held only its carried-over assumed record, is no entry");
        }
        bool written = false;
        for (const QJsonValue &v : readJson(af + "/rig.json").value("state").toObject().value("cameras").toArray())
            written = written || v.toObject().value("key").toString() == kBlackfly;
        check(!written, "A1d ... and is not written to rig.json");
    }


    // ── The sensor group (C-6a.2) ─────────────────────────────────────────────────────────────────────────────────
    const QList<RigStore::Group> sensorsOnly{ RigStore::Group::Sensors };
    const QString kHm = QStringLiteral("{0d7a1c2e-0000-4000-8000-0000000000a1}");
    // The rig state's roles as imu/roles holds them, by the test's own code.
    auto rolesOf = [](const Rig &rig) {
        QVariantMap out;
        for (const SensorSetup &x : rig.parts().state.sensors)
            if (x.role != analysis::SegmentRole::Unknown) out.insert(x.key.persisted(), analysis::placementRoleName(x.role));
        return out;
    };
    auto aliasOf = [](const Rig &rig, const QString &spelling) {
        for (const auto &a : rig.parts().other.aliases)
            if (a.first.isSensor() && a.first.persistedWithDescription() == spelling) return a.second;
        return QString();
    };
    auto enabledOf = [](const Rig &rig, const DeviceKey &k) {
        for (const auto &f : rig.parts().other.enabledOnThisMachine)
            if (f.first == k) return f.second;
        return true;
    };
    auto keptSensorEntries = [](const QString &folder, const char *key) {
        return readJson(folder + "/rig.json").value("keptFromSettings").toObject().value(QLatin1String(key)).toObject()
            .value("entries").toObject();
    };
    // The six imu/roles entries S4 writes that the typed keys cannot hold, as kept in rig.json (W5's value as given).
    auto allSixKept = [](const QJsonObject &k, const QString &w5) {
        return k.size() == 6 && k.value("W5").toString() == w5 && k.value("W6").toString() == "other" && k.value("W7").isString()
               && k.value("W7").toString().isEmpty() && k.value("W8").toInt() == 5 && k.value("W9").toString() == "trailForearm"
               && k.value("").toString() == "thorax";
    };
    // S9: every sensor copy below is checked against the settings file's bytes before it.
    int sensorCopies = 0, settingsWrites = 0;
    auto copyWith = [&](const QString &folder, const QList<RigStore::Group> &groups) {
        const QByteArray before = readAll(ini);
        auto l = std::make_unique<Launch>(folder, groups);
        ++sensorCopies;
        if (readAll(ini) != before) ++settingsWrites;
        return l;
    };

    std::printf("\nS1 an un-migrated machine: imu/placement only, no marker\n");
    const QString s1 = stores.path() + "/s1/rig";
    QVariantMap migratedRoles;
    {
        {
            QSettings s = ppSettings();
            s.clear();
            s.setValue("imu/placement", QVariantMap{ { "W1", "A" }, { "W2", "B" }, { "W3", "C" }, { "W4", "D" }, { "W5", "other" },
                                                     { kHm, "A" } });
            s.sync();
        }
        reparse(ini);
        const QByteArray before = readAll(ini);
        // What AppSettings reads on this machine today: loadRoles with no marker, over imu/placement.
        const imu_roles::RolesLoad today = imu_roles::loadRoles(false, {}, ppSettings().value("imu/placement").toMap());
        migratedRoles = today.roles;
        messagesSince(seq);
        const auto l = copyWith(s1, sensorsOnly);
        const QString log = messagesSince(seq);
        check(rolesOf(l->rig) == today.roles
                  && today.roles == QVariantMap{ { "W1", "leadForearm" }, { "W2", "leadHand" }, { "W3", "leadUpperArm" },
                                                 { kHm, "leadForearm" } },
              "S1a the roles the move from imu/placement gives (A, B, C; a HackMotion's bare pin as it stands), as today's "
              "loadRoles reads them");
        check(readAll(ini) == before && !ppSettings().contains("imu/roles") && !ppSettings().contains("imu/rolesMigrated"),
              "S1b the copy wrote neither imu/roles nor the marker: the settings file unchanged, byte for byte");
        check(l->events == 0 && !readJson(s1 + "/rig.json").value("groupsTaken").toObject().value("sensors").toObject().isEmpty()
                  && log.contains("keys are copied from settings") && log.contains("slot D has no role"),
              "S1c one load, no event; the group taken; the copy and the move's lines logged");
        check(keptSensorEntries(s1, "imu/roles").isEmpty(),
              "S1d the slots with no role (D, other) are dropped as the move drops them; imu/placement still holds them");
    }

    std::printf("\nS2 that machine's settings then make the move, after a claim through the Rig\n");
    {
        {
            const auto l = copyWith(s1, sensorsOnly);
            SensorFactsTable t;
            t.set(DeviceKey::fromPersistedId(u"W4"), SensorFacts{ true, true, false });
            check(l->rig.claimRole(DeviceKey::sensor("WT901BLE68", "W4"), analysis::SegmentRole::Pelvis, t).ok,
                  "S2a a claim through the Rig (W4 to the pelvis)");
        }
        {   // AppSettings::loadImuRoles' move, as it writes it on this machine
            QSettings s = ppSettings();
            s.setValue("imu/roles", migratedRoles);
            s.setValue("imu/rolesMigrated", true);
            s.sync();
        }
        reparse(ini);
        messagesSince(seq);
        const auto l = copyWith(s1, sensorsOnly);
        const QString log = messagesSince(seq);
        check(!log.contains("no longer match") && !log.contains("copied") && rolesOf(l->rig).value("W4") == "pelvis"
                  && rolesOf(l->rig).size() == 5,
              "S2b the moved keys decode to the same fingerprint: no copy again, and the Rig's claim stands");
    }

    std::printf("\nS3 aliases and the excluded list, under both sensor spellings\n");
    const QString s3 = stores.path() + "/s3/rig";
    {
        {
            QSettings s = ppSettings();
            s.clear();
            s.setValue("imu/rolesMigrated", true);
            s.setValue("imu/roles", QVariantMap{ { "W1", "pelvis" } });
            s.setValue("imu/alias", QVariantMap{ { imu_roles::aliasKey("WT901BLE68", "W1"), "Pelvis" },
                                                 { imu_roles::aliasKey("HackMotion wG3", kHm), "Wrist" },
                                                 { "NOPIPE", "no device" }, { "WT901BLE67|W2", 5 } });
            s.setValue("imu/excluded", QStringList{ "W2", kHm });
            s.sync();
        }
        reparse(ini);
        const auto l = copyWith(s3, sensorsOnly);
        check(aliasOf(l->rig, "WT901BLE68|W1") == "Pelvis" && aliasOf(l->rig, "HackMotion wG3|" + kHm) == "Wrist"
                  && l->rig.parts().other.aliases.size() == 2,
              "S3a the aliases, by description|deviceId (imu_roles::aliasKey's spelling)");
        const DeviceKey w2 = DeviceKey::sensor("WT901BLE67", "W2");   // the enumerator's key, with its description
        check(!enabledOf(l->rig, w2) && !enabledOf(l->rig, DeviceKey::sensor("HackMotion wG3", kHm))
                  && enabledOf(l->rig, DeviceKey::sensor("WT901BLE68", "W1")) && l->rig.parts().other.enabledOnThisMachine.size() == 2,
              "S3b the excluded list by bare id: a key with a description finds it (a sensor's identity is its id)");
        const QJsonObject other = readJson(s3 + "/rig.json").value("other").toObject();
        check(other.value("aliases").toArray().contains(QJsonObject{ { "sensor", "WT901BLE68|W1" }, { "alias", "Pelvis" } })
                  && other.value("enabledOnThisMachine").toArray().contains(QJsonObject{ { "sensor", "|W2" }, { "enabled", false } }),
              "S3c in rig.json: the alias under description|deviceId, the switched-off sensor under its bare id");
        const QJsonObject kept = keptSensorEntries(s3, "imu/alias");
        check(kept.value("NOPIPE").toString() == "no device" && kept.value("WT901BLE67|W2").toInt() == 5 && kept.size() == 2,
              "S3d an alias key with no '|' and an alias that is not a string: kept verbatim, matched to no sensor");
        l->rig.setEnabledOnThisMachine(w2, true);
        l->rig.setAlias(DeviceKey::sensor("WT901BLE68", "W1"), "Hips");
        check(enabledOf(l->rig, DeviceKey::fromPersistedId(u"W2")) && aliasOf(l->rig, "WT901BLE68|W1") == "Hips"
                  && l->rig.parts().other.aliases.size() == 2,
              "S3e through the Rig's commands: the copied flag switched back on by the enumerator's key, the alias renamed in place");
    }

    std::printf("\nS4 L4: every imu/roles entry copied verbatim\n");
    const QString s4 = stores.path() + "/s4/rig";
    {
        {
            QSettings s = ppSettings();
            s.clear();
            s.setValue("imu/rolesMigrated", true);
            s.setValue("imu/roles", QVariantMap{ { "W1", "pelvis" }, { kHm + "#lowerArm", "leadForearm" }, { kHm + "#palm", "leadHand" },
                                                 { "W5", "D" }, { "W6", "other" }, { "W7", "" }, { "W8", 5 },
                                                 { "W9", "trailForearm" }, { "", "thorax" } });
            s.sync();
        }
        reparse(ini);
        {
            const auto l = copyWith(s4, sensorsOnly);
            check(rolesOf(l->rig) == QVariantMap{ { "W1", "pelvis" }, { kHm + "#lowerArm", "leadForearm" }, { kHm + "#palm", "leadHand" } }
                      && l->rig.parts().state.sensors.size() == 3,
                  "S4a the three typed entries in the rig state (a Witmotion; a HackMotion's two lanes)");
        }
        auto allKept = [&](const QJsonObject &k) {
            return k.size() == 6 && k.value("W5").toString() == "D" && k.value("W6").toString() == "other" && k.value("W7").isString()
                   && k.value("W7").toString().isEmpty() && k.value("W8").toInt() == 5 && k.value("W9").toString() == "trailForearm"
                   && k.value("").toString() == "thorax";
        };
        check(allKept(keptSensorEntries(s4, "imu/roles")),
              "S4b the six it cannot hold (a slot letter, \"other\", \"\", a number, a newer build's role, an empty key) kept "
              "verbatim in rig.json");
        {
            const auto l = copyWith(s4, sensorsOnly);
            SensorFactsTable t;
            l->rig.claimRole(DeviceKey::fromPersistedId(u"W5"), analysis::SegmentRole::Thorax, t);
        }
        {
            const auto l = copyWith(s4, sensorsOnly);
            check(allKept(keptSensorEntries(s4, "imu/roles")) && rolesOf(l->rig).value("W5") == "thorax",
                  "S4c saved again verbatim after a command and a reload (W5's claim beside its kept \"D\")");
        }
        {
            QSettings s = ppSettings();
            QVariantMap r = s.value("imu/roles").toMap();
            r["W5"] = "E";
            s.setValue("imu/roles", r);
            s.sync();
        }
        reparse(ini);
        messagesSince(seq);
        {
            const auto l = copyWith(s4, sensorsOnly);
            check(messagesSince(seq).contains("copied again") && keptSensorEntries(s4, "imu/roles").value("W5").toString() == "E",
                  "S4d a kept entry changed is a change: the group is copied again, the entry kept as it now is");
        }
        check(allSixKept(keptSensorEntries(s4, "imu/roles"), "E"),
              "S4e all six kept entries survive the re-copy (W5 as it now is, the other five as they were)");
    }

    std::printf("\nS5 P1 for the sensor group\n");
    {
        const QByteArray atCopy = readAll(s4 + "/rig.json");
        const QVariant   roles  = ppSettings().value("imu/roles");
        auto launchLeavesIt = [&](const char *label) {
            reparse(ini);
            messagesSince(seq);
            const QByteArray before = readAll(s4 + "/rig.json");
            const auto l = copyWith(s4, sensorsOnly);
            const QString log = messagesSince(seq);
            check(readAll(s4 + "/rig.json") == before && !log.contains("copied") && !log.contains("no longer match")
                      && rolesOf(l->rig).size() == 3 && l->events == 0,
                  QStringLiteral("S5 %1: the Rig is left as it is, nothing written").arg(QLatin1String(label)));
        };
        {
            QSettings s = ppSettings();
            s.clear();
            s.sync();
        }
        launchLeavesIt("a no sensor key at all");
        {
            QSettings s = ppSettings();
            s.setValue("imu/rolesMigrated", true);
            s.sync();
        }
        launchLeavesIt("b the marker alone");
        {
            QSettings s = ppSettings();
            s.setValue("imu/placement", QVariantMap{ { "W1", "A" } });
            s.sync();
        }
        launchLeavesIt("c imu/placement alone on a migrated machine (it outlives the roles)");
        {
            QSettings s = ppSettings();
            s.setValue("imu/roles", roles);
            s.setValue("imu/alias", QVariantMap{ { "WT901BLE68|W1", "Renamed by an older build" } });
            s.sync();
        }
        reparse(ini);
        messagesSince(seq);
        {
            const auto l = copyWith(s4, sensorsOnly);
            check(messagesSince(seq).contains("copied again") && aliasOf(l->rig, "WT901BLE68|W1") == "Renamed by an older build",
                  "S5d a group with its keys back and its fingerprint different is copied again, as before");
        }
        Q_UNUSED(atCopy);
    }

    std::printf("\nS6 a re-copy keeps a sensor entry this build cannot read (P2)\n");
    {
        QJsonObject j = readJson(s4 + "/rig.json");
        QJsonObject state = j.value("state").toObject();
        QJsonArray sensors = state.value("sensors").toArray();
        const QJsonObject unreadable{ { "placement", "W10" }, { "role", "trailHand" } };
        sensors.append(unreadable);
        state.insert("sensors", sensors);
        j.insert("state", state);
        writeAll(s4 + "/rig.json", QJsonDocument(j).toJson());
        {
            QSettings s = ppSettings();
            s.setValue("imu/excluded", QStringList{ "W1", "W3" });
            s.sync();
        }
        reparse(ini);
        messagesSince(seq);
        {
            const auto l = copyWith(s4, sensorsOnly);
            check(messagesSince(seq).contains("copied again") && !enabledOf(l->rig, DeviceKey::fromPersistedId(u"W3")),
                  "S6a the group is copied again");
        }
        check(readJson(s4 + "/rig.json").value("state").toObject().value("sensors").toArray().contains(unreadable),
              "S6b the sensor entry this build cannot read is written back as read");
        check(allSixKept(keptSensorEntries(s4, "imu/roles"), "E"),
              "S6c and all six roles entries kept from settings survive this re-copy too");
    }

    std::printf("\nS7 the groups apart\n");
    {
        const QString s7 = stores.path() + "/s7/rig";
        const QList<RigStore::Group> camerasAndSensors{ RigStore::Group::Cameras, RigStore::Group::Sensors };
        writeFixture();   // the camera keys, with an alias, an excluded camera and the bare key's kept entries
        {
            QSettings s = ppSettings();
            s.setValue("imu/alias", QVariantMap{ { "WT901BLE68|W1", "Pelvis" } });
            s.setValue("imu/excluded", QStringList{ "W2" });
            s.sync();
        }
        reparse(ini);
        { const auto l = copyWith(s7, camerasAndSensors); }
        {
            QSettings s = ppSettings();
            s.setValue("imu/excluded", QStringList{ "W3" });
            s.sync();
        }
        reparse(ini);
        messagesSince(seq);
        const auto l = copyWith(s7, camerasAndSensors);
        const QString log = messagesSince(seq);
        bool cameraAlias = false;
        for (const auto &a : l->rig.parts().other.aliases) cameraAlias = cameraAlias || (a.first == blackfly && a.second == "Face on");
        check(lineHas(log, "sensors", "no longer match") && !lineHas(log, "cameras", "no longer match") && cameraAlias
                  && !enabledOf(l->rig, facetime) && !enabledOf(l->rig, DeviceKey::fromPersistedId(u"W3"))
                  && enabledOf(l->rig, DeviceKey::fromPersistedId(u"W2")) && aliasOf(l->rig, "WT901BLE68|W1") == "Pelvis",
              "S7a the sensors copied again (W3 off, W2 on again) leave the cameras' alias and enabled flag");
        check(readJson(s7 + "/rig.json").value("keptFromSettings").toObject().contains("camera/alias"),
              "S7b and the cameras' kept entries");
    }

    std::printf("\nS8 imu/calibration and imu/mountOrientation are not copied\n");
    {
        const QString fingerprint = RigStore::settingsFingerprint(RigStore::Group::Sensors);
        {
            QSettings s = ppSettings();
            s.setValue("imu/calibration", QVariantMap{ { "W1", QVariantMap{ { "a", 1 } } } });
            s.setValue("imu/mountOrientation", QVariantMap{ { "W1", "flipped" } });
            s.sync();
        }
        reparse(ini);
        check(RigStore::settingsFingerprint(RigStore::Group::Sensors) == fingerprint,
              "S8 the two unused keys are not part of the group: the same fingerprint");
    }

    std::printf("\nS9 settings writes by the copy\n");
    std::printf("  settings writes by the copy: %d, over %d launches that run the sensor group's copy\n", settingsWrites, sensorCopies);
    check(settingsWrites == 0 && sensorCopies >= 15, "S9 settings writes by the copy: 0");

    std::printf(g_fail ? "\nFAILURES: %d\n" : "\nALL PASS\n", g_fail);
    return g_fail ? 1 : 0;
}
