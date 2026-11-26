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

// THE ONE SESSION LIST FOLLOWS ITS ATHLETES' EVENTS (refactor step 5.7a; information_model.md R-QML-6, OBJ-SESSION-INDEX;
// src/Gui/session/session_list_presenter.h), over the REAL parts the app wires: one session store (its archive and
// restore jobs), one athlete store, two athletes, their Athletes and Sessions and Shots — on a scratch library and a
// scratch settings file. The report list (SessionReportListModel) is its view, as in the app.
//
// ⚠ EVERY EXPECTATION COMES FROM A STATED CONTRACT: R-QML-6 (one list, following EVT-17…20, EVT-23), EVT-18/19/20, the
// move (R-UNASSIGNED-4: sessionMovedOut / sessionMovedIn), EVT-21 (a club declared: its chain reaches the summary).
//
//   L1  ROWS: every session of each followed athlete's index, its kept reads (swings, clubs) read
//   L2  A CLUB CORRECTED (EVT-21 through Session::shotChanged): the row's clubs follow, and the report list's, with no reload
//   L3  EVT-18 / EVT-19: the row reads archived, then restored (EVT-19 now emitted by Session::restoreLanded)
//   L4  EVT-20: a trashed session leaves the list and the report list at once
//   L5  THE MOVE: a session moved to the other athlete leaves the first one's rows and joins the second's
//   L6  EVT-17: a session started joins the rows (no swing yet: the report list does not show it)
//   L7  ASYNCHRONOUS, a slow kept read (a gate): a trash while a reload is in flight — after a club correction (whose
//       refresh reloads), on a first load (no rows yet), and with a second reload asked — never shows the row again, in the
//       presenter's rows (the drawer's and Archiving's source: neither controller is linked here) and the report list (B1)
//   L8  COALESCED: a burst of reloads while one is in flight reads the index and each folder at most twice (B2)
//   L9  THE ENTRY'S SUMMARY (proposal P1, step 5.7a-P1; OBJ-SESSION-INDEX): a bare index leaves it unread; the list's rows
//       have it filled through SessionRepository::deferredSummary (the swing roll-up, the size on disk, the lead work-on);
//       a session with no summary (work_ons.json) yet, or whose swings have no cheap summary, says NA, never 0 or "none";
//       a written summary's work-on 1 shows when its row is read again
//   L3/L10  THE SIZE FOLLOWS: an archive then a restore (EVT-18/19), and a shot trashed (EVT-03), move the row's size with
//       the folder's
//
//   ctest --test-dir build/tests -R '^session_list_test$' --output-on-failure

#include "session/session_list_presenter.h"
#include "report/session_report_list_model.h"
#include "../../DomainModel/athlete.h"
#include "../../DomainModel/session.h"
#include "../../DomainModel/shot.h"
#include "../../Stores/athlete_store.h"
#include "../../Stores/recording_loader.h"
#include "../../Stores/swing_store_repository.h"
#include "../../Stores/session_archiver.h"
#include "../../Stores/session_store.h"
#include "../../Stores/session_index_summary.h"   // the entries' summaries' file work, set on the store as main.cpp sets it
#include "../../Diagnostics/derived/work_ons.h"
#include "../../Stores/tests/swing_doc_test_writes.h"
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
#include <QSettings>
#include <QElapsedTimer>
#include <QHash>
#include <QMutex>
#include <QTemporaryDir>
#include <QThread>
#include <QTimeZone>
#include <QUuid>
#include <QWaitCondition>

#include <cstdio>
#include <memory>
#include "../../Core/tests/isolated_stores.h"   // pinpoint::test::IsolatedStores (#187)

using namespace pinpoint;

static int g_fail = 0;

static void check(const QString &label, bool ok)
{
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", qPrintable(label));
    if (!ok) ++g_fail;
}

static bool put(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}

// A swing as capture writes one (its document through the one door), its club declared through the review.
static void makeSwing(const QString &dir, int index, const AthleteId &athlete, const QDate &day, const QString &club)
{
    QDir().mkpath(dir);
    const QJsonObject manifest{
        { "schema", "pinpoint.swing/2" },
        { "athlete", QJsonObject{ { "name", "Golfer" }, { "uuid", athlete.toString() } } },
        { "clock", QJsonObject{ { "t0_us", 0 }, { "wallclock",
            QDateTime(day, QTime(12, index), QTimeZone::UTC).toString(Qt::ISODateWithMs) } } },
        { "swing", QJsonObject{ { "index", index }, { "id", QStringLiteral("swing_%1").arg(index, 4, 10, QLatin1Char('0')) } } },
        { "streams", QJsonArray{ QJsonObject{ { "kind", "video" }, { "alias", "Face-On" }, { "file", "Face-On.mp4" } } } },
        { "thumbnail", QJsonObject{ { "file", "thumb.jpg" } } } };
    testdoc::writeSwingJson(dir, manifest, nullptr);
    testdoc::updateReview(dir, 3, QString(), club);
    put(dir + "/Face-On.mp4", QByteArray(4096, 'v'));
    put(dir + "/thumb.jpg", QByteArray(512, 't'));
}

// A session folder as SessionStore writes one (FILE-SESS-01), ended, started at noon UTC on `day`.
static QString makeSession(const QString &athleteDir, const AthleteId &athlete, const QDate &day, int swings,
                           const QString &club)
{
    const QString dir = QStringLiteral("%1/%2_Golfer_Wrist_01").arg(athleteDir, day.toString(Qt::ISODate));
    QDir().mkpath(dir);
    const QJsonObject o{ { "schema", "pinpoint.session/1" },
                         { "sessionId", QUuid::createUuid().toString(QUuid::WithoutBraces) },
                         { "athleteId", athlete.toString() }, { "type", "wrist" }, { "status", "ended" },
                         { "startedAt", QDateTime(day, QTime(12, 0), QTimeZone::UTC).toString(Qt::ISODateWithMs) },
                         { "endedAt", QDateTime(day, QTime(13, 0), QTimeZone::UTC).toString(Qt::ISODateWithMs) },
                         { "origin", QJsonObject{ { "kind", "captured" } } } };
    put(dir + "/session.json", QJsonDocument(o).toJson(QJsonDocument::Indented));
    for (int i = 1; i <= swings; ++i)
        makeSwing(dir + QStringLiteral("/swing_%1").arg(i, 4, 10, QLatin1Char('0')), i, athlete, day, club);
    return dir;
}

static std::optional<SessionListRow> rowIn(const SessionListPresenter &p, const AthleteId &a, const QString &folder)
{
    for (const SessionListRow &r : p.rowsOf(a))
        if (QDir::cleanPath(r.entry.folder) == QDir::cleanPath(folder)) return r;
    return std::nullopt;
}

static QStringList clubsOf(const SessionListRow &r)
{
    return r.entry.summary.swings.clubs;
}

static int reportRow(const SessionReportListModel &m, const QString &folder)
{
    for (int i = 0; i < m.count(); ++i)
        if (m.data(m.index(i), SessionReportListModel::DirRole).toString() == QDir::cleanPath(folder)) return i;
    return -1;
}

// A slow kept read: the pool thread waits at the gate until it is opened (no sleep: the test decides when a read lands).
struct Gate {
    QMutex m;
    QWaitCondition c;
    bool open = true;
    QHash<QString, int> reads;   // per folder
    void wait(const QString &folder)
    {
        QMutexLocker l(&m);
        while (!open) c.wait(&m);
        ++reads[QDir::cleanPath(folder)];
    }
    void set(bool o) { QMutexLocker l(&m); open = o; c.wakeAll(); }
    int maxReads() { QMutexLocker l(&m); int n = 0; for (int v : std::as_const(reads)) n = qMax(n, v); return n; }
    void clear() { QMutexLocker l(&m); reads.clear(); }
};

static SessionListPresenter::ReadFn gatedReader(Gate *g)
{
    return [g](const QString &folder) { g->wait(folder); return SessionListReads::read(folder); };
}

static void spin(int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(2);
    }
}

// Until the group is not loading, then long enough for the coalesced row reads (50 ms) to be asked and land.
static bool settle(const SessionListPresenter &p, const AthleteId &a)
{
    QElapsedTimer t;
    t.start();
    for (int round = 0; round < 2; ++round) {
        while (p.loading(a) && t.elapsed() < 20000) spin(10);
        spin(300);
    }
    return !p.loading(a);
}

int main(int argc, char **argv)
{
    pinpoint::test::IsolatedStores stores;   // #187: every per-user store points at a scratch folder, first
    QCoreApplication app(argc, argv);
    if (!stores.contains(ppSettings().fileName())) {
        std::printf("FAIL: the settings file is not the scratch one: %s\n", qPrintable(ppSettings().fileName()));
        return 1;
    }
    QTemporaryDir tmp;
    const QString root = tmp.filePath("library");
    const QString archive = tmp.filePath("archive");
    QDir().mkpath(root);
    QDir().mkpath(archive);

    SessionStore sessions;
    sessions.setOsTrashForTest(false);
    sessions.setArchiveJobs(&SessionArchiveRun::archiveInOwnRun);
    sessions.setRestoreJobs(&SessionArchiveRun::restoreInOwnRun);
    sessions.setSummaryReads(&SessionIndexSummaries::read);   // main.cpp's wiring (5.7a-P1)
    AthleteStore athletes([&root] { return root; }, &sessions);
    athletes.setOsTrashForTest(false);
    // The shots' doors, as main.cpp hands them: a Shot's commands write through the one swing door.
    SwingStoreRecordingRepository recordings(sessions);
    athletes.setShotDoors({ &recordings, std::make_shared<SwingStoreRecordingReader>(sessions) });
    AthleteRecord ra; ra.name = QStringLiteral("Alpha Golfer");
    AthleteRecord rb; rb.name = QStringLiteral("Beta Golfer");
    const AthleteCreateResult ma = athletes.create(ra);
    const AthleteCreateResult mb = athletes.create(rb);
    if (!ma.ok() || !mb.ok()) { std::printf("FAIL: the two athletes were not made\n"); return 1; }
    Athlete *A = ma.athlete;
    Athlete *B = mb.athlete;
    const QDate d1(2026, 11, 2), d2(2026, 11, 9);
    const QString a1 = makeSession(A->folder(), A->id(), d1, 3, QStringLiteral("7 IRON"));
    const QString a2 = makeSession(A->folder(), A->id(), d2, 4, QStringLiteral("7 IRON"));
    const QString b1 = makeSession(B->folder(), B->id(), QDate(2026, 11, 5), 3, QStringLiteral("DRIVER"));   // another day: the move keeps the name
    // L7 and L8's own athlete, so L1–L6 are untouched.
    AthleteRecord rc; rc.name = QStringLiteral("Gamma Golfer");
    const AthleteCreateResult mc = athletes.create(rc);
    if (!mc.ok()) { std::printf("FAIL: the third athlete was not made\n"); return 1; }
    Athlete *C = mc.athlete;
    const QString c1 = makeSession(C->folder(), C->id(), QDate(2026, 10, 1), 3, QStringLiteral("7 IRON"));
    const QString c2 = makeSession(C->folder(), C->id(), QDate(2026, 10, 2), 2, QStringLiteral("7 IRON"));
    const QString c3 = makeSession(C->folder(), C->id(), QDate(2026, 10, 3), 2, QStringLiteral("7 IRON"));
    const QString c4 = makeSession(C->folder(), C->id(), QDate(2026, 10, 4), 2, QStringLiteral("7 IRON"));
    // L9's own athlete: one session whose one swing folder holds nothing yet (no cheap summary, no summary).
    AthleteRecord rd; rd.name = QStringLiteral("Delta Golfer");
    const AthleteCreateResult md = athletes.create(rd);
    if (!md.ok()) { std::printf("FAIL: the fourth athlete was not made\n"); return 1; }
    Athlete *D = md.athlete;
    const QString dn = makeSession(D->folder(), D->id(), QDate(2026, 9, 1), 0, QString());
    QDir().mkpath(dn + QStringLiteral("/swing_0001"));
    Gate gateP, gateQ, gateR;   // before the presenters: each waits for its pool when it goes

    SessionListPresenter list;
    list.setSynchronous(true);
    list.follow(A);
    list.follow(B);
    list.reload(A->id());
    list.reload(B->id());
    SessionReportListModel reports;   // the report list, a view over A's rows
    reports.setSynchronous(true);
    reports.setSessionList(&list);
    reports.setAthlete(A->id());

    std::printf("=== L1 rows ===\n");
    {
        const auto r1 = rowIn(list, A->id(), a1), r2 = rowIn(list, A->id(), a2), r3 = rowIn(list, B->id(), b1);
        check("L1 A's two sessions and B's one", list.rowsOf(A->id()).size() == 2 && list.rowsOf(B->id()).size() == 1
                                                     && r1 && r2 && r3);
        check("L1 the entry's summary: swings and clubs", r1 && r1->entry.summary.read && r1->entry.summary.swings.swings == 3
                                                             && r2->entry.summary.swings.swings == 4
                                                             && clubsOf(*r1) == QStringList{ "7 IRON" });
        check("L1 the report list shows A's two", reports.count() == 2 && reportRow(reports, a1) >= 0 && reportRow(reports, a2) >= 0);
    }

    std::printf("=== L9 the entry's summary (P1) ===\n");
    {
        const QList<SessionIndexEntry> bare = A->index();
        bool unread = !bare.isEmpty();
        for (const SessionIndexEntry &e : bare) unread = unread && !e.summary.read;
        check("L9 a bare index leaves every entry's summary unread (NA)", unread);

        const auto r1 = rowIn(list, A->id(), a1);
        const SessionIndexSummary sm = r1 ? r1->entry.summary : SessionIndexSummary{};
        const qint64 first = QDateTime(d1, QTime(12, 1), QTimeZone::UTC).toMSecsSinceEpoch();
        check("L9 the swing roll-up: three swings, each summarised", sm.read && sm.swings.swings == 3
                                                                      && sm.swings.summarised == 3 && sm.swings.indexed());
        check(QStringLiteral("L9 the first swing's time and the span (%1, %2)").arg(sm.swings.firstMs).arg(sm.swings.spanMs),
              sm.swings.firstMs == first && sm.swings.spanMs == 2 * 60000);
        check("L9 the clubs, the devices, the thumbnails, the mean score",
              sm.swings.clubs == QStringList{ "7 IRON" } && sm.swings.devices == QStringList{ "camera" }
                  && sm.swings.thumbnails.size() == 3 && sm.swings.meanScore.has_value());
        check(QStringLiteral("L9 the size on disk is the folder's (%1)").arg(sm.bytes),
              sm.bytes > 0 && sm.bytes == SessionArchiver::folderBytes(a1));
        check("L9 no summary (work_ons.json) yet: the lead work-on is NA", !sm.workOn.known && sm.workOn.id.isEmpty());
        const int i = reportRow(reports, a1);
        check("L9 the report list says it unknown (its \"-\"), never \"no work-on\"",
              i >= 0 && !reports.data(reports.index(i), SessionReportListModel::WorkOnKnownRole).toBool()
                  && reports.data(reports.index(i), SessionReportListModel::WorkOn1Role).toString().isEmpty());

        // A swing with no cheap summary yet: counted, and every folded fact NA (never a 0 score, never a date).
        list.follow(D);
        list.reload(D->id());
        const auto rn = rowIn(list, D->id(), dn);
        const SessionIndexSummary sn = rn ? rn->entry.summary : SessionIndexSummary{};
        check("L9 an un-indexed swing: counted, nothing folded", sn.read && sn.swings.swings == 1 && sn.swings.summarised == 0
                                                                   && !sn.swings.indexed());
        check("L9 its mean score, first swing time and clubs are NA", !sn.swings.meanScore.has_value() && sn.swings.firstMs == 0
                                                                        && sn.swings.clubs.isEmpty() && !sn.workOn.known);

        // The summary written (as the session reduction writes it): its work-on 1 shows when the row is read again.
        analysis::SessionPriorities rec;
        analysis::PriorityEntry e;
        e.id = QStringLiteral("second_one"); e.name = QStringLiteral("Second"); e.rank = 2;
        rec.entries.push_back(e);
        e.id = QStringLiteral("lead_one"); e.name = QStringLiteral("Lead"); e.rank = 1;
        rec.entries.push_back(e);
        QJsonObject root = analysis::toJson(rec);
        root[QStringLiteral("schemaVersion")] = analysis::kPrioritySchemaVersion;
        root[QStringLiteral("ruleVersion")]   = analysis::kPriorityRuleVersion;
        const WriteResult w = r1 ? sessions.writeWorkOns(r1->entry.id(), root) : WriteResult{};
        if (r1) list.reread(r1->entry.id());
        const auto again = rowIn(list, A->id(), a1);
        check("L9 a written summary: its rank-1 work-on", w.ok() && again && again->entry.summary.workOn.known
                                                            && again->entry.summary.workOn.id == QStringLiteral("lead_one"));
        const int j = reportRow(reports, a1);
        check("L9 and the report list's", j >= 0 && reports.data(reports.index(j), SessionReportListModel::WorkOnKnownRole).toBool()
                                              && reports.data(reports.index(j), SessionReportListModel::WorkOn1Role).toString()
                                                     == QStringLiteral("Lead"));
    }

    std::printf("=== L2 a club corrected ===\n");
    {
        Session *s = A->sessionAt(a1);
        QList<Shot *> shots = s ? s->shots() : QList<Shot *>{};
        const WriteResult w = shots.isEmpty() ? WriteResult{} : shots.first()->setDeclaredClub(QStringLiteral("DRIVER"));
        check("L2 the club is declared through the Shot" + (w.error.isEmpty() ? QString() : ": " + w.error),
              s && !shots.isEmpty() && w.ok());
        const auto r1 = rowIn(list, A->id(), a1);
        check("L2 the row's clubs follow, with no reload", r1 && clubsOf(*r1).contains(QStringLiteral("DRIVER")));
        const int i = reportRow(reports, a1);
        check("L2 the report list's clubs follow",
              i >= 0 && reports.data(reports.index(i), SessionReportListModel::ClubsRole).toString().contains(QStringLiteral("Driver")));
    }

    std::printf("=== L3 archived, then restored ===\n");
    {
        Session *s = A->sessionAt(a2);
        const auto r0 = rowIn(list, A->id(), a2);
        const qint64 full = r0 ? r0->entry.summary.bytes : 0;
        const SessionArchiveResult ar = s ? s->archive(SessionArchiveOptions{ archive, true }) : SessionArchiveResult{};
        const auto r = rowIn(list, A->id(), a2);
        check("L3 EVT-18: the row reads archived", ar.ok && r && r->entry.archived);
        const qint64 stubs = r ? r->entry.summary.bytes : 0;
        check(QStringLiteral("L3 the size follows the archive: the stubs (%1 of %2)").arg(stubs).arg(full),
              full > 0 && stubs < full && stubs == SessionArchiver::folderBytes(a2));
        const int i = reportRow(reports, a2);
        check("L3 the report list reads it archived",
              i >= 0 && reports.data(reports.index(i), SessionReportListModel::ArchivedRole).toBool());
        bool restored = false;
        if (s) QObject::connect(s, &Session::sessionRestored, &app, [&restored] { restored = true; });
        const SessionArchiveJob job = s ? s->restoreJob() : SessionArchiveJob{};
        const SessionArchiveResult rr = job ? job.run({}) : SessionArchiveResult{};
        if (s) s->restoreLanded(rr);
        const auto back = rowIn(list, A->id(), a2);
        check("L3 EVT-19 is emitted on a restore", rr.ok && restored);
        check("L3 the row reads restored", back && !back->entry.archived);
        const qint64 put = back ? back->entry.summary.bytes : 0;
        check(QStringLiteral("L3 the size follows the restore (%1)").arg(put),
              put > stubs && put == SessionArchiver::folderBytes(a2));
    }

    std::printf("=== L10 a shot trashed: the size follows ===\n");
    {
        Session *s = A->sessionAt(a2);
        const auto r0 = rowIn(list, A->id(), a2);
        const qint64 before = r0 ? r0->entry.summary.bytes : 0;
        QList<Shot *> shots = s ? s->shots() : QList<Shot *>{};
        const WriteResult t = shots.isEmpty() ? WriteResult{} : s->trashShot(shots.last()->ref());
        const auto r = rowIn(list, A->id(), a2);
        check("L10 EVT-03: trashed through the Session" + (t.error.isEmpty() ? QString() : ": " + t.error), t.ok());
        check(QStringLiteral("L10 the row's swings and size follow (%1 -> %2)").arg(before).arg(r ? r->entry.summary.bytes : -1),
              r && r->entry.summary.swings.swings == 3 && r->entry.summary.bytes < before
                  && r->entry.summary.bytes == SessionArchiver::folderBytes(a2));
    }

    std::printf("=== L4 trashed ===\n");
    {
        Session *s = A->sessionAt(a2);
        const WriteResult t = s ? A->trashSession(s->id()) : WriteResult{};
        check("L4 trashed through the Athlete", t.ok());
        check("L4 EVT-20: the row goes at once", !rowIn(list, A->id(), a2) && list.rowsOf(A->id()).size() == 1);
        check("L4 the report list loses it at once", reports.count() == 1 && reportRow(reports, a2) < 0);
    }

    std::printf("=== L5 moved to the other athlete ===\n");
    {
        Session *s = A->sessionAt(a1);
        QString moved;
        const WriteResult m = s ? A->moveSessionTo(s->id(), *B, &moved) : WriteResult{};
        check("L5 moved through the Athlete" + (m.error.isEmpty() ? QString() : ": " + m.error), m.ok() && !moved.isEmpty());
        check("L5 it leaves A's rows", list.rowsOf(A->id()).isEmpty() && reports.count() == 0);
        const auto r = rowIn(list, B->id(), moved);
        check("L5 it joins B's, its summary read", list.rowsOf(B->id()).size() == 2 && r && r->entry.summary.read
                                                       && r->entry.summary.swings.swings == 3);
    }

    std::printf("=== L6 a session started ===\n");
    {
        WriteResult w;
        Session *s = A->startSession(QStringLiteral("wrist"), QStringLiteral("date-type-name"), &w);
        check("L6 started through the Athlete", s && w.ok());
        const auto r = s ? list.row(s->id()) : std::nullopt;
        check("L6 EVT-17: it joins A's rows", r && r->owner == A->id() && r->entry.status == SessionStatus::InProgress);
        check("L6 no swing yet: the report list does not show it", reports.count() == 0);
    }

    std::printf("=== L7 a trash while a reload is in flight ===\n");
    {
        SessionListPresenter P;   // as the app runs it: its kept reads on its pool
        P.setReader(gatedReader(&gateP));
        P.follow(C);
        P.reload(C->id());
        settle(P, C->id());
        SessionReportListModel cReports;
        cReports.setSynchronous(true);
        cReports.setSessionList(&P);
        cReports.setAthlete(C->id());
        spin(100);
        check("L7 C's four sessions listed", P.rowsOf(C->id()).size() == 4 && cReports.count() == 4);

        // Every change the presenter announces after a trash: the trashed row must not be in it.
        SessionId gone;
        bool shown = false;
        const auto watch = [&] { if (!gone.isNull() && P.row(gone)) shown = true; };
        QObject::connect(&P, &SessionListPresenter::rowsChanged, &app, watch);
        QObject::connect(&P, &SessionListPresenter::rowChanged, &app, watch);

        // (a) A club corrected (its refresh reloads, main.cpp's itemsChanged → refresh), then a trash at once.
        Session *s1 = C->sessionAt(c1);
        QList<Shot *> shots = s1 ? s1->shots() : QList<Shot *>{};
        gateP.set(false);
        const WriteResult w = shots.isEmpty() ? WriteResult{} : shots.first()->setDeclaredClub(QStringLiteral("DRIVER"));
        P.reload(C->id());
        const SessionId id1 = s1 ? s1->id() : SessionId{};
        gone = id1;
        const WriteResult t1 = s1 ? C->trashSession(id1) : WriteResult{};
        gateP.set(true);
        settle(P, C->id());
        spin(100);
        check("L7a corrected and trashed" + (t1.error.isEmpty() ? QString() : ": " + t1.error), w.ok() && t1.ok());
        check("L7a the row never shows again", !shown);
        check("L7a it stays gone from every group", !P.row(id1) && !P.rowAt(c1) && P.rowsOf(C->id()).size() == 3);
        check("L7a and from the report list", cReports.count() == 3 && reportRow(cReports, c1) < 0);

        // (b) A second reload asked while one runs (a bulk job's refresh), then a trash.
        Session *s3 = C->sessionAt(c3);
        const SessionId id3 = s3 ? s3->id() : SessionId{};
        gateP.set(false);
        P.reload(C->id());
        P.reload(C->id());
        gone = id3;
        const WriteResult t3 = s3 ? C->trashSession(id3) : WriteResult{};
        gateP.set(true);
        settle(P, C->id());
        spin(100);
        check("L7b with a second reload owed: trashed", t3.ok());
        check("L7b the row never shows again", !shown);
        check("L7b it stays gone, and the report list", !P.row(id3) && P.rowsOf(C->id()).size() == 2
                                                         && cReports.count() == 2 && reportRow(cReports, c3) < 0);

        // (c) A first load: the group has no rows yet when the trash comes.
        SessionListPresenter Q;
        Q.setReader(gatedReader(&gateQ));
        gateQ.set(false);
        Q.follow(C);
        Q.reload(C->id());
        Session *s2 = C->sessionAt(c2);
        const SessionId id2 = s2 ? s2->id() : SessionId{};
        bool shownQ = false;
        QObject::connect(&Q, &SessionListPresenter::rowsChanged, &app, [&] { if (Q.row(id2)) shownQ = true; });
        const bool empty = Q.rowsOf(C->id()).isEmpty() && Q.loading(C->id());
        const WriteResult t2 = s2 ? C->trashSession(id2) : WriteResult{};
        gateQ.set(true);
        settle(Q, C->id());
        check("L7c a first load in flight, its rows empty, trashed", empty && t2.ok());
        check("L7c the row never shows", !shownQ && !Q.row(id2) && Q.rowsOf(C->id()).size() == 1 && Q.rowAt(c4));
    }

    std::printf("=== L8 reloads coalesced ===\n");
    {
        SessionListPresenter R;
        R.setReader(gatedReader(&gateR));
        int indexed = 0;
        R.setSource(C->id(), [&indexed, C] { ++indexed; return C->index(); });
        int landed = 0;
        QObject::connect(&R, &SessionListPresenter::rowsChanged, &app, [&landed] { ++landed; });
        gateR.set(false);
        constexpr int kBurst = 25;
        for (int i = 0; i < kBurst; ++i) {
            R.reload(C->id());
            QCoreApplication::processEvents();
        }
        gateR.set(true);
        settle(R, C->id());
        check(QStringLiteral("L8 %1 reloads: the index read %2 times (at most 2)").arg(kBurst).arg(indexed), indexed >= 1 && indexed <= 2);
        check(QStringLiteral("L8 each folder read at most twice (%1)").arg(gateR.maxReads()), gateR.maxReads() >= 1 && gateR.maxReads() <= 2);
        check(QStringLiteral("L8 the rows land at most twice (%1)").arg(landed), landed >= 1 && landed <= 2);
        check("L8 the rows are C's", R.rowsOf(C->id()).size() == 1 && R.rowsOf(C->id()).first().facts.read);
        check("L8 a source with no summary read leaves the summary unread (NA)", !R.rowsOf(C->id()).first().entry.summary.read);
    }

    std::printf("%s (%d failed)\n", g_fail ? "FAIL" : "PASS", g_fail);
    return g_fail ? 1 : 0;
}
