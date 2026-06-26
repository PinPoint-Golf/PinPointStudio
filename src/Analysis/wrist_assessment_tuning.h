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

#include <QVariantMap>

#include "analysis_tuning.h"
#include "wrist_assessment_engine.h"

// SwingLab tuning for the Tier-1/2 wrist assessment engine. Builds a WristAssessmentConfig
// from dotted-key overrides so the windowed-median sampler, the Tier-2 rule knobs, and the
// reference-band amber margins are all sweepable without a rebuild (analysis_tuning.h):
//   sampler.windowHalfUs / .gimbalThresholdDeg / .minValidSamples
//   rules.confidenceFloor / .scoreScale / .severityWeightFault / .severityWeightWatch
//          / .corroborationBoost / .strengthsRequireAdjacentFault
//   bands.radUlnMargin / .flexExtMargin / .forearmMargin / .trailWristMargin / .elbowMargin
// Empty map ⇒ all frozen defaults (the production path). Defaults live in pp_tuned_constants.h.

namespace pinpoint::analysis {

inline WristAssessmentConfig wristAssessmentConfigFor(const QVariantMap &ov)
{
    WristAssessmentConfig cfg;
    if (ov.isEmpty())
        return cfg;
    namespace tn = pinpoint::analysis::tuning;

    tn::apply(ov, "sampler.windowHalfUs",       cfg.sampling.windowHalfUs);
    tn::apply(ov, "sampler.gimbalThresholdDeg", cfg.sampling.gimbalThresholdDeg);
    tn::apply(ov, "sampler.minValidSamples",    cfg.sampling.minValidSamples);

    tn::apply(ov, "rules.confidenceFloor",               cfg.rules.confidenceFloor);
    tn::apply(ov, "rules.scoreScale",                    cfg.rules.scoreScale);
    tn::apply(ov, "rules.severityWeightFault",           cfg.rules.severityWeightFault);
    tn::apply(ov, "rules.severityWeightWatch",           cfg.rules.severityWeightWatch);
    tn::apply(ov, "rules.corroborationBoost",            cfg.rules.corroborationBoost);
    tn::apply(ov, "rules.strengthsRequireAdjacentFault", cfg.rules.strengthsRequireAdjacentFault);

    tn::apply(ov, "bands.radUlnMargin",     cfg.band.tuning.radUlnMargin);
    tn::apply(ov, "bands.flexExtMargin",    cfg.band.tuning.flexExtMargin);
    tn::apply(ov, "bands.forearmMargin",    cfg.band.tuning.forearmMargin);
    tn::apply(ov, "bands.trailWristMargin", cfg.band.tuning.trailWristMargin);
    tn::apply(ov, "bands.elbowMargin",      cfg.band.tuning.elbowMargin);
    return cfg;
}

} // namespace pinpoint::analysis
