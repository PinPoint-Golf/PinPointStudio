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

// THE SESSION REPORT'S SENTENCES (src/Gui/report/report_words.h, and the gate words the model carries in
// src/Resources/diagnostics/gate_words.json, read through Diagnostics/packs/gate_words_pack.h):
// every template with every case written (docs/design/session_report_design.md §9.1 rule 7) — one and
// several, every and almost every, none / one / several — plus the honesty rules of §10 that live in
// words (link grades, counts as swings, no percentage, times against impact, the true minus), the two
// dates (§8.4), and the bugs §9.3 found in the mock, each made impossible here: a count doubling its
// noun, hard-coded number words, a session-level kicker that broke, "Coaches call the habit X"
// repeating the title, "125 %" losing its words, "came down to 2 of 2" on two swings.
//
// The posture page's block (report/report_posture.h, posture_chart_design.md §5.7) is cut from the Charts
// Posture presenter's session payload; here on a payload of the presenter's shape (the real one is cut in
// charts_posture_presenter_test): absent without a reading, the sentences verbatim, the caps and their "N more",
// the strip renumbered to the report's swings, "What moved" only where there is a line.
//
// The map itself is assembled by SessionReportModel from a live diagnostics model, and is proved on
// real sessions by tools/session-report-mock/dump_report.qml (build/report-build/model-I/): a model test
// over a fixture session would need the whole diagnostics stack linked into a test, which no Gui test
// does today; the probe is the stronger check of that half.
//
// WHAT A STUDIO EDITS REACHES THE NEXT REPORT (#60), proved one level below the map for that reason: the
// report's builder (SessionReportModel::build) reads three things per make — the pack through
// makeCharacteristicPackProvider(), the drills through sharedDrillSet() and the gate words through
// sharedGateWordSet() after dropping that cache — and its page readers take a work-on's goal from the
// habit, its drills by the habit's `drills` ids from the drill set, and a not-checked line from the gate's
// clause after the subject's words. The block at the end writes a user pack, a user drill and a user
// gate-word override with the writers the Diagnostic Model saves through, into Qt's test data location,
// and reads them back through exactly those three calls and the back page's own sentence.

#include "report/report_words.h"
#include "report/report_posture.h"
#include "report/report_wrist.h"
#include "../../Diagnostics/packs/drill_pack.h"
#include "../../Diagnostics/packs/gate_words_pack.h"
#include "../../Diagnostics/packs/pack_provider.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>

#include <cstdio>

using namespace pinpoint::report::words;
using pinpoint::analysis::SpreadCorridor;

static int g_fail = 0;
static void check(bool c, const char *label)
{
    std::printf("  [%s] %s\n", c ? "PASS" : "FAIL", label);
    if (!c) ++g_fail;
}
static void eq(const QString &got, const QString &want, const char *label)
{
    const bool ok = got == want;
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) { std::printf("        got:  %s\n        want: %s\n", qPrintable(got), qPrintable(want)); ++g_fail; }
}
static bool noDoubledNoun(const QString &s) { return !s.contains(QLatin1String("swings swings")) && !s.contains(QLatin1String("swing swing")); }
static const QString M = QString(QChar(0x2212));

static SpreadCorridor corridor(double lo, double hi, bool lowOpen, bool highOpen, double faultLo, double faultHi)
{
    SpreadCorridor c;
    c.known = true;
    c.signalLo = lo; c.signalHi = hi;
    c.lowOpen = lowOpen; c.highOpen = highOpen;
    c.faultLo = faultLo; c.faultHi = faultHi;
    c.mu = (lo + hi) / 2;
    return c;
}

// The user layers the end-to-end block writes, all three in diagnosticsDataDir().
static void removeUserLayers()
{
    QFile::remove(pinpoint::analysis::userPackPath());
    QFile::remove(pinpoint::analysis::userDrillSetPath());
    QFile::remove(pinpoint::analysis::userGateWordSetPath());
    pinpoint::analysis::resetSharedDrillSet();
    pinpoint::analysis::resetSharedGateWordSet();
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    // Qt's test data location from the first line, so the gate words read below are never a user layer a
    // studio left in the real one, and the end-to-end block's writes never land there.
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setApplicationName(QStringLiteral("report_model_test"));
    removeUserLayers();

    std::printf("counts — the noun once, never a percentage\n");
    eq(countOf(9, 10), QStringLiteral("9 of 10 swings"), "countOf several");
    eq(countOf(1, 1), QStringLiteral("1 of 1 swing"), "countOf one judged");
    eq(gridCount(6, 6), QStringLiteral("6 of 6"), "grid count carries no noun");
    eq(swingsOn(10, 10), QStringLiteral("on all 10 swings"), "every swing");
    eq(swingsOn(9, 10), QStringLiteral("on 9 of 10 swings"), "some swings");
    eq(swingsOn(1, 1), QStringLiteral("on the one swing we could measure"), "one swing judged");
    eq(swingsOn(0, 0), QString(), "nothing judged: absent");
    eq(swingsN(1), QStringLiteral("1 swing"), "1 swing");
    eq(swingsN(7), QStringLiteral("7 swings"), "7 swings");
    eq(strengthCount(true, 10, 10), QStringLiteral("0 of 10 swings"), "outcome strength: it never happened");
    eq(strengthCount(false, 10, 10), QStringLiteral("all 10 swings"), "movement strength on every swing");
    eq(strengthCount(false, 6, 7), QStringLiteral("6 of 7 swings"), "movement strength, one swing unmeasured");
    eq(habitsWord(1), QStringLiteral("one habit"), "one habit");
    eq(habitsWord(5), QStringLiteral("five habits"), "five habits");
    check(numberWord(0) == QLatin1String("no") && numberWord(20) == QLatin1String("twenty") && numberWord(21) == QLatin1String("21"),
          "number words to twenty, then figures");

    std::printf("headline variants (§3, §3.1)\n");
    eq(headline(5, 0, 0.9, partWords(4.0), QStringLiteral("Keep the flex in your trail knee")),
       QStringLiteral("Five habits showed on almost every swing. We start with your backswing: keep the flex in your trail knee."),
       "several habits, almost every swing");
    eq(headline(1, 0, 1.0, partWords(1.0), QStringLiteral("Set your feet about as wide as your shoulders")),
       QStringLiteral("One habit showed on every swing. We start with your set-up: set your feet about as wide as your shoulders."),
       "one habit, every swing");
    eq(headline(3, 1, 0.85, partWords(1.0), QStringLiteral("Stand taller")),
       QStringLiteral("Four habits showed: three on almost every swing, and one over the whole session. We start with your set-up: stand taller."),
       "swing-by-swing and session-level habits together");
    eq(headline(0, 1, 1.0, partWords(11.0), QStringLiteral("Fire your body the same way every swing")),
       QStringLiteral("One habit showed over the whole session. We start with your timing: fire your body the same way every swing."),
       "a session-level habit alone");
    check(headline(2, 0, 0.6, partWords(7.0), QStringLiteral("X")).contains(QLatin1String("on most swings")), "most swings below 80 %");
    check(headline(2, 0, 0.4, partWords(7.0), QStringLiteral("X")).contains(QLatin1String("many of your swings")), "many swings below half");
    eq(headline(0, 0, 1.0, QString(), QString()), QString(), "no habits: no habit headline");
    check(headlineClean(3).startsWith(QLatin1String("Nothing repeated often enough to call a habit.")), "clean session, with a watch list");
    check(!headlineClean(0).contains(QLatin1String("now and then")), "clean session, nothing on the watch list");
    eq(headlineNote(2, true), QStringLiteral("Two swings is too few to call a habit, so this is a short note rather than a full report: here is what the ball did."),
       "two-swing note with a launch monitor");
    eq(headlineNote(1, false), QStringLiteral("One swing is too few to call a habit, so this is a short note rather than a full report."),
       "one-swing note, no launch monitor");
    check(QStringLiteral("We start with ") + partWords(7.0) == QLatin1String("We start with impact"), "impact names no 'your'");
    check(partWords(8.0) == QLatin1String("your follow-through") && partWords(10.0) == QLatin1String("your finish"), "follow-through, finish");

    std::printf("swing by swing (§9.3: no hard-coded number words)\n");
    eq(gridTitle(true, 5, false), QStringLiteral("The same five, every swing"), "the same five");
    eq(gridTitle(true, 3, false), QStringLiteral("The same three, every swing"), "the same three — generated, not 'five'");
    eq(gridTitle(true, 1, false), QStringLiteral("Your habits, swing by swing"), "one habit is not 'the same one'");
    eq(gridTitle(false, 5, false), QStringLiteral("Your habits, swing by swing"), "not every habit on every swing");
    eq(gridTitle(false, 0, true), QStringLiteral("What we measured, swing by swing"), "clean session's grid");
    check(gridStandfirst(7, false).contains(QLatin1String("one of your seven swings")) && !gridStandfirst(7, false).contains(QLatin1String("ten")),
          "standfirst counts this session's swings in words");
    check(gridStandfirst(10, true).contains(QLatin1String("stub")) && !gridStandfirst(10, false).contains(QLatin1String("stub")),
          "the stub is explained only when one is drawn");
    eq(gridGroupTitle(QStringLiteral("habit"), 5), QStringLiteral("Your habits — the five work-ons"), "group title, five");
    eq(gridGroupTitle(QStringLiteral("habit"), 1), QStringLiteral("Your habit — the one work-on"), "group title, one");
    eq(gridMore(QStringLiteral("seen"), 2), QStringLiteral("and two more seen on fewer swings"), "seen ending");
    eq(gridMore(QStringLiteral("habit"), 1), QStringLiteral("and one more habit, not on the list today"), "habit ending, one");
    eq(gridMore(QStringLiteral("ball"), 0), QString(), "no ending when nothing is cut");
    eq(sessionLevelGridNote(QStringLiteral("Sequence not repeatable"), 5),
       QStringLiteral("Sequence not repeatable is read over the whole session, not swing by swing: see work-on 5."),
       "a session-level habit is a line under the grid, never a 0-of-0 row");
    check(referenceText(3, true, QStringLiteral("148"), QStringLiteral("105"), false, 0, 5).startsWith(QLatin1String("Swing 3 came closest to your targets: a 148 yard carry at 105 mph ball speed.")),
          "reference swing, not the longest");
    check(referenceText(10, true, QStringLiteral("155"), QStringLiteral("109"), true, 5, 5).contains(QLatin1String("your longest")),
          "'your longest' only when it was");
    check(!referenceText(3, true, QStringLiteral("148"), QStringLiteral("105"), false, 5, 5).contains(QLatin1String("longest")), "and never otherwise");
    eq(referenceText(2, false, QStringLiteral("-"), QStringLiteral("-"), false, 1, 1), QStringLiteral("Swing 2 came closest to your targets. It was inside the target range for your work-on."),
       "no launch monitor, one work-on");

    std::printf("earlier today (§9.3: never 'eased' on two swings)\n");
    eq(todayCell(true, 8, 8, false), QStringLiteral("habit 8/8"), "habit cell");
    eq(todayCell(false, 4, 10, false), QStringLiteral("4/10"), "quiet cell");
    eq(todayCell(false, 0, 0, false), QStringLiteral("-"), "not measured: the NA dash");
    eq(todayCell(true, 0, 0, true), QStringLiteral("habit"), "session-level habit: no per-swing counts");
    eq(todayAlready(5, 5), QStringLiteral("All five of today's habits were already there in your first session."), "all already there");
    eq(todayAlready(1, 5), QStringLiteral("One of today's habits was already there in your first session."), "one already there");
    eq(todayAlready(3, 5), QStringLiteral("Three of today's habits were already there in your first session."), "several already there");
    eq(todayAlready(0, 5), QStringLiteral("None of today's habits was a habit in your first session."), "none already there");
    eq(todayAlready(1, 1), QStringLiteral("Today's habit was already there in your first session."), "the one habit, already there");
    eq(todayEarlierHabit(QStringLiteral("Out-of-order sequence"), 2, 2),
       QStringLiteral("Out-of-order sequence was a habit earlier today; this session could only judge it on two swings, too few to say."),
       "two swings: too few to say");
    eq(todayEarlierHabit(QStringLiteral("X"), 1, 1), QStringLiteral("X was a habit earlier today; this session could only judge it on one swing, too few to say."), "one swing");
    eq(todayEarlierHabit(QStringLiteral("X"), 0, 0), QStringLiteral("X was a habit earlier today; this session could not measure it."), "not measured");
    eq(todayEarlierHabit(QStringLiteral("X"), 4, 10), QStringLiteral("X was a habit earlier today and came down to 4 of 10 swings here."), "came down");
    eq(todayEarlierHabit(QStringLiteral("X"), 0, 10), QStringLiteral("X was a habit earlier today and did not show here (0 of 10 swings)."), "did not show");
    eq(todayMore(1), QStringLiteral("1 earlier session not shown"), "one session past the cap");
    eq(todayMore(3), QStringLiteral("3 earlier sessions not shown"), "several past the cap");
    eq(todayMore(0), QString(), "none past the cap: absent");

    std::printf("why this order (§4.2: the rule that ran, every case)\n");
    eq(orderIntro(4), QStringLiteral("Your list is ordered by four rules, applied in turn. Here is each rule and what it did today."), "four rules under v4");
    eq(orderIntro(3), QStringLiteral("Your list is ordered by three rules, applied in turn. Here is each rule and what it did today."), "three when links are testable");
    check(ruleStillHappeningToday(0, 5).startsWith(QLatin1String("None of the five eased")), "rule 1: none eased");
    check(ruleStillHappeningToday(0, 1).startsWith(QLatin1String("Your habit did not ease")), "rule 1: the one habit");
    eq(ruleStillHappeningToday(1, 5), QStringLiteral("One eased during the session and moved to the back."), "rule 1: one eased");
    eq(ruleStillHappeningToday(2, 5), QStringLiteral("Two eased during the session and moved to the back."), "rule 1: two eased");
    check(ruleCauseFirstToday(0, 1).startsWith(QLatin1String("One habit waits behind another")), "rule 2: one supported link");
    check(ruleCauseFirstToday(0, 2).startsWith(QLatin1String("Two habits wait behind another")), "rule 2: two");
    check(ruleCauseFirstToday(0, 0).contains(QLatin1String("changed nothing")), "rule 2: testable, nothing supported");
    check(ruleCauseFirstToday(1, 0).contains(QLatin1String("nothing to do")), "rule 2: no authored links");
    check(ruleCauseFirstToday(2, 0).startsWith(QLatin1String("It could not be tested today.")) && ruleCauseFirstToday(2, 0).contains(QLatin1String("every swing")),
          "rule 2: the cause on every swing");
    check(ruleCauseFirstToday(3, 0).contains(QLatin1String("too few swings")), "rule 2: too few swings");
    check(ruleSwingOrderToday(3).startsWith(QLatin1String("This is what set today's order")) && ruleSwingOrderToday(0).contains(QLatin1String("changed nothing")),
          "rule v4: decided / not");
    check(ruleBiggestMissToday(true, 2).contains(QLatin1String("same point in the swing")), "rule 3 under v4 breaks ties");
    check(ruleBiggestMissToday(false, 4).startsWith(QLatin1String("This is what set today's order")), "rule 3 when links were testable");
    check(ruleBiggestMissToday(true, 0).contains(QLatin1String("changed nothing")), "rule 3 decided nothing");
    check(ruleMovementsFirstToday(1).startsWith(QLatin1String("One number")) && ruleMovementsFirstToday(2).startsWith(QLatin1String("Two numbers")),
          "delivery numbers filling the list");
    check(barsNote(0).endsWith(QLatin1String("The longest bar goes first.")), "bars set the order");
    check(barsNote(1).contains(QLatin1String("same point in the swing")), "bars break ties under v4");
    check(barsNote(2).contains(QLatin1String("not necessarily first")), "bars did not set it — the caption changes with v4");
    check(timelineNote(true, true, 1, {}).startsWith(QLatin1String("Your first work-on is also the earliest")), "timeline: first is earliest");
    check(timelineNote(true, false, 1, {}).contains(QLatin1String("it missed by most")), "timeline: first leads by magnitude");
    check(timelineNote(true, false, 0, {}).contains(QLatin1String("setting up another")), "timeline: first leads by a supported link");
    eq(timelineNote(false, false, 1, { QStringLiteral("Sequence not repeatable") }),
       QStringLiteral("Sequence not repeatable is read over the whole session, so it has no place on it."), "only a session-level habit");
    check(timelineNote(true, true, 1, { QStringLiteral("A"), QStringLiteral("B") }).contains(QLatin1String("A and B are read over the whole session, so they have")),
          "two session-level habits");

    std::printf("links at the grade the session earned (§10 rule 3 — never cause)\n");
    const QString supported = linkWords(0, QStringLiteral("Over the top"), QStringLiteral("Early extension"), 4, 3);
    check(supported.contains(QLatin1String("went away too (3 of 4 swings)")) && supported.contains(QLatin1String("not proof")),
          "supported: it went away on the swings where the cause did, still not proof");
    check(linkWords(1, QStringLiteral("A"), QStringLiteral("B")).contains(QLatin1String("did not show it")), "testable, not shown");
    check(linkWords(2, QStringLiteral("Over the top"), QStringLiteral("B")).contains(QLatin1String("cannot test it, because over the top showed on every swing")),
          "untestable: cause on every swing");
    check(linkWords(3, QStringLiteral("A"), QStringLiteral("B")).contains(QLatin1String("too few swings")), "untestable: too few swings");
    check(linkWords(4, QStringLiteral("Early extension"), QStringLiteral("Loss of posture"))
              == QStringLiteral("Linked in the coaching model. Early extension and loss of posture are measured from the same readings, so your swings cannot test it."),
          "untestable: measured from the same readings (sharedEvidence, posture_metrics_design.md §6.5)");
    for (int g = 0; g < 5; ++g) {
        const QString w = linkWords(g, QStringLiteral("A"), QStringLiteral("B"), 4, 2);
        check(!w.contains(QLatin1String("causes ")) || w.contains(QLatin1String("not proof")), "no grade claims cause");
    }

    std::printf("the work-on page\n");
    eq(workOnKicker(1, 5, 4.0, 10, 10), QStringLiteral("Work-on 1 of 5  ·  P4, at the top  ·  10 of 10 swings"), "kicker at a position");
    eq(workOnKicker(5, 5, 11.0, 0, 0), QStringLiteral("Work-on 5 of 5  ·  Over the whole session"),
       "session-level kicker: no ', over the session · over the session' (§9.3)");
    eq(workOnKicker(2, 3, 4.5, 6, 8), QStringLiteral("Work-on 2 of 3  ·  At the start of the downswing  ·  6 of 8 swings"), "between positions: words, no code");
    check(noDoubledNoun(workOnKicker(1, 1, 7.0, 1, 1)), "kicker never doubles its noun");
    eq(coachTerm(QStringLiteral("Stance too wide"), QStringLiteral("Stance too wide"), {}), QString(),
       "title IS the coach's term, no aliases: no line (§9.3)");
    eq(coachTerm(QStringLiteral("Stance too wide"), QStringLiteral("Set your feet about as wide as your shoulders"), {}),
       QStringLiteral("Coaches call the habit <i>Stance too wide</i>."), "coach's term, no aliases");
    eq(coachTerm(QStringLiteral("Early extension"), QStringLiteral("Stay in your posture"), { QStringLiteral("standing up"), QStringLiteral("hip thrust"), QStringLiteral("third") }),
       QStringLiteral("Coaches call the habit <i>Early extension</i>, or “standing up”, “hip thrust”."), "two aliases at most");
    eq(coachTerm(QStringLiteral("A & B <x>"), QStringLiteral("Goal"), {}), QStringLiteral("Coaches call the habit <i>A &amp; B &lt;x&gt;</i>."),
       "pack text is escaped in a styled string");
    eq(trendWords(true, 0, 10), QStringLiteral("easing as the session went on"), "trend: easing");
    eq(trendWords(true, 2, 10), QStringLiteral("growing as the session went on"), "trend: growing");
    eq(trendWords(false, 1, 10), QStringLiteral("steady from first swing to last"), "trend: steady");
    eq(trendWords(false, 1, 4), QString(), "trend: too few swings to say anything");
    eq(sawLead(QStringLiteral("your trail leg straightens going back"), swingsOn(10, 10), trendWords(false, 1, 10)),
       QStringLiteral("Your trail leg straightens going back — on all 10 swings, steady from first swing to last."), "what we saw, lead");
    eq(sawLead(QStringLiteral("x"), swingsOn(3, 4), QString()), QStringLiteral("X — on 3 of 4 swings."), "lead without a trend");
    eq(sawResolving(QStringLiteral("your head dips going back"), 5, 10, 6, 4),
       QStringLiteral("Your head dips going back — on 5 of 10 swings, but it stopped after swing 6: none of your last four swings showed it."),
       "resolving: the page leads with the change");
    eq(sawResolving(QStringLiteral("x"), 5, 10, 9, 1), QStringLiteral("X — on 5 of 10 swings, but it stopped after swing 9: your last swing did not show it."),
       "resolving with one quiet swing");
    eq(typicalClause(QStringLiteral("trail-knee bend lost by the top"), QStringLiteral("22°"), QStringLiteral("12° or less"),
                     QStringLiteral("a clear miss: well past the range on 9 of 10 swings")),
       QStringLiteral("Typical trail-knee bend lost by the top: 22°, against a target of 12° or less; this is a clear miss: well past the range on 9 of 10 swings."),
       "typical clause, every part");
    eq(typicalClause(QStringLiteral("x"), QStringLiteral("3°"), QString(), QStringLiteral("ignored")), QStringLiteral("Typical x: 3°."),
       "no range: no target, and no magnitude words without one");
    eq(typicalClause(QStringLiteral("x"), QStringLiteral("-"), QStringLiteral("1 or more"), QString()), QString(), "no value: absent");
    eq(lowTailValue(QStringLiteral("4°"), QStringLiteral("your lead wrist cups more at impact")),
       QStringLiteral("4° the other way (your lead wrist cups more at impact)"), "low tail of a +1 measure: magnitude with golferLow");
    eq(notMeasuredClause(3, 10, QStringLiteral("the camera could not find the ball clearly at address")),
       QStringLiteral("It could not be measured on 3 of 10 swings: the camera could not find the ball clearly at address."), "unmeasured swings, with the reason");
    eq(notMeasuredClause(0, 10, QStringLiteral("x")), QString(), "every swing measured: absent");
    eq(timingSpreadSentence(QStringLiteral("lead arm"), 12.6006, 4.0937),
       QStringLiteral("Your lead arm's peak timing varied 12.6× ± 4.1 as widely as touring professionals' do."),
       "timing spread: the ± only, no citation in body text (§9.3)");
    eq(timingSpreadSentence(QString(), 2.0, 0.0), QStringLiteral("Your peak timing varied 2.0× as widely as touring professionals' do."), "no σ: no ±");
    eq(orderCountSentence(3, 9), QStringLiteral("Your hips, chest, arms and club fired in that order on 3 of 9 swings."),
       "order share: a COUNT of swings, never the inferred share");
    check(!orderCountSentence(3, 9).contains(QLatin1Char('%')), "no percentage for an inferred share");
    check(orderCountSentence(0, 0).contains(QLatin1String("could not be judged")), "order not judged");
    eq(figureLabel(QStringLiteral("hand rise"), QStringLiteral("% hand rise")), QStringLiteral("Each dot is one swing: hand rise, in percent hand rise."), "figure label, % unit");
    eq(figureLabel(QStringLiteral("stance width"), QStringLiteral("% shoulder width")), QStringLiteral("Each dot is one swing: stance width, in percent of shoulder width."), "figure label, width");
    eq(cameWithText({}, {}, true), QString(), "nothing came with it: absent (rule 6)");
    eq(cameWithText({ cameWithShown(QStringLiteral("The club comes back toe-up"), 7, 7) }, {}, true),
       QStringLiteral("It came with the club comes back toe-up (7 of the 7 swings we could see). That fits the coaching model, though showing up together is not proof that one causes the other."),
       "came with: shown only");
    eq(cameWithText({}, { QStringLiteral("your hips turn in place going back") }, true),
       QStringLiteral("The coaching model says it can bring other habits with it. Not today — on every swing, your hips turn in place going back."),
       "came with: clean only");
    check(cameWithText({ QStringLiteral("a (1 of the 3 swings we could see)") }, { QStringLiteral("b"), QStringLiteral("c") }, false)
              .endsWith(QLatin1String("And on every swing we could see, b and c.")), "came with: both, some swings unmeasured");

    std::printf("what might be behind it (§3.3: every lead case)\n");
    eq(causesLead(1, 1, false, true, true, true, 5),
       QStringLiteral("One possible cause showed up on the same swings. Because both happened on every swing, we can't prove one drives the other — so here is what we spotted, what we couldn't check and what we've ruled out."),
       "spotted, on every swing, all three groups");
    check(causesLead(1, 2, false, false, false, false, 2).startsWith(QLatin1String("Two possible causes showed up")), "two spotted");
    check(causesLead(1, 1, true, false, false, true, 2).contains(QLatin1String("your swings point to it")), "spotted and supported");
    check(causesLead(1, 1, false, false, false, false, 1).endsWith(QLatin1String("here is what we spotted.")), "only the spotted group named");
    eq(causesLead(2, 0, false, false, true, false, 3), QStringLiteral("We couldn't pin down what drives this one today. Here are the possible causes: the ones we couldn't check."),
       "unchecked, nothing ruled out");
    check(causesLead(2, 0, false, false, true, true, 3).endsWith(QLatin1String("and the ones we've ruled out.")), "unchecked and ruled out");
    check(causesLead(3, 0, false, false, false, true, 1).startsWith(QLatin1String("We checked the one cause we know of")), "the one cause, ruled out");
    check(causesLead(3, 0, false, false, false, true, 4).startsWith(QLatin1String("We checked every cause we know of")), "every cause ruled out");
    check(causesLead(0, 0, false, false, false, false, 0).startsWith(QLatin1String("We couldn't pin down")),
          "no authored cause (inconsistent_sequence, §10.9): the couldn't-pin-down lead");
    eq(causesSpottedItem(QStringLiteral("Your hands come over the top coming down"), 10, 10, 4),
       QStringLiteral("Your hands come over the top coming down — on 10 of 10 swings, work-on 4"), "spotted item");
    eq(causesCouldBeMore(4), QStringLiteral("4 more, less likely"), "could-be ending");
    eq(causesRuledOutMore(0), QString(), "nothing cut: no ending");
    eq(targetLine(QStringLiteral("trail-knee bend lost by the top"), QStringLiteral("12° or less"), QStringLiteral("22°"), false),
       QStringLiteral("Trail-knee bend lost by the top: 12° or less, on most swings. Today: 22°."), "next time");
    eq(targetLine(QStringLiteral("x"), QStringLiteral("2 or less"), QString(), true), QStringLiteral("X: 2 or less, over a session."), "next time, session-level, no today");
    eq(targetLine(QStringLiteral("x"), QString(), QStringLiteral("3"), false), QString(), "no range: no next-time line");

    std::printf("figures, units, the target range (signs: raw × golferSign, never abs)\n");
    eq(withUnit(QStringLiteral("125"), QStringLiteral("% shoulder width")), QStringLiteral("125% shoulder width"), "'125 %' keeps its words, closed up (§9.3)");
    eq(withUnit(QStringLiteral("22"), QStringLiteral("°")), QStringLiteral("22°"), "degrees close up");
    eq(withUnit(QStringLiteral("9"), QStringLiteral("cm")), QStringLiteral("9 cm"), "a worded unit takes a space");
    eq(withUnit(QStringLiteral("12.6"), QStringLiteral("ratio")), QStringLiteral("12.6×"), "a ratio");
    eq(withUnit(QStringLiteral("-"), QStringLiteral("°")), QStringLiteral("-"), "the NA dash takes no unit");
    eq(golferValue(-21.6, QStringLiteral("°"), 0, -1), QStringLiteral("22°"), "sign −1: a loss reads as a magnitude");
    eq(golferValue(2.0, QStringLiteral("°"), 0, -1), M + QStringLiteral("2°"), "sign −1 on a swing that GAINED: it reads the other way, never abs()");
    eq(golferValue(-3.2, QStringLiteral("°"), 0, 1), M + QStringLiteral("3°"), "the true minus");
    eq(golferValue(-0.2, QStringLiteral("°"), 0, 1), QStringLiteral("0°"), "−0 prints as 0");
    eq(golferValue(std::nan(""), QStringLiteral("°"), 0, 1), QStringLiteral("-"), "no value: the dash");
    eq(golferValue(12.94, QStringLiteral("°"), 1, 1), QStringLiteral("12.9°"), "the model's own decimals, never σ-rounded");
    eq(timeVsImpact(-87.4), QStringLiteral("87 ms before impact"), "before impact, never −87 ms");
    eq(timeVsImpact(12), QStringLiteral("12 ms after impact"), "after impact");
    eq(timeVsImpact(0.3), QStringLiteral("at impact"), "at impact");
    eq(targetWords(corridor(0, 12, true, false, 0, 18), QStringLiteral("°")), QStringLiteral("12° or less"), "ceiling");
    eq(targetWords(corridor(1.3, 5, false, true, 1.2, 5), QString()), QStringLiteral("1.3 or more"), "floor");
    eq(targetWords(corridor(18, 58, false, false, 8, 68), QStringLiteral("°")), QStringLiteral("18° to 58°"), "two-sided, degrees on both");
    eq(targetWords(corridor(-6, 6, false, false, -9, 9), QStringLiteral("cm")), M + QStringLiteral("6 to 6 cm"), "two-sided, worded unit once");
    eq(targetWords(corridor(82, 118, false, false, 70, 130), QStringLiteral("% shoulder width")),
       QStringLiteral("82% shoulder width to 118% shoulder width"), "two-sided, a percent of something");
    eq(targetWords(corridor(-12, 0, false, true, -18, 0), QStringLiteral("°"), -1), QStringLiteral("12° or less"),
       "sign −1 mirrors a floor into a ceiling: 'bend lost: 12° or less'");
    eq(targetWords(corridor(-80, -20, false, false, -120, 10), QStringLiteral("ms"), 1, true),
       QStringLiteral("between 80 ms before impact and 20 ms before impact"), "an instant's window, said against impact");
    eq(targetWords(corridor(0, 33, true, false, 0, 60), QStringLiteral("ms"), 1, false), QStringLiteral("33 ms or less"),
       "a duration in ms is a plain figure");
    check(isInstantVsImpact(QStringLiteral("ms"), QStringLiteral("when your hips reached full speed"))
              && !isInstantVsImpact(QStringLiteral("ms"), QStringLiteral("how long before the ball the club stopped speeding up")),
          "the golfer words decide instant against duration");
    eq(targetWords(corridor(0, -20, false, false, 0, 0), QStringLiteral("ms")), QString(), "an empty band: no words");
    SpreadCorridor none;
    eq(targetWords(none, QStringLiteral("°")), QString(), "no corridor: no words");
    eq(unitWords(QStringLiteral("°")), QStringLiteral("degrees"), "unit words");

    std::printf("dates (§8.4: the session's date and the prepared date, never confused)\n");
    const QDate s(2026, 10, 20), p(2026, 10, 22);
    eq(longDate(s), QStringLiteral("Tuesday 20 October 2026"), "long date");
    eq(shortDate(s), QStringLiteral("20 Oct 2026"), "short date");
    eq(mastheadDate(p), QStringLiteral("Prepared 22 October 2026"), "masthead: the prepared date");
    eq(folioDate(p), QStringLiteral("Prepared 22 Oct 2026"), "folio: the prepared date");
    eq(pdfTitle(QStringLiteral("Nick Henn"), s), QStringLiteral("Session report — Nick Henn — session of 20 Oct 2026"), "PDF title: the session date");
    eq(aboutDates(s, p), QStringLiteral("This report covers your session on Tuesday 20 October 2026 and was prepared on 22 October 2026. "
                                        "If a session is re-analysed, the report is prepared again and carries the new date."), "about: both dates");
    eq(sessionOfDay(3, 3, true), QStringLiteral("Session 3 of 3 today"), "session of the day, prepared that day");
    eq(sessionOfDay(3, 3, false), QStringLiteral("Session 3 of 3 that day"), "prepared on a later day: never 'today'");
    eq(sessionOfDay(1, 1, true), QString(), "alone on its day: absent");
    eq(kicker(s, QStringLiteral("7 iron"), 10, sessionOfDay(3, 3, true)),
       QStringLiteral("Session of Tuesday 20 October 2026  ·  7 iron  ·  10 swings  ·  Session 3 of 3 today"), "cover kicker");
    eq(kicker(s, QString(), 2, QString()), QStringLiteral("Session of Tuesday 20 October 2026  ·  2 swings"), "kicker with empty parts left out");
    eq(runningHead(QStringLiteral("Nick Henn"), s, QStringLiteral("7 iron")), QStringLiteral("Nick Henn  ·  session of 20 Oct 2026  ·  7 iron"), "running head");

    std::printf("clubs, cameras, about this report\n");
    eq(clubWord(QStringLiteral("7 IRON")), QStringLiteral("7 iron"), "club as a golfer says it");
    eq(clubWord(QStringLiteral("PW")), QStringLiteral("PW"), "a short code stays");
    eq(clubsWords({ QStringLiteral("7 IRON"), QStringLiteral("DRIVER"), QStringLiteral("7 IRON") }), QStringLiteral("7 iron and driver"), "several clubs, once each");
    eq(article(QStringLiteral("8 iron")), QStringLiteral("an"), "an 8 iron (the mock printed 'a 8 iron')");
    eq(article(QStringLiteral("7 iron")), QStringLiteral("a"), "a 7 iron");
    eq(article(QStringLiteral("11 wood")), QStringLiteral("an"), "an 11 wood");
    eq(article(QStringLiteral("iron")), QStringLiteral("an"), "an iron");
    eq(camerasWords({ QStringLiteral("DTL"), QStringLiteral("Face-On") }), QStringLiteral("Down-the-line and face-on cameras"), "two cameras");
    eq(camerasWords({ QStringLiteral("Face-On") }), QStringLiteral("Face-on camera"), "one camera");
    eq(aboutMeasured(7, QStringLiteral("8 iron"), { QStringLiteral("DTL"), QStringLiteral("Face-On") }, QStringLiteral("GC Quad"), 7),
       QStringLiteral("7 swings with an 8 iron, filmed down-the-line and face-on at high frame rate, with every ball measured by a GC Quad. "
                      "Your body positions are read from the video by pose tracking; the club and ball numbers come from the launch monitor."),
       "how we measured, every ball");
    check(aboutMeasured(10, QStringLiteral("7 iron"), { QStringLiteral("Face-On") }, QString(), 0).endsWith(QLatin1String("read from the video by pose tracking.")),
          "no launch monitor: no ball clause");
    check(aboutMeasured(10, QStringLiteral("7 iron"), { QStringLiteral("Face-On") }, QStringLiteral("GC Quad"), 6).contains(QLatin1String("with six of the balls measured")),
          "some balls measured");
    check(aboutRanges(true).contains(QLatin1String("on these swings with these cameras")) && !aboutRanges(false).contains(QLatin1String("Ruled out")),
          "'ruled out' explained once, only when printed");
    eq(aboutNotChecked({}, false), QString(), "nothing gated: absent");
    check(aboutNotChecked({ notCheckedItem(QStringLiteral("when your hips turned fastest"), QStringLiteral("the reading jumped"), 10, 10) }, false)
              .endsWith(QLatin1String("When your hips turned fastest: the reading jumped (10 of 10 swings). Nothing in this report depends on them.")),
          "what we could not check, nothing depends on it");
    check(aboutNotChecked({ QStringLiteral("X") }, true).endsWith(QLatin1String("its page says so.")), "a gated reading under a work-on");
    eq(notCheckedMore(1), QStringLiteral("One more is listed in the technical appendix"), "one more");
    eq(notCheckedMore(5), QStringLiteral("Five more are listed in the technical appendix"), "several more");
    check(aboutFootnote(true).contains(QLatin1String("Swing images")) && !aboutFootnote(false).contains(QLatin1String("image")),
          "the no-photos footnote never mentions swing images");

    std::printf("the ball, the numbers, the sheet, the sign-off\n");
    eq(ballByline(QStringLiteral("GC Quad"), 10, 10), QStringLiteral("measured on every swing by GC Quad"), "every swing");
    eq(ballByline(QStringLiteral("GC Quad"), 8, 10), QStringLiteral("measured on 8 of 10 swings by GC Quad"), "some swings");
    check(ballNote(10, false).contains(QLatin1String("the middle of your ten swings")), "ball note, swings in words");
    eq(ballNote(2, false), QStringLiteral("Middle of your two swings in large type; the lower and higher beneath."), "two swings");
    eq(ballMore(1), QStringLiteral("1 more club in your numbers"), "one more club");
    eq(ballMore(0), QString(), "no more clubs: absent");
    check(numbersNote(false, 5, 10, true, false).contains(QLatin1String("how many of your five work-ons")), "numbers note, five work-ons");
    check(numbersNote(false, 1, 10, true, false).contains(QLatin1String("whether your work-on showed")), "numbers note, one work-on");
    check(!numbersNote(false, 5, 2, false, false).contains(QLatin1String("Typical")) && !numbersNote(false, 5, 2, false, false).contains(QLatin1String("Habits")),
          "a two-swing note has no Typical row and no Habits column to explain");
    check(numbersNote(true, 0, 10, false, false).contains(QLatin1String("left-handed golfer")), "the hand in the sign convention");
    eq(photoReason(2, QStringLiteral("Sam"), QString()), QStringLiteral("Sam is under 18 — no parent or guardian approval recorded, so no swing photos"), "junior, no approval");
    eq(photoReason(1, QStringLiteral("Sam"), QStringLiteral("20 Oct 2026")), QStringLiteral("Sam is under 18 — a parent or guardian approved swing photos on 20 Oct 2026"), "junior, approved");
    eq(photoReason(3, QStringLiteral("Sam"), QString()), QStringLiteral("No date of birth — photos included"), "no date of birth");
    eq(photoReason(0, QStringLiteral("Sam"), QString()), QString(), "adult: nothing to say");
    eq(signoff(QStringLiteral("northfieldgolf.example"), true), QStringLiteral("Book your next session at northfieldgolf.example or reply to this email."), "a booking link");
    eq(signoff(QStringLiteral("Call us to book."), true), QStringLiteral("Call us to book."), "a booking sentence as typed");
    eq(signoff(QString(), false), QString(), "nothing to sign off with: absent");
    eq(checksTitle(1, true), QStringLiteral("A quick physical test would tell. Each could sit behind two or more of your work-ons:"), "worth checking, each behind two");
    eq(checksTitle(1, false), QStringLiteral("A quick physical test would tell:"), "worth checking, not all behind two");
    eq(checksItem(QStringLiteral("Lead-hip mobility"), { 3, 4 }), QStringLiteral("Lead-hip mobility (3, 4)"), "worth checking item");
    eq(notListedName(QStringLiteral("Thin"), 6, 10), QStringLiteral("Thin (6 of 10)"), "not listed: name carries its count");
    check(notListedResult(QStringLiteral("Early extension"), 3).contains(QLatin1String("early extension (work-on 3)")), "a result linked to a work-on");

    std::printf("the launch monitor appendix\n");
    eq(lmAppendixStandfirst(QStringLiteral("GC Quad"), 10, 10, { QStringLiteral("7 IRON") }),
       QStringLiteral("Every reading the GC Quad took, on every swing: 7 iron."), "appendix standfirst, every swing");
    eq(lmAppendixStandfirst(QString(), 7, 19, { QStringLiteral("DRIVER"), QStringLiteral("7 IRON") }),
       QStringLiteral("Every reading the launch monitor took, on 7 of 19 swings: driver and 7 iron."), "appendix standfirst, some swings, no device");
    eq(lmClubHeadline(QStringLiteral("146"), QStringLiteral("22"), 10),
       QStringLiteral("146 yd average carry, 22 yd from shortest to longest, 10 shots"), "a club's headline");
    eq(lmClubHeadline(QStringLiteral("146"), QString(), 1), QStringLiteral("146 yd average carry, 1 shot"), "one shot: no spread");
    eq(lmNoReading({}), QString(), "no missing readings, no sentence");
    eq(lmNoReading({ 4 }), QStringLiteral("No reading on swing 4."), "one missing reading");
    eq(lmNoReading({ 4, 9 }), QStringLiteral("No reading on swings 4 and 9."), "two missing readings");
    eq(lmNoReading({ 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 }), QStringLiteral("No reading on swings 1–12."), "a run becomes a range");
    eq(lmNoReading({ 1, 2, 5, 6, 7, 9 }), QStringLiteral("No reading on swings 1, 2, 5–7 and 9."), "runs of three or more only");
    check(lmTablesNote(false, false, false, QString()).contains(QLatin1String("open (face to path)")), "tables note, right-handed: plus is open");
    check(lmTablesNote(true, false, false, QString()).contains(QLatin1String("closed (face to path)")), "tables note, left-handed: plus is closed");
    check(lmTablesNote(false, true, true, QString()).contains(QLatin1String("Out:")) && lmTablesNote(false, true, true, QString()).contains(QLatin1String("*:")),
          "tables note names the marks it uses");
    check(!lmTablesNote(false, false, false, QString()).contains(QLatin1String("Out:")), "tables note leaves out marks it does not use");
    check(lmStatsNote(true).contains(QLatin1String("not left out")) && !lmStatsNote(false).contains(QLatin1String("left out")), "stats note, left-out shots");

    std::printf("gate words (§9.3 item 5: golfer words, lower case, no digits — the model's gate_words.json)\n");
    {
        using pinpoint::analysis::gateWordsFor;
        using pinpoint::analysis::subjectWordsFor;
        // The SHIPPED layer: what every studio's report prints until someone edits it on the Gates page.
        const pinpoint::analysis::GateWordSet &shipped = pinpoint::analysis::coreGateWordSet();
        check(!shipped.gates.empty() && !shipped.subjects.empty(), "the shipped gate words are read (PINPOINT_CORE_GATE_WORDS)");
        const QStringList ids{ QStringLiteral("sequence.sightedBand"), QStringLiteral("shot.captureHoles"), QStringLiteral("club3d.planeScatter"),
                               QStringLiteral("rule.diag.noCorridor"), QStringLiteral("lm.noData"), QStringLiteral("body.ballPosition") };
        bool ok = true;
        for (const QString &id : ids) {
            const QString w = gateWordsFor(shipped, id);
            if (w.isEmpty() || w[0].isUpper() || w.contains(QRegularExpression(QStringLiteral("\\d")))) { ok = false; std::printf("        bad: %s\n", qPrintable(id)); }
        }
        check(ok, "known gates have lower-case, digit-free golfer words");
        check(gateWordsFor(shipped, QStringLiteral("no.such.gate")).isEmpty(), "an unknown gate has no words (left to the appendix)");
        check(subjectWordsFor(shipped, QStringLiteral("ks.pelvis")) == QLatin1String("when your hips turned fastest"), "a subject in golfer words");
        check(subjectWordsFor(shipped, QStringLiteral("ballPosition")).isEmpty(), "a metric subject is the caller's (its measure's golferName)");
    }

    std::printf("the doubled noun cannot happen anywhere a count is built\n");
    check(noDoubledNoun(sawLead(QStringLiteral("x"), swingsOn(6, 6), QString())) && noDoubledNoun(countOf(6, 6))
              && noDoubledNoun(cameWithShown(QStringLiteral("x"), 6, 6)) && noDoubledNoun(causesSpottedItem(QStringLiteral("x"), 6, 6, 1))
              && noDoubledNoun(notMeasuredClause(2, 6, QString())) && noDoubledNoun(todayEarlierHabit(QStringLiteral("x"), 2, 6))
              && noDoubledNoun(ballByline(QStringLiteral("x"), 5, 6))
              && noDoubledNoun(lmAppendixStandfirst(QStringLiteral("x"), 5, 6, {})),
          "no 'swings swings'");

    std::printf("the posture page: absent without a reading, the presenter's words, the caps\n");
    {
        using pinpoint::report::postureReportBlock;
        const QHash<QString, int> noSwings;
        check(postureReportBlock(QVariantMap{}, noSwings).isNull(), "no payload: no posture block");
        check(postureReportBlock(QVariantMap{ { QStringLiteral("available"), false } }, noSwings).isNull(), "not available: no posture block");
        check(postureReportBlock(QVariantMap{ { QStringLiteral("available"), true },
                                              { QStringLiteral("profile"), QVariantMap{ { QStringLiteral("rows"), QVariantList{} } } } }, noSwings).isNull(),
              "available but no profile row: no posture block (empty means absent)");

        const QString dir = QStringLiteral("/lib/Golfer/2026-10-20_Golfer_01/swing_%1");
        QVariantList marks, deps, cells, rows, positions;
        for (int i = 1; i <= 10; ++i)
            marks.append(QVariantMap{ { QStringLiteral("ordinal"), i }, { QStringLiteral("swingDir"), dir.arg(i + 2, 4, 10, QLatin1Char('0')) },
                                      { QStringLiteral("ok"), i != 4 }, { QStringLiteral("differs"), i == 7 }, { QStringLiteral("between"), i == 7 },
                                      { QStringLiteral("word"), i == 4 ? QStringLiteral("-") : i == 7 ? QStringLiteral("between") : QStringLiteral("Level") } });
        for (int i = 0; i < 8; ++i)
            deps.append(QVariantMap{ { QStringLiteral("id"), QStringLiteral("D%1").arg(i + 1) }, { QStringLiteral("name"), QStringLiteral("Departure %1").arg(i + 1) },
                                     { QStringLiteral("detail"), QStringLiteral("from P5  ·  10 of 10 shots") },
                                     { QStringLiteral("line"), QStringLiteral("Departure %1, from P5: 10 of 10 shots").arg(i + 1) }, { QStringLiteral("shots"), 10 } });
        for (int p = 0; p < 8; ++p) {
            cells.append(QVariantMap{ { QStringLiteral("tag"), QStringLiteral("P%1").arg(p + 1) }, { QStringLiteral("text"), p == 3 ? QStringLiteral("-") : QStringLiteral("+1.0") } });
            positions.append(QVariantMap{ { QStringLiteral("tag"), QStringLiteral("P%1").arg(p + 1) }, { QStringLiteral("name"), QStringLiteral("P%1").arg(p + 1) },
                                          { QStringLiteral("available"), true }, { QStringLiteral("seekUs"), 1000 * p } });
        }
        for (const char *k : { "head", "hips", "bend", "leadKnee", "trailKnee", "pelvisHeight", "extra" })
            rows.append(QVariantMap{ { QStringLiteral("key"), QString::fromLatin1(k) }, { QStringLiteral("label"), QString::fromLatin1(k) },
                                     { QStringLiteral("present"), true }, { QStringLiteral("cells"), cells } });
        QStringList trust;
        for (int i = 0; i < 18; ++i) trust << QStringLiteral("Trust line %1.").arg(i + 1);
        trust << QString();
        const QVariantMap payload{
            { QStringLiteral("available"), true }, { QStringLiteral("kicker"), QStringLiteral("10 shots  ·  7 iron  ·  Posture") },
            { QStringLiteral("headText"), QStringLiteral("Level on 9 of 10 shots; you leave it the same way every time: hips in from P5.") },
            { QStringLiteral("patternLine"), QStringLiteral("Level (nearest, of your session)") },
            { QStringLiteral("profile"), QVariantMap{ { QStringLiteral("positions"), positions }, { QStringLiteral("rows"), rows },
                                                      { QStringLiteral("sub"), QStringLiteral("change from address") }, { QStringLiteral("caption"), QStringLiteral("Shaded: the Level band.") } } },
            { QStringLiteral("whatMoved"), QVariantMap{ { QStringLiteral("top"), QVariantMap{ { QStringLiteral("when"), QStringLiteral("At the top") }, { QStringLiteral("line"), QString() } } },
                                                        { QStringLiteral("impact"), QVariantMap{ { QStringLiteral("tag"), QStringLiteral("P7") }, { QStringLiteral("when"), QStringLiteral("At impact") },
                                                                                                 { QStringLiteral("line"), QStringLiteral("hips 6.5 cm in under a level head") } } } } },
            { QStringLiteral("strip"), QVariantMap{ { QStringLiteral("marks"), marks }, { QStringLiteral("line"), QStringLiteral("Level on 9 of 10 shots") },
                                                    { QStringLiteral("legend"), QStringLiteral("each mark is a shot's nearest pattern") } } },
            { QStringLiteral("departures"), deps },
            { QStringLiteral("repeat"), QVariantMap{ { QStringLiteral("words"), QStringLiteral("The tightest: head at P7.") } } },
            { QStringLiteral("trust"), QVariantMap{ { QStringLiteral("lines"), trust } } },
            { QStringLiteral("address"), QVariantMap{ { QStringLiteral("available"), true }, { QStringLiteral("title"), QStringLiteral("At address") },
                                                      { QStringLiteral("sub"), QStringLiteral("this session against the good-player band") },
                                                      { QStringLiteral("legend"), QStringLiteral("Shaded: the good-player band.") },
                                                      { QStringLiteral("rows"), QVariantList{
                                                          QVariantMap{ { QStringLiteral("measureId"), QStringLiteral("m_spineBendAtAddress") },
                                                                       { QStringLiteral("text"), QStringLiteral("39°") }, { QStringLiteral("warned"), true },
                                                                       { QStringLiteral("warnShort"), QStringLiteral("depends on where the camera stands") } } } } } } };
        QHash<QString, int> swingNo;
        for (int i = 1; i <= 10; ++i) swingNo.insert(QDir::cleanPath(dir.arg(i + 2, 4, 10, QLatin1Char('0'))), i == 10 ? 0 : 20 + i);
        swingNo.remove(QDir::cleanPath(dir.arg(12, 4, 10, QLatin1Char('0'))));      // shot 10: a swing the report does not number

        const QVariant bv = postureReportBlock(payload, swingNo);
        check(!bv.isNull(), "a reading: the posture block");
        const QVariantMap b = bv.toMap();
        bool keys = true;
        for (const char *k : { "kicker", "head", "patternLine", "profile", "moved", "strip", "departures", "departuresMore", "departuresSub", "repeat", "trust", "trustMore" })
            if (!b.contains(QString::fromLatin1(k))) { keys = false; std::printf("        missing: %s\n", k); }
        check(keys, "the block carries every documented key");
        eq(b.value(QStringLiteral("head")).toString(), payload.value(QStringLiteral("headText")).toString(), "the head is the presenter's, verbatim");
        eq(b.value(QStringLiteral("kicker")).toString(), QStringLiteral("10 shots  ·  7 iron  ·  Posture"), "the kicker is the presenter's");
        const QVariantMap pf = b.value(QStringLiteral("profile")).toMap();
        check(pf.value(QStringLiteral("rows")).toList().size() == 6, "rows capped at six");
        check(pf.value(QStringLiteral("positions")).toList().size() == 8, "eight positions");
        check(!pf.value(QStringLiteral("positions")).toList().at(0).toMap().contains(QStringLiteral("seekUs")), "no seek target on paper");
        eq(pf.value(QStringLiteral("rows")).toList().at(1).toMap().value(QStringLiteral("cells")).toList().at(3).toMap().value(QStringLiteral("text")).toString(),
           QStringLiteral("-"), "no value is \"-\", as the presenter said it");
        const QVariantList mv = b.value(QStringLiteral("moved")).toList();
        check(mv.size() == 1 && mv.at(0).toMap().value(QStringLiteral("when")).toString() == QLatin1String("At impact"), "What moved: only the line that says something");
        const QVariantList mk = b.value(QStringLiteral("strip")).toMap().value(QStringLiteral("marks")).toList();
        check(mk.size() == 10, "every shot's mark");
        check(mk.at(0).toMap().value(QStringLiteral("swing")).toInt() == 21 && mk.at(8).toMap().value(QStringLiteral("swing")).toInt() == 29,
              "marks numbered as the report numbers its swings");
        check(mk.at(9).toMap().value(QStringLiteral("swing")).toInt() == 10, "a shot the report does not number keeps its ordinal");
        check(mk.at(6).toMap().value(QStringLiteral("differs")).toBool() && !mk.at(3).toMap().value(QStringLiteral("ok")).toBool(), "ringed and unread marks carried");
        eq(b.value(QStringLiteral("strip")).toMap().value(QStringLiteral("more")).toString(), QString(), "no strip ending under the cap");
        check(b.value(QStringLiteral("departures")).toList().size() == 6, "departures capped at six");
        eq(b.value(QStringLiteral("departuresMore")).toString(), QStringLiteral("2 more"), "the departures' ending");
        check(!b.value(QStringLiteral("departuresSub")).toString().isEmpty(), "the departures' sub is the map's");
        eq(b.value(QStringLiteral("repeat")).toString(), QStringLiteral("The tightest: head at P7."), "repeatability verbatim");
        check(b.value(QStringLiteral("trust")).toStringList().size() == 16, "trust lines capped at sixteen");
        eq(b.value(QStringLiteral("trustMore")).toString(), QStringLiteral("2 more"), "the trust ending counts only lines with words");

        {
            const QVariantMap ad = b.value(QStringLiteral("address")).toMap();
            eq(ad.value(QStringLiteral("title")).toString(), QStringLiteral("At address"), "the At address block, its title the presenter's");
            check(ad.value(QStringLiteral("rows")).toList().size() == 1
                  && ad.value(QStringLiteral("rows")).toList().at(0).toMap().value(QStringLiteral("warnShort")).toString() == QLatin1String("depends on where the camera stands"),
                  "its rows verbatim, the ⚠ words with them");
        }

        QVariantMap bare = payload;
        bare.remove(QStringLiteral("address"));
        bare.insert(QStringLiteral("departures"), QVariantList{});
        bare.insert(QStringLiteral("strip"), QVariantMap{ { QStringLiteral("marks"), QVariantList{} } });
        bare.insert(QStringLiteral("whatMoved"), QVariantMap{});
        bare.insert(QStringLiteral("repeat"), QVariantMap{});
        const QVariantMap bb = postureReportBlock(bare, noSwings).toMap();
        check(bb.value(QStringLiteral("departures")).toList().isEmpty() && bb.value(QStringLiteral("departuresSub")).toString().isEmpty(),
              "no departure: no list and no sub");
        check(bb.value(QStringLiteral("address")).isNull(), "no address reading: no At address block");
        check(bb.value(QStringLiteral("strip")).isNull() && bb.value(QStringLiteral("moved")).toList().isEmpty() && bb.value(QStringLiteral("repeat")).toString().isEmpty(),
              "no mark, no line, no words: absent");
        check(bb.value(QStringLiteral("picture")).isNull() && bb.value(QStringLiteral("pictureWhy")).toStringList().isEmpty(),
              "no picture in the payload: none on the page, and nothing said");

        // the picture strip (stage 5): the typical swing numbered as the report numbers it, the coach lines cut to the
        // picture's default sets (paper has no toggle), bands and ink kept, the frames' words verbatim
        const auto mark = [](const char *role, const char *set) {
            return QVariantMap{ { QStringLiteral("kind"), QStringLiteral("vline") }, { QStringLiteral("role"), QString::fromLatin1(role) },
                                { QStringLiteral("set"), QString::fromLatin1(set) }, { QStringLiteral("x"), 10.0 } };
        };
        const QVariantMap frame{ { QStringLiteral("key"), QStringLiteral("dtl_P7") }, { QStringLiteral("camera"), QStringLiteral("dtl") },
                                 { QStringLiteral("tag"), QStringLiteral("P7") }, { QStringLiteral("when"), QStringLiteral("impact") },
                                 { QStringLiteral("caption"), QStringLiteral("impact: good players' hips sit in the shaded strip") },
                                 { QStringLiteral("note"), QStringLiteral("The white line: your butt line at impact, 7.6 cm toward the ball.") },
                                 { QStringLiteral("width"), 300 }, { QStringLiteral("height"), 600 }, { QStringLiteral("src"), QString() },
                                 { QStringLiteral("imageWhy"), QString() }, { QStringLiteral("seekUs"), 2500000 },
                                 { QStringLiteral("still"), QVariantMap{ { QStringLiteral("phase"), QStringLiteral("P7") } } },
                                 { QStringLiteral("marks"), QVariantList{ mark("coach", "posture"), mark("coach", "plane"), mark("coach", "headHips"),
                                                                         mark("band", ""), mark("ink", "") } } };
        QVariantMap withPic = payload;
        withPic.insert(QStringLiteral("picture"), QVariantMap{
            { QStringLiteral("available"), true }, { QStringLiteral("swingDir"), dir.arg(5, 4, 10, QLatin1Char('0')) }, { QStringLiteral("ordinal"), 3 },
            { QStringLiteral("typicalWhy"), QStringLiteral("the swing whose fit to Level is nearest the session's median") },
            { QStringLiteral("legend"), QStringLiteral("Thin lines: your lines at address, frozen on every frame.") },
            { QStringLiteral("why"), QStringList{} },
            { QStringLiteral("defaultSets"), QVariantMap{ { QStringLiteral("posture"), true }, { QStringLiteral("plane"), false }, { QStringLiteral("headHips"), true } } },
            { QStringLiteral("groups"), QVariantList{ QVariantMap{ { QStringLiteral("camera"), QStringLiteral("dtl") }, { QStringLiteral("label"), QStringLiteral("down the line") },
                                                                   { QStringLiteral("frames"), QVariantList{ frame } } } } } });
        const QVariantMap pb = postureReportBlock(withPic, swingNo).toMap();
        const QVariantMap pic = pb.value(QStringLiteral("picture")).toMap();
        eq(pic.value(QStringLiteral("typical")).toString(), QStringLiteral("A typical swing: swing 23"), "the typical swing, numbered as the report numbers it");
        const QVariantMap pf0 = pic.value(QStringLiteral("groups")).toList().value(0).toMap().value(QStringLiteral("frames")).toList().value(0).toMap();
        check(pf0.value(QStringLiteral("marks")).toList().size() == 4, "the plane set is cut (paper draws the default sets); bands and ink kept");
        eq(pf0.value(QStringLiteral("caption")).toString(), frame.value(QStringLiteral("caption")).toString(), "the frame's caption verbatim");
        check(!pf0.contains(QStringLiteral("seekUs")), "no seek target on paper");
        check(pf0.value(QStringLiteral("still")).toMap().value(QStringLiteral("phase")).toString() == QLatin1String("P7"), "the still to make, kept for the model");
        QVariantMap noPic = payload;
        noPic.insert(QStringLiteral("picture"), QVariantMap{ { QStringLiteral("available"), false },
                                                             { QStringLiteral("why"), QStringList{ QStringLiteral("No picture: the swing's document is not on disk.") } } });
        const QVariantMap nb = postureReportBlock(noPic, swingNo).toMap();
        check(nb.value(QStringLiteral("picture")).isNull() && nb.value(QStringLiteral("pictureWhy")).toStringList().size() == 1, "no frame: no picture, and why");

        eq(pinpoint::report::postureMoreWords(1, "shot"), QStringLiteral("1 more shot not shown"), "the strip's ending, one");
        eq(pinpoint::report::postureMoreWords(3, "shot"), QStringLiteral("3 more shots not shown"), "the strip's ending, several");
    }

    std::printf("the wrist page: absent without a reading, the presenter's words, the posture row's keys, the caps\n");
    {
        using pinpoint::report::wristReportBlock;
        const QHash<QString, int> noSwings;
        check(wristReportBlock(QVariantMap{}, noSwings).isNull(), "no payload: no wrist block");
        check(wristReportBlock(QVariantMap{ { QStringLiteral("available"), false } }, noSwings).isNull(), "not available: no wrist block");
        check(wristReportBlock(QVariantMap{ { QStringLiteral("available"), true },
                                            { QStringLiteral("rows"), QVariantList{ QVariantMap{ { QStringLiteral("numbersOnly"), true } } } } }, noSwings).isNull(),
              "available but no banded row: no wrist block (empty means absent)");

        const QString dir = QStringLiteral("/lib/Golfer/2026-10-20_Golfer_01/swing_%1");
        const int N = 130;
        QVariantList positions, points, boxes, rows, pts, cols, marks, recurring, mwRows, cellsK;
        for (int p = 0; p < 8; ++p) {
            positions.append(QVariantMap{ { QStringLiteral("tag"), QStringLiteral("P%1").arg(p + 1) }, { QStringLiteral("name"), QStringLiteral("Pos %1").arg(p + 1) },
                                          { QStringLiteral("available"), true }, { QStringLiteral("seekUs"), 1000 * p } });
            points.append(QVariantMap{ { QStringLiteral("available"), false }, { QStringLiteral("banded"), p > 0 }, { QStringLiteral("wash"), p > 0 ? QStringLiteral("band") : QStringLiteral("none") },
                                       { QStringLiteral("bandLo"), p > 0 ? QVariant(-16.0) : QVariant::fromValue(nullptr) },
                                       { QStringLiteral("bandHi"), p > 0 ? QVariant(6.0) : QVariant::fromValue(nullptr) } });
            boxes.append(QVariantMap{ { QStringLiteral("box"), QVariantMap{ { QStringLiteral("n"), 6 } } }, { QStringLiteral("values"), QVariantList{ 1.0, 2.0 } },
                                      { QStringLiteral("n"), p == 2 ? 2 : 6 }, { QStringLiteral("median"), p == 2 ? QVariant::fromValue(nullptr) : QVariant(p == 3 ? -29.0 : 0.0) },
                                      { QStringLiteral("medianText"), p == 2 ? QStringLiteral("-") : p == 3 ? QStringLiteral("−29°") : QStringLiteral("0°") },
                                      { QStringLiteral("wide"), 2.0 }, { QStringLiteral("wideText"), p == 2 ? QString() : QStringLiteral("2° wide") },
                                      { QStringLiteral("outside"), p == 3 }, { QStringLiteral("ringWord"), p == 3 ? QStringLiteral("open") : QString() },
                                      { QStringLiteral("picked"), QVariant::fromValue(nullptr) } });
        }
        for (const char *k : { "leadWristRadUln", "leadWristFlexExt", "leadForearmRot", "trailWristFlexExt", "extraA", "extraB" })
            rows.append(QVariantMap{ { QStringLiteral("numbersOnly"), false }, { QStringLiteral("dof"), QString::fromLatin1(k) },
                                     { QStringLiteral("key"), QStringLiteral("hm.") + QString::fromLatin1(k) }, { QStringLiteral("label"), QStringLiteral("Lead wrist bow / cup") },
                                     { QStringLiteral("name"), QStringLiteral("Lead wrist") }, { QStringLiteral("sub"), QStringLiteral("flexion–extension · face") },
                                     { QStringLiteral("camera"), QStringLiteral("wrist sensor") }, { QStringLiteral("source"), QStringLiteral("IMU") },
                                     { QStringLiteral("unit"), QStringLiteral("°") }, { QStringLiteral("poleTop"), QStringLiteral("closed") },
                                     { QStringLiteral("poleBottom"), QStringLiteral("open") }, { QStringLiteral("poleTopText"), QStringLiteral("↑ closed") },
                                     { QStringLiteral("poleBottomText"), QStringLiteral("↓ open") }, { QStringLiteral("yLo"), -40.0 }, { QStringLiteral("yHi"), 20.0 },
                                     { QStringLiteral("outCount"), 1 }, { QStringLiteral("points"), points }, { QStringLiteral("boxes"), boxes } });
        rows.prepend(QVariantMap{ { QStringLiteral("numbersOnly"), true }, { QStringLiteral("dof"), QStringLiteral("leadElbowFlex") } });
        for (int i = 1; i <= N; ++i) {
            const QString d = dir.arg(i + 2, 4, 10, QLatin1Char('0'));
            pts.append(QVariantMap{ { QStringLiteral("ordinal"), i }, { QStringLiteral("swingDir"), d }, { QStringLiteral("value"), i == 4 ? QVariant::fromValue(nullptr) : QVariant(80 + i % 7) },
                                    { QStringLiteral("measured"), i != 4 } });
            cols.append(QVariantMap{ { QStringLiteral("ordinal"), i }, { QStringLiteral("swingDir"), d }, { QStringLiteral("label"), QStringLiteral("Swing %1").arg(i) } });
            cellsK.append(QVariantMap{ { QStringLiteral("text"), i == 4 ? QStringLiteral("-") : QStringLiteral("50") }, { QStringLiteral("graded"), false } });
            marks.append(QVariantMap{ { QStringLiteral("ordinal"), i }, { QStringLiteral("swingDir"), d }, { QStringLiteral("fired"), i % 2 == 0 } });
        }
        for (int k = 0; k < 10; ++k)
            recurring.append(QVariantMap{ { QStringLiteral("id"), QStringLiteral("c%1").arg(k) }, { QStringLiteral("name"), QStringLiteral("Cupped at the top") },
                                          { QStringLiteral("severity"), QStringLiteral("pattern") }, { QStringLiteral("tier"), QStringLiteral("pattern") },
                                          { QStringLiteral("count"), 65 }, { QStringLiteral("countText"), QStringLiteral("65 of 130 swings, at P4") }, { QStringLiteral("marks"), marks } });
        for (int k = 0; k < 7; ++k) mwRows.append(QVariantMap{ { QStringLiteral("label"), QStringLiteral("Reading %1").arg(k) }, { QStringLiteral("rText"), QStringLiteral(".41") } });
        QStringList trust;
        for (int i = 0; i < 18; ++i) trust << QStringLiteral("Trust line %1.").arg(i + 1);
        trust << QString();
        const auto sent = [](const QString &t) { return QVariantMap{ { QStringLiteral("text"), t }, { QStringLiteral("runs"), QVariantList{} } }; };
        const QVariantMap payload{
            { QStringLiteral("available"), true }, { QStringLiteral("kicker"), QStringLiteral("130 swings  ·  7 iron  ·  Wrist") },
            { QStringLiteral("head"), sent(QStringLiteral("Graded as a Cupped wrist on every swing")) },
            { QStringLiteral("styleLine"), QStringLiteral("By the resemblance index, most resemble Cupped (130)") },
            { QStringLiteral("scoreLine"), QStringLiteral("Wrist score median 84, middle half 78–90") },
            { QStringLiteral("repeat"), QStringLiteral("From P3 on, most repeatable: lead wrist bow / cup at P7 (middle half 2° wide); least: lead wrist hinge at P4 (9°).") },
            { QStringLiteral("sizeLine"), QStringLiteral("Each box is drawn from 130 values.") },
            { QStringLiteral("positions"), positions }, { QStringLiteral("rows"), rows },
            { QStringLiteral("sub"), QStringLiteral("Δ from address, degrees  ·  a box per checkpoint, n 130") },
            { QStringLiteral("caption"), QStringLiteral("Shaded: the Cupped band, the style every swing was graded as.") },
            { QStringLiteral("scoreRun"), QVariantMap{ { QStringLiteral("caption"), QStringLiteral("Wrist score, swing by swing (0–100)") }, { QStringLiteral("points"), pts },
                                                       { QStringLiteral("ticks"), QVariantList{ QVariantMap{ { QStringLiteral("v"), 0 }, { QStringLiteral("text"), QStringLiteral("0") } },
                                                                                                QVariantMap{ { QStringLiteral("v"), 50 }, { QStringLiteral("text"), QStringLiteral("50") } },
                                                                                                QVariantMap{ { QStringLiteral("v"), 100 }, { QStringLiteral("text"), QStringLiteral("100") } } } },
                                                       { QStringLiteral("drift"), QVariantMap{ { QStringLiteral("defined"), true }, { QStringLiteral("rText"), QStringLiteral(".12") } } } } },
            { QStringLiteral("styles"), QVariantMap{ { QStringLiteral("columns"), cols }, { QStringLiteral("hasIndex"), true },
                                                     { QStringLiteral("rows"), QVariantList{ QVariantMap{ { QStringLiteral("key"), QStringLiteral("cupped") }, { QStringLiteral("name"), QStringLiteral("Cupped") },
                                                                                                          { QStringLiteral("cells"), cellsK } } } } } },
            { QStringLiteral("recurring"), recurring }, { QStringLiteral("recurringEmpty"), QStringLiteral("No wrist finding fired on any swing.") },
            { QStringLiteral("movesWith"), QVariantMap{ { QStringLiteral("available"), true }, { QStringLiteral("head"), sent(QStringLiteral("What moves with bow / cup P7")) },
                                                        { QStringLiteral("rows"), mwRows }, { QStringLiteral("chance"), QVariantMap{ { QStringLiteral("line"), sent(QStringLiteral("|r| under .17 is chance at n = 130")) } } },
                                                        { QStringLiteral("note"), sent(QStringLiteral("They occur together; that is not to say one causes another.")) },
                                                        { QStringLiteral("sub"), QStringLiteral("bow / cup at P7 against every reading, launch monitor included") } } },
            { QStringLiteral("trust"), QVariantMap{ { QStringLiteral("lines"), trust } } } };
        QHash<QString, int> swingNo;
        for (int i = 1; i <= N; ++i) if (i != 10) swingNo.insert(QDir::cleanPath(dir.arg(i + 2, 4, 10, QLatin1Char('0'))), 200 + i);

        const QVariant bv = wristReportBlock(payload, swingNo);
        check(!bv.isNull(), "a reading: the wrist block");
        const QVariantMap b = bv.toMap();
        bool keys = true;
        for (const char *k : { "kicker", "head", "styleLine", "scoreLine", "profile", "scoreRun", "styles", "recurring", "recurringMore", "recurringEmpty",
                               "repeat", "sizeLine", "movesWith", "trust", "trustMore" })
            if (!b.contains(QString::fromLatin1(k))) { keys = false; std::printf("        missing: %s\n", k); }
        check(keys, "the block carries every documented key");
        eq(b.value(QStringLiteral("head")).toString(), QStringLiteral("Graded as a Cupped wrist on every swing"), "the head is the presenter's, verbatim");
        eq(b.value(QStringLiteral("scoreLine")).toString(), payload.value(QStringLiteral("scoreLine")).toString(), "the score line verbatim");
        eq(b.value(QStringLiteral("repeat")).toString(), payload.value(QStringLiteral("repeat")).toString(), "the repeatability words verbatim");
        const QVariantMap pf = b.value(QStringLiteral("profile")).toMap();
        const QVariantList pr = pf.value(QStringLiteral("rows")).toList();
        check(pr.size() == pinpoint::report::kWristRowCap, "rows capped, the numbers-only lead elbow left off (the screen's Session leaves it off)");
        eq(pf.value(QStringLiteral("caption")).toString(), payload.value(QStringLiteral("caption")).toString(), "the caption verbatim");
        check(!pf.value(QStringLiteral("positions")).toList().at(0).toMap().contains(QStringLiteral("seekUs")), "no seek target on paper");
        const QVariantMap r0 = pr.at(1).toMap();
        bool rowKeys = true;
        for (const char *k : { "key", "label", "metricKey", "hueKey", "camera", "unit", "poleText", "styleText", "yLo", "yHi", "cells", "boxes" })
            if (!r0.contains(QString::fromLatin1(k))) { rowKeys = false; std::printf("        row missing: %s\n", k); }
        check(rowKeys, "a row carries every key the posture page's profile reads");
        eq(r0.value(QStringLiteral("key")).toString(), QStringLiteral("leadWristFlexExt"), "key = the DOF");
        eq(r0.value(QStringLiteral("hueKey")).toString(), QStringLiteral("hm.leadWristFlexExt"), "hueKey = the metric key");
        eq(r0.value(QStringLiteral("camera")).toString(), QStringLiteral("wrist sensor"), "camera = the source words");
        eq(r0.value(QStringLiteral("poleText")).toString(), QStringLiteral("↑ closed"), "poleText = which way is plus");
        const QVariantList cells = r0.value(QStringLiteral("cells")).toList();
        check(cells.size() == 8 && r0.value(QStringLiteral("boxes")).toList().size() == 8, "eight cells, eight boxes");
        bool cellKeys = true;
        for (const char *k : { "tag", "wash", "bandLo", "bandHi", "outside", "ringWord", "warn", "warnShort", "text" })
            if (!cells.at(0).toMap().contains(QString::fromLatin1(k))) { cellKeys = false; std::printf("        cell missing: %s\n", k); }
        check(cellKeys, "a cell carries every key the posture page's profile reads");
        const QVariantMap c3 = cells.at(3).toMap(), c2 = cells.at(2).toMap(), c0 = cells.at(0).toMap();
        check(c3.value(QStringLiteral("outside")).toBool() && c3.value(QStringLiteral("ringWord")).toString() == QLatin1String("open")
              && c3.value(QStringLiteral("text")).toString() == QStringLiteral("−29°") && c3.value(QStringLiteral("wash")).toString() == QLatin1String("band"),
              "a median outside the band: outside, its side in words, its figure");
        eq(c2.value(QStringLiteral("text")).toString(), QStringLiteral("-"), "no median is \"-\", never 0");
        check(c0.value(QStringLiteral("wash")).toString() == QLatin1String("none") && c0.value(QStringLiteral("bandLo")).isNull(), "no band at P1: no wash");
        eq(r0.value(QStringLiteral("boxes")).toList().at(3).toMap().value(QStringLiteral("wideText")).toString(), QStringLiteral("2° wide"), "the width words verbatim");
        const QVariantList sp = b.value(QStringLiteral("scoreRun")).toMap().value(QStringLiteral("points")).toList();
        check(sp.size() == N && sp.at(0).toMap().value(QStringLiteral("swing")).toInt() == 201 && sp.at(9).toMap().value(QStringLiteral("swing")).toInt() == 10,
              "score points numbered as the report numbers its swings (an unnumbered one keeps its ordinal)");
        eq(sp.at(3).toMap().value(QStringLiteral("text")).toString(), QStringLiteral("-"), "an unscored swing is \"-\"");
        check(b.value(QStringLiteral("scoreRun")).toMap().value(QStringLiteral("ticks")).toList().size() == 3, "the score run's ticks carried to paper");
        const QVariantMap st = b.value(QStringLiteral("styles")).toMap();
        check(st.value(QStringLiteral("columns")).toList().size() == pinpoint::report::kWristStyleColumnCap, "style columns capped");
        eq(st.value(QStringLiteral("columns")).toList().at(0).toMap().value(QStringLiteral("label")).toString(), QStringLiteral("Swing 201"), "style columns renumbered");
        check(st.value(QStringLiteral("rows")).toList().at(0).toMap().value(QStringLiteral("cells")).toList().size() == pinpoint::report::kWristStyleColumnCap,
              "the style cells cut with the columns");
        eq(st.value(QStringLiteral("more")).toString(), QStringLiteral("10 more swings not shown"), "the style table's ending");
        check(!st.value(QStringLiteral("legend")).toString().isEmpty(), "the style table says what its figures are");
        check(!b.value(QStringLiteral("recurringLegend")).toString().isEmpty() && !b.value(QStringLiteral("scoreRun")).toMap().value(QStringLiteral("legend")).toString().isEmpty(),
              "the findings and the score run say what their marks are");
        const QVariantList rec = b.value(QStringLiteral("recurring")).toList();
        check(rec.size() == pinpoint::report::kWristRecurringCap, "findings capped");
        eq(b.value(QStringLiteral("recurringMore")).toString(), QStringLiteral("2 more"), "the findings' ending");
        check(rec.at(0).toMap().value(QStringLiteral("marks")).toList().at(1).toMap().value(QStringLiteral("swing")).toInt() == 202
              && rec.at(0).toMap().value(QStringLiteral("marks")).toList().at(1).toMap().value(QStringLiteral("fired")).toBool(), "marks renumbered, fired kept");
        eq(rec.at(0).toMap().value(QStringLiteral("countText")).toString(), QStringLiteral("65 of 130 swings, at P4"), "count words verbatim");
        const QVariantMap mw = b.value(QStringLiteral("movesWith")).toMap();
        check(mw.value(QStringLiteral("rows")).toList().size() == 7, "movesWith: four readings and three launch-monitor ones");
        eq(mw.value(QStringLiteral("chanceLine")).toString(), QStringLiteral("|r| under .17 is chance at n = 130"), "the chance line verbatim");
        check(b.value(QStringLiteral("trust")).toStringList().size() == 16, "trust lines capped at sixteen");
        eq(b.value(QStringLiteral("trustMore")).toString(), QStringLiteral("2 more"), "the trust ending counts only lines with words");

        QVariantMap refused = payload;
        refused.insert(QStringLiteral("movesWith"), QVariantMap{ { QStringLiteral("available"), false },
                                                                 { QStringLiteral("refusal"), sent(QStringLiteral("Five swings are needed to relate readings across a session; this session has three.")) } });
        refused.insert(QStringLiteral("recurring"), QVariantList{});
        const QVariantMap rb = wristReportBlock(refused, noSwings).toMap();
        eq(rb.value(QStringLiteral("movesWith")).toMap().value(QStringLiteral("refusal")).toString(),
           QStringLiteral("Five swings are needed to relate readings across a session; this session has three."), "movesWith refused: its reason, verbatim");
        check(rb.value(QStringLiteral("recurring")).toList().isEmpty() && rb.value(QStringLiteral("recurringMore")).toString().isEmpty()
              && rb.value(QStringLiteral("recurringEmpty")).toString() == QLatin1String("No wrist finding fired on any swing."), "nothing fired: the empty words");
        QVariantMap noMw = payload;
        noMw.remove(QStringLiteral("movesWith"));
        check(wristReportBlock(noMw, noSwings).toMap().value(QStringLiteral("movesWith")).isNull(), "no movesWith in the payload: none on the page");
        eq(pinpoint::report::wristMoreWords(1, "swing"), QStringLiteral("1 more swing not shown"), "the ending, one");

        // the paper words (the presenter's, Stage C): a camera-only session's caption names the band alone and its
        // one trust line says why; a middle half of one value is "all at"; no line names the Auto control
        QVariantMap cam = payload;
        cam.insert(QStringLiteral("caption"), QStringLiteral("Shaded: the Neutral band."));
        cam.insert(QStringLiteral("scoreLine"), QStringLiteral("Wrist score median 100, middle half all at 100"));
        cam.insert(QStringLiteral("styleLine"), QString());
        cam.insert(QStringLiteral("trust"), QVariantMap{ { QStringLiteral("lines"), QStringList{
            QStringLiteral("Trail wrist: from the face-on camera on every swing, confidence ●●○○ (the median)."),
            QStringLiteral("No swing reads the lead wrist's bow / cup, so every swing is graded against the Neutral band.") } } });
        const QVariantMap cb = wristReportBlock(cam, noSwings).toMap();
        eq(cb.value(QStringLiteral("profile")).toMap().value(QStringLiteral("caption")).toString(), QStringLiteral("Shaded: the Neutral band."),
           "camera-only: the caption names the band alone");
        eq(cb.value(QStringLiteral("scoreLine")).toString(), QStringLiteral("Wrist score median 100, middle half all at 100"), "a one-value middle half, verbatim");
        eq(cb.value(QStringLiteral("styleLine")).toString(), QString(), "no index: no style line");
        bool noAuto = true;
        for (const QString &l : cb.value(QStringLiteral("trust")).toStringList()) noAuto = noAuto && !l.contains(QStringLiteral("Auto"));
        check(noAuto && cb.value(QStringLiteral("trust")).toStringList().size() == 2, "camera-only: two trust lines, neither names the Auto control");
    }

    std::printf("an edit in the Diagnostic Model reaches the next report (#60): goal, an attached drill, a gate's words\n");
    {
        namespace A = pinpoint::analysis;
        const QString habit   = QStringLiteral("s_posture");             // a work-on with a page: goal, cue, check, picture, drill.postureRod
        const QString drillId = QStringLiteral("drill.reportTestWallTilt");
        const QString goal    = QStringLiteral("Stand tall with a soft curve in your lower back");
        const QString gate    = QStringLiteral("sequence.sightedBand");  // a gate the back page prints, after its subject's words
        const QString subject = QStringLiteral("ks.pelvis");
        const QString clause  = QStringLiteral("a camera square to you cannot see a turn begin; one down the line can");

        // Before anything is written: the report reads what shipped.
        const std::unique_ptr<A::ICharacteristicPackProvider> before = A::makeCharacteristicPackProvider();
        const A::Condition *shippedHabit = before->pack().condition(habit);
        check(shippedHabit && shippedHabit->goal == QLatin1String("Set up with a natural curve in your lower back")
                  && shippedHabit->drills.contains(QStringLiteral("drill.postureRod")) && !shippedHabit->drills.contains(drillId),
              "before: the habit's shipped goal and drills");
        const QString shippedClause = A::gateWordsFor(A::sharedGateWordSet(), gate);
        check(!shippedClause.isEmpty() && shippedClause != clause, "before: the gate's shipped clause");

        // The user's edits, written as the Diagnostic Model's save() writes them: the habit copied whole into
        // the user pack (its copy-on-write) with the goal changed and a drill attached; the drill in the user
        // drill set; one clause in the user gate-word set. Then the caches dropped, as save() drops them.
        A::CharacteristicPack user;
        user.id = QStringLiteral("user");
        user.version = QStringLiteral("1");
        user.schemaVersion = A::kPackSchemaVersion;
        A::Condition edited = shippedHabit ? *shippedHabit : A::Condition{};
        edited.goal = goal;
        edited.drills << drillId;
        user.conditions.push_back(edited);
        QString whyNot;
        check(A::saveUserPack(user, &whyNot), "the user pack saves");

        A::DrillSet drills;
        drills.id = QStringLiteral("user");
        drills.version = QStringLiteral("1");
        A::Drill d;
        d.id = drillId;
        d.label = QStringLiteral("Back to the wall");
        d.instruction = QStringLiteral("Stand with your heels a foot from a wall, hinge until your seat touches it, and feel your lower back stay long.");
        d.targets = QStringLiteral("To feel a natural curve in the lower back at address.");
        drills.drills.push_back(d);
        check(A::saveUserDrillSet(drills, &whyNot), "the user drill set saves");

        A::GateWordSet words;
        words.id = QStringLiteral("user");
        words.version = QStringLiteral("1");
        words.gates.push_back(A::GateWord{ gate, clause });
        check(A::saveUserGateWordSet(words, &whyNot), "the user gate words save");
        if (!whyNot.isEmpty()) std::printf("        %s\n", qPrintable(whyNot));
        A::resetSharedDrillSet();
        A::resetSharedGateWordSet();

        // The next report's three reads, as build() makes them.
        const std::unique_ptr<A::ICharacteristicPackProvider> after = A::makeCharacteristicPackProvider();
        const A::Condition *c = after->pack().condition(habit);
        check(c && c->goal.trimmed() == goal, "the work-on page's title is the edited goal");
        check(c && shippedHabit && c->cue == shippedHabit->cue && c->check == shippedHabit->check, "the habit's other report text is untouched");
        const A::DrillSet &shared = A::sharedDrillSet();
        QStringList labels, instructions;
        if (c)
            for (const QString &did : c->drills)
                if (const A::Drill *dr = shared.drill(did)) { labels << dr->label; instructions << dr->instruction; }
        check(labels.contains(QStringLiteral("Rod along the spine")) && labels.contains(d.label) && instructions.contains(d.instruction),
              "the work-on page's drills: the shipped one and the user's new one, label and instruction");

        const A::GateWordSet &gw = A::sharedGateWordSet();
        eq(A::gateWordsFor(gw, gate), clause, "the gate reads the user's clause");
        eq(notCheckedItem(A::subjectWordsFor(gw, subject), A::gateWordsFor(gw, gate), 3, 10),
           QStringLiteral("When your hips turned fastest: ") + clause + QStringLiteral(" (3 of 10 swings)"),
           "the back page's not-checked line carries the overridden clause");
        eq(A::gateWordsFor(gw, QStringLiteral("sequence.validityHole")), A::gateWordsFor(A::coreGateWordSet(), QStringLiteral("sequence.validityHole")),
           "a gate the user did not touch keeps its shipped clause");

        removeUserLayers();
        QDir().rmdir(QFileInfo(A::userPackPath()).absolutePath());   // only if empty: nothing else of ours is there
        check(A::gateWordsFor(A::sharedGateWordSet(), gate) == shippedClause, "cleaned up: the shipped clause again");
    }

    std::printf("%s (%d failed)\n", g_fail ? "FAILED" : "OK", g_fail);
    return g_fail ? 1 : 0;
}
