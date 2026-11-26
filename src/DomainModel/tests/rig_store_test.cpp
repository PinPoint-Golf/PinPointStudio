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

// rig_store_test: the Rig and its one file (src/DomainModel/rig.*, src/Stores/rig_store.*; information_model.md
// OBJ-RIG, FILE-APP-07; docs/design/rig_design.md §4.1, §4.4). Every case runs in a temporary folder under
// IsolatedStores (made first, #187), whose PINPOINT_RIG_DIR keeps RigStore::defaultFolder() off the machine's real
// rig folder. The real folder is only looked at (QFileInfo), never opened.
//
//   S1  a machine with no rig stored yet: an empty rig, a new RigId, rig.json written once (schema pinpoint.rig/1:
//       the id, both parts empty, no groups taken), no event
//   S2  the next launch reads the same RigId and writes nothing
//   S3  the schema is checked on read: a file that is not a pinpoint.rig file, not JSON, or has no rigId is
//       read-only to this build: an empty rig in memory, the file's bytes untouched, ppWarn
//   S4  a newer pinpoint.rig/N is read for the fields this build knows and never saved over
//   S5  the write is atomic and whole: a save over an earlier file leaves one valid file and no temporary
//   S6  a failed save keeps the state in memory, warns, and emits nothing
//   S7  a member this build does not know is written back as it was read
//   S8  both parts round-trip; a sensor's alignment is never written
//   S10 the cameras round-trip (C-5a.1): every versioned field, an unknown perspective code and a calibration record,
//       read back to the same RigStateId; and through the Rig's commands, the same after a relaunch
//   S9  the test override: defaultFolder() is PINPOINT_RIG_DIR, inside the temporary directory; without it, Qt's
//       test location, never the real one; the real folder is untouched by the run

#include "../../Core/tests/isolated_stores.h"

#include "../rig.h"
#include "../../Core/PpMessageLog.h"
#include "../../Stores/rig_store.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTimeZone>

#include <cstdio>

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

// The warnings logged since `seq`, joined.
QString warningsSince(int &seq)
{
    QStringList out;
    for (const auto &e : PpMessageLog::instance()->fetchSince(seq))
        if (e.severity == QLatin1String("WARN")) out << e.message;
    return out.join(QLatin1Char('\n'));
}

struct Launch {
    explicit Launch(const QString &folder) : store(folder), rig(store)
    {
        QObject::connect(&rig, &Rig::rigChanged, &rig, [this] { ++events; });
        rig.load();
    }
    RigStore store;
    Rig      rig;
    int      events = 0;
};

int g_case = 0;
QString caseFolder(const pinpoint::test::IsolatedStores &stores)
{
    return stores.path() + QStringLiteral("/case%1/rig").arg(++g_case);
}

} // namespace

int main(int argc, char **argv)
{
    pinpoint::test::IsolatedStores stores;
    QCoreApplication app(argc, argv);
    std::printf("rig_store_test — the temporary directory: %s\n", qPrintable(stores.path()));

    // The real folder, as a path only (test mode off for the one call, the override unset), and what is there now.
    // The app's own name, for the one call: the real folder is the APP's (<AppData> of "PinPointStudio"), not this suite's.
    const QByteArray envRig = qgetenv("PINPOINT_RIG_DIR");
    const QString    suiteName = QCoreApplication::applicationName();
    qunsetenv("PINPOINT_RIG_DIR");
    QCoreApplication::setApplicationName(QStringLiteral("PinPointStudio"));
    QStandardPaths::setTestModeEnabled(false);
    const QString realFolder = RigStore::defaultFolder();
    QStandardPaths::setTestModeEnabled(true);
    const QString testModeFolder = RigStore::defaultFolder();
    QCoreApplication::setApplicationName(suiteName);
    qputenv("PINPOINT_RIG_DIR", envRig);
    const QFileInfo realBefore(realFolder + QStringLiteral("/rig.json"));
    const bool      realExisted = realBefore.exists();
    const qint64    realSize    = realExisted ? realBefore.size() : -1;
    const QDateTime realTime    = realExisted ? realBefore.lastModified() : QDateTime();
    int seq = 0;
    warningsSince(seq);

    std::printf("\nS1 a machine with no rig stored yet\n");
    const QString f1 = caseFolder(stores);
    RigId firstId;
    {
        Launch l(f1);
        firstId = l.rig.id();
        const QJsonObject j = readJson(l.store.filePath());
        check(!firstId.isNull() && l.rig.parts().state.cameras.isEmpty() && l.rig.parts().state.sensors.isEmpty()
                  && l.rig.parts().other.aliases.isEmpty(), "S1a an empty rig with a new RigId");
        check(QFileInfo::exists(l.store.filePath()), "S1b rig.json is written at the first launch");
        check(j.value("schema").toString() == "pinpoint.rig/1" && j.value("rigId").toString() == firstId.toString(),
              "S1c its schema is pinpoint.rig/1 and it holds the RigId");
        const QJsonObject st = j.value("state").toObject(), ot = j.value("other").toObject();
        check(st.value("cameras").toArray().isEmpty() && st.value("sensors").toArray().isEmpty()
                  && ot.value("aliases").toArray().isEmpty() && ot.value("enabledOnThisMachine").toArray().isEmpty()
                  && ot.value("clubLengthPriors").toArray().isEmpty() && j.value("groupsTaken").isObject()
                  && j.value("groupsTaken").toObject().isEmpty(),
              "S1d both parts empty, and an empty list of groups taken");
        check(l.events == 0, "S1e no RigChanged for the state loaded at launch");
        check(l.rig.stateId() == rigStateIdOf(RigState{}), "S1f the state id is the empty state's digest");
    }

    std::printf("\nS2 the next launch\n");
    {
        // The same file, written compact: a launch that wrote it again (indented) would change its bytes.
        const QByteArray before = QJsonDocument(readJson(f1 + "/rig.json")).toJson(QJsonDocument::Compact);
        writeAll(f1 + "/rig.json", before);
        Launch l(f1);
        check(l.rig.id() == firstId, "S2a the same RigId is read back");
        check(readAll(f1 + "/rig.json") == before, "S2b nothing is written: the file's bytes are unchanged");
        check(l.events == 0, "S2c no event");
    }

    std::printf("\nS3 the schema is checked on read\n");
    const struct { const char *what; QByteArray bytes; } bad[] = {
        { "another schema", R"({"schema":"pinpoint.studio/1","rigId":"6f1c2b6e-0d1e-4c71-9a64-3e2b9d0c5a11"})" },
        { "not JSON", "this is not json {" },
        { "no rigId", R"({"schema":"pinpoint.rig/1","state":{"cameras":[]}})" },
        { "a schema of version 0", R"({"schema":"pinpoint.rig/0","rigId":"6f1c2b6e-0d1e-4c71-9a64-3e2b9d0c5a11"})" },
    };
    for (const auto &b : bad) {
        const QString f = caseFolder(stores);
        writeAll(f + "/rig.json", b.bytes);
        warningsSince(seq);
        Launch l(f);
        const QString warned = warningsSince(seq);
        check(l.store.isReadOnly() && !l.rig.id().isNull() && l.rig.parts().state.cameras.isEmpty()
                  && readAll(f + "/rig.json") == b.bytes && warned.contains("never saves over"),
              QStringLiteral("S3 %1: read-only, an empty rig in memory, the bytes untouched, a warning").arg(b.what));
        check(!l.store.save(l.rig.id(), l.rig.parts()) && readAll(f + "/rig.json") == b.bytes,
              QStringLiteral("S3 %1: a save is refused and writes nothing").arg(b.what));
    }

    std::printf("\nS4 a newer schema\n");
    {
        const QString f = caseFolder(stores);
        const QByteArray newer = R"({"schema":"pinpoint.rig/2","rigId":"6f1c2b6e-0d1e-4c71-9a64-3e2b9d0c5a11",)"
                                 R"("state":{"cameras":[{"key":"Blackfly S|0123","perspective":2,"mirrored":true,)"
                                 R"("fixedInPlace":false,"calibrations":[],"lens":"a field of version 2"}],"sensors":[]},)"
                                 R"("other":{},"somethingNew":[1,2,3]})";
        writeAll(f + "/rig.json", newer);
        warningsSince(seq);
        Launch l(f);
        const QString warned = warningsSince(seq);
        check(l.rig.id() == RigId::fromString(u"6f1c2b6e-0d1e-4c71-9a64-3e2b9d0c5a11")
                  && l.rig.parts().state.cameras.size() == 1 && l.rig.parts().state.cameras.first().mirrored
                  && l.rig.parts().state.cameras.first().perspectiveCode() == 2,
              "S4a the fields this build knows are read (the RigId, the camera)");
        check(l.store.isReadOnly() && warned.contains("pinpoint.rig/2") && warned.contains("read-only"),
              "S4b the file is read-only to this build, and a warning says so");
        check(readAll(f + "/rig.json") == newer, "S4c the launch did not save over it");
        check(!l.store.save(l.rig.id(), l.rig.parts()) && readAll(f + "/rig.json") == newer,
              "S4d a save is refused: the file is never saved over");
        check(l.events == 0, "S4e no event");
    }

    std::printf("\nS5 the write is atomic and whole\n");
    {
        const QString f = caseFolder(stores);
        Launch l(f);
        RigParts parts = l.rig.parts();
        parts.other.aliases.append({ DeviceKey::camera("Blackfly S", "0123", "id-1"), "Down the line" });
        const bool ok = l.store.save(l.rig.id(), parts);
        const QStringList left = QDir(f).entryList(QDir::Files | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
        const QJsonObject j = readJson(f + "/rig.json");
        check(ok && left == QStringList{ "rig.json" }, "S5a one file is left, rig.json, and no temporary beside it");
        check(j.value("other").toObject().value("aliases").toArray().size() == 1
                  && j.value("rigId").toString() == l.rig.id().toString(),
              "S5b it is the whole new file");
    }

    std::printf("\nS6 a failed save\n");
    {
        const QString blocker = stores.path() + QStringLiteral("/case-blocked");
        writeAll(blocker, "a file where the rig folder should be");
        warningsSince(seq);
        Launch l(blocker + "/rig");
        const QString warned = warningsSince(seq);
        check(!l.rig.id().isNull() && l.rig.parts().state.cameras.isEmpty(), "S6a the rig in memory stands (its new RigId)");
        check(warned.contains("[RigStore] not saved") && warned.contains("the rig in memory stands"),
              "S6b the failed save is logged with ppWarn");
        check(l.events == 0, "S6c no event");
        check(!QFileInfo::exists(blocker + "/rig/rig.json") && readAll(blocker) == "a file where the rig folder should be",
              "S6d nothing was written, and the file in the way is untouched");
    }

    std::printf("\nS7 a member this build does not know\n");
    {
        const QString f = caseFolder(stores);
        writeAll(f + "/rig.json",
                 R"({"schema":"pinpoint.rig/1","rigId":"0d6c1f7a-1b2c-4d3e-8f90-a1b2c3d4e5f6","futureTop":{"a":1},)"
                 R"("state":{"sinceUtc":"2026-11-26T10:00:00.000Z","cameras":[],"sensors":[],"futureState":"kept"},)"
                 R"("other":{"aliases":[],"futureOther":true}})");
        Launch l(f);
        check(l.store.save(l.rig.id(), l.rig.parts()), "S7a a /1 file with extra members is writable");
        const QJsonObject j = readJson(f + "/rig.json");
        check(j.value("futureTop").toObject().value("a").toInt() == 1
                  && j.value("state").toObject().value("futureState").toString() == "kept"
                  && j.value("other").toObject().value("futureOther").toBool(),
              "S7b each unknown member is written back as it was read");
        check(l.rig.parts().state.sinceUtc == QDateTime(QDate(2026, 11, 26), QTime(10, 0), QTimeZone::UTC),
              "S7c sinceUtc is read back");
    }

    std::printf("\nS8 both parts round-trip; an alignment is never written\n");
    {
        const QString f = caseFolder(stores);
        RigId id;
        RigParts parts;
        {
            Launch l(f);
            id    = l.rig.id();
            parts = l.rig.parts();
        }
        const DeviceKey cam = DeviceKey::camera("Blackfly S", "", "usb-7");
        const DeviceKey imu = DeviceKey::sensor("WT901BLE68", "{767a6a67-0000-4000-8000-000000000001}");
        SensorSetup s;
        s.key  = PlacementKey(imu);
        s.role = analysis::SegmentRole::Pelvis;
        s.calibration = CalibrationRecord::forSensor(s.key, CalibrationMethod::Solved, SensorAlignmentParameters{},
                                                     Uncertainty::notKnown(), QDateTime::currentDateTimeUtc());
        parts.state.sensors.append(s);
        parts.other.aliases.append({ imu, "Pelvis sensor" });
        parts.other.enabledOnThisMachine.append({ cam, false });
        parts.other.clubLengthPriors.append(
            { ClubLengthPriorKey(AthleteId::fromString(u"11111111-2222-4333-8444-555555555555"), "7i", cam),
              QVariantMap{ { "lengthM", 0.94 }, { "n", 12 } } });
        RigStore store(f);
        store.load();
        check(store.save(id, parts), "S8a saved");
        const QByteArray bytes = readAll(f + "/rig.json");
        check(!bytes.contains("calibration\"") && !bytes.contains("alignment") && !bytes.contains("Solved")
                  && !bytes.contains("solved"),
              "S8b the sensor's alignment record is not in the file");
        RigStore again(f);
        const RigLoad back = again.load();
        check(back.id == id && back.parts.state.sensors.size() == 1 && back.parts.state.sensors.first().key == s.key
                  && back.parts.state.sensors.first().role == analysis::SegmentRole::Pelvis
                  && !back.parts.state.sensors.first().calibration,
              "S8c the sensor's role is read back, with no alignment");
        check(back.parts.other.aliases.size() == 1 && back.parts.other.aliases.first().first == imu
                  && back.parts.other.aliases.first().first.description() == "WT901BLE68"
                  && back.parts.other.aliases.first().second == "Pelvis sensor"
                  && back.parts.other.enabledOnThisMachine.size() == 1
                  && back.parts.other.enabledOnThisMachine.first().first == cam
                  && !back.parts.other.enabledOnThisMachine.first().second
                  && back.parts.other.clubLengthPriors.size() == 1
                  && back.parts.other.clubLengthPriors.first().first == parts.other.clubLengthPriors.first().first
                  && back.parts.other.clubLengthPriors.first().second.value("lengthM").toDouble() == 0.94,
              "S8d the other facts are read back, each device by its own spelling");
    }

    std::printf("\nS10 the cameras round-trip\n");
    {
        const QString f = caseFolder(stores);
        RigStore store(f);
        store.load();
        const DeviceKey a = DeviceKey::camera("Blackfly S BFS-U3-16S2M", "24157001", "usb-1");
        const DeviceKey b = DeviceKey::camera("FaceTime HD Camera", "", "0x1420000005ac8600");
        RigParts parts;
        CameraSetup ca(a);
        ca.setPerspective(recording::Perspective::Impact);
        ca.mirrored     = true;
        ca.fixedInPlace = true;
        ca.hittingArea  = QRectF(0.1 + 0.2, 1.0 / 3.0, 0.125, 2.0 / 7.0);   // values with no short decimal form
        CalibrationRecord rec = CalibrationRecord::forCamera(
            a, CalibrationKind::Extrinsics, CalibrationMethod::Assumed, NamedValues{ { "yawDeg", 0.0 }, { "pitchDeg", -1.5 } },
            Uncertainty::sigmas(NamedValues{ { "yawDeg", 3.0 } }), QDateTime(QDate(2026, 11, 26), QTime(9, 30, 1, 250), QTimeZone::UTC));
        rec.evidence = CalibrationEvidence{ SessionId::fromString(u"aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee"), "extrinsics_run.json" };
        ca.calibrations.append(rec);
        ca.calibrations.append(CalibrationRecord::forCamera(a, CalibrationKind::Focus, CalibrationMethod::UserMeasured, {},
                                                            Uncertainty::notKnown(), QDateTime()));
        CameraSetup cb(b);
        cb.setPerspectiveCode(9);   // a code this build does not know
        cb.hittingArea = QRectF(0.5, 0.5, 0.25, 0.25);   // a box on a camera that is not fixed: kept
        parts.state.cameras = { ca, cb };
        parts.state.sinceUtc = QDateTime(QDate(2026, 11, 26), QTime(9, 31, 0, 5), QTimeZone::UTC);
        const RigId id = RigId::create();
        check(store.save(id, parts), "S10a saved");
        RigStore again(f);
        const RigLoad back = again.load();
        check(back.source == RigLoad::Source::File && back.id == id && back.parts.state.cameras.size() == 2,
              "S10b both cameras are read back");
        check(rigStateIdOf(back.parts.state) == rigStateIdOf(parts.state),
              "S10c the same RigStateId: every versioned field and both records came back exactly");
        const CameraSetup *bb = nullptr;
        const CameraSetup *ba = nullptr;
        for (const CameraSetup &c : back.parts.state.cameras) (c.key() == b ? bb : ba) = &c;
        check(bb && bb->perspectiveCode() == 9 && bb->perspective() == recording::Perspective::None && !bb->fixedInPlace
                  && bb->hittingArea == QRectF(0.5, 0.5, 0.25, 0.25),
              "S10d the unknown code 9 is kept, and the box of a camera not fixed");
        check(ba && ba->calibrations.size() == 2 && ba->calibrations.first().evidence
                  && ba->calibrations.first().evidence->fileName == "extrinsics_run.json"
                  && !ba->calibrations.last().uncertainty.isKnown() && back.parts.state.sinceUtc == parts.state.sinceUtc,
              "S10e the record's evidence (outside the digest), a not-known uncertainty and sinceUtc are read back");
        {
            Launch l(caseFolder(stores));
            l.rig.assignPerspective(a, 2);
            l.rig.setHittingArea(a, QRectF(0.2, 0.3, 0.4, 0.5));
            l.rig.setAlias(b, "Impact strip");
            const RigRef ref = l.rig.ref();
            Launch relaunch(l.store.folder());
            check(relaunch.rig.ref() == ref && relaunch.rig.parts().other.aliases.size() == 1 && relaunch.events == 0,
                  "S10f through the commands: a relaunch gives the same RigRef and alias, with no event");
        }
    }

    std::printf("\nS9 the test override\n");
    {
        const QString d = RigStore::defaultFolder();
        std::printf("  defaultFolder() = %s\n  without the override, test mode: %s\n  the real folder: %s\n",
                    qPrintable(d), qPrintable(testModeFolder), qPrintable(realFolder));
        check(d == QDir::cleanPath(QString::fromLocal8Bit(envRig)) && stores.contains(d),
              "S9a defaultFolder() is PINPOINT_RIG_DIR, set by IsolatedStores, inside the temporary directory");
        check(QDir::cleanPath(testModeFolder) != QDir::cleanPath(realFolder),
              "S9b without the override, it is Qt's test location, not the real folder");
        Launch l(d);   // a rig made exactly as the composition root makes one, over the default folder
        check(stores.contains(l.store.filePath()) && QFileInfo::exists(l.store.filePath()),
              "S9c a rig over the default folder writes inside the temporary directory");
        const QFileInfo realAfter(realFolder + QStringLiteral("/rig.json"));
        check(realAfter.exists() == realExisted
                  && (!realExisted || (realAfter.size() == realSize && realAfter.lastModified() == realTime)),
              QStringLiteral("S9d the real folder's rig.json is %1 after the run, as before")
                  .arg(realExisted ? "unchanged" : "absent"));
    }

    std::printf(g_fail ? "\nFAILURES: %d\n" : "\nALL PASS\n", g_fail);
    return g_fail ? 1 : 0;
}
