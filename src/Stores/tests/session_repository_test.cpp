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

// SessionRepository (SessionStore) — the session file, Start / resume / End (which never discards: the empty
// discard is Session::end's from step 3.4a-ii, session_test S3), the identity map, enumerate, and the one swing
// allocator (refactor step 2.2; brief §4.3, §4.4, §4.13).
//
// ⚠ EVERY EXPECTATION COMES FROM A STATED CONTRACT OR FROM THE FILESYSTEM, NEVER FROM RUNNING THE CODE.
// The contracts: src/DomainModel/session_repository.h and src/Stores/session_store.h (the file's keys),
// and the decisions they cite — DEC-91 (a swing number is never reissued), DEC-93 (the empty discard),
// DEC-134 (resume: today's session of the same type, same id), DEC-78/DEC-73 (one InProgress per
// athlete), DEC-136 (the typed entry), DEC-150 (an old session's id), DEC-154 (D7), and J-18 (Mark, 15 Nov
// 2026, build/refactor/step-2/JUDGEMENT-ANSWERS.md: a crash-left session's end inferred and marked so; J).
//
// EVERY TRASH STAYS IN THE TEMPORARY FOLDER: each store is set with setOsTrashForTest(false), so a session's
// and a swing's trash land in the library's own .pinpoint-trash (asserted in R5, S14), never in the OS trash of
// the machine that runs the suite (step 3.4a, review 1).
//
// Sections N, A, R, B, E and K are swing_paths_test's allocator sections, moved here with the allocator
// (they were written against SwingPaths' instance half, deleted in this step); F, L, I, O, D, X are new.
//
//   cmake --build build/tests --target session_repository_test -j 6
//   ctest --test-dir build/tests -R '^session_repository_test$' --output-on-failure

#include "../session_store.h"
#include "../session_files.h"
#include "../swing_paths.h"
#include "temp_library.h"

#include "../../Core/folder_lock.h"

#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QTimeZone>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <cstdio>
#include <thread>
#include "../../Core/tests/isolated_stores.h"   // #187: the suite links pp_stores, whose stores read the settings

using namespace pinpoint;
using namespace pinpoint::test;

static int g_fail = 0;
static int g_run  = 0;
static int g_skip = 0;

static void check(const QString &label, bool ok)
{
    ++g_run;
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", qPrintable(label));
    if (!ok) ++g_fail;
}
static void checkEq(const QString &label, const QString &got, const QString &want)
{
    check(QStringLiteral("%1 (got \"%2\", want \"%3\")").arg(label, got, want), got == want);
}
static void checkEqInt(const QString &label, int got, int want)
{
    check(QStringLiteral("%1 (got %2, want %3)").arg(label).arg(got).arg(want), got == want);
}
static void skip(const QString &label, const QString &why)
{
    ++g_skip;
    std::printf("  [SKIP] %s — %s\n", qPrintable(label), qPrintable(why));
}

static QString today() { return QDate::currentDate().toString(Qt::ISODate); }

// The suite's athlete: name as typed, the token it sanitises to, and its id.
static const QString   kName  = QStringLiteral("Mark Liversedge");
static const QString   kToken = QStringLiteral("Mark-Liversedge");
static const AthleteId kAthlete = AthleteId::fromString(u"3f2c8a4e-1b7d-4c9a-9e21-6a0d5c1b2a10");
static const QString   kType  = QStringLiteral("swing");   // by name (DEC-88)
static const QString   kLabel = QStringLiteral("Swing");   // its folder token (SET-04)
static const QString   kPat   = QStringLiteral("date-name-type");

static SessionOwner owner(const TempLibrary &lib) { return { kAthlete, kName, lib.root() }; }

// The default pattern's base, from the documented token order — not read back.
static QString baseDefault() { return today() + QLatin1Char('_') + kToken + QLatin1Char('_') + kLabel; }

static QJsonObject readJson(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}
// Plain file writes and reads, for planting fixtures and comparing bytes (not the code under test).
static bool plant(const QString &path, const QByteArray &bytes)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}
static QByteArray bytesOf(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}
static QJsonObject sessionFile(const QString &dir) { return readJson(SessionFiles::sessionFile(dir)); }
// What the library's own trash holds for the suite's athlete (FILE-LIB-01: <root>/.pinpoint-trash/<athlete>/),
// where every trash of this suite goes (SessionStore::setOsTrashForTest(false)), never the OS trash.
static QStringList libraryTrash(const TempLibrary &lib)
{
    return QDir(lib.root() + QStringLiteral("/.pinpoint-trash/") + kToken).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
}
static bool trashedAs(const TempLibrary &lib, const QString &suffix)
{
    const QStringList in = libraryTrash(lib);
    return in.size() == 1 && in.first().endsWith(QLatin1Char('_') + suffix);
}

static QString folderOf(SessionStore &store, const SessionId &id)
{
    const auto e = store.open(id);
    return e ? e->folder : QString();
}

// ---------------------------------------------------------------------------
// F — Start writes the session file (FILE-SESS-01; IM-S2-1)
// ---------------------------------------------------------------------------
static void testStartWritesTheFile()
{
    std::printf("\nF — Start writes session.json\n");
    TempLibrary lib;
    if (!lib.valid()) { skip(QStringLiteral("F"), QStringLiteral("no temp dir")); return; }
    SessionStore store;
    store.setOsTrashForTest(false);   // every trash into the library's own
    const QDateTime t0 = QDateTime(QDate(2026, 11, 15), QTime(10, 4, 5, 123), QTimeZone::UTC);
    store.setClockForTest([t0] { return t0; });
    store.setTimeZoneForTest(QTimeZone::UTC);

    const SessionStartResult s = store.start(owner(lib), QStringLiteral("wrist"), kPat);
    check(QStringLiteral("F1 the session started"), s.ok());
    checkEq(QStringLiteral("F2 its folder is named by SET-04 with the start's day"), QFileInfo(s.folder).fileName(),
            QStringLiteral("2026-11-15_Mark-Liversedge_Wrist_01"));
    const QJsonObject f = sessionFile(s.folder);
    checkEq(QStringLiteral("F3 schema"), f.value("schema").toString(), QStringLiteral("pinpoint.session/1"));
    checkEq(QStringLiteral("F4 sessionId is the id handed back"), f.value("sessionId").toString(), s.id.toString());
    check(QStringLiteral("F5 and is a random (v4) uuid"), s.id.uuid().version() == QUuid::Random);
    checkEq(QStringLiteral("F6 athleteId"), f.value("athleteId").toString(), kAthlete.toString());
    checkEq(QStringLiteral("F7 type by name"), f.value("type").toString(), QStringLiteral("wrist"));
    checkEq(QStringLiteral("F8 status"), f.value("status").toString(), QStringLiteral("inProgress"));
    checkEq(QStringLiteral("F9 startedAt, UTC with milliseconds"), f.value("startedAt").toString(),
            QStringLiteral("2026-11-15T10:04:05.123Z"));
    check(QStringLiteral("F10 no endedAt while in progress"), !f.contains("endedAt"));
    checkEq(QStringLiteral("F11 origin captured"), f.value("origin").toObject().value("kind").toString(),
            QStringLiteral("captured"));
    check(QStringLiteral("F12 no lock file is left behind"),
          !TempLibrary::exists(s.folder + QLatin1Char('/') + SessionFiles::lockFileName()));

    store.setClockForTest([t0] { return t0.addSecs(3600); });
    const WriteResult e = store.end(s.id);
    const QJsonObject g = sessionFile(s.folder);
    check(QStringLiteral("F13 End writes"), e.changed());
    checkEq(QStringLiteral("F14 status ended"), g.value("status").toString(), QStringLiteral("ended"));
    checkEq(QStringLiteral("F15 endedAt"), g.value("endedAt").toString(), QStringLiteral("2026-11-15T11:04:05.123Z"));
    checkEq(QStringLiteral("F16 the start is unchanged"), g.value("startedAt").toString(),
            QStringLiteral("2026-11-15T10:04:05.123Z"));

    // Keys this build does not know survive a rewrite.
    QJsonObject withExtra = g;
    withExtra.insert("futureKey", 42);
    plant(SessionFiles::sessionFile(s.folder), QJsonDocument(withExtra).toJson());
    SessionStore fresh;
    fresh.setClockForTest([t0] { return t0.addSecs(7200); });
    fresh.setTimeZoneForTest(QTimeZone::UTC);
    const SessionId again = fresh.todaysSession(owner(lib), QStringLiteral("wrist"));
    checkEq(QStringLiteral("F17 a new store finds the session by its file's id"), again.toString(), s.id.toString());
    fresh.resume(again, QStringLiteral("wrist"));
    checkEqInt(QStringLiteral("F18 an unknown key is kept on rewrite"),
               sessionFile(s.folder).value("futureKey").toInt(), 42);

    // A newer schema is read-only: no write touches it.
    QJsonObject newer = sessionFile(s.folder);
    newer.insert("schema", QStringLiteral("pinpoint.session/2"));
    const QByteArray newerBytes = QJsonDocument(newer).toJson();
    plant(SessionFiles::sessionFile(s.folder), newerBytes);
    SessionStore third;
    const SessionId id3 = third.openFolder(s.folder);
    check(QStringLiteral("F19 a newer file is read (its id)"), id3 == s.id && third.open(id3)->readOnly);
    check(QStringLiteral("F20 End refuses it (ReadOnly)"),
          third.end(id3).status == WriteResult::Status::ReadOnly);
    check(QStringLiteral("F21 resume refuses it"), !third.resume(id3, QStringLiteral("wrist")).ok());
    check(QStringLiteral("F22 allocate refuses it"), !third.allocateSwing(id3).ok());
    check(QStringLiteral("F23 and the file is byte-identical"), bytesOf(SessionFiles::sessionFile(s.folder)) == newerBytes);
}

// ---------------------------------------------------------------------------
// N — the composed session name honours the documented patterns
// ---------------------------------------------------------------------------
static void testNamingPatterns()
{
    std::printf("\nN — session naming patterns\n");
    struct Case { const char *pattern; QString want; };
    const Case cases[] = {
        { "date-name-type", today() + "_" + kToken + "_" + kLabel },
        { "date-type-name", today() + "_" + kLabel + "_" + kToken },
        { "name-date-type", kToken  + "_" + today() + "_" + kLabel },
        { "date-only",      today() },
    };
    for (const Case &c : cases) {
        TempLibrary lib;
        if (!lib.valid()) { skip(QStringLiteral("N %1").arg(c.pattern), QStringLiteral("no temp dir")); continue; }
        SessionStore store;
        store.setOsTrashForTest(false);   // every trash into the library's own
        const SessionStartResult s = store.start(owner(lib), kType, QString::fromLatin1(c.pattern));
        checkEq(QStringLiteral("N %1 folder name").arg(c.pattern), QFileInfo(s.folder).fileName(),
                c.want + QStringLiteral("_01"));
    }
    {
        TempLibrary lib;
        if (lib.valid()) {
            SessionStore store;
            store.setOsTrashForTest(false);   // every trash into the library's own
            const QString name = QFileInfo(store.start(owner(lib), QString(), kPat).folder).fileName();
            check(QStringLiteral("N5 an untyped session leaves no dangling separator (\"%1\")").arg(name),
                  !name.contains(QStringLiteral("__")) && !name.endsWith(QLatin1Char('_')));
        }
    }
    // Every type's folder label reads back as the type (the folder name and the file agree).
    for (const QString &t : { QStringLiteral("swing"), QStringLiteral("wrist"), QStringLiteral("grf"),
                              QStringLiteral("coach") })
        checkEq(QStringLiteral("N6 %1's label reads back as its type").arg(t),
                SessionIdentity::typeFromToken(SessionStore::folderTypeLabel(t)), t);
}

// ---------------------------------------------------------------------------
// A — allocateSwing(): where a swing lands
// ---------------------------------------------------------------------------
static void testAllocate()
{
    std::printf("\nA — swing allocation\n");
    {
        TempLibrary lib;
        if (!lib.valid()) { skip(QStringLiteral("A1"), QStringLiteral("no temp dir")); return; }
        SessionStore store;
        store.setOsTrashForTest(false);   // every trash into the library's own
        const SessionStartResult s = store.start(owner(lib), kType, kPat);
        const SwingAllocation a1 = store.allocateSwing(s.id);
        checkEq(QStringLiteral("A1 the first swing is swing_0001"), a1.swingName, QStringLiteral("swing_0001"));
        check(QStringLiteral("A2 its folder is created on disk"), TempLibrary::exists(a1.swingFolder));
        checkEqInt(QStringLiteral("A3 its ShotRef's number matches its name"), a1.shot.swingNumber(), 1);
        check(QStringLiteral("A3b and its ShotRef names the session"), a1.shot.session() == s.id);
        const SwingAllocation a2 = store.allocateSwing(s.id);
        checkEq(QStringLiteral("A4 the second is swing_0002"), a2.swingName, QStringLiteral("swing_0002"));
        checkEq(QStringLiteral("A5 both are in the session's folder"), QFileInfo(a2.swingFolder).absolutePath(),
                QFileInfo(s.folder).absoluteFilePath());
        checkEq(QStringLiteral("A6 the session folder's name is reported (provenance)"), a2.sessionFolderName,
                QFileInfo(s.folder).fileName());
        checkEq(QStringLiteral("A7 swingFolder(ShotRef) is the folder made"), store.swingFolder(a2.shot), a2.swingFolder);
        const QList<ShotRef> shots = store.shots(s.id);
        check(QStringLiteral("A8 shots() lists both, by number"),
              shots.size() == 2 && shots.at(0) == a1.shot && shots.at(1) == a2.shot);
        check(QStringLiteral("A9 an unknown session allocates nothing"),
              !store.allocateSwing(SessionId::create()).ok());
    }
    // The unwritable library: Start reports it; nothing is half made.
    {
        TempLibrary lib;
        if (!lib.valid()) { skip(QStringLiteral("A10"), QStringLiteral("no temp dir")); return; }
        if (!makeUnwritable(lib.root())) {
            skip(QStringLiteral("A10 unwritable library"), QStringLiteral("the platform will not make a directory unwritable here"));
        } else {
            SessionStore store;
            store.setOsTrashForTest(false);   // every trash into the library's own
            const SessionStartResult s = store.start(owner(lib), kType, kPat);
            check(QStringLiteral("A10 an unwritable library starts no session"), !s.ok() && s.id.isNull());
            makeWritable(lib.root());
        }
    }
    // Two athletes: two athlete folders.
    {
        TempLibrary lib;
        if (!lib.valid()) { skip(QStringLiteral("A11"), QStringLiteral("no temp dir")); return; }
        SessionStore store;
        store.setOsTrashForTest(false);   // every trash into the library's own
        const SessionStartResult a = store.start(owner(lib), kType, kPat);
        const SessionStartResult b = store.start(
            { AthleteId::fromString(u"a1b2c3d4-0000-4000-8000-000000000002"), QStringLiteral("Other Golfer"), lib.root() },
            kType, kPat);
        check(QStringLiteral("A11 another athlete's session is in another athlete folder"),
              QFileInfo(b.folder).path() == lib.athleteDir(QStringLiteral("Other-Golfer")) && a.folder != b.folder);
        checkEq(QStringLiteral("A12 and the first athlete's is still in progress"),
                sessionFile(a.folder).value("status").toString(), QStringLiteral("inProgress"));
    }
}

// ---------------------------------------------------------------------------
// R — a swing number is never reissued in a session (#56; DEC-91)
// ---------------------------------------------------------------------------
static void testNumberingAfterDelete()
{
    std::printf("\nR — numbers are never reissued\n");
    {
        TempLibrary lib;
        if (!lib.valid()) { skip(QStringLiteral("R1"), QStringLiteral("no temp dir")); return; }
        SessionStore store;
        store.setOsTrashForTest(false);   // every trash into the library's own
        const SessionId id = store.start(owner(lib), kType, kPat).id;
        SwingAllocation a;
        for (int i = 0; i < 3; ++i) a = store.allocateSwing(id);
        QDir(a.swingFolder).removeRecursively();
        checkEq(QStringLiteral("R1 deleting the newest swing does not hand its number on"),
                store.allocateSwing(id).swingName, QStringLiteral("swing_0004"));
    }
    {
        TempLibrary lib;
        if (!lib.valid()) { skip(QStringLiteral("R2"), QStringLiteral("no temp dir")); return; }
        SessionStore store;
        store.setOsTrashForTest(false);   // every trash into the library's own
        const SessionId id = store.start(owner(lib), kType, kPat).id;
        QStringList dirs;
        for (int i = 0; i < 5; ++i) dirs << store.allocateSwing(id).swingFolder;
        QDir(dirs[2]).removeRecursively();
        QDir(dirs[3]).removeRecursively();
        const SwingAllocation next = store.allocateSwing(id);
        checkEq(QStringLiteral("R2 deleting 3 and 4 of 1-5: the next is 6, not 4"), next.swingName, QStringLiteral("swing_0006"));
        checkEqInt(QStringLiteral("R3 and its ShotRef says the same"), next.shot.swingNumber(), 6);
        checkEqInt(QStringLiteral("R3b the high-water records it"), SessionFiles::recordedSwingHighWater(QFileInfo(next.swingFolder).path()), 6);
    }
    // Across a restart: a new store has no memory; the folder has to.
    {
        TempLibrary lib;
        if (!lib.valid()) { skip(QStringLiteral("R4"), QStringLiteral("no temp dir")); return; }
        QString newest;
        {
            SessionStore store;
            store.setOsTrashForTest(false);   // every trash into the library's own
            const SessionId id = store.start(owner(lib), kType, kPat).id;
            for (int i = 0; i < 2; ++i) newest = store.allocateSwing(id).swingFolder;
            store.end(id);
        }
        QDir(newest).removeRecursively();
        SessionStore again;
        again.setOsTrashForTest(false);   // every trash into the library's own
        const SessionId id = again.todaysSession(owner(lib), kType);
        check(QStringLiteral("R4a today's session is found again"), !id.isNull() && again.resume(id, kType).ok());
        checkEq(QStringLiteral("R4 a resumed session after a restart does not reuse it either"),
                again.allocateSwing(id).swingName, QStringLiteral("swing_0003"));
    }
    // A session from before the record existed: the swing that goes to the trash records the number.
    {
        TempLibrary lib;
        if (!lib.valid()) { skip(QStringLiteral("R5"), QStringLiteral("no temp dir")); return; }
        const QString session = lib.makeSession(kToken, baseDefault() + QStringLiteral("_01"), 3);
        if (!SwingPaths::trashPath(session + QStringLiteral("/swing_0003"), nullptr, /*useOsTrash=*/false)) {
            skip(QStringLiteral("R5"), QStringLiteral("the temp volume refused the move"));
        } else {
            SessionStore store;
            store.setOsTrashForTest(false);   // every trash into the library's own
            const SessionId id = store.todaysSession(owner(lib), kType);
            store.resume(id, kType);
            checkEq(QStringLiteral("R5 an older session's trashed newest swing keeps its number"),
                    store.allocateSwing(id).swingName, QStringLiteral("swing_0004"));
            check(QStringLiteral("R5b the swing is in the library's own trash, named for its session"),
                  trashedAs(lib, QFileInfo(session).fileName() + QStringLiteral("_swing_0003")));
        }
    }
}

// ---------------------------------------------------------------------------
// B — Start, and resume (extend today's): MD-05, DEC-134
// ---------------------------------------------------------------------------
static void testStartAndResume()
{
    std::printf("\nB — start, and resume today's\n");
    {
        TempLibrary lib;
        if (!lib.valid()) { skip(QStringLiteral("B1"), QStringLiteral("no temp dir")); return; }
        SessionStore store;
        store.setOsTrashForTest(false);   // every trash into the library's own
        check(QStringLiteral("B1 no session today before the first Start"), store.todaysSession(owner(lib), kType).isNull());
        const SessionStartResult s1 = store.start(owner(lib), kType, kPat);
        checkEq(QStringLiteral("B2 the first session is _01"), QFileInfo(s1.folder).fileName(), baseDefault() + QStringLiteral("_01"));
        check(QStringLiteral("B3 it is on disk"), TempLibrary::exists(s1.folder));
        store.end(s1.id);
        const SessionStartResult s2 = store.start(owner(lib), kType, kPat);
        checkEq(QStringLiteral("B4 a second new session is _02"), QFileInfo(s2.folder).fileName(), baseDefault() + QStringLiteral("_02"));
        check(QStringLiteral("B5 a new session has a new id"), s1.id != s2.id);
        store.end(s2.id);

        // Resume keeps the id (DEC-77).
        const SessionId latest = store.todaysSession(owner(lib), kType);
        checkEq(QStringLiteral("B6 today's latest session of the type is the second"), latest.toString(), s2.id.toString());
        check(QStringLiteral("B7 resume succeeds"), store.resume(latest, kType).ok());
        const QJsonObject f = sessionFile(s2.folder);
        checkEq(QStringLiteral("B8 it keeps its SessionId"), f.value("sessionId").toString(), s2.id.toString());
        checkEq(QStringLiteral("B9 and is in progress again"), f.value("status").toString(), QStringLiteral("inProgress"));
        check(QStringLiteral("B10 its end time is cleared"), !f.contains("endedAt"));
        checkEqInt(QStringLiteral("B11 resume adds no session folder"), TempLibrary::countSessions(lib.athleteDir(kToken)), 2);
        // Of another type: not today's session for that type, and resume refuses it.
        check(QStringLiteral("B12 another type has no session today"), store.todaysSession(owner(lib), QStringLiteral("wrist")).isNull());
        check(QStringLiteral("B13 resume of another type is refused"), !store.resume(s1.id, QStringLiteral("wrist")).ok());
    }
    // Only a session started TODAY.
    {
        TempLibrary lib;
        if (!lib.valid()) { skip(QStringLiteral("B14"), QStringLiteral("no temp dir")); return; }
        SessionStore store;
        store.setOsTrashForTest(false);   // every trash into the library's own
        const QDateTime now = QDateTime::currentDateTimeUtc();
        store.setClockForTest([now] { return now.addDays(-1); });
        const SessionStartResult y = store.start(owner(lib), kType, kPat);
        store.end(y.id);
        store.setClockForTest([now] { return now; });
        check(QStringLiteral("B14 yesterday's session is not today's"), store.todaysSession(owner(lib), kType).isNull());
        check(QStringLiteral("B15 and resume refuses it"), !store.resume(y.id, kType).ok());
        checkEq(QStringLiteral("B16 its file still says ended"), sessionFile(y.folder).value("status").toString(),
                QStringLiteral("ended"));
    }
    // The most recent of several legacy folders; a new Start continues the counter.
    {
        TempLibrary lib;
        if (!lib.valid()) { skip(QStringLiteral("B17"), QStringLiteral("no temp dir")); return; }
        lib.makeSession(kToken, baseDefault() + QStringLiteral("_01"), 1);
        lib.makeSession(kToken, baseDefault() + QStringLiteral("_02"), 1);
        const QString third = lib.makeSession(kToken, baseDefault() + QStringLiteral("_03"), 1);
        SessionStore store;
        store.setOsTrashForTest(false);   // every trash into the library's own
        checkEq(QStringLiteral("B17 the most recent legacy session is today's"), folderOf(store, store.todaysSession(owner(lib), kType)), third);
        checkEq(QStringLiteral("B18 a new session continues the counter"),
                QFileInfo(store.start(owner(lib), kType, kPat).folder).fileName(), baseDefault() + QStringLiteral("_04"));
        // ...and with a session file, the new one is now today's latest.
        const SessionId latest = store.todaysSession(owner(lib), kType);
        checkEq(QStringLiteral("B19 a session-file session outranks the older legacy ones"),
                QFileInfo(folderOf(store, latest)).fileName(), baseDefault() + QStringLiteral("_04"));
    }
}

// ---------------------------------------------------------------------------
// L — a legacy folder: read as D3 says, never written by a read (DEC-150)
// ---------------------------------------------------------------------------
static void testLegacy()
{
    std::printf("\nL — a session older than step 2\n");
    TempLibrary lib;
    if (!lib.valid()) { skip(QStringLiteral("L"), QStringLiteral("no temp dir")); return; }
    const QString name    = baseDefault() + QStringLiteral("_01");
    const QString session = lib.makeSession(kToken, name, 2);
    SessionStore store;
    store.setOsTrashForTest(false);   // every trash into the library's own
    const QList<SessionIndexEntry> all = store.enumerate(owner(lib));
    check(QStringLiteral("L1 enumerate lists it"), all.size() == 1);
    if (all.size() != 1) return;
    const SessionIndexEntry &e = all.first();
    checkEq(QStringLiteral("L2 its id is D3's"), e.id().toString(), SessionIdentity::legacySessionId(kAthlete, name).toString());
    check(QStringLiteral("L3 it has no session file, and is Ended"), !e.hasSessionFile && e.status == SessionStatus::Ended);
    checkEq(QStringLiteral("L4 its type is the name's"), e.identity.type(), kType);
    check(QStringLiteral("L5 its day is the name's"), e.identity.localDay() == QDate::currentDate());
    store.todaysSession(owner(lib), kType);
    store.openFolder(session, kAthlete);
    store.shots(e.id());
    check(QStringLiteral("L6 enumerate / todaysSession / openFolder / shots wrote nothing"),
          !TempLibrary::exists(SessionFiles::sessionFile(session)));
    check(QStringLiteral("L7 End of a session nothing resumed writes nothing"),
          store.end(e.id()).status == WriteResult::Status::Unchanged && !TempLibrary::exists(SessionFiles::sessionFile(session))
              && TempLibrary::countSwings(session) == 2);
    checkEqInt(QStringLiteral("L8 its shots are its swing folders"), int(store.shots(e.id()).size()), 2);
    checkEq(QStringLiteral("L9 openFolder gives the same id"), store.openFolder(session, kAthlete).toString(), e.id().toString());
    check(QStringLiteral("L10 without an athlete a legacy folder has no id"), store.openFolder(session).isNull());

    // Its first write — a resume — writes its file, with D3's id and an inferred start, saying so.
    check(QStringLiteral("L11 resume of today's legacy session"), store.resume(e.id(), kType).ok());
    const QJsonObject f = sessionFile(session);
    checkEq(QStringLiteral("L12 the file carries D3's id"), f.value("sessionId").toString(), e.id().toString());
    checkEq(QStringLiteral("L13 origin backfilled"), f.value("origin").toObject().value("kind").toString(), QStringLiteral("backfilled"));
    check(QStringLiteral("L14 with what it read"), !f.value("origin").toObject().value("read").toString().isEmpty());
    check(QStringLiteral("L15 and a start time"), QDateTime::fromString(f.value("startedAt").toString(), Qt::ISODateWithMs).isValid());
    // A corrupt session file is never overwritten.
    {
        const QString other = lib.makeSession(kToken, baseDefault() + QStringLiteral("_02"), 1);
        plant(SessionFiles::sessionFile(other), "{ not json");
        SessionStore s2;
        const SessionId id = s2.openFolder(other, kAthlete);
        check(QStringLiteral("L16 a corrupt file is read as its folder (D3), read-only"), !id.isNull() && s2.open(id)->readOnly);
        s2.end(id);
        s2.resume(id, kType);
        check(QStringLiteral("L17 and no write replaces it"), bytesOf(SessionFiles::sessionFile(other)) == "{ not json");
    }
}

// ---------------------------------------------------------------------------
// I — one InProgress session per athlete (DEC-78, DEC-73)
// ---------------------------------------------------------------------------
static void testOneInProgress()
{
    std::printf("\nI — one session in progress per athlete\n");
    TempLibrary lib;
    if (!lib.valid()) { skip(QStringLiteral("I"), QStringLiteral("no temp dir")); return; }
    QString crashed;
    {
        SessionStore store;   // a run that crashed: started, never ended
        crashed = store.start(owner(lib), kType, kPat).folder;
    }
    SessionStore store;
    store.setOsTrashForTest(false);   // every trash into the library's own
    const SessionStartResult s = store.start(owner(lib), QStringLiteral("wrist"), kPat);
    const QJsonObject f = sessionFile(crashed);
    checkEq(QStringLiteral("I1 the athlete's next Start ends the crash-left session"), f.value("status").toString(), QStringLiteral("ended"));
    check(QStringLiteral("I2 no swing records a capture time: no made-up end time"), !f.contains("endedAt") && !f.contains("endInferred"));
    int inProgress = 0;
    for (const SessionIndexEntry &e : store.enumerate(owner(lib)))
        inProgress += e.status == SessionStatus::InProgress ? 1 : 0;
    checkEqInt(QStringLiteral("I3 one session in progress"), inProgress, 1);
    // A resume ends the other one too.
    store.end(s.id);
    const SessionStartResult s3 = store.start(owner(lib), QStringLiteral("wrist"), kPat);
    store.resume(store.openFolder(crashed), kType);
    checkEq(QStringLiteral("I4 resuming one session ends the other in progress"),
            sessionFile(s3.folder).value("status").toString(), QStringLiteral("ended"));
    checkEq(QStringLiteral("I5 and the resumed one is in progress"), sessionFile(crashed).value("status").toString(),
            QStringLiteral("inProgress"));
}

// ---------------------------------------------------------------------------
// G — a tool meets a live session (D11: DEC-158; H-14): SessionStore::fileSaysInProgress, the one check every
// writing tool asks before it writes into a session
// ---------------------------------------------------------------------------
static void testToolGuard()
{
    std::printf("\nG — the tools' in-progress check (D11)\n");
    TempLibrary lib;
    if (!lib.valid()) { skip(QStringLiteral("G"), QStringLiteral("no temp dir")); return; }
    SessionStore store;
    store.setOsTrashForTest(false);   // every trash into the library's own
    const SessionStartResult s = store.start(owner(lib), kType, kPat);
    const SwingAllocation a = store.allocateSwing(s.id);
    check(QStringLiteral("G1 a session being recorded: in progress"), SessionStore::fileSaysInProgress(s.folder));
    check(QStringLiteral("G2 its swing's session folder is the session"),
          QDir::cleanPath(SessionStore::sessionFolderOfSwing(a.swingFolder)) == QDir::cleanPath(s.folder));
    const QByteArray before = bytesOf(SessionFiles::sessionFile(s.folder));
    check(QStringLiteral("G3 the check writes nothing (no lock file, the session file byte-identical)"),
          !TempLibrary::exists(s.folder + QLatin1Char('/') + SessionFiles::lockFileName())
              && bytesOf(SessionFiles::sessionFile(s.folder)) == before);
    const QString refusal = SessionStore::inProgressRefusal(s.folder);
    check(QStringLiteral("G4 the refusal names the session and the override"),
          refusal.contains(QDir::cleanPath(s.folder)) && refusal.contains(QStringLiteral("--allow-in-progress")));
    store.end(s.id);
    check(QStringLiteral("G5 ended: not in progress"), !SessionStore::fileSaysInProgress(s.folder));
    const QString legacy = lib.makeSession(kToken, baseDefault() + QStringLiteral("_09"), 1);
    check(QStringLiteral("G6 a session older than step 2 (no file): not in progress"), !SessionStore::fileSaysInProgress(legacy));
    const QString broken = lib.makeSession(kToken, baseDefault() + QStringLiteral("_10"), 1);
    plant(SessionFiles::sessionFile(broken), "{ \"status\": \"inProgress\"");
    check(QStringLiteral("G7 an unreadable file: not in progress (read as nothing)"), !SessionStore::fileSaysInProgress(broken));
    check(QStringLiteral("G8 no folder: not in progress"), !SessionStore::fileSaysInProgress(QString()));
}

// ---------------------------------------------------------------------------
// J — J-18 (Mark, 15 Nov 2026): a crash-left session ended by the next Start gets its last swing's capture
// end, MARKED AS INFERRED (`endInferred`); an observed End is never marked
// ---------------------------------------------------------------------------
static void testInferredEnd()
{
    std::printf("\nJ — the inferred end of a crash-left session (J-18)\n");
    TempLibrary lib;
    if (!lib.valid()) { skip(QStringLiteral("J"), QStringLiteral("no temp dir")); return; }
    const QDateTime t1 = QDateTime(QDate(2026, 11, 14), QTime(10, 4, 5, 123), QTimeZone::UTC);
    const QDateTime t3 = QDateTime(QDate(2026, 11, 14), QTime(10, 31, 0, 500), QTimeZone::UTC);
    // The swings' capture ends as the swing door reads them: swing 2 records none (a crash mid-write).
    const auto reader = [&](const QString &swingDir) -> QDateTime {
        const QString name = QFileInfo(swingDir).fileName();
        if (name == QLatin1String("swing_0001")) return t1;
        if (name == QLatin1String("swing_0003")) return t3;
        return {};
    };
    QString crashed;
    {
        SessionStore store;   // a run that crashed: started, three swings, never ended
        const SessionStartResult s = store.start(owner(lib), kType, kPat);
        crashed = s.folder;
        for (int i = 0; i < 3; ++i) store.allocateSwing(s.id);
    }
    SessionStore store;
    store.setOsTrashForTest(false);   // every trash into the library's own
    store.setCaptureEndReader(reader);
    const SessionStartResult next = store.start(owner(lib), QStringLiteral("wrist"), kPat);
    const QJsonObject f = sessionFile(crashed);
    checkEq(QStringLiteral("J1 the next Start ends it"), f.value("status").toString(), QStringLiteral("ended"));
    checkEq(QStringLiteral("J2 endedAt = the latest swing capture end (swing 3; swing 2 records none)"),
            f.value("endedAt").toString(), QStringLiteral("2026-11-14T10:31:00.500Z"));
    check(QStringLiteral("J3 marked as inferred"), f.value("endInferred").toBool());
    SessionStore reader2;
    const auto e = reader2.open(reader2.openFolder(crashed));
    check(QStringLiteral("J4 read back: endInferred, the end time"), e && e->endInferred && e->endedAtUtc == t3);
    // An observed End is never marked inferred, even over a file that was.
    store.end(next.id);
    store.resume(store.openFolder(crashed), kType);
    check(QStringLiteral("J5 resumed: in progress, no end, no mark"),
          sessionFile(crashed).value("status").toString() == QLatin1String("inProgress") && !sessionFile(crashed).contains("endedAt")
              && !sessionFile(crashed).contains("endInferred"));
    store.end(store.openFolder(crashed));
    check(QStringLiteral("J6 ended by End: endedAt observed, no endInferred"),
          sessionFile(crashed).contains("endedAt") && !sessionFile(crashed).contains("endInferred"));
    // A crash-left session none of whose swings records a capture time: no end, no mark (as I2, with a reader).
    QString bare;
    {
        SessionStore s0;
        const SessionStartResult s = s0.start(owner(lib), QStringLiteral("grf"), kPat);
        bare = s.folder;
        s0.allocateSwing(s.id);   // a swing folder whose document (if any) records no capture time
    }
    SessionStore store2;
    store2.setCaptureEndReader([](const QString &) { return QDateTime(); });
    store2.start(owner(lib), QStringLiteral("grf"), kPat);
    check(QStringLiteral("J7 no capture time anywhere: ended, end not known, not marked"),
          sessionFile(bare).value("status").toString() == QLatin1String("ended") && !sessionFile(bare).contains("endedAt")
              && !sessionFile(bare).contains("endInferred"));
}

// ---------------------------------------------------------------------------
// O — the identity map and enumerate's typed entries (DEC-136)
// ---------------------------------------------------------------------------
static void testEnumerate()
{
    std::printf("\nO — enumerate\n");
    TempLibrary lib;
    if (!lib.valid()) { skip(QStringLiteral("O"), QStringLiteral("no temp dir")); return; }
    SessionStore store;
    store.setOsTrashForTest(false);   // every trash into the library's own
    const QDateTime t = QDateTime::currentDateTimeUtc();
    store.setClockForTest([t] { return t; });
    const SessionStartResult a = store.start(owner(lib), kType, QStringLiteral("name-date-type"));
    store.end(a.id);
    store.setClockForTest([t] { return t.addSecs(60); });
    const SessionStartResult b = store.start(owner(lib), QStringLiteral("wrist"), QStringLiteral("name-date-type"));
    QDir().mkpath(lib.athleteDir(kToken) + QStringLiteral("/not-a-session"));   // no name, no swing, no file

    const QList<SessionIndexEntry> all = store.enumerate(owner(lib));
    checkEqInt(QStringLiteral("O1 two sessions (a stray folder is not one)"), int(all.size()), 2);
    const auto find = [&all](const SessionId &id) {
        for (const SessionIndexEntry &e : all) if (e.id() == id) return e;
        return SessionIndexEntry();
    };
    const SessionIndexEntry ea = find(a.id), eb = find(b.id);
    check(QStringLiteral("O2 each entry has its status"), ea.status == SessionStatus::Ended && eb.status == SessionStatus::InProgress);
    check(QStringLiteral("O3 and its times"), ea.startedAtUtc.isValid() && ea.endedAtUtc.isValid() && eb.startedAtUtc.isValid()
                                                   && !eb.endedAtUtc.isValid());
    // F-06: under name-date-type no folder starts with its date; the order comes from the start time.
    check(QStringLiteral("O4 n-of-day by start time under name-date-type"), ea.identity.nOfDay() == 1 && eb.identity.nOfDay() == 2);
    checkEq(QStringLiteral("O5 the open record is the same entry"), store.open(b.id)->folder, b.folder);
}

// ---------------------------------------------------------------------------
// E — End: the store ends the session and NEVER discards it (step 3.4a-ii: the empty discard, DEC-93, is
//     Session::end's, which asks Athlete::trashSession: DEC-143; session_test S3)
// ---------------------------------------------------------------------------
static void testEnd()
{
    std::printf("\nE — ending a session ends it and never discards it\n");
    {
        TempLibrary lib;
        if (!lib.valid()) { skip(QStringLiteral("E1"), QStringLiteral("no temp dir")); return; }
        SessionStore store;
        store.setOsTrashForTest(false);
        const SessionStartResult s = store.start(owner(lib), kType, kPat);
        check(QStringLiteral("E1 the store's End of an empty new session keeps it (the discard is Session::end's)"),
              store.end(s.id).ok() && TempLibrary::exists(s.folder));
        checkEq(QStringLiteral("E1b it is ended"), sessionFile(s.folder).value("status").toString(), QStringLiteral("ended"));
        check(QStringLiteral("E2 and still held, with nothing in the library's trash"),
              store.open(s.id).has_value() && !TempLibrary::exists(lib.root() + QStringLiteral("/.pinpoint-trash")));
    }
    {
        TempLibrary lib;
        if (!lib.valid()) { skip(QStringLiteral("E3"), QStringLiteral("no temp dir")); return; }
        SessionStore store;
        store.setOsTrashForTest(false);   // every trash into the library's own
        const SessionStartResult s = store.start(owner(lib), kType, kPat);
        store.allocateSwing(s.id);
        store.end(s.id);
        check(QStringLiteral("E3 a new session with a swing survives"), TempLibrary::exists(s.folder));
        checkEqInt(QStringLiteral("E4 with its swing"), TempLibrary::countSwings(s.folder), 1);
    }
    // ⭐⭐ THE ONE THAT LOSES DATA: this morning's 20 swings, resumed after lunch, nothing hit.
    {
        TempLibrary lib;
        if (!lib.valid()) { skip(QStringLiteral("E5"), QStringLiteral("no temp dir")); return; }
        const QString existing = lib.makeSession(kToken, baseDefault() + QStringLiteral("_01"), 20);
        SessionStore store;
        store.setOsTrashForTest(false);   // every trash into the library's own
        const SessionId id = store.todaysSession(owner(lib), kType);
        checkEq(QStringLiteral("E5 today's session is this morning's folder"), folderOf(store, id), existing);
        store.resume(id, kType);
        store.end(id);
        check(QStringLiteral("E6 a resumed session that captured nothing is NOT discarded"), TempLibrary::exists(existing));
        checkEqInt(QStringLiteral("E7 and this morning's 20 swings survive"), TempLibrary::countSwings(existing), 20);
        checkEq(QStringLiteral("E7b it is ended"), sessionFile(existing).value("status").toString(), QStringLiteral("ended"));
    }
    {
        TempLibrary lib;
        if (!lib.valid()) { skip(QStringLiteral("E8"), QStringLiteral("no temp dir")); return; }
        const QString existing = lib.makeSession(kToken, baseDefault() + QStringLiteral("_01"), 5);
        SessionStore store;
        store.setOsTrashForTest(false);   // every trash into the library's own
        const SessionId id = store.todaysSession(owner(lib), kType);
        store.resume(id, kType);
        const SwingAllocation a = store.allocateSwing(id);
        check(QStringLiteral("E8 the new swing landed in the resumed session"), a.ok() && QFileInfo(a.swingFolder).path() == existing);
        QDir(existing + QStringLiteral("/swing_0001")).removeRecursively();
        store.end(id);
        check(QStringLiteral("E9 a session that captured a swing is never discarded"), TempLibrary::exists(existing));
        check(QStringLiteral("E10 and its swing is still there"), TempLibrary::exists(a.swingFolder));
    }
    {
        TempLibrary lib;
        if (!lib.valid()) { skip(QStringLiteral("E11"), QStringLiteral("no temp dir")); return; }
        SessionStore store;
        store.setOsTrashForTest(false);   // every trash into the library's own
        const SessionStartResult s = store.start(owner(lib), kType, kPat);
        store.end(s.id);
        check(QStringLiteral("E11 End keeps an empty session"), TempLibrary::exists(s.folder));
        check(QStringLiteral("E12 a second End keeps it too"),
              store.end(s.id).ok() && TempLibrary::exists(s.folder));
        check(QStringLiteral("E13 End of an unknown session touches nothing"),
              store.end(SessionId::create()).status == WriteResult::Status::Unchanged);
    }
}

// ---------------------------------------------------------------------------
// K — a session's folder is fixed for its life (E-01); the library re-pointed is D7 (DEC-154)
// ---------------------------------------------------------------------------
static void testFolderFixedAndD7()
{
    std::printf("\nK — the folder is fixed; a re-pointed library ends the session (D7)\n");
    {
        TempLibrary lib;
        if (!lib.valid()) { skip(QStringLiteral("K1"), QStringLiteral("no temp dir")); return; }
        SessionStore store;
        store.setOsTrashForTest(false);   // every trash into the library's own
        const QDateTime t = QDateTime(QDate(2026, 11, 15), QTime(23, 59), QTimeZone::UTC);
        store.setClockForTest([t] { return t; });
        store.setTimeZoneForTest(QTimeZone::UTC);
        const SessionStartResult s = store.start(owner(lib), kType, kPat);
        store.allocateSwing(s.id);
        store.setClockForTest([t] { return t.addSecs(120); });   // past midnight
        const SwingAllocation b = store.allocateSwing(s.id);
        checkEq(QStringLiteral("K1 a swing after midnight is in the session's folder"), QFileInfo(b.swingFolder).path(), s.folder);
        checkEq(QStringLiteral("K2 …and is the next number"), b.swingName, QStringLiteral("swing_0002"));
        // A rename: the session keeps its folder (allocation takes no name); the next Start uses the new name.
        const SwingAllocation c = store.allocateSwing(s.id);
        checkEq(QStringLiteral("K3 the session keeps its folder"), QFileInfo(c.swingFolder).path(), s.folder);
        store.end(s.id);
        const SessionStartResult n = store.start({ kAthlete, QStringLiteral("Mark L"), lib.root() }, kType, kPat);
        check(QStringLiteral("K4 the next session goes under the new name"), QFileInfo(n.folder).path() == lib.athleteDir(QStringLiteral("Mark-L")));
        checkEqInt(QStringLiteral("K5 one session folder under the old name"), TempLibrary::countSessions(lib.athleteDir(kToken)), 1);
    }
    // D7: the library re-pointed mid-session. From step 3.4a-ii the capture service makes it from two
    // commands — the running session's End (Session::end, its empty discard included: session_test S3) and
    // a Start in the new library — so these check the store's half: the old session ends where it is,
    // and a session of the same type starts under the new library.
    {
        TempLibrary oldLib, newLib;
        if (!oldLib.valid() || !newLib.valid()) { skip(QStringLiteral("K6"), QStringLiteral("no temp dir")); return; }
        SessionStore store;
        store.setOsTrashForTest(false);   // every trash into the library's own
        const SessionStartResult s = store.start(owner(oldLib), QStringLiteral("wrist"), kPat);
        store.allocateSwing(s.id);
        check(QStringLiteral("K6a the old one ends"), store.end(s.id).ok());
        const SessionStartResult n = store.start(owner(newLib), store.open(s.id)->identity.type(), kPat);
        check(QStringLiteral("K6 a new session started"), n.ok() && n.id != s.id);
        check(QStringLiteral("K7 under the new library"), QFileInfo(n.folder).path() == newLib.athleteDir(kToken));
        checkEq(QStringLiteral("K8 of the same type"), sessionFile(n.folder).value("type").toString(), QStringLiteral("wrist"));
        checkEq(QStringLiteral("K9 the old one ended"), sessionFile(s.folder).value("status").toString(), QStringLiteral("ended"));
        const SwingAllocation a = store.allocateSwing(n.id);
        check(QStringLiteral("K10 the next swing goes into the new session"), QFileInfo(a.swingFolder).path() == n.folder);
        checkEqInt(QStringLiteral("K11 the old session gained nothing"), TempLibrary::countSwings(s.folder), 1);
        checkEq(QStringLiteral("K12 and its folder never moved"), folderOf(store, s.id), s.folder);
    }
    // D7 with the old session's folder unwritable: its End fails and it stays in progress; the folder is still
    // there (folderPresent, the capture service's test), so it is reported.
    {
        TempLibrary oldLib, newLib;
        if (!oldLib.valid() || !newLib.valid()) { skip(QStringLiteral("K13"), QStringLiteral("no temp dir")); return; }
        SessionStore store;
        store.setOsTrashForTest(false);   // every trash into the library's own
        const SessionStartResult s = store.start(owner(oldLib), kType, kPat);
        store.allocateSwing(s.id);
        if (!makeUnwritable(s.folder)) {
            skip(QStringLiteral("K13 unreachable old library"), QStringLiteral("the platform will not make a directory unwritable here"));
        } else {
            const WriteResult ended = store.end(s.id);
            const bool stillThere = store.folderPresent(s.id);
            const SessionStartResult n = store.start(owner(newLib), kType, kPat);
            makeWritable(s.folder);
            check(QStringLiteral("K13 the new session still starts"), n.ok());
            check(QStringLiteral("K13b the old End fails and its folder is still there, so it is reported (%1)").arg(ended.error),
                  !ended.ok() && stillThere);
            checkEq(QStringLiteral("K14 the old one stays in progress (DEC-73)"), sessionFile(s.folder).value("status").toString(),
                    QStringLiteral("inProgress"));
        }
    }
    // D7 with the old folder present but its session file unreadable as well as the folder unwritable (review 2
    // of 3.4a-ii; DEC-140): the End fails and the failure is still reported — the test is the folder, not the
    // session file (before step 3.4a-ii: QFileInfo::exists in restartInLibrary).
    {
        TempLibrary oldLib;
        if (!oldLib.valid()) { skip(QStringLiteral("K13c"), QStringLiteral("no temp dir")); return; }
        SessionStore store;
        store.setOsTrashForTest(false);
        const SessionStartResult s = store.start(owner(oldLib), kType, kPat);
        store.allocateSwing(s.id);
        const QString file = SessionFiles::sessionFile(s.folder);
        if (!QFile::setPermissions(file, QFileDevice::Permissions()) || QFile(file).open(QIODevice::ReadOnly)
            || !makeUnwritable(s.folder)) {
            QFile::setPermissions(file, QFile::ReadOwner | QFile::WriteOwner);
            skip(QStringLiteral("K13c unreadable session file"), QStringLiteral("the platform will not make a file unreadable here"));
        } else {
            const WriteResult ended = store.end(s.id);
            const bool reported = !ended.ok() && store.folderPresent(s.id);
            const bool fileReads = !store.openFolder(s.folder).isNull();
            makeWritable(s.folder);
            QFile::setPermissions(file, QFile::ReadOwner | QFile::WriteOwner);
            check(QStringLiteral("K13c an unreadable session file in a present folder: the failed End is still reported (%1)")
                      .arg(ended.error),
                  reported);
            check(QStringLiteral("K13d (the case: the session file does not read)"), !fileReads);
        }
    }
    // D7 with the old library gone (moved away): its End fails, the folder is not there — expected, not
    // reported; the new session starts.
    {
        TempLibrary oldLib, newLib;
        if (!oldLib.valid() || !newLib.valid()) { skip(QStringLiteral("K16"), QStringLiteral("no temp dir")); return; }
        SessionStore store;
        store.setOsTrashForTest(false);   // every trash into the library's own
        const SessionStartResult s = store.start(owner(oldLib), kType, kPat);
        store.allocateSwing(s.id);
        const QString away = oldLib.root() + QStringLiteral("/away");
        QDir().rename(oldLib.athleteDir(kToken), away);
        const WriteResult ended = store.end(s.id);
        const SessionStartResult n = store.start(owner(newLib), kType, kPat);
        check(QStringLiteral("K16 a gone library: the new session starts"), n.ok());
        check(QStringLiteral("K17 and the old session's folder is not there, so its end is not reported (%1)").arg(ended.error),
              !ended.ok() && !store.folderPresent(s.id));
    }
    // D7 with the old session's folder locked by another writer: its End is Locked, and the folder reads: reported.
    {
        TempLibrary oldLib, newLib;
        if (!oldLib.valid() || !newLib.valid()) { skip(QStringLiteral("K18"), QStringLiteral("no temp dir")); return; }
        SessionStore store;
        store.setOsTrashForTest(false);   // every trash into the library's own
        store.setLockWaitForTest(200);
        const SessionStartResult s = store.start(owner(oldLib), kType, kPat);
        store.allocateSwing(s.id);
        FolderLock other(s.folder);
        other.lock(0);
        const WriteResult ended = store.end(s.id);
        const SessionStartResult n = store.start(owner(newLib), kType, kPat);
        check(QStringLiteral("K18 a locked old session: the new session starts"), n.ok());
        check(QStringLiteral("K19 and the old session's failed end is reported"),
              ended.status == WriteResult::Status::Locked && store.folderPresent(s.id));
        other.unlock();
    }
    // D7 on a session that captured nothing: the store's End keeps it (Session::end discards it: session_test
    // S3, the capture service's D7 calls it).
    {
        TempLibrary oldLib, newLib;
        if (!oldLib.valid() || !newLib.valid()) { skip(QStringLiteral("K15"), QStringLiteral("no temp dir")); return; }
        SessionStore store;
        store.setOsTrashForTest(false);
        const SessionStartResult s = store.start(owner(oldLib), kType, kPat);
        store.end(s.id);
        check(QStringLiteral("K15 the store's End of an empty session keeps it"), TempLibrary::exists(s.folder));
    }
}

// ---------------------------------------------------------------------------
// C — a copied session: the same id read from another library takes that library's folder
// ---------------------------------------------------------------------------
// The library re-pointed to a copy of itself (the remedy when the share drops) holds the same
// SessionIds: D3's for a legacy folder, the file's for a copied session.json. A store that has read the
// first library must hand out the copy's folder once it reads the copy — never the old library's.
static bool copyTree(const QString &from, const QString &to)
{
    if (!QDir().mkpath(to)) return false;
    for (const QFileInfo &fi : QDir(from).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden)) {
        const QString dst = to + QLatin1Char('/') + fi.fileName();
        if (fi.isDir() ? !copyTree(fi.filePath(), dst) : !QFile::copy(fi.filePath(), dst)) return false;
    }
    return true;
}

static void testCopiedSession()
{
    std::printf("\nC — a copied session is read where it is\n");
    // A legacy folder (D3's id), and a session-file session, each copied to a second library.
    for (const bool withFile : { false, true }) {
        const QString tag = withFile ? QStringLiteral("with a session file") : QStringLiteral("legacy");
        TempLibrary a, b;
        if (!a.valid() || !b.valid()) { skip(QStringLiteral("C ") + tag, QStringLiteral("no temp dir")); return; }
        SessionStore store;
        store.setOsTrashForTest(false);   // every trash into the library's own
        QString inA;
        if (withFile) {
            const SessionStartResult s = store.start(owner(a), kType, kPat);
            store.allocateSwing(s.id);
            store.end(s.id);
            inA = s.folder;
        } else {
            inA = a.makeSession(kToken, baseDefault() + QStringLiteral("_01"), 2);
        }
        const QString inB = b.athleteDir(kToken) + QLatin1Char('/') + QFileInfo(inA).fileName();
        if (!copyTree(inA, inB)) { skip(QStringLiteral("C ") + tag, QStringLiteral("copy failed")); continue; }
        const QByteArray fileInA = bytesOf(SessionFiles::sessionFile(inA));

        const SessionId idA = store.todaysSession(owner(a), kType);   // A is read first (the Wrist screen, the prompt)
        const SessionId idB = store.todaysSession(owner(b), kType);   // then the library is re-pointed to B
        check(QStringLiteral("C1 %1: the copy has the same id").arg(tag), !idA.isNull() && idA == idB);
        checkEq(QStringLiteral("C2 %1: its folder is the copy's, under B").arg(tag), folderOf(store, idB), inB);
        check(QStringLiteral("C3 %1: resume succeeds").arg(tag), store.resume(idB, kType).ok());
        checkEq(QStringLiteral("C4 %1: it is in progress under B").arg(tag), sessionFile(inB).value("status").toString(),
                QStringLiteral("inProgress"));
        check(QStringLiteral("C5 %1: and nothing was written under A").arg(tag),
              bytesOf(SessionFiles::sessionFile(inA)) == fileInA);
        const SwingAllocation next = store.allocateSwing(idB);
        check(QStringLiteral("C6 %1: the next swing goes under B").arg(tag), QFileInfo(next.swingFolder).path() == inB);
        // The discard can never take the copy: it is not the folder this run's start() made.
        store.end(idB);
        check(QStringLiteral("C7 %1: End keeps the copy").arg(tag), TempLibrary::exists(inB));
    }
    // The running session this run started keeps its folder, whatever else holds its id.
    {
        TempLibrary a, b;
        if (!a.valid() || !b.valid()) { skip(QStringLiteral("C8"), QStringLiteral("no temp dir")); return; }
        SessionStore store;
        store.setOsTrashForTest(false);   // every trash into the library's own
        const SessionStartResult s = store.start(owner(a), kType, kPat);
        store.allocateSwing(s.id);
        const QString inB = b.athleteDir(kToken) + QLatin1Char('/') + QFileInfo(s.folder).fileName();
        copyTree(s.folder, inB);
        store.enumerate(owner(b));
        checkEq(QStringLiteral("C8 the running session keeps the folder it was started in"), folderOf(store, s.id), s.folder);
        check(QStringLiteral("C9 its swings still go there"), QFileInfo(store.allocateSwing(s.id).swingFolder).path() == s.folder);
    }
}

// ---------------------------------------------------------------------------
// S — a swing folder to its ShotRef, and a ShotRef to its folder (step 2.4: the recording
//     repository's key; DEC-90's edge translation)
// ---------------------------------------------------------------------------
static void testSwingFolders()
{
    std::printf("\nS — swing folders and ShotRefs\n");
    TempLibrary lib;
    if (!lib.valid()) { skip(QStringLiteral("S"), QStringLiteral("no temp dir")); return; }

    // A session with a file: the ref carries its file's id.
    SessionStore store;
    store.setOsTrashForTest(false);   // every trash into the library's own
    const SessionStartResult s = store.start(owner(lib), kType, kPat);
    const SwingAllocation    a = store.allocateSwing(s.id);
    const ShotRef ref = store.openSwingFolder(a.swingFolder);
    check(QStringLiteral("S1 an allocated swing's folder translates to the allocation's ShotRef"), ref == a.shot);
    checkEq(QStringLiteral("S2 and back"), store.swingFolder(ref), a.swingFolder);
    SessionStore other;   // another process (a tool) reading the same folder
    check(QStringLiteral("S3 a fresh store names it the same, from its session file"),
          other.openSwingFolder(a.swingFolder) == a.shot);

    // A session older than step 2 (no session file): D3's id (DEC-150).
    const QString name   = baseDefault() + QStringLiteral("_07");
    const QString legacy = lib.makeSession(kToken, name, 3);
    SessionStore fresh;
    const ShotRef lref = fresh.openSwingFolder(legacy + QStringLiteral("/swing_0002"), kAthlete);
    checkEq(QStringLiteral("S4 a legacy swing's ShotRef is D3's id"), lref.session().toString(),
            SessionIdentity::legacySessionId(kAthlete, name).toString());
    checkEqInt(QStringLiteral("S5 ... and its swing number"), lref.swingNumber(), 2);
    checkEq(QStringLiteral("S6 it resolves to its folder"), QDir::cleanPath(fresh.swingFolder(lref)),
            QDir::cleanPath(legacy + QStringLiteral("/swing_0002")));
    check(QStringLiteral("S7 and the translation wrote nothing"), !TempLibrary::exists(SessionFiles::sessionFile(legacy)));
    check(QStringLiteral("S8 without an athlete a legacy swing has no ShotRef here"),
          SessionStore().openSwingFolder(legacy + QStringLiteral("/swing_0001")).isNull());
    // A folder the athlete's list already holds keeps the id it was listed with.
    SessionStore listed;
    const QList<SessionIndexEntry> all = listed.enumerate(owner(lib));
    const AthleteId stranger = AthleteId::fromString(u"0c1d2e3f-4a5b-4c6d-8e7f-901a2b3c4d5e");
    check(QStringLiteral("S9 a held session keeps its listed id whatever athlete the caller names"),
          listed.openSwingFolder(legacy + QStringLiteral("/swing_0003"), stranger).session()
              == SessionIdentity::legacySessionId(kAthlete, name));
    check(QStringLiteral("S10 not a swing folder: no ShotRef"),
          store.openSwingFolder(legacy).isNull() && store.openSwingFolder(QString()).isNull());
    check(QStringLiteral("S11 a shot of a session not held: no folder"),
          SessionStore().swingFolder(a.shot).isEmpty() && store.swingFolder(ShotRef()).isEmpty());

    // swingFolder from a worker thread while the owner thread grows the map (the published folders).
    QString fromWorker;
    std::thread worker([&] {
        for (int i = 0; i < 2000; ++i)
            fromWorker = store.swingFolder(a.shot);
    });
    for (int i = 0; i < 20; ++i)
        store.enumerate(owner(lib));
    worker.join();
    checkEq(QStringLiteral("S12 a worker resolves the shot while the map changes"), fromWorker, a.swingFolder);

    // A trashed session's folders are no longer published (the empty discard's path: Session::end ends it,
    // then its Athlete trashes it).
    const SessionStartResult empty = store.start(owner(lib), kType, kPat);
    const QString emptyFolder = store.swingFolder(ShotRef(empty.id, 1));
    check(QStringLiteral("S13 a started session's folder is published"), !emptyFolder.isEmpty());
    store.setOsTrashForTest(false);
    store.end(empty.id);
    store.trash(empty.id);
    check(QStringLiteral("S14 once trashed, no folder"), store.swingFolder(ShotRef(empty.id, 1)).isEmpty());
    check(QStringLiteral("S14b into the library's own trash"), trashedAs(lib, QFileInfo(empty.folder).fileName()));
}

// ---------------------------------------------------------------------------
// D — the advisory lock: a write never lands while another writer holds the folder (R-PERS-6)
// ---------------------------------------------------------------------------
static void testLock()
{
    std::printf("\nD — a held lock refuses the write, and says so\n");
    TempLibrary lib;
    if (!lib.valid()) { skip(QStringLiteral("D"), QStringLiteral("no temp dir")); return; }
    SessionStore store;
    store.setOsTrashForTest(false);   // every trash into the library's own
    store.setLockWaitForTest(200);
    const SessionStartResult s = store.start(owner(lib), kType, kPat);
    FolderLock other(s.folder);
    check(QStringLiteral("D1 another writer holds the session folder"), other.lock(0) == FolderLock::Result::Acquired);
    const WriteResult e = store.end(s.id);
    check(QStringLiteral("D2 End is refused"), !e.ok() && !e.error.isEmpty());
    check(QStringLiteral("D2b ... as Locked (J-22), in the lock's own words"),
          e.status == WriteResult::Status::Locked && e.error.contains(QStringLiteral("being changed by")));
    checkEq(QStringLiteral("D3 the file still says in progress"), sessionFile(s.folder).value("status").toString(), QStringLiteral("inProgress"));
    const SwingAllocation a = store.allocateSwing(s.id);
    check(QStringLiteral("D4 an allocation is refused"), !a.ok());
    check(QStringLiteral("D4b ... as Locked, naming the holder"),
          a.result.status == WriteResult::Status::Locked && a.result.error.contains(QStringLiteral("being changed by")));
    other.unlock();
    check(QStringLiteral("D5 once let go, End writes"), store.end(s.id).ok());
}

int main(int argc, char **argv)
{
    pinpoint::test::IsolatedStores stores;   // #187: every per-user store points at a scratch folder, first
    QCoreApplication app(argc, argv);   // QStandardPaths / QTemporaryDir
    std::printf("session_repository_test — the session file and the one allocator\n");
    testStartWritesTheFile();
    testNamingPatterns();
    testAllocate();
    testNumberingAfterDelete();
    testStartAndResume();
    testLegacy();
    testOneInProgress();
    testInferredEnd();
    testToolGuard();
    testEnumerate();
    testEnd();
    testFolderFixedAndD7();
    testCopiedSession();
    testSwingFolders();
    testLock();
    std::printf("\n%d checks, %d failed, %d skipped\n", g_run, g_fail, g_skip);
    return g_fail == 0 ? 0 : 1;
}
