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
//   S3  the schema is checked on read: a file this build cannot read at all (not a pinpoint.rig file, not JSON, no
//       rigId) is renamed rig.json.unreadable-<UTC time>, byte for byte, and the run starts as on a machine with
//       nothing stored: a new RigId, a new rig.json, saving as normal, ppWarn (P3); two in one second keep both; when
//       the rename fails, read-only as before: never saved over
//   S4  a newer pinpoint.rig/N is read for the fields this build knows and never saved over, nor moved aside, even
//       with a rigId this build cannot read (P3)
//   S5  the write is atomic and whole: a save over an earlier file leaves one valid file and no temporary
//   S6  a failed save keeps the state in memory, warns, and emits nothing
//   S7  a member this build does not know is written back as it was read
//   S8  both parts round-trip; a sensor's alignment is never written
//   S11 entries this build cannot read (P2): a camera entry with no key, a calibration record of an unknown kind or
//       method, and a sensor whose role this build does not know are kept out of the rig state and its digest, and
//       written back exactly as read at every save; a camera that holds only such records stays out of the state
//   S12 what this build cannot read, anywhere (the Solution Architect, 27 Nov 2026, widening P2; `[^rig-unknown]`): an
//       entry of the other facts whose device it cannot read (a phone's alias and enabled flag, a prior with no
//       face-on camera) is kept as read, and a field it does not know inside an entry it reads (a camera entry, a
//       record, a sensor, an alias, an enabled flag, a prior) is written back as read, at every save; a field it knows
//       and clears (the box) is not written back from the file; the digest ignores all of it
//   S10 the cameras round-trip (C-5a.1): every versioned field, an unknown perspective code and a calibration record,
//       read back to the same RigStateId; and through the Rig's commands, the same after a relaunch
//   S13 the assumed extrinsics records (C-7b) in rig.json: a file from before them gains them at the first launch,
//       written with each camera's other records (extrinsics, assumed, yawDeg 0, the sigma, no time observed), with no
//       event; the next launch reads them to the same RigStateId and writes nothing; a later save writes each back over
//       itself unchanged; a camera that holds only unreadable records and every default fact stays out of the state
//       and is written back exactly as read, with no record added; a down-the-line one keeps its unreadable record
//       beside its assumed one; a fact cleared removes its record from the file; a save that leaves the facts as they
//       were writes the whole file back byte for byte; an all-default camera entry with no record, as read, is not
//       touched by the derivation at the launch (no save, the entry and its unknown field kept)
//   S9  the test override: defaultFolder() is PINPOINT_RIG_DIR, inside the temporary directory; without it, Qt's
//       test location, never the real one; the real folder is untouched by the run

#include "../../Core/tests/isolated_stores.h"

#include "../rig.h"
#include "../../Core/PpMessageLog.h"
#include "../../Stores/rig_store.h"
#include "../../Vocabulary/Rig/camera_assumptions.h"   // the yaw sigma the analysis reads (S13)

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimeZone>

#include <algorithm>
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
    // The files beside rig.json named rig.json.unreadable-<UTC time> (P3).
    const auto asides = [](const QString &folder) {
        return QDir(folder).entryList(QStringList{ QStringLiteral("rig.json.unreadable-*") }, QDir::Files, QDir::Name);
    };
    for (const auto &b : bad) {
        const QString f = caseFolder(stores);
        writeAll(f + "/rig.json", b.bytes);
        warningsSince(seq);
        Launch l(f);
        const QString warned = warningsSince(seq);
        const QStringList kept = asides(f);
        static const QRegularExpression stamp(QStringLiteral(R"(^rig\.json\.unreadable-\d{8}T\d{6}Z(-\d+)?$)"));
        check(kept.size() == 1 && stamp.match(kept.value(0)).hasMatch() && readAll(f + "/" + kept.value(0)) == b.bytes,
              QStringLiteral("S3 %1: renamed rig.json.unreadable-<UTC time>, its bytes kept exactly").arg(b.what));
        const QJsonObject j = readJson(f + "/rig.json");
        check(!l.store.isReadOnly() && !l.rig.id().isNull() && l.rig.parts().state.cameras.isEmpty()
                  && j.value("schema").toString() == "pinpoint.rig/1" && j.value("rigId").toString() == l.rig.id().toString(),
              QStringLiteral("S3 %1: the run starts as on a machine with nothing stored: a new RigId in a new rig.json").arg(b.what));
        check(warned.contains("cannot read") && warned.contains(kept.value(0)),
              QStringLiteral("S3 %1: a warning names the file kept").arg(b.what));
        check(l.store.save(l.rig.id(), l.rig.parts()) && l.events == 0,
              QStringLiteral("S3 %1: it saves as normal; no event").arg(b.what));
    }
    {   // Two unreadable files in one second: both kept, under two names.
        const QString f = caseFolder(stores);
        writeAll(f + "/rig.json", "first junk");
        { Launch l(f); }
        writeAll(f + "/rig.json", "second junk");
        { Launch l(f); }
        const QStringList kept = asides(f);
        check(kept.size() == 2 && ((readAll(f + "/" + kept.value(0)) == "first junk" && readAll(f + "/" + kept.value(1)) == "second junk")
                                   || (readAll(f + "/" + kept.value(1)) == "first junk" && readAll(f + "/" + kept.value(0)) == "second junk")),
              "S3e two unreadable files in a row: both kept, neither overwritten");
    }
#ifdef Q_OS_WIN
    std::printf("  [SKIP] S3f the rename fails: a read-only folder does not stop a rename on Windows; covered on the Mac and Linux\n");
#else
    {   // The rename fails (a folder that cannot be written): read-only as before, never saved over.
        const QString f = caseFolder(stores);
        const QByteArray junk = "not json, in a folder this run cannot write";
        writeAll(f + "/rig.json", junk);
        QFile::setPermissions(f, QFileDevice::ReadOwner | QFileDevice::ExeOwner);
        warningsSince(seq);
        bool readOnly = false, refused = false;
        {
            Launch l(f);
            readOnly = l.store.isReadOnly();
            refused  = !l.store.save(l.rig.id(), l.rig.parts());
        }
        const QString warned = warningsSince(seq);
        QFile::setPermissions(f, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        check(readOnly && refused && readAll(f + "/rig.json") == junk && asides(f).isEmpty() && warned.contains("never saves over"),
              "S3f the rename fails: read-only, as before; the file untouched, never saved over, and a warning");
    }
#endif

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
        check(QDir(f).entryList(QStringList{ QStringLiteral("rig.json.unreadable-*") }, QDir::Files).isEmpty(),
              "S4f a newer schema's file is never moved aside");
    }
    {
        const QString f = caseFolder(stores);
        const QByteArray newer = R"({"schema":"pinpoint.rig/4","rigId":"a rigId this build cannot read","state":{}})";
        writeAll(f + "/rig.json", newer);
        Launch l(f);
        check(l.store.isReadOnly() && readAll(f + "/rig.json") == newer
                  && QDir(f).entryList(QStringList{ QStringLiteral("rig.json.unreadable-*") }, QDir::Files).isEmpty(),
              "S4g a newer schema with a rigId this build cannot read: read-only, kept where it is, not moved aside");
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

    std::printf("\nS11 entries this build cannot read (P2)\n");
    {
        const QString f = caseFolder(stores);
        const QByteArray file =
            R"({"schema":"pinpoint.rig/1","rigId":"2b5e8d14-7c3a-4f6e-9a1d-0c4b7e2f9a63",)"
            R"("state":{"sinceUtc":"2026-11-26T10:00:00.000Z","cameras":[)"
            R"({"key":"Blackfly S|0123","perspective":2,"mirrored":false,"fixedInPlace":false,"calibrations":[)"
            R"({"subject":{"camera":"Blackfly S|0123"},"kind":"thermalDrift","method":"solved","parameters":{"k":1},"uncertainty":null,"observedAtUtc":null}]},)"
            R"({"key":"FaceTime HD Camera|0x14","perspective":0,"mirrored":false,"fixedInPlace":false,"calibrations":[)"
            R"({"subject":{"camera":"FaceTime HD Camera|0x14"},"kind":"extrinsics","method":"laserTracker","parameters":{},"uncertainty":null,"observedAtUtc":null}]},)"
            R"({"perspective":1,"note":"an entry with no key"}],)"
            R"("sensors":[{"placement":"{dev-1}","role":"leftKnee"},{"placement":"{dev-2}","role":"pelvis"}]},)"
            R"("other":{"aliases":[]}})";
        writeAll(f + "/rig.json", file);
        const QJsonObject original = readJson(f + "/rig.json");
        const QJsonArray  origCams = original.value("state").toObject().value("cameras").toArray();
        const QJsonArray  origSens = original.value("state").toObject().value("sensors").toArray();
        const DeviceKey   blackfly = DeviceKey::fromPersistedCameraKey(u"Blackfly S|0123");
        RigState expected;
        CameraSetup b(blackfly);
        b.setPerspectiveCode(2);
        expected.cameras = { b };
        SensorSetup pelvis;
        pelvis.key  = PlacementKey::fromPersisted(u"{dev-2}");
        pelvis.role = analysis::SegmentRole::Pelvis;
        expected.sensors = { pelvis };
        RigStateId idAtLoad;
        {
            Launch l(f);
            idAtLoad = l.rig.stateId();
            check(l.rig.parts().state.cameras.size() == 1 && l.rig.parts().state.cameras.first().key() == blackfly
                      && l.rig.parts().state.cameras.first().calibrations.isEmpty()
                      && l.rig.parts().state.sensors.size() == 1
                      && l.rig.parts().state.sensors.first().role == analysis::SegmentRole::Pelvis,
                  "S11a the rig state holds only what this build reads (one camera, one sensor; no unknown record)");
            check(l.rig.stateId() == rigStateIdOf(expected),
                  "S11b the entries it cannot read are outside the digest");
            l.rig.setMirrored(blackfly, true);
        }
        const QJsonObject saved = readJson(f + "/rig.json");
        const QJsonArray  cams  = saved.value("state").toObject().value("cameras").toArray();
        const QJsonArray  sens  = saved.value("state").toObject().value("sensors").toArray();
        QJsonObject bf, ft;
        bool noKeyKept = false;
        for (const QJsonValue &v : cams) {
            const QJsonObject o = v.toObject();
            if (o.value("key").toString() == "Blackfly S|0123") bf = o;
            else if (o.value("key").toString() == "FaceTime HD Camera|0x14") ft = o;
            else if (o == origCams.at(2).toObject()) noKeyKept = true;
        }
        check(bf.value("mirrored").toBool()
                  && bf.value("calibrations").toArray().contains(origCams.at(0).toObject().value("calibrations").toArray().at(0)),
              "S11c after a command's save: the record of an unknown kind is written back in its camera's entry, as read");
        check(ft.value("calibrations").toArray() == origCams.at(1).toObject().value("calibrations").toArray(),
              "S11d the record of an unknown method is written back, on its camera that holds nothing else");
        check(noKeyKept, "S11e the camera entry with no key is written back as read");
        check(sens.contains(origSens.at(0)) && sens.size() == 2, "S11f the sensor whose role this build does not know is written back as read");
        {
            Launch l(f);
            RigState mirrored = expected;
            mirrored.cameras.first().mirrored = true;
            check(l.rig.stateId() == rigStateIdOf(mirrored) && l.rig.parts().state.cameras.size() == 1,
                  "S11g a reload reads the same state: the kept entries still outside it");
            l.rig.setMirrored(blackfly, false);
        }
        const QJsonObject again = readJson(f + "/rig.json");
        check(again.value("state").toObject().value("cameras").toArray().size() == 3
                  && again.value("state").toObject().value("sensors").toArray().contains(origSens.at(0)),
              "S11h and a second save writes them back again");
        check(idAtLoad == rigStateIdOf(expected), "S11i (the state id at the first load)");
    }

    std::printf("\nS12 what this build cannot read, anywhere\n");
    {
        const QString f = caseFolder(stores);
        const QByteArray file =
            R"({"schema":"pinpoint.rig/1","rigId":"4c2a9e71-5b3d-4e8f-a1c6-7d9e0b2f4a58",)"
            R"("state":{"sinceUtc":"2026-11-26T10:00:00.000Z","cameras":[)"
            R"({"key":"Blackfly S|0123","perspective":2,"mirrored":false,"fixedInPlace":true,)"
            R"("hittingArea":{"x":0.25,"y":0.5,"w":0.125,"h":0.25},"laterField":{"lens":"8mm"},"calibrations":[)"
            R"({"subject":{"camera":"Blackfly S|0123"},"kind":"extrinsics","method":"assumed","parameters":{"yawDeg":0},)"
            R"("uncertainty":null,"observedAtUtc":null,"laterRecordField":1}]}],)"
            R"("sensors":[{"placement":"{dev-2}","role":"pelvis","laterSensorField":true}]},)"
            R"("other":{"aliases":[{"camera":"Blackfly S|0123","alias":"Face on","laterAliasField":7},)"
            R"({"phone":"P-123","alias":"My phone"}],)"
            R"("enabledOnThisMachine":[{"phone":"P-123","enabled":false},)"
            R"({"camera":"FaceTime HD Camera|0x14","enabled":false,"laterEnabledField":"x"}],)"
            R"("clubLengthPriors":[{"athlete":"11111111-2222-4333-8444-555555555555","club":"7i",)"
            R"("faceOnCamera":"Blackfly S|0123","value":{"lengthM":0.94},"laterPriorField":2},)"
            R"({"athlete":"11111111-2222-4333-8444-555555555555","club":"PW","faceOnCamera":"","value":{"lengthM":0.9}}]}})";
        writeAll(f + "/rig.json", file);
        const QJsonObject original = readJson(f + "/rig.json");
        const DeviceKey   blackfly = DeviceKey::fromPersistedCameraKey(u"Blackfly S|0123");
        RigStateId atLoad;
        {
            Launch l(f);
            atLoad = l.rig.stateId();
            check(l.rig.parts().other.aliases.size() == 1 && l.rig.parts().other.enabledOnThisMachine.size() == 1
                      && l.rig.parts().other.clubLengthPriors.size() == 1,
                  "S12a only what this build reads is in the Rig's other facts");
            l.rig.setMirrored(blackfly, true);   // one command, one save
        }
        const QJsonObject saved = readJson(f + "/rig.json");
        const QJsonObject st = saved.value("state").toObject(), ot = saved.value("other").toObject();
        const QJsonObject cam = st.value("cameras").toArray().at(0).toObject();
        const QJsonObject rec = cam.value("calibrations").toArray().at(0).toObject();
        const QJsonObject sen = st.value("sensors").toArray().at(0).toObject();
        const QJsonArray  aliases = ot.value("aliases").toArray(), enabled = ot.value("enabledOnThisMachine").toArray(),
                          priors = ot.value("clubLengthPriors").toArray();
        const QJsonArray  origOtherAliases = original.value("other").toObject().value("aliases").toArray();
        const QJsonArray  origEnabled = original.value("other").toObject().value("enabledOnThisMachine").toArray();
        const QJsonArray  origPriors = original.value("other").toObject().value("clubLengthPriors").toArray();
        check(aliases.contains(origOtherAliases.at(1)) && enabled.contains(origEnabled.at(0)) && priors.contains(origPriors.at(1)),
              "S12b an alias, an enabled flag and a prior whose device this build cannot read are kept as read (B1)");
        check(cam.value("mirrored").toBool() && cam.value("laterField").toObject().value("lens").toString() == "8mm",
              "S12c a field this build does not know inside a camera entry is written back, beside the command's change (B2)");
        check(rec.value("laterRecordField").toInt() == 1, "S12d ... inside a calibration record");
        check(sen.value("laterSensorField").toBool(), "S12e ... inside a sensor entry");
        bool aliasField = false, enabledField = false, priorField = false;
        for (const QJsonValue &v : aliases) aliasField = aliasField || v.toObject().value("laterAliasField").toInt() == 7;
        for (const QJsonValue &v : enabled) enabledField = enabledField || v.toObject().value("laterEnabledField").toString() == "x";
        for (const QJsonValue &v : priors) priorField = priorField || v.toObject().value("laterPriorField").toInt() == 2;
        check(aliasField && enabledField && priorField, "S12f ... inside an alias, an enabled flag and a prior");
        {
            Launch l(f);
            check(l.rig.parts().other.aliases.size() == 1, "S12g a reload reads the same facts");
            l.rig.clearHittingArea(blackfly);
            l.rig.setAlias(blackfly, "Renamed");
            check(l.rig.stateId() != atLoad, "S12h (the state changed)");
        }
        const QJsonObject again = readJson(f + "/rig.json");
        const QJsonObject cam2 = again.value("state").toObject().value("cameras").toArray().at(0).toObject();
        bool renamedKeepsField = false;
        for (const QJsonValue &v : again.value("other").toObject().value("aliases").toArray())
            if (v.toObject().value("alias").toString() == "Renamed") renamedKeepsField = v.toObject().value("laterAliasField").toInt() == 7;
        check(!cam2.contains("hittingArea") && cam2.value("laterField").isObject(),
              "S12i a field this build knows and clears (the box) is not written back from the file; the unknown one is");
        check(renamedKeepsField && again.value("other").toObject().value("aliases").toArray().contains(origOtherAliases.at(1)),
              "S12j an alias renamed keeps its unknown field; the unreadable alias is still there");
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

    std::printf("\nS13 the assumed records in rig.json\n");
    {
        // A file as the build before the records wrote it: a fixed face-on camera, a down-the-line camera, and two
        // cameras that hold only records this build cannot read (one with every fact at its default, which stays out of
        // the state; one down the line, which is in it).
        const QString f = caseFolder(stores);
        const QByteArray file =
            R"({"schema":"pinpoint.rig/1","rigId":"6d1f3a8e-2c4b-4d7e-9f0a-1b2c3d4e5f60",)"
            R"("state":{"sinceUtc":"2026-11-27T09:00:00.000Z","cameras":[)"
            R"({"key":"Blackfly S|0123","perspective":2,"mirrored":false,"fixedInPlace":true,"calibrations":[]},)"
            R"({"key":"Blackfly S|0456","perspective":1,"mirrored":true,"fixedInPlace":false,"calibrations":[]},)"
            R"({"key":"Cam Q|7","perspective":0,"mirrored":false,"fixedInPlace":false,"calibrations":[)"
            R"({"subject":{"camera":"Cam Q|7"},"kind":"extrinsics","method":"laserTracker","parameters":{},"uncertainty":null,"observedAtUtc":null}]},)"
            R"({"key":"Cam R|8","perspective":1,"mirrored":false,"fixedInPlace":false,"calibrations":[)"
            R"({"subject":{"camera":"Cam R|8"},"kind":"thermalDrift","method":"solved","parameters":{},"uncertainty":null,"observedAtUtc":null}]}],)"
            R"("sensors":[]},"other":{"aliases":[]}})";
        writeAll(f + "/rig.json", file);
        const QJsonArray origCams = readJson(f + "/rig.json").value("state").toObject().value("cameras").toArray();
        const DeviceKey faceOn = DeviceKey::fromPersistedCameraKey(u"Blackfly S|0123");
        const DeviceKey dtl    = DeviceKey::fromPersistedCameraKey(u"Blackfly S|0456");
        const DeviceKey camQ   = DeviceKey::fromPersistedCameraKey(u"Cam Q|7");
        const DeviceKey camR   = DeviceKey::fromPersistedCameraKey(u"Cam R|8");
        auto entryOf = [](const QJsonObject &root, const QString &key) {
            for (const QJsonValue &v : root.value("state").toObject().value("cameras").toArray())
                if (v.toObject().value("key").toString() == key) return v.toObject();
            return QJsonObject();
        };
        auto recordsOf = [&](const QJsonObject &root, const QString &key) { return entryOf(root, key).value("calibrations").toArray(); };
        // Rulings 2 and 4 (the Solution Architect, 27 Nov 2026): yaw, pitch and roll 0 on both; the face-on camera's
        // sigma 0 on each, the down-the-line camera's on the yaw only.
        auto isAssumedJson = [](const QJsonValue &v, const QString &key, const QJsonObject &sigmas) {
            const QJsonObject r = v.toObject();
            return r.value("subject").toObject() == QJsonObject{ { "camera", key } } && r.value("kind").toString() == "extrinsics"
                   && r.value("method").toString() == "assumed"
                   && r.value("parameters").toObject() == QJsonObject{ { "yawDeg", 0.0 }, { "pitchDeg", 0.0 }, { "rollDeg", 0.0 } }
                   && r.value("uncertainty").toObject() == sigmas && r.value("observedAtUtc").isNull() && !r.contains("evidence");
        };
        const QJsonObject squareSigmas{ { "yawDeg", 0.0 }, { "pitchDeg", 0.0 }, { "rollDeg", 0.0 } };
        const QJsonObject dtlSigma{ { "yawDeg", pinpoint::rig::kDtlYawSigmaUncalDeg } };
        RigStateId first;
        {
            Launch l(f);
            first = l.rig.stateId();
            const CameraSetup *q = nullptr, *r = nullptr;
            for (const CameraSetup &c : l.rig.parts().state.cameras) {
                if (c.key() == camQ) q = &c;
                if (c.key() == camR) r = &c;
            }
            check(!q, "S13a a camera holding only a record this build cannot read, every fact at its default, stays out "
                      "of the state: no assumed record makes it appear");
            check(r && r->calibrations.size() == 1 && r->calibrations.first().method == CalibrationMethod::Assumed,
                  "S13b a down-the-line camera holding only an unreadable record gains its assumed record in the state");
            check(l.events == 0, "S13c the records derived at the launch emit nothing");
        }
        const QByteArray afterFirst = readAll(f + "/rig.json");
        const QJsonObject saved = QJsonDocument::fromJson(afterFirst).object();
        check(recordsOf(saved, "Blackfly S|0123").size() == 1 && isAssumedJson(recordsOf(saved, "Blackfly S|0123").at(0), "Blackfly S|0123", squareSigmas),
              "S13d rig.json holds the fixed face-on camera's square record: extrinsics, assumed, yaw/pitch/roll 0, each sigma 0, no time");
        check(recordsOf(saved, "Blackfly S|0456").size() == 1
                  && isAssumedJson(recordsOf(saved, "Blackfly S|0456").at(0), "Blackfly S|0456", dtlSigma),
              "S13e ... and the down-the-line camera's: yaw/pitch/roll 0, a sigma on the yaw only, the analysis's constant");
        check(entryOf(saved, "Cam Q|7") == origCams.at(2).toObject(),
              "S13f the camera with only an unreadable record is written back exactly as read, with no assumed record");
        const QJsonArray rRecs = recordsOf(saved, "Cam R|8");
        check(rRecs.size() == 2 && rRecs.contains(origCams.at(3).toObject().value("calibrations").toArray().at(0))
                  && std::any_of(rRecs.begin(), rRecs.end(), [&](const QJsonValue &v) { return isAssumedJson(v, "Cam R|8", dtlSigma); }),
              "S13g the down-the-line camera keeps its unreadable record, as read, beside its assumed one");
        {
            Launch l(f);
            check(readAll(f + "/rig.json") == afterFirst && l.rig.stateId() == first && l.events == 0,
                  "S13h the next launch reads the records back to the same RigStateId and writes nothing");
            l.rig.setAlias(dtl, "Down the line");   // two saves that change no record, and leave the facts as they were
            l.rig.setAlias(dtl, QString());
        }
        const QJsonObject aliased = readJson(f + "/rig.json");
        check(recordsOf(aliased, "Blackfly S|0123") == recordsOf(saved, "Blackfly S|0123")
                  && recordsOf(aliased, "Blackfly S|0456") == recordsOf(saved, "Blackfly S|0456")
                  && recordsOf(aliased, "Cam R|8") == recordsOf(saved, "Cam R|8"),
              "S13i a later save writes each record back over itself, unchanged (merge over the file as read)");
        check(readAll(f + "/rig.json") == afterFirst,
              "S13l ... and the whole file, byte for byte, once the alias set and cleared leaves the facts as they were");
        {
            Launch l(f);
            check(l.rig.stateId() == first, "S13j and a relaunch after it reads the same RigStateId");
            l.rig.setFixedInPlace(faceOn, false);
        }
        const QJsonObject unfixed = readJson(f + "/rig.json");
        check(recordsOf(unfixed, "Blackfly S|0123").isEmpty() && recordsOf(unfixed, "Blackfly S|0456").size() == 1,
              "S13k the face-on camera no longer fixed: its record is gone from rig.json; the other's stays");
    }
    {
        // A camera entry with every fact at its default and no record (no copy from settings writes one; a later
        // build's, or a hand edit) is in the state as read. The records' derivation at the launch must not touch it:
        // the launch writes nothing, and the entry, with a field this build does not know, stays.
        const QString f = caseFolder(stores);
        const QByteArray file =
            R"({"schema":"pinpoint.rig/1","rigId":"7e2a4b9c-3d5f-4a6b-8c7d-9e0f1a2b3c4d",)"
            R"("state":{"sinceUtc":"2026-11-27T09:00:00.000Z","cameras":[)"
            R"({"key":"Cam S|9","perspective":0,"mirrored":false,"fixedInPlace":false,"calibrations":[],"laterField":3}],)"
            R"("sensors":[]},"other":{"aliases":[]}})";
        writeAll(f + "/rig.json", file);
        RigStateId atLoad;
        {
            Launch l(f);
            atLoad = l.rig.stateId();
            check(l.rig.parts().state.cameras.size() == 1 && readAll(f + "/rig.json") == file && l.events == 0,
                  "S13m an all-default camera entry with no record: kept in the state, and the launch writes nothing");
        }
        {
            Launch l(f);
            check(l.rig.stateId() == atLoad && readAll(f + "/rig.json") == file,
                  "S13n ... and at the next launch, the same (its unknown field still in the file)");
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
