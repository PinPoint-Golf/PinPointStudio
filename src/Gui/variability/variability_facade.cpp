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

#include "variability_facade.h"

#include <QVariantList>

namespace pinpoint::variability {

namespace {

QVariantList listOf(const std::vector<double> &v)
{
    QVariantList out;
    out.reserve(qsizetype(v.size()));
    for (double x : v) out << x;
    return out;
}

// A JS null, so `corridor` is present and null rather than undefined.
QVariant jsNull() { return QVariant::fromValue(nullptr); }

} // namespace

QString VariabilityResultView::basisToken(Basis basis)
{
    switch (basis) {
    case Basis::Scalar:      return QStringLiteral("scalar");
    case Basis::Trace:       return QStringLiteral("trace");
    case Basis::Ordinal:     return QStringLiteral("ordinal");
    case Basis::Measurement: return QStringLiteral("measurement");
    }
    return QStringLiteral("scalar");
}

QString VariabilityResultView::domainToken(Basis basis)
{
    // The Needle is the only measurement-quality mark (§4.6, §7.2); every other basis grades the golfer.
    return basis == Basis::Measurement ? QStringLiteral("measurement") : QStringLiteral("consistency");
}

QString VariabilityResultView::ungradedReasonToken(UngradedReason reason)
{
    switch (reason) {
    case UngradedReason::None:                return {};
    case UngradedReason::TooFewSwings:        return QStringLiteral("tooFewSwings");
    case UngradedReason::NoScale:             return QStringLiteral("noScale");
    case UngradedReason::NonPositiveForRatio: return QStringLiteral("nonPositiveForRatio");
    case UngradedReason::NoUncertainty:       return QStringLiteral("noUncertainty");
    case UngradedReason::ScaleTypeMismatch:   return QStringLiteral("scaleTypeMismatch");
    case UngradedReason::MalformedInput:      return QStringLiteral("malformedInput");
    }
    return {};
}

QString VariabilityResultView::typicalityBandToken(TypicalityBand band)
{
    switch (band) {
    case TypicalityBand::Usual:      return QStringLiteral("usual");
    case TypicalityBand::ALittleOff: return QStringLiteral("aLittleOff");
    case TypicalityBand::Unusual:    return QStringLiteral("unusual");
    }
    return QStringLiteral("usual");
}

QString VariabilityResultView::trendDirectionToken(TrendDirection direction)
{
    switch (direction) {
    case TrendDirection::Tightening:   return QStringLiteral("tightening");
    case TrendDirection::Loosening:    return QStringLiteral("loosening");
    case TrendDirection::Holding:      return QStringLiteral("holding");
    case TrendDirection::Insufficient: return QStringLiteral("insufficient");
    }
    return QStringLiteral("insufficient");
}

VariabilityResultView VariabilityResultView::fromCellGrade(const CellGrade &cell)
{
    VariabilityResultView v;
    v.m_grade              = cell.grade;
    v.m_graded             = cell.graded;
    v.m_basis              = basisToken(Basis::Scalar);
    v.m_ungradedReason     = cell.graded && cell.grade == 0 ? ungradedReasonToken(UngradedReason::TooFewSwings)
                                                            : QString();
    v.m_measurementLimited = cell.measurementLimited;
    v.m_n                  = cell.n;
    v.m_rho                = std::isfinite(cell.rho) ? cell.rho : VariabilityResult::kNaN;
    v.m_domain             = domainToken(Basis::Scalar);
    return v;
}

QVariantMap VariabilityResultView::toMap() const
{
    return { { QStringLiteral("grade"), m_grade }, { QStringLiteral("graded"), m_graded }, { QStringLiteral("basis"), m_basis },
             { QStringLiteral("ungradedReason"), m_ungradedReason }, { QStringLiteral("measurementLimited"), m_measurementLimited },
             { QStringLiteral("n"), m_n }, { QStringLiteral("hasRho"), hasRho() },
             { QStringLiteral("rho"), hasRho() ? QVariant(m_rho) : jsNull() }, { QStringLiteral("basisText"), m_basisText },
             { QStringLiteral("domain"), m_domain } };
}

TypicalityPayload typicalityPayload(const ScalarTypicalityWindow &window, const QString &unit, int decimals)
{
    return typicalityPayload(window, [&unit, decimals](double v) { return formatWithUnit(v, unit, decimals); });
}

TypicalityPayload typicalityPayload(const ScalarTypicalityWindow &window, const std::function<QString(double)> &formatValue)
{
    QVariantList ghosts;
    for (const TypicalityGhost &g : window.ghosts)
        ghosts << QVariantMap{ { QStringLiteral("swingId"), g.swingId }, { QStringLiteral("z"), g.z } };
    TypicalityPayload p;
    p.values = QVariantMap{
        { QStringLiteral("z"), window.typicality.z },
        { QStringLiteral("ghosts"), ghosts },
        { QStringLiteral("corridor"),
          window.corridor ? QVariant(QVariantMap{ { QStringLiteral("loZ"), window.corridor->loZ },
                                                  { QStringLiteral("hiZ"), window.corridor->hiZ } })
                          : jsNull() },
        { QStringLiteral("band"), VariabilityResultView::typicalityBandToken(window.typicality.band) },
    };
    p.basisText = window.typicality.basisString(formatValue);
    return p;
}

TypicalityPayload typicalityTracePayload(const TraceTypicalityWindow &window)
{
    TypicalityPayload p;
    p.values = QVariantMap{
        { QStringLiteral("mean"), listOf(window.mean) },
        { QStringLiteral("sd"), listOf(window.sd) },
        { QStringLiteral("swing"), listOf(window.swing) },
        { QStringLiteral("corridor"),
          window.corridor ? QVariant(QVariantMap{ { QStringLiteral("loT"), window.corridor->loT },
                                                  { QStringLiteral("hiT"), window.corridor->hiT } })
                          : jsNull() },
        { QStringLiteral("band"), VariabilityResultView::typicalityBandToken(window.typicality.band) },
    };
    p.basisText = window.typicality.basisString();
    return p;
}

VariabilityResultView VariabilityResultView::fromResult(const VariabilityResult &result, const QString &unit,
                                                        int decimals)
{
    VariabilityResultView v;
    v.m_grade              = result.grade;
    v.m_graded             = result.graded;
    v.m_basis              = basisToken(result.basis);
    v.m_ungradedReason     = ungradedReasonToken(result.ungradedReason);
    // Passed through untouched, NOT applied to `grade`: §8.6's golfer-layer "Checking" is the surface's
    // choice (see the class comment).
    v.m_measurementLimited = result.measurementLimited;
    v.m_n                  = result.n;
    v.m_rho                = std::isfinite(result.rho) ? result.rho : VariabilityResult::kNaN;
    v.m_basisText          = result.basisString(unit, decimals);
    v.m_domain             = domainToken(result.basis);
    return v;
}

} // namespace pinpoint::variability
