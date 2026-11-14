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

#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <memory>

#include "swing_analysis.h"
#include "shot_analysis_job.h"   // ShotAnalysisJob — the value an analysis consumes (step 1.1)

namespace pinpoint { class SwingWindow; }

// Result shapes mirror the ShotListModel roles so the join can hand them to
// addShot() unmodified: metrics is key → { label, value }, tracePoints is a
// normalised (0..1) QPointF list.
struct ShotAnalysisResult {
    bool         ok = false;
    int          score = 0;        // 0–100 quality
    QVariantMap  metrics;
    QVariantList tracePoints;
    QString      error;

    // Rich analyzed-swing detail (skeleton/series/score/faults). Null for the
    // stub analyzers; folded into swing.json's "analysis" object at the join.
    std::shared_ptr<pinpoint::analysis::SwingAnalysis> detail;
};

// Abstract shot analyzer. analyze() runs on a QtConcurrent
// worker thread while the EventBuffer is Paused; it may only read the frozen
// SwingWindow (const methods, zero-copy) and the job values. It runs
// concurrently with the swing export over the same window — both are pure
// readers of frozen ring memory.
class ShotAnalyzer
{
public:
    virtual ~ShotAnalyzer() = default;
    virtual ShotAnalysisResult analyze(const pinpoint::SwingWindow &window,
                                       const ShotAnalysisJob &job) = 0;
};

// The analyzer for every swing: the full pipeline, whose stages gate on the data
// and devices the swing has, never on its session type (issue #111). Never
// returns nullptr.
std::unique_ptr<ShotAnalyzer> makeShotAnalyzer();
