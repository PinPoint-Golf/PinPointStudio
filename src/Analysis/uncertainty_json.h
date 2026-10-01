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

// The uncertainty fields' JSON / QVariant spelling, in ONE place so the document writer
// (swing_doc.cpp), the reuse reader (recorded_products.cpp), the live detail bridge
// (shot_processor.cpp) and the reload (disk_replay_source.cpp) cannot drift. Every key is
// written ONLY when the field was assessed (shaft_uncertainty_propagation_design.md §7), so
// an uncertainty-off run serialises byte-identically.

#include <QJsonObject>
#include <QVariantMap>

#include "swing_analysis.h"

namespace pinpoint::analysis {

inline void insertShaftSigma(QJsonObject &o, const ShaftSample2D &s)
{
    if (s.sigmaThetaDeg >= 0.f) o.insert(QStringLiteral("sigTheta"), double(s.sigmaThetaDeg));
    if (s.pGross >= 0.f)        o.insert(QStringLiteral("pGross"),   double(s.pGross));
    if (s.tier >= 0)            o.insert(QStringLiteral("tier"),     int(s.tier));
}

inline void readShaftSigma(const QJsonObject &o, ShaftSample2D &s)
{
    s.sigmaThetaDeg = float(o.value(QStringLiteral("sigTheta")).toDouble(-1.0));
    s.pGross        = float(o.value(QStringLiteral("pGross")).toDouble(-1.0));
    s.tier          = int8_t(o.value(QStringLiteral("tier")).toInt(-1));
}

inline void insertPhaseSampleSigma(QJsonObject &o, const PhaseSample &ps)
{
    if (ps.sigma)          o.insert(QStringLiteral("sigma"), *ps.sigma);
    if (ps.sigmaKind > 0)  o.insert(QStringLiteral("sigmaKind"), int(ps.sigmaKind));
    if (ps.grossRisk >= 0.f) o.insert(QStringLiteral("grossRisk"), double(ps.grossRisk));
}

inline void insertPhaseSampleSigma(QVariantMap &o, const PhaseSample &ps)
{
    if (ps.sigma)          o.insert(QStringLiteral("sigma"), *ps.sigma);
    if (ps.sigmaKind > 0)  o.insert(QStringLiteral("sigmaKind"), int(ps.sigmaKind));
    if (ps.grossRisk >= 0.f) o.insert(QStringLiteral("grossRisk"), double(ps.grossRisk));
}

// Copy the per-instant σ keys of a stored phase sample into the QML map (the reload path).
inline void copyPhaseSampleSigma(const QJsonObject &from, QVariantMap &to)
{
    for (const char *k : { "sigma", "sigmaKind", "grossRisk" }) {
        const QString key = QString::fromLatin1(k);
        if (from.contains(key)) to.insert(key, from.value(key).toVariant());
    }
}

} // namespace pinpoint::analysis
