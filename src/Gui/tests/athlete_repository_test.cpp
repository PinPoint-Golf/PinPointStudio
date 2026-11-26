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
// The contracts: src/DomainModel/athlete.h, athlete_repository.h, src/Stores/athlete_store.h (the file),
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
#include "../../Stores/athlete_files.h"
#include "../../Stores/session_files.h"
#include "../../DomainModel/session.h"
#include "../../Stores/swing_paths.h"
#include "../../Stores/swing_store_repository.h"   // the Shots' recordings door (the Wrist ★, step 5.7b)
#include "../../DomainModel/shot.h"

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
#include "../../Core/tests/isolated_stores.h"   // pinpoint::test::IsolatedStores (#187)

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

// The tuning log's lines read back as its readers read them (shot_commands_test's tuningLines,
// session_diagnostics_model_test's tuningLinesIn): split on LF, a blank (trimmed) line skipped, each parsed as it is.
QList<QJsonObject> tuningObjects(const QString &path)
{
    QList<QJsonObject> out;
    for (const QByteArray &line : readBytes(path).split('\n'))
        if (!line.trimmed().isEmpty()) out << QJsonDocument::fromJson(line).object();   // untrimmed, as theirs: a CR is JSON whitespace
    return out;
}

// True when no key and no string value of `o` holds a CR.
bool noCarriageReturn(const QJsonObject &o)
{
    for (auto it = o.begin(); it != o.end(); ++it)
        if (it.key().contains(QLatin1Char('\r')) || (it.value().isString() && it.value().toString().contains(QLatin1Char('\r'))))
            return false;
    return true;
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
    pinpoint::test::IsolatedStores m_stores;   // #187: the first member, so every store is scratch before any other

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
        QVERIFY(lib.sessions.end(s1.id).ok());

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
        QVERIFY(lib.sessions.end(s2.id).ok());
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

        QVERIFY(lib.sessions.end(s.id).ok());
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
        const QString folder = c.folderOf(uuid);                                                      // J-29 (step 2.7b)
        QVERIFY(!folder.isEmpty());
        QCOMPARE(folder, a->folder());

        QSignalSpy record(a, &Athlete::recordChanged);
        QSignalSpy switched(&c, &AthleteController::currentAthleteChanged);
        QVERIFY(c.updateAthlete(uuid, QStringLiteral("handicap"), QStringLiteral("12")));            // E1 (as text: 5.7b)
        QCOMPARE(record.count(), 1);
        QCOMPARE(switched.count(), 0);
        QCOMPARE(c.saveAthlete(uuid, QStringLiteral("Edited")), uuid);                               // E2 a rename
        QCOMPARE(record.count(), 2);
        QCOMPARE(switched.count(), 0);
        QCOMPARE(c.currentName(), QStringLiteral("Edited"));
        // J-29: the renamed athlete is still found in the folder it always had (the report's lookup), never in
        // one its new name would sanitise to; an athlete nobody knows has no folder
        QCOMPARE(c.folderOf(uuid), folder);
        QVERIFY(!folder.endsWith(QStringLiteral("/Edited")));
        QCOMPARE(c.folderOf(QUuid::createUuid().toString(QUuid::WithoutBraces)), QString());
        QCOMPARE(c.folderOf(QString()), QString());
        QCOMPARE(c.saveAthlete(uuid, QStringLiteral("Edited")), uuid);                               // E3 the same Save again:
        QCOMPARE(record.count(), 2);                                                                 //    nothing changed, nothing said
        c.clearCurrentAthlete();                                                                     // E4 a switch (to none)
        QCOMPARE(switched.count(), 1);
        QCOMPARE(record.count(), 2);
    }

    // R1–R4 (J-66, step 2.8b): the session report's athlete is the one whose FOLDER holds the session — never the
    // athlete a name spells. Jo Bloggs is renamed Jo Smith (her folder stays Jo-Bloggs); a new Jo Bloggs gets
    // Jo-Bloggs-2. A session in Jo-Bloggs is Jo Smith's, though its folder's name reads "Jo Bloggs" (the old
    // lookup by that name found the new athlete).
    void theReportsAthleteIsTheFoldersOwner()
    {
        AthleteTestLibrary lib;
        AthleteController &c = lib.makeController();
        const QString first = c.saveAthlete(QString(), QStringLiteral("Jo Bloggs"));
        QVERIFY(!first.isEmpty());
        const QString firstFolder = c.folderOf(first);
        QCOMPARE(c.saveAthlete(first, QStringLiteral("Jo Smith")), first);
        QCOMPARE(c.folderOf(first), firstFolder);
        const QString second = c.saveAthlete(QString(), QStringLiteral("Jo Bloggs"));
        QVERIFY(!second.isEmpty() && second != first);
        QVERIFY(clean(c.folderOf(second)) != clean(firstFolder));
        const QString inFirst  = firstFolder + QStringLiteral("/2026-11-02_Jo-Bloggs_Swing_01");
        const QString inSecond = c.folderOf(second) + QStringLiteral("/2026-11-03_Jo-Bloggs_Swing_01");
        QCOMPARE(c.reportDetailsForSession(inFirst).uuid, first);     // R1 the folder's owner
        QCOMPARE(c.reportDetailsForSession(inFirst).name, QStringLiteral("Jo Smith"));
        QCOMPARE(c.reportDetailsForSession(inSecond).uuid, second);   // R2
        QVERIFY(c.reportDetailsForSession(lib.root() + QStringLiteral("/Nobody/2026-11-02_Nobody_Swing_01")).isEmpty());   // R3
        QVERIFY(c.reportDetailsForSession(QString()).isEmpty());                                         // R4
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
        // The Wrist ★ goes through the owners since 5.7b (the Session's Shot names its folder), so the Shots have their
        // recordings door, as main.cpp gives them.
        SwingStoreRecordingRepository recordings(lib.sessions);
        lib.athletes->setShotDoors({ &recordings, {} });
        AthleteController &c = lib.makeController();
        const QString uuid = c.saveAthlete(QString(), QStringLiteral("Wrist Golfer"));
        Athlete *a = c.currentAthlete();
        QVERIFY(a);
        const SessionStartResult s = lib.sessions.start(a->sessionOwner(), QStringLiteral("wrist"), QStringLiteral("date-name-type"));
        QVERIFY(s.ok());
        const SwingAllocation sw = lib.sessions.allocateSwing(s.id);
        QVERIFY(sw.ok());
        QVERIFY(lib.sessions.end(s.id).ok());

        QVERIFY(c.wristReferenceSwingDir().isEmpty());                                               // W1 none yet
        QVERIFY(c.setWristReferenceSwingDir(sw.swingFolder));                                        // W2 recorded…
        QCOMPARE(a->record().wristReference, ShotRef(s.id, 1));
        QCOMPARE(readJson(AthleteFiles::athleteFile(a->folder())).value(QStringLiteral("record")).toObject()
                     .value(QStringLiteral("wristReference")).toString(), ShotRef(s.id, 1).toString());   // …as a ShotRef

        {   // W3 a fresh run (another store and controller) brings the same swing back.
            SessionStore  sessions;
            AthleteStore  athletes([&lib] { return lib.root(); }, &sessions);
            SwingStoreRecordingRepository againRecordings(sessions);
            athletes.setShotDoors({ &againRecordings, {} });
            AthleteController again;
            again.setSynchronousActivity(true);
            again.setCallables(callablesOver(athletes), &sessions);
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

    // T1–T3: the tuning log is appended, a line at a time, under the athlete folder's lock (§4.9). Its bytes are
    // compared: every line ends in LF on every platform (PR-60), Windows included.
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

    // T4–T5: a log the studio PC wrote in text mode before PR-60 holds CRLF lines, and one on a shared library can mix
    // both. An append leaves those lines as they are and adds its own in LF; the whole log reads back as before, with
    // no CR in any field.
    void tuningLogReadsAMixedFile()
    {
        QTemporaryDir lib;
        const QString log = lib.path() + QStringLiteral("/Golfer/tuning.jsonl");
        const QByteArray mixed = QByteArrayLiteral("{\"a\":1,\"why\":\"cabin\"}\r\n"
                                                   "{\"b\":\"mac\"}\n"
                                                   "{\"c\":\"cabin\"}\r\n");
        QVERIFY(writeBytes(log, mixed));
        QVERIFY(AthleteStore::appendTuningLog(log, QByteArrayLiteral("{\"d\":\"now\"}")).ok());
        QCOMPARE(readBytes(log), mixed + QByteArrayLiteral("{\"d\":\"now\"}\n"));               // T4 old lines kept; new in LF

        const QList<QJsonObject> lines = tuningObjects(log);                                         // T5 read as before
        QCOMPARE(lines.size(), 4);
        QCOMPARE(lines.at(0), (QJsonObject{ { QStringLiteral("a"), 1 }, { QStringLiteral("why"), QStringLiteral("cabin") } }));
        QCOMPARE(lines.at(1), (QJsonObject{ { QStringLiteral("b"), QStringLiteral("mac") } }));
        QCOMPARE(lines.at(2), (QJsonObject{ { QStringLiteral("c"), QStringLiteral("cabin") } }));
        QCOMPARE(lines.at(3), (QJsonObject{ { QStringLiteral("d"), QStringLiteral("now") } }));
        for (const QJsonObject &o : lines)
            QVERIFY(noCarriageReturn(o));
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
        QVERIFY(lib.sessions.end(s.id).ok());

        AthleteController &c = lib.makeController();                                                // the controller's rows
        QCOMPARE(c.athletes().first().sessionCount, 2);
        QCOMPARE(c.athletes().first().lastSessionAt, entry->startedAtUtc.toSecsSinceEpoch());
    }

    // F1–F6 (refactor step 5.7b): the picker's activity follows the owners — the one session list's rows over each Athlete's
    // SessionIndex — so a session added (its first swing), trashed or moved changes the list at once, with no
    // refreshActivity (the picker's reopen), and a shot in a session that already has one announces nothing.
    void activityFollowsTheOwners()
    {
        AthleteTestLibrary lib;
        Athlete *a = lib.athletes->create(named(QStringLiteral("Follow Golfer"))).athlete;
        Athlete *b = lib.athletes->create(named(QStringLiteral("Target Golfer"))).athlete;
        Athlete *u = lib.athletes->unassigned(/*create=*/true);
        QVERIFY(a && b && u);
        AthleteController &c = lib.makeController();
        const auto rowOf = [&c](const Athlete *x) {
            for (const AthleteRow &r : c.athletes())
                if (r.uuid == x->id().toString()) return r;
            return AthleteRow{};
        };
        QCOMPARE(rowOf(a).sessionCount, 0);
        QSignalSpy announced(&c, &AthleteController::athletesChanged);

        // F1 a session started (EVT-17) and its first swing (EVT-01): counted, its start the last session.
        Session *s = a->startSession(QStringLiteral("swing"), QStringLiteral("date-name-type"));
        QVERIFY(s);
        QCOMPARE(rowOf(a).sessionCount, 0);                                                          // nothing hit in it yet
        Shot *first = s->addShot();
        QVERIFY(first && s->shotDocumentWritten(first->ref()));
        QCOMPARE(rowOf(a).sessionCount, 1);
        QCOMPARE(rowOf(a).lastSessionAt, s->entry()->startedAtUtc.toSecsSinceEpoch());
        QCOMPARE(c.athletes().first().uuid, a->id().toString());                                     // F2 Recent order follows
        // F3 a second swing changes no figure: the list is not announced again.
        const int before = announced.count();
        Shot *second = s->addShot();
        QVERIFY(second && s->shotDocumentWritten(second->ref()));
        QCOMPARE(announced.count(), before);
        QVERIFY(s->end().ok());

        // F4 trashed (EVT-20): gone from the count at once.
        QVERIFY(a->trashSession(s->id()).ok());
        QCOMPARE(rowOf(a).sessionCount, 0);

        // F5 moved (the move out and in): Unassigned's session leaves it and joins the golfer's count.
        Session *loose = u->startSession(QStringLiteral("swing"), QStringLiteral("date-name-type"));
        QVERIFY(loose);
        Shot *shot = loose->addShot();
        QVERIFY(shot && loose->shotDocumentWritten(shot->ref()) && loose->end().ok());
        QCOMPARE(rowOf(u).sessionCount, 1);                                                          // listed: it holds a session
        QCOMPARE(rowOf(b).sessionCount, 0);
        QVERIFY(u->moveSessionTo(loose->id(), *b).ok());
        QCOMPARE(rowOf(b).sessionCount, 1);
        QVERIFY(rowOf(u).uuid.isEmpty());                                                            // F6 empty again: not listed
    }

    // U1–U8 (refactor step 5.1; information_model.md §5.11, R-UNASSIGNED-1..5; DEC-198; J-66, J-67): the Unassigned
    // owner — its record at the first write (id and name only, its own folder verbatim), a no-golfer session found on
    // disk held where it is with nothing written, a capture's session a full Session of it, never edited or deleted.
    void unassignedOwnerRecordAndFolder()
    {
        AthleteTestLibrary lib;
        QVERIFY(lib.athletes->unassigned(/*create=*/false) == nullptr);                                // U1 nothing on first need
        QVERIFY(!lib.athletes->list().contains(unassignedAthleteId()));
        QVERIFY(!QFileInfo::exists(lib.root() + QStringLiteral("/(unassigned)")));                    // …and a read writes nothing

        WriteResult made;
        Athlete *u = lib.athletes->unassigned(/*create=*/true, &made);
        QVERIFY2(u, qPrintable(made.error));
        QCOMPARE(u->id(), unassignedAthleteId());                                                      // U2 the fixed id
        QVERIFY(u->isUnassigned());
        QCOMPARE(clean(u->folder()), clean(lib.root() + QStringLiteral("/(unassigned)")));              // U3 verbatim, never sanitised
        const QJsonObject file = readJson(AthleteFiles::athleteFile(u->folder()));
        QCOMPARE(file.value(QStringLiteral("athleteId")).toString(), unassignedAthleteId().toString());
        QCOMPARE(file.value(QStringLiteral("record")).toObject().keys(), QStringList{ QStringLiteral("name") });   // U4 id and name only
        QCOMPARE(file.value(QStringLiteral("record")).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("Unassigned"));
        QVERIFY(u->record().sex.isEmpty() && u->record().dob.isEmpty());                              // …no norm cohort

        // U5 a golfer called "unassigned" gets the sanitised folder, never Unassigned's.
        Athlete *g = lib.athletes->create(named(QStringLiteral("(unassigned)"))).athlete;
        QVERIFY(g && !g->isUnassigned());
        QCOMPARE(QFileInfo(g->folder()).fileName(), QStringLiteral("unassigned"));

        // U6 a capture's session: a full Session of it, its file naming the fixed id; never edited, never deleted.
        Session *s = u->startSession(QStringLiteral("swing"), QStringLiteral("date-name-type"));
        QVERIFY(s && s->entry());
        QCOMPARE(QFileInfo(s->entry()->folder).absolutePath(), QFileInfo(u->folder()).absoluteFilePath());
        QCOMPARE(readJson(SessionFiles::sessionFile(s->entry()->folder)).value(QStringLiteral("athleteId")).toString(),
                 unassignedAthleteId().toString());
        QVERIFY(lib.sessions.allocateSwing(s->id()).ok());
        QVERIFY(s->end().ok());
        AthleteRecord edited = u->record();
        edited.sex = QStringLiteral("male");
        QVERIFY(!u->editRecord(edited).ok());
        QVERIFY(!lib.athletes->trash(unassignedAthleteId()).ok());
        QVERIFY(QFileInfo::exists(u->folder()));

        // U7 another store over the same library lists it (found like any athlete's folder).
        AthleteStore other([&lib] { return lib.root(); }, &lib.sessions);
        QVERIFY(other.list().contains(unassignedAthleteId()));
        QCOMPARE(clean(other.load(unassignedAthleteId())->folder()), clean(u->folder()));
    }

    void unassignedHoldsAnOldNoGolferFolderWhereItIs()
    {
        AthleteTestLibrary lib;
        // A session an older build captured with no golfer: <library>/unknown, no athlete.json, a file naming no athlete.
        SessionStore older;
        older.setOsTrashForTest(false);
        const SessionStartResult s = older.start(SessionOwner{ AthleteId(), QString(), lib.root() }, QStringLiteral("swing"),
                                                 QStringLiteral("date-name-type"));
        QVERIFY(s.ok());
        QVERIFY(QFileInfo(s.folder).absolutePath().endsWith(QStringLiteral("/unknown")));
        QVERIFY(older.allocateSwing(s.id).ok());
        QVERIFY(older.end(s.id).ok());

        QVERIFY(lib.athletes->list().contains(unassignedAthleteId()));                                 // U8 listed, in memory…
        QVERIFY(!QFileInfo::exists(lib.root() + QStringLiteral("/(unassigned)")));                    // …nothing written
        Athlete *u = lib.athletes->load(unassignedAthleteId());
        QVERIFY(u);
        Session *held = u->sessionAt(s.folder);
        QVERIFY(held);                                                                                 // U9 held where it is (J-66)
        QCOMPARE(held->id(), s.id);
        QCOMPARE(u->index().size(), 1);
    }

    // P1–P4: the picker lists Unassigned only while its SessionIndex holds a session (R-UNASSIGNED-3; DEC-198, (a)).
    void pickerShowsUnassignedOnlyWithSessions()
    {
        AthleteTestLibrary lib;
        Athlete *golfer = lib.athletes->create(named(QStringLiteral("Real Golfer"))).athlete;
        QVERIFY(golfer);
        Athlete *u = lib.athletes->unassigned(/*create=*/true);
        QVERIFY(u);
        AthleteController &c = lib.makeController();
        const auto listed = [&c](const AthleteId &id) {
            for (const AthleteRow &r : c.athletes())
                if (r.uuid == id.toString()) return true;
            return false;
        };
        QVERIFY(listed(golfer->id()));
        QVERIFY(!listed(unassignedAthleteId()));                                                       // P1 empty: hidden
        Session *s = u->startSession(QStringLiteral("swing"), QStringLiteral("date-name-type"));
        QVERIFY(s && lib.sessions.allocateSwing(s->id()).ok() && s->end().ok());
        c.refreshActivity();
        QVERIFY(listed(unassignedAthleteId()));                                                        // P2 holds a session: shown
        QVERIFY(c.isUnassignedSession(s->entry()->folder));
        QVERIFY(!c.isUnassignedSession(golfer->folder() + QStringLiteral("/nothing")));

        // M1–M5: the move to a golfer (R-UNASSIGNED-4; J-67).
        const QString from = s->entry()->folder;
        const SessionId id = s->id();
        QSignalSpy moved(&c, &AthleteController::sessionMoved);
        QCOMPARE(c.moveSessionToAthlete(from, golfer->id().toString()), QString());                    // M1 moved
        QCOMPARE(moved.count(), 1);
        const QString to = moved.first().at(1).toString();
        QCOMPARE(QFileInfo(to).fileName(), QFileInfo(from).fileName());                                // M2 its folder name kept…
        QCOMPARE(clean(QFileInfo(to).absolutePath()), clean(golfer->folder()));                        // …in the golfer's folder
        QVERIFY(!QFileInfo::exists(from) && QFileInfo::exists(to + QStringLiteral("/swing_0001")));
        QCOMPARE(readJson(SessionFiles::sessionFile(to)).value(QStringLiteral("athleteId")).toString(), golfer->id().toString());
        QCOMPARE(readJson(SessionFiles::sessionFile(to)).value(QStringLiteral("sessionId")).toString(), id.toString());   // M3 its id kept
        Session *there = golfer->sessionAt(to);
        QVERIFY(there && there->id() == id);                                                           // M4 the golfer's now
        QVERIFY(u->index().isEmpty());
        QVERIFY(!listed(unassignedAthleteId()));                                                       // P3 empty again: hidden

        // J1 (J-68, review B3): the last session moved out while Unassigned is the athlete on screen — the app goes to the
        // golfer it went to, and Unassigned, empty, leaves the picker (R-UNASSIGNED-3).
        Session *s2 = u->startSession(QStringLiteral("swing"), QStringLiteral("date-name-type"));
        QVERIFY(s2 && lib.sessions.allocateSwing(s2->id()).ok() && s2->end().ok());
        const QString from2 = s2->entry()->folder;
        c.refreshActivity();
        QVERIFY(c.selectAthlete(unassignedAthleteId().toString()));
        QVERIFY(c.currentIsUnassigned() && listed(unassignedAthleteId()));
        // To a second golfer: a same-day Unassigned session takes the moved one's folder name again (…_Swing_01), which
        // the first golfer's folder already holds — that refusal is its own finding (NOTES-5.1 E-8), not J-68's.
        Athlete *second = lib.athletes->create(named(QStringLiteral("Second Golfer"))).athlete;
        QVERIFY(second);
        c.refreshActivity();
        QCOMPARE(c.moveSessionToAthlete(from2, second->id().toString()), QString());
        QCOMPARE(c.currentUuid(), second->id().toString());
        QVERIFY(!c.currentIsUnassigned());
        QVERIFY(!listed(unassignedAthleteId()));

        // M5 a session in progress is not moved (P3).
        Session *live = u->startSession(QStringLiteral("swing"), QStringLiteral("date-name-type"));
        QVERIFY(live && live->inProgress());
        QVERIFY(!c.moveSessionToAthlete(live->entry()->folder, golfer->id().toString()).isEmpty());
        QVERIFY(QFileInfo::exists(live->entry()->folder));
        live->end();
    }
};

QTEST_GUILESS_MAIN(AthleteRepositoryTest)
#include "athlete_repository_test.moc"
