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

// shot_number_test — "SHOT N" IN DIAGNOSTICS IS THE SWING NUMBER, AND A SESSION IS PLACED BY ITS IDENTITY, NOT ITS
// FOLDER'S NAME (refactor step 2.7a; audit E-06/F-07, E-05, F-06/C-11; build/refactor/step-2/BRIEF.md §3.1, §3.2;
// docs/developer/information_model.md ID-SWING-NUMBER, P7, GL-07).
//
//   S  after a trashed swing (3) and one with no document yet (4), the ledger holds swings 1, 2, 5, 6: its "shot N"
//      — the header's label, the review badge, every readout on the run and the current one — is the swing number
//      (shot 5, shot 6), never the ledger position (shot 3, shot 4). A session with no gap reads as it always did.
//      A swing folder of ANOTHER session is never matched by its name (E-05)
//   D  the session's day and its place in the day come from SessionIdentity: under the name-date-type pattern (no
//      folder starts with its date) the session still has its day, so a live panel freezes its club baseline over
//      the day before (F-06: before step 2.7a it had no day and stayed unfrozen); two sessions of one day are
//      placed by their session files' start times, whatever their names say, and the tendencies "as of" a session
//      follow that place
//
//   cmake --build build/tests --target shot_number_test -j 6
//   ctest --test-dir build/tests -R '^shot_number_test$' --output-on-failure

#include "../../Gui/diagnostics/session_diagnostics_model.h"
#include "../../Export/rule_data_install.h"
#include "../../Export/session_diagnostics_files.h"
#include "../../Export/session_files.h"
#include "../../Export/swing_paths.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QUuid>

#include <cstdio>
#include <memory>

using pinpoint::SessionDiagnosticsFiles;
using pinpoint::SessionFiles;
using pinpoint::SwingPaths;
using pinpoint::analysis::SessionPlace;

static int g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}
static void checkEq(const QString &got, const QString &want, const char *label)
{
    const bool ok = got == want;
    std::printf("  [%s] %s%s%s%s\n", ok ? "PASS" : "FAIL", label, ok ? "" : " — got \"", ok ? "" : qPrintable(got),
                ok ? "" : "\"");
    if (!ok) ++g_fail;
}

#ifndef PP_LIVE_SWINGS_DIR
#  define PP_LIVE_SWINGS_DIR "."
#endif

static constexpr qint64 kStageEpochMs = 1767225600000LL;   // 2026-01-01T00:00:00Z

static QString makeSession(const QTemporaryDir &tmp, const char *athlete, const char *session)
{
    const QString dir = QDir(tmp.path()).filePath(QLatin1String(athlete) + QLatin1Char('/') + QLatin1String(session));
    QDir().mkpath(dir);
    return dir;
}

static QString swingDir(const QString &sessionDir, int n)
{
    return QDir(sessionDir).filePath(SwingPaths::swingFolderName(n));
}

// A corpus fixture as <session>/swing_000N/swing.json, its mtime pinned (session_diagnostics_model_test's rule).
static bool stageShot(const QString &sessionDir, int n, const char *fixture = "rich_7iron")
{
    const QString dst = swingDir(sessionDir, n);
    if (!QDir().mkpath(dst)) return false;
    const QString src = QDir(QLatin1String(PP_LIVE_SWINGS_DIR)).filePath(QLatin1String(fixture) + QLatin1String("/swing.json"));
    const QString file = QDir(dst).filePath(QStringLiteral("swing.json"));
    if (!QFile::copy(src, file)) return false;
    QFile f(file);
    if (!f.open(QIODevice::ReadWrite)) return false;
    return f.setFileTime(QDateTime::fromMSecsSinceEpoch(kStageEpochMs + qint64(n) * 1000, QTimeZone::UTC),
                         QFileDevice::FileModificationTime);
}

static std::unique_ptr<SessionDiagnosticsModel> freshModel(
    SessionDiagnosticsModel::BaselineAuthority authority = SessionDiagnosticsModel::BaselineAuthority::Never)
{
    auto m = std::make_unique<SessionDiagnosticsModel>();
    m->setSynchronous(true);
    m->setCadence(QStringLiteral("everyShot"));
    m->setBaselineAuthority(authority);
    return m;
}

static QList<int> ledgerIds(const SessionDiagnosticsModel &m)
{
    QList<int> ids;
    for (const auto &s : m.shotRecords()) ids << s.shotId;
    return ids;
}

static QVariantMap spreadOf(const SessionDiagnosticsModel &m)
{
    return m.detail().value(QStringLiteral("header")).toMap().value(QStringLiteral("spread")).toMap();
}

static QString runReadout(const QVariantMap &spread, int i)
{
    const QVariantList run = spread.value(QStringLiteral("run")).toList();
    return i >= 0 && i < run.size() ? run.at(i).toMap().value(QStringLiteral("readout")).toString() : QString();
}

// A session file as the store writes it (FILE-SESS-01), its start time given.
static bool writeSessionFile(const QString &sessionDir, const QDateTime &startedAtUtc)
{
    const QJsonObject o{
        { QStringLiteral("schema"), QStringLiteral("pinpoint.session/1") },
        { QStringLiteral("sessionId"), QUuid::createUuid().toString(QUuid::WithoutBraces) },
        { QStringLiteral("athleteId"), QUuid::createUuid().toString(QUuid::WithoutBraces) },
        { QStringLiteral("type"), QStringLiteral("swing") },
        { QStringLiteral("status"), QStringLiteral("ended") },
        { QStringLiteral("startedAt"), startedAtUtc.toUTC().toString(Qt::ISODateWithMs) },
    };
    QFile f(SessionFiles::sessionFile(sessionDir));
    return f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(QJsonDocument(o).toJson()) > 0;
}

// ── S: "shot N" is the swing number ───────────────────────────────────────────────────────────────────────
static void testShotNumber(const QTemporaryDir &tmp)
{
    std::printf("\nS — \"shot N\" is the swing number\n");
    const QString dir = makeSession(tmp, "athlete_numbers", "2026-11-15_M_01");
    for (int n : { 1, 2, 3, 5, 6 }) check(stageShot(dir, n), "a swing staged");
    check(QDir().mkpath(swingDir(dir, 4)), "swing 4: its folder made, no document yet (allocated, never written)");
    {
        auto first = freshModel();
        first->activateSession(dir);
        check(ledgerIds(*first) == QList<int>({ 1, 2, 3, 5, 6 }), "the first read: swings 1, 2, 3, 5, 6 (4 has no document)");
    }
    // Swing 3 leaves the session (the trash): its number goes with it.
    check(QDir(swingDir(dir, 3)).removeRecursively(), "swing 3 trashed");

    auto m = freshModel();
    m->activateSession(dir);
    check(ledgerIds(*m) == QList<int>({ 1, 2, 5, 6 }), "S1 the ledger holds swings 1, 2, 5, 6");
    checkEq(m->headerInfo().value(QStringLiteral("shotLabel")).toString(), QStringLiteral("shot 6"),
            "S2 live, the header names the newest shot by its swing number: shot 6 (not shot 4)");

    m->openDetail(QStringLiteral("over_the_top"));
    {
        const QVariantMap sp = spreadOf(*m);
        check(sp.value(QStringLiteral("run")).toList().size() == 4, "S3 the run has a point for each of the four shots");
        check(runReadout(sp, 0).startsWith(QStringLiteral("shot 1 · ")) && runReadout(sp, 1).startsWith(QStringLiteral("shot 2 · ")),
              "S4 before the gap the readouts read shot 1, shot 2");
        check(runReadout(sp, 2).startsWith(QStringLiteral("shot 5 · ")) && runReadout(sp, 3).startsWith(QStringLiteral("shot 6 · ")),
              "S5 after it: shot 5, shot 6 — the swing numbers, never positions 3 and 4");
        std::printf("      run[2]: \"%s\"\n", qPrintable(runReadout(sp, 2)));
        check(sp.value(QStringLiteral("currentReadout")).toString().startsWith(QStringLiteral("shot 6 · ")),
              "S6 the current readout reads the newest swing: shot 6");
    }

    m->setReviewing(true);
    m->setSelectedShotId(2);
    checkEq(m->headerInfo().value(QStringLiteral("reviewBadge")).toString(), QStringLiteral("REVIEWING · shot 2 of 4"),
            "S6b reviewing swing 2, before the gap (number = place): today's two-part text");
    m->setSelectedShotId(5);
    checkEq(m->headerInfo().value(QStringLiteral("reviewBadge")).toString(), QStringLiteral("REVIEWING · shot 5 · 3 of 4"),
            "S7 reviewing swing 5: the badge names swing 5, and its place, 3rd of the ledger's 4 (J-54)");
    checkEq(m->headerInfo().value(QStringLiteral("shotLabel")).toString(), QStringLiteral("shot 5 · 3 of 4"),
            "S8 …and so does the header's label");
    check(spreadOf(*m).value(QStringLiteral("currentReadout")).toString().startsWith(QStringLiteral("shot 5 · ")),
          "S9 …and the readout of the picked shot");
    m->setReviewing(false);
    m->setSelectedShotId(-1);

    // The selection by folder: this session's swing is the shot of its swing number. (Another session's swing of
    // the same number is still found by its name — audit E-05, left to step 4's selection by ShotRef.)
    check(m->shotIdForSwingDir(swingDir(dir, 5)) == 5, "S10 this session's swing_0005 is shot 5");
    check(m->shotIdForSwingDir(QDir(dir).filePath(QStringLiteral("./swing_0005"))) == 5,
          "S11 …however its path is spelled");
    check(m->shotIdForSwingDir(swingDir(dir, 3)) == -1, "S12 the trashed swing 3 is no shot any more");
    check(m->shotIdForSwingDir(swingDir(dir, 4)) == -1, "S13 swing 4, with no document, is no shot yet");

    // A session with no gap reads exactly as before: the numbers are the positions.
    const QString plain = makeSession(tmp, "athlete_plain", "2026-11-15_M_01");
    for (int n : { 1, 2, 3 }) stageShot(plain, n);
    auto p = freshModel();
    p->activateSession(plain);
    checkEq(p->headerInfo().value(QStringLiteral("shotLabel")).toString(), QStringLiteral("shot 3"),
            "S14 no gap: \"shot 3\", as before");
    p->openDetail(QStringLiteral("over_the_top"));
    check(runReadout(spreadOf(*p), 1).startsWith(QStringLiteral("shot 2 · ")), "S15 no gap: the second readout is shot 2, as before");
    p->setReviewing(true);
    p->setSelectedShotId(2);
    checkEq(p->headerInfo().value(QStringLiteral("reviewBadge")).toString(), QStringLiteral("REVIEWING · shot 2 of 3"),
            "S16 no gap: \"REVIEWING · shot 2 of 3\", as before (the number and the place agree)");
    checkEq(p->headerInfo().value(QStringLiteral("shotLabel")).toString(), QStringLiteral("shot 2 of 3"),
            "S17 …and so does the header's label");
}

// ── S: the edges — swing 1 gone, and every swing gone ───────────────────────────────────────────────────────
static void testShotNumberEdges(const QTemporaryDir &tmp)
{
    std::printf("\nS — the edges: swing 1 trashed, then every swing\n");
    const QString dir = makeSession(tmp, "athlete_edges", "2026-11-15_M_01");
    for (int n : { 1, 2, 3 }) check(stageShot(dir, n), "a swing staged");
    {
        auto first = freshModel();
        first->activateSession(dir);
        check(first->shotCount() == 3, "the first read: three shots");
    }
    check(QDir(swingDir(dir, 1)).removeRecursively(), "swing 1 trashed");
    {
        auto m = freshModel();
        m->activateSession(dir);
        check(ledgerIds(*m) == QList<int>({ 2, 3 }), "S18 the ledger holds swings 2 and 3");
        checkEq(m->headerInfo().value(QStringLiteral("shotLabel")).toString(), QStringLiteral("shot 3"),
                "S19 live: \"shot 3\", the newest swing");
        m->setReviewing(true);
        m->setSelectedShotId(2);
        checkEq(m->headerInfo().value(QStringLiteral("reviewBadge")).toString(), QStringLiteral("REVIEWING · shot 2 · 1 of 2"),
                "S20 reviewing swing 2, now first: \"REVIEWING · shot 2 · 1 of 2\"");
        checkEq(m->headerInfo().value(QStringLiteral("shotLabel")).toString(), QStringLiteral("shot 2 · 1 of 2"),
                "S21 …and the header's label");
    }
    for (int n : { 2, 3 }) QDir(swingDir(dir, n)).removeRecursively();
    {
        auto m = freshModel();
        m->activateSession(dir);
        check(m->shotCount() == 0, "S22 every swing trashed: the ledger is empty");
        checkEq(m->headerInfo().value(QStringLiteral("shotLabel")).toString(), QStringLiteral("no shots yet"),
                "S23 …the header says \"no shots yet\", never a stale number");
        m->setReviewing(true);
        m->setSelectedShotId(2);
        checkEq(m->headerInfo().value(QStringLiteral("reviewBadge")).toString(), QString(),
                "S24 …and reviewing a shot that is gone shows no badge");
        check(m->shotIdForSwingDir(swingDir(dir, 2)) == -1, "S25 …and the trashed swing 2 is no shot");
    }
}

// ── D: the day and the place in it are SessionIdentity's ────────────────────────────────────────────────────
static void testDayOrder(const QTemporaryDir &tmp)
{
    std::printf("\nD — a session's day and place come from its identity, not its folder's name\n");

    // name-date-type: "<athlete>_<date>_<type>_NN" — no folder starts with its date (F-06).
    const QString earlier = makeSession(tmp, "M", "M_2026-11-14_Swing_01");     // legacy: no session file
    const QString now     = makeSession(tmp, "M", "M_2026-11-15_Swing_01");
    for (int n : { 1, 2, 3 }) check(stageShot(earlier, n, "lm_7iron"), "a 7 iron with a carry, the day before");
    for (int n : { 1, 2 }) check(stageShot(now, n, "lm_7iron"), "a 7 iron in the session to freeze");
    check(writeSessionFile(now, QDateTime(QDate(2026, 11, 15), QTime(9, 0), QTimeZone::UTC)),
          "the session's file: started 15 Nov 09:00");
    {
        auto m = freshModel();
        m->activateSession(earlier);        // its ledger, for the next session to read
        check(m->shotCount() == 3, "the day before is graded");
        check(m->sessionIdentity().localDay() == QDate(2026, 11, 14),
              "D1 a legacy name-date-type folder has its day (SessionIdentity reads it from the name)");
    }
    {
        // A LIVE panel freezes a session's club baseline only when the session has a day (a session with none has
        // no predecessors, and an empty snapshot would stand for good). Before step 2.7a a name-date-type folder
        // had no day — its first ten characters are no date — so a live panel left it unfrozen (F-06).
        auto m = freshModel(SessionDiagnosticsModel::BaselineAuthority::Live);
        m->activateSession(now);
        check(m->sessionIdentity().localDay() == QDate(2026, 11, 15),
              "D2 a session with a session file has its start time's day, under name-date-type");
        check(m->baselineFrozen() && QFileInfo::exists(SessionFiles::clubBaselineFile(now)),
              "D3 a live panel freezes its club baseline: the session has a day (before 2.7a: none, left unfrozen)");
        const QList<pinpoint::SessionIndexEntry> index = SessionDiagnosticsFiles::athleteSessions(QFileInfo(now).absolutePath());
        const SessionPlace pe = SessionDiagnosticsFiles::placeOf(index, earlier), pn = SessionDiagnosticsFiles::placeOf(index, now);
        check(pe.day == QDate(2026, 11, 14) && pn.day == QDate(2026, 11, 15) && pe.day < pn.day,
              "D4 …and the day before is its predecessor, placed by its day");
    }

    // Two sessions of one day: placed by their start times, whatever their names say.
    const QString a = makeSession(tmp, "N", "2026-11-16_N_Swing_01");
    const QString b = makeSession(tmp, "N", "2026-11-16_N_Swing_02");
    stageShot(a, 1);
    stageShot(b, 1);
    check(writeSessionFile(a, QDateTime(QDate(2026, 11, 16), QTime(15, 0), QTimeZone::UTC))
              && writeSessionFile(b, QDateTime(QDate(2026, 11, 16), QTime(9, 0), QTimeZone::UTC)),
          "\"…_01\" started at 15:00, \"…_02\" at 09:00");
    const QList<pinpoint::SessionIndexEntry> index = SessionDiagnosticsFiles::athleteSessions(QFileInfo(a).absolutePath());
    const SessionPlace pa = SessionDiagnosticsFiles::placeOf(index, a), pb = SessionDiagnosticsFiles::placeOf(index, b);
    std::printf("      _01: %s #%d   _02: %s #%d\n", qPrintable(pa.day.toString(Qt::ISODate)), pa.nOfDay,
                qPrintable(pb.day.toString(Qt::ISODate)), pb.nOfDay);
    check(index.size() == 2 && pb.nOfDay == 1 && pa.nOfDay == 2, "D5 the 09:00 session is 1st of the day, the 15:00 one 2nd");
    check(pinpoint::analysis::tendencySessionCounts(pb, pa) && !pinpoint::analysis::tendencySessionCounts(pa, pb),
          "D6 tendencies as of the 15:00 session count the 09:00 one, and not the other way round");
    check(pinpoint::analysis::sessionPlaceBefore(pb, pa), "D7 the day order puts \"…_02\" first");
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    pinpoint::installProcessRuleData();   // this process's packs and norms (refactor step 3.2)
    QTemporaryDir tmp;
    if (!tmp.isValid()) { std::printf("no temporary folder\n"); return 1; }

    testShotNumber(tmp);
    testShotNumberEdges(tmp);
    testDayOrder(tmp);

    std::printf("\n%s (%d failure%s)\n", g_fail == 0 ? "PASS" : "FAIL", g_fail, g_fail == 1 ? "" : "s");
    return g_fail == 0 ? 0 : 1;
}
