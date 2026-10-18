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

// A box as the Charts QML takes it — the ONE serialisation, shared by the relations and wrist presenters
// (each kept its own copy until 18 Oct 2026). { n, min, max, q1, med, q3, lo, hi, outliers: [ordinal] }, NaN
// as null, and with a formatter `texts` { lo, q1, med, q3, hi }: the numbers PpBoxPlot lays on the plot while
// it is hovered, in the reading's own rounding (C++ decides the numbers, QML places them).

#include "../typeset/box_stats.h"

#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include <cmath>
#include <functional>

namespace pinpoint::charts {

inline QVariantMap boxMap(const pinpoint::box::BoxStats &b, const std::function<QString(double)> &fmt = {})
{
    const auto nv = [](double v) { return std::isfinite(v) ? QVariant(v) : QVariant::fromValue(nullptr); };
    QVariantList out;
    for (int i : b.outliers) out.append(i + 1);             // values are per shot: index + 1 = ordinal
    QVariantMap m{ { QStringLiteral("n"), b.n }, { QStringLiteral("min"), nv(b.min) }, { QStringLiteral("max"), nv(b.max) },
                   { QStringLiteral("q1"), nv(b.q1) }, { QStringLiteral("med"), nv(b.med) }, { QStringLiteral("q3"), nv(b.q3) },
                   { QStringLiteral("lo"), nv(b.lo) }, { QStringLiteral("hi"), nv(b.hi) }, { QStringLiteral("outliers"), out } };
    if (fmt && b.n > 0)
        m.insert(QStringLiteral("texts"), QVariantMap{ { QStringLiteral("lo"), fmt(b.lo) }, { QStringLiteral("q1"), fmt(b.q1) },
                                                       { QStringLiteral("med"), fmt(b.med) }, { QStringLiteral("q3"), fmt(b.q3) },
                                                       { QStringLiteral("hi"), fmt(b.hi) } });
    return m;
}

} // namespace pinpoint::charts
