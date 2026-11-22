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

// HOUSEKEEPING (refactor step 3.8; build/refactor/step-3/BRIEF.md §4.11, §6 row 3.8; src/App/housekeeping.h): the one
// application service that ends things for good, over the REAL parts the app wires (main.cpp): ONE session store with its
// worker-safe archive job (SessionArchiveRun::archiveInOwnRun), ONE athlete store and the athlete callables over it, the
// purge and the size (SwingPaths), and the Archiving tab's walk (SessionArchiveRun::scanLibrary, archiveInOwnRun) — on a
// temporary library and a scratch settings file, never the share, the live INI or the OS trash.
//
// ⚠ EVERY EXPECTATION COMES FROM A STATED CONTRACT, NEVER FROM RUNNING THE CODE: information_model.md §5.4 (housekeeping),
// LC-SESSION (`archive`, InProgress → Ended → Archived), EVT-18, FILE-LIB-01, SET-10…SET-14; DEC-80 (one service, through
// the roots' commands), DEC-137 (one purge; SET-14 off drops raw frames), DEC-167 (the composition root's callables),
// DEC-175 (the OS trash is not the app's), DEC-188 (the library's size from housekeeping); the Archiving tab's rules as
// archive_controller.cpp kept them before the move (today's sessions and the live one never touched; the oldest first
// below the floor).
//
//   H1  ONE PURGE: an entry trashed before the cutoff goes, a newer one stays; no cutoff empties the trash; the retention
//       pass goes through the same purge door, once; the pass purges nothing with retention off
//   H2  BY AGE, THROUGH THE APP'S Session, FROM A WORKER: the pass on another thread; an ended session older than SET-11 is
//       archived (its marker, its mirror) and EVT-18 is heard on the app's one Session (held before the pass), on its own
//       thread; no second Athlete is made (every load answers the one instance); a session the pass loaded itself is let
//       go again; a newer one, today's and an archived one are left alone
//   H3  REFUSES AN InProgress SESSION: Session::archive refuses it (no marker, no event) and the pass leaves it alone
//   H4  BY SPACE: below SET-12's floor the OLDEST sessions go first, until there is room; the live session is skipped
//   H5  SET-14 off: the archive leaves the raw frames out (a permanent delete of raw frames, DEC-137)
//   H6  the library's size: every file, hidden ones included; -1 when aborted part-way
//   H7  nothing switched on: the pass does nothing (no purge, no athlete listed)
//   H9  CANCEL WHILE A GUI STEP RUNS (review 2, A2): the started step is waited for, its session finished and landed, no
//       Session is left "in use", and nothing after it is archived
//   H8  A FOLDER NO ATHLETE HOLDS (an athlete this machine does not list): archived by today's path, in the one
//       oldest-first order with the athletes' sessions. The no-golfer `unknown` folder is no longer one: since step 5.1
//       its sessions are the Unassigned owner's (§5.11; J-66: held where they are), archived through their Sessions
//
//   cmake --build build/tests --target housekeeping_test -j 6
//   ctest --test-dir build/tests -R '^housekeeping_test$' --output-on-failure

#include "../housekeeping.h"
#include "../../DomainModel/athlete.h"
#include "../../DomainModel/session.h"
#include "../../Export/athlete_store.h"
#include "../../Export/session_archiver.h"
#include "../../Export/session_files.h"
#include "../../Export/session_store.h"
#include "../../Export/swing_paths.h"
#include "../../Export/tests/swing_doc_test_writes.h"   // a swing's document, through the one door
#include "../../Core/pp_settings.h"

#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QThread>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QUuid>

#include <atomic>
#include <cstdio>
#include <chrono>
#include <memory>
#include <thread>

using namespace pinpoint;

static int g_fail = 0;
static int g_run  = 0;

static void check(const QString &label, bool ok)
{
    ++g_run;
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", qPrintable(label));
    if (!ok) ++g_fail;
}

static bool put(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}

// A swing as capture writes one (its document through the one door), with its media and, optionally, raw frames.
static void makeSwing(const QString &dir, int index, const AthleteId &athlete, bool withRaw)
{
    QDir().mkpath(dir);
    QJsonArray tUs;
    for (int i = 0; i < 10; ++i) tUs.append(qint64(i) * 6667);
    const QJsonObject manifest{
        { "schema", "pinpoint.swing/2" },
        { "athlete", QJsonObject{ { "name", "Test Golfer" }, { "uuid", athlete.toString() } } },
        { "clock", QJsonObject{ { "t0_us", 0 }, { "wallclock", "2026-01-01T10:00:00.000Z" } } },
        { "swing", QJsonObject{ { "index", index }, { "id", QStringLiteral("swing_%1").arg(index, 4, 10, QLatin1Char('0')) } } },
        { "streams", QJsonArray{ QJsonObject{ { "kind", "video" }, { "alias", "Face-On" },
                                              { "file", "Face-On.mp4" }, { "frames", QJsonObject{ { "t_us", tUs } } } } } },
        { "thumbnail", QJsonObject{ { "file", "thumb.jpg" } } } };
    testdoc::writeSwingJson(dir, manifest, nullptr);
    put(dir + "/Face-On.mp4", QByteArray(4096, 'v'));
    put(dir + "/thumb.jpg", QByteArray(512, 't'));
    if (withRaw) put(dir + "/Face-On.raw", QByteArray(8192, 'r'));
}

// A session folder as SessionStore writes one (FILE-SESS-01), started at noon UTC on `day`, with `swings` swings.
static QString makeSession(const QString &athleteDir, const AthleteId &athlete, const QDate &day, int n, bool inProgress,
                           int swings, bool withRaw = false)
{
    const QString dir = QStringLiteral("%1/%2_Test-Golfer_Wrist_%3").arg(athleteDir, day.toString(Qt::ISODate))
                            .arg(n, 2, 10, QLatin1Char('0'));
    QDir().mkpath(dir);
    QJsonObject o{ { "schema", "pinpoint.session/1" },
                   { "sessionId", QUuid::createUuid().toString(QUuid::WithoutBraces) },
                   { "athleteId", athlete.toString() }, { "type", "wrist" },
                   { "status", inProgress ? "inProgress" : "ended" },
                   { "startedAt", QDateTime(day, QTime(12, 0), QTimeZone::UTC).toString(Qt::ISODateWithMs) },
                   { "origin", QJsonObject{ { "kind", "captured" } } } };
    if (!inProgress) o.insert("endedAt", QDateTime(day, QTime(13, 0), QTimeZone::UTC).toString(Qt::ISODateWithMs));
    put(dir + "/session.json", QJsonDocument(o).toJson(QJsonDocument::Indented));
    for (int i = 1; i <= swings; ++i)
        makeSwing(dir + QStringLiteral("/swing_%1").arg(i, 4, 10, QLatin1Char('0')), i, athlete, withRaw);
    return dir;
}

static bool archivedMark(const QString &sessionDir) { return QFileInfo::exists(SessionFiles::archiveMarker(sessionDir)); }

// The app's composition root in small: one session store (its archive job wired, its trash the library's own), one
// athlete store over it, the athlete callables over that (every Athlete handed out recorded), the purge and the size
// over SwingPaths, the Archiving tab's walk, and an owner object on this (the "GUI") thread.
struct AppRig {
    SessionStore                 sessions;
    std::unique_ptr<AthleteStore> athletes;
    QObject                      owner;
    QSet<Athlete *>              handedOut;
    int                          purges = 0, lists = 0;
    QString                      root;

    explicit AppRig(const QString &libraryRoot) : root(libraryRoot)
    {
        sessions.setOsTrashForTest(false);
        sessions.setArchiveJobs(&SessionArchiveRun::archiveInOwnRun);
        athletes = std::make_unique<AthleteStore>([this] { return root; }, &sessions);
    }
    HousekeepingDoors doors()
    {
        HousekeepingDoors d;
        d.athletes.listAthletes = [this] { ++lists; return athletes->list(); };
        d.athletes.loadAthlete  = [this](const AthleteId &id) {
            Athlete *a = athletes->load(id);
            if (a) handedOut.insert(a);
            return a;
        };
        d.purgeTrash = [this](const QString &r, const QDateTime &cutoff) {
            ++purges;
            const SwingPaths::Purged p = SwingPaths::purgeTrash(r, cutoff);
            return TrashPurge{ p.removed, p.failed };
        };
        d.libraryBytes = [](const QString &r, const std::atomic_bool *abort) { return SwingPaths::libraryBytes(r, abort); };
        return d;
    }
    static LibraryWalk walk()
    {
        LibraryWalk w;
        w.scan = [](const QString &r) {
            QList<LibraryWalk::Folder> out;
            for (const auto &s : SessionArchiveRun::scanLibrary(r, false)) out.append({ s.dir, s.dateMs, s.archived });
            return out;
        };
        w.archiveUnheld = [](const QString &folder, const SessionArchiveOptions &o) {
            return SessionArchiveRun::archiveInOwnRun(folder, o);
        };
        return w;
    }
};

// The pass on a worker thread while this thread runs its event loop (as the Archiving tab's job runs it), then the posted
// landings delivered.
static Housekeeping::Report runOnWorker(const Housekeeping &hk, const Housekeeping::Plan &plan, AppRig &app,
                                        const std::atomic_bool *abort = nullptr)
{
    Housekeeping::Report r;
    std::atomic_bool done{ false };
    std::thread worker([&] { r = hk.run(plan, AppRig::walk(), &app.owner, abort); done = true; });
    while (!done) {
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    worker.join();
    for (int i = 0; i < 20; ++i) QCoreApplication::processEvents();
    return r;
}

// One scratch library with one athlete (athlete.json, made by the real store), and a scratch archive.
struct Library {
    QTemporaryDir tmp;
    QString       root, archive, athleteDir;
    AthleteId     athlete;

    Library()
    {
        root    = tmp.filePath("library");
        archive = tmp.filePath("archive");
        QDir().mkpath(root);
        QDir().mkpath(archive);
        SessionStore sessions;
        AthleteStore athletes([this] { return root; }, &sessions);
        AthleteRecord rec;
        rec.name = QStringLiteral("Test Golfer");
        const AthleteCreateResult made = athletes.create(rec);
        if (made.ok()) {
            athlete    = made.athlete->id();
            athleteDir = made.athlete->folder();
        }
    }
    bool valid() const { return tmp.isValid() && !athlete.isNull() && QFileInfo(athleteDir).isDir(); }
};

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    // ppSettings() (the run's athlete store reads this machine's settings athletes) points at a scratch folder first.
    QTemporaryDir cfg;
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, cfg.path());
    if (!QFileInfo(ppSettings().fileName()).absoluteFilePath().startsWith(QFileInfo(cfg.path()).absoluteFilePath())) {
        std::printf("FAIL: the settings file is not the scratch one: %s\n", qPrintable(ppSettings().fileName()));
        return 1;
    }
    const QDate today = QDate::currentDate();

    std::printf("=== H1 one purge ===\n");
    {
        Library lib;
        check("H1 the scratch library and its athlete", lib.valid());
        const QString trash = lib.root + "/.pinpoint-trash/Test-Golfer";
        const QString oldEntry = trash + "/" + QDateTime::currentDateTime().addDays(-30).toString("yyyyMMdd-HHmmss")
                                 + "_2026-01-01_Test-Golfer_Wrist_01";
        const QString newEntry = trash + "/" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss")
                                 + "_2026-01-02_Test-Golfer_Wrist_01_swing_0001";
        put(oldEntry + "/session.json", "{}");
        put(newEntry + "/swing.ppsw", "x");
        AppRig app(lib.root);
        const Housekeeping hk(app.doors());
        const TrashPurge p = hk.purge(lib.root, QDateTime::currentDateTime().addDays(-7));
        check("H1 an entry trashed before the cutoff is deleted for good", p.removed == 1 && !QFileInfo::exists(oldEntry));
        check("H1 one trashed after it stays", QFileInfo::exists(newEntry) && p.failed == 0);
        check("H1 the purge went through the one purge door", app.purges == 1);
        put(oldEntry + "/session.json", "{}");
        Housekeeping::Plan plan;
        plan.libraryRoot = lib.root;
        plan.trashRetentionDays = 7;
        const Housekeeping::Report r = hk.run(plan, AppRig::walk(), &app.owner);
        check("H1 the retention pass purges through the same door, once", app.purges == 2 && r.trashRemoved == 1
                  && !QFileInfo::exists(oldEntry) && QFileInfo::exists(newEntry));
        check("H1 …and lists no athlete when no archiving is switched on", app.lists == 0);
        const TrashPurge all = hk.purge(lib.root);
        check("H1 no cutoff: the whole trash is emptied (\"Empty now\")", all.removed == 1 && SwingPaths::trashEntries(lib.root).isEmpty());
    }

    std::printf("\n=== H2 by age, through the app's Session, from a worker; H3 an InProgress session refused ===\n");
    {
        Library lib;
        check("H2 the scratch library and its athlete", lib.valid());
        const QString old1    = makeSession(lib.athleteDir, lib.athlete, today.addDays(-60), 1, false, 2);
        const QString old2    = makeSession(lib.athleteDir, lib.athlete, today.addDays(-45), 1, false, 1);
        const QString recent  = makeSession(lib.athleteDir, lib.athlete, today.addDays(-10), 1, false, 1);
        const QString todays  = makeSession(lib.athleteDir, lib.athlete, today, 1, false, 1);
        const QString crashed = makeSession(lib.athleteDir, lib.athlete, today.addDays(-50), 1, /*inProgress=*/true, 1);
        AppRig app(lib.root);
        const Housekeeping hk(app.doors());

        // The app's Athlete, and old1's Session held before the pass (a view has it open).
        Athlete *athlete = app.athletes->load(lib.athlete);
        Session *open1   = athlete ? athlete->sessionAt(old1) : nullptr;
        check("H2 the app's Athlete holds old1's Session before the pass", open1 != nullptr);
        QStringList heard;
        if (open1)
            QObject::connect(open1, &Session::sessionArchived, open1, [&heard](const SessionId &id) {
                heard << id.toString() + (QThread::currentThread() == QCoreApplication::instance()->thread() ? "@owner" : "@worker");
            });

        // H3 first, directly: the Session's own command refuses a session in progress (no job, no write, no event).
        {
            Session *s = athlete ? athlete->sessionAt(crashed) : nullptr;
            check("H3 the app's Athlete holds the in-progress session", s && s->inProgress());
            bool fired = false;
            if (s) QObject::connect(s, &Session::sessionArchived, s, [&fired] { fired = true; });
            const SessionArchiveJob job = s ? s->archiveJob(SessionArchiveOptions{ lib.archive, true }) : SessionArchiveJob{};
            const SessionArchiveResult r = s ? s->archive(SessionArchiveOptions{ lib.archive, true }) : SessionArchiveResult{};
            check("H3 Session::archiveJob refuses a session in progress, saying why", !job && !job.refused.error.isEmpty());
            check("H3 Session::archive refuses it: no marker, no mirror", !r.ok && !archivedMark(crashed)
                      && !QFileInfo::exists(lib.archive + "/Test-Golfer/" + QFileInfo(crashed).fileName()));
            check("H3 …and emits no EVT-18", !fired);
        }

        Housekeeping::Plan plan;
        plan.libraryRoot      = lib.root;
        plan.archiveAfterDays = 30;
        plan.archiveLocation  = lib.archive;
        const Housekeeping::Report r = runOnWorker(hk, plan, app);
        check("H2 the ended sessions older than SET-11 are archived (their markers)", archivedMark(old1) && archivedMark(old2));
        check("H2 …mirrored into <archive>/<athlete>/<session>",
              QFileInfo::exists(lib.archive + "/Test-Golfer/" + QFileInfo(old1).fileName() + "/swing_0001/Face-On.mp4"));
        check("H2 …and thinned: its media are no longer in the library", !QFileInfo::exists(old1 + "/swing_0001/Face-On.mp4"));
        check("H2 EVT-18 is heard on the app's one Session, on its own thread",
              open1 && heard == QStringList{ open1->id().toString() + "@owner" });
        check("H2 …and its record reads archived", open1 && open1->entry() && open1->entry()->archived);
        check("H2 no second Athlete: every load answered the app's one instance",
              app.handedOut.size() == 1 && app.handedOut.contains(athlete));
        check("H2 the Session held before the pass is still the Athlete's", athlete && athlete->loadedSession(open1->id()) == open1);
        bool old2Held = false;
        if (athlete)
            for (Session *s : athlete->loadedSessions())
                if (s->entry() && QDir::cleanPath(s->entry()->folder) == QDir::cleanPath(old2)) old2Held = true;
        check("H2 a Session the pass loaded itself is let go again", !old2Held);
        check("H2 counted: 2 archived through their Sessions, none by N1's path", r.archived == 2 && r.archivedUnheld == 0);
        check("H2 a session newer than SET-11 is left alone", !archivedMark(recent));
        check("H2 today's session is never touched", !archivedMark(todays));
        check("H3 the pass leaves the in-progress session alone", !archivedMark(crashed) && r.inProgress == 1);
        check("H2 nothing failed, nothing stopped", r.archiveFailed == 0 && !r.stopped);
        const Housekeeping::Report again = runOnWorker(hk, plan, app);
        check("H2 a second pass archives nothing more", again.archived == 0 && heard.size() == 1 && again.archiveFailed == 0);
    }

    std::printf("\n=== H4 by space: the oldest first, until there is room; the live session skipped ===\n");
    {
        Library lib;
        check("H4 the scratch library and its athlete", lib.valid());
        const QString oldest = makeSession(lib.athleteDir, lib.athlete, today.addDays(-40), 1, false, 1);
        const QString older  = makeSession(lib.athleteDir, lib.athlete, today.addDays(-30), 1, false, 1);
        const QString old    = makeSession(lib.athleteDir, lib.athlete, today.addDays(-20), 1, false, 1);
        const QString recent = makeSession(lib.athleteDir, lib.athlete, today.addDays(-2), 1, false, 1);
        AppRig app(lib.root);
        const Housekeeping hk(app.doors());
        Housekeeping::Plan plan;
        plan.libraryRoot     = lib.root;
        plan.archiveFloorGb  = 50;
        plan.archiveLocation = lib.archive;
        plan.liveFolder      = oldest;   // the running session's folder: never archived
        // The volume has room again once two sessions have gone.
        plan.freeBytes = [&]() -> qint64 { return (archivedMark(older) && archivedMark(old)) ? qint64(100) << 30 : 0; };
        const Housekeeping::Report r = hk.run(plan, AppRig::walk(), &app.owner);
        check("H4 the live session is skipped", !archivedMark(oldest));
        check("H4 the oldest others go first", archivedMark(older) && archivedMark(old));
        check("H4 …until there is room: the newer one stays", !archivedMark(recent) && r.archived == 2);
    }

    std::printf("\n=== H5 SET-14 off: the raw frames left out ===\n");
    {
        Library lib;
        const QString s = makeSession(lib.athleteDir, lib.athlete, today.addDays(-60), 1, false, 1, /*withRaw=*/true);
        AppRig app(lib.root);
        const Housekeeping hk(app.doors());
        Housekeeping::Plan plan;
        plan.libraryRoot      = lib.root;
        plan.archiveAfterDays = 30;
        plan.archiveLocation  = lib.archive;
        plan.keepRaw          = false;
        hk.run(plan, AppRig::walk(), &app.owner);
        const QString mirror = lib.archive + "/Test-Golfer/" + QFileInfo(s).fileName() + "/swing_0001";
        check("H5 archived", archivedMark(s) && QFileInfo::exists(mirror + "/Face-On.mp4"));
        check("H5 the raw frames are neither in the archive nor in the library",
              !QFileInfo::exists(mirror + "/Face-On.raw") && !QFileInfo::exists(s + "/swing_0001/Face-On.raw"));
    }

    std::printf("\n=== H6 the library's size (DEC-188) ===\n");
    {
        Library lib;
        AppRig app(lib.root);
        const Housekeeping hk(app.doors());
        const qint64 base = hk.libraryBytes(lib.root);   // the athlete's own file
        check("H6 the athlete's file counts", base >= QFileInfo(lib.athleteDir + "/athlete.json").size() && base > 0);
        put(lib.root + "/a/one.bin", QByteArray(1000, 'a'));
        put(lib.root + "/a/b/two.bin", QByteArray(2000, 'b'));
        put(lib.root + "/.hidden/three.bin", QByteArray(3000, 'c'));
        check("H6 every file, in sub-folders and hidden ones included", hk.libraryBytes(lib.root) == base + 6000);
        check("H6 the size door handed out answers the same", hk.librarySize()(lib.root, nullptr) == base + 6000);
        for (int i = 0; i < 300; ++i) put(lib.root + QStringLiteral("/many/f%1").arg(i), "x");
        std::atomic_bool stop{ true };
        check("H6 aborted part-way: -1, never a partial sum", hk.libraryBytes(lib.root, &stop) == -1);
    }

    std::printf("\n=== H7 nothing switched on ===\n");
    {
        Library lib;
        const QString s = makeSession(lib.athleteDir, lib.athlete, today.addDays(-60), 1, false, 1);
        AppRig app(lib.root);
        const Housekeeping hk(app.doors());
        Housekeeping::Plan plan;
        plan.libraryRoot     = lib.root;
        plan.archiveLocation = lib.archive;   // a location alone switches nothing on
        const Housekeeping::Report r = hk.run(plan, AppRig::walk(), &app.owner);
        check("H7 no purge, no athlete listed, nothing archived", app.purges == 0 && app.lists == 0 && !archivedMark(s)
                  && r.archived == 0 && !plan.anythingOn());
    }

    std::printf("\n=== H8 a folder no Athlete holds: today's path, in the one oldest-first order ===\n");
    {
        Library lib;
        check("H8 the scratch library and its athlete", lib.valid());
        // A folder no athlete of the library lists (no athlete.json; not the no-golfer `unknown` folder, which is the
        // Unassigned owner's since step 5.1: H8u).
        const QString unknownDir = lib.root + "/stranger";
        const QString lone   = makeSession(unknownDir, AthleteId(), today.addDays(-35), 1, false, 1);
        const QString mine40 = makeSession(lib.athleteDir, lib.athlete, today.addDays(-40), 1, false, 1);
        const QString mine30 = makeSession(lib.athleteDir, lib.athlete, today.addDays(-30), 1, false, 1);
        AppRig app(lib.root);
        const Housekeeping hk(app.doors());
        // By age: both kinds archived, each by its own path.
        {
            Housekeeping::Plan plan;
            plan.libraryRoot      = lib.root;
            plan.archiveAfterDays = 20;
            plan.archiveLocation  = lib.archive;
            const Housekeeping::Report r = hk.run(plan, AppRig::walk(), &app.owner);
            check("H8 the unheld session is archived (its marker, its mirror under stranger/)", archivedMark(lone)
                      && QFileInfo::exists(lib.archive + "/stranger/" + QFileInfo(lone).fileName() + "/swing_0001/Face-On.mp4"));
            check("H8 …by today's path, counted as such; the athlete's two through their Sessions",
                  r.archived == 3 && r.archivedUnheld == 1 && archivedMark(mine40) && archivedMark(mine30));
        }
    }
    {
        // H8u (step 5.1): a no-golfer session an older build left in `unknown/` is the Unassigned owner's, held where it
        // is (J-66), so it is archived through its Session — not by today's path.
        Library lib;
        const QString lone = makeSession(lib.root + "/unknown", AthleteId(), today.addDays(-35), 1, false, 1);
        AppRig app(lib.root);
        const Housekeeping hk(app.doors());
        Housekeeping::Plan plan;
        plan.libraryRoot      = lib.root;
        plan.archiveAfterDays = 20;
        plan.archiveLocation  = lib.archive;
        const Housekeeping::Report r = hk.run(plan, AppRig::walk(), &app.owner);
        check("H8u a no-golfer session in unknown/: archived through the Unassigned owner's Session, none by today's path",
              archivedMark(lone) && r.archived == 1 && r.archivedUnheld == 0);
    }
    {
        // Below the floor, one session's room: the oldest of the WHOLE library goes first, whoever holds it.
        Library lib;
        const QString unknownDir = lib.root + "/stranger";
        const QString lone   = makeSession(unknownDir, AthleteId(), today.addDays(-50), 1, false, 1);
        const QString mine40 = makeSession(lib.athleteDir, lib.athlete, today.addDays(-40), 1, false, 1);
        AppRig app(lib.root);
        const Housekeeping hk(app.doors());
        Housekeeping::Plan plan;
        plan.libraryRoot     = lib.root;
        plan.archiveFloorGb  = 50;
        plan.archiveLocation = lib.archive;
        plan.freeBytes = [&]() -> qint64 { return archivedMark(lone) ? qint64(100) << 30 : 0; };
        const Housekeeping::Report r = hk.run(plan, AppRig::walk(), &app.owner);
        check("H8 below the floor the unheld session, the oldest, goes first; then there is room",
              archivedMark(lone) && !archivedMark(mine40) && r.archived == 1 && r.archivedUnheld == 1);
    }

    std::printf("\n=== H9 cancel while a GUI step runs: nothing left in use ===\n");
    {
        Library lib;
        const QString a1 = makeSession(lib.athleteDir, lib.athlete, today.addDays(-60), 1, false, 1);
        const QString a2 = makeSession(lib.athleteDir, lib.athlete, today.addDays(-50), 1, false, 1);
        AppRig app(lib.root);
        Athlete *athlete = app.athletes->load(lib.athlete);
        Session *s1 = athlete ? athlete->sessionAt(a1) : nullptr;   // held by a view before the pass
        Session *s2 = athlete ? athlete->sessionAt(a2) : nullptr;
        std::atomic_bool abort{ false };
        HousekeepingDoors doors = app.doors();
        // Cancel is pressed while the GUI thread is inside the pass's first step (listing and loading the athletes), and
        // the step goes on long enough for the worker to see it.
        const auto load = doors.athletes.loadAthlete;
        doors.athletes.loadAthlete = [load, &abort](const AthleteId &id) {
            abort = true;
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            return load(id);
        };
        const Housekeeping hk(doors);
        Housekeeping::Plan plan;
        plan.libraryRoot      = lib.root;
        plan.archiveAfterDays = 30;
        plan.archiveLocation  = lib.archive;
        const Housekeeping::Report r = runOnWorker(hk, plan, app, &abort);
        check("H9 the pass stopped after the cancel", r.stopped);
        check("H9 the started step was finished: its session archived and landed, the other not",
              r.archived == 1 && archivedMark(a2) && !archivedMark(a1));
        check("H9 no Session is left in use", s1 && s2 && s1->uses() == 0 && s2->uses() == 0);
    }

    std::printf("\n%s: %d of %d checks passed\n", g_fail ? "FAIL" : "PASS", g_run - g_fail, g_run);
    return g_fail ? 1 : 0;
}
