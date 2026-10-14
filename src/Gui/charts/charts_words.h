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
// Words.qml) and from the house rules:
//
//   · missing is "-", never 0;
//   · figures are WHOLE UNITS except where a whole unit says nothing (a 2.6:1 tempo, a 1.4 in low
//     point, a 0.73 s backswing) — decimalsFor() is that rule and the only place it lives
//     (ChartMetrics::formatBare uses it too); it is per UNIT, never derived from σ;
//   · σ is shown only as the ±, at the figure's own precision, and never as "± 0";
//   · the minus sign is U+2212.
//
// A SENTENCE is published to QML as plain text plus runs, so the panel can set the figures in the
// figure face without parsing: { text, runs: [ { text, fig } ] }. Sentence::toVariant() makes that.

#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <cmath>
#include <vector>

namespace pinpoint::charts::words {

// Display decimals for a unit: whole units except tempo ratio, inches and seconds.
inline int decimalsFor(const QString &unit)
{
    if (unit == QLatin1String(":1") || unit == QLatin1String("in")) return 1;
    if (unit == QLatin1String("s")) return 2;
    return 0;
}

inline QString minus(QString s) { return s.replace(QLatin1Char('-'), QChar(0x2212)); }

// A figure at `dec` decimals, U+2212 minus, "-" when missing; −0 prints as 0.
inline QString fig(double v, int dec = 0)
{
    if (!std::isfinite(v)) return QStringLiteral("-");
    const double p = std::pow(10.0, dec);
    double x = std::round(v * p) / p;
    if (std::fabs(x) < 0.5 / p) x = 0.0;
    const QString body = QString::number(std::fabs(x), 'f', dec);
    return x < 0 ? QString(QChar(0x2212)) + body : body;
}

// A figure in its unit's display precision, with the unit closed up for °, % and :1, else joined by a
// no-break space (U+00A0) so a figure and its unit never break across lines.
inline QString figUnit(double v, const QString &unit)
{
    const QString f = fig(v, decimalsFor(unit));
    if (f == QLatin1String("-") || unit.isEmpty()) return f;
    if (unit == QStringLiteral("°") || unit.startsWith(QLatin1Char('%')) || unit == QLatin1String(":1"))
        return f + (unit.startsWith(QLatin1Char('%')) ? QStringLiteral("%") : unit);
    return f + QChar(0x00A0) + unit;          // a no-break space: "82 mph" never breaks
}

// σ as the ± only, at the figure's precision; empty when there is none or it rounds to 0.
inline QString pm(double sigma, int dec = 0)
{
    if (!std::isfinite(sigma) || sigma <= 0) return QString();
    const double p = std::pow(10.0, dec);
    const double x = std::round(sigma * p) / p;
    if (x <= 0) return QString();
    return QStringLiteral("± ") + QString::number(x, 'f', dec);
}

// "+12 ms", "−3 ms", "0 ms", "-" when missing. Number and unit joined by a no-break space (U+00A0).
inline QString signedMs(double ms)
{
    if (!std::isfinite(ms)) return QStringLiteral("-");
    const long x = std::lround(ms);
    return (x > 0 ? QStringLiteral("+") : x < 0 ? QString(QChar(0x2212)) : QString())
           + QString::number(std::labs(x)) + QChar(0x00A0) + QStringLiteral("ms");
}

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

// A sentence as text plus runs; append() for words, figure() for a figure set in the figure face.
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
        return { { QStringLiteral("text"), text() }, { QStringLiteral("runs"), rl } };
    }
};

// ── appended by the traces presenter (step 2a-T) ──────────────────────────────────────────────

// A signed difference in its unit's precision: "+12°", "−3 mph", "0 mph", "-" when missing. The "+"
// is the point (a Δ has a direction); figUnit's closing-up rules apply.
inline QString figSigned(double v, const QString &unit)
{
    const QString f = figUnit(v, unit);
    if (f == QLatin1String("-") || !std::isfinite(v)) return f;
    const double p = std::pow(10.0, decimalsFor(unit));
    return (std::round(v * p) / p > 0 ? QStringLiteral("+") : QString()) + f;
}

// A duration in whole ms with its unit, no-break: "58 ms"; "-" when missing.
inline QString figMs(double ms)
{
    const QString f = fig(ms);
    return f == QLatin1String("-") ? f : f + QChar(0x00A0) + QStringLiteral("ms");
}

// An instant on a shot's own clock said against impact: "40 ms before impact", "at impact" (within
// 2 ms), "12 ms after impact"; "-" when missing.
inline QString whenImpact(double shotMs)
{
    if (!std::isfinite(shotMs)) return QStringLiteral("-");
    const long x = std::lround(shotMs);
    if (std::labs(x) <= 2) return QStringLiteral("at impact");
    return QString::number(std::labs(x)) + QChar(0x00A0) + (x > 0 ? QStringLiteral("ms after impact") : QStringLiteral("ms before impact"));
}

} // namespace pinpoint::charts::words
