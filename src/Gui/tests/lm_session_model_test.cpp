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
// Rows are a view of an in-memory session (fake_shot_source.h: the carousel is a view of a Session since refactor
// step 5.2), and each row's readings come from the board's detail reader, so nothing is read from disk.
//
// And the variability marks the panel shows (docs/design/system/variability.md): G, club delivery's
// grade per club (R4b) — a two-sided corridor's half-width, the CV for club speed, ungraded without a
// scale, never smash factor or ball flight (R4), never across clubs; H, typicality — this shot against
// the session's other shots of its club, this one left out, at least N_MIN of them (R8).
//
// And N, a saved corridor edit (audit B-01, Stage 0.4b): the board re-takes the shared norm set at the
// top of its next grade pass once the save's reset has moved its generation; with no edit, or before
// the reset, it grades exactly as it did.
//
// And T, the board THROUGH THE SHOT (refactor 6.1a fix round; review EDGE-2, EDGE-3): no detail reader, a real
// Session's Shot over a document on disk (the carousel's port, SessionShotSource), its late readings through the
// Shot's own command (EVT-22): the board takes the reading from the Shot's set, its low point our own until the
// monitor reads one, then the monitor's.

#include "launchmonitor/lm_session_model.h"
#include "../../Export/rule_data_install.h"
#include "../../DomainModel/rule_data.h"   // the packs and norms: RuleData's snapshot (refactor step 3.2)
#include "variability/variability_measures.h"
#include "shot/shot_list_model.h"
#include "shot/qml_payload.h"
#include "fake_shot_source.h"
#include "Vocabulary/Analysis/swing_analysis.h"
#include "norms/norm_provider.h"
#include "norms/context_tree.h"
#include "norms/metric_corridor.h"
#include "packs/pack_provider.h"
#include "packs/characteristic_pack.h"      // measuresForMetricAtPhase (P, refactor 5.7e)
#include "measures/measure_vocabulary.h"   // measureKeyLadder
#include "../../App/session_shot_source.h"         // T: the carousel's port over a real Session
#include "../../DomainModel/athlete.h"
#include "../../DomainModel/session.h"
#include "../../DomainModel/shot.h"
#include "../../Export/athlete_store.h"
#include "../../Export/session_store.h"
#include "../../Export/swing_store_repository.h"
#include "../../LaunchMonitor/launch_monitor_reading.h"
#include "../../Core/figure_format.h"   // T5: the app's one ±

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <cstdio>
#include <functional>
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
// THE TEST'S SESSION: each carousel here is a view of an in-memory session (its port: MemoryShotSource), and each
// shot's readings are its "document" — the board reads them through its detail reader (g_details, by folder).
static QHash<QString, QVariantMap> g_details;
static QHash<ShotListModel *, pinpoint::testdoc::MemoryShotSource *> g_sources;
static pinpoint::testdoc::MemoryShotSource *sessionOf(ShotListModel &m)
{
    if (pinpoint::testdoc::MemoryShotSource *s = g_sources.value(&m)) return s;
    auto owned = std::make_unique<pinpoint::testdoc::MemoryShotSource>(QStringLiteral("/nonexistent"));
    pinpoint::testdoc::MemoryShotSource *s = owned.get();
    m.setSource(std::move(owned));
    g_sources.insert(&m, s);
    QObject::connect(&m, &QObject::destroyed, [&m] { g_sources.remove(&m); });
    return s;
}
static QVariantMap detailOf(const QString &dir) { return g_details.value(dir); }
// One shot of the session lands (its document: EVT-01), with its readings.
static void land(ShotListModel &m, int ord, const QString &time, const QString &club, const QVariantMap &detail,
                 bool measured, bool leftOut, bool legacyStub = false)
{
    pinpoint::testdoc::MemoryShotSource *src = sessionOf(m);
    const pinpoint::ShotRef ref = src->allocate(ord);
    g_details.insert(src->swingFolder(ref), detail);
    pinpoint::ShotSummary s;
    s.ordinal = ord;
    s.timestampLabel = time;
    s.club = club;
    s.lmDevice = measured ? QStringLiteral("gcquad") : QString();
    s.lmExcluded = leftOut;
    s.legacyClubStub = legacyStub;   // as the summary read derives it (swingDocIsLegacyClubStub; J-106)
    src->land(ref, s);
}

static void fill(ShotListModel &m, int n, std::initializer_list<int> measured, int driverFrom = 0,
                 std::initializer_list<int> leftOut = {})
{
    for (int ord = 1; ord <= n; ++ord) {
        bool lm = false, out = false;
        for (int x : measured) lm = lm || x == ord;
        for (int x : leftOut) out = out || x == ord;
        const QString club = driverFrom > 0 && ord >= driverFrom ? QStringLiteral("DRIVER") : QStringLiteral("7 IRON");
        land(m, ord, QStringLiteral("10:%1").arg(ord, 2, 10, QLatin1Char('0')), club, lmDetail(lm), lm, out);
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
static void addShot(ShotListModel &m, const QString &club, const QVariantMap &values, bool leftOut = false,
                    bool legacyStub = false)
{
    const int ord = m.rowCount() + 1;
    land(m, ord, QStringLiteral("11:%1").arg(ord, 2, 10, QLatin1Char('0')), club, lmDetailOf(values), true, leftOut,
         legacyStub);
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

#ifndef PP_LIVE_SWINGS_DIR
#  define PP_LIVE_SWINGS_DIR "."
#endif

// Spin the event loop until `cond` holds (the board's queued rebuild, the Shot's events), at most `ms`.
static bool settle(const std::function<bool()> &cond, int ms = 5000)
{
    QElapsedTimer t;
    t.start();
    while (!cond() && t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return cond();
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    // T's athlete store reads and writes ppSettings() (IniFormat/UserScope): a scratch folder, never the live INI.
    QTemporaryDir cfg;
    if (!cfg.isValid()) return 1;
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, cfg.path());
    pinpoint::installProcessRuleData();   // this process's packs and norms (refactor step 3.2)
    // NO QML engine, on purpose (#173): the board reads each row's slice in C++ (its detail reader)
    // and never touches the engine; reading a detail through QML is what froze the app for minutes
    // on entering Analyse.
    QmlPayload::setEngine(nullptr);
    std::printf("lm_session_model_test — the launch monitor board's empty lines (#57)\n");

    {
        std::printf("\nA — shots 2 and 3 read, shot 1 not\n");
        ShotListModel shots;
        fill(shots, 3, { 2, 3 });
        LmSessionModel board;
        board.setDetailReader(&detailOf);
        board.setReviewing(true);
        board.setShotModel(&shots);

        board.setFocusedShotId(idOf(shots, 2));
        checkStr("A1 picked shot with a reading: no session line", board.emptyText(), QString());
        checkStr("A1 picked shot with a reading: no shot line", board.shotEmptyText(), QString());
        check("A1 the shot and the session both have readings", has(board) && sessionHas(board));

        // The typed bands (refactor 6.1b, lint A6): one per band with a reading, their counts the band counts, every
        // tile one reading of the field list, and a tile's formatted latest the annotation's (one formatter, two views).
        {
            const QList<lm::LmBand> bands = board.bands();
            const QList<int> counts = board.bandCounts();
            bool shaped = !bands.isEmpty() && counts.size() == bands.size();
            const QVariantMap vals = board.graphics().value(QStringLiteral("values")).toMap();
            for (int i = 0; shaped && i < bands.size(); ++i) {
                shaped = counts.at(i) == bands.at(i).count() && bands.at(i).count() == int(bands.at(i).tiles.size())
                         && !bands.at(i).band.isEmpty();
                for (const lm::LmTile &t : bands.at(i).tiles)
                    shaped = shaped && t.group == bands.at(i).band
                             && t.latest == vals.value(t.key).toMap().value(QStringLiteral("text")).toString();
            }
            check(QStringLiteral("A1b the typed bands: %1 bands, counts as published, each tile's latest its annotation's text")
                      .arg(bands.size()), shaped);
        }

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
        board.setDetailReader(&detailOf);
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
        board.setDetailReader(&detailOf);
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
        board.setDetailReader(&detailOf);
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
        board.setDetailReader(&detailOf);
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
        board.setDetailReader(&detailOf);
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
        board.setDetailReader(&detailOf);
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
        board.setDetailReader(&detailOf);
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
        board.setDetailReader(&detailOf);
            board.setReviewing(true);
            board.setShotModel(&shots);
            board.setFocusedShotId(idOf(shots, 9));
            check("H4 7 same-club others (one left out, a driver beside): no result",
                  typOf(board, "lm.clubheadSpeed").isEmpty()
                      && board.graphics().value(QStringLiteral("typicality")).toMap().isEmpty());
        }
    }

    {
        std::printf("\nN — a saved corridor edit reaches the board's next grade pass (audit B-01)\n");
        // The user norm set lives under AppDataLocation: a folder of this run's own — test mode, and the
        // application named after a fresh QTemporaryDir in the test-mode data location, so AppDataLocation
        // IS that temporary directory — so no real profile and no other run meets it.
        QStandardPaths::setTestModeEnabled(true);
        const QString dataBase = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
        QDir().mkpath(dataBase);
        QTemporaryDir runDir(dataBase + QStringLiteral("/lm_session_model_test-XXXXXX"));
        QCoreApplication::setOrganizationName(QString());
        QCoreApplication::setApplicationName(QFileInfo(runDir.path()).fileName());
        pinpoint::installProcessRuleData().reload();   // the data location just moved (step 3.2)
        check("N0 the user norm set goes in this run's own temporary directory",
              runDir.isValid()
                  && QFileInfo(analysis::userNormPath()).absolutePath().startsWith(QFileInfo(runDir.path()).absoluteFilePath()));
        analysis::resetSharedNormProvider();

        ShotListModel shots;
        for (int i = 0; i < 11; ++i)
            addShot(shots, QStringLiteral("7 IRON"), QVariantMap{ { QStringLiteral("lm.clubPath"), -1.0 + 0.2 * (i % 5) } });
        LmSessionModel board;
        board.setDetailReader(&detailOf);
        board.setReviewing(true);
        board.setShotModel(&shots);
        const QVariantMap path0 = varOf(clubStats(board, QStringLiteral("7 IRON")), "lm.clubPath");
        check(QStringLiteral("N1 7 iron path graded against its corridor (\"%1\")").arg(path0.value(QStringLiteral("basisText")).toString()),
              path0.value(QStringLiteral("graded")).toBool()
                  && path0.value(QStringLiteral("basisText")).toString().contains(QLatin1String("of corridor")));
        board.setFocusedShotId(idOf(shots, 3));                    // a grade pass, no edit
        check("N2 with no edit, the next grade pass grades the same",
              varOf(clubStats(board, QStringLiteral("7 IRON")), "lm.clubPath") == path0);

        // The corridor three times as wide, written as ModelBrowser::save() writes it — on the measure the
        // board's corridor resolves through (corridorForMetricAtPhase, the board's own join).
        const std::shared_ptr<const analysis::INormProvider> shipped = analysis::sharedNormProvider();
        const QString ctx      = analysis::contextIdForClub(QStringLiteral("7 IRON"));
        const auto    packProv = analysis::makeCharacteristicPackProvider();
        const std::optional<analysis::MetricCorridor> corridor =
            analysis::corridorForMetricAtPhase(packProv->pack(), *shipped, QStringLiteral("lm.clubPath"),
                                               analysis::Phase::Impact, ctx);
        const analysis::NormResolution res =
            corridor ? shipped->resolve(corridor->measureId, ctx) : analysis::NormResolution{};
        check(QStringLiteral("N3 the path corridor resolves to a shipped row (%1)").arg(corridor ? corridor->measureId : QString()),
              res.found() && !res.overridden);
        if (res.found()) {
            analysis::NormPack pack;
            pack.id      = QStringLiteral("user");
            pack.version = QStringLiteral("1");
            analysis::Norm wider = *res.norm;
            wider.sigmaLo *= 3.0;
            wider.sigmaHi *= 3.0;
            pack.norms.push_back(wider);
            QString why;
            check("N3 a user norm set widening it is written", analysis::saveUserNormPack(pack, &why));
            // The save is RuleData's command (refactor step 3.2): it publishes the new norms and resets the
            // shared slot itself, so the old "N4: before the reset the board grades as it did" state is gone.
            // The reset below is what ModelBrowser::save() still calls; it re-asks the same snapshot.
            analysis::resetSharedNormProvider();
            board.setFocusedShotId(idOf(shots, 5));                // the next grade pass
            const QVariantMap path1 = varOf(clubStats(board, QStringLiteral("7 IRON")), "lm.clubPath");
            check(QStringLiteral("N5 after the reset, the next grade pass grades against the edit (\"%1\" -> \"%2\")")
                      .arg(path0.value(QStringLiteral("basisText")).toString(), path1.value(QStringLiteral("basisText")).toString()),
                  path1.value(QStringLiteral("graded")).toBool()
                      && path1.value(QStringLiteral("basisText")).toString() != path0.value(QStringLiteral("basisText")).toString());
            board.setFocusedShotId(idOf(shots, 6));
            check("N6 and with no further edit, the pass after grades the same again",
                  varOf(clubStats(board, QStringLiteral("7 IRON")), "lm.clubPath") == path1);
        }
        QFile::remove(analysis::userNormPath());
        analysis::resetSharedNormProvider();
    }

    {
        std::printf("\nP — a saved pack edit (EVT-13) reaches a board made before it, at once (refactor 5.7e)\n");
        // The user pack lives under AppDataLocation: this run's own temporary directory, as in N.
        QStandardPaths::setTestModeEnabled(true);
        const QString dataBase = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
        QDir().mkpath(dataBase);
        QTemporaryDir runDir(dataBase + QStringLiteral("/lm_session_model_test-XXXXXX"));
        QCoreApplication::setOrganizationName(QString());
        QCoreApplication::setApplicationName(QFileInfo(runDir.path()).fileName());
        RuleData &rules = pinpoint::installProcessRuleData();
        rules.reload();   // the data location just moved
        check("P0 the user pack goes in this run's own temporary directory",
              runDir.isValid()
                  && QFileInfo(analysis::userPackPath()).absolutePath().startsWith(QFileInfo(runDir.path()).absoluteFilePath()));

        ShotListModel shots;
        for (int i = 0; i < 11; ++i)
            addShot(shots, QStringLiteral("7 IRON"), QVariantMap{ { QStringLiteral("lm.clubPath"), -1.0 + 0.2 * (i % 5) } });
        LmSessionModel board;
        board.setDetailReader(&detailOf);
        board.setReviewing(true);
        board.setShotModel(&shots);
        const QVariantMap path0 = varOf(clubStats(board, QStringLiteral("7 IRON")), "lm.clubPath");
        check(QStringLiteral("P1 7 iron path graded against its corridor (\"%1\")").arg(path0.value(QStringLiteral("basisText")).toString()),
              path0.value(QStringLiteral("graded")).toBool()
                  && path0.value(QStringLiteral("basisText")).toString().contains(QLatin1String("of corridor")));

        // The edit: no measure reads the device's club path any more (each one that did, replaced by id in the user
        // layer), so the board has no corridor to grade it against.
        analysis::CharacteristicPack user;
        user.id      = QStringLiteral("user");
        user.version = QStringLiteral("1");
        for (const analysis::Measure &m : rules.snapshot()->pack->pack().measures) {
            if (!analysis::measureKeyLadder(m).contains(QStringLiteral("lm.clubPath"))) continue;
            analysis::Measure c = m;
            c.preferKeys.removeAll(QStringLiteral("lm.clubPath"));
            if (c.metricKey == QStringLiteral("lm.clubPath")) c.metricKey = QStringLiteral("clubPath");
            user.measures.push_back(c);
        }
        int resets = 0;
        QObject::connect(&board, &LmSessionModel::graphicsChanged, [&resets] { ++resets; });   // each reduction publishes
        QString why;
        check(QStringLiteral("P2 the edit (%1 measures) is saved through RuleData's command").arg(int(user.measures.size())),
              !user.measures.empty() && rules.saveUserPack(user, &why));
        check("P3 …and the new snapshot has no measure reading lm.clubPath at impact",
              analysis::measuresForMetricAtPhase(rules.snapshot()->pack->pack(), QStringLiteral("lm.clubPath"),
                                                 analysis::Phase::Impact).empty());
        // No grade pass asked: the event alone.
        const QVariantMap path1 = varOf(clubStats(board, QStringLiteral("7 IRON")), "lm.clubPath");
        check(QStringLiteral("P4 the board graded again on the event, against the new pack (\"%1\" -> \"%2\")")
                  .arg(path0.value(QStringLiteral("basisText")).toString(), path1.value(QStringLiteral("basisText")).toString()),
              resets >= 1 && !path1.value(QStringLiteral("basisText")).toString().contains(QLatin1String("of corridor")));
        QFile::remove(analysis::userPackPath());
        rules.reload();
    }

    {
        std::printf("\nT — the board through the Shot: a real Session's Shot, its document, its late readings\n");
        using pinpoint::analysis::Instrument;
        QTemporaryDir dir;
        pinpoint::SessionStore store;
        store.setLockWaitForTest(200);
        store.setOsTrashForTest(false);   // never the OS trash
        pinpoint::SwingStoreRecordingRepository recordings(store);
        const QString lib = dir.path() + QStringLiteral("/lib");
        QDir().mkpath(lib);
        pinpoint::AthleteStore athletes([lib] { return lib; }, &store);
        athletes.setOsTrashForTest(false);
        athletes.setLockWaitForTest(200);
        athletes.setShotDoors({ &recordings, nullptr });
        pinpoint::AthleteRecord golfer;
        golfer.name = QStringLiteral("Board Golfer");
        pinpoint::Athlete *athlete = athletes.create(golfer).athlete;
        pinpoint::Session *session =
            athlete ? athlete->startSession(QStringLiteral("swing"), QStringLiteral("date-name-type")) : nullptr;
        pinpoint::SwingAllocation a;
        pinpoint::Shot *shot = session ? session->addShot(pinpoint::recording::ShotKind::Captured, &a) : nullptr;
        // A camera shot's document (the corpus fixture): our own low point, no reading.
        const bool staged =
            shot && QFile::copy(QDir(QLatin1String(PP_LIVE_SWINGS_DIR)).filePath(QStringLiteral("rich_7iron/swing.json")),
                                QDir(a.swingFolder).filePath(QStringLiteral("swing.json")));
        check("T0 a session and a camera shot with its document on disk", staged);
        if (staged) {
            session->shotDocumentWritten(shot->ref());   // EVT-01: the carousel's row
            ShotListModel shots;
            shots.setSource(pinpoint::shotSourceOf(session));
            LmSessionModel board;                        // NO detail reader: each row's Shot
            board.setReviewing(true);
            board.setShotModel(&shots);
            const int id = shots.idOfShotRef(shot->ref());
            board.setFocusedShotId(id);
            const auto lowPoint = [&board] { return board.graphics().value(QStringLiteral("lowPoint")).toMap(); };
            const auto value = [&board](const char *key) {
                return board.graphics().value(QStringLiteral("values")).toMap().value(QString::fromLatin1(key)).toMap()
                    .value(QStringLiteral("value"), -1.0).toDouble();
            };
            settle([&] { return shots.rowCount() == 1; });
            check("T1 the row is the Shot's", shots.rowCount() == 1 && id >= 0);
            // The session's only shot: the session line, not the shot's (emptyText before shotEmptyText, #57).
            checkStr("T1 before any reading: the session has none", board.emptyText(),
                     QStringLiteral("No launch monitor readings in this session"));
            const pinpoint::analysis::Measurement *own =
                shot->measurements().find(QStringLiteral("lowPointAhead"), Instrument::Own);
            check("T1 the Shot's set holds our own low point at impact", own && own->value);
            const double ownLowPoint = own && own->value ? *own->value : 99.0;

            // A late reading, through the Shot's own command (EVT-22): the board reads it from the Shot's set.
            pinpoint::lm::LaunchMonitorReading r;
            r.deviceShotId  = QStringLiteral("t2");
            r.deviceKind    = QStringLiteral("fsx2020");
            r.clubheadSpeed = 80.0;
            r.ballSpeed     = 112.0;
            r.launchAngle   = 17.0;
            check("T2 a late reading through the Shot is saved", shot->completeCapture(r).ok());
            settle([&] { return value("lm.clubheadSpeed") == 80.0; });
            check(QStringLiteral("T2 …and the board shows it, from the Shot's set (club speed %1)").arg(value("lm.clubheadSpeed")),
                  value("lm.clubheadSpeed") == 80.0 && value("lm.ballSpeed") == 112.0 && board.shotEmptyText().isEmpty());
            check(QStringLiteral("T3 no low point from the monitor: the IMPACT card's is ours, badged inferred (%1)")
                      .arg(lowPoint().value(QStringLiteral("value")).toDouble()),
                  lowPoint().value(QStringLiteral("has")).toBool()
                      && lowPoint().value(QStringLiteral("source")).toString() == QStringLiteral("inferred")
                      && lowPoint().value(QStringLiteral("value")).toDouble() == ownLowPoint);

            // The monitor reads an attack angle and no low point (refactor 6.4, D-4): the pack's middle rung, the low point
            // derived from that angle, wins over our camera's, with the catalogue's label and its σ (no ⚠: no gate touches
            // it, as the findings read it) — the number the findings read (the low point measure's ladder).
            r.attackAngle = -3.0;
            check("T5 a reading with an attack angle and no low point through the Shot is saved", shot->completeCapture(r).ok());
            settle([&] { return lowPoint().value(QStringLiteral("source")).toString() == QStringLiteral("fromAttack"); });
            const pinpoint::analysis::Measurement *derived = shot->measurements().byKey(QStringLiteral("lowPointFromAttack"));
            check("T5 …the Shot's set holds the low point derived from it, with its σ",
                  derived && derived->value && derived->sigma && *derived->sigma > 0.0);
            const double derivedValue = derived && derived->value ? *derived->value : 99.0;
            check(QStringLiteral("T5 …and the IMPACT card's low point is it, not ours (%1 against our %2)")
                      .arg(lowPoint().value(QStringLiteral("value")).toDouble()).arg(ownLowPoint),
                  lowPoint().value(QStringLiteral("has")).toBool()
                      && lowPoint().value(QStringLiteral("source")).toString() == QStringLiteral("fromAttack")
                      && lowPoint().value(QStringLiteral("value")).toDouble() == derivedValue
                      && derivedValue != ownLowPoint);
            checkStr("T5 …named by the catalogue's label", lowPoint().value(QStringLiteral("label")).toString(),
                     QStringLiteral("Low point (from attack angle)"));
            check(QStringLiteral("T5 …with its σ as the app's one ± (%1), and no ⚠ (no gate touched it)")
                      .arg(lowPoint().value(QStringLiteral("sigmaText")).toString()),
                  derived && derived->sigma && lowPoint().value(QStringLiteral("sigma")).toDouble() == *derived->sigma
                      && lowPoint().value(QStringLiteral("sigmaText")).toString() == pinpoint::figures::pm(*derived->sigma, 1)
                      && !lowPoint().contains(QStringLiteral("estimated")));
            const QVariantMap focused = board.graphics();
            check("T5 …the device's reading of the attack angle stays a reading, apart from the low point",
                  focused.value(QStringLiteral("values")).toMap().contains(QStringLiteral("lm.attackAngle"))
                      && !focused.value(QStringLiteral("values")).toMap().contains(QStringLiteral("lowPointFromAttack")));

            // The monitor reads a low point too (the device-wins branch, at board level): the rule takes the monitor's,
            // over the one derived from its attack angle.
            r.lowPointAhead = 1.5;
            check("T4 a fuller reading through the Shot is saved", shot->completeCapture(r).ok());
            settle([&] { return lowPoint().value(QStringLiteral("source")).toString() == QStringLiteral("measured"); });
            check(QStringLiteral("T4 …and the IMPACT card's low point is the monitor's, badged measured (%1)")
                      .arg(lowPoint().value(QStringLiteral("value")).toDouble()),
                  lowPoint().value(QStringLiteral("source")).toString() == QStringLiteral("measured")
                      && lowPoint().value(QStringLiteral("value")).toDouble() == 1.5);
        }
    }

    {
        // THE ONE CLUB (refactor 6.2; R-CLUB-2, R-CLUB-4; audit G-07): the board has no club rule of its own. A shot the
        // golfer corrected counts under the correction; a shot whose readings contradict its club is FLAGGED, never
        // relabelled (the evidence is not one of the shot's club facts); an undeclared shot is undeclared, never a driver.
        std::printf("\nK — the one club: a corrected shot, evidence that only flags, an undeclared shot\n");
        ShotListModel shots;
        const QVariantMap iron{ { QStringLiteral("lm.dynamicLoft"), 35.0 }, { QStringLiteral("lm.smashFactor"), 1.30 },
                                { QStringLiteral("lm.spinRate"), 7500.0 }, { QStringLiteral("lm.ballSpeed"), 110.0 } };
        addShot(shots, QStringLiteral("7 IRON"), iron);   // 1
        addShot(shots, QStringLiteral("7 IRON"), iron);   // 2: corrected on the board below
        addShot(shots, QStringLiteral("DRIVER"), iron);   // 3: recorded as a driver, reads like the session's iron
        addShot(shots, QString(), iron);                  // 4: undeclared
        LmSessionModel board;
        board.setDetailReader(&detailOf);
        board.setReviewing(true);
        board.setShotModel(&shots);
        shots.setLmClubOverride(idOf(shots, 2), QStringLiteral("8 IRON"));
        const auto rowOf = [&shots](int ord) {
            for (int r = 0; r < shots.rowCount(); ++r)
                if (shots.data(shots.index(r), ShotListModel::OrdinalRole).toInt() == ord) return shots.index(r);
            return QModelIndex();
        };
        const auto shotOf = [&board](int ord) {
            for (const QVariant &v : board.graphics().value(QStringLiteral("shots")).toList())
                if (v.toMap().value(QStringLiteral("number")).toInt() == ord) return v.toMap();
            return QVariantMap();
        };
        check("K1 the shot list's club is the one club: the correction; its recorded club the uncorrected one",
              shots.data(rowOf(2), ShotListModel::ClubRole).toString() == QLatin1String("8 IRON")
                  && shots.data(rowOf(2), ShotListModel::RecordedClubRole).toString() == QLatin1String("7 IRON")
                  && shots.shotSummary(idOf(shots, 2)).value(QStringLiteral("club")).toString() == QLatin1String("8 IRON"));
        const QVariantMap s2 = shotOf(2);
        check("K1 …and the board counts it under the correction, recorded as the 7 iron",
              s2.value(QStringLiteral("club")).toString() == QLatin1String("8 IRON")
                  && s2.value(QStringLiteral("recordedClub")).toString() == QLatin1String("7 IRON")
                  && s2.value(QStringLiteral("clubSource")).toString() == QLatin1String("user"));
        const QVariantMap s3 = shotOf(3);
        const QVariantMap mm = board.graphics().value(QStringLiteral("mismatch")).toMap();
        check(QStringLiteral("K2 the evidence only flags: shot 3 stays a driver (suggests \"%1\"), the note relabels nothing")
                  .arg(s3.value(QStringLiteral("evidence")).toMap().value(QStringLiteral("suggest")).toString()),
              s3.value(QStringLiteral("club")).toString() == QLatin1String("DRIVER")
                  && s3.value(QStringLiteral("clubSource")).toString() == QLatin1String("recorded")
                  && s3.value(QStringLiteral("evidence")).toMap().value(QStringLiteral("suggest")).toString() == QLatin1String("7 IRON")
                  && mm.value(QStringLiteral("count")).toInt() == 1 && !mm.value(QStringLiteral("relabelled")).toBool());
        check("K3 an undeclared shot is undeclared on the list and the board, never a driver",
              shots.data(rowOf(4), ShotListModel::ClubRole).toString().isEmpty()
                  && shotOf(4).value(QStringLiteral("club")).toString().isEmpty() && !shotOf(4).isEmpty());
        shots.setLmClubOverride(idOf(shots, 2), QString());
        check("K4 the correction cleared: the recorded club again, on the list and the board",
              shots.data(rowOf(2), ShotListModel::ClubRole).toString() == QLatin1String("7 IRON")
                  && shotOf(2).value(QStringLiteral("club")).toString() == QLatin1String("7 IRON"));
    }

    {
        // THE LEGACY STUB EXCEPTION (J-106, interim; club_vocabulary.h swingDocIsLegacyClubStub): a stored "DRIVER" stub
        // keeps the board's pre-6.2 behaviour, counted under the iron its readings point at, with Undo. Beside it, a shot
        // recorded as a driver (K2) stays a flagged driver and an undeclared shot (K3) stays undeclared.
        std::printf("\nK5 — a legacy DRIVER stub: the board's old evidence relabel, and its Undo\n");
        ShotListModel shots;
        const QVariantMap iron{ { QStringLiteral("lm.dynamicLoft"), 35.0 }, { QStringLiteral("lm.smashFactor"), 1.30 },
                                { QStringLiteral("lm.spinRate"), 7500.0 }, { QStringLiteral("lm.ballSpeed"), 110.0 } };
        addShot(shots, QStringLiteral("7 IRON"), iron);                           // 1
        addShot(shots, QStringLiteral("7 IRON"), iron);                           // 2
        addShot(shots, QStringLiteral("DRIVER"), iron, false, /*legacyStub*/ true); // 3: the stub, hit with the iron
        addShot(shots, QString(), iron);                                          // 4: undeclared
        LmSessionModel board;
        board.setDetailReader(&detailOf);
        board.setReviewing(true);
        board.setShotModel(&shots);
        const auto shotOf = [&board](int ord) {
            for (const QVariant &v : board.graphics().value(QStringLiteral("shots")).toList())
                if (v.toMap().value(QStringLiteral("number")).toInt() == ord) return v.toMap();
            return QVariantMap();
        };
        const QVariantMap s3 = shotOf(3);
        const QVariantMap mm = board.graphics().value(QStringLiteral("mismatch")).toMap();
        check(QStringLiteral("K5 the stub counts under the iron its readings suggest (club %1, source %2)")
                  .arg(s3.value(QStringLiteral("club")).toString(), s3.value(QStringLiteral("clubSource")).toString()),
              s3.value(QStringLiteral("club")).toString() == QLatin1String("7 IRON")
                  && s3.value(QStringLiteral("clubSource")).toString() == QLatin1String("evidence")
                  && s3.value(QStringLiteral("recordedClub")).toString() == QLatin1String("DRIVER"));
        check("K5 …the note says it relabelled the one shot, so it offers Undo",
              mm.value(QStringLiteral("relabelled")).toBool() && mm.value(QStringLiteral("count")).toInt() == 1
                  && mm.value(QStringLiteral("recorded")).toString() == QLatin1String("DRIVER")
                  && mm.value(QStringLiteral("suggested")).toString() == QLatin1String("7 IRON"));
        check("K5 …and the undeclared shot beside it stays undeclared",
              shotOf(4).value(QStringLiteral("club")).toString().isEmpty() && !shotOf(4).isEmpty());
        board.undoMismatch();
        const QVariantMap u3 = shotOf(3);
        check("K5 Undo stores the recorded club as the golfer's: a driver, the evidence declined",
              [&shots] { for (int r = 0; r < shots.rowCount(); ++r)
                                    if (shots.data(shots.index(r), ShotListModel::OrdinalRole).toInt() == 3)
                                        return shots.data(shots.index(r), ShotListModel::LmClubOverrideRole).toString();
                                return QString(); }() == QLatin1String("DRIVER")
                  && u3.value(QStringLiteral("club")).toString() == QLatin1String("DRIVER")
                  && u3.value(QStringLiteral("clubSource")).toString() == QLatin1String("recorded"));
    }

    std::printf("\n%d checks, %d failed\n", g_run, g_fail);
    return g_fail == 0 ? 0 : 1;
}
