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

// THE SESSION REPORT'S LAUNCH MONITOR FIGURES COME FROM ONE SOURCE (audit F-03; report/report_lm.h).
//
// A session of seven 7-iron swings, as the golfer left it on the launch monitor board:
//   swing 3 RE-LABELLED as an 8 iron (review.clubOverride), swing 6 LEFT OUT (review.excluded — a 230 yd misread),
//   swing 7 recorded as an 8 iron.
// The board's own model (LmSessionModel) runs over an in-memory shot list carrying exactly those two annotations, as the
// session loader hands them over. The report's figures are then built from LmSessionModel::boardShots() and its
// graphics, as SessionReportModel::build() builds them, and the test checks that:
//   - the cover's ball tiles, the numbers table's Typical row, the swing grid's carry row and the reference swing's
//     carries leave swing 6 out and count swing 3 under the 8 iron;
//   - those figures and the appendix agree: the same clubs, the same shots per club, the same lowest and highest carry;
//   - the tables still LIST the left-out swing ("out") and mark the re-labelled one ("*"), and the notes say so.
// Before F-03 the cover read the raw launchMonitor block of every swing under its ledger club: a 7 iron carry ranging
// 120–230 over six swings (the misread and the re-labelled 8 iron in it) and an 8 iron of one, beside an appendix of
// 4 + 2 shots.

#include "report/report_lm.h"
#include "report/report_words.h"
#include "launchmonitor/lm_session_model.h"
#include "shot/shot_list_model.h"
#include "shot/qml_payload.h"
#include "Analysis/swing_analysis.h"

#include <QCoreApplication>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <cmath>
#include <cstdio>

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

// One shot's readings as the writer leaves them: each lm.* a PointInTime sample at Impact.
static QVariantMap lmDetailOf(double carry, double ball, double club)
{
    QVariantList series;
    for (const auto &[key, v] : { std::pair{ "lm.carryDistance", carry }, std::pair{ "lm.ballSpeed", ball },
                                  std::pair{ "lm.clubheadSpeed", club }, std::pair{ "lm.smashFactor", ball / club },
                                  std::pair{ "lm.closureRate", 2250.0 } }) {
        const QVariantMap sample{ { QStringLiteral("phase"), int(analysis::Phase::Impact) }, { QStringLiteral("value"), v } };
        series.append(QVariantMap{ { QStringLiteral("key"), QString::fromLatin1(key) },
                                   { QStringLiteral("phaseSamples"), QVariantList{ sample } } });
    }
    return { { QStringLiteral("series"), series } };
}

static QString dirOf(int ord) { return QStringLiteral("/nonexistent/session/swing_%1").arg(ord, 4, 10, QLatin1Char('0')); }

// The report's carry tile of one ball group, by its club words ("7 iron"; "" for a single group).
static QVariantMap tileOf(const QVariant &ball, const QString &club, const QString &label)
{
    for (const QVariant &g : ball.toMap().value(QStringLiteral("groups")).toList()) {
        if (g.toMap().value(QStringLiteral("club")).toString() != club) continue;
        for (const QVariant &t : g.toMap().value(QStringLiteral("tiles")).toList())
            if (t.toMap().value(QStringLiteral("label")).toString() == label) return t.toMap();
    }
    return {};
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QmlPayload::setEngine(nullptr);   // the board reads the C++ map, never the QML role (#173)
    std::printf("report_lm_test — the report's launch monitor figures honour the board (F-03)\n");

    // ── the session, as the loader hands it over ──
    struct Shot { const char *club; double carry, ball, clubSpeed; bool out; const char *override_; };
    const Shot shots[] = {
        { "7 IRON", 140, 100, 78, false, "" },
        { "7 IRON", 150, 106, 80, false, "" },
        { "7 IRON", 120,  92, 76, false, "8 IRON" },   // swing 3: the golfer says it was an 8 iron
        { "7 IRON", 146, 104, 79, false, "" },
        { "7 IRON", 144, 103, 79, false, "" },
        { "7 IRON", 230, 104, 79, true,  "" },         // swing 6: a carry misread, left out
        { "8 IRON", 126,  95, 77, false, "" },
    };
    ShotListModel list;
    QStringList dirs, ledgerClubs;
    int ord = 0;
    for (const Shot &s : shots) {
        ++ord;
        list.addPersistedShot(dirOf(ord), ord, QStringLiteral("10:%1").arg(ord, 2, 10, QLatin1Char('0')), QString::fromLatin1(s.club),
                              false, QUrl(), 0, 0, QString(), {}, lmDetailOf(s.carry, s.ball, s.clubSpeed), false,
                              QStringLiteral("gcquad"), {}, s.out, QString::fromLatin1(s.override_));
        dirs << dirOf(ord);
        ledgerClubs << QString::fromLatin1(s.club);   // the diagnostics ledger's club ignores the board's override
    }
    LmSessionModel board;
    board.setReviewing(true);
    board.setDeviceName(QStringLiteral("GC Quad"));
    board.setShotModel(&list);

    std::printf("\nA — the board's shots, as the report reads them\n");
    const std::vector<lm::LmBoardShot> &bs = board.boardShots();
    check(QStringLiteral("A1 one board shot per row, oldest first"), bs.size() == 7 && bs.front().ordinal == 1 && bs.back().ordinal == 7);
    check(QStringLiteral("A2 swing 3 is under the 8 iron, by the golfer"), bs.size() == 7 && bs[2].club == QLatin1String("8 IRON")
                                                                             && bs[2].source == QLatin1String("user") && bs[2].relabelled());
    check(QStringLiteral("A3 swing 6 is left out: listed, not in the figures"), bs.size() == 7 && bs[5].excluded && !bs[5].inFigures());

    const report::LmFigures f = report::lmFiguresOf(bs, dirs, ledgerClubs, QStringLiteral("GC Quad"), false);
    check(QStringLiteral("A4 every swing joined to its board shot"), f.N() == 7 && std::all_of(f.swings.begin(), f.swings.end(),
                                                                                               [](const report::LmSwing &s) { return s.shot.has_value(); }));
    check(QStringLiteral("A5 seven measured, six in the figures, swing 6 left out"),
          f.measured == 7 && f.counted == 6 && f.leftOut == QList<int>{ 6 } && f.anyRelabelled);
    checkStr(QStringLiteral("A6 the clubs, effective, in bag order"), f.clubs.join(QLatin1Char(',')), QStringLiteral("7 IRON,8 IRON"));

    std::printf("\nB — the cover's ball tiles\n");
    const QVariant ball = report::lmBallBlock(f);
    const QVariantMap c7 = tileOf(ball, QStringLiteral("7 iron"), QStringLiteral("Carry"));
    const QVariantMap c8 = tileOf(ball, QStringLiteral("8 iron"), QStringLiteral("Carry"));
    checkStr(QStringLiteral("B1 7 iron carry: the middle of 140, 144, 146, 150"), c7.value(QStringLiteral("value")).toString(), QStringLiteral("145"));
    checkStr(QStringLiteral("B2 7 iron range: the misread is not in it"), c7.value(QStringLiteral("range")).toString(), QStringLiteral("140–150"));
    checkStr(QStringLiteral("B3 8 iron carry: swing 3 and swing 7"), c8.value(QStringLiteral("value")).toString(), QStringLiteral("123"));
    checkStr(QStringLiteral("B4 8 iron range"), c8.value(QStringLiteral("range")).toString(), QStringLiteral("120–126"));
    check(QStringLiteral("B5 the note says which swing is left out"),
          ball.toMap().value(QStringLiteral("note")).toString().contains(QLatin1String("Swing 6 is left out, as on the launch monitor panel.")));
    checkStr(QStringLiteral("B6 the byline counts every swing the monitor read"), ball.toMap().value(QStringLiteral("byline")).toString(),
             QStringLiteral("measured on every swing by GC Quad"));
    checkStr(QStringLiteral("B7 smash factor keeps its two decimals (8 iron: 92/76 and 95/77)"),
             tileOf(ball, QStringLiteral("8 iron"), QStringLiteral("Smash")).value(QStringLiteral("value")).toString(), QStringLiteral("1.22"));

    std::printf("\nC — the numbers table\n");
    const QVariantMap nums = report::lmNumbersBlock(f, true, {}, 0).toMap();
    const QStringList heads = nums.value(QStringLiteral("cols")).toStringList();
    checkStr(QStringLiteral("C1 heads are the board's (fieldDefs abbrev)"), heads.join(QLatin1Char('|')),
             QStringLiteral("Swing|Club|Carry|Ball speed|Club speed|Smash fac."));
    const QVariantList rows = nums.value(QStringLiteral("rows")).toList();
    auto cell = [&](int row, int col) { return rows.value(row).toMap().value(QStringLiteral("cells")).toStringList().value(col); };
    checkStr(QStringLiteral("C2 swing 3 marked re-labelled"), cell(2, 0), QStringLiteral("3*"));
    checkStr(QStringLiteral("C3 swing 3 under its effective club"), cell(2, 1), QStringLiteral("8 iron"));
    checkStr(QStringLiteral("C4 swing 6 still listed, marked out"), cell(5, 0), QStringLiteral("6 out"));
    checkStr(QStringLiteral("C5 swing 6's reading still shown"), cell(5, 2), QStringLiteral("230"));
    const QVariantList typ = nums.value(QStringLiteral("typical")).toList();
    auto typCell = [&](int row, int col) { return typ.value(row).toMap().value(QStringLiteral("cells")).toStringList().value(col); };
    check(QStringLiteral("C6 a Typical row per effective club"), typ.size() == 2 && typCell(0, 1) == QLatin1String("7 iron") && typCell(1, 1) == QLatin1String("8 iron"));
    checkStr(QStringLiteral("C7 7 iron Typical carry = the cover's"), typCell(0, 2), c7.value(QStringLiteral("value")).toString());
    checkStr(QStringLiteral("C8 8 iron Typical carry = the cover's"), typCell(1, 2), c8.value(QStringLiteral("value")).toString());
    const QString note = nums.value(QStringLiteral("note")).toString();
    check(QStringLiteral("C9 the note explains both marks"), note.contains(QLatin1String(" Out: left out on the launch monitor panel")) && note.contains(QLatin1String(" *: counted under")));

    std::printf("\nD — the swing grid's carry row and the reference swing\n");
    const QVariantList carry = report::lmGridCarry(f);
    const QStringList carryText = report::lmGridCarryText(f);
    check(QStringLiteral("D1 a carry slot per swing"), carry.size() == 7 && carryText.size() == 7);
    check(QStringLiteral("D2 swing 6 draws no bar"), carry.value(5).isNull() || !carry.value(5).isValid() || carry.value(5).metaType().id() == QMetaType::Nullptr);
    checkStr(QStringLiteral("D3 swing 6 says out"), carryText.value(5), QStringLiteral("out"));
    check(QStringLiteral("D4 swing 3 draws its carry"), carry.value(2).toDouble() == 120.0 && carryText.value(2) == QLatin1String("120"));
    const std::vector<double> refCarry = report::lmReferenceCarry(f);
    check(QStringLiteral("D5 the reference swing never sees the misread"), refCarry.size() == 7 && std::isnan(refCarry[5])
                                                                              && *std::max_element(refCarry.begin(), refCarry.begin() + 5) == 150.0);
    checkStr(QStringLiteral("D6 a left-out reference swing quotes no carry"), report::lmReferenceFigure(f, 5, QStringLiteral("lm.carryDistance")), QStringLiteral("-"));
    checkStr(QStringLiteral("D7 a counted one does"), report::lmReferenceFigure(f, 1, QStringLiteral("lm.carryDistance")), QStringLiteral("150"));

    std::printf("\nE — the appendix agrees with every page above\n");
    const QVariantMap g = board.graphics();
    const QVariantMap apx = report::lmAppendixBlock(f, g).toMap();
    QStringList apxClubs;
    bool countsAgree = true, extremesAgree = true;
    for (const QVariant &cv : g.value(QStringLiteral("clubs")).toList()) {
        const QVariantMap c = cv.toMap();
        const QString club = c.value(QStringLiteral("club")).toString();
        apxClubs << club;
        const QVariantMap st = c.value(QStringLiteral("stats")).toMap().value(QStringLiteral("lm.carryDistance")).toMap();
        const std::vector<double> mine = f.series(QStringLiteral("lm.carryDistance"), &club);
        countsAgree = countsAgree && st.value(QStringLiteral("n")).toInt() == int(mine.size());
        extremesAgree = extremesAgree && !mine.empty()
                     && st.value(QStringLiteral("min")).toDouble() == *std::min_element(mine.begin(), mine.end())
                     && st.value(QStringLiteral("max")).toDouble() == *std::max_element(mine.begin(), mine.end());
    }
    checkStr(QStringLiteral("E1 the appendix's clubs are the cover's"), apxClubs.join(QLatin1Char(',')), f.clubs.join(QLatin1Char(',')));
    check(QStringLiteral("E2 the same shots per club"), countsAgree);
    check(QStringLiteral("E3 the same shortest and longest carry per club"), extremesAgree);
    QStringList headlines;
    for (const QVariant &v : apx.value(QStringLiteral("figures")).toList()) headlines << v.toMap().value(QStringLiteral("headline")).toString();
    checkStr(QStringLiteral("E4 the appendix headlines"), headlines.join(QStringLiteral(" | ")),
             QStringLiteral("145 yd average carry, 10 yd from shortest to longest, 4 shots | 123 yd average carry, 6 yd from shortest to longest, 2 shots"));
    // the shot tables: swing 6 listed out under the 7 iron, swing 3 marked * under the 8 iron
    QStringList listed;
    for (const QVariant &gv : apx.value(QStringLiteral("ball")).toMap().value(QStringLiteral("groups")).toList())
        for (const QVariant &rv : gv.toMap().value(QStringLiteral("rows")).toList())
            listed << gv.toMap().value(QStringLiteral("club")).toString() + QLatin1Char(':') + rv.toMap().value(QStringLiteral("swing")).toString()
                          + (rv.toMap().value(QStringLiteral("out")).toBool() ? QStringLiteral(" out") : QString());
    checkStr(QStringLiteral("E5 the appendix lists every swing, marked as the numbers table marks it"), listed.join(QLatin1Char(',')),
             QStringLiteral("7 iron:1,7 iron:2,7 iron:4,7 iron:5,7 iron:6 out,8 iron:3*,8 iron:7"));

    // the appendix prints at the board's precision, a four-figure rate grouped (°/s, as rpm)
    QString closure;
    for (const QVariant &gv : apx.value(QStringLiteral("club")).toMap().value(QStringLiteral("groups")).toList()) {
        const QVariantList cols = apx.value(QStringLiteral("club")).toMap().value(QStringLiteral("cols")).toList();
        for (int i = 0; i < cols.size() && closure.isEmpty(); ++i)
            if (cols[i].toMap().value(QStringLiteral("key")).toString() == QLatin1String("lm.closureRate"))
                closure = gv.toMap().value(QStringLiteral("rows")).toList().value(0).toMap().value(QStringLiteral("cells")).toStringList().value(i);
    }
    checkStr(QStringLiteral("E6 the appendix groups a closure rate's thousands"), closure, QStringLiteral("2,250"));

    std::printf("\nF — precision and words\n");
    check(QStringLiteral("F1 golfer precision: whole yards, mph, launch; smash 2; club path 1"),
          report::lmGolferDecimals(QStringLiteral("lm.carryDistance")) == 0 && report::lmGolferDecimals(QStringLiteral("lm.ballSpeed")) == 0
              && report::lmGolferDecimals(QStringLiteral("lm.launchAngle")) == 0 && report::lmGolferDecimals(QStringLiteral("lm.smashFactor")) == 2
              && report::lmGolferDecimals(QStringLiteral("lm.clubPath")) == 1 && report::lmGolferDecimals(QStringLiteral("lm.spinRate")) == 0);
    namespace W = report::words;
    checkStr(QStringLiteral("F2 one swing left out"), W::lmLeftOutNote({ 6 }), QStringLiteral("Swing 6 is left out, as on the launch monitor panel."));
    checkStr(QStringLiteral("F3 several, a run as a range"), W::lmLeftOutNote({ 2, 3, 4, 9 }), QStringLiteral("Swings 2–4 and 9 are left out, as on the launch monitor panel."));
    checkStr(QStringLiteral("F4 none: no sentence"), W::lmLeftOutNote({}), QString());
    checkStr(QStringLiteral("F5 single club, nine counted, one out"), W::ballNote(9, false, { 10 }),
             QStringLiteral("Typical value (the middle of your nine swings) in large type; the range from lowest to highest beneath. "
                            "Swing 10 is left out, as on the launch monitor panel."));
    checkStr(QStringLiteral("F6 no marks, no sentence"), W::lmMarksNote(false, false), QString());

    std::printf("\nG — a session with no launch monitor\n");
    const report::LmFigures none = report::lmFiguresOf({}, dirs, ledgerClubs, QString(), false);
    check(QStringLiteral("G1 no ball block, no numbers, no carry row"), !none.has() && report::lmBallBlock(none).isNull()
              && report::lmNumbersBlock(none, true, {}, 0).isNull() && report::lmGridCarry(none).isEmpty() && report::lmAppendixBlock(none, {}).isNull());

    board.setShotModel(nullptr);
    std::printf("\n%d checks, %d failed\n", g_run, g_fail);
    return g_fail == 0 ? 0 : 1;
}
