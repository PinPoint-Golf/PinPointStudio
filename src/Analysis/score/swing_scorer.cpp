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

#include "swing_scorer.h"

#include <QHash>
#include <QString>
#include <algorithm>
#include <cmath>

namespace pinpoint::analysis {
namespace {

// One reference band. oneSidedDir: 0 = two-sided; +1 = penalise values BELOW mu
// (good side is above, e.g. lead-wrist flexion — cupping/extension is the fault);
// -1 = penalise values ABOVE mu (e.g. a bent lead arm).
struct ScoreBand {
    const char *key;
    Phase       phase;
    double      mu, sigma;
    int         oneSidedDir;
    double      weight;
    const char *region;
};

// PROVISIONAL Wrist (session type 1) bands — HackMotion-shaped, to be tuned once the
// FE/RUD/pronation signs are locked on the "check your sensors" page.
constexpr ScoreBand kWristBands[] = {
    { "leadWristFlexExt", Phase::Impact, 15.0, 12.0, +1, 0.40, "wrist"   },
    { "leadWristRadUln",  Phase::Impact,  0.0, 15.0,  0, 0.20, "wrist"   },
    { "forearmPronation", Phase::Impact,  0.0, 25.0,  0, 0.20, "forearm" },
    { "leadArmFlexion",   Phase::Impact,  5.0, 12.0, -1, 0.20, "arm"     },
};

std::vector<ScoreBand> bandsFor(int sessionType)
{
    if (sessionType == 1)   // Wrist Motion
        return { std::begin(kWristBands), std::end(kWristBands) };
    return {};
}

const MetricSeries *findSeries(const std::vector<MetricSeries> &v, const QString &key)
{
    for (const MetricSeries &m : v)
        if (m.key == key) return &m;
    return nullptr;
}

double phaseValue(const MetricSeries &m, Phase p)
{
    for (const PhaseSample &s : m.phaseSamples)
        if (s.phase == p) return s.value;
    return m.value.empty() ? 0.0 : m.value.back();
}

QString phaseName(Phase p)
{
    static const char *kN[] = { "address", "takeaway", "top", "transition",
                                "downswing", "impact", "release", "finish" };
    return QString::fromLatin1(kN[int(p)]);
}

// Deadband + bounded falloff (design §B.1). |z|<=zIn → 100; ramps to ~0 at zOut; then 1.
// One-sided: the "good" side is clamped to 100. Sets *band to green/yellow/red.
double bandSubScore(double value, const ScoreBand &b, QString *band)
{
    constexpr double zIn = 1.0, zOut = 3.0, p = 2.0;
    double z = (value - b.mu) / b.sigma;
    if (b.oneSidedDir > 0 && z > 0) z = 0.0;   // good side (above mu) — no penalty
    if (b.oneSidedDir < 0 && z < 0) z = 0.0;   // good side (below mu) — no penalty
    z = std::abs(z);

    if (band) *band = (z <= zIn) ? QStringLiteral("green")
                     : (z <= zOut) ? QStringLiteral("yellow")
                                   : QStringLiteral("red");

    if (z <= zIn)  return 100.0;
    if (z >= zOut) return 1.0;
    const double t = (z - zIn) / (zOut - zIn);
    return std::max(1.0, 100.0 * (1.0 - std::pow(t, p)));
}

// Weighted geometric mean of (subScore, weight) pairs, floored at 1.
int weightedGeoMean(const std::vector<std::pair<double, double>> &sw)
{
    double wsum = 0.0, lnsum = 0.0;
    for (const auto &[s, w] : sw) { wsum += w; lnsum += w * std::log(std::max(s, 1.0)); }
    return wsum > 0.0 ? int(std::lround(std::exp(lnsum / wsum))) : 0;
}

} // namespace

ScoreBreakdown SwingScorer::score(const std::vector<MetricSeries> &series, int sessionType)
{
    ScoreBreakdown sb;
    const std::vector<ScoreBand> bands = bandsFor(sessionType);

    std::vector<std::pair<double, double>> overall;            // (subScore, weight)
    QHash<QString, std::vector<std::pair<double, double>>> byRegion, byPhase;

    for (const ScoreBand &b : bands) {
        const MetricSeries *m = findSeries(series, QString::fromLatin1(b.key));
        if (!m)
            continue;
        const double value = phaseValue(*m, b.phase);

        ScoredMetric sm;
        sm.key      = m->key;
        sm.value    = value;
        sm.mu       = b.mu;
        sm.sigma    = b.sigma;
        sm.oneSided = (b.oneSidedDir != 0);
        sm.phase    = b.phase;
        sm.weight   = b.weight;
        sm.subScore = bandSubScore(value, b, &sm.band);
        sb.metrics.push_back(sm);

        overall.push_back({ sm.subScore, b.weight });
        byRegion[QString::fromLatin1(b.region)].push_back({ sm.subScore, b.weight });
        byPhase[phaseName(b.phase)].push_back({ sm.subScore, b.weight });
    }

    sb.overall = weightedGeoMean(overall);
    for (auto it = byRegion.constBegin(); it != byRegion.constEnd(); ++it)
        sb.perRegion.insert(it.key(), weightedGeoMean(it.value()));
    for (auto it = byPhase.constBegin(); it != byPhase.constEnd(); ++it)
        sb.perPhase.insert(it.key(), weightedGeoMean(it.value()));
    return sb;
}

} // namespace pinpoint::analysis
