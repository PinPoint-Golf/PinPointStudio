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

#include "shot_analyzer.h"
#include "wrist_analyzer.h"

#include <QPointF>
#include <algorithm>
#include <cmath>

#include "swing_window.h"
#include "../Core/pp_debug.h"

namespace {

// Deterministic pseudo-random trace, normalised to 0..1 — moved verbatim from
// shot_list_model.cpp's stub seeding so each stub-analysed shot gets a distinct
// but stable curve until the real analysis pipelines land.
QVariantList stubTracePoints(int seed)
{
    QVariantList pts;
    double y = 0.6;
    for (int i = 0; i <= 12; ++i) {
        const double noise = double((seed * 9301 + i * 49297) % 233280) / 233280.0 - 0.5;
        y += (std::sin(i * 1.2 + seed) * 0.5 + noise) * 0.16;
        y = std::clamp(y, 0.16, 0.84);
        pts.append(QPointF(i / 12.0, y));
    }
    return pts;
}

// Shared stub behaviour: an immediate, deterministic placeholder result.
// Score and trace derive from the impact timestamp so each shot differs but
// re-running the same shot is stable.
ShotAnalysisResult stubResult(const pinpoint::SwingWindow &window,
                              const ShotAnalysisJob &job,
                              QVariantMap metrics)
{
    ppInfo() << "[ShotAnalysis] stub analysis — sessionType" << job.sessionType
             << "impact_ts_us" << job.impactUs
             << "window entries" << static_cast<qint64>(window.entries().size());

    ShotAnalysisResult r;
    r.ok          = true;
    r.score       = 40 + static_cast<int>((job.impactUs / 1000) % 56);  // 40–95
    r.tracePoints = stubTracePoints(static_cast<int>(job.impactUs % 97));
    r.metrics     = std::move(metrics);
    return r;
}

class SwingStubAnalyzer : public ShotAnalyzer
{
public:
    ShotAnalysisResult analyze(const pinpoint::SwingWindow &window,
                               const ShotAnalysisJob &job) override
    {
        return stubResult(window, job, {});
    }
};

class GrfStubAnalyzer : public ShotAnalyzer
{
public:
    ShotAnalysisResult analyze(const pinpoint::SwingWindow &window,
                               const ShotAnalysisJob &job) override
    {
        return stubResult(window, job, {});
    }
};

class CoachStubAnalyzer : public ShotAnalyzer
{
public:
    ShotAnalysisResult analyze(const pinpoint::SwingWindow &window,
                               const ShotAnalysisJob &job) override
    {
        return stubResult(window, job, {});
    }
};

} // namespace

std::unique_ptr<ShotAnalyzer> makeShotAnalyzer(int sessionType)
{
    switch (sessionType) {
    case 1:  return std::make_unique<WristAnalyzer>();       // Wrist (real M1 analysis)
    case 2:  return std::make_unique<GrfStubAnalyzer>();     // GRF
    case 3:  return std::make_unique<CoachStubAnalyzer>();   // Coach
    case 0:                                                  // Swing
    default: return std::make_unique<SwingStubAnalyzer>();
    }
}
