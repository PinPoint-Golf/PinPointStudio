/*
 * Copyright (C) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

// AthleteRepository (AthleteStore) and the Athlete — refactor step 2.3 (build/refactor/step-2/BRIEF.md
// §4.5, §6 row 2.3).
//
// ⚠ EVERY EXPECTATION COMES FROM A STATED CONTRACT OR FROM THE FILESYSTEM, NEVER FROM RUNNING THE CODE.
// The contracts: src/DomainModel/athlete.h, athlete_repository.h, src/Export/athlete_store.h (the file),
// and the decisions they cite — DEC-52 (one instance per id), DEC-04/DEC-151 (the folder chosen once,
// no `folder` key, kept on a rename), DEC-05 (athlete.json at creation), DEC-79/DEC-99 (delete = the
// whole folder to the trash, refused while a session is InProgress), FILE-LIB-01 (the library trash's
// layout, which housekeeping purges), DEC-89 (activity derived from the index), DEC-152 (the Wrist ★ on
// the record as a ShotRef), §4.9 (the tuning log's append under the athlete lock), R-APP-1/E-03 (an
// edit is recordChanged, a switch is currentAthleteChanged).
//
//   cmake --build build/tests --target athlete_repository_test -j 6
//   ctest --test-dir build/tests -R '^athlete_repository_test$' --output-on-failure

#include "athlete_controller.h"
#include "athlete_test_library.h"
#include "pp_settings.h"

#include "../../Core/folder_lock.h"
#include "../../Export/athlete_files.h"
#include "../../Export/swing_paths.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

using namespace pinpoint;

namespace {

QJsonObject readJson(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

QByteArray readBytes(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

bool writeBytes(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    return f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(bytes) == bytes.size();
}

AthleteRecord named(const QString &name)
{
    AthleteRecord r;
    r.name      = name;
    r.createdAt = 1'700'000'000;
    return r;
}

QString clean(const QString &p) { return QDir::cleanPath(QFileInfo(p).absoluteFilePath()); }

// Every athlete.json under `root`, the trash included (a read must write none).
int athleteFilesUnder(const QString &root)
{
    int n = 0;
    QDir d(root);
    for (const QString &e : d.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden))
        if (QFileInfo::exists(AthleteFiles::athleteFile(d.filePath(e)))) ++n;
    return n;
}

} // namespace

class AthleteRepositoryTest : public QObject
{
    Q_OBJECT

    QTemporaryDir m_cfg;

private slots:
    void initTestCase()
    {
        QVERIFY(m_cfg.isValid());
        // ppSettings() is IniFormat/UserScope: pointed at the scratch folder before the first QSettings.
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_cfg.path());
        QVERIFY(QFileInfo(ppSettings().fileName()).absoluteFilePath().startsWith(QFileInfo(m_cfg.path()).absoluteFilePath()));
    }

    void init()
    {
        QSettings s = ppSettings();
        s.clear();
        s.sync();
    }

    // C1–C9: create writes athlete.json once, in a folder chosen from the name; one instance per id.
    void createWritesTheFileInAFolderChosenOnce()
    {
        AthleteTestLibrary lib;
        const AthleteCreateResult made = lib.athletes->create(named(QStringLiteral("Mark Liversedge")));
        QVERIFY2(made.ok(), qPrintable(made.result.error));
        Athlete *a = made.athlete;
        QCOMPARE(clean(a->folder()), clean(lib.root() + QStringLiteral("/Mark-Liversedge")));       // C1 the sanitised name
        const QJsonObject file = readJson(AthleteFiles::athleteFile(a->folder()));
        QCOMPARE(file.value(QStringLiteral("schema")).toString(), QStringLiteral("pinpoint.athlete/1"));   // C2
        QCOMPARE(file.value(QStringLiteral("athleteId")).toString(), a->id().toString());            // C3
        QCOMPARE(file.value(QStringLiteral("record")).toObject().value(QStringLiteral("name")).toString(),
                 QStringLiteral("Mark Liversedge"));                                                  // C4
        QVERIFY(!file.contains(QStringLiteral("folder")));                                            // C5 no folder key (DEC-151)
        QVERIFY(!file.value(QStringLiteral("record")).toObject().contains(QStringLiteral("sessionCount")));   // DEC-89
        QVERIFY(!file.value(QStringLiteral("record")).toObject().contains(QStringLiteral("lastSessionAt")));
        QCOMPARE(a->source(), Athlete::Source::Library);

        QCOMPARE(lib.athletes->load(a->id()), a);                                                     // C6 one instance per id
        QVERIFY(lib.athletes->list().contains(a->id()));
        QCOMPARE(lib.athletes->load(a->id()), a);

        // C7 the same name again: the folder is taken, so the next number.
        const AthleteCreateResult twin = lib.athletes->create(named(QStringLiteral("Mark Liversedge")));
        QVERIFY(twin.ok());
        QCOMPARE(QFileInfo(twin.athlete->folder()).fileName(), QStringLiteral("Mark-Liversedge-2"));
        QVERIFY(twin.athlete->id() != a->id());

        // C8 another store over the same library reads the same record (the file is the record).
        AthleteStore other([&lib] { return lib.root(); }, &lib.sessions);
        QVERIFY(other.list().contains(a->id()));
        QCOMPARE(other.load(a->id())->record().name, QStringLiteral("Mark Liversedge"));
        QCOMPARE(clean(other.load(a->id())->folder()), clean(a->folder()));

        // C9 a blank name makes nothing.
        QVERIFY(!lib.athletes->create(named(QStringLiteral("  "))).ok());
    }

    // N1–N6: a rename changes the name only — the folder, and the sessions in it, stay (E-02, D4).
    void renameKeepsTheFolder()
    {
        AthleteTestLibrary lib;
        Athlete *a = lib.athletes->create(named(QStringLiteral("Jess Ratcliffe"))).athlete;
        QVERIFY(a);
        const QString folder = a->folder();
        const SessionStartResult s1 = lib.sessions.start(a->sessionOwner(), QStringLiteral("swing"), QStringLiteral("date-name-type"));
        QVERIFY(s1.ok());
        QVERIFY(lib.sessions.allocateSwing(s1.id).ok());
        QVERIFY(lib.sessions.end(s1.id, true).ok());

        QSignalSpy edited(a, &Athlete::recordChanged);
        AthleteRecord r = a->record();
        r.name = QStringLiteral("Jess Smith");
        QVERIFY(a->editRecord(r).ok());
        QCOMPARE(edited.count(), 1);                                                                 // N1 announced as an edit
        QCOMPARE(a->record().name, QStringLiteral("Jess Smith"));
        QCOMPARE(a->folder(), folder);                                                               // N2 the folder stays
        QCOMPARE(readJson(AthleteFiles::athleteFile(folder)).value(QStringLiteral("record")).toObject()
                     .value(QStringLiteral("name")).toString(), QStringLiteral("Jess Smith"));        // N3 in its file
        QVERIFY(!QFileInfo::exists(lib.root() + QStringLiteral("/Jess-Smith")));                     // N4 no new folder

        const QList<SessionIndexEntry> index = a->index();                                          // N5 the history stays
        QCOMPARE(index.size(), 1);
        QCOMPARE(index.first().id(), s1.id);
        const SessionStartResult s2 = lib.sessions.start(a->sessionOwner(), QStringLiteral("swing"), QStringLiteral("date-name-type"));
        QVERIFY(s2.ok());
        QCOMPARE(clean(QFileInfo(s2.folder).absolutePath()), clean(folder));                         // N6 the next session too
        QVERIFY(lib.sessions.end(s2.id, true).ok());
    }

    // S1–S9: an athlete created before step 2 — read from settings, never written by a read; its
    // edits go back to settings; an athlete.json with its id wins.
    void settingsAthleteUntilTheMigration()
    {
        const QString uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
        {
            QSettings s = ppSettings();
            s.beginGroup(QStringLiteral("athletes"));
            s.beginGroup(uuid);
            s.setValue(QStringLiteral("name"), QStringLiteral("Chris Marriott"));
            s.setValue(QStringLiteral("heightValue"), 5.75);
            s.setValue(QStringLiteral("lastSessionAt"), 123LL);    // the derived key today's records carry
            s.endGroup();
            s.endGroup();
            s.sync();
        }
        AthleteTestLibrary lib;
        const QString legacyFolder = lib.root() + QStringLiteral("/Chris-Marriott");
        QVERIFY(QDir().mkpath(legacyFolder + QStringLiteral("/2026-10-24_Chris-Marriott_Wrist_01/swing_0001")));

        const AthleteId id = AthleteId::fromString(uuid);
        QVERIFY(lib.athletes->list().contains(id));
        Athlete *a = lib.athletes->load(id);
        QVERIFY(a);
        QCOMPARE(a->source(), Athlete::Source::Settings);                                            // S1
        QCOMPARE(a->record().name, QStringLiteral("Chris Marriott"));
        QCOMPARE(a->record().heightValue, 5.75);
        QVERIFY(!a->record().toMap().contains(QStringLiteral("lastSessionAt")));                     // S2 derived, not the record's
        QCOMPARE(clean(a->folder()), clean(legacyFolder));                                           // S3 today's derivation
        QCOMPARE(a->index().size(), 1);

        AthleteController &c = lib.makeController();                                                // S4 a read writes nothing:
        QVERIFY(!c.clubsFor(uuid).isEmpty());                                                        //    the standard bag shown…
        QSettings s = ppSettings();
        QVERIFY(!s.contains(QStringLiteral("athletes/%1/clubs").arg(uuid)));                         //    …not written
        QCOMPARE(athleteFilesUnder(lib.root()), 0);                                                  //    and no athlete.json (M1's)

        QVERIFY(c.updateAthlete(uuid, QStringLiteral("notes"), QStringLiteral("hello")));            // S5 an edit goes to settings
        QCOMPARE(ppSettings().value(QStringLiteral("athletes/%1/notes").arg(uuid)).toString(), QStringLiteral("hello"));
        QCOMPARE(athleteFilesUnder(lib.root()), 0);
        QCOMPARE(ppSettings().value(QStringLiteral("athletes/%1/lastSessionAt").arg(uuid)).toLongLong(), 123LL);   // untouched

        // S6 an athlete.json with the same id (as M1 writes it) wins over the settings record.
        QJsonObject rec{ { QStringLiteral("name"), QStringLiteral("Chris M") } };
        QJsonObject file{ { QStringLiteral("schema"), QStringLiteral("pinpoint.athlete/1") },
                          { QStringLiteral("athleteId"), uuid }, { QStringLiteral("record"), rec } };
        QVERIFY(writeBytes(AthleteFiles::athleteFile(legacyFolder), QJsonDocument(file).toJson()));
        QVERIFY(lib.athletes->list().contains(id));
        QCOMPARE(lib.athletes->load(id), a);                                                         // still the one instance
        QCOMPARE(a->source(), Athlete::Source::Library);
        QCOMPARE(a->record().name, QStringLiteral("Chris M"));

        // S7 a new athlete whose name a settings athlete's (not yet made) folder holds is numbered.
        {
            QSettings w = ppSettings();
            w.setValue(QStringLiteral("athletes/%1/name").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)),
                       QStringLiteral("Paddy Marriott"));
            w.sync();
        }
        lib.athletes->list();
        QVERIFY(!QFileInfo::exists(lib.root() + QStringLiteral("/Paddy-Marriott")));
        const AthleteCreateResult paddy = lib.athletes->create(named(QStringLiteral("Paddy Marriott")));
        QVERIFY(paddy.ok());
        QCOMPARE(QFileInfo(paddy.athlete->folder()).fileName(), QStringLiteral("Paddy-Marriott-2"));
    }

    // D1–D9: delete — refused while a session is InProgress; else the whole folder into the trash,
    // inside the library, in the layout housekeeping purges.
    void removeTrashesTheWholeFolder()
    {
        AthleteTestLibrary lib;
        Athlete *a = lib.athletes->create(named(QStringLiteral("Nick Henn"))).athlete;
        QVERIFY(a);
        const QString folder = a->folder();
        const SessionStartResult s = lib.sessions.start(a->sessionOwner(), QStringLiteral("wrist"), QStringLiteral("date-name-type"));
        QVERIFY(s.ok());
        QVERIFY(lib.sessions.allocateSwing(s.id).ok());

        const AthleteRemoval refused = a->remove();                                                 // D1 InProgress: refused
        QCOMPARE(refused.result.status, WriteResult::Status::Failed);
        QVERIFY(QFileInfo::exists(AthleteFiles::athleteFile(folder)));
        QVERIFY(lib.athletes->list().contains(a->id()));

        QVERIFY(lib.sessions.end(s.id, true).ok());
        QVERIFY2(a->remove().ok(), "remove after the session ended");                                // D2
        QVERIFY(!QFileInfo::exists(folder));                                                         // D3 the folder left the library
        QVERIFY(!lib.athletes->list().contains(a->id()));                                            // D4 and the list
        const QDir trash(lib.root() + QStringLiteral("/.pinpoint-trash/Nick-Henn"));                 // D5 FILE-LIB-01's layout
        const QStringList entries = trash.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        QCOMPARE(entries.size(), 1);
        QVERIFY(entries.first().endsWith(QStringLiteral("_Nick-Henn")));
        QVERIFY(QDateTime::fromString(entries.first().left(15), QStringLiteral("yyyyMMdd-HHmmss")).isValid());   // the stamp
        const QString moved = trash.filePath(entries.first());
        QVERIFY(QFileInfo::exists(AthleteFiles::athleteFile(moved)));                                // D6 the record travels
        QCOMPARE(QDir(moved).entryList(QDir::Dirs | QDir::NoDotAndDotDot).size(), 1);                // D7 the session too

        // D8 a settings athlete: its folder carries its record into the trash, and the group goes.
        const QString uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
        {
            QSettings w = ppSettings();
            w.setValue(QStringLiteral("athletes/%1/name").arg(uuid), QStringLiteral("Old Timer"));
            w.sync();
        }
        QVERIFY(QDir().mkpath(lib.root() + QStringLiteral("/Old-Timer/2026-01-02_Old-Timer_Swing_01/swing_0001")));
        Athlete *old = lib.athletes->load(AthleteId::fromString(uuid));
        QVERIFY(old);
        QVERIFY(old->remove().ok());
        QVERIFY(!QFileInfo::exists(lib.root() + QStringLiteral("/Old-Timer")));
        QVERIFY(!ppSettings().contains(QStringLiteral("athletes/%1/name").arg(uuid)));
        const QDir oldTrash(lib.root() + QStringLiteral("/.pinpoint-trash/Old-Timer"));
        const QStringList oldEntries = oldTrash.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        QCOMPARE(oldEntries.size(), 1);
        const QJsonObject carried = readJson(AthleteFiles::athleteFile(oldTrash.filePath(oldEntries.first())));
        QCOMPARE(carried.value(QStringLiteral("athleteId")).toString(), uuid);
        QCOMPARE(carried.value(QStringLiteral("record")).toObject().value(QStringLiteral("name")).toString(),
                 QStringLiteral("Old Timer"));

        // D9 the controller: deleting through it trashes the folder too.
        AthleteController &c = lib.makeController();
        const QString kept = c.saveAthlete(QString(), QStringLiteral("Kept Back"));
        QVERIFY(!kept.isEmpty());
        const QString keptFolder = c.currentFolder();
        QVERIFY(QFileInfo::exists(keptFolder));
        QVERIFY(c.deleteAthlete(kept));
        QVERIFY(!QFileInfo::exists(keptFolder));
    }

    // J1–J9: a folder that is not this athlete's alone is never moved (J-30): another athlete
    // resolving to it keeps it (only this record goes); another athlete's athlete.json in it refuses.
    void sharedFolderIsNeverMoved()
    {
        const QString first  = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QString second = QUuid::createUuid().toString(QUuid::WithoutBraces);
        {
            QSettings w = ppSettings();
            w.setValue(QStringLiteral("athletes/%1/name").arg(first),  QStringLiteral("Chris Marriott"));
            w.setValue(QStringLiteral("athletes/%1/name").arg(second), QStringLiteral("Chris  Marriott"));   // sanitises alike
            w.sync();
        }
        AthleteTestLibrary lib;
        const QString shared = lib.root() + QStringLiteral("/Chris-Marriott");
        QVERIFY(QDir().mkpath(shared + QStringLiteral("/2026-10-24_Chris-Marriott_Wrist_01/swing_0001")));
        AthleteController &c = lib.makeController();
        QSignalSpy notices(&c, &AthleteController::deleteNotice);

        QVERIFY(c.deleteAthlete(first));                                                             // J1 deleted…
        QVERIFY(QFileInfo::exists(shared + QStringLiteral("/2026-10-24_Chris-Marriott_Wrist_01/swing_0001")));   // J2 …folder kept
        QVERIFY(!QFileInfo::exists(lib.root() + QStringLiteral("/.pinpoint-trash")));
        QVERIFY(!QFileInfo::exists(AthleteFiles::athleteFile(shared)));                              //    and no record written into it
        QVERIFY(!ppSettings().contains(QStringLiteral("athletes/%1/name").arg(first)));             // J3 only its record went
        QVERIFY(lib.athletes->list().contains(AthleteId::fromString(second)));                      //    the other stays
        QCOMPARE(notices.count(), 1);                                                                // J4 said so
        QVERIFY(notices.first().at(1).toString().contains(QStringLiteral("Chris  Marriott")));

        // A library athlete, and a settings athlete renamed onto its folder.
        Athlete *lib1 = lib.athletes->create(named(QStringLiteral("Delete Me"))).athlete;
        QVERIFY(lib1);
        const QString folder = lib1->folder();
        QVERIFY(QDir().mkpath(folder + QStringLiteral("/2026-11-02_Delete-Me_Swing_01/swing_0001")));
        const QString renamed = QUuid::createUuid().toString(QUuid::WithoutBraces);
        {
            QSettings w = ppSettings();
            w.setValue(QStringLiteral("athletes/%1/name").arg(renamed), QStringLiteral("Delete Me"));
            w.sync();
        }
        Athlete *settingsOne = lib.athletes->load(AthleteId::fromString(renamed));
        QVERIFY(settingsOne);
        QCOMPARE(clean(settingsOne->folder()), clean(folder));

        const AthleteRemoval refused = settingsOne->remove();                                       // J5 another's athlete.json: refused
        QCOMPARE(refused.result.status, WriteResult::Status::Failed);
        QVERIFY(!refused.note.isEmpty());
        QVERIFY(QFileInfo::exists(AthleteFiles::athleteFile(folder)));                               // J6 nothing moved
        QVERIFY(ppSettings().contains(QStringLiteral("athletes/%1/name").arg(renamed)));
        QVERIFY(!c.deleteAthlete(renamed));                                                          //    through the controller, said so
        QCOMPARE(notices.count(), 2);

        const AthleteRemoval kept = lib1->remove();                                                  // J7 the library athlete: shared,
        QVERIFY(kept.ok());                                                                          //    so only its athlete.json goes
        QVERIFY(!kept.folderMoved);
        QVERIFY(kept.note.contains(QStringLiteral("kept")));
        QVERIFY(!QFileInfo::exists(AthleteFiles::athleteFile(folder)));
        QVERIFY(QFileInfo::exists(folder + QStringLiteral("/2026-11-02_Delete-Me_Swing_01/swing_0001")));   // J8 the sessions stay
        QVERIFY(!lib.athletes->list().contains(lib1->id()));                                        // J9 its record is in the trash
        QVERIFY(lib.athletes->list().contains(AthleteId::fromString(renamed)));
        const QDir trash(lib.root() + QStringLiteral("/.pinpoint-trash/Delete-Me"));
        QCOMPARE(trash.entryList(QDir::Files).size(), 1);
        QVERIFY(trash.entryList(QDir::Files).first().endsWith(QStringLiteral("_Delete-Me_athlete.json")));
    }

    // E1–E4: an edit emits recordChanged, not currentAthleteChanged (R-APP-1, E-03).
    void anEditIsRecordChangedNotASwitch()
    {
        AthleteTestLibrary lib;
        AthleteController &c = lib.makeController();
        const QString uuid = c.saveAthlete(QString(), QStringLiteral("Edit Me"));
        QVERIFY(!uuid.isEmpty());
        Athlete *a = c.currentAthlete();
        QVERIFY(a);
        QCOMPARE(a->id().toString(), uuid);

        QSignalSpy record(a, &Athlete::recordChanged);
        QSignalSpy switched(&c, &AthleteController::currentAthleteChanged);
        QVERIFY(c.updateAthlete(uuid, QStringLiteral("handicap"), 12.0));                            // E1
        QCOMPARE(record.count(), 1);
        QCOMPARE(switched.count(), 0);
        QCOMPARE(c.saveAthlete(uuid, QStringLiteral("Edited")), uuid);                               // E2 a rename
        QCOMPARE(record.count(), 2);
        QCOMPARE(switched.count(), 0);
        QCOMPARE(c.currentName(), QStringLiteral("Edited"));
        QCOMPARE(c.saveAthlete(uuid, QStringLiteral("Edited")), uuid);                               // E3 the same Save again:
        QCOMPARE(record.count(), 2);                                                                 //    nothing changed, nothing said
        c.clearCurrentAthlete();                                                                     // E4 a switch (to none)
        QCOMPARE(switched.count(), 1);
        QCOMPARE(record.count(), 2);
    }

    // R1–R4: a newer build's file is read-only; an unreadable one is never listed nor overwritten.
    void newerAndUnreadableFilesAreLeftAlone()
    {
        AthleteTestLibrary lib;
        const QString uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QString newer = AthleteFiles::athleteFile(lib.root() + QStringLiteral("/Future-Golfer"));
        QJsonObject file{ { QStringLiteral("schema"), QStringLiteral("pinpoint.athlete/2") },
                          { QStringLiteral("athleteId"), uuid },
                          { QStringLiteral("record"), QJsonObject{ { QStringLiteral("name"), QStringLiteral("Future Golfer") } } } };
        QVERIFY(writeBytes(newer, QJsonDocument(file).toJson()));
        const QByteArray before = readBytes(newer);
        QVERIFY(lib.athletes->list().contains(AthleteId::fromString(uuid)));                         // R1 listed
        Athlete *a = lib.athletes->load(AthleteId::fromString(uuid));
        QVERIFY(a && a->readOnly());
        AthleteRecord r = a->record();
        r.notes = QStringLiteral("x");
        QCOMPARE(a->editRecord(r).status, WriteResult::Status::ReadOnly);                            // R2 refused
        QCOMPARE(readBytes(newer), before);                                                          //    byte for byte

        const QString broken = AthleteFiles::athleteFile(lib.root() + QStringLiteral("/Broken"));
        QVERIFY(writeBytes(broken, QByteArrayLiteral("{ not json")));
        const int listed = lib.athletes->list().size();
        QCOMPARE(listed, 1);                                                                         // R3 not listed
        const AthleteCreateResult again = lib.athletes->create(named(QStringLiteral("Broken")));
        QVERIFY(again.ok());
        QCOMPARE(QFileInfo(again.athlete->folder()).fileName(), QStringLiteral("Broken-2"));
        QCOMPARE(readBytes(broken), QByteArrayLiteral("{ not json"));                                // R4 untouched
    }

    // W1–W5: the Wrist reference ★ on the record, as a ShotRef (D5: DEC-152).
    void wristReferenceOnTheRecord()
    {
        AthleteTestLibrary lib;
        AthleteController &c = lib.makeController();
        const QString uuid = c.saveAthlete(QString(), QStringLiteral("Wrist Golfer"));
        Athlete *a = c.currentAthlete();
        QVERIFY(a);
        const SessionStartResult s = lib.sessions.start(a->sessionOwner(), QStringLiteral("wrist"), QStringLiteral("date-name-type"));
        QVERIFY(s.ok());
        const SwingAllocation sw = lib.sessions.allocateSwing(s.id);
        QVERIFY(sw.ok());
        QVERIFY(lib.sessions.end(s.id, true).ok());

        QVERIFY(c.wristReferenceSwingDir().isEmpty());                                               // W1 none yet
        QVERIFY(c.setWristReferenceSwingDir(sw.swingFolder));                                        // W2 recorded…
        QCOMPARE(a->record().wristReference, ShotRef(s.id, 1));
        QCOMPARE(readJson(AthleteFiles::athleteFile(a->folder())).value(QStringLiteral("record")).toObject()
                     .value(QStringLiteral("wristReference")).toString(), ShotRef(s.id, 1).toString());   // …as a ShotRef

        {   // W3 a fresh run (another store and controller) brings the same swing back.
            SessionStore  sessions;
            AthleteStore  athletes([&lib] { return lib.root(); }, &sessions);
            AthleteController again;
            again.setSynchronousActivity(true);
            again.setRepositories(&athletes, &sessions);
            QCOMPARE(again.currentUuid(), uuid);
            QCOMPARE(clean(again.wristReferenceSwingDir()), clean(sw.swingFolder));
        }

        QTemporaryDir elsewhere;                                                                     // W4 not this athlete's swing
        QVERIFY(QDir().mkpath(elsewhere.path() + QStringLiteral("/Other/2026-01-01_Other_Wrist_01/swing_0003")));
        QVERIFY(!c.setWristReferenceSwingDir(elsewhere.path() + QStringLiteral("/Other/2026-01-01_Other_Wrist_01/swing_0003")));
        QCOMPARE(a->record().wristReference, ShotRef(s.id, 1));
        QVERIFY(c.setWristReferenceSwingDir(QString()));                                             // W5 cleared
        QVERIFY(a->record().wristReference.isNull());
    }

    // T1–T3: the tuning log is appended, a line at a time, under the athlete folder's lock (§4.9).
    void tuningLogAppendsUnderTheLock()
    {
        QTemporaryDir lib;
        const QString folder = lib.path() + QStringLiteral("/Golfer");
        QVERIFY(QDir().mkpath(folder));
        const QString log = folder + QStringLiteral("/tuning.jsonl");
        QVERIFY(AthleteStore::appendTuningLog(log, QByteArrayLiteral("{\"a\":1}")).ok());           // T1
        QVERIFY(AthleteStore::appendTuningLog(log, QByteArrayLiteral("{\"b\":2}\n")).ok());
        QCOMPARE(readBytes(log), QByteArrayLiteral("{\"a\":1}\n{\"b\":2}\n"));                       // T2 appended, never rewritten
        QVERIFY(!QFileInfo::exists(folder + QLatin1Char('/') + FolderLock::fileName()));             //    the lock let go

        FolderLock holder(folder);
        QCOMPARE(holder.lock(), FolderLock::Result::Acquired);
        const WriteResult held = AthleteStore::appendTuningLog(log, QByteArrayLiteral("{\"c\":3}"), 100);
        QCOMPARE(held.status, WriteResult::Status::Failed);                                          // T3 held: refused, said so
        QVERIFY(!held.error.isEmpty());
        QCOMPARE(readBytes(log), QByteArrayLiteral("{\"a\":1}\n{\"b\":2}\n"));
    }

    // A1–A4: the picker's two figures, derived from the index (DEC-89).
    void activityFromTheIndex()
    {
        AthleteTestLibrary lib;
        Athlete *a = lib.athletes->create(named(QStringLiteral("Busy Golfer"))).athlete;
        QVERIFY(a);
        const QString folder = a->folder();
        QVERIFY(QDir().mkpath(folder + QStringLiteral("/2026-10-20_Busy-Golfer_Wrist_01/swing_0001")));
        QVERIFY(QDir().mkpath(folder + QStringLiteral("/2026-11-03_Busy-Golfer_Wrist_01")));         // nothing hit in it
        AthleteActivity act = lib.athletes->readActivity(a->sessionOwner());
        QCOMPARE(act.sessions, 1);                                                                   // A1 the empty one is not a session
        QCOMPARE(act.lastAtSec, QDateTime(QDate(2026, 10, 20), QTime(0, 0)).toSecsSinceEpoch());     // A2 a legacy one: its day

        const SessionStartResult s = lib.sessions.start(a->sessionOwner(), QStringLiteral("swing"), QStringLiteral("date-name-type"));
        QVERIFY(s.ok());
        QVERIFY(lib.sessions.allocateSwing(s.id).ok());
        act = lib.athletes->readActivity(a->sessionOwner());
        QCOMPARE(act.sessions, 2);                                                                   // A3
        const auto entry = lib.sessions.open(s.id);
        QVERIFY(entry);
        QCOMPARE(act.lastAtSec, entry->startedAtUtc.toSecsSinceEpoch());                             // A4 a new one: its start
        QVERIFY(lib.sessions.end(s.id, true).ok());

        AthleteController &c = lib.makeController();                                                // the controller's rows
        QCOMPARE(c.athletes().first().toMap().value(QStringLiteral("sessionCount")).toInt(), 2);
    }
};

QTEST_GUILESS_MAIN(AthleteRepositoryTest)
#include "athlete_repository_test.moc"
