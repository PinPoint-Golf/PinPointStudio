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

#include "wrist_analyzer.h"

#include <QPointF>
#include <QString>
#include <algorithm>
#include <cmath>

#include "fusion/imu_vision_fuser.h"
#include "metrics/metric_extractor.h"
#include "phase/phase_segmenter.h"
#include "swing_window.h"
#include "../Core/pp_debug.h"

using namespace pinpoint::analysis;

namespace {

const MetricSeries *find(const std::vector<MetricSeries> &v, const QString &key)
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

// Short carousel value string: signed degrees (descriptors come once the signs are
// locked on the "check your sensors" page).
QString fmtDeg(double v)
{
    const long r = std::lround(v);
    return (r > 0 ? QStringLiteral("+") : QString()) + QString::number(r) + QStringLiteral("°");
}

// Build the flat key → {label, value} map the carousel renders, sampled at Impact.
QVariantMap buildMetricsMap(const std::vector<MetricSeries> &series)
{
    QVariantMap out;
    for (const MetricSeries &m : series) {
        out.insert(m.key, QVariantMap{
            { QStringLiteral("label"), m.label },
            { QStringLiteral("value"), fmtDeg(phaseValue(m, Phase::Impact)) },
        });
    }
    return out;
}

// Lead-wrist FE curve from Address → Impact, ~24 points, normalised to 0..1 (y up
// = more flexion) for the existing PpTrace sparkline.
QVariantList buildTrace(const std::vector<MetricSeries> &series,
                        const std::vector<PhaseEvent> &phases)
{
    QVariantList out;
    const MetricSeries *fe = find(series, QStringLiteral("leadWristFlexExt"));
    if (!fe || fe->value.empty() || fe->t_us.empty())
        return out;

    int64_t addrT = fe->t_us.front(), impT = fe->t_us.back();
    for (const PhaseEvent &e : phases) {
        if (e.phase == Phase::Address) addrT = e.t_us;
        if (e.phase == Phase::Impact)  impT  = e.t_us;
    }
    const auto lo = std::lower_bound(fe->t_us.begin(), fe->t_us.end(), addrT) - fe->t_us.begin();
    auto hi = std::lower_bound(fe->t_us.begin(), fe->t_us.end(), impT) - fe->t_us.begin();
    hi = std::min<long>(hi, static_cast<long>(fe->value.size()) - 1);
    if (hi <= lo) return out;

    double vmin = fe->value[lo], vmax = fe->value[lo];
    for (long i = lo; i <= hi; ++i) { vmin = std::min(vmin, fe->value[i]); vmax = std::max(vmax, fe->value[i]); }
    const double span = std::max(vmax - vmin, 1e-6);

    const int kPts = 24;
    for (int k = 0; k < kPts; ++k) {
        const double f = double(k) / (kPts - 1);
        const long i = lo + std::lround(f * (hi - lo));
        const double x = f;
        const double y = 0.9 - 0.8 * (fe->value[i] - vmin) / span;   // y-down, flexion up
        out.append(QPointF(x, y));
    }
    return out;
}

// Provisional first-pass score (the proper multi-metric SwingScorer is the next
// increment). Tolerance-band sub-scores at Impact + weighted geometric mean. Signs
// are provisional until locked on "check your sensors", so the bands are lenient.
double bandScore(double value, double mu, double sigma)
{
    const double z = std::abs(value - mu) / sigma;
    if (z <= 1.0) return 100.0;
    if (z >= 3.0) return 1.0;
    const double t = (z - 1.0) / 2.0;
    return std::max(1.0, 100.0 * (1.0 - t * t));
}

ScoreBreakdown scoreWrist(const std::vector<MetricSeries> &series)
{
    struct Band { const char *key; double mu, sigma, weight; };
    static const Band kBands[] = {
        { "leadWristFlexExt", 15.0, 18.0, 0.5 },   // a flat-to-flexed lead wrist at impact
        { "leadArmFlexion",    5.0, 15.0, 0.3 },   // near-straight lead arm
        { "forearmPronation",  0.0, 30.0, 0.2 },   // square-ish through impact
    };
    ScoreBreakdown sb;
    double wsum = 0.0, lnsum = 0.0;
    for (const Band &b : kBands) {
        const MetricSeries *m = find(series, QString::fromLatin1(b.key));
        if (!m) continue;
        const double v  = phaseValue(*m, Phase::Impact);
        const double ss = bandScore(v, b.mu, b.sigma);
        sb.metrics.push_back(ScoredMetric{ m->key, v, b.mu, b.sigma, false, ss, QString(), Phase::Impact, b.weight });
        wsum  += b.weight;
        lnsum += b.weight * std::log(std::max(ss, 1.0));
    }
    sb.overall = wsum > 0.0 ? int(std::lround(std::exp(lnsum / wsum))) : 0;
    return sb;
}

} // namespace

ShotAnalysisResult WristAnalyzer::analyze(const pinpoint::SwingWindow &window,
                                          const ShotAnalysisJob &job)
{
    ShotAnalysisResult r;

    const FusedStreams streams = ImuVisionFuser::fuse(window, job.imuBindings);
    if (streams.timeGrid.empty() || streams.segments.empty()) {
        r.ok = false;
        r.error = QStringLiteral("no IMU data in window");
        ppWarn() << "[WristAnalysis] no fusable IMU streams — degraded result";
        return r;
    }

    const std::vector<PhaseEvent>  phases = PhaseSegmenter::segment(streams, job.impactUs);
    const std::vector<MetricSeries> series = MetricExtractor::extract(streams, phases, job.handedness);
    if (series.empty()) {
        r.ok = false;
        r.error = QStringLiteral("no wrist metrics (need forearm + hand IMUs)");
        return r;
    }

    auto detail   = std::make_shared<SwingAnalysis>();
    detail->series = series;
    detail->phases = phases;
    detail->tier   = static_cast<int>(ReconstructionTier::Mono3DPlusImu);
    detail->score  = scoreWrist(series);

    r.metrics     = buildMetricsMap(series);
    r.tracePoints = buildTrace(series, phases);
    r.score       = detail->score.overall;
    r.detail      = detail;
    r.ok          = true;

    ppInfo() << "[WristAnalysis]" << series.size() << "metrics, score" << r.score
             << "— grid" << static_cast<qint64>(streams.timeGrid.size());
    return r;
}
