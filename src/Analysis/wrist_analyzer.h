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

#include "shot_analyzer.h"

// Real Wrist-session (SessionController::Type::Wrist == 1) analyzer. Runs the M1
// IMU-only chain on a QtConcurrent worker over the frozen window:
//   ImuVisionFuser (q_anat = A·q_raw·M, resampled) → PhaseSegmenter (Address/Top/
//   Impact) → MetricExtractor (lead-wrist flex/ext + radial/ulnar, forearm pronation,
//   elbow flexion as MetricSeries) → score + trace + SwingAnalysis detail.
// Degrades gracefully: returns ok=false (lands on the carousel with score 0) when no
// usable IMU data is present, rather than crashing.
class WristAnalyzer : public ShotAnalyzer
{
public:
    ShotAnalysisResult analyze(const pinpoint::SwingWindow &window,
                               const ShotAnalysisJob &job) override;
};
