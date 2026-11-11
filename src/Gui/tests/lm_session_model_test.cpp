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
//
// And the variability marks the panel shows (docs/design/system/variability.md): G, club delivery's
// grade per club (R4b) — a two-sided corridor's half-width, the CV for club speed, ungraded without a
// scale, never smash factor or ball flight (R4), never across clubs; H, typicality — this shot against
// the session's other shots of its club, this one left out, at least N_MIN of them (R8).

#include "launchmonitor/lm_session_model.h"
#include "variability/variability_measures.h"
#include "shot/shot_list_model.h"
#include "shot/qml_payload.h"
#include "Analysis/swing_analysis.h"

#include <QCoreApplication>
#include <QSignalSpy>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <cstdio>
#include <initializer_list>
#include <optional>

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

// ── the variability marks: rows with readings of our choosing ──
// One shot's readings, every one a PointInTime sample at Impact.
static QVariantMap lmDetailOf(const QVariantMap &values)
{
    QVariantList series;
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        const QVariantMap sample{ { QStringLiteral("phase"), int(analysis::Phase::Impact) },
                                  { QStringLiteral("value"), it.value().toDouble() } };
        series.append(QVariantMap{ { QStringLiteral("key"), it.key() },
                                   { QStringLiteral("phaseSamples"), QVariantList{ sample } } });
    }
    return { { QStringLiteral("series"), series } };
}

// One shot, appended in capture order with the next ordinal.
static void addShot(ShotListModel &m, const QString &club, const QVariantMap &values, bool leftOut = false)
{
    const int ord = m.rowCount() + 1;
    m.addPersistedShot(QStringLiteral("/nonexistent/swing_%1").arg(ord, 4, 10, QLatin1Char('0')), ord,
                       QStringLiteral("11:%1").arg(ord, 2, 10, QLatin1Char('0')), club,
                       false, QUrl(), 0, 0, QString(), {}, lmDetailOf(values), false,
                       QStringLiteral("gcquad"), {}, leftOut);
}

// A club's statistics in graphics.clubs, by its effective club label.
static QVariantMap clubStats(const LmSessionModel &b, const QString &club)
{
    for (const QVariant &c : b.graphics().value(QStringLiteral("clubs")).toList())
        if (c.toMap().value(QStringLiteral("club")).toString() == club)
            return c.toMap().value(QStringLiteral("stats")).toMap();
    return {};
}
static QVariantMap varOf(const QVariantMap &stats, const char *key)
{
    return stats.value(QString::fromLatin1(key)).toMap().value(QStringLiteral("variability")).toMap();
}
static bool hasVar(const QVariantMap &stats, const char *key)
{
    return stats.value(QString::fromLatin1(key)).toMap().contains(QStringLiteral("variability"));
}
static QVariantMap typOf(const LmSessionModel &b, const char *key)
{
    return b.graphics().value(QStringLiteral("typicality")).toMap().value(QString::fromLatin1(key)).toMap();
}

static bool has(const LmSessionModel &b)        { return b.graphics().value(QStringLiteral("has")).toBool(); }
static bool sessionHas(const LmSessionModel &b) { return b.graphics().value(QStringLiteral("sessionHas")).toBool(); }
static int headlineId(const LmSessionModel &b)  { return b.graphics().value(QStringLiteral("headlineShotId"), -1).toInt(); }

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    // NO QML engine, on purpose (#173). The shot model's AnalysisDetailRole is the row's JavaScript
    // object (qml_payload.h) and with no engine registered it is undefined — so a board that read
    // its numbers through the role would see no readings and every case below would fail. The
    // board reads the C++ map (ShotListModel::analysisDetailMap) and never touches the engine;
    // reading through the role is what froze the app for minutes on entering Analyse.
    QmlPayload::setEngine(nullptr);
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

    {
        std::printf("\nG — club delivery is graded per club (R4b); ball flight and smash are not (R4)\n");
        // 11 7 irons (shot 6 left out) and 5 drivers. Path, attack and face to path have two-sided
        // corridors (core norms); club speed's is one-sided, graded on its σ all the same (R12); face, lie and strike height
        // have no corridor (Interval: ungraded); smash factor's norm is a floor, and an outcome.
        ShotListModel shots;
        for (int i = 0; i < 11; ++i)
            addShot(shots, QStringLiteral("7 IRON"), QVariantMap{
                { QStringLiteral("lm.clubPath"),       -1.0 + 0.2 * (i % 5) },
                { QStringLiteral("lm.attackAngle"),    -4.0 + 0.3 * ((i * 3) % 5) },
                { QStringLiteral("lm.faceToPath"),      0.5 * ((i % 4) - 1.5) },
                { QStringLiteral("lm.clubheadSpeed"),  80.0 + 0.5 * (i % 4) },
                { QStringLiteral("lm.faceAngle"),       0.4 * (i % 3) },
                { QStringLiteral("lm.lieAngle"),        1.0 + 0.1 * (i % 3) },
                { QStringLiteral("lm.strikeLocation"),  2.0 * ((i % 5) - 2) },
                { QStringLiteral("lm.strikeHeight"),    double((i % 3) - 1) },
                { QStringLiteral("lm.smashFactor"),     1.35 + 0.01 * (i % 3) },
                { QStringLiteral("lm.carryDistance"), 150.0 + i },
                { QStringLiteral("lm.ballSpeed"),     110.0 + (i % 3) } }, i == 5);
        for (int i = 0; i < 5; ++i)
            addShot(shots, QStringLiteral("DRIVER"), QVariantMap{
                { QStringLiteral("lm.clubPath"),       3.0 + 2.0 * (i % 5) },
                { QStringLiteral("lm.attackAngle"),    2.0 + 0.5 * i },
                { QStringLiteral("lm.clubheadSpeed"), 100.0 + i },
                { QStringLiteral("lm.carryDistance"), 240.0 + 3 * i } });
        LmSessionModel board;
        board.setReviewing(true);
        board.setShotModel(&shots);

        const QVariantMap iron = clubStats(board, QStringLiteral("7 IRON"));
        const QVariantMap drv  = clubStats(board, QStringLiteral("DRIVER"));
        const QVariantMap path = varOf(iron, "lm.clubPath");
        check(QStringLiteral("G1 7 iron path: graded on its 10 included shots (n %1, \"%2\")")
                  .arg(path.value(QStringLiteral("n")).toInt()).arg(path.value(QStringLiteral("basisText")).toString()),
              path.value(QStringLiteral("graded")).toBool() && path.value(QStringLiteral("grade")).toInt() >= 1
                  && path.value(QStringLiteral("n")).toInt() == 10);
        check("G1 7 iron path: against its corridor",
              path.value(QStringLiteral("basisText")).toString().contains(QLatin1String("of corridor")));
        check("G1 7 iron attack and face to path: graded against their corridors",
              varOf(iron, "lm.attackAngle").value(QStringLiteral("basisText")).toString().contains(QLatin1String("of corridor"))
                  && varOf(iron, "lm.faceToPath").value(QStringLiteral("basisText")).toString().contains(QLatin1String("of corridor")));
        const QVariantMap speed = varOf(iron, "lm.clubheadSpeed");
        check(QStringLiteral("G2 club speed (a one-sided norm): graded against its (σLo + σHi)/2, R12 (\"%1\")").arg(speed.value(QStringLiteral("basisText")).toString()),
              speed.value(QStringLiteral("graded")).toBool()
                  && speed.value(QStringLiteral("basisText")).toString().contains(QLatin1String("of corridor"))
                  && speed.value(QStringLiteral("basisText")).toString().contains(QLatin1String("mph")));
        check("G3 face, lie, strike height (Interval, no corridor): ungraded, noScale",
              !varOf(iron, "lm.faceAngle").value(QStringLiteral("graded")).toBool()
                  && varOf(iron, "lm.faceAngle").value(QStringLiteral("ungradedReason")).toString() == QLatin1String("noScale")
                  && !varOf(iron, "lm.lieAngle").value(QStringLiteral("graded")).toBool()
                  && !varOf(iron, "lm.strikeHeight").value(QStringLiteral("graded")).toBool());
        check("G3 strike location (H): graded against its corridor",
              varOf(iron, "lm.strikeLocation").value(QStringLiteral("basisText")).toString().contains(QLatin1String("of corridor")));
        check("G4 smash factor: no grade at all (an outcome, R4b)", !hasVar(iron, "lm.smashFactor"));
        check("G4 carry and ball speed: no grade at all (ball flight, R4)",
              !hasVar(iron, "lm.carryDistance") && !hasVar(iron, "lm.ballSpeed") && !hasVar(drv, "lm.carryDistance"));
        const QVariantMap dpath = varOf(drv, "lm.clubPath");
        check(QStringLiteral("G5 driver path: its own 5 shots, never pooled with the irons (grade %1, n %2, %3)")
                  .arg(dpath.value(QStringLiteral("grade")).toInt()).arg(dpath.value(QStringLiteral("n")).toInt())
                  .arg(dpath.value(QStringLiteral("ungradedReason")).toString()),
              dpath.value(QStringLiteral("graded")).toBool() && dpath.value(QStringLiteral("grade")).toInt() == 0
                  && dpath.value(QStringLiteral("n")).toInt() == 5
                  && dpath.value(QStringLiteral("ungradedReason")).toString() == QLatin1String("tooFewSwings"));
        check("G6 the focused shot changes no club's grade",
              [&] { board.setFocusedShotId(idOf(shots, 14));
                    return varOf(clubStats(board, QStringLiteral("7 IRON")), "lm.clubPath") == path; }());

        // The scale rule on its own (R12): (σLo + σHi)/2 whatever the norm's shape; no σ, no scale.
        check("G7 a norm's (σLo + σHi)/2", pinpoint::variability::corridorHalfWidthOf(1.0, 2.0) == 1.5);
        check("G7 a floor's or a ceiling's σ grade it too (R12)", pinpoint::variability::corridorHalfWidthOf(0.06, 0.06) == 0.06);
        check("G7 a zero σ: no scale", std::isnan(pinpoint::variability::corridorHalfWidthOf(0.0, 0.0)));
        check("G8 the outcome rule: path, lie, strike height, spin loft in; smash, carry, offline out",
              lm::lmIsClubDelivery(QStringLiteral("lm.clubPath")) && lm::lmIsClubDelivery(QStringLiteral("lm.lieAngle"))
                  && lm::lmIsClubDelivery(QStringLiteral("lm.strikeHeight"))
                  && lm::lmIsClubDelivery(QStringLiteral("lm.spinLoft"))
                  && !lm::lmIsClubDelivery(QStringLiteral("lm.smashFactor"))
                  && !lm::lmIsClubDelivery(QStringLiteral("lm.carryDistance"))
                  && !lm::lmIsClubDelivery(QStringLiteral("lm.offline")));
    }
    {
        std::printf("\nH — typicality: this shot against the OTHER shots of its club (R8)\n");
        // 9 7 irons: shots 1-8 at club speeds 78..82 (median 80), shot 9 at 100; then a driver. With shot 9 in its own
        // window the mean would read 82.2; left out, the median is exactly 80. Path 3.5° left at the median, shot 9 1.2° left.
        auto session = [](ShotListModel &shots, int leftOut) {
            const double others[] = { 78, 79, 80, 81, 82, 79, 80, 81 };
            const double paths[]  = { -3.0, -4.0, -3.5, -3.0, -4.0, -3.5, -3.5, -3.5 };
            int ord = 0;
            for (int i = 0; i < 8; ++i)
                addShot(shots, QStringLiteral("7 IRON"), QVariantMap{ { QStringLiteral("lm.clubheadSpeed"), others[i] },
                                                                    { QStringLiteral("lm.clubPath"), paths[i] },
                                                                    { QStringLiteral("lm.ballSpeed"), others[i] * 1.35 },
                                                                    { QStringLiteral("lm.carryDistance"), 150.0 + others[i] - 80.0 } },
                        ++ord == leftOut);
            addShot(shots, QStringLiteral("7 IRON"), QVariantMap{ { QStringLiteral("lm.clubheadSpeed"), 100.0 },
                                                                 { QStringLiteral("lm.clubPath"), -1.2 },
                                                                 { QStringLiteral("lm.ballSpeed"), 130.0 },
                                                                 { QStringLiteral("lm.carryDistance"), 150.0 } });
            addShot(shots, QStringLiteral("DRIVER"), QVariantMap{ { QStringLiteral("lm.clubheadSpeed"), 110.0 } });
        };
        {
            ShotListModel shots;
            session(shots, 0);
            LmSessionModel board;
            board.setReviewing(true);
            board.setShotModel(&shots);
            board.setFocusedShotId(idOf(shots, 9));
            const QVariantMap t = typOf(board, "lm.clubheadSpeed");
            const QString basis = t.value(QStringLiteral("basisText")).toString();
            checkStr("H1 8 others: a result, the median without this shot (R22)", basis,
                     QStringLiteral("this swing 100.0 mph · median 80.0 mph"));
            checkStr("H1 the band", t.value(QStringLiteral("values")).toMap().value(QStringLiteral("band")).toString(),
                     QStringLiteral("unusual"));
            checkStr("H1 a directional reading's basis reads as the board does: size and side, no sign",
                     typOf(board, "lm.clubPath").value(QStringLiteral("basisText")).toString(),
                     QStringLiteral("this swing 1.2° L · median 3.5° L"));
            check("H1 ball flight has no typicality at all (R4): no ball speed, no carry",
                  typOf(board, "lm.ballSpeed").isEmpty() && typOf(board, "lm.carryDistance").isEmpty());
            board.setFocusedShotId(idOf(shots, 10));
            check("H2 the driver (no other drivers): no result", typOf(board, "lm.clubheadSpeed").isEmpty());
            board.setFocusedShotId(-1);
            check("H3 nothing picked: the headline is the latest shot, the lone driver: no result",
                  typOf(board, "lm.clubheadSpeed").isEmpty());
        }
        {
            // One other 7 iron left out: 7 others, under N_MIN, and the driver does not make up the count.
            ShotListModel shots;
            session(shots, 3);
            LmSessionModel board;
            board.setReviewing(true);
            board.setShotModel(&shots);
            board.setFocusedShotId(idOf(shots, 9));
            check("H4 7 same-club others (one left out, a driver beside): no result",
                  typOf(board, "lm.clubheadSpeed").isEmpty()
                      && board.graphics().value(QStringLiteral("typicality")).toMap().isEmpty());
        }
    }

    std::printf("\n%d checks, %d failed\n", g_run, g_fail);
    return g_fail == 0 ? 0 : 1;
}
