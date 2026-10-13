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

#include "assessment_rule.h"
#include "reference_bands.h"
#include "wrist_angle_sampler.h"
#include "wrist_assessment_result.h"

// The Tier-1 orchestrator (design §9.1). Pure, deterministic, Qt-GUI-free so it is unit-tested
// standalone: sample the angle source → derive Δ-from-address → classify each cell against the band
// provider → roll up a simple composite score. Tier-2 rules (findings/strengths) hang off this in
// Phase 2. The QML-facing WristDiagnosticsModel (src/Gui/diagnostics) wraps this and exposes the
// result to the view.

namespace pinpoint::analysis {

struct WristAssessmentConfig {
    PpWristSamplingConfig sampling;          // windowed-median sampling (Phase 0)
    BandContext           band;              // archetype / club / shape (v1 neutral)
    RuleTuning            rules;             // Tier-2 rule-engine + score-v2 knobs
};

struct WristAssessmentEngine {
    // Sample → Tier-1 band → Tier-2 rules → findings-weighted score (v2). The source supplies the
    // angle series + P1–P8 timeline; the provider supplies the expected corridors. Deterministic for
    // a given (source, provider, config).
    static PpWristAssessmentResult assess(const IWristAngleSource &source,
                                          const IReferenceBandProvider &provider,
                                          const WristAssessmentConfig &cfg = {});
};

} // namespace pinpoint::analysis
