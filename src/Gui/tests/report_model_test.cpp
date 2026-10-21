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

// THE SESSION REPORT'S SENTENCES (src/Gui/report/report_words.h, src/Coaching/report_gate_words.h):
// every template with every case written (docs/design/session_report_design.md §9.1 rule 7) — one and
// several, every and almost every, none / one / several — plus the honesty rules of §10 that live in
// words (link grades, counts as swings, no percentage, times against impact, the true minus), the two
// dates (§8.4), and the bugs §9.3 found in the mock, each made impossible here: a count doubling its
// noun, hard-coded number words, a session-level kicker that broke, "Coaches call the habit X"
// repeating the title, "125 %" losing its words, "came down to 2 of 2" on two swings.
//
// The map itself is assembled by SessionReportModel from a live diagnostics model, and is proved on
// real sessions by tools/session-report-mock/dump_report.qml (build/report-build/model-I/): a model test
// over a fixture session would need the whole diagnostics stack linked into a test, which no Gui test
// does today; the probe is the stronger check of that half.

#include "report/report_words.h"
#include "../../Coaching/report_gate_words.h"

#include <QCoreApplication>
#include <QRegularExpression>

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

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

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
    for (int g = 0; g < 4; ++g) {
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

    std::printf("gate words (§9.3 item 5: golfer words, lower case, no digits)\n");
    {
        const QStringList ids{ QStringLiteral("sequence.sightedBand"), QStringLiteral("shot.captureHoles"), QStringLiteral("club3d.planeScatter"),
                               QStringLiteral("rule.diag.noCorridor"), QStringLiteral("lm.noData"), QStringLiteral("body.ballPosition") };
        bool ok = true;
        for (const QString &id : ids) {
            const QString w = pinpoint::report::gateGolferWords(id);
            if (w.isEmpty() || w[0].isUpper() || w.contains(QRegularExpression(QStringLiteral("\\d")))) { ok = false; std::printf("        bad: %s\n", qPrintable(id)); }
        }
        check(ok, "known gates have lower-case, digit-free golfer words");
        check(pinpoint::report::gateGolferWords(QStringLiteral("no.such.gate")).isEmpty(), "an unknown gate has no words (left to the appendix)");
        check(pinpoint::report::gateSubjectWords(QStringLiteral("ks.pelvis")) == QLatin1String("when your hips turned fastest"), "a subject in golfer words");
        check(pinpoint::report::gateSubjectWords(QStringLiteral("ballPosition")).isEmpty(), "a metric subject is the caller's (its measure's golferName)");
    }

    std::printf("the doubled noun cannot happen anywhere a count is built\n");
    check(noDoubledNoun(sawLead(QStringLiteral("x"), swingsOn(6, 6), QString())) && noDoubledNoun(countOf(6, 6))
              && noDoubledNoun(cameWithShown(QStringLiteral("x"), 6, 6)) && noDoubledNoun(causesSpottedItem(QStringLiteral("x"), 6, 6, 1))
              && noDoubledNoun(notMeasuredClause(2, 6, QString())) && noDoubledNoun(todayEarlierHabit(QStringLiteral("x"), 2, 6))
              && noDoubledNoun(ballByline(QStringLiteral("x"), 5, 6))
              && noDoubledNoun(lmAppendixStandfirst(QStringLiteral("x"), 5, 6, {})),
          "no 'swings swings'");

    std::printf("%s (%d failed)\n", g_fail ? "FAILED" : "OK", g_fail);
    return g_fail ? 1 : 0;
}
