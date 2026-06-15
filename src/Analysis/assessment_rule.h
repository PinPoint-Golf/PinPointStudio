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

#include "wrist_assessment_result.h"

#include <optional>

// Tier-2 rule-engine vocabulary (design §7.2). Rules read the Tier-1 result (per-cell Δ / RAG /
// confidence already computed) and emit Findings — named faults AND strengths (PpWristFinding lives
// in wrist_assessment_result.h, which the result contains). Severity (how bad) and confidence (how
// much we trust it) are orthogonal. Pure value types; no Qt-GUI.

namespace pinpoint::analysis {

// What a rule reads. Thin accessors over the Tier-1 result + the segmentation timeline.
struct RuleContext {
    const PpWristAssessmentResult &result;
    const PpSwingPositionTimeline &timeline;

    bool present(PpJointDof d) const { return result.row(d).present; }
    const PpRagCell &cell(PpJointDof d, PpSwingPosition p) const
    {
        return result.row(d).cells[static_cast<int>(p)];
    }
    bool   ok(PpJointDof d, PpSwingPosition p) const { return cell(d, p).status == PpCellStatus::Ok; }
    double delta(PpJointDof d, PpSwingPosition p) const { return cell(d, p).deltaDeg; }
    PpRag  rag(PpJointDof d, PpSwingPosition p) const { return cell(d, p).rag; }
    bool   banded(PpJointDof d, PpSwingPosition p) const { return cell(d, p).banded; }
};

// Tunable Tier-2 knobs (design §7.6 / §11 — starting heuristics). Owned by WristAssessmentConfig.
struct RuleTuning {
    float  confidenceFloor   = 0.45f;   // confidence below this → lowConfidence (demoted, not dropped)
    double scoreScale        = 18.0;    // score v2 penalty scale
    double severityWeightFault = 1.0;
    double severityWeightWatch = 0.5;
    double corroborationBoost  = 0.30;  // confidence multiplier add when corroborated (capped at 1.0)
    bool   strengthsRequireAdjacentFault = true;   // curated "protect this" — see assessment_rules.cpp
};

// The abstract rule seam. A concrete rule inspects the context and optionally emits one finding.
class IAssessmentRule {
public:
    virtual ~IAssessmentRule() = default;
    virtual std::optional<PpWristFinding> evaluate(const RuleContext &ctx) const = 0;
};

} // namespace pinpoint::analysis
