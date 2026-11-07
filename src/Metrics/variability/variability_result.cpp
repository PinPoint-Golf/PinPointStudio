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

#include "variability_result.h"

#include <QCoreApplication>

#include <algorithm>
#include <cmath>
#include <limits>

// The §7.5 basis strings. Every call names its literal so lupdate sees it; the "VariabilityBasis"
// context keeps them together for a translator. Arrow glyphs and the computed numbers are arguments,
// never part of the translatable text.

namespace pinpoint::variability {

namespace {

// A figure at `decimals` places. One that rounds to zero is "0", never "-0": rounding a value just below zero is no
// measurement of a side.
QString num(double v, int decimals)
{
    QString s = QString::number(v, 'f', std::max(0, decimals));
    if (s.startsWith(QLatin1Char('-')) && std::all_of(s.cbegin() + 1, s.cend(), [](QChar c) { return c == QLatin1Char('0') || c == QLatin1Char('.'); }))
        s.remove(0, 1);
    return s;
}

// A spread's places: the measure's own, or more while that would print fewer than two significant figures, so a
// spread of 3.2 % never reads "σ 3%" and one of 0.04° never reads "σ 0.0°". One rule for every basis string.
// Counted on the printed figure, so a σ of 0.99996 that prints "1.0" is two figures already.
int sigmaDecimals(double sd, int decimals)
{
    decimals = std::max(0, decimals);
    if (!std::isfinite(sd) || !(std::fabs(sd) > 0.0)) return decimals;
    const auto figures = [sd](int d) {
        const QString s = QString::number(std::fabs(sd), 'f', d);
        int n = 0;
        bool leading = true;
        for (QChar c : s) {
            if (!c.isDigit()) continue;
            if (leading && c == QLatin1Char('0')) continue;
            leading = false;
            ++n;
        }
        return n;
    };
    while (figures(decimals) < 2 && decimals < std::numeric_limits<double>::digits10) ++decimals;
    return decimals;
}

// "σ 2.1° · 0.42 of corridor · n 24", named for the scale actually used: a fallback scale is never
// called a corridor.
QString spreadString(double sd, double rho, ScaleSource source, int n, const QString &unit, int decimals)
{
    const QString sigma = formatWithUnit(sd, unit, sigmaDecimals(sd, decimals));
    const QString ratio = num(rho, 2);
    const QString count = QString::number(n);
    switch (source) {
    case ScaleSource::Corridor:
        //: Technical-layer consistency basis (variability spec §7.5). %1 the spread with its unit,
        //: %2 the spread as a fraction of the norm corridor half-width, %3 the swing count.
        return QCoreApplication::translate("VariabilityBasis", "σ %1 · %2 of corridor · n %3").arg(sigma, ratio, count);
    default:
        //: Technical-layer consistency basis. %2 the spread as a fraction of the measure's own
        //: variability scale (not a norm corridor).
        return QCoreApplication::translate("VariabilityBasis", "σ %1 · %2 of scale · n %3").arg(sigma, ratio, count);
    }
}

} // namespace

QString formatWithUnit(double value, const QString &unit, int decimals)
{
    const QString v = num(value, decimals);
    if (unit.isEmpty()) return v;
    // Degree, percent and ratio units sit against the number; every other unit follows a space.
    if (unit.startsWith(QStringLiteral("°")) || unit.startsWith(QLatin1Char('%')) || unit.startsWith(QLatin1Char(':')))
        return v + unit;
    return v + QLatin1Char(' ') + unit;
}

QString ballFlightBasisString(double offlineSd, double carrySd, int shots, const QString &unit, int decimals)
{
    //: Technical-layer ball-flight basis (variability spec §7.5, §9.6). %1 the offline SD and %2 the
    //: carry SD, each with its unit; %n the shot count.
    return QCoreApplication::translate("VariabilityBasis", "offline ±%1 · carry ±%2 · %n shots", nullptr, shots)
        .arg(formatWithUnit(offlineSd, unit, decimals), formatWithUnit(carrySd, unit, decimals));
}

double VariabilityResult::bandValue() const
{
    if (!graded || grade == 0) return kNaN;
    switch (basis) {
    case Basis::Scalar:
    case Basis::Trace:       return rho;
    case Basis::Ordinal:     return sPrime;
    case Basis::Measurement: return rhoM;
    }
    return kNaN;
}

const GradeBands &VariabilityResult::bands() const
{
    return basis == Basis::Ordinal ? kBands.ordinal : kBands.ratio;
}

QString VariabilityResult::basisString(const QString &unit, int decimals) const
{
    if (!graded || grade == 0) return {};

    QString out;
    switch (basis) {
    case Basis::Scalar:
        out = spreadString(sd, rho, scaleSource, n, unit, decimals);
        break;
    case Basis::Trace:
        if (limitedBy == TraceLimiter::Timing) {
            //: Technical-layer basis of a time-series consistency grade set by peak timing.
            //: %1 the SD of time-to-peak in milliseconds, %2 the swing count.
            out = QCoreApplication::translate("VariabilityBasis", "σ peak %1 ms · n %2")
                      .arg(num(sdPeakMs, 0), QString::number(n));
        } else {
            out = spreadString(sd, rhoAmp, scaleSource, n, unit, decimals);
        }
        break;
    case Basis::Ordinal:
        //: Technical-layer basis of an order or checkpoint consistency grade. %1 the swings in the
        //: most common order, %n all swings.
        out = QCoreApplication::translate("VariabilityBasis", "Same order in %1 of %n swings", nullptr, n)
                  .arg(matching);
        break;
    case Basis::Measurement:
        return {};   // §7.5 has no Needle row
    }

    if (measurementLimited)
        //: Appended to a consistency basis when the source's measurement grade is Unsteady or Noisy
        //: (variability spec §8.6), or the observed spread is no larger than the reading's own
        //: propagated measurement σ. %1 is the consistency basis string.
        out = QCoreApplication::translate("VariabilityBasis", "%1 · measurement-limited").arg(out);
    return out;
}

} // namespace pinpoint::variability
