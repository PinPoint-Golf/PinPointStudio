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

// THE CHARTS PANEL'S WORDS AND FIGURES — the small vocabulary both presenters (traces and
// relations) build their sentences from, so "3rd highest", "−12 ms" and "1.6 in" read the same in
// every view. Ported from the mocks (variant_a/lib/traces.js "words and figures", variant_b/lib/
// Words.qml) and from the house rules.
//
// THE FIGURES ARE NOT SET HERE: figure_format.h is the one home for every number the app prints
// (missing "-", the U+2212 minus, decimalsFor, the unit-joining rule, "at impact", the axis ticks), and
// this header re-exports it into charts::words so a presenter says words::fig as it always has. What
// lives here is the WORDS — number words, ordinals, cap1, list, plural, the fixed notes, Sentence —
// which the session report (report/report_words.h) uses too.
//
// A SENTENCE is published to QML as plain text plus runs, so the panel can set the figures in the
// figure face without parsing: { text, runs: [ { text, fig } ], clause }. Sentence::toVariant() makes that.

#include "../../Vocabulary/Common/figure_format.h"

#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <cmath>
#include <vector>

namespace pinpoint::charts::words {

// "SHOT N" IS THE SWING NUMBER (ID-SWING-NUMBER, DEC-91; step 2.7b, audit E-06/F-07): the number on the
// carousel's card and in the diagnostics panel, never the shot's rank among the session's analysed shots,
// which a trashed swing, or one with no document or no analysis, moves. The rank stays what it is — a
// position on an axis, the shot's place in a box or a strip — and is never printed as "shot N".
// shotNumber: the swing number, else (a shot with none: a caller's own id, a fixture) its place.
inline int shotNumber(int swingNumber, int ordinal) { return swingNumber > 0 ? swingNumber : ordinal; }
// "Shot 8 of 15" where the swing number and the place agree (every session with no gap: today's text);
// "Shot 8 · 6 of 7" where they differ — the number names the shot, the place counts the session's shots
// (the diagnostics' rule, the lead's J-54, step 2.7a).
inline QString shotOfText(int swingNumber, int ordinal, int n)
{
    const int no = shotNumber(swingNumber, ordinal);
    return no == ordinal ? QStringLiteral("Shot %1 of %2").arg(no).arg(n)
                         : QStringLiteral("Shot %1 · %2 of %3").arg(no).arg(ordinal).arg(n);
}

// The figures (figure_format.h), as words::fig, words::figUnit… for every presenter.
using pinpoint::figures::decimalsFor;
using pinpoint::figures::fig;
using pinpoint::figures::figMs;
using pinpoint::figures::figR;
using pinpoint::figures::figSigned;
using pinpoint::figures::figThousands;
using pinpoint::figures::figUnit;
using pinpoint::figures::fin;
using pinpoint::figures::ImpactTime;
using pinpoint::figures::impactTime;
using pinpoint::figures::isAfterImpact;
using pinpoint::figures::minus;
using pinpoint::figures::pm;
using pinpoint::figures::shortUnit;
using pinpoint::figures::signedMs;
using pinpoint::figures::tickDecimals;
using pinpoint::figures::tickStep;
using pinpoint::figures::tickValues;
using pinpoint::figures::whenImpact;
using pinpoint::figures::withUnit;

// "no", "one" … "twenty", then figures. countWord says "none" for 0 standing alone.
inline QString word(int n)
{
    static const char *const W[] = { "no", "one", "two", "three", "four", "five", "six", "seven",
        "eight", "nine", "ten", "eleven", "twelve", "thirteen", "fourteen", "fifteen", "sixteen",
        "seventeen", "eighteen", "nineteen", "twenty" };
    return (n >= 0 && n <= 20) ? QString::fromLatin1(W[n]) : QString::number(n);
}
inline QString countWord(int n) { return n == 0 ? QStringLiteral("none") : word(n); }

// "1st", "2nd", "3rd", "11th", "22nd".
inline QString ordinalWord(int n)
{
    const int v = n % 100;
    const char *suf = (v >= 11 && v <= 13) ? "th"
                    : (n % 10 == 1) ? "st" : (n % 10 == 2) ? "nd" : (n % 10 == 3) ? "rd" : "th";
    return QString::number(n) + QLatin1String(suf);
}

inline QString cap1(QString s) { if (!s.isEmpty()) s[0] = s[0].toUpper(); return s; }

// "habit" / "habits" by the count.
inline QString plural(int n, const QString &one, const QString &many) { return n == 1 ? one : many; }

// "a, b and c".
inline QString list(const QStringList &items)
{
    if (items.size() <= 1) return items.join(QString());
    return QStringList(items.mid(0, items.size() - 1)).join(QStringLiteral(", "))
           + QStringLiteral(" and ") + items.last();
}

// "shot 8" / "shots 3, 4 and 12" from ordinals.
inline QString shotsList(const std::vector<int> &ordinals)
{
    QStringList s;
    for (int o : ordinals) s << QString::number(o);
    return (ordinals.size() == 1 ? QStringLiteral("shot ") : QStringLiteral("shots ")) + list(s);
}

// Every co-occurrence line ends with this (design §2.3).
inline QString coOccurrenceNote()
{
    return QStringLiteral("They occur together; that is not to say one causes another.");
}

// The late pelvis, said neutrally and only this way (design §7, contract rule 5).
inline QString latePelvisNote()
{
    return QStringLiteral("This may be the golfer (hips held at impact, then catching up) or the "
                          "two-camera measurement; it cannot yet tell them apart.");
}

// A sentence's first clause: its text up to the first ';' or '.', or the whole text when it has neither — what a
// folded section's head keeps of its sentence (the session view's fold heads). It cuts at the point in "1.6 in" and
// gives "" for a text that starts with one, as the view's old /^[^;.]*/ did.
inline QString firstClauseOf(const QString &text)
{
    for (qsizetype i = 0; i < text.size(); ++i)
        if (text.at(i) == QLatin1Char(';') || text.at(i) == QLatin1Char('.')) return text.left(i);
    return text;
}

// A sentence as text plus runs; append() for words, figure() for a figure set in the figure face.
// toVariant() also carries `clause`, the text's firstClauseOf, so no view cuts a sentence itself.
struct Sentence {
    struct Run { QString text; bool fig = false; };
    std::vector<Run> runs;

    Sentence &append(const QString &t) { if (!t.isEmpty()) runs.push_back({ t, false }); return *this; }
    Sentence &figure(const QString &t) { if (!t.isEmpty()) runs.push_back({ t, true }); return *this; }
    QString text() const { QString s; for (const Run &r : runs) s += r.text; return s; }
    bool isEmpty() const { return runs.empty(); }
    QVariantMap toVariant() const
    {
        QVariantList rl;
        for (const Run &r : runs)
            rl.append(QVariantMap{ { QStringLiteral("text"), r.text }, { QStringLiteral("fig"), r.fig } });
        const QString t = text();
        return { { QStringLiteral("text"), t }, { QStringLiteral("runs"), rl }, { QStringLiteral("clause"), firstClauseOf(t) } };
    }
};

} // namespace pinpoint::charts::words
