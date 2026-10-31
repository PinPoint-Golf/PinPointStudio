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

// The launch monitor board's empty lines (#57): which sentence a loaded session gets, and when the
// Session view may draw. Four states, each a different sentence to the golfer:
//   the picked shot has a reading        → the board draws, no line;
//   the picked shot has none, others do  → about THE SHOT, never "no readings in this session";
//   nothing picked, the latest has none  → about the latest shot (the board headlines it);
//   no shot has a reading                → "No launch monitor readings in this session".
// graphics.has is the headline shot's; graphics.sessionHas the scope's, which gates the Session view.
// Rows are built in memory (addPersistedShot with an analysisDetail), so nothing is read from disk.

#include "launchmonitor/lm_session_model.h"
#include "shot/shot_list_model.h"
#include "shot/qml_payload.h"
#include "Analysis/swing_analysis.h"

#include <QCoreApplication>
#include <QJSEngine>
#include <QSignalSpy>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <cstdio>
#include <initializer_list>

using namespace pinpoint;

static int g_fail = 0;
static int g_run  = 0;

static void check(const QString &label, bool ok)
{
    ++g_run;
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", qPrintable(label));
    if (!ok) ++g_fail;
}

static void checkStr(const QString &label, const QString &got, const QString &want)
{
    check(QStringLiteral("%1 (got \"%2\", want \"%3\")").arg(label, got, want), got == want);
}

// A row's analysisDetail as the writer leaves it: each lm.* reading a PointInTime sample at Impact.
static QVariantMap lmDetail(bool measured)
{
    QVariantList series;
    if (measured) {
        for (const auto &[key, v] : { std::pair{ "lm.ballSpeed", 80.6 }, std::pair{ "lm.clubheadSpeed", 75.2 },
                                      std::pair{ "lm.carryDistance", 96.9 } }) {
            const QVariantMap sample{ { QStringLiteral("phase"), int(analysis::Phase::Impact) },
                                      { QStringLiteral("value"), v } };
            series.append(QVariantMap{ { QStringLiteral("key"), QString::fromLatin1(key) },
                                       { QStringLiteral("phaseSamples"), QVariantList{ sample } } });
        }
    }
    return { { QStringLiteral("series"), series } };
}

// Shots 1..n in capture order (the model keeps them newest-first); `measured` lists the ordinals a
// monitor read; `driverFrom` makes that ordinal and every later one a driver (0: all 7 irons).
static void fill(ShotListModel &m, int n, std::initializer_list<int> measured, int driverFrom = 0,
                 std::initializer_list<int> leftOut = {})
{
    for (int ord = 1; ord <= n; ++ord) {
        bool lm = false, out = false;
        for (int x : measured) lm = lm || x == ord;
        for (int x : leftOut) out = out || x == ord;
        const QString club = driverFrom > 0 && ord >= driverFrom ? QStringLiteral("DRIVER") : QStringLiteral("7 IRON");
        m.addPersistedShot(QStringLiteral("/nonexistent/swing_%1").arg(ord, 4, 10, QLatin1Char('0')), ord,
                           QStringLiteral("10:%1").arg(ord, 2, 10, QLatin1Char('0')), club,
                           false, QUrl(), 0, 0, QString(), {}, lmDetail(lm), false,
                           lm ? QStringLiteral("gcquad") : QString(), {}, out);
    }
}

static int idOf(const ShotListModel &m, int ordinal)
{
    for (int r = 0; r < m.rowCount(); ++r)
        if (m.data(m.index(r), ShotListModel::OrdinalRole).toInt() == ordinal)
            return m.data(m.index(r), ShotListModel::ShotIdRole).toInt();
    return -1;
}

static bool has(const LmSessionModel &b)        { return b.graphics().value(QStringLiteral("has")).toBool(); }
static bool sessionHas(const LmSessionModel &b) { return b.graphics().value(QStringLiteral("sessionHas")).toBool(); }
static int headlineId(const LmSessionModel &b)  { return b.graphics().value(QStringLiteral("headlineShotId"), -1).toInt(); }

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    // The shot model hands a row's analysisDetail out as a value of the app's one engine
    // (qml_payload.h); with none registered it is undefined and the board would see no readings.
    QJSEngine engine;
    QmlPayload::setEngine(&engine);
    std::printf("lm_session_model_test — the launch monitor board's empty lines (#57)\n");

    {
        std::printf("\nA — shots 2 and 3 read, shot 1 not\n");
        ShotListModel shots;
        fill(shots, 3, { 2, 3 });
        LmSessionModel board;
        board.setReviewing(true);
        board.setShotModel(&shots);

        board.setFocusedShotId(idOf(shots, 2));
        checkStr("A1 picked shot with a reading: no session line", board.emptyText(), QString());
        checkStr("A1 picked shot with a reading: no shot line", board.shotEmptyText(), QString());
        check("A1 the shot and the session both have readings", has(board) && sessionHas(board));

        board.setFocusedShotId(idOf(shots, 1));
        checkStr("A2 picked shot without one: no session line", board.emptyText(), QString());
        checkStr("A2 picked shot without one: says so about the shot", board.shotEmptyText(),
                 QStringLiteral("No launch monitor reading for this shot"));
        check("A2 the shot has none, the Session view still draws", !has(board) && sessionHas(board));
    }
    {
        std::printf("\nB — shots 1 and 2 read, the latest (3) not, nothing picked\n");
        ShotListModel shots;
        fill(shots, 3, { 1, 2 });
        LmSessionModel board;
        board.setReviewing(true);
        board.setShotModel(&shots);
        checkStr("B1 no session line", board.emptyText(), QString());
        checkStr("B1 names the latest shot", board.shotEmptyText(),
                 QStringLiteral("No launch monitor reading for the latest shot"));
        check("B1 the latest has none, the Session view still draws", !has(board) && sessionHas(board));
        check("B1 the headline is the latest shot", headlineId(board) == idOf(shots, 3));

        // A focus id no row carries (a shot of the session left behind): read as nothing picked.
        QSignalSpy spy(&board, &LmSessionModel::stateTextChanged);
        board.setFocusedShotId(9999);
        check("B2 a focus change re-announces the lines (stateTextChanged)", spy.count() >= 1);
        checkStr("B2 stale focus: names the latest shot, not \"this shot\"", board.shotEmptyText(),
                 QStringLiteral("No launch monitor reading for the latest shot"));
        check("B2 stale focus: the Session view still draws", sessionHas(board));
        spy.clear();
        board.setFocusedShotId(idOf(shots, 2));
        check("B3 picking a read shot re-announces the lines", spy.count() >= 1);
        checkStr("B3 picking a read shot clears the shot line", board.shotEmptyText(), QString());
    }
    {
        std::printf("\nD — nothing picked: 7 irons 1-4 read, then two unread drivers (5, 6, the latest)\n");
        ShotListModel shots;
        fill(shots, 6, { 1, 2, 3, 4 }, 5);
        LmSessionModel board;
        board.setReviewing(true);
        board.setShotModel(&shots);
        checkStr("D1 no session line (the 7 irons have readings)", board.emptyText(), QString());
        check("D1 the Session view draws", sessionHas(board));
        check(QStringLiteral("D1 scoped to the latest READ shot's club, its 4 shots (scope \"%1\")").arg(board.scopeText()),
              board.scopeText().contains(QLatin1String("7 IRON")) && board.shotCount() == 4);
        check("D1 headlines the latest 7 iron (shot 4), which has a reading",
              headlineId(board) == idOf(shots, 4) && has(board));
        checkStr("D1 no shot line", board.shotEmptyText(), QString());
    }
    {
        std::printf("\nE — nothing picked: the latest read shot is left out\n");
        // 7 irons 1-2 read, then a driver (3) read but left out: its club's only reading.
        ShotListModel shots;
        fill(shots, 3, { 1, 2, 3 }, 3, { 3 });
        LmSessionModel board;
        board.setReviewing(true);
        board.setShotModel(&shots);
        checkStr("E1 no \"no readings for this club\" line", board.emptyText(), QString());
        check(QStringLiteral("E1 scoped to the next read, kept shot's club (scope \"%1\")").arg(board.scopeText()),
              board.scopeText().contains(QLatin1String("7 IRON")) && headlineId(board) == idOf(shots, 2) && has(board));
        check("E1 the Session view draws", sessionHas(board));
    }
    {
        std::printf("\nF — nothing picked: the only read shot is left out\n");
        ShotListModel shots;
        fill(shots, 3, { 2 }, 0, { 2 });
        LmSessionModel board;
        board.setReviewing(true);
        board.setShotModel(&shots);
        check(QStringLiteral("F1 no read, kept shot: the whole session (scope \"%1\")").arg(board.scopeText()),
              board.scopeText().contains(QLatin1String("all clubs")));
    }
    {
        std::printf("\nC — no shot read\n");
        ShotListModel shots;
        fill(shots, 2, {});
        LmSessionModel board;
        board.setReviewing(true);
        board.setShotModel(&shots);
        checkStr("C1 nothing picked: the session line", board.emptyText(),
                 QStringLiteral("No launch monitor readings in this session"));
        checkStr("C1 nothing picked: no shot line beside it", board.shotEmptyText(), QString());
        board.setFocusedShotId(idOf(shots, 1));
        checkStr("C2 a shot picked: still the session line", board.emptyText(),
                 QStringLiteral("No launch monitor readings in this session"));
        checkStr("C2 a shot picked: no shot line beside it", board.shotEmptyText(), QString());
        check("C2 neither the shot nor the session has readings", !has(board) && !sessionHas(board));
        board.setFocusedShotId(-1);
        check(QStringLiteral("C3 nothing picked, nothing read: the whole session (scope \"%1\")").arg(board.scopeText()),
              board.scopeText().contains(QLatin1String("all clubs")));
    }

    std::printf("\n%d checks, %d failed\n", g_run, g_fail);
    return g_fail == 0 ? 0 : 1;
}
