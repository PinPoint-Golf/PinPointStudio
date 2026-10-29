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

#pragma once

// EVERY SENTENCE THE SESSION REPORT PRINTS, AS A TEMPLATE WITH ALL ITS CASES WRITTEN
// (docs/design/session_report_design.md §9.1 rule 7, §10). SessionReportModel decides WHAT to say from
// the session (session_report.h's reductions, the ledger, the pack); this header decides HOW it is said,
// and nothing else. It is pure and header-only so report_model_test can walk every variant — one habit
// and five, every swing and almost every, none / one / several — without a session behind it.
//
// THE RULES THESE TEMPLATES KEEP, each one a bug the mock's prototype shipped (§9.3) or a house rule:
//   · A COUNT CARRIES ITS NOUN ONCE. countOf() returns "9 of 10 swings"; a caller never appends "swings"
//     (the mock printed "6 of 6 swings swings"). gridCount() is the one noun-less form, for the grid,
//     whose column already says what is counted.
//   · NO HARD-CODED NUMBER WORDS. "ten swings", "the same five" come from numberWord(), never a literal.
//   · NO PERCENTAGE FOR ANYTHING INFERRED, and no σ-rounding: figures arrive formatted at the model's own
//     decimals; σ is only ever the "±".
//   · TIMES ARE SAID AGAINST IMPACT: "87 ms before impact", never "−87 ms" (timeVsImpact).
//   · THE TRUE MINUS (U+2212) for every negative figure a golfer reads; "-" is reserved for "no value".
//   · EMPTY MEANS ABSENT (§9.1 rule 6): a template with nothing to say returns "", never "none".
//   · LINKS AT THE GRADE THE SESSION EARNED (§10 rule 3): "linked in the coaching model", "together on
//     the same swings… not proof", "went away on the swings where X did" — never cause.
//   · STRINGS MARKED [styled] may carry <i>/<b>/<br>; anything typed by a person or authored in the pack
//     goes through esc() before it is put in one.

#include "../diagnostics/session_spread.h"

#include <QDate>
#include <QLocale>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <vector>

namespace pinpoint::report::words {

using pinpoint::analysis::SpreadCorridor;
using pinpoint::analysis::spreadDecimals;
using pinpoint::analysis::spreadNumber;

// ── Small words ─────────────────────────────────────────────────────────────────────────────────

inline QString minusSign() { return QString(QChar(0x2212)); }

// "no", "one" … "twenty", then figures.
inline QString numberWord(int n)
{
    static const char *const W[] = { "no", "one", "two", "three", "four", "five", "six", "seven", "eight",
        "nine", "ten", "eleven", "twelve", "thirteen", "fourteen", "fifteen", "sixteen", "seventeen",
        "eighteen", "nineteen", "twenty" };
    return (n >= 0 && n <= 20) ? QString::fromLatin1(W[n]) : QString::number(n);
}

inline QString cap1(QString s)
{
    if (!s.isEmpty()) s[0] = s[0].toUpper();
    return s;
}
inline QString low1(QString s)
{
    // Lower-case the first letter of a sentence that is being folded into another — unless the first
    // word is an acronym or a name the pack wrote in capitals ("GC Quad", "OTT").
    if (s.size() >= 2 && s[0].isUpper() && s[1].isUpper()) return s;
    if (!s.isEmpty()) s[0] = s[0].toLower();
    return s;
}

inline QString plural(int n, const QString &one, const QString &many) { return n == 1 ? one : many; }

// "a, b and c".
inline QString andList(const QStringList &xs)
{
    if (xs.size() <= 1) return xs.join(QString());
    return QStringList(xs.mid(0, xs.size() - 1)).join(QStringLiteral(", ")) + QStringLiteral(" and ") + xs.last();
}

// Escape text going into a [styled] string (Qt StyledText): & < >.
inline QString esc(QString s)
{
    s.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
    s.replace(QLatin1Char('<'), QStringLiteral("&lt;"));
    s.replace(QLatin1Char('>'), QStringLiteral("&gt;"));
    return s;
}

// "a" or "an" before a word or figure: "an 8 iron", "an 11-swing session", "a 7 iron", "an iron".
inline QString article(const QString &next)
{
    const QString w = next.trimmed().toLower();
    if (w.isEmpty()) return QStringLiteral("a");
    if (w[0].isDigit()) {
        int n = 0, i = 0;
        while (i < w.size() && w[i].isDigit()) { n = n * 10 + w[i].digitValue(); ++i; }
        const bool an = n == 8 || n == 11 || n == 18 || (n >= 80 && n <= 89) || (n >= 800 && n <= 899);
        return an ? QStringLiteral("an") : QStringLiteral("a");
    }
    static const QString vowels = QStringLiteral("aeio");
    return vowels.contains(w[0]) ? QStringLiteral("an") : QStringLiteral("a");
}

// ── Counts (the noun once, never a percentage) ──────────────────────────────────────────────────

// "1 swing", "10 swings".
inline QString swingsN(int n) { return QString::number(n) + (n == 1 ? QStringLiteral(" swing") : QStringLiteral(" swings")); }

// "9 of 10 swings"; "1 of 1 swing". The whole phrase: nobody appends a noun to it.
inline QString countOf(int fired, int judged)
{
    return QStringLiteral("%1 of %2").arg(fired).arg(judged) + (judged == 1 ? QStringLiteral(" swing") : QStringLiteral(" swings"));
}

// The grid's own count, its noun in the column head: "9 of 10".
inline QString gridCount(int fired, int judged) { return QStringLiteral("%1 of %2").arg(fired).arg(judged); }

// "on all 10 swings", "on 9 of 10 swings", "on the one swing we could measure". Empty when nothing
// was judged.
inline QString swingsOn(int fired, int judged)
{
    if (judged <= 0) return QString();
    if (fired == judged)
        return judged == 1 ? QStringLiteral("on the one swing we could measure")
                           : QStringLiteral("on all %1 swings").arg(judged);
    return QStringLiteral("on ") + countOf(fired, judged);
}

// A strength's count (§3.2): a movement was clean on the swings it was judged on, an outcome never
// happened. "all 10 swings" / "9 of 10 swings" (the tenth could not be measured); "0 of 10 swings".
inline QString strengthCount(bool outcome, int judged, int swings)
{
    if (outcome) return countOf(0, judged);
    if (judged == swings) return judged == 1 ? QStringLiteral("the one swing") : QStringLiteral("all %1 swings").arg(judged);
    return countOf(judged, swings);
}

// "one habit", "five habits".
inline QString habitsWord(int n) { return numberWord(n) + (n == 1 ? QStringLiteral(" habit") : QStringLiteral(" habits")); }

// ── Dates (§8.4: the session's date and the prepared date, never confused) ─────────────────────

inline QLocale english() { return QLocale(QLocale::English, QLocale::UnitedKingdom); }
inline QString longDate(const QDate &d) { return d.isValid() ? english().toString(d, QStringLiteral("dddd d MMMM yyyy")) : QString(); }
inline QString dayMonthYear(const QDate &d) { return d.isValid() ? english().toString(d, QStringLiteral("d MMMM yyyy")) : QString(); }
inline QString shortDate(const QDate &d) { return d.isValid() ? english().toString(d, QStringLiteral("d MMM yyyy")) : QString(); }

inline QString mastheadDate(const QDate &prepared) { return QStringLiteral("Prepared ") + dayMonthYear(prepared); }
inline QString folioDate(const QDate &prepared)    { return QStringLiteral("Prepared ") + shortDate(prepared); }
inline QString pdfTitle(const QString &golfer, const QDate &session)
{
    return QStringLiteral("Session report — %1 — session of %2").arg(golfer, shortDate(session));
}
// "This report covers your session on … and was prepared on …" — both dates, said once (§8.4).
inline QString aboutDates(const QDate &session, const QDate &prepared)
{
    return QStringLiteral("This report covers your session on %1 and was prepared on %2. If a session is re-analysed, "
                          "the report is prepared again and carries the new date.")
        .arg(longDate(session), dayMonthYear(prepared));
}

// "Session 3 of 3 today" — or "that day" when the report is prepared on a later day. Empty for a
// session alone on its day.
inline QString sessionOfDay(int index, int count, bool preparedSameDay)
{
    if (count <= 1 || index < 1) return QString();
    return QStringLiteral("Session %1 of %2 %3").arg(index).arg(count)
        .arg(preparedSameDay ? QStringLiteral("today") : QStringLiteral("that day"));
}

// The cover kicker: "Session of Tuesday 20 October 2026  ·  7 iron  ·  10 swings  ·  Session 3 of 3 today".
// Empty parts are left out.
inline QString kicker(const QDate &session, const QString &clubs, int swings, const QString &ofDay)
{
    QStringList parts;
    if (session.isValid()) parts << QStringLiteral("Session of ") + longDate(session);
    if (!clubs.isEmpty()) parts << clubs;
    if (swings > 0) parts << swingsN(swings);
    if (!ofDay.isEmpty()) parts << ofDay;
    return parts.join(QStringLiteral("  ·  "));
}
// "Nick Henn  ·  session of 20 Oct 2026  ·  7 iron".
inline QString runningHead(const QString &golfer, const QDate &session, const QString &clubs)
{
    QStringList parts;
    if (!golfer.isEmpty()) parts << golfer;
    if (session.isValid()) parts << QStringLiteral("session of ") + shortDate(session);
    if (!clubs.isEmpty()) parts << clubs;
    return parts.join(QStringLiteral("  ·  "));
}

// A club as a golfer says it: "7 IRON" → "7 iron", "DRIVER" → "driver"; a short all-capitals token
// stays as written ("PW", "3W").
inline QString clubWord(const QString &club)
{
    QStringList out;
    for (const QString &w : club.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        bool shortCode = w.size() <= 3 && w == w.toUpper() && std::any_of(w.begin(), w.end(), [](QChar c) { return c.isDigit(); })
                         && std::any_of(w.begin(), w.end(), [](QChar c) { return c.isLetter(); });
        shortCode = shortCode || (w.size() <= 2 && w == w.toUpper());
        out << (shortCode ? w : w.toLower());
    }
    return out.join(QLatin1Char(' '));
}
// "7 iron", "7 iron and driver", "7 iron, 9 iron and driver".
inline QString clubsWords(const QStringList &clubs)
{
    QStringList w;
    for (const QString &c : clubs) { const QString x = clubWord(c); if (!x.isEmpty() && !w.contains(x)) w << x; }
    return andList(w);
}

// ── The swing's positions, in words (no P-numbers in body text) ────────────────────────────────

// The rule-v4 swing position (work_ons.h: P1 = 1 … P10 = 10, takeaway 1.5, transition 4.5, downswing
// 5.5, max speed 6.5, release 7.5, the session 11) as the kicker's code: "P4", or "" between positions.
inline QString phaseCode(double pos)
{
    if (pos >= 11.0 || pos < 1.0) return QString();
    const double r = std::round(pos);
    return std::fabs(pos - r) < 1e-9 ? QStringLiteral("P%1").arg(int(r)) : QString();
}

// "at the top", "coming down", "over the session".
inline QString phaseWords(double pos)
{
    if (pos >= 11.0) return QStringLiteral("over the session");
    if (pos < 1.25) return QStringLiteral("at address");
    if (pos < 3.75) return QStringLiteral("going back");
    if (pos < 4.25) return QStringLiteral("at the top");
    if (pos < 4.75) return QStringLiteral("at the start of the downswing");
    if (pos < 6.75) return QStringLiteral("coming down");
    if (pos < 7.25) return QStringLiteral("at impact");
    if (pos < 9.75) return QStringLiteral("after impact");
    return QStringLiteral("at the finish");
}

// The part of the swing a headline names: "We start with your backswing".
inline QString partWords(double pos)
{
    if (pos >= 11.0) return QStringLiteral("your timing");
    if (pos < 1.25) return QStringLiteral("your set-up");
    if (pos < 4.25) return QStringLiteral("your backswing");      // the top included: "lost by the top" is the backswing's
    if (pos < 4.75) return QStringLiteral("your transition");
    if (pos < 6.75) return QStringLiteral("your downswing");
    if (pos < 7.25) return QStringLiteral("impact");
    if (pos < 9.75) return QStringLiteral("your follow-through");
    return QStringLiteral("your finish");
}

// A still's caption for a position: "Address", "Top", "Impact".
inline QString positionCaption(const QString &phase)
{
    const QString p = phase.trimmed().toUpper();
    if (p == QLatin1String("P1"))  return QStringLiteral("Address");
    if (p == QLatin1String("P2"))  return QStringLiteral("Going back");
    if (p == QLatin1String("P3"))  return QStringLiteral("Halfway back");
    if (p == QLatin1String("P4"))  return QStringLiteral("Top");
    if (p == QLatin1String("P5") || p == QLatin1String("P6")) return QStringLiteral("Coming down");
    if (p == QLatin1String("P7"))  return QStringLiteral("Impact");
    if (p == QLatin1String("P8") || p == QLatin1String("P9")) return QStringLiteral("After impact");
    if (p == QLatin1String("P10")) return QStringLiteral("Finish");
    return QString();
}

// ── Figures ─────────────────────────────────────────────────────────────────────────────────────

// A number with its unit as the report prints it: "22°", "11% hand rise", "125% shoulder width",
// "9 cm", "12.6×" (a ratio), "1.33" (no unit). "-" stays "-".
inline QString withUnit(const QString &number, const QString &unit)
{
    if (number == QLatin1String("-") || unit.isEmpty()) return number;
    if (unit.startsWith(QChar(0x00B0))) return number + unit;
    if (unit.startsWith(QLatin1Char('%'))) {
        const QString rest = unit.mid(1).trimmed();
        return number + QLatin1Char('%') + (rest.isEmpty() ? QString() : QLatin1Char(' ') + rest);
    }
    if (unit == QLatin1String("ratio") || unit == QLatin1String("x") || unit == QString(QChar(0x00D7)))
        return number + QChar(0x00D7);
    return number + QLatin1Char(' ') + unit;
}

// A golfer-facing figure: raw × golferSign (never abs() — a habit's swing that went the other way must
// read the other way), at the model's own decimals, the true minus, "-" for none.
inline QString golferFigure(double raw, int decimals, int golferSign = 1)
{
    if (!std::isfinite(raw)) return QStringLiteral("-");
    return spreadNumber(raw * (golferSign < 0 ? -1.0 : 1.0), std::max(0, decimals));
}
inline QString golferValue(double raw, const QString &unit, int decimals, int golferSign = 1)
{
    return withUnit(golferFigure(raw, decimals, golferSign), unit);
}

// AN INSTANT AGAINST IMPACT (ms, negative = before): "87 ms before impact", "12 ms after impact",
// "at impact" within half a millisecond. Never "−87 ms".
inline QString timeVsImpact(double ms)
{
    if (!std::isfinite(ms)) return QStringLiteral("-");
    const long x = std::lround(ms);
    if (x == 0) return QStringLiteral("at impact");
    return QStringLiteral("%1 ms %2").arg(std::labs(x)).arg(x < 0 ? QStringLiteral("before impact") : QStringLiteral("after impact"));
}
// AN INSTANT, said against impact, is a measure in ms whose golfer words name a moment ("when your hips
// reached full speed"); a DURATION in ms ("how long before the ball the club stopped speeding up") is a
// plain figure. The words decide, because the unit is the same.
inline bool isTimeUnit(const QString &unit) { return unit == QLatin1String("ms"); }
inline bool isInstantVsImpact(const QString &unit, const QString &golferName)
{
    return isTimeUnit(unit) && golferName.trimmed().startsWith(QLatin1String("when "), Qt::CaseInsensitive);
}

// A unit in words, for a chart's caption: "degrees", "percent of stance width", "centimetres".
inline QString unitWords(const QString &unit)
{
    if (unit.isEmpty()) return QString();
    if (unit == QStringLiteral("°")) return QStringLiteral("degrees");
    if (unit == QStringLiteral("°/s")) return QStringLiteral("degrees a second");
    if (unit == QLatin1String("cm")) return QStringLiteral("centimetres");
    if (unit == QLatin1String("mm")) return QStringLiteral("millimetres");
    if (unit == QLatin1String("in")) return QStringLiteral("inches");
    if (unit == QLatin1String("ms")) return QStringLiteral("milliseconds from impact");
    if (unit == QLatin1String("mph")) return QStringLiteral("miles an hour");
    if (unit == QLatin1String("yd")) return QStringLiteral("yards");
    if (unit == QLatin1String("rpm")) return QStringLiteral("revolutions a minute");
    if (unit == QLatin1String("ratio")) return QStringLiteral("multiples of the professionals' spread");
    if (unit.startsWith(QLatin1Char('%'))) {
        const QString rest = unit.mid(1).trimmed();
        if (rest.isEmpty()) return QStringLiteral("percent");
        return rest.contains(QLatin1String("width")) ? QStringLiteral("percent of ") + rest : QStringLiteral("percent ") + rest;
    }
    return unit;
}

// THE TARGET RANGE IN WORDS, in the golfer's sign (session_report.h's rangeWords rule — the pass band's
// edges, the open side never named, mirrored for golferSign −1 — printed with this header's units):
// "12° or less", "1.3 or more", "18° to 58°", "−6 to 6 cm", "9% hand rise or less". An instant (vsImpact)
// is said against impact: "no later than 20 ms before impact". Empty when there is no range.
inline QString targetWords(const SpreadCorridor &c, const QString &unit, int golferSign = 1, bool vsImpact = false)
{
    if (!c.known) return QString();
    double plo = c.signalLo, phi = c.signalHi;
    bool lowOpen = c.lowOpen, highOpen = c.highOpen;
    if (!(phi - plo > 0.0) || !std::isfinite(phi - plo)) return QString();
    if (golferSign < 0) { const double t = plo; plo = -phi; phi = -t; std::swap(lowOpen, highOpen); }
    if (lowOpen && highOpen) return QString();
    const int d = spreadDecimals(std::max(c.faultHi - c.faultLo, c.signalHi - c.signalLo));
    if (vsImpact) {
        if (lowOpen)  return QStringLiteral("no later than ") + timeVsImpact(phi);
        if (highOpen) return QStringLiteral("no earlier than ") + timeVsImpact(plo);
        return QStringLiteral("between %1 and %2").arg(timeVsImpact(plo), timeVsImpact(phi));
    }
    auto n = [&](double v) { return spreadNumber(v, d, /*trimZeros*/ true); };
    if (lowOpen)  return withUnit(n(phi), unit) + QStringLiteral(" or less");
    if (highOpen) return withUnit(n(plo), unit) + QStringLiteral(" or more");
    if (unit.startsWith(QChar(0x00B0)) || unit.startsWith(QLatin1Char('%')) || unit == QLatin1String("ratio"))
        return withUnit(n(plo), unit) + QStringLiteral(" to ") + withUnit(n(phi), unit);
    return withUnit(n(plo) + QStringLiteral(" to ") + n(phi), unit);
}

// ── The cover ───────────────────────────────────────────────────────────────────────────────────

// How often the per-swing habits showed, from the LEAST frequent of them (fired / judged): every one on
// every swing → "every swing"; none below 80 % → "almost every swing"; none below half → "most swings";
// else "many of your swings".
inline QString everyWords(double leastShare)
{
    if (leastShare >= 1.0 - 1e-9) return QStringLiteral("every swing");
    if (leastShare >= 0.8)        return QStringLiteral("almost every swing");
    if (leastShare >= 0.5)        return QStringLiteral("most swings");
    return QStringLiteral("many of your swings");
}

// THE HEADLINE (§3, §3.1). perSwing habits judged swing by swing, sessionLevel read over the session;
// `leastShare` as everyWords(); `startPart` partWords() of work-on 1; `firstGoal` its goal.
//   "Five habits showed on almost every swing. We start with your backswing: keep the flex in your trail knee."
//   "One habit showed on every swing. We start with …"
//   "Five habits showed: four on almost every swing, and one over the whole session. We start with …"
//   "One habit showed over the whole session. We start with your timing: …"
inline QString headline(int perSwing, int sessionLevel, double leastShare, const QString &startPart, const QString &firstGoal)
{
    const int n = perSwing + sessionLevel;
    if (n <= 0) return QString();
    QString s;
    if (sessionLevel == 0)
        s = cap1(habitsWord(n)) + QStringLiteral(" showed on ") + everyWords(leastShare) + QLatin1Char('.');
    else if (perSwing == 0)
        s = cap1(habitsWord(n)) + QStringLiteral(" showed over the whole session.");
    else
        s = cap1(habitsWord(n)) + QStringLiteral(" showed: %1 on %2, and %3 over the whole session.")
                .arg(numberWord(perSwing), everyWords(leastShare), numberWord(sessionLevel));
    if (!firstGoal.isEmpty())
        s += QStringLiteral(" We start with %1: %2.").arg(startPart, low1(firstGoal));
    return s;
}

// A clean session (§3.1): nothing repeated often enough to call a habit.
inline QString headlineClean(int watching)
{
    return watching > 0
        ? QStringLiteral("Nothing repeated often enough to call a habit. Below: what went well, and what showed now and then — worth keeping an eye on.")
        : QStringLiteral("Nothing repeated often enough to call a habit. Below: what went well.");
}

// Fewer than three swings (§3.1): a short note, not a report.
inline QString headlineNote(int swings, bool launchMonitor)
{
    QString s = cap1(numberWord(swings)) + (swings == 1 ? QStringLiteral(" swing is") : QStringLiteral(" swings is"))
              + QStringLiteral(" too few to call a habit, so this is a short note rather than a full report");
    return s + (launchMonitor ? QStringLiteral(": here is what the ball did.") : QStringLiteral("."));
}

// ── Swing by swing (page 2) ─────────────────────────────────────────────────────────────────────

inline QString gridTitle(bool everyOnEverySwing, int perSwingHabits, bool clean)
{
    if (clean) return QStringLiteral("What we measured, swing by swing");
    if (everyOnEverySwing && perSwingHabits > 1)
        return QStringLiteral("The same %1, every swing").arg(numberWord(perSwingHabits));
    return QStringLiteral("Your habits, swing by swing");
}

inline QString gridStandfirst(int swings, bool anyStub)
{
    QString s = QStringLiteral("Each row is one thing we measured; each column is ")
              + (swings == 1 ? QStringLiteral("your one swing") : QStringLiteral("one of your %1 swings, in the order you hit them").arg(numberWord(swings)))
              + QStringLiteral(". A filled dot means it showed on that swing.");
    if (anyStub) s += QStringLiteral(" A short stub means it could not be measured on that swing.");
    return s;
}

inline QString gridGroupTitle(const QString &key, int workOns)
{
    if (key == QLatin1String("habit"))
        return workOns == 1 ? QStringLiteral("Your habit — the one work-on")
                            : QStringLiteral("Your habits — the %1 work-ons").arg(numberWord(workOns));
    if (key == QLatin1String("seen")) return QStringLiteral("Seen, not yet habits");
    if (key == QLatin1String("ball")) return QStringLiteral("What the ball did");
    return QString();
}

// A group's ending past its cap: "and 2 more seen on fewer swings". Empty for none.
inline QString gridMore(const QString &key, int more)
{
    if (more <= 0) return QString();
    if (key == QLatin1String("habit"))
        return QStringLiteral("and %1 more %2, not on the list today").arg(numberWord(more), plural(more, QStringLiteral("habit"), QStringLiteral("habits")));
    if (key == QLatin1String("seen")) return QStringLiteral("and %1 more seen on fewer swings").arg(numberWord(more));
    return QStringLiteral("and %1 more the ball did on fewer swings").arg(numberWord(more));
}

// "Sequence not repeatable is read over the whole session, not swing by swing: see work-on 5."
inline QString sessionLevelGridNote(const QString &name, int rank)
{
    return QStringLiteral("%1 is read over the whole session, not swing by swing: see work-on %2.").arg(name).arg(rank);
}

// THE REFERENCE SWING (§6): "Swing 3 came closest to your targets: a 148 yard carry at 105 mph ball speed."
// "your longest" only when it was. `inside` / `judgedOn`: how many work-ons it sat inside the range on.
inline QString referenceText(int swing, bool haveBall, const QString &carry, const QString &ballSpeed, bool longest,
                             int inside, int judgedOn)
{
    QString s = QStringLiteral("Swing %1 came closest to your targets").arg(swing);
    if (haveBall && carry != QLatin1String("-")) {
        const QString ball = ballSpeed == QLatin1String("-") ? QString() : QStringLiteral(" at %1 mph ball speed").arg(ballSpeed);
        s += longest ? QStringLiteral(" — and it was your longest, a %1 yard carry%2.").arg(carry, ball)
                     : QStringLiteral(": a %1 yard carry%2.").arg(carry, ball);
    } else {
        s += QLatin1Char('.');
    }
    if (judgedOn > 0) {
        if (inside == judgedOn)
            s += judgedOn == 1 ? QStringLiteral(" It was inside the target range for your work-on.")
                               : QStringLiteral(" It was inside the target range for every work-on we could measure on it.");
        else if (inside > 0)
            s += QStringLiteral(" It was inside the target range for %1 of the %2 work-ons we could measure on it.")
                     .arg(numberWord(inside), numberWord(judgedOn));
        else
            s += QStringLiteral(" No swing was inside every target range; this one missed by least.");
    }
    if (longest && haveBall) s += QStringLiteral(" Keep it in mind: it is the swing the work-ons are moving you toward.");
    return s;
}

// ── Earlier today ───────────────────────────────────────────────────────────────────────────────

// "Session 3 · 10 swings".
inline QString todayColumn(int index, int swings) { return QStringLiteral("Session %1 · %2").arg(index).arg(swingsN(swings)); }
// A cell: "habit 8/8" for a habit, "4/10" otherwise, "-" when not measured; a session-level habit has no
// per-swing counts — "habit" or "".
inline QString todayCell(bool pattern, int fired, int judged, bool sessionLevel)
{
    if (sessionLevel) return pattern ? QStringLiteral("habit") : QString();
    if (judged <= 0) return QStringLiteral("-");
    return (pattern ? QStringLiteral("habit ") : QString()) + QStringLiteral("%1/%2").arg(fired).arg(judged);
}
// What was already there in the first session of the day: "Five of today's habits were already there in
// your first session." — every case.
inline QString todayAlready(int already, int habits)
{
    if (habits <= 0) return QString();
    if (already <= 0)
        return habits == 1 ? QStringLiteral("Today's habit was not a habit in your first session.")
                           : QStringLiteral("None of today's habits was a habit in your first session.");
    if (already == habits)
        return habits == 1 ? QStringLiteral("Today's habit was already there in your first session.")
                           : QStringLiteral("All %1 of today's habits were already there in your first session.").arg(numberWord(habits));
    return QStringLiteral("%1 of today's habits %2 already there in your first session.")
        .arg(cap1(numberWord(already)), already == 1 ? QStringLiteral("was") : QStringLiteral("were"));
}
// An earlier habit, as this session saw it: "came down to 4 of 10 here"; judged on fewer than three:
// "this session could only judge it on two swings, too few to say" (§9.3 — never "eased" on two swings).
inline QString todayEarlierHabit(const QString &name, int fired, int judged, int minJudged = 3)
{
    if (judged < minJudged)
        return judged <= 0
            ? QStringLiteral("%1 was a habit earlier today; this session could not measure it.").arg(name)
            : QStringLiteral("%1 was a habit earlier today; this session could only judge it on %2, too few to say.")
                  .arg(name, judged == 1 ? QStringLiteral("one swing") : numberWord(judged) + QStringLiteral(" swings"));
    if (fired == 0)
        return QStringLiteral("%1 was a habit earlier today and did not show here (%2).").arg(name, countOf(0, judged));
    return QStringLiteral("%1 was a habit earlier today and came down to %2 here.").arg(name, countOf(fired, judged));
}
// Past the six-session cap: "3 earlier sessions not shown".
inline QString todayMore(int more)
{
    if (more <= 0) return QString();
    return more == 1 ? QStringLiteral("1 earlier session not shown") : QStringLiteral("%1 earlier sessions not shown").arg(more);
}

// ── Why this order (page 3, §4.2) ───────────────────────────────────────────────────────────────

inline QString orderIntro(int rules)
{
    return QStringLiteral("Your list is ordered by %1 rules, applied in turn. Here is each rule and what it did today.").arg(numberWord(rules));
}

inline QString ruleStillHappeningTitle() { return QStringLiteral("What is still happening comes first"); }
inline QString ruleStillHappeningBody()  { return QStringLiteral("A habit you started fixing during the session goes to the back of the list."); }
inline QString ruleStillHappeningToday(int resolving, int listed)
{
    if (resolving <= 0)
        return listed == 1 ? QStringLiteral("Your habit did not ease during the session, so this rule changed nothing today.")
                           : QStringLiteral("None of the %1 eased during the session, so this rule changed nothing today.").arg(numberWord(listed));
    return resolving == 1 ? QStringLiteral("One eased during the session and moved to the back.")
                          : QStringLiteral("%1 eased during the session and moved to the back.").arg(cap1(numberWord(resolving)));
}

inline QString ruleCauseFirstTitle() { return QStringLiteral("Causes before the things they cause"); }
inline QString ruleCauseFirstBody()
{
    return QStringLiteral("When one habit sets up another, working on the first often fixes the second for free. We only put one "
                          "behind another when your swings show it: the second goes away on the swings where the first does.");
}
// why: 0 testable, 1 no links, 2 the cause on every swing, 3 too few swings (session_report.h LinksUntestable).
inline QString ruleCauseFirstToday(int why, int behindCause)
{
    switch (why) {
    case 0:
        if (behindCause > 0)
            return behindCause == 1 ? QStringLiteral("One habit waits behind another today: on the swings where the first did not show, the second went away too.")
                                    : QStringLiteral("%1 habits wait behind another today: on the swings where the first did not show, the second went away too.").arg(cap1(numberWord(behindCause)));
        return QStringLiteral("Your swings could test it, and no habit went away on the swings where another did, so this rule changed nothing today.");
    case 1:
        return QStringLiteral("None of today's habits is linked to another in the coaching model, so this rule had nothing to do.");
    case 2:
        return QStringLiteral("It could not be tested today. The habits that could set up another showed on every swing, so there is no swing without one to compare against.");
    default:
        return QStringLiteral("It could not be tested today: too few swings had both habits measured to compare.");
    }
}

inline QString ruleSwingOrderTitle() { return QStringLiteral("Then the order of your swing"); }
inline QString ruleSwingOrderBody()
{
    return QStringLiteral("When your swings cannot show which habit sets up which, the one that comes earliest in the swing goes "
                          "first: what happens early tends to shape what follows.");
}
inline QString ruleSwingOrderToday(int decided)
{
    return decided > 0 ? QStringLiteral("This is what set today's order: from the earliest point in the swing to the latest.")
                       : QStringLiteral("Today's habits sit at the same point in the swing, so this rule changed nothing.");
}

inline QString ruleBiggestMissTitle() { return QStringLiteral("Then the biggest miss first"); }
inline QString ruleBiggestMissBody()
{
    return QStringLiteral("Each habit is measured against a target range. The one that missed by most, for its range, comes first.");
}
// swingOrderRan: rule v4 was in force (no link testable). decided: places this rule settled.
inline QString ruleBiggestMissToday(bool swingOrderRan, int decided)
{
    if (decided <= 0)
        return swingOrderRan ? QStringLiteral("No two of today's habits sit at the same point in the swing, so this rule changed nothing.")
                             : QStringLiteral("Nothing was left for it to decide today.");
    if (swingOrderRan)
        return QStringLiteral("It settled the order between habits at the same point in the swing. The bars below show how far each one missed.");
    return QStringLiteral("This is what set today's order. The bars below show how far each one missed.");
}

inline QString ruleMovementsFirstTitle() { return QStringLiteral("Movements before numbers"); }
inline QString ruleMovementsFirstBody()
{
    return QStringLiteral("A club or ball number joins the list only when there are fewer than three movements to work on, and it goes after them.");
}
inline QString ruleMovementsFirstToday(int added)
{
    return added == 1 ? QStringLiteral("One number joined the list this way today.")
                      : QStringLiteral("%1 numbers joined the list this way today.").arg(cap1(numberWord(added)));
}

// The bars' caption — what a bar measures, and whether it set today's order. mode: 0 the bars set it
// (links testable), 1 swing order set it and the bars only broke ties, 2 swing order alone.
inline QString barsNote(int mode)
{
    const QString what = QStringLiteral("Bar: how far outside its target range each habit went, compared with the range’s own width.");
    if (mode == 0) return what + QStringLiteral(" The longest bar goes first.");
    if (mode == 1) return what + QStringLiteral(" Where two habits sit at the same point in the swing, the longer bar goes first.");
    return what + QStringLiteral(" Today the order of your swing set the list, so the longest bar is not necessarily first.");
}

// The timeline's caption. firstIsEarliest from workOnTimeline(); `why` when it is not: 0 links (a cause
// your swings support), 1 magnitude; sessionLevelNames: habits with no place on the swing.
inline QString timelineNote(bool any, bool firstIsEarliest, int why, const QStringList &sessionLevelNames)
{
    QString s;
    if (any) {
        if (firstIsEarliest)
            s = QStringLiteral("Your first work-on is also the earliest in the swing. Habits early in the swing tend to set up the ones "
                               "after them, which is one more reason to start there.");
        else
            s = QStringLiteral("Your first work-on is not the earliest in the swing; it leads because %1. Your coach may still choose to "
                               "start earlier in the swing.")
                    .arg(why == 0 ? QStringLiteral("your swings show it setting up another") : QStringLiteral("it missed by most"));
    }
    if (!sessionLevelNames.isEmpty()) {
        if (!s.isEmpty()) s += QLatin1Char(' ');
        s += andList(sessionLevelNames) + (sessionLevelNames.size() == 1 ? QStringLiteral(" is") : QStringLiteral(" are"))
           + QStringLiteral(" read over the whole session, so ") + (sessionLevelNames.size() == 1 ? QStringLiteral("it has") : QStringLiteral("they have"))
           + QStringLiteral(" no place on it.");
    }
    return s;
}

// A link between two listed habits, at the grade the session earned (§10 rule 3).
// grade: 0 supported (the effect went away where the cause did), 1 testable, not shown, 2 untestable —
// the cause on every swing, 3 untestable — too few swings together, 4 untestable — the two are measured
// from the same readings (Edge::sharedEvidence; the panel's "so the link is not tested").
inline QString linkWords(int grade, const QString &cause, const QString &effect, int bothJudgedWithoutCause = 0,
                         int effectGoneWithoutCause = 0)
{
    switch (grade) {
    case 0:
        return QStringLiteral("On the swings where %1 did not show, %2 went away too (%3), so it waits behind it. Together on the same "
                              "swings is still not proof that one causes the other.")
            .arg(low1(cause), low1(effect), countOf(effectGoneWithoutCause, bothJudgedWithoutCause));
    case 1:
        return QStringLiteral("Linked in the coaching model, but your swings did not show it: %1 did not go away on the swings where %2 did.")
            .arg(low1(effect), low1(cause));
    case 2:
        return QStringLiteral("Linked in the coaching model. This session cannot test it, because %1 showed on every swing.").arg(low1(cause));
    case 4:
        return QStringLiteral("Linked in the coaching model. %1 and %2 are measured from the same readings, so your swings cannot test it.")
            .arg(cap1(cause), low1(effect));
    default:
        return QStringLiteral("Linked in the coaching model. This session cannot test it: too few swings had both measured.");
    }
}

// Not on the list, and why.
inline QString notListedResult(const QString &linkedName, int linkedRank)
{
    if (!linkedName.isEmpty())
        return QStringLiteral("A result, not a movement to practice: the coaching model links it to %1 (work-on %2). Work on that, "
                              "and this may follow.").arg(low1(linkedName)).arg(linkedRank);
    return QStringLiteral("A result, not a movement to practice. It is what the club or ball did, and it changes when the movements behind it do.");
}
inline QString notListedOverCap(int cap)
{
    return QStringLiteral("A habit too, but the list stops at %1. It moves up when one of these clears.").arg(numberWord(cap));
}
inline QString notListedBallWatch()
{
    return QStringLiteral("Something the ball did, not a movement to practice. It is on the watch list until it repeats often enough to call a habit.");
}
inline QString notListedName(const QString &name, int fired, int judged) { return QStringLiteral("%1 (%2)").arg(name, gridCount(fired, judged)); }

// Worth checking next time: one title per kind. check: 1 physical test, 2 coach can look, 3 hidden today
// (session_report.h CouldBeCheck). everyBehindTwo: every item sits behind two or more work-ons.
inline QString checksTitle(int check, bool everyBehindTwo)
{
    if (check == 1)
        return everyBehindTwo ? QStringLiteral("A quick physical test would tell. Each could sit behind two or more of your work-ons:")
                              : QStringLiteral("A quick physical test would tell:");
    if (check == 2) return QStringLiteral("Your coach can look; the cameras can't see these yet:");
    return QStringLiteral("Hidden on today's swings; worth another look next time:");
}
// "Lead-hip mobility (3, 4)".
inline QString checksItem(const QString &name, const QVector<int> &ranks)
{
    QStringList r;
    for (int x : ranks) r << QString::number(x);
    return r.isEmpty() ? name : QStringLiteral("%1 (%2)").arg(name, r.join(QStringLiteral(", ")));
}
inline QString checksMore(int more)
{
    if (more <= 0) return QString();
    return QStringLiteral("and %1 more on the work-on pages").arg(numberWord(more));
}

// ── The work-on page ────────────────────────────────────────────────────────────────────────────

// "Work-on 1 of 5  ·  P4, at the top  ·  10 of 10 swings"; read over the session: "Work-on 5 of 5  ·
// Over the whole session".
inline QString workOnKicker(int rank, int of, double swingPos, int fired, int judged)
{
    const QString head = QStringLiteral("Work-on %1 of %2").arg(rank).arg(of);
    if (swingPos >= 11.0) return head + QStringLiteral("  ·  Over the whole session");
    const QString code = phaseCode(swingPos), where = phaseWords(swingPos);
    const QString pos = code.isEmpty() ? cap1(where) : code + QStringLiteral(", ") + where;
    return head + QStringLiteral("  ·  ") + pos + (judged > 0 ? QStringLiteral("  ·  ") + countOf(fired, judged) : QString());
}

// [styled] "Coaches call the habit <i>Trail knee straightens</i>, or “standing up on it going back”."
// Empty when the page's title already IS the coach's term and there is no alias (§9.3: the mock repeated
// the title).
inline QString coachTerm(const QString &label, const QString &goal, const QStringList &aliases)
{
    QStringList a;
    for (const QString &x : aliases.mid(0, 2)) if (!x.trimmed().isEmpty()) a << QStringLiteral("“%1”").arg(esc(x.trimmed()));
    if (a.isEmpty() && goal.trimmed().compare(label.trimmed(), Qt::CaseInsensitive) == 0) return QString();
    if (a.isEmpty()) return QStringLiteral("Coaches call the habit <i>%1</i>.").arg(esc(label));
    return QStringLiteral("Coaches call the habit <i>%1</i>, or %2.").arg(esc(label), a.join(QStringLiteral(", ")));
}
inline QString aliasesWords(const QStringList &aliases)
{
    QStringList a;
    for (const QString &x : aliases.mid(0, 2)) if (!x.trimmed().isEmpty()) a << QStringLiteral("“%1”").arg(x.trimmed());
    return a.join(QStringLiteral(", "));
}

// The run's direction. trend: Trend enum as int (0 improving, 1 stable, 2 worsening), `known` whether the
// test was significant, `points` the assessable swings it ran on.
inline QString trendWords(bool known, int trend, int points)
{
    if (known && trend == 0) return QStringLiteral("easing as the session went on");
    if (known && trend == 2) return QStringLiteral("growing as the session went on");
    if (points >= 6) return QStringLiteral("steady from first swing to last");
    return QString();
}

// "Your trail leg straightens going back — on all 10 swings, steady from first swing to last."
inline QString sawLead(const QString &golfer, const QString &swingsOnWords, const QString &trend)
{
    QString s = cap1(golfer);
    if (!swingsOnWords.isEmpty()) s += QStringLiteral(" — ") + swingsOnWords;
    if (!trend.isEmpty()) s += (swingsOnWords.isEmpty() ? QStringLiteral(" — ") : QStringLiteral(", ")) + trend;
    return s + QLatin1Char('.');
}
// A habit that stopped during the session (§3.1): its page leads with the change.
inline QString sawResolving(const QString &golfer, int fired, int judged, int stoppedAfter, int quietSince)
{
    const QString since = quietSince == 1 ? QStringLiteral("your last swing did not show it")
                                          : QStringLiteral("none of your last %1 swings showed it").arg(numberWord(quietSince));
    return QStringLiteral("%1 — %2, but it stopped after swing %3: %4.").arg(cap1(golfer), swingsOn(fired, judged)).arg(stoppedAfter).arg(since);
}
// "Typical trail-knee bend lost by the top: 22°, against a target of 12° or less; this is a clear miss…"
inline QString typicalClause(const QString &measureName, const QString &value, const QString &target, const QString &miss)
{
    if (value.isEmpty() || value == QLatin1String("-")) return QString();
    QString s = QStringLiteral("Typical %1: %2").arg(measureName, value);
    if (!target.isEmpty()) s += QStringLiteral(", against a target of ") + target;
    if (!target.isEmpty() && !miss.isEmpty()) s += QStringLiteral("; this is ") + miss;
    return s + QLatin1Char('.');
}
// A habit firing on the LOW tail of a measure whose words read upward (golferSign +1) with a value below
// zero would print "lead-wrist bow added by impact: −4°". The magnitude, with the measure's own words for
// the low side, says it the right way round: "4° the other way (your lead wrist is more cupped at impact)".
inline QString lowTailValue(const QString &magnitude, const QString &golferLow)
{
    if (golferLow.isEmpty()) return magnitude;
    return QStringLiteral("%1 the other way (%2)").arg(magnitude, golferLow);
}
// "It could not be measured on 3 of 10 swings: the camera could not find the ball clearly at address."
inline QString notMeasuredClause(int unmeasured, int swings, const QString &reason)
{
    if (unmeasured <= 0) return QString();
    QString s = QStringLiteral("It could not be measured on %1").arg(countOf(unmeasured, swings));
    if (!reason.isEmpty()) s += QStringLiteral(": ") + reason;
    return s + QLatin1Char('.');
}
// The session-level habit's opening (read over the session, never one swing).
inline QString sawSessionLevel(const QString &golfer) { return cap1(golfer) + QStringLiteral(" — read over the whole session, never one swing."); }
// "Your lead arm's peak timing varied 12.6× ± 4.1 as widely as touring professionals' do."
inline QString timingSpreadSentence(const QString &segment, double value, double sigma)
{
    if (!std::isfinite(value) || value < 0) return QString();
    const QString pmText = (std::isfinite(sigma) && sigma > 0 && std::round(sigma * 10.0) > 0)
                         ? QStringLiteral(" ± ") + QString::number(sigma, 'f', 1) : QString();
    const QString who = segment.isEmpty() ? QStringLiteral("Your") : QStringLiteral("Your ") + segment + QStringLiteral("'s");
    return QStringLiteral("%1 peak timing varied %2×%3 as widely as touring professionals' do.")
        .arg(who, QString::number(value, 'f', 1), pmText);
}
// "In order on 3 of 9 swings" — a COUNT of the swings whose four peaks came hips, chest, arms, club;
// never the inferred share. resolved 0: the order could not be judged.
inline QString orderCountSentence(int inOrder, int resolved)
{
    if (resolved <= 0) return QStringLiteral("The order itself could not be judged: too few swings had all four peaks timed.");
    return QStringLiteral("Your hips, chest, arms and club fired in that order %1.").arg(swingsOn(inOrder, resolved));
}

// The figure's caption: "Each dot is one swing: trail-knee bend lost by the top, in degrees."
inline QString figureLabel(const QString &measureName, const QString &unit)
{
    const QString u = unitWords(unit);
    return QStringLiteral("Each dot is one swing: ") + measureName + (u.isEmpty() ? QString() : QStringLiteral(", in ") + u) + QLatin1Char('.');
}

// [styled] What came with it — the habit's authored effects the session judged (co-occurrence, never
// cause). shown: "club returns toe-up at impact (7 of the 7 swings we could see)"; clean: golferWell
// phrases; cleanEverySwing: each clean one was judged on every swing of the session.
inline QString cameWithText(const QStringList &shown, const QStringList &clean, bool cleanEverySwing)
{
    QStringList out;
    if (!shown.isEmpty())
        out << QStringLiteral("It came with %1. That fits the coaching model, though showing up together is not proof that one causes the other.")
                   .arg(andList(shown));
    if (!clean.isEmpty()) {
        const QString every = cleanEverySwing ? QStringLiteral("on every swing") : QStringLiteral("on every swing we could see");
        out << (shown.isEmpty() ? QStringLiteral("The coaching model says it can bring other habits with it. Not today — %1, %2.")
                                : QStringLiteral("And %1, %2."))
                   .arg(every, andList(clean));
    }
    return out.join(QLatin1Char(' '));
}
// "club returns toe-up at impact (7 of the 7 swings we could see)".
inline QString cameWithShown(const QString &golfer, int fired, int judged)
{
    return QStringLiteral("%1 (%2 of the %3 swings we could see)").arg(low1(golfer)).arg(fired).arg(judged);
}

// THE CAUSES LEAD (§3.3), one per case. lead: 0 none authored, 1 spotted, 2 unchecked, 3 all ruled out
// (session_report.h CauseLead). The groups named are the ones the page prints.
inline QString causesLead(int lead, int spotted, bool supported, bool everySwing, bool couldBe, bool ruledOut, int authored)
{
    QStringList groups;
    if (spotted > 0) groups << QStringLiteral("what we spotted");
    if (couldBe)     groups << QStringLiteral("what we couldn't check");
    if (ruledOut)    groups << QStringLiteral("what we've ruled out");
    const QString here = groups.isEmpty() ? QString() : QStringLiteral("here is ") + andList(groups);
    switch (lead) {
    case 0:
        return QStringLiteral("We couldn't pin down what drives this one today, and the coaching model names no measured cause for it yet. "
                              "Worth talking through with your coach.");
    case 1: {
        const QString who = spotted == 1 ? QStringLiteral("One possible cause showed up on the same swings")
                                         : QStringLiteral("%1 possible causes showed up on the same swings").arg(cap1(numberWord(spotted)));
        if (supported)
            return who + QStringLiteral(", and your swings point to it: on the swings where it did not show, this one went away too. ")
                 + cap1(here) + QLatin1Char('.');
        if (everySwing)
            return who + QStringLiteral(". Because both happened on every swing, we can't prove one drives the other — so ") + here + QLatin1Char('.');
        return who + QStringLiteral(". Showing up together is not proof that one drives the other — so ") + here + QLatin1Char('.');
    }
    case 2:
        return QStringLiteral("We couldn't pin down what drives this one today. Here are the possible causes: the ones we couldn't check")
             + (ruledOut ? QStringLiteral(", and the ones we've ruled out.") : QStringLiteral("."));
    default:
        return authored == 1
            ? QStringLiteral("We checked the one cause we know of for this one, and it was fine — so it may come from something we can't "
                             "yet put a name to. Worth talking through with your coach.")
            : QStringLiteral("We checked every cause we know of for this one, and each was fine — so it may come from something we can't "
                             "yet put a name to. Worth talking through with your coach.");
    }
}
inline QString causesSpottedTitle()  { return QStringLiteral("What we spotted"); }
inline QString causesCouldBeTitle()  { return QStringLiteral("Could be behind it — we couldn't check"); }
inline QString causesRuledOutTitle() { return QStringLiteral("Ruled out — measured, and fine"); }
// check: 1 physical test, 2 coach can look, 3 hidden today.
inline QString causesHow(int check)
{
    if (check == 1) return QStringLiteral("A quick physical test would tell");
    if (check == 2) return QStringLiteral("Your coach can look");
    return QStringLiteral("Hidden on today's swings");
}
// [styled] "Your hands come over the top coming down — on 10 of 10 swings, work-on 4".
inline QString causesSpottedItem(const QString &name, int fired, int judged, int rank)
{
    QString s = esc(name) + QStringLiteral(" — on ") + countOf(fired, judged);
    if (rank > 0) s += QStringLiteral(", work-on %1").arg(rank);
    return s;
}
inline QString causesSpottedNote(bool supported) { return supported ? QStringLiteral("your swings point to it") : QStringLiteral("it fits, but isn't proof"); }
inline QString causesCouldBeMore(int more) { return more > 0 ? QStringLiteral("%1 more, less likely").arg(more) : QString(); }
inline QString causesRuledOutMore(int more) { return more > 0 ? QStringLiteral("%1 more").arg(more) : QString(); }

// Next time: "Trail-knee bend lost by the top: 12° or less, on most swings. Today: 22°."
inline QString targetLine(const QString &measureName, const QString &target, const QString &today, bool sessionLevel)
{
    if (target.isEmpty() || measureName.isEmpty()) return QString();
    QString s = cap1(measureName) + QStringLiteral(": ") + target + (sessionLevel ? QStringLiteral(", over a session.") : QStringLiteral(", on most swings."));
    if (!today.isEmpty() && today != QLatin1String("-")) s += QStringLiteral(" Today: ") + today + QLatin1Char('.');
    return s;
}

// ── The ball, the numbers ───────────────────────────────────────────────────────────────────────

inline QString ballByline(const QString &device, int measured, int swings)
{
    const QString by = device.isEmpty() ? QStringLiteral("the launch monitor") : device;
    return measured >= swings ? QStringLiteral("measured on every swing by ") + by
                              : QStringLiteral("measured on %1 by %2").arg(countOf(measured, swings), by);
}
inline QString ballNote(int swings, bool severalClubs)
{
    if (severalClubs)
        return QStringLiteral("Each club's typical value (the middle of its swings), with its range from lowest to highest.");
    if (swings <= 2)
        return swings == 1 ? QStringLiteral("Your one swing's numbers.")
                           : QStringLiteral("Middle of your two swings in large type; the lower and higher beneath.");
    return QStringLiteral("Typical value (the middle of your %1 swings) in large type; the range from lowest to highest beneath.").arg(numberWord(swings));
}
inline QString ballMore(int moreClubs)
{
    if (moreClubs <= 0) return QString();
    return moreClubs == 1 ? QStringLiteral("1 more club in your numbers") : QStringLiteral("%1 more clubs in your numbers").arg(moreClubs);
}
// The numbers table's note: the sign convention for the golfer's hand, what Habits and Typical mean.
inline QString numbersNote(bool leftHanded, int workOnsPerSwing, int swings, bool habits, bool severalClubs)
{
    QString s = QStringLiteral("Path, face and offline: positive is right of target, negative left (%1-handed golfer).")
                    .arg(leftHanded ? QStringLiteral("left") : QStringLiteral("right"));
    if (habits && workOnsPerSwing > 0)
        s += workOnsPerSwing == 1 ? QStringLiteral(" Habits: whether your work-on showed on that swing.")
                                  : QStringLiteral(" Habits: how many of your %1 work-ons showed on that swing.").arg(numberWord(workOnsPerSwing));
    if (swings >= 3)
        s += severalClubs ? QStringLiteral(" Typical: the middle value of each club's swings.")
                          : QStringLiteral(" Typical: the middle value of your %1 swings.").arg(numberWord(swings));
    return s;
}
// "4 of 5" — how many of the swing-by-swing work-ons showed on this swing.
inline QString habitsCell(int fired, int of) { return gridCount(fired, of); }

// ── The launch monitor appendix ──
// The standfirst: "Every reading the GC Quad took, on 18 of 20 swings: 7 iron and driver."
inline QString lmAppendixStandfirst(const QString &device, int measured, int swings, const QStringList &clubs)
{
    const QString by = device.isEmpty() ? QStringLiteral("the launch monitor") : QStringLiteral("the ") + device;
    const QString on = measured >= swings ? QStringLiteral("on every swing") : QStringLiteral("on %1").arg(countOf(measured, swings));
    const QString cl = clubsWords(clubs);
    return QStringLiteral("Every reading %1 took, %2").arg(by, on) + (cl.isEmpty() ? QString() : QStringLiteral(": ") + cl) + QStringLiteral(".");
}
// A club's headline over its figures: "152 yd average carry, 18 yd from shortest to longest, 18 shots".
inline QString lmClubHeadline(const QString &carry, const QString &spread, int shots)
{
    QStringList parts;
    if (!carry.isEmpty()) parts << carry + QStringLiteral(" yd average carry");
    if (!spread.isEmpty()) parts << spread + QStringLiteral(" yd from shortest to longest");
    parts << (shots == 1 ? QStringLiteral("1 shot") : QStringLiteral("%1 shots").arg(shots));
    return parts.join(QStringLiteral(", "));
}
// Under the two shot tables: the sign convention for the golfer's hand, then what the marks mean.
inline QString lmTablesNote(bool leftHanded, bool anyOut, bool anyRelabelled, const QString &noReading)
{
    QString s = QStringLiteral("As the device reported them. Signed readings: plus is right of the target line (start, side spin, "
                               "spin axis, offline, path, face), up (attack, and lie, toe up), %1 (face to path), toward the toe "
                               "(strike location) and high (strike height); minus the other way.")
                    .arg(leftHanded ? QStringLiteral("closed") : QStringLiteral("open"));
    if (anyOut) s += QStringLiteral(" Out: left out on the launch monitor panel, and in none of the averages.");
    if (anyRelabelled) s += QStringLiteral(" *: listed under the club its readings point to, not the one recorded.");
    if (!noReading.isEmpty()) s += QStringLiteral(" ") + noReading;
    return s;
}
// "No reading on swing 4." / "No reading on swings 4 and 9." / "No reading on swings 1–12 and 15." (runs of three or
// more as a range). `swings` in ascending order.
inline QString lmNoReading(const QList<int> &swings)
{
    if (swings.isEmpty()) return QString();
    QStringList parts;
    for (int i = 0; i < swings.size();) {
        int j = i;
        while (j + 1 < swings.size() && swings[j + 1] == swings[j] + 1) ++j;
        if (j - i >= 2) parts << QString::number(swings[i]) + QChar(0x2013) + QString::number(swings[j]);
        else for (int k = i; k <= j; ++k) parts << QString::number(swings[k]);
        i = j + 1;
    }
    return (swings.size() == 1 ? QStringLiteral("No reading on swing ") : QStringLiteral("No reading on swings ")) + andList(parts) + QStringLiteral(".");
}
inline QString lmStatsNote(bool anyOut)
{
    return QStringLiteral("Over each club's shots") + (anyOut ? QStringLiteral(" not left out") : QString())
         + QStringLiteral(". SD is the standard deviation (n − 1): about two in three shots fall within one SD of the average. "
                          "It needs three shots; below that it shows -. Signs as in the shot tables.");
}

// ── About this report ───────────────────────────────────────────────────────────────────────────

// "Down-the-line and face-on cameras" / "Face-on camera".
inline QString camerasWords(const QStringList &aliases)
{
    QStringList w;
    for (const QString &a : aliases) {
        const QString l = a.toLower();
        QString x = l.contains(QLatin1String("dtl")) || l.contains(QLatin1String("down")) ? QStringLiteral("down-the-line")
                  : l.contains(QLatin1String("face")) ? QStringLiteral("face-on")
                  : l.contains(QLatin1String("impact")) ? QStringLiteral("impact") : l;
        if (!w.contains(x)) w << x;
    }
    if (w.isEmpty()) return QString();
    return cap1(andList(w)) + (w.size() == 1 ? QStringLiteral(" camera") : QStringLiteral(" cameras"));
}
inline QString devicesWords(const QStringList &aliases, const QString &lmDevice)
{
    QStringList parts;
    const QString cams = camerasWords(aliases);
    if (!cams.isEmpty()) parts << cams;
    if (!lmDevice.isEmpty()) parts << lmDevice + QStringLiteral(" launch monitor");
    return parts.join(QStringLiteral(" · "));
}
// "10 swings with a 7 iron, filmed down-the-line and face-on at high frame rate, with every ball measured
// by a GC Quad. Your body positions are read from the video by pose tracking; the club and ball numbers
// come from the launch monitor."
inline QString aboutMeasured(int swings, const QString &clubs, const QStringList &cameraAliases, const QString &lmDevice, int lmSwings)
{
    QString s = swingsN(swings);
    if (!clubs.isEmpty()) s += QStringLiteral(" with ") + article(clubs) + QLatin1Char(' ') + clubs;
    const QString cams = camerasWords(cameraAliases);
    if (!cams.isEmpty()) {
        QString c = cams.toLower();
        c.chop(c.endsWith(QLatin1String("cameras")) ? 8 : 7);
        s += QStringLiteral(", filmed ") + c + QStringLiteral(" at high frame rate");
    }
    if (!lmDevice.isEmpty() && lmSwings > 0)
        s += lmSwings >= swings ? QStringLiteral(", with every ball measured by %1 %2").arg(article(lmDevice), lmDevice)
                                : QStringLiteral(", with %1 of the balls measured by %2 %3").arg(numberWord(lmSwings), article(lmDevice), lmDevice);
    s += QLatin1Char('.');
    if (!cams.isEmpty())
        s += QStringLiteral(" Your body positions are read from the video by pose tracking")
           + (lmSwings > 0 ? QStringLiteral("; the club and ball numbers come from the launch monitor.") : QStringLiteral("."));
    return s;
}
inline QString aboutRanges(bool ruledOutShown)
{
    QString s = QStringLiteral("Each habit is judged against a target range. Most ranges are coaching judgement, set from what good "
                               "ball-strikers do; a few come from published measurements. They are not fitted to you. A swing outside "
                               "the range is not a mistake in itself — it is where the evidence points.");
    if (ruledOutShown)
        s += QStringLiteral(" “Ruled out” on a work-on page means measured, and fine, on these swings with these cameras — not gone for good.");
    return s;
}
// WHAT WE COULD NOT CHECK (§10 rule 7), said once, a sentence per reading: "How fast the club was moving
// at its peak: our cameras could see it, but not well enough to judge (10 of 10 swings)." touchesWorkOn: a
// gated reading sits under one of the work-ons, whose page says so; otherwise nothing in the report
// depends on them.
inline QString aboutNotChecked(const QStringList &items, bool touchesWorkOn)
{
    if (items.isEmpty()) return QString();
    return QStringLiteral("Some things could not be checked on every swing. ") + items.join(QStringLiteral(". ")) + QLatin1Char('.')
         + (touchesWorkOn ? QStringLiteral(" Where that touches one of your work-ons, its page says so.")
                          : QStringLiteral(" Nothing in this report depends on them."));
}
inline QString notCheckedItem(const QString &subject, const QString &clause, int count, int swings)
{
    return QStringLiteral("%1: %2 (%3)").arg(cap1(subject), clause, countOf(count, swings));
}
inline QString notCheckedMore(int more)
{
    if (more <= 0) return QString();
    return more == 1 ? QStringLiteral("One more is listed in the technical appendix")
                     : QStringLiteral("%1 more are listed in the technical appendix").arg(cap1(numberWord(more)));
}
inline QString aboutFootnote(bool photos)
{
    return photos ? QStringLiteral("Measured and prepared with PinPoint Studio. Swing images show only the golfer; video stays in the studio.")
                  : QStringLiteral("Measured and prepared with PinPoint Studio. Video stays in the studio.");
}

// The photo line for the sheet (§7.3): why photos are, or are not, in.
// reason: 0 adult, 1 junior approved, 2 junior not approved, 3 no date of birth (session_report.h PhotoReason).
inline QString photoReason(int reason, const QString &first, const QString &approvedOn)
{
    switch (reason) {
    case 1:
        return approvedOn.isEmpty()
            ? QStringLiteral("%1 is under 18 — a parent or guardian approved swing photos").arg(first)
            : QStringLiteral("%1 is under 18 — a parent or guardian approved swing photos on %2").arg(first, approvedOn);
    case 2: return QStringLiteral("%1 is under 18 — no parent or guardian approval recorded, so no swing photos").arg(first);
    case 3: return QStringLiteral("No date of birth — photos included");
    default: return QString();
    }
}

// The studio's sign-off line from its booking field: a link reads "Book your next session at … or reply
// to this email."; a sentence the coach typed is used as typed.
inline QString signoff(const QString &booking, bool coachEmail)
{
    const QString b = booking.trimmed();
    if (b.isEmpty()) return coachEmail ? QStringLiteral("Reply to this email to book your next session.") : QString();
    if (b.contains(QLatin1Char(' '))) return b;
    return QStringLiteral("Book your next session at %1%2.").arg(b, coachEmail ? QStringLiteral(" or reply to this email") : QString());
}

} // namespace pinpoint::report::words
