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

// The carousel's bookkeeping: which number a shot is called, what survives a
// reload, and what an edit writes through.
//
// ⚠ THE ORACLES ARE AGREEMENT AND ROUND-TRIP, NOT RECORDED OUTPUT.  Two things
// in this application number a swing — ShotListModel::addShot() counts rows,
// SwingPaths::allocateSwingDir() counts folders — and swing.json carries the
// second while the carousel shows the first.  Nothing has ever checked that
// they agree.  Likewise every user edit is written to disk and read back on the
// next session, so what comes back must be what went in.  Neither kind of test
// can be written by running the code and recording the answer: agreement is a
// property of two producers, and a round-trip is a property of a pair.
//
// shot_list_model_test covers shotSummary(); this covers the ledger underneath it.

#include "shot/shot_list_model.h"
#include "swing_doc.h"
#include "swing_paths.h"
#include "temp_library.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QSignalSpy>
#include <QUrl>

#include <cstdio>

using namespace pinpoint;
using namespace pinpoint::test;

static int g_fail = 0;
static int g_run  = 0;

static void check(const QString &label, bool ok)
{
    ++g_run;
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", qPrintable(label));
    if (!ok) ++g_fail;
}

static void checkInt(const QString &label, long long got, long long want)
{
    check(QStringLiteral("%1 (got %2, want %3)").arg(label).arg(got).arg(want), got == want);
}

static void checkStr(const QString &label, const QString &got, const QString &want)
{
    check(QStringLiteral("%1 (got \"%2\", want \"%3\")").arg(label, got, want), got == want);
}

// ---------------------------------------------------------------------------
// Fixtures
// ---------------------------------------------------------------------------

// A swing.json as the exporter writes one: the raw manifest through the real
// writer, so what the reader gets back has been through the real round-trip.
static bool writeSwingDoc(const QString &swingDir, int index, const QString &club,
                          bool withVideo, const QDateTime &wallclock)
{
    if (!QDir().mkpath(swingDir))
        return false;

    QJsonObject root;
    root[QStringLiteral("swing")] = QJsonObject{
        { QStringLiteral("index"), index },
        { QStringLiteral("id"),    QStringLiteral("swing_%1").arg(index, 4, 10, QLatin1Char('0')) },
    };
    root[QStringLiteral("athlete")] = QJsonObject{
        { QStringLiteral("name"), QStringLiteral("Mark Liversedge") },
        { QStringLiteral("uuid"), QStringLiteral("uuid-0001") },
    };
    root[QStringLiteral("clock")] = QJsonObject{
        { QStringLiteral("t0_us"),     0 },
        { QStringLiteral("unit"),      QStringLiteral("us") },
        { QStringLiteral("wallclock"), wallclock.toString(Qt::ISODateWithMs) },
    };
    QJsonArray streams;
    if (withVideo)
        streams.append(QJsonObject{ { QStringLiteral("kind"), QStringLiteral("video") },
                                    { QStringLiteral("file"), QStringLiteral("dtl.mp4") } });
    root[QStringLiteral("streams")] = streams;

    QString err;
    const bool ok = SwingDocWriter::writeSwingJson(swingDir, root, nullptr, &err, club);
    if (!ok)
        std::printf("        fixture write failed: %s\n", qPrintable(err));
    return ok;
}

static int diskOrdinal(const QString &swingDir)
{
    return SwingDocReader::readSwingJson(swingDir).ordinal;
}

// The suite's standard athlete/session identity, matching swing_paths_test.
static const QString kName = QStringLiteral("Mark Liversedge");
static const QString kUuid = QStringLiteral("uuid-0001");
static const QString kType = QStringLiteral("Swing");
static const QString kPat  = QStringLiteral("date-name-type");

// ---------------------------------------------------------------------------
// O — the number the carousel shows
// ---------------------------------------------------------------------------
//
// shot_list_model.h: "A notification saying 'Shot 7' must mean the row the user
// can see."  For that to hold, an ordinal has to identify one shot within the
// session — if two shots are ever both called Shot 3, the sentence names
// nothing.  And the number the carousel shows must be the number the document
// records, or the same swing is Shot 6 today and Shot 3 after a restart.

static void testOrdinals()
{
    std::printf("\nO — the ordinal identifies one shot\n");

    {
        ShotListModel m;
        const int a = m.addShot(QStringLiteral("/s/swing_0001"), QStringLiteral("13:00"),
                                QStringLiteral("7-iron"), true, QUrl(), {}, 70, {});
        const int b = m.addShot(QStringLiteral("/s/swing_0002"), QStringLiteral("13:01"),
                                QStringLiteral("7-iron"), true, QUrl(), {}, 71, {});
        const int c = m.addShot(QStringLiteral("/s/swing_0003"), QStringLiteral("13:02"),
                                QStringLiteral("7-iron"), true, QUrl(), {}, 72, {});
        checkInt(QStringLiteral("O1 first shot is Shot 1"),  m.ordinalForId(a), 1);
        checkInt(QStringLiteral("O2 second shot is Shot 2"), m.ordinalForId(b), 2);
        checkInt(QStringLiteral("O3 third shot is Shot 3"),  m.ordinalForId(c), 3);
        checkInt(QStringLiteral("O4 an unknown id has no ordinal"), m.ordinalForId(9999), 0);
    }

    // ⭐ An ordinal already used this session must never be handed out again.
    // The golfer trashes a bad shot and hits another; both are announced as
    // "Shot 3", and the second one's toast points at a row that is gone.
    {
        ShotListModel m;
        QSet<int> issued;
        for (int i = 1; i <= 3; ++i) {
            const int id = m.addShot(QStringLiteral("/s/swing_%1").arg(i),
                                     QStringLiteral("13:0%1").arg(i),
                                     QStringLiteral("7-iron"), true, QUrl(), {}, 70, {});
            issued.insert(m.ordinalForId(id));
        }
        // Trash an analysis-only row, so no filesystem is involved and the
        // question stays about the ordinal.
        const int mem = m.addShot(QString(), QStringLiteral("13:04"),
                                  QStringLiteral("7-iron"), false, QUrl(), {}, 70, {});
        issued.insert(m.ordinalForId(mem));
        check(QStringLiteral("O5 an in-memory shot takes the next ordinal (4)"),
              m.ordinalForId(mem) == 4);
        m.moveToTrash(mem);                       // no folder — drops from the model

        const int next = m.addShot(QString(), QStringLiteral("13:05"),
                                   QStringLiteral("7-iron"), false, QUrl(), {}, 70, {});
        const int nextOrdinal = m.ordinalForId(next);
        check(QStringLiteral("O6 an ordinal is never reissued after a trash (got %1, already issued %2)")
                  .arg(nextOrdinal).arg(issued.contains(nextOrdinal) ? "yes" : "no"),
              !issued.contains(nextOrdinal));
    }
}

// ---------------------------------------------------------------------------
// X — the carousel and the document must agree about the number
// ---------------------------------------------------------------------------

static void testOrdinalAgreement()
{
    std::printf("\nX — the carousel number and swing.index are one number\n");

    TempLibrary lib;
    if (!lib.valid()) { check(QStringLiteral("X skipped — no temp dir"), false); return; }

    // A session from this morning: three swings, of which the middle one was
    // later deleted from the carousel. That leaves swing_0001 and swing_0003 —
    // an ordinary state, reachable by trashing one shot.
    SwingPaths paths;
    const QString session = paths.beginSession(lib.root(), kName, kUuid, kPat, kType, false);
    check(QStringLiteral("X0 fixture session created"), !session.isEmpty());

    const QDateTime base = QDateTime::currentDateTime().addSecs(-3600);
    writeSwingDoc(session + QStringLiteral("/swing_0001"), 1, QStringLiteral("7-iron"), true,
                  base);
    writeSwingDoc(session + QStringLiteral("/swing_0005"), 5, QStringLiteral("7-iron"), true,
                  base.addSecs(240));

    // The carousel reloads that session ...
    ShotListModel m;
    m.loadSessionDir(session);
    checkInt(QStringLiteral("X1 both surviving swings reload"), m.rowCount(), 2);

    // ... and the golfer hits another. The document gets its number from
    // SwingPaths; the row gets its number from the model.
    const SwingPaths::Allocation alloc = paths.allocateSwingDir(lib.root(), kName, kUuid, kPat, kType);
    check(QStringLiteral("X2 the new swing got a folder"), !alloc.swingDir.isEmpty());
    writeSwingDoc(alloc.swingDir, alloc.swingIndex, QStringLiteral("7-iron"), true,
                  QDateTime::currentDateTime());
    const int id = m.addShot(alloc.swingDir, QStringLiteral("14:00"), QStringLiteral("7-iron"),
                             true, QUrl(), {}, 75, {});

    checkInt(QStringLiteral("X3 the row and the document agree on the number"),
             m.ordinalForId(id), diskOrdinal(alloc.swingDir));

    // And the same swing must keep that number across a restart — this is the
    // sharper form of the same claim: the number is a property of the shot, not
    // of when you happen to look at it.
    ShotListModel reloaded;
    reloaded.loadSessionDir(session);
    int reloadedOrdinal = 0;
    for (int row = 0; row < reloaded.rowCount(); ++row) {
        const QModelIndex ix = reloaded.index(row);
        if (reloaded.data(ix, ShotListModel::SwingDirRole).toString() == alloc.swingDir)
            reloadedOrdinal = reloaded.data(ix, ShotListModel::OrdinalRole).toInt();
    }
    checkInt(QStringLiteral("X4 the shot keeps its number across a restart"),
             reloadedOrdinal, m.ordinalForId(id));
}

// ---------------------------------------------------------------------------
// L — loadSessionDir: newest first, and everything that was written
// ---------------------------------------------------------------------------

static void testLoadSessionDir()
{
    std::printf("\nL — reloading a session from disk\n");

    TempLibrary lib;
    if (!lib.valid()) { check(QStringLiteral("L skipped — no temp dir"), false); return; }
    const QString session = lib.makeSession(QStringLiteral("Mark-Liversedge"),
                                            QStringLiteral("2026-09-07_session_01"));

    const QDateTime base = QDateTime::currentDateTime().addSecs(-3600);
    const int kCount = 12;   // past 9, so a lexical sort would misorder
    for (int i = 1; i <= kCount; ++i)
        writeSwingDoc(session + QStringLiteral("/swing_%1").arg(i, 4, 10, QLatin1Char('0')),
                      i, QStringLiteral("7-iron"), i % 2 == 0, base.addSecs(i * 60));

    ShotListModel m;
    m.loadSessionDir(session);
    checkInt(QStringLiteral("L1 every swing reloads"), m.rowCount(), kCount);
    checkInt(QStringLiteral("L2 activeCount agrees with the row count"), m.activeCount(), kCount);

    // The header's stated invariant: "Prepend (newest first); callers reload
    // ascending so the highest ordinal lands first."
    bool descending = true;
    int  previous   = INT_MAX;
    for (int row = 0; row < m.rowCount(); ++row) {
        const int o = m.data(m.index(row), ShotListModel::OrdinalRole).toInt();
        if (o >= previous) descending = false;
        previous = o;
    }
    check(QStringLiteral("L3 rows come back newest-first"), descending);
    checkInt(QStringLiteral("L4 row 0 is the newest swing"),
             m.data(m.index(0), ShotListModel::OrdinalRole).toInt(), kCount);

    // hasVideo has to survive: it is what decides whether the row offers a replay.
    checkInt(QStringLiteral("L5 the video flag reloads (newest was even -> video)"),
             m.data(m.index(0), ShotListModel::HasVideoRole).toBool() ? 1 : 0, 1);

    // A second load must replace, not append.
    m.loadSessionDir(session);
    checkInt(QStringLiteral("L6 reloading replaces rather than appends"), m.rowCount(), kCount);

    // An empty dir clears (the documented behaviour).
    m.loadSessionDir(QString());
    checkInt(QStringLiteral("L7 an empty session dir clears the carousel"), m.rowCount(), 0);

    // A folder with no readable documents in it is an empty session, not a crash.
    ShotListModel m2;
    m2.loadSessionDir(lib.root());
    checkInt(QStringLiteral("L8 a folder with no swings loads as empty"), m2.rowCount(), 0);

    // An unreadable document is skipped, and does not take the session with it.
    const QString broken = session + QStringLiteral("/swing_0099");
    QDir().mkpath(broken);
    QFile bad(broken + QStringLiteral("/swing.json"));
    if (bad.open(QIODevice::WriteOnly)) { bad.write("{ this is not json"); bad.close(); }
    ShotListModel m3;
    m3.loadSessionDir(session);
    checkInt(QStringLiteral("L9 a corrupt document is skipped, the rest still load"),
             m3.rowCount(), kCount);
}

// ---------------------------------------------------------------------------
// W — the user's edits are written through and come back
// ---------------------------------------------------------------------------

static void testReviewWriteThrough()
{
    std::printf("\nW — rating, note and club survive a restart\n");

    TempLibrary lib;
    if (!lib.valid()) { check(QStringLiteral("W skipped — no temp dir"), false); return; }
    const QString session = lib.makeSession(QStringLiteral("Mark-Liversedge"),
                                            QStringLiteral("2026-09-07_session_01"));
    const QString swing = session + QStringLiteral("/swing_0001");
    writeSwingDoc(swing, 1, QStringLiteral("7-iron"), true, QDateTime::currentDateTime());

    ShotListModel m;
    m.loadSessionDir(session);
    checkInt(QStringLiteral("W0 the fixture swing loaded"), m.rowCount(), 1);
    const int id = m.data(m.index(0), ShotListModel::ShotIdRole).toInt();

    const QString note = QStringLiteral("Left wrist cupped at the top — “fix this”\nline two");
    m.setRating(id, 4);
    m.setNote(id, note);
    m.setClub(id, QStringLiteral("Driver"));

    // Round-trip through a fresh model, exactly as the next session would.
    ShotListModel reloaded;
    reloaded.loadSessionDir(session);
    checkInt(QStringLiteral("W1 the rating comes back"),
             reloaded.data(reloaded.index(0), ShotListModel::RatingRole).toInt(), 4);
    checkStr(QStringLiteral("W2 the note comes back verbatim"),
             reloaded.data(reloaded.index(0), ShotListModel::NoteRole).toString(), note);
    checkStr(QStringLiteral("W3 the club comes back"),
             reloaded.data(reloaded.index(0), ShotListModel::ClubRole).toString(),
             QStringLiteral("Driver"));

    // Ratings are 0..5; the model clamps rather than storing nonsense.
    m.setRating(id, 99);
    checkInt(QStringLiteral("W4 an over-range rating clamps to 5"),
             m.data(m.index(0), ShotListModel::RatingRole).toInt(), 5);
    m.setRating(id, -3);
    checkInt(QStringLiteral("W5 an under-range rating clamps to 0"),
             m.data(m.index(0), ShotListModel::RatingRole).toInt(), 0);

    // ⭐ The club is a vocabulary, not free text: shot_list_model.h says
    // setClub takes a club "from clubOptions", the picker only offers those,
    // and everything downstream — the session ledger's per-club grouping, the
    // club-length prior keyed by club — assumes the value is one of them. A
    // value from outside the bag is not a club the rest of the app can read.
    {
        const QString before = m.data(m.index(0), ShotListModel::ClubRole).toString();
        m.setClub(id, QStringLiteral("Excalibur"));
        const QString after = m.data(m.index(0), ShotListModel::ClubRole).toString();
        check(QStringLiteral("W6 a club outside the bag is refused (was \"%1\", now \"%2\")")
                  .arg(before, after),
              ShotListModel::clubOptions().contains(after));
    }

    // A shot that never reached disk has nowhere to write through to, and the
    // contract is that this is harmless rather than an error.
    {
        ShotListModel mem;
        const int memId = mem.addShot(QString(), QStringLiteral("13:00"),
                                      QStringLiteral("7-iron"), false, QUrl(), {}, 0, {});
        mem.setRating(memId, 3);
        mem.setNote(memId, QStringLiteral("in memory only"));
        checkInt(QStringLiteral("W7 an in-memory shot still holds its rating"),
                 mem.data(mem.index(0), ShotListModel::RatingRole).toInt(), 3);
    }
}

// ---------------------------------------------------------------------------
// P — previousAnalysisDetail: the "compare to previous" ghost
// ---------------------------------------------------------------------------

static void testPreviousAnalysisDetail()
{
    std::printf("\nP — the previous swing\n");

    ShotListModel m;
    auto detail = [](int marker) {
        return QVariantMap{ { QStringLiteral("overall"), marker } };
    };
    m.addShot(QStringLiteral("/s/swing_0001"), QStringLiteral("13:00"), QStringLiteral("7-iron"),
              true, QUrl(), {}, 70, {}, detail(1));
    m.addShot(QStringLiteral("/s/swing_0002"), QStringLiteral("13:01"), QStringLiteral("7-iron"),
              true, QUrl(), {}, 71, {}, detail(2));
    m.addShot(QStringLiteral("/s/swing_0003"), QStringLiteral("13:02"), QStringLiteral("7-iron"),
              true, QUrl(), {}, 72, {}, detail(3));

    checkInt(QStringLiteral("P1 the previous of the newest is the middle one"),
             m.previousAnalysisDetail(QStringLiteral("/s/swing_0003"))
                 .value(QStringLiteral("overall")).toInt(), 2);
    checkInt(QStringLiteral("P2 the previous of the middle is the oldest"),
             m.previousAnalysisDetail(QStringLiteral("/s/swing_0002"))
                 .value(QStringLiteral("overall")).toInt(), 1);
    check(QStringLiteral("P3 the oldest has no previous"),
          m.previousAnalysisDetail(QStringLiteral("/s/swing_0001")).isEmpty());
    check(QStringLiteral("P4 a swing not in this model has no previous"),
          m.previousAnalysisDetail(QStringLiteral("/s/nowhere")).isEmpty());
    check(QStringLiteral("P5 an empty dir has no previous"),
          m.previousAnalysisDetail(QString()).isEmpty());
}

// ---------------------------------------------------------------------------
// A — attachSwingDir: the launch monitor arriving late
// ---------------------------------------------------------------------------
//
// shot_list_model.h: "two rows for one swing is worse than none".

static void testAttachSwingDir()
{
    std::printf("\nA — a late launch monitor reading finds its row\n");

    TempLibrary lib;
    if (!lib.valid()) { check(QStringLiteral("A skipped — no temp dir"), false); return; }
    const QString session = lib.makeSession(QStringLiteral("Mark-Liversedge"),
                                            QStringLiteral("2026-09-07_session_01"));
    const QString swing = session + QStringLiteral("/swing_0001");
    writeSwingDoc(swing, 1, QStringLiteral("Driver"), false, QDateTime::currentDateTime());

    ShotListModel m;
    const int id = m.addShot(QString(), QStringLiteral("13:00"), QStringLiteral("7-iron"),
                             false, QUrl(), {}, 0, {});
    checkInt(QStringLiteral("A1 the shot has a row before the reading arrives"), m.rowCount(), 1);

    m.attachSwingDir(id, swing);
    checkInt(QStringLiteral("A2 attaching adds no second row"), m.rowCount(), 1);
    checkStr(QStringLiteral("A3 the row now points at the folder"),
             m.data(m.index(0), ShotListModel::SwingDirRole).toString(), swing);
    checkStr(QStringLiteral("A4 and picked up the document's club"),
             m.data(m.index(0), ShotListModel::ClubRole).toString(), QStringLiteral("Driver"));

    // A row that already belongs to a different folder must not be repointed.
    const QString other = session + QStringLiteral("/swing_0002");
    writeSwingDoc(other, 2, QStringLiteral("PW"), false, QDateTime::currentDateTime());
    m.attachSwingDir(id, other);
    checkStr(QStringLiteral("A5 a row is never repointed at another swing"),
             m.data(m.index(0), ShotListModel::SwingDirRole).toString(), swing);

    m.attachSwingDir(9999, other);
    checkInt(QStringLiteral("A6 attaching to an unknown id changes nothing"), m.rowCount(), 1);
}

// ---------------------------------------------------------------------------
// C — the count the carousel shows
// ---------------------------------------------------------------------------

static void testActiveCountSignal()
{
    std::printf("\nC — activeCount announces every change\n");

    ShotListModel m;
    QSignalSpy spy(&m, &ShotListModel::activeCountChanged);

    const int a = m.addShot(QString(), QStringLiteral("13:00"), QStringLiteral("7-iron"),
                            false, QUrl(), {}, 0, {});
    checkInt(QStringLiteral("C1 adding a shot announces the count"), spy.count(), 1);
    checkInt(QStringLiteral("C2 the count is 1"), m.activeCount(), 1);

    m.moveToTrash(a);
    checkInt(QStringLiteral("C3 trashing announces the count"), spy.count(), 2);
    checkInt(QStringLiteral("C4 the count is back to 0"), m.activeCount(), 0);

    m.addShot(QString(), QStringLiteral("13:01"), QStringLiteral("7-iron"),
              false, QUrl(), {}, 0, {});
    m.clear();
    checkInt(QStringLiteral("C5 clearing announces the count"), spy.count(), 4);
    checkInt(QStringLiteral("C6 the count is 0"), m.activeCount(), 0);

    // Clearing an already-empty model announces nothing — the count did not change.
    const int before = spy.count();
    m.clear();
    checkInt(QStringLiteral("C7 clearing an empty model announces nothing"),
             spy.count(), before);
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    std::printf("shot_ledger_test — carousel ordinals, reload and write-through\n");
    testOrdinals();
    testOrdinalAgreement();
    testLoadSessionDir();
    testReviewWriteThrough();
    testPreviousAnalysisDetail();
    testAttachSwingDir();
    testActiveCountSignal();
    std::printf("\n%d checks, %d failed\n", g_run, g_fail);
    return g_fail == 0 ? 0 : 1;
}
