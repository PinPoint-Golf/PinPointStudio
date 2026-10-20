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
#include "swing_store.h"
#include "Analysis/swing_analysis.h"
#include "launch_monitor_reading.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QSignalSpy>
#include <QTimer>
#include <QUrl>

#include <cmath>
#include <cstdio>
#include <utility>

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
    m.setClub(id, QStringLiteral("DRIVER"));   // as the picker offers it

    // Round-trip through a fresh model, exactly as the next session would.
    ShotListModel reloaded;
    reloaded.loadSessionDir(session);
    checkInt(QStringLiteral("W1 the rating comes back"),
             reloaded.data(reloaded.index(0), ShotListModel::RatingRole).toInt(), 4);
    checkStr(QStringLiteral("W2 the note comes back verbatim"),
             reloaded.data(reloaded.index(0), ShotListModel::NoteRole).toString(), note);
    checkStr(QStringLiteral("W3 the club comes back"),
             reloaded.data(reloaded.index(0), ShotListModel::ClubRole).toString(),
             QStringLiteral("DRIVER"));

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
// M — the launch monitor board's annotations are written through and come back
// ---------------------------------------------------------------------------
//
// "Leave out" and "Club" on the FSX panel's pinned card (docs/design/lm_panel_fsx.md) are
// persisted in the shot's review block as review.excluded / review.clubOverride. The contract
// is that both survive a reload of the session — through the cheap summary path the live
// carousel uses AND the full parse a reviewed session uses — that a star rating does not wipe
// them, and that clearing them leaves the document saying nothing, as if never touched.

static void testLmAnnotationsWriteThrough()
{
    std::printf("\nM — launch monitor annotations survive a reload\n");

    TempLibrary lib;
    if (!lib.valid()) { check(QStringLiteral("M skipped — no temp dir"), false); return; }
    const QString session = lib.makeSession(QStringLiteral("Mark-Liversedge"),
                                            QStringLiteral("2026-10-13_session_01"));
    const QString swing = session + QStringLiteral("/swing_0001");
    writeSwingDoc(swing, 1, QStringLiteral("DRIVER"), true, QDateTime::currentDateTime());

    ShotListModel m;
    m.loadSessionDir(session);
    checkInt(QStringLiteral("M0 the fixture swing loaded"), m.rowCount(), 1);
    const int id = m.data(m.index(0), ShotListModel::ShotIdRole).toInt();
    check(QStringLiteral("M1 a document without the keys reads as not left out"),
          !m.data(m.index(0), ShotListModel::LmExcludedRole).toBool());
    checkStr(QStringLiteral("M2 …and as no club override"),
             m.data(m.index(0), ShotListModel::LmClubOverrideRole).toString(), QString());

    QSignalSpy changed(&m, &QAbstractItemModel::dataChanged);
    m.setLmExcluded(id, true);
    m.setLmClubOverride(id, QStringLiteral("7 IRON"));
    checkInt(QStringLiteral("M3 each edit announces itself (the board rebuilds on it)"), changed.count(), 2);

    // The cheap path: loadSessionDir reads the summary, never the document.
    {
        ShotListModel reloaded;
        reloaded.loadSessionDir(session);
        check(QStringLiteral("M4 left out comes back through the summary path"),
              reloaded.data(reloaded.index(0), ShotListModel::LmExcludedRole).toBool());
        checkStr(QStringLiteral("M5 the club override comes back through the summary path"),
                 reloaded.data(reloaded.index(0), ShotListModel::LmClubOverrideRole).toString(),
                 QStringLiteral("7 IRON"));
        checkStr(QStringLiteral("M6 the RECORDED club is untouched by the override"),
                 reloaded.data(reloaded.index(0), ShotListModel::ClubRole).toString(),
                 QStringLiteral("DRIVER"));
    }
    // The full parse: a reviewed session (SessionReviewController) reads this.
    {
        const pinpoint::PersistedShot ps = pinpoint::SwingDocReader::readSwingJson(swing);
        check(QStringLiteral("M7 the full parse reads excluded"), ps.ok && ps.excluded);
        checkStr(QStringLiteral("M8 the full parse reads clubOverride"), ps.clubOverride,
                 QStringLiteral("7 IRON"));
    }

    // A star rating rewrites the review block — and must merge, not replace.
    m.setRating(id, 4);
    {
        ShotListModel reloaded;
        reloaded.loadSessionDir(session);
        checkInt(QStringLiteral("M9 the rating landed"),
                 reloaded.data(reloaded.index(0), ShotListModel::RatingRole).toInt(), 4);
        check(QStringLiteral("M10 a rating does not un-leave-out the shot"),
              reloaded.data(reloaded.index(0), ShotListModel::LmExcludedRole).toBool());
        checkStr(QStringLiteral("M11 a rating does not drop the club override"),
                 reloaded.data(reloaded.index(0), ShotListModel::LmClubOverrideRole).toString(),
                 QStringLiteral("7 IRON"));
    }

    // Choosing the recorded club is how the golfer declines the evidence — accepted whatever
    // its case, and stored in the recorded spelling.
    m.setLmClubOverride(id, QStringLiteral("driver"));
    checkStr(QStringLiteral("M12 the recorded club, any case, is stored as recorded"),
             m.data(m.index(0), ShotListModel::LmClubOverrideRole).toString(), QStringLiteral("DRIVER"));
    m.setLmClubOverride(id, QStringLiteral("Excalibur"));
    checkStr(QStringLiteral("M13 a club outside the bag is refused"),
             m.data(m.index(0), ShotListModel::LmClubOverrideRole).toString(), QStringLiteral("DRIVER"));
    {
        ShotListModel reloaded;
        reloaded.loadSessionDir(session);
        checkStr(QStringLiteral("M14 the decline survives a reload"),
                 reloaded.data(reloaded.index(0), ShotListModel::LmClubOverrideRole).toString(),
                 QStringLiteral("DRIVER"));
    }

    // Cleared: the keys leave the document entirely.
    m.setLmExcluded(id, false);
    m.setLmClubOverride(id, QString());
    {
        const QJsonObject review = pinpoint::SwingStore::load(swing)
                                       .value(QStringLiteral("review")).toObject();
        check(QStringLiteral("M15 restored: no excluded key left in the document"),
              !review.contains(QStringLiteral("excluded")));
        check(QStringLiteral("M16 default club: no clubOverride key left in the document"),
              !review.contains(QStringLiteral("clubOverride")));
        checkInt(QStringLiteral("M17 and the rating beside them is still there"),
                 review.value(QStringLiteral("rating")).toInt(), 4);
        ShotListModel reloaded;
        reloaded.loadSessionDir(session);
        check(QStringLiteral("M18 restored reads back as not left out"),
              !reloaded.data(reloaded.index(0), ShotListModel::LmExcludedRole).toBool());
    }

    // In memory only: no folder, nothing to write, nothing goes wrong.
    {
        ShotListModel mem;
        const int memId = mem.addShot(QString(), QStringLiteral("13:00"), QStringLiteral("DRIVER"),
                                      false, QUrl(), {}, 0, {});
        mem.setLmExcluded(memId, true);
        check(QStringLiteral("M19 an in-memory shot still holds its left-out flag"),
              mem.data(mem.index(0), ShotListModel::LmExcludedRole).toBool());
    }
}

// ---------------------------------------------------------------------------
// R — the launch monitor's readings after the cheap reload (#52)
// ---------------------------------------------------------------------------
//
// loadSessionDir seeds rows from the summary and leaves analysisDetail empty, so the board
// (LmSessionModel), which reads the raw doubles out of it, said "no readings" after End Session
// over a session full of them. It now reads such a row's slice with readLaunchMonitorDetail().
// The oracle is agreement with the full read: the board must see the same entries whichever
// read filled the row.

static QVariantList lmEntries(const QVariantMap &detail)
{
    QVariantList out;
    for (const QVariant &v : detail.value(QStringLiteral("series")).toList())
        if (v.toMap().value(QStringLiteral("key")).toString().startsWith(QLatin1String("lm.")))
            out.append(v);
    return out;
}

static double impactValue(const QVariantMap &detail, const QString &key)
{
    for (const QVariant &v : detail.value(QStringLiteral("series")).toList()) {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("key")).toString() != key)
            continue;
        for (const QVariant &p : m.value(QStringLiteral("phaseSamples")).toList())
            if (p.toMap().value(QStringLiteral("phase")).toInt() == int(analysis::Phase::Impact))
                return p.toMap().value(QStringLiteral("value")).toDouble();
    }
    return std::nan("");
}

static void testLmReadingsAfterCheapReload()
{
    std::printf("\nR — a cheap-loaded row still has its launch monitor readings (#52)\n");

    TempLibrary lib;
    if (!lib.valid()) { check(QStringLiteral("R skipped — no temp dir"), false); return; }
    const QString session = lib.makeSession(QStringLiteral("Mark-Liversedge"),
                                            QStringLiteral("2026-10-20_session_01"));

    // Two analysed swings, as the capture pipeline writes them: our own clubhead speed and optical
    // low point on both. The second then has a monitor's reading folded in late, the 20 Oct way.
    auto writeAnalysed = [](const QString &dir, int index) {
        QDir().mkpath(dir);
        QJsonObject root;
        root[QStringLiteral("swing")] = QJsonObject{ { QStringLiteral("index"), index } };
        root[QStringLiteral("clock")] = QJsonObject{
            { QStringLiteral("t0_us"), 0 }, { QStringLiteral("unit"), QStringLiteral("us") },
            { QStringLiteral("wallclock"), QDateTime::currentDateTime().toString(Qt::ISODateWithMs) } };
        root[QStringLiteral("capture")] = QJsonObject{ { QStringLiteral("impactUs"), 1234567 } };
        analysis::SwingAnalysis an;
        an.tier = int(analysis::ReconstructionTier::Angles2D);
        an.gatesVersion = 1;    // analysed after gate records existed, so the monitor's can land
        for (const auto &[key, v] : { std::pair{ QStringLiteral("clubheadSpeed"), 84.0 },
                                      std::pair{ QStringLiteral("lowPointAhead"), 2.5 } }) {
            analysis::MetricSeries s;
            s.key = key;
            s.phaseSamples.push_back({ analysis::Phase::Impact, 1234567, v, QString() });
            an.series.push_back(s);
        }
        QString err;
        return SwingDocWriter::writeSwingJson(dir, root, &an, &err, QStringLiteral("7 IRON"));
    };
    const QString plain = session + QStringLiteral("/swing_0001");
    const QString withLm = session + QStringLiteral("/swing_0002");
    check(QStringLiteral("R0 two analysed swings written"),
          writeAnalysed(plain, 1) && writeAnalysed(withLm, 2));

    lm::LaunchMonitorReading reading;
    reading.deviceKind    = QStringLiteral("gcquad");
    reading.deviceShotId  = QStringLiteral("283");
    reading.ballSpeed     = 79.4;
    reading.spinRate      = 9401.0;
    reading.attackAngle   = -5.0;
    reading.clubheadSpeed = 61.2;
    // A field the import dropped, so the slice has a gate record to carry (the tile's words for "-").
    lm::DroppedField face;
    face.key  = QStringLiteral("lm.faceAngle");
    face.code = QStringLiteral("noData");
    reading.dropped.push_back(face);
    QString lerr;
    check(QStringLiteral("R1 the reading folds into the second swing"),
          SwingDocWriter::updateLaunchMonitor(withLm, reading, &lerr));

    // The precondition the bug lived in: the cheap reload knows a device measured the shot, and
    // holds none of what it measured.
    ShotListModel m;
    m.loadSessionDir(session);
    checkInt(QStringLiteral("R2 both swings reloaded"), m.rowCount(), 2);
    const QModelIndex lmRow = m.index(0), plainRow = m.index(1);   // newest first
    checkStr(QStringLiteral("R3 the cheap row knows its device"),
             m.data(lmRow, ShotListModel::LmDeviceKindRole).toString(), QStringLiteral("gcquad"));
    check(QStringLiteral("R4 …and carries no analysisDetail (the summary read)"),
          m.data(lmRow, ShotListModel::AnalysisDetailRole).toMap().isEmpty());
    checkStr(QStringLiteral("R5 the shot no device measured has no device"),
             m.data(plainRow, ShotListModel::LmDeviceKindRole).toString(), QString());

    // The slice against the full read the sessions drawer does.
    const QVariantMap slice = SwingDocReader::readLaunchMonitorDetail(withLm);
    const pinpoint::PersistedShot full = SwingDocReader::readSwingJson(withLm);
    const QVariantList sliceLm = lmEntries(slice), fullLm = lmEntries(full.analysisDetail);
    check(QStringLiteral("R6 the slice holds lm. entries (%1)").arg(sliceLm.size()), !sliceLm.isEmpty());
    check(QStringLiteral("R7 …exactly the full read's, entry for entry"), sliceLm == fullLm);
    check(QStringLiteral("R8 ball speed at Impact is the raw double (got %1)")
              .arg(impactValue(slice, QStringLiteral("lm.ballSpeed"))),
          impactValue(slice, QStringLiteral("lm.ballSpeed")) == 79.4);
    check(QStringLiteral("R9 our optical low point rides with it, as the full read has it"),
          impactValue(slice, QStringLiteral("lowPointAhead")) == 2.5
              && impactValue(full.analysisDetail, QStringLiteral("lowPointAhead")) == 2.5);
    check(QStringLiteral("R10 our own bare curves are not converted"),
          std::isnan(impactValue(slice, QStringLiteral("clubheadSpeed")))
              && impactValue(full.analysisDetail, QStringLiteral("clubheadSpeed")) == 84.0);
    check(QStringLiteral("R11 the gate records match the full read's (%1)")
              .arg(slice.value(QStringLiteral("gates")).toList().size()),
          !slice.value(QStringLiteral("gates")).toList().isEmpty()
              && slice.value(QStringLiteral("gates")) == full.analysisDetail.value(QStringLiteral("gates"))
              && slice.value(QStringLiteral("gatesVersion")) == full.analysisDetail.value(QStringLiteral("gatesVersion")));

    check(QStringLiteral("R12 a swing no device measured has no lm. entries"),
          lmEntries(SwingDocReader::readLaunchMonitorDetail(plain)).isEmpty());
    check(QStringLiteral("R13 no document: empty"),
          SwingDocReader::readLaunchMonitorDetail(session + QStringLiteral("/swing_0099")).isEmpty());
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

// ---------------------------------------------------------------------------
// D — deleting a swing whose videos are open (#56)
// ---------------------------------------------------------------------------
//
// Selecting a swing opens its MP4s in the replay, and in a live session with a
// cast screen the newest swing is open too. Windows will not move a folder with
// a file open in it, so "select, then delete" failed on the cabin PC every
// time. The contract under test: the model asks its releaser to let go of the
// folder BEFORE the move, and retries while the player closes its files (they
// close a moment after it is told to). The real case is a file genuinely held
// open and a genuine rename, which is the refusal on Windows and passes
// trivially elsewhere; the retry is also driven through a trash function that
// refuses while a lock is held, so it is exercised on every platform.

// A swing folder holding a swing.json and a video file, as the exporter leaves one.
static QString makeSwing(const QString &sessionDir, int index)
{
    const QString dir = sessionDir + QStringLiteral("/swing_%1").arg(index, 4, 10, QLatin1Char('0'));
    if (!writeSwingDoc(dir, index, QStringLiteral("7-iron"), true, QDateTime::currentDateTime()))
        return {};
    QFile video(dir + QStringLiteral("/dtl.mp4"));
    if (!video.open(QIODevice::WriteOnly) || video.write(QByteArray(4096, '\x5a')) < 0)
        return {};
    return dir;
}

// The move as the share's fallback makes it: a rename on the same volume.
static bool renameAway(const QString &dir)
{
    return QDir().rename(dir, dir + QStringLiteral("-trashed"));
}

static void testTrashOpenSwing()
{
    std::printf("\nD — a swing open in replay can be deleted\n");

    TempLibrary lib;
    if (!lib.valid()) { check(QStringLiteral("D0 temp dir"), false); return; }
    const QString session = lib.makeSession(QStringLiteral("Mark-Liversedge"),
                                            QStringLiteral("2026-10-20_Mark-Liversedge_Swing_01"));

    // D1–D6: a real file held open, released by the releaser, a real move.
    {
        const QString dir = makeSwing(session, 1);
        ShotListModel m;
        const int id = m.addShot(dir, QStringLiteral("13:00"), QStringLiteral("7-iron"),
                                 true, QUrl(), {}, 70, {});
        auto *video = new QFile(dir + QStringLiteral("/dtl.mp4"));
        check(QStringLiteral("D1 the swing's video is open, as the replay holds it"),
              video->open(QIODevice::ReadOnly));
#ifdef Q_OS_WIN
        // The bug, reproduced: with the video open the folder will not move.
        const bool movedOpen = renameAway(dir);
        if (movedOpen) QDir().rename(dir + QStringLiteral("-trashed"), dir);   // put it back for D2
        check(QStringLiteral("D1w Windows refuses to move a folder with a file open in it"), !movedOpen);
#endif
        QStringList order;
        m.setFolderReleaser([&](const QString &d) {
            order << QStringLiteral("release");
            if (d == dir && video) {
                // The player lets go a moment later, not at once.
                // Owned by the model's lifetime, so a timer that outlives this block never fires.
                QTimer::singleShot(250, &m, [&video] { delete video; video = nullptr; });
            }
        });
        m.setTrashFunction([&](const QString &d) { order << QStringLiteral("move"); return renameAway(d); });
        m.setTrashRetry(50, 4000);
        QSignalSpy trashed(&m, &ShotListModel::shotTrashed);
        QSignalSpy finished(&m, &ShotListModel::trashFinished);
        m.trashShots({ id });
        if (finished.isEmpty()) finished.wait(5000);
        check(QStringLiteral("D2 the releaser is asked before the first move (%1)").arg(order.join(QLatin1Char(','))),
              !order.isEmpty() && order.first() == QStringLiteral("release"));
        checkInt(QStringLiteral("D3 the request finishes once"), finished.count(), 1);
        if (!finished.isEmpty()) {
            checkInt(QStringLiteral("D4 and reports the swing moved"), finished.first().at(1).toInt(), 1);
            checkStr(QStringLiteral("D4b with no reason"), finished.first().at(2).toString(), QString());
        }
        check(QStringLiteral("D5 the folder has left the session"),
              !QFileInfo::exists(dir) && QFileInfo::exists(dir + QStringLiteral("-trashed")));
        checkInt(QStringLiteral("D6 its row is gone, announced with its folder"), m.activeCount(), 0);
        if (!trashed.isEmpty())
            checkStr(QStringLiteral("D6b shotTrashed names the folder"), trashed.first().at(1).toString(), dir);
        delete video;
    }

    // D7–D9: the retry, on every platform — a move refused while the "file" is held.
    {
        const QString dir = makeSwing(session, 2);
        ShotListModel m;
        const int id = m.addShot(dir, QStringLiteral("13:01"), QStringLiteral("7-iron"),
                                 true, QUrl(), {}, 70, {});
        bool held = true;
        int attempts = 0;
        m.setFolderReleaser([&](const QString &) { QTimer::singleShot(300, &m, [&held] { held = false; }); });
        m.setTrashFunction([&](const QString &d) { ++attempts; return !held && renameAway(d); });
        m.setTrashRetry(40, 4000);
        QSignalSpy finished(&m, &ShotListModel::trashFinished);
        m.trashShots({ id });
        checkInt(QStringLiteral("D7 the row stays while its folder is refused"), m.activeCount(), 1);
        if (finished.isEmpty()) finished.wait(5000);
        check(QStringLiteral("D8 the move is tried again until the files are let go (%1 attempts)").arg(attempts),
              attempts > 1 && !finished.isEmpty() && finished.first().at(1).toInt() == 1);
        checkInt(QStringLiteral("D9 and then the row goes"), m.activeCount(), 0);
    }

    // D10–D12: never let go — it gives up, says why, and keeps the row.
    {
        const QString dir = makeSwing(session, 3);
        ShotListModel m;
        const int id = m.addShot(dir, QStringLiteral("13:02"), QStringLiteral("7-iron"),
                                 true, QUrl(), {}, 70, {});
        m.setTrashFunction([](const QString &) { return false; });
        m.setTrashRetry(20, 200);
        QSignalSpy finished(&m, &ShotListModel::trashFinished);
        m.trashShots({ id });
        if (finished.isEmpty()) finished.wait(3000);
        check(QStringLiteral("D10 a folder that never comes free is given up on"),
              finished.count() == 1 && finished.first().at(1).toInt() == 0);
        if (!finished.isEmpty())
            check(QStringLiteral("D11 with the reason (\"%1\")").arg(finished.first().at(2).toString()),
                  finished.first().at(2).toString().contains(QStringLiteral("#3"))
                      && finished.first().at(2).toString().contains(QStringLiteral("in use")));
        check(QStringLiteral("D12 and the row and its files stay"), m.activeCount() == 1 && QFileInfo::exists(dir));
    }

    // D13–D17: one request, three kinds of shot — a swing being re-analysed (refused, with
    // the reason, and not released), a swing that moves, and a row with no folder.
    {
        const QString busy = makeSwing(session, 4);
        const QString free = makeSwing(session, 5);
        ShotListModel m;
        const int a = m.addShot(busy, QStringLiteral("13:03"), QStringLiteral("7-iron"), true, QUrl(), {}, 70, {});
        const int b = m.addShot(free, QStringLiteral("13:04"), QStringLiteral("7-iron"), true, QUrl(), {}, 70, {});
        const int c = m.addShot(QString(), QStringLiteral("13:05"), QStringLiteral("7-iron"), false, QUrl(), {}, 0, {});
        QStringList released;
        m.setFolderReleaser([&](const QString &d) { released << d; });
        m.setFolderBusyCheck([&](const QString &d) {
            return d == busy ? QStringLiteral("is being re-analysed") : QString();
        });
        m.setTrashFunction(renameAway);
        QSignalSpy trashed(&m, &ShotListModel::shotTrashed);
        QSignalSpy finished(&m, &ShotListModel::trashFinished);
        m.trashShots({ a, b, c });
        if (finished.isEmpty()) finished.wait(3000);
        check(QStringLiteral("D13 one report for the request: 2 of 3 moved"),
              finished.count() == 1 && finished.first().at(0).toInt() == 3
                  && finished.first().at(1).toInt() == 2);
        if (!finished.isEmpty())
            checkStr(QStringLiteral("D14 the reason names the refused swing"),
                     finished.first().at(2).toString(), QStringLiteral("Swing #4 is being re-analysed"));
        check(QStringLiteral("D15 the refused swing is not released, and stays"),
              !released.contains(busy) && QFileInfo::exists(busy) && m.ordinalForId(a) == 4);
        check(QStringLiteral("D16 the others are gone"), m.ordinalForId(b) == 0 && m.ordinalForId(c) == 0
                                                          && !QFileInfo::exists(free));
        checkInt(QStringLiteral("D17 each row that left was announced"), trashed.count(), 2);
    }
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    std::printf("shot_ledger_test — carousel ordinals, reload and write-through\n");
    testOrdinals();
    testOrdinalAgreement();
    testLoadSessionDir();
    testReviewWriteThrough();
    testLmAnnotationsWriteThrough();
    testLmReadingsAfterCheapReload();
    testPreviousAnalysisDetail();
    testAttachSwingDir();
    testActiveCountSignal();
    testTrashOpenSwing();
    std::printf("\n%d checks, %d failed\n", g_run, g_fail);
    return g_fail == 0 ? 0 : 1;
}
