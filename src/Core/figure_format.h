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

// HOW EVERY FIGURE A GOLFER READS IS SET — the one C++ home for number formatting, so the Charts panel,
// Session diagnostics, the work-ons, the review charts and the session report cannot print the same reading
// two ways. Until 31 Oct 2026 there were nine base formatters, seven unit-joining rules (one reading printed
// "13 % hand rise", "13% hand rise" and "13%" on three surfaces), three tick generators and two "at impact"
// windows (gui-catalogue C15–C17, C33–C35).
//
// The house rules, each kept in exactly one function below:
//   · missing is "-" (ASCII hyphen-minus), never 0 and never an em dash             — fig, kMissing
//   · the minus sign is U+2212; a value that rounds to zero prints as 0, never "−0"  — fig
//   · figures are WHOLE UNITS except where a whole unit says nothing (a 2.6:1 tempo, a 1.4 in low point,
//     a 0.73 s backswing); per UNIT, never derived from σ                            — decimalsFor
//   · σ is shown only as the ±, at the figure's own precision, and never as "± 0"     — pm
//   · a figure and its unit: a no-break space (U+00A0), except that °, %, ″ and ":1" close up; words after
//     a % stay ("13% hand rise"); a "ratio" prints the bare number                   — withUnit
//   · an instant is said against impact, "at impact" within 2 ms                     — whenImpact
//   · axis ticks step 1-2-5, cut at 1.5 / 3 / 7                                     — tickValues
//
// Header-only and Qt Core only, and in Core so every library can use it: the gate catalogue (Metrics) sets its
// evidence figures with it, diagnostics/session_spread.h (and through it Coaching/session_report.h) its
// corridor words, and Gui/charts/charts_words.h re-exports all of it into charts::words.

#include <QChar>
#include <QLocale>
#include <QString>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

namespace pinpoint::figures {

inline constexpr QLatin1StringView kMissing("-");       // the house missing mark
constexpr char16_t kMinusSign = 0x2212;                  // the true minus
constexpr char16_t kNoBreakSpace = 0x00A0;               // "82 mph" never breaks across lines
constexpr int kAtImpactMs = 2;                           // within this, an instant is "at impact"

inline bool fin(double v) { return std::isfinite(v); }

// Display decimals for a unit: whole units except tempo ratio, inches and seconds.
inline int decimalsFor(const QString &unit)
{
    if (unit == QLatin1String(":1") || unit == QLatin1String("in")) return 1;
    if (unit == QLatin1String("s")) return 2;
    return 0;
}

// Every hyphen-minus in `s` as U+2212.
inline QString minus(QString s) { return s.replace(QLatin1Char('-'), QChar(kMinusSign)); }

// A figure rounded at `dec` decimals (ties away from zero), U+2212 minus, "-" when missing; −0 prints as 0.
// A negative `dec` rounds to tens, hundreds… and prints no decimals. `trimZeros` drops trailing zeros after
// the point (13.0 → "13", 8.70 → "8.7") for a figure whose decimals come from a span rather than its unit.
inline QString fig(double v, int dec = 0, bool trimZeros = false)
{
    if (!std::isfinite(v)) return QString(kMissing);
    const double p = std::pow(10.0, dec);
    double x = std::round(v * p) / p;
    if (std::fabs(x) < 0.5 / p) x = 0.0;
    QString body = QString::number(std::fabs(x), 'f', std::max(0, dec));
    if (trimZeros && body.contains(QLatin1Char('.'))) {
        while (body.endsWith(QLatin1Char('0'))) body.chop(1);
        if (body.endsWith(QLatin1Char('.'))) body.chop(1);
    }
    return x < 0 ? QString(QChar(kMinusSign)) + body : body;
}

// fig() with the thousands grouped as English sets them: "5,009" rpm. Same rounding, minus and missing mark.
inline QString figThousands(double v, int dec = 0)
{
    if (!std::isfinite(v)) return QString(kMissing);
    const double p = std::pow(10.0, dec);
    double x = std::round(v * p) / p;
    if (std::fabs(x) < 0.5 / p) x = 0.0;
    const QString body = QLocale(QLocale::English, QLocale::UnitedKingdom).toString(std::fabs(x), 'f', std::max(0, dec));
    return x < 0 ? QString(QChar(kMinusSign)) + body : body;
}

// A signed figure: "+3", "−3", "0" (a value that rounds to zero has no direction), "-" when missing. For a
// quantity whose sign IS the reading (a Δ, a deviation from address, a launch monitor's path).
inline QString figSigned(double v, int dec)
{
    const QString f = fig(v, dec);
    if (!std::isfinite(v)) return f;
    return (std::round(v * std::pow(10.0, dec)) > 0 ? QStringLiteral("+") : QString()) + f;
}

// r as an annual report sets it: ".62", "−.88", "1.00"; "-" when undefined; a value that rounds to zero
// prints ".00", never "−.00".
inline QString figR(double r)
{
    const QString f = fig(r, 2);
    if (f.startsWith(QLatin1String("0."))) return f.mid(1);
    if (f.startsWith(QString(QChar(kMinusSign)) + QLatin1String("0."))) return QString(QChar(kMinusSign)) + f.mid(2);
    return f;
}

// THE ONE RULE FOR A FIGURE AND ITS UNIT. A no-break space, except that a degree (°, °/s), a percent, a
// double prime (″) and a tempo ratio (:1) close up; any words after a percent stay, after a plain space
// ("13% hand rise", "125% shoulder width"); the unit "ratio" prints no unit at all ("1.33") — a "×" would
// read a share (ksOrderShare) as a multiplier. "-" and an empty unit pass through unchanged. `figure` may be a phrase ending in a figure ("pass up to 8.7 · fault above 13").
inline QString withUnit(const QString &figure, const QString &unit)
{
    if (figure == kMissing || unit.isEmpty()) return figure;
    if (unit.startsWith(QChar(0x00B0)) || unit.startsWith(QChar(0x2033)) || unit == QLatin1String(":1"))
        return figure + unit;
    if (unit.startsWith(QLatin1Char('%'))) {
        const QString rest = unit.mid(1).trimmed();
        return figure + QLatin1Char('%') + (rest.isEmpty() ? QString() : QLatin1Char(' ') + rest);
    }
    if (unit == QLatin1String("ratio")) return figure;    // a share or a multiple: the bare number, never "×"
    return figure + QChar(kNoBreakSpace) + unit;
}

// A figure in its unit's display precision, joined by withUnit: "82 mph", "13°", "2.6:1".
inline QString figUnit(double v, const QString &unit) { return withUnit(fig(v, decimalsFor(unit)), unit); }

// A signed difference in its unit's precision: "+12°", "−3 mph", "0 mph", "-" when missing.
inline QString figSigned(double v, const QString &unit) { return withUnit(figSigned(v, decimalsFor(unit)), unit); }

// A unit as one short token: every percent of a body dimension is "%" (the reading's own name carries the
// denominator), anything else as it is.
inline QString shortUnit(const QString &unit) { return unit.startsWith(QLatin1Char('%')) ? QStringLiteral("%") : unit; }

// σ as the ± only, at the figure's precision; empty when there is none or it rounds to 0.
inline QString pm(double sigma, int dec = 0)
{
    if (!std::isfinite(sigma) || sigma <= 0) return QString();
    const double p = std::pow(10.0, dec);
    const double x = std::round(sigma * p) / p;
    if (x <= 0) return QString();
    return QStringLiteral("± ") + QString::number(x, 'f', dec);
}

// "+12 ms", "−3 ms", "0 ms", "-" when missing.
inline QString signedMs(double ms) { return withUnit(figSigned(ms, 0), QStringLiteral("ms")); }

// A duration in whole ms with its unit: "58 ms"; "-" when missing.
inline QString figMs(double ms) { return withUnit(fig(ms), QStringLiteral("ms")); }

// AN INSTANT ON A SHOT'S OWN CLOCK SAID AGAINST IMPACT, in two parts so a sentence can set the figure in the
// figure face: { "40 ms", " before impact" }, { "", "at impact" } (within kAtImpactMs, rounded to the ms),
// { "12 ms", " after impact" }; { "-", "" } when missing.
struct ImpactTime {
    QString figure;     // "40 ms"; empty at impact
    QString words;      // " before impact" / " after impact" / "at impact"
    bool atImpact() const { return figure.isEmpty(); }
};
inline ImpactTime impactTime(double shotMs)
{
    if (!std::isfinite(shotMs)) return { QString(kMissing), QString() };
    const long x = std::lround(shotMs);
    if (std::labs(x) <= kAtImpactMs) return { QString(), QStringLiteral("at impact") };
    return { figMs(double(std::labs(x))), x > 0 ? QStringLiteral(" after impact") : QStringLiteral(" before impact") };
}
// Whether an instant is AFTER impact by the same window — so a count of "peaks after impact" never takes in a
// peak the sentence beside it calls "at impact". False when missing.
inline bool isAfterImpact(double shotMs) { return std::isfinite(shotMs) && std::lround(shotMs) > kAtImpactMs; }
// The same as one string: "40 ms before impact", "at impact", "12 ms after impact"; "-" when missing.
inline QString whenImpact(double shotMs)
{
    const ImpactTime t = impactTime(shotMs);
    return t.figure + t.words;
}

// ── Axis ticks ────────────────────────────────────────────────────────────────────────────────────

// The step for about `maxTicks` round ticks over lo..hi: 1, 2, 5 or 10 × a power of ten (cut at 1.5, 3, 7);
// 0 when there is no span.
inline double tickStep(double lo, double hi, int maxTicks)
{
    if (!std::isfinite(lo) || !std::isfinite(hi) || !(hi > lo) || maxTicks < 1) return 0.0;
    const double raw = (hi - lo) / maxTicks, p = std::pow(10.0, std::floor(std::log10(raw))), f = raw / p;
    return (f <= 1.5 ? 1 : f <= 3 ? 2 : f <= 7 ? 5 : 10) * p;
}

// Decimals that print every tick of a step: 0 for a whole step, 1 for 0.5, 2 for 0.05.
inline int tickDecimals(double step)
{
    return step >= 1 || !(step > 0) ? 0 : int(std::ceil(-std::log10(step) - 1e-9));
}

// ROUND TICKS for a value axis: every multiple of tickStep() inside lo..hi (at most 64), so the labels read
// 0 5 10 15, never 2.7 7.9 13.1. Each is k × step worked out against an exact power of ten, so 3 × 0.1 is 0.3
// and not 0.30000000000000004; a tick at zero is +0. Values only — the caller formats them.
inline std::vector<double> tickValues(double lo, double hi, int maxTicks)
{
    std::vector<double> out;
    const double st = tickStep(lo, hi, maxTicks);
    if (!(st > 0.0)) return out;
    const int e = int(std::floor(std::log10(st) + 1e-9));
    const double m = std::round(st / std::pow(10.0, e));                       // 1, 2, 5 or 10
    const auto at = [&](double k) { return e < 0 ? k * m / std::pow(10.0, -e) : k * m * std::pow(10.0, e); };
    for (double k = std::ceil(lo / st - 1e-6); out.size() < 64; k += 1.0) {
        const double v = at(k);
        if (v > hi + st * 1e-6) break;
        out.push_back(v == 0.0 ? 0.0 : v);
    }
    return out;
}

} // namespace pinpoint::figures
