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

// migrate_step2_test — pps_migrate_step2's engine (tools/migrate/migrate_step2.h) on a FIXTURE LIBRARY in a
// temporary folder (refactor step 2.8a; build/refactor/step-2/BRIEF.md §6 row 2.8). Never the real library or
// settings: the fixture's own settings file, QSettings pointed at the fixture (QSettings::setPath), and the
// fixture folder as the only "scratch root" a write may touch.
//
// The fixture: two settings records on this machine ("mac") — Mark (whose swings record two OTHER uuids, the
// cabin PC's: "two uuids in one folder", J-39) and an athlete RENAMED since her sessions (her record now names
// "Jess Smith", her sessions sit in Jess-Ratcliffe/, E-02); a second machine's records ("cabin": Mark under
// another id with another height, H-09); legacy sessions, two on one day whose name order is not their start
// order; one whose name says Swing and whose shots say Wrist; one with no swing; one whose session.json the
// app backfilled at a resume with a folder-time start (J-19); golfer input in diagnostics.json's older keys,
// one entry for a shot whose folder is gone; golfer input only a BACKUP holds (H-04's 10 Jul case); the Wrist
// ★ setting.
//
//   A  the dry-run report: M1–M4 as expected (blocked first: the machines disagree; then decided)
//   B  a dry run writes NOTHING: every file of the fixture (library, settings, inputs) hashed before and after
//   C  the guards: a write outside the scratch roots, with a folder lock held, with the app running — refused,
//      nothing written
//   D  a scratch write: athlete.json, session.json, session_input.json, the reviews, diagnostics.json, the ★,
//      the settings, the backups — each read back through the app's own doors
//   E  a second run writes nothing
//
//   cmake --build build/tests --target migrate_step2_test -j 6
//   ctest --test-dir build/tests -R '^migrate_step2_test$' --output-on-failure

#include "../migrate_step2.h"

#include "../../../src/DomainModel/session_identity.h"
#include "../../../src/Export/athlete_files.h"
#include "../../../src/Export/athlete_store.h"
#include "../../../src/Export/session_diagnostics_files.h"
#include "../../../src/Export/session_files.h"
#include "../../../src/Export/session_store.h"
#include "../../../src/Export/swing_store.h"
#include "../../../src/Export/swing_store_repository.h"
#include "../../../src/Core/folder_lock.h"
#include "../../../src/Core/pp_settings.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimeZone>

#include <cstdio>

using namespace pinpoint;
using namespace pinpoint::migrate;

static int g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}

static const QString kMark  = QStringLiteral("aaaaaaaa-1111-4111-8111-aaaaaaaaaaaa");   // this machine's Mark
static const QString kJess  = QStringLiteral("bbbbbbbb-2222-4222-8222-bbbbbbbbbbbb");   // this machine's renamed Jess
static const QString kCabin = QStringLiteral("c1c1c1c1-3333-4333-8333-c1c1c1c1c1c1");   // the cabin's Mark (in his swings)
static const QString kCabin2 = QStringLiteral("c2c2c2c2-4444-4444-8444-c2c2c2c2c2c2");  // a second uuid in his swings

static const char *S1 = "2026-07-10_Mark-Liversedge_Wrist_01";
static const char *S2 = "2026-07-10_Mark-Liversedge_Wrist_02";
static const char *S3 = "2026-07-12_Mark-Liversedge_Swing_01";
static const char *S4 = "2026-07-14_Mark-Liversedge_Wrist_01";
static const char *S5 = "2026-07-15_Mark-Liversedge_Wrist_01";
static const char *S6 = "2026-07-11_Jess-Ratcliffe_Wrist_01";

static bool writeFile(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    return f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(bytes) == bytes.size();
}
static bool writeJson(const QString &path, const QJsonObject &o) { return writeFile(path, QJsonDocument(o).toJson()); }
static QJsonObject jsonOf(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll()).object() : QJsonObject();
}

// A JSON-era swing document: just the header a capture writes (and an empty review).
static void swing(const QString &session, int n, const QString &uuid, const QString &name, const QString &wallclock,
                  int sessionType, qint64 windowUs = 2000000)
{
    const QString dir = session + QStringLiteral("/swing_%1").arg(n, 4, 10, QLatin1Char('0'));
    QJsonObject root{
        { QStringLiteral("schema"), QStringLiteral("pinpoint.swing/2") },
        { QStringLiteral("swing"), QJsonObject{ { QStringLiteral("id"), QStringLiteral("swing_%1").arg(n, 4, 10, QLatin1Char('0')) },
                                                { QStringLiteral("index"), n } } },
        { QStringLiteral("athlete"), QJsonObject{ { QStringLiteral("name"), name }, { QStringLiteral("uuid"), uuid } } },
        { QStringLiteral("session"), QJsonObject{ { QStringLiteral("dir"), QFileInfo(session).fileName() } } },
        { QStringLiteral("clock"), QJsonObject{ { QStringLiteral("t0_us"), 0 }, { QStringLiteral("wallclock"), wallclock } } },
        { QStringLiteral("window"), QJsonObject{ { QStringLiteral("start_us"), 0 }, { QStringLiteral("end_us"), windowUs } } },
        { QStringLiteral("capture"), QJsonObject{ { QStringLiteral("sessionType"), sessionType } } },
        { QStringLiteral("review"), QJsonObject{ { QStringLiteral("club"), QStringLiteral("7 IRON") }, { QStringLiteral("note"), QString() },
                                                 { QStringLiteral("rating"), 0 } } },
        { QStringLiteral("streams"), QJsonArray() },
    };
    writeJson(dir + QStringLiteral("/swing.json"), root);
}

static QJsonObject envelope(const QJsonObject &golfer)
{
    QJsonObject o = golfer;
    o.insert(QStringLiteral("schemaVersion"), 1);
    o.insert(QStringLiteral("session"), QJsonObject{ { QStringLiteral("stage"), QStringLiteral("reduced") } });
    o.insert(QStringLiteral("ledger"), QJsonObject{ { QStringLiteral("schemaVersion"), 1 }, { QStringLiteral("rows"), QJsonArray() } });
    return o;
}

static void settingsRecord(QSettings &s, const QString &uuid, const QString &name, double heightFt)
{
    s.beginGroup(QStringLiteral("athletes/") + uuid);
    s.setValue(QStringLiteral("name"), name);
    s.setValue(QStringLiteral("handedness"), QStringLiteral("Right"));
    s.setValue(QStringLiteral("heightValue"), heightFt);
    s.setValue(QStringLiteral("heightUnit"), QStringLiteral("ft"));
    s.setValue(QStringLiteral("createdAt"), 1785245456);
    s.setValue(QStringLiteral("lastSessionAt"), 0);   // derived: never migrated (DEC-89)
    s.setValue(QStringLiteral("clubs"), QVariantMap{ { QStringLiteral("7 IRON"), QVariantMap{ { QStringLiteral("lengthMm"), 943 } } } });
    s.endGroup();
}

struct Fixture {
    QTemporaryDir tmp;
    QString root, lib, settings, cabinSettings, exportFile, decisions, backup;

    Fixture()
    {
        root          = QDir::cleanPath(tmp.path());
        lib           = root + QStringLiteral("/lib");
        settings      = root + QStringLiteral("/cfg/PinPointStudio/PinPointStudio.ini");
        cabinSettings = root + QStringLiteral("/cabin/PinPointStudio.ini");
        exportFile    = root + QStringLiteral("/cabin-records.json");
        decisions     = root + QStringLiteral("/decisions.json");
        backup        = root + QStringLiteral("/backup");
        const QString mark = lib + QStringLiteral("/Mark-Liversedge");
        const QString jess = lib + QStringLiteral("/Jess-Ratcliffe");

        // S1 and S2: one day, S2 started first (the name order is not the start order). Two uuids in the folder.
        swing(mark + '/' + S1, 1, kCabin, QStringLiteral("Mark Liversedge"), QStringLiteral("2026-07-10T13:00:00.000Z"), 1);
        swing(mark + '/' + S1, 2, kCabin, QStringLiteral("Mark Liversedge"), QStringLiteral("2026-07-10T13:01:00.000Z"), 1);
        swing(mark + '/' + S2, 1, kCabin2, QStringLiteral("Mark Liversedge"), QStringLiteral("2026-07-10T11:00:00.000Z"), 1);
        swing(mark + '/' + S2, 2, kCabin2, QStringLiteral("Mark Liversedge"), QStringLiteral("2026-07-10T11:02:00.000Z"), 1);
        // S2's golfer input, in the older keys: a miss, a shot intent (and one for a shot whose folder is gone), a correction.
        writeJson(SessionFiles::diagnosticsFile(mark + '/' + S2), envelope(QJsonObject{
            { QStringLiteral("intent"), QJsonObject{ { QStringLiteral("focusConditionId"), QString() }, { QStringLiteral("focusFromShot"), -1 },
                                                     { QStringLiteral("declaredMiss"), QStringLiteral("push") } } },
            { QStringLiteral("screens"), QJsonObject() },
            { QStringLiteral("shotIntent"), QJsonObject{ { QStringLiteral("2"), QStringLiteral("fade") }, { QStringLiteral("9"), QStringLiteral("draw") } } },
            { QStringLiteral("corrections"), QJsonObject{ { QStringLiteral("1"), QJsonArray{ QJsonObject{
                  { QStringLiteral("family"), QStringLiteral("strike") }, { QStringLiteral("from"), QStringLiteral("fat") },
                  { QStringLiteral("to"), QStringLiteral("thin") } } } } } } }));
        // S3: the name says Swing, the shots say Wrist. Its live diagnostics.json lost a focus a backup still holds.
        swing(mark + '/' + S3, 1, kCabin, QStringLiteral("Mark Liversedge"), QStringLiteral("2026-07-12T10:00:00.000Z"), 1);
        writeJson(SessionFiles::diagnosticsFile(mark + '/' + S3), envelope(QJsonObject{
            { QStringLiteral("intent"), QJsonObject{ { QStringLiteral("focusConditionId"), QString() }, { QStringLiteral("focusFromShot"), -1 },
                                                     { QStringLiteral("declaredMiss"), QString() } } } }));
        writeJson(SessionFiles::diagnosticsBackupFile(mark + '/' + S3, QStringLiteral("pre-79ffef1")), envelope(QJsonObject{
            { QStringLiteral("intent"), QJsonObject{ { QStringLiteral("focusConditionId"), QStringLiteral("excessive_shaft_lean") },
                                                     { QStringLiteral("focusFromShot"), 1 }, { QStringLiteral("declaredMiss"), QString() } } } }));
        // S4: the app backfilled its session.json at a resume (J-19), the start from folder times (wrong).
        swing(mark + '/' + S4, 1, kCabin, QStringLiteral("Mark Liversedge"), QStringLiteral("2026-07-14T10:00:00.000Z"), 1);
        writeJson(SessionFiles::sessionFile(mark + '/' + S4), QJsonObject{
            { QStringLiteral("schema"), QStringLiteral("pinpoint.session/1") },
            { QStringLiteral("sessionId"), QStringLiteral("44444444-4444-4444-8444-444444444444") },
            { QStringLiteral("athleteId"), kMark }, { QStringLiteral("type"), QStringLiteral("wrist") },
            { QStringLiteral("status"), QStringLiteral("ended") }, { QStringLiteral("startedAt"), QStringLiteral("2026-07-14T00:00:00.000Z") },
            { QStringLiteral("endedAt"), QStringLiteral("2026-07-14T11:00:00.000Z") },
            { QStringLiteral("origin"), QJsonObject{ { QStringLiteral("kind"), QStringLiteral("backfilled") }, { QStringLiteral("tool"), QStringLiteral("PinPointStudio") } } } });
        // S5: a session with no swing.
        QDir().mkpath(mark + '/' + S5);
        writeFile(AthleteFiles::tendenciesFile(mark), QByteArray("{\"schema\":1}"));
        // S6: the renamed athlete's session, in the folder her old name gave.
        swing(jess + '/' + S6, 1, kJess, QStringLiteral("Jess Ratcliffe"), QStringLiteral("2026-07-11T10:00:00.000Z"), 1);

        // This machine's settings, and the cabin's.
        {
            QSettings s(settings, QSettings::IniFormat);
            s.setValue(QStringLiteral("currentAthleteUuid"), kMark);
            s.setValue(QStringLiteral("General/athleteLibraryPath"), lib);
            s.setValue(QStringLiteral("ui/wristReferenceSwingDir"), mark + '/' + S2 + QStringLiteral("/swing_0002"));
            settingsRecord(s, kMark, QStringLiteral("Mark Liversedge"), 6.0);
            settingsRecord(s, kJess, QStringLiteral("Jess Smith"), 5.0);
            s.sync();
        }
        {
            QSettings s(cabinSettings, QSettings::IniFormat);
            settingsRecord(s, kCabin, QStringLiteral("Mark Liversedge"), 5.9);
            s.sync();
        }
    }

    Options options() const
    {
        Options o;
        o.library      = lib;
        o.settingsFile = settings;
        o.machine      = QStringLiteral("mac");
        o.scratchRoots = { root };
        o.appRunning   = [] { return false; };
        return o;
    }
};

// Every file and folder under `dir`, hidden ones included: path → SHA-256 ("dir" for a folder).
static QMap<QString, QByteArray> treeHash(const QString &dir)
{
    QMap<QString, QByteArray> out;
    QDirIterator it(dir, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString p = it.next();
        const QFileInfo fi(p);
        if (fi.isDir()) { out.insert(p, "dir"); continue; }
        QFile f(p);
        QCryptographicHash h(QCryptographicHash::Sha256);
        if (f.open(QIODevice::ReadOnly)) h.addData(&f);
        out.insert(p, h.result().toHex());
    }
    return out;
}

static const AthletePlan *athlete(const Result &r, const QString &token)
{
    for (const AthletePlan &p : r.athletes) if (p.token == token) return &p;
    return nullptr;
}
static const SessionPlan *session(const Result &r, const char *name)
{
    for (const SessionPlan &p : r.sessions) if (p.name == QLatin1String(name)) return &p;
    return nullptr;
}
static const GolferPlan *golfer(const Result &r, const char *name)
{
    for (const GolferPlan &p : r.golfer) if (p.name == QLatin1String(name)) return &p;
    return nullptr;
}
static GolferMove::Action moveOf(const GolferPlan *g, const char *key)
{
    if (g) for (const GolferMove &m : g->moves) if (m.key == QLatin1String(key)) return m.action;
    return GolferMove::Action::Empty;
}

// A decisions file naming one athlete's id (and, when asked, the height pick the cabin's export needs).
static void writeDecisions(const QString &path, const QString &token, const QString &id, bool height)
{
    QJsonObject a{ { QStringLiteral("athleteId"), id } };
    if (height)
        a.insert(QStringLiteral("fields"), QJsonObject{ { QStringLiteral("heightValue"), QJsonObject{ { QStringLiteral("from"), QStringLiteral("mac") } } } });
    writeJson(path, QJsonObject{ { QStringLiteral("schema"), QLatin1String(kDecisionsSchema) },
                                 { QStringLiteral("athletes"), QJsonObject{ { token, a } } } });
}

// A one-athlete library under `root`: lib/<token>/<session>/swing_0001 (its swing records `uuid`, the record's own
// id, so M1 needs no decision), the settings record, and — when asked — golfer input in diagnostics.json's older keys.
struct OneAthlete {
    QString root, lib, settings, backup, athlete, session;
    OneAthlete(const QString &r, const QString &name, const QString &token, const QString &uuid, const QString &sessionName,
               bool golferKeys)
        : root(r), lib(r + QStringLiteral("/lib")), settings(r + QStringLiteral("/cfg/PinPointStudio.ini")),
          backup(r + QStringLiteral("/backup")), athlete(lib + '/' + token), session(athlete + '/' + sessionName)
    {
        swing(session, 1, uuid, name, QStringLiteral("2026-08-01T10:00:00.000Z"), 1);
        if (golferKeys)
            writeJson(SessionFiles::diagnosticsFile(session), envelope(QJsonObject{
                { QStringLiteral("intent"), QJsonObject{ { QStringLiteral("focusConditionId"), QString() }, { QStringLiteral("focusFromShot"), -1 },
                                                         { QStringLiteral("declaredMiss"), QStringLiteral("push") } } },
                { QStringLiteral("shotIntent"), QJsonObject{ { QStringLiteral("1"), QStringLiteral("fade") } } } }));
        QSettings s(settings, QSettings::IniFormat);
        settingsRecord(s, uuid, name, 6.0);
        s.sync();
    }
    Options options(const QStringList &roots) const
    {
        Options o;
        o.library = lib; o.settingsFile = settings; o.machine = QStringLiteral("mac"); o.scratchRoots = roots;
        o.write = true; o.backupDir = backup; o.appRunning = [] { return false; };
        return o;
    }
};

static QByteArray bytesOf(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}
static bool anyFailureSays(const Result &r, const char *what)
{
    for (const QString &f : r.log.failures) if (f.contains(QLatin1String(what))) return true;
    return false;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    Fixture fx;
    // Nothing in this test may reach the real settings: ppSettings() (AthleteStore's) reads the fixture's.
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, fx.root + QStringLiteral("/cfg"));
    const QString mark = fx.lib + QStringLiteral("/Mark-Liversedge");

    // The cabin's dry run writes its records export (M1's appendix) — the file the Mac's run compares against.
    {
        Options o = fx.options();
        o.settingsFile = fx.cabinSettings;
        o.machine = QStringLiteral("cabin");
        const Result cabin = run(o);
        check(!cabin.refused, "cabin: the dry run ran");
        writeJson(fx.exportFile, exportRecords(cabin));
        check(jsonOf(fx.exportFile).value(QStringLiteral("athletes")).toArray().size() == 1, "cabin: its export holds its one record");
    }

    std::printf("A1 the dry run, with the cabin's export and no decisions\n");
    const QMap<QString, QByteArray> before = treeHash(fx.root);
    Options dry = fx.options();
    dry.otherMachineExport = fx.exportFile;
    const Result a = run(dry);
    check(!a.refused, "A1 not refused");
    const AthletePlan *am = athlete(a, QStringLiteral("Mark-Liversedge"));
    check(am && am->outcome == AthletePlan::Outcome::Blocked && am->idProvisional, "A1 Mark: blocked, the machines' ids differ");
    bool heightListed = false, createdSame = true;
    if (am) for (const FieldDifference &d : am->differences) {
        if (d.key == QLatin1String("heightValue") && d.thisMachine.toDouble() == 6.0 && d.otherMachine.toDouble() == 5.9 && !d.decided) heightListed = true;
        if (d.key == QLatin1String("createdAt")) createdSame = false;
    }
    check(heightListed, "A1 Mark: heightValue 6 here, 5.9 on the cabin, undecided");
    check(createdSame, "A1 Mark: a field the machines agree on is not listed");
    check(am && am->headerIds.size() == 2 && am->headerIds.contains(kCabin) && am->headerIds.contains(kCabin2),
          "A1 Mark: two uuids in one folder, reported");
    check(am && !am->record.contains(QStringLiteral("lastSessionAt")), "A1 Mark: the derived lastSessionAt is not migrated (DEC-89)");
    const AthletePlan *js = athlete(a, QStringLiteral("Jess-Smith"));
    check(js && js->outcome == AthletePlan::Outcome::WriteNew && !js->folderExists, "A1 Jess Smith: written, her folder made (no session yet)");
    const AthletePlan *jr = athlete(a, QStringLiteral("Jess-Ratcliffe"));
    check(jr && jr->outcome == AthletePlan::Outcome::NoRecord && jr->headerIds.contains(kJess), "A1 Jess-Ratcliffe: a folder with no record (E-02)");
    bool renamedNoted = false;
    if (jr) for (const QString &n : jr->notes) if (n.contains(QStringLiteral("Jess Smith"))) renamedNoted = true;
    check(renamedNoted, "A1 Jess-Ratcliffe: its swings' uuid is the renamed record's, said so");
    const SessionPlan *a1 = session(a, S1);
    check(a1 && a1->outcome == SessionPlan::Outcome::Skipped && a1->ownerProvisional, "A1 M2 waits for M1 where the owner is not settled");
    check(a.wrist.outcome == WristPlan::Outcome::Waits, "A1 M4 waits for M1");
    const QString mdA = reportMarkdown(a);
    check(mdA.contains(QStringLiteral("pick one")) && mdA.contains(QStringLiteral("dry run (nothing was written)")), "A1 the report asks for the pick");
    check(treeHash(fx.root) == before, "B1 the dry run wrote nothing (every file of the fixture hashed before and after)");

    std::printf("A2 the dry run with Mark's decisions\n");
    writeJson(fx.decisions, QJsonObject{
        { QStringLiteral("schema"), QLatin1String(kDecisionsSchema) },
        { QStringLiteral("athletes"), QJsonObject{ { QStringLiteral("Mark-Liversedge"), QJsonObject{
              { QStringLiteral("athleteId"), kCabin },   // the id his swings record
              { QStringLiteral("fields"), QJsonObject{ { QStringLiteral("heightValue"), QJsonObject{ { QStringLiteral("from"), QStringLiteral("mac") } } } } } } } } },
        { QStringLiteral("restore"), QJsonArray{ QJsonObject{ { QStringLiteral("session"), QStringLiteral("Mark-Liversedge/") + S3 },
                                                             { QStringLiteral("from"), QStringLiteral("diagnostics.json.pre-79ffef1") },
                                                             { QStringLiteral("field"), QStringLiteral("declaredFocus") } } } } });
    const QMap<QString, QByteArray> before2 = treeHash(fx.root);
    dry.decisionsFile = fx.decisions;
    const Result b = run(dry);
    const AthletePlan *bm = athlete(b, QStringLiteral("Mark-Liversedge"));
    check(bm && bm->outcome == AthletePlan::Outcome::WriteNew && bm->id.toString() == kCabin, "A2 Mark: written under the decided id");
    check(bm && bm->record.value(QStringLiteral("heightValue")).toDouble() == 6.0, "A2 Mark: heightValue from the mac, as decided");
    const AthleteId markId = AthleteId::fromString(kCabin);
    const SessionPlan *b1 = session(b, S1), *b2 = session(b, S2), *b3 = session(b, S3), *b4 = session(b, S4), *b5 = session(b, S5), *b6 = session(b, S6);
    check(b1 && b1->outcome == SessionPlan::Outcome::Write && b1->id == SessionIdentity::legacySessionId(markId, QLatin1String(S1)),
          "A2 M2 S1: D3's id from the settled owner");
    check(b1 && b1->startUtc == QDateTime::fromString(QStringLiteral("2026-07-10T13:00:00.000Z"), Qt::ISODateWithMs)
              && b1->endUtc == QDateTime::fromString(QStringLiteral("2026-07-10T13:01:02.000Z"), Qt::ISODateWithMs),
          "A2 M2 S1: start = the first capture time, end = the last window's end");
    check(b2 && b2->type == QLatin1String("wrist") && !b2->typeDisagreement, "A2 M2 S2: wrist, no disagreement");
    check(b3 && b3->type == QLatin1String("wrist") && b3->typeDisagreement && b3->nameType == QLatin1String("swing"),
          "A2 M2 S3: the name says swing, the shots wrist: listed, the shots' type written");
    check(b4 && b4->outcome == SessionPlan::Outcome::CorrectStart
              && b4->startUtc == QDateTime::fromString(QStringLiteral("2026-07-14T10:00:00.000Z"), Qt::ISODateWithMs),
          "A2 M2 S4: the app's backfilled start corrected from the capture time (J-19)");
    check(b5 && b5->outcome == SessionPlan::Outcome::Write && b5->timesFromFolderDate, "A2 M2 S5: no swing, the folder's date, flagged");
    check(b6 && b6->outcome == SessionPlan::Outcome::Skipped && b6->ownerFromHeaders, "A2 M2 S6: no owner, not written");
    bool orderListed = false;
    for (const DayOrderChange &c : b.orderChanges)
        if (c.day == QDate(2026, 7, 10) && c.byName.first() == QLatin1String(S1) && c.byStart.first() == QLatin1String(S2)) orderListed = true;
    check(orderListed, "A2 M2: the 10 Jul order change listed (parity B's set)");
    const GolferPlan *g2 = golfer(b, S2);
    check(g2 && moveOf(g2, "intent.declaredMiss") == GolferMove::Action::Move && moveOf(g2, "shotIntent.2") == GolferMove::Action::Move
              && moveOf(g2, "corrections.1") == GolferMove::Action::Move && moveOf(g2, "shotIntent.9") == GolferMove::Action::ShotGone,
          "A2 M3 S2: each older key → its new home; the gone shot's entry reported");
    check(g2 && g2->reviewEdits.size() == 2 && g2->keysKept == QStringList{ QStringLiteral("shotIntent") }
              && g2->keysToRemove.contains(QStringLiteral("intent")) && g2->keysToRemove.contains(QStringLiteral("corrections")),
          "A2 M3 S2: two swing files rewritten; shotIntent kept, the rest removed");
    const GolferPlan *g3 = golfer(b, S3);
    check(g3 && g3->recovered.size() == 1 && g3->recovered.first().field == QLatin1String("declaredFocus")
              && g3->recovered.first().value.contains(QStringLiteral("excessive_shaft_lean")) && g3->recovered.first().restore,
          "A2 M3 S3: the focus only a backup holds is found, and restored as decided");
    check(b.tendenciesToDrop.size() == 1, "A2 M3: Mark's tendencies.json dropped");
    check(b.wrist.outcome == WristPlan::Outcome::Write && b.wrist.shot == ShotRef(b2 ? b2->id : SessionId(), 2), "A2 M4: the path → its ShotRef");
    check(!b.otherIdReferences.isEmpty(), "A2 M1: currentAthleteUuid names the id that changes: said so");
    check(treeHash(fx.root) == before2, "B2 the dry run with decisions wrote nothing");


    std::printf("A3-A6 M1 honours or refuses every id decision (review 1, finding 1; J-58)\n");
    {
        const QString dec = fx.root + QStringLiteral("/decisions-ids.json");
        Options o = fx.options();
        o.decisionsFile = dec;
        // A3: the uuid the swings record (neither machine's record id) — J-58's recommendation.
        writeDecisions(dec, QStringLiteral("Mark-Liversedge"), kCabin2, true);
        o.otherMachineExport = fx.exportFile;
        const Result r3 = run(o);
        const AthletePlan *m3 = athlete(r3, QStringLiteral("Mark-Liversedge"));
        const SessionPlan *s3 = session(r3, S1);
        check(m3 && m3->outcome == AthletePlan::Outcome::WriteNew && m3->id.toString() == kCabin2,
              "A3 a decided uuid the swings record is honoured");
        check(s3 && s3->id == SessionIdentity::legacySessionId(AthleteId::fromString(kCabin2), QLatin1String(S1)),
              "A3 … and M2's ids follow it");
        // A4: a uuid nobody knows.
        writeDecisions(dec, QStringLiteral("Mark-Liversedge"), QStringLiteral("dddddddd-8888-4888-8888-dddddddddddd"), true);
        const Result r4 = run(o);
        const AthletePlan *m4 = athlete(r4, QStringLiteral("Mark-Liversedge"));
        check(m4 && m4->outcome == AthletePlan::Outcome::Blocked && m4->why.contains(QStringLiteral("refused")),
              "A4 a decided uuid that is no record's and no swing's is refused, with the reason");
        check(reportMarkdown(r4).contains(QStringLiteral("dddddddd-8888-4888-8888-dddddddddddd")), "A4 … the report names it");
        // A5: no export, no decision — no swing records this machine's id (J-58): needs a decision.
        o.otherMachineExport.clear();
        o.decisionsFile.clear();
        const Result r5 = run(o);
        const AthletePlan *m5 = athlete(r5, QStringLiteral("Mark-Liversedge"));
        const SessionPlan *s5 = session(r5, S1);
        check(m5 && m5->outcome == AthletePlan::Outcome::Blocked && m5->idProvisional && m5->why.contains(QStringLiteral("no swing")),
              "A5 no decision and no swing records the id M1 would settle: blocked (J-58)");
        check(s5 && s5->outcome == SessionPlan::Outcome::Skipped && s5->ownerProvisional, "A5 … and M2 waits, writing no id");
        const AthletePlan *j5 = athlete(r5, QStringLiteral("Jess-Smith"));
        check(j5 && j5->outcome == AthletePlan::Outcome::WriteNew, "A5 an athlete with no swing needs no decision");
        // A6: this machine's id, decided explicitly: honoured although no swing records it.
        o.decisionsFile = dec;
        writeDecisions(dec, QStringLiteral("Mark-Liversedge"), kMark, false);
        const Result r6 = run(o);
        const AthletePlan *m6 = athlete(r6, QStringLiteral("Mark-Liversedge"));
        check(m6 && m6->outcome == AthletePlan::Outcome::WriteNew && m6->id.toString() == kMark, "A6 this machine's id, decided, is honoured");
        QFile::remove(dec);
    }
    check(treeHash(fx.root) == before2, "A3-A6 the dry runs wrote nothing");
    std::printf("C the guards\n");
    Options w = dry;
    w.write = true;
    w.backupDir = fx.backup;
    {
        Options o = w;
        o.scratchRoots = { fx.root + QStringLiteral("/elsewhere") };
        const Result r = run(o);
        check(r.refused && r.refusal.contains(QStringLiteral("i-have-marks-approval")), "C1 a write outside the scratch roots is refused");
    }
    {
        FolderLock lock(mark + '/' + S1);
        check(lock.lock(0) == FolderLock::Result::Acquired, "C2 (a lock taken in the library)");
        const QMap<QString, QByteArray> held = treeHash(fx.root);
        const Result r = run(w);
        check(r.refused && !r.locksHeld.isEmpty(), "C2 a write while a folder lock is held is refused");
        check(treeHash(fx.root) == held, "C2 … and wrote nothing");
    }
    {
        Options o = w;
        o.appRunning = [] { return true; };
        check(run(o).refused, "C3 a write while the app runs is refused");
    }
    {
        Options o = w;
        o.backupDir = mark + QStringLiteral("/backups");
        check(run(o).refused, "C4 backups inside an athlete folder are refused");
    }
    check(treeHash(fx.root) == before2, "C the refused writes wrote nothing");

    std::printf("D the scratch write\n");
    const Result d = run(w);
    check(!d.refused && d.log.failures.isEmpty(), "D1 the write ran with no failure");
    for (const QString &f : d.log.failures) std::printf("      %s\n", qPrintable(f));
    // M1, read back through the app's own door.
    {
        SessionStore sessions;
        AthleteStore athletes([&] { return fx.lib; }, &sessions);
        const QList<AthleteId> ids = athletes.list();
        Athlete *m = athletes.load(markId);
        Athlete *j = athletes.load(AthleteId::fromString(kJess));
        check(ids.size() == 2 && m && j, "D2 M1: the app lists two athletes");
        check(m && m->source() == Athlete::Source::Library && QDir::cleanPath(m->folder()) == mark, "D2 M1: Mark from athlete.json in his folder");
        check(m && m->record().heightValue == 6.0 && m->record().bag.contains(QStringLiteral("7 IRON")), "D2 M1: his record and bag, as decided");
        check(j && j->source() == Athlete::Source::Library && QDir::cleanPath(j->folder()) == fx.lib + QStringLiteral("/Jess-Smith"),
              "D2 M1: Jess Smith in her new folder");
        check(m && m->record().wristReference == ShotRef(SessionIdentity::legacySessionId(markId, QLatin1String(S2)), 2),
              "D2 M4: the ★ on Mark's record as a ShotRef");
        QSettings s(fx.settings, QSettings::IniFormat);
        s.beginGroup(QStringLiteral("athletes"));
        check(s.childGroups().isEmpty(), "D2 M1: the settings records are gone");
        s.endGroup();
        check(s.value(QStringLiteral("currentAthleteUuid")).toString() == kCabin, "D2 M1: currentAthleteUuid re-keyed to the settled id");
        check(jsonOf(AthleteFiles::athleteFile(mark)).value(QStringLiteral("migratedFrom")).toObject().value(QStringLiteral("machine")).toString()
                  == QLatin1String("mac"), "D2 M1: migratedFrom names the machine");
    }
    // M2, read back through the session repository.
    {
        SessionStore st;
        const auto entry = [&](const char *name) { const SessionId id = st.openFolder(mark + '/' + name); return st.open(id); };
        const auto e1 = entry(S1), e3 = entry(S3), e4 = entry(S4), e5 = entry(S5);
        check(e1 && e1->hasSessionFile && e1->id() == SessionIdentity::legacySessionId(markId, QLatin1String(S1))
                  && e1->startedAtUtc == QDateTime::fromString(QStringLiteral("2026-07-10T13:00:00.000Z"), Qt::ISODateWithMs)
                  && e1->status == SessionStatus::Ended && e1->identity.type() == QLatin1String("wrist"),
              "D3 M2 S1: session.json, the same id the dry run showed, ended, wrist");
        check(jsonOf(SessionFiles::sessionFile(mark + '/' + S1)).value(QStringLiteral("origin")).toObject().value(QStringLiteral("kind")).toString()
                  == QLatin1String("backfilled"), "D3 M2 S1: origin backfilled");
        // J-18 (Mark, 15 Nov 2026): the same rule as the app's for a crash-left session — the latest swing's
        // capture end (swing 2: 13:01:00 + its 2 s window), marked as inferred.
        check(e1 && e1->endInferred
                  && e1->endedAtUtc == QDateTime::fromString(QStringLiteral("2026-07-10T13:01:02.000Z"), Qt::ISODateWithMs)
                  && jsonOf(SessionFiles::sessionFile(mark + '/' + S1)).value(QStringLiteral("endInferred")).toBool(),
              "D3 M2 S1: endedAt = the latest swing's capture end, endInferred (J-18)");
        check(e3 && e3->identity.type() == QLatin1String("wrist"), "D3 M2 S3: the shots' type");
        check(e4 && e4->id().toString() == QLatin1String("44444444-4444-4444-8444-444444444444")
                  && e4->startedAtUtc == QDateTime::fromString(QStringLiteral("2026-07-14T10:00:00.000Z"), Qt::ISODateWithMs),
              "D3 M2 S4: the app's file kept its id, its start corrected");
        check(e5 && e5->startedAtUtc == QDateTime(QDate(2026, 7, 15), QTime(0, 0), QTimeZone::systemTimeZone()).toUTC() && !e5->endedAtUtc.isValid()
                  && !e5->endInferred,
              "D3 M2 S5: the folder's date, no end (and nothing marked inferred)");
        check(e4 && !e4->endInferred, "D3 M2 S4: the app's own end time is left as the app wrote it (not marked)");
        check(!QFileInfo::exists(SessionFiles::sessionFile(fx.lib + QStringLiteral("/Jess-Ratcliffe/") + S6)), "D3 M2 S6: no owner, no file");
    }
    // M3, read back through the one reader of golfer input.
    {
        const GolferInput g = SessionDiagnosticsFiles::golferInput(mark + '/' + S2,
            SessionDiagnosticsFiles::readDiagnostics(mark + '/' + S2).envelope.legacyGolferInput);
        const SessionInputRead in = SessionDiagnosticsFiles::readSessionInput(mark + '/' + S2);
        check(in.state == SessionInputRead::State::Read && in.input.declaredMiss && *in.input.declaredMiss == QLatin1String("push"),
              "D4 M3 S2: the miss in session_input.json");
        SessionStore st;
        SwingStoreRecordingRepository door(st);
        const ShotReview r2 = door.readReview(door.shotOfFolder(mark + '/' + S2 + QStringLiteral("/swing_0002")));
        const ShotReview r1 = door.readReview(door.shotOfFolder(mark + '/' + S2 + QStringLiteral("/swing_0001")));
        check(r2.hasShotIntent && r2.shotIntent == QLatin1String("fade"), "D4 M3 S2: swing 2's intent on its review");
        check(r1.hasLabelCorrections && r1.labelCorrections.size() == 1 && r1.labelCorrections.first().to == QLatin1String("thin"),
              "D4 M3 S2: swing 1's correction on its review");
        const QJsonObject env = jsonOf(SessionFiles::diagnosticsFile(mark + '/' + S2));
        check(!env.contains(QStringLiteral("intent")) && !env.contains(QStringLiteral("corrections")) && !env.contains(QStringLiteral("screens"))
                  && env.contains(QStringLiteral("shotIntent")) && env.contains(QStringLiteral("ledger")) && env.contains(QStringLiteral("session")),
              "D4 M3 S2: the moved keys out of diagnostics.json, the gone shot's kept, the cache untouched");
        check(g.declaredMiss == QLatin1String("push") && g.shotIntent.value(2) == QLatin1String("fade") && g.shotIntent.value(9) == QLatin1String("draw")
                  && g.corrections.contains(1), "D4 M3 S2: the golfer's input reads as it did");
        const SessionInputRead in3 = SessionDiagnosticsFiles::readSessionInput(mark + '/' + S3);
        check(in3.input.declaredFocus && in3.input.declaredFocus->conditionId == QLatin1String("excessive_shaft_lean"),
              "D4 M3 S3: the recovered focus restored to session_input.json");
        check(!jsonOf(SessionFiles::diagnosticsFile(mark + '/' + S3)).contains(QStringLiteral("intent")), "D4 M3 S3: the empty older key removed");
        check(!QFileInfo::exists(AthleteFiles::tendenciesFile(mark)), "D4 M3: tendencies.json dropped");
    }
    // The backups.
    {
        const QJsonObject manifest = jsonOf(d.log.backupRoot + QStringLiteral("/manifest.json"));
        QStringList sources;
        for (const QJsonValue &v : manifest.value(QStringLiteral("files")).toArray()) sources << v.toObject().value(QStringLiteral("source")).toString();
        const auto has = [&](const QString &p) { return sources.contains(QDir::cleanPath(p)); };
        check(has(fx.settings) && has(SessionFiles::diagnosticsFile(mark + '/' + S2)) && has(SessionFiles::sessionFile(mark + '/' + S4))
                  && has(mark + '/' + S2 + QStringLiteral("/swing_0002/swing.json")) && has(AthleteFiles::tendenciesFile(mark)),
              "D5 every changed file was backed up first (manifest)");
        check(QFileInfo::exists(d.log.backupRoot + QStringLiteral("/library/Mark-Liversedge/") + S2 + QStringLiteral("/diagnostics.json")),
              "D5 the backup copy is there");
    }

    std::printf("E a second run\n");
    const QMap<QString, QByteArray> after = treeHash(fx.root);
    const Result e = run(w);
    check(!e.refused && e.writesPlanned() == 0 && e.log.written.isEmpty() && e.log.failures.isEmpty(), "E1 a second run plans and writes nothing");
    check(treeHash(fx.root) == after, "E1 … every file of the fixture is as it was (no backup made either)");
    const SessionPlan *e1 = session(e, S1);
    check(e1 && e1->outcome == SessionPlan::Outcome::AlreadyMigrated, "E1 M2 S1: already migrated");
    const AthletePlan *em = athlete(e, QStringLiteral("Mark-Liversedge"));
    check(em && em->outcome == AthletePlan::Outcome::AlreadyInLibrary, "E1 M1 Mark: already in the library");
    check(e.wrist.outcome == WristPlan::Outcome::AlreadyThere, "E1 M4: already on the record");

    std::printf("F the other machine's run, after this one (H-09: the cabin's settings record goes, the library's stands)\n");
    {
        // F0: on the cabin's run, a decision naming another id than the library's athlete.json holds is refused.
        const QString dec = fx.root + QStringLiteral("/decisions-f0.json");
        writeDecisions(dec, QStringLiteral("Mark-Liversedge"), kMark, true);
        Options o = w;
        o.settingsFile = fx.cabinSettings; o.machine = QStringLiteral("cabin"); o.otherMachineExport.clear();
        o.decisionsFile = dec; o.write = false;
        const Result r = run(o);
        const AthletePlan *m = athlete(r, QStringLiteral("Mark-Liversedge"));
        check(m && m->outcome == AthletePlan::Outcome::Blocked && m->why.contains(QStringLiteral("already holds")),
              "F0 a decision contradicting athlete.json is refused");
        QFile::remove(dec);
    }

    {
        const QByteArray markFile = jsonOf(AthleteFiles::athleteFile(mark)).isEmpty() ? QByteArray()
                                                                                       : QJsonDocument(jsonOf(AthleteFiles::athleteFile(mark))).toJson();
        Options o = w;   // the same decisions file serves both machines' runs
        o.settingsFile = fx.cabinSettings;
        o.machine = QStringLiteral("cabin");
        o.otherMachineExport.clear();
        const Result dryF = [&] { Options x = o; x.write = false; return run(x); }();
        const AthletePlan *fm = athlete(dryF, QStringLiteral("Mark-Liversedge"));
        bool decidedFromLibrary = false;
        if (fm) for (const FieldDifference &dd : fm->differences)
            if (dd.key == QLatin1String("heightValue") && dd.decided && dd.chosen.toDouble() == 6.0) decidedFromLibrary = true;
        check(fm && fm->outcome == AthletePlan::Outcome::AlreadyInLibrary && fm->settingsGroupsToRemove == QStringList{ kCabin },
              "F1 the cabin's Mark: already in the library; its settings record goes");
        check(decidedFromLibrary, "F1 the field decided \"from mac\" resolves to the library's value on the cabin's run");
        check(dryF.writesPlanned() == 1, "F1 one file to change: the cabin's settings");
        const Result f = run(o);
        check(!f.refused && f.log.failures.isEmpty() && f.log.written.isEmpty(), "F2 the cabin's write ran, writing no library file");
        QSettings s(fx.cabinSettings, QSettings::IniFormat);
        s.beginGroup(QStringLiteral("athletes"));
        check(s.childGroups().isEmpty(), "F2 the cabin's settings record is gone");
        check(QJsonDocument(jsonOf(AthleteFiles::athleteFile(mark))).toJson() == markFile, "F2 Mark's athlete.json is as the Mac's run left it");
    }


    std::printf("G the archive mirror (review 1, finding 2)\n");
    {
        QTemporaryDir g;
        const QString root = QDir::cleanPath(g.path());
        const QString kAnn = QStringLiteral("a0a0a0a0-5555-4555-8555-a0a0a0a0a0a0");
        const QString name = QStringLiteral("2026-08-01_Ann-Lee_Wrist_01");
        OneAthlete fa(root, QStringLiteral("Ann Lee"), QStringLiteral("Ann-Lee"), kAnn, name, false);
        const QString mirror = root + QStringLiteral("/archive/Ann-Lee/") + name;
        writeFile(SessionFiles::sessionFile(mirror), QByteArray("{\"old\":true}"));   // a copy already in the mirror
        writeJson(SessionFiles::archiveMarker(fa.session), QJsonObject{ { QStringLiteral("schema"), QStringLiteral("pinpoint.archive/1") },
                                                                        { QStringLiteral("location"), mirror } });
        const QMap<QString, QByteArray> pre = treeHash(root);
        // G1: the mirror is not under the scratch roots (only the library, settings and backups are): refused.
        const Result r1 = run(fa.options({ fa.lib, root + QStringLiteral("/cfg"), fa.backup }));
        check(r1.refused && r1.refusal.contains(QStringLiteral("archive mirror")), "G1 a mirror outside the scratch roots: refused");
        check(treeHash(root) == pre, "G1 … nothing written");
        // G2: a lock held in the mirror refuses the write.
        {
            FolderLock lock(mirror);
            lock.lock(0);
            const Result r2 = run(fa.options({ root }));
            check(r2.refused && !r2.locksHeld.isEmpty(), "G2 a lock held in the mirror: refused");
        }
        // G3: under the scratch roots: written, the same bytes as the library's, the old copy backed up first.
        const Result r3 = run(fa.options({ root }));
        check(!r3.refused && r3.log.failures.isEmpty(), "G3 the write ran");
        const QByteArray libFile = bytesOf(SessionFiles::sessionFile(fa.session));
        check(!libFile.isEmpty() && bytesOf(SessionFiles::sessionFile(mirror)) == libFile, "G3 the mirror's session.json = the library's");
        bool backedUp = false;
        for (const QJsonValue &v : jsonOf(r3.log.backupRoot + QStringLiteral("/manifest.json")).value(QStringLiteral("files")).toArray())
            if (v.toObject().value(QStringLiteral("source")).toString() == QDir::cleanPath(SessionFiles::sessionFile(mirror))) backedUp = true;
        check(backedUp, "G3 the mirror's old session.json was backed up first");
    }

    std::printf("H symlinks (review 1, finding 3)\n");
    {
        QTemporaryDir rdir, odir;
        const QString root = QDir::cleanPath(rdir.path()), outside = QDir::cleanPath(odir.path());
        const QString kBo = QStringLiteral("b0b0b0b0-6666-4666-8666-b0b0b0b0b0b0");
        OneAthlete fb(outside, QStringLiteral("Bo Ng"), QStringLiteral("Bo-Ng"), kBo, QStringLiteral("2026-08-02_Bo-Ng_Wrist_01"), false);
        const QMap<QString, QByteArray> pre = treeHash(outside);
        QDir().mkpath(root + QStringLiteral("/cfg"));
        QFile::copy(fb.settings, root + QStringLiteral("/cfg/PinPointStudio.ini"));
        // H1: --library is a symlink under scratch to a library outside it.
        QFile::link(fb.lib, root + QStringLiteral("/linklib"));
        Options o1 = fb.options({ root });
        o1.library = root + QStringLiteral("/linklib");
        o1.settingsFile = root + QStringLiteral("/cfg/PinPointStudio.ini");
        o1.backupDir = root + QStringLiteral("/backup");
        const Result r1 = run(o1);
        check(r1.refused && r1.refusal.contains(QStringLiteral("i-have-marks-approval")), "H1 a library that is a symlink out of scratch: refused");
        // H2: a real scratch library whose athlete folder is a symlink out of it: every write there refused.
        QDir().mkpath(root + QStringLiteral("/lib2"));
        QFile::link(fb.athlete, root + QStringLiteral("/lib2/Bo-Ng"));
        Options o2 = o1;
        o2.library = root + QStringLiteral("/lib2");
        const Result r2 = run(o2);
        check(!r2.refused && anyFailureSays(r2, "outside the library") && r2.log.written.isEmpty(),
              "H2 an athlete folder that resolves outside the library: its writes refused, nothing written");
        check(treeHash(outside) == pre, "H1-H2 the folder outside scratch is untouched");
        // H3: a real scratch athlete and session whose SWING folder is a symlink out of scratch (review 2): M3 writes
        // nothing for that session — no review, no session_input.json, diagnostics.json and its keys as they were.
        const QString kDi = QStringLiteral("d1d1d1d1-9999-4999-8999-d1d1d1d1d1d1");
        const QString sess = root + QStringLiteral("/lib3/Di-Ra/2026-08-04_Di-Ra_Wrist_01");
        swing(outside + QStringLiteral("/swingDi"), 1, kDi, QStringLiteral("Di Ra"), QStringLiteral("2026-08-04T10:00:00.000Z"), 1);
        const QMap<QString, QByteArray> preDi = treeHash(outside);
        QDir().mkpath(sess);
        QFile::link(outside + QStringLiteral("/swingDi/swing_0001"), sess + QStringLiteral("/swing_0001"));
        writeJson(SessionFiles::diagnosticsFile(sess), envelope(QJsonObject{
            { QStringLiteral("intent"), QJsonObject{ { QStringLiteral("focusConditionId"), QString() }, { QStringLiteral("focusFromShot"), -1 },
                                                     { QStringLiteral("declaredMiss"), QStringLiteral("push") } } },
            { QStringLiteral("shotIntent"), QJsonObject{ { QStringLiteral("1"), QStringLiteral("fade") } } } }));
        const QByteArray diagWas = bytesOf(SessionFiles::diagnosticsFile(sess));
        const QString ini3 = root + QStringLiteral("/cfg3/PinPointStudio.ini");
        { QSettings s3(ini3, QSettings::IniFormat); settingsRecord(s3, kDi, QStringLiteral("Di Ra"), 6.0); s3.sync(); }
        Options o3 = o1;
        o3.library = root + QStringLiteral("/lib3");
        o3.settingsFile = ini3;
        const Result r3 = run(o3);
        check(!r3.refused && anyFailureSays(r3, "nothing written for this session"), "H3 a swing folder that resolves out of scratch: M3 refused, reported");
        check(bytesOf(SessionFiles::diagnosticsFile(sess)) == diagWas && !QFileInfo::exists(SessionFiles::sessionInputFile(sess)),
              "H3 … no session_input.json, diagnostics.json and its keys as they were");
        check(treeHash(outside).value(outside + QStringLiteral("/swingDi/swing_0001/swing.json")) == preDi.value(outside + QStringLiteral("/swingDi/swing_0001/swing.json")),
              "H3 … the swing document outside scratch untouched");
        check(treeHash(outside) == preDi, "H3 the folder outside scratch is untouched");
        QSettings s(o2.settingsFile, QSettings::IniFormat);
        s.beginGroup(QStringLiteral("athletes"));
        check(s.childGroups().size() == 1, "H2 … and its settings record is kept");
    }

    std::printf("I M3 fails safe (review 1, finding 4)\n");
    {
        const QString kCy = QStringLiteral("c0c0c0c0-7777-4777-8777-c0c0c0c0c0c0");
        const QString name = QStringLiteral("2026-08-03_Cy-Do_Wrist_01");
        // I1: the review cannot be written (a read-only swing folder): diagnostics.json and its keys come through as they were.
        {
            QTemporaryDir t;
            OneAthlete fc(QDir::cleanPath(t.path()), QStringLiteral("Cy Do"), QStringLiteral("Cy-Do"), kCy, name, true);
            const QString diag = SessionFiles::diagnosticsFile(fc.session);
            const QByteArray was = bytesOf(diag);
            const QString swingDir = fc.session + QStringLiteral("/swing_0001");
            QFile::setPermissions(swingDir + QStringLiteral("/swing.json"), QFileDevice::ReadOwner | QFileDevice::ReadUser);
            QFile::setPermissions(swingDir, QFileDevice::ReadOwner | QFileDevice::ExeOwner | QFileDevice::ReadUser | QFileDevice::ExeUser);
            const Result r = run(fc.options({ fc.root }));
            QFile::setPermissions(swingDir, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
            QFile::setPermissions(swingDir + QStringLiteral("/swing.json"), QFileDevice::ReadOwner | QFileDevice::WriteOwner);
            check(anyFailureSays(r, "review not written"), "I1 the failed review write is reported");
            check(bytesOf(diag) == was && jsonOf(diag).contains(QStringLiteral("intent")) && jsonOf(diag).contains(QStringLiteral("shotIntent")),
                  "I1 diagnostics.json byte-identical, its older keys kept");
        }
        // I2: the check fails (the new home is lost before it): diagnostics.json and its keys come through as they were.
        {
            QTemporaryDir t;
            OneAthlete fc(QDir::cleanPath(t.path()), QStringLiteral("Cy Do"), QStringLiteral("Cy-Do"), kCy, name, true);
            const QString diag = SessionFiles::diagnosticsFile(fc.session);
            const QByteArray was = bytesOf(diag);
            Options o = fc.options({ fc.root });
            o.afterNewHomesForTest = [](const QString &folder) { QFile::remove(SessionFiles::sessionInputFile(folder)); };
            const Result r = run(o);
            check(anyFailureSays(r, "the check failed"), "I2 the failed check is reported");
            check(bytesOf(diag) == was, "I2 diagnostics.json byte-identical, its older keys kept");
        }
    }

    std::printf(g_fail ? "migrate_step2_test: %d FAILED\n" : "migrate_step2_test: ALL PASSED\n", g_fail);
    return g_fail ? 1 : 0;
}
