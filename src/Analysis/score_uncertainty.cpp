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

#include "score_uncertainty.h"

#include "wrist_resemblance.h"
#include "../Core/pp_tuned_constants.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace pinpoint::analysis {
namespace {

namespace tu = pinpoint::tuned::scoring::uncertainty;

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

const PhaseEvent *eventFor(const std::vector<PhaseEvent> &phases, Phase p)
{
    for (const PhaseEvent &e : phases)
        if (e.phase == p) return &e;
    return nullptr;
}

// |dθ/dt| of the FE curve at time t (deg per µs), by central difference on the shared grid.
double localSlopeDegPerUs(const MetricSeries &m, int64_t t)
{
    const size_t n = m.t_us.size();
    if (n < 2 || m.value.size() != n) return 0.0;
    size_t i = 0;
    for (size_t k = 1; k < n; ++k)
        if (std::llabs(m.t_us[k] - t) < std::llabs(m.t_us[i] - t)) i = k;
    const size_t lo = (i > 0) ? i - 1 : i;
    const size_t hi = (i + 1 < n) ? i + 1 : i;
    const int64_t dt = m.t_us[hi] - m.t_us[lo];
    if (dt == 0) return 0.0;
    return std::abs((m.value[hi] - m.value[lo]) / double(dt));
}

double R_of(double d2) { return 100.0 * std::exp(-0.5 * d2); }

} // namespace

ScoreInterval ScoreUncertainty::wristInterval(const ScoreBreakdown &sb,
                                              const std::vector<MetricSeries> &series,
                                              const std::vector<PhaseEvent> &phases,
                                              const QVariantMap &ov)
{
    ScoreInterval iv;   // invalid by default (halfWidth -1)
    if (sb.kind != ScoreKind::Resemblance)
        return iv;

    const MetricSeries *fe = findSeries(series, QStringLiteral("leadWristFlexExt"));
    if (!fe) return iv;

    const std::array<ResemblancePattern, 3> pats = WristResemblanceScorer::patterns(ov);
    const ResemblancePattern *win = nullptr;
    for (const ResemblancePattern &p : pats)
        if (p.label == sb.patternLabel) { win = &p; break; }
    if (!win) return iv;

    const PhaseEvent *top = eventFor(phases, Phase::Top);
    const PhaseEvent *imp = eventFor(phases, Phase::Impact);
    if (!top || !imp) return iv;

    // Per-phase angle uncertainty → variance of d² of the winning pattern. d² is a sum of
    // squared near-normal z-scores; Var(d²) = Σ (2·v² + 4·z²·v), v = (σ_x/σ_p)².  The 2·v²
    // term keeps the interval non-zero even at the pattern centre (z=0), where R is locally
    // flat — honest about the ±crosstalk floor.
    struct Cell { Phase phase; double mu; const PhaseEvent *ev; };
    const Cell cells[2] = { { Phase::Top, win->muTop, top }, { Phase::Impact, win->muImpact, imp } };

    double d2 = 0.0, varD2 = 0.0;
    for (const Cell &c : cells) {
        const double x = phaseValue(*fe, c.phase);
        const double z = (x - c.mu) / win->sigma;

        // Per-cell FE error budget; the timing term is dθ/dt × jitter. Low phase confidence
        // inflates the whole budget (poorly-seated/badly-segmented ⇒ less certain), so it widens
        // the interval without touching the central score.
        const double conf       = std::clamp(double(c.ev->conf), 0.0, 1.0);
        const double cf         = 1.0 + (1.0 - conf) * tu::kConfInflate;
        const double timingTerm = localSlopeDegPerUs(*fe, c.ev->t_us) * double(tu::kTimingSigmaUs);
        const double sigmaX     = cf * std::sqrt(tu::kSensorSigmaDeg * tu::kSensorSigmaDeg
                                               + tu::kCrosstalkSigmaDeg * tu::kCrosstalkSigmaDeg
                                               + timingTerm * timingTerm);
        const double v = (sigmaX / win->sigma) * (sigmaX / win->sigma);

        d2    += z * z;
        varD2 += 2.0 * v * v + 4.0 * z * z * v;
    }

    const double sigmaD2 = tu::kIntervalSigmas * std::sqrt(std::max(0.0, varD2));
    const int central = sb.overall;
    const int hi = std::clamp(int(std::lround(R_of(std::max(0.0, d2 - sigmaD2)))), 0, 100);
    const int lo = std::clamp(int(std::lround(R_of(d2 + sigmaD2))), 0, 100);

    iv.lo        = std::min(lo, central);
    iv.hi        = std::max(hi, central);
    iv.halfWidth = int(std::lround((iv.hi - iv.lo) / 2.0));
    return iv;
}

} // namespace pinpoint::analysis
