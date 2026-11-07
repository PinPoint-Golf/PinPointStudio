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

#include "variability_trend.h"

#include "variability_constants.h"

#include <QCoreApplication>

#include <algorithm>
#include <cmath>

namespace pinpoint::variability {

namespace {

bool hasReading(const TrendPoint &p) { return std::isfinite(p.rho) && p.rho >= 0.0 && p.n >= 2; }

bool sameSeries(const TrendPoint &a, const TrendPoint &b)
{
    return a.contextKey == b.contextKey && a.basisKind == b.basisKind && a.scaleKey == b.scaleKey;
}

} // namespace

TrendResult variabilityTrend(const std::vector<TrendPoint> &points)
{
    TrendResult r;

    // The latest unbroken run, newest point backwards.
    std::vector<const TrendPoint *> run;
    for (int i = int(points.size()) - 1; i >= 0; --i) {
        const TrendPoint &p = points[size_t(i)];
        if (!hasReading(p)) continue;
        if (!run.empty() && !sameSeries(p, *run.back())) break;
        if (int(run.size()) == kFunnelMaxSessions) break;
        run.push_back(&p);
        r.runStart = i;
    }
    const int m = int(run.size());
    r.sessions = m;
    if (m == 0) return r;
    std::reverse(run.begin(), run.end());   // oldest first, indexed 0..m−1
    r.firstRho = run.front()->rho;
    r.lastRho  = run.back()->rho;

    if (run.back()->basisKind == Basis::Ordinal) { r.ordinalBasis = true; return r; }
    if (m < kTrendMinSessions) return r;

    const double tbar = (m - 1) / 2.0;
    double sxx = 0.0, sxy = 0.0, ybar = 0.0;
    for (const TrendPoint *p : run) ybar += p->rho;
    ybar /= m;
    for (int i = 0; i < m; ++i) {
        const double dt = i - tbar;
        sxx += dt * dt;
        sxy += dt * (run[size_t(i)]->rho - ybar);
    }
    r.slope  = sxy / sxx;
    r.change = r.slope * (m - 1);

    // SE_i ≈ ρ_i/√(2(n_i − 1)) (§8.8), propagated through the least-squares slope.
    double var = 0.0;
    for (int i = 0; i < m; ++i) {
        const TrendPoint &p = *run[size_t(i)];
        const double dt = i - tbar;
        const double se = p.rho / std::sqrt(2.0 * (p.n - 1));
        var += dt * dt * se * se;
    }
    var /= sxx * sxx;
    r.changeSe = (m - 1) * std::sqrt(var);

    if (std::abs(r.change) > kTrendSeMult * r.changeSe)
        r.direction = r.change < 0.0 ? TrendDirection::Tightening : TrendDirection::Loosening;
    else
        r.direction = TrendDirection::Holding;
    return r;
}

QString TrendResult::basisString(int decimals) const
{
    if (direction == TrendDirection::Insufficient) return {};
    //: Technical-layer trend basis (variability spec §7.5): ρ in the first and last session of the
    //: run. %1 first ρ, %2 an arrow glyph (not translated), %3 last ρ, %n the session count.
    return QCoreApplication::translate("VariabilityBasis", "ρ %1 %2 %3 over %n sessions", nullptr, sessions)
        .arg(QString::number(firstRho, 'f', decimals), QStringLiteral("→"),
             QString::number(lastRho, 'f', decimals));
}

} // namespace pinpoint::variability
