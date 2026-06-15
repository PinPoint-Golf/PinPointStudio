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

#include <functional>
#include <memory>

// The default Tier-2 rule set (design §7.3 faults + the strengths) and the registry that owns it.
// Rules are data: each is a declarative RuleDef (metadata + a predicate over the Tier-1 result),
// wrapped in a generic PredicateRule. The registry's run() applies the relationship post-pass
// (confidence finalisation, corroboration, linkage, mutual-exclusion, suppression, demotion, the
// strengths policy) — design §7.2/§7.4.

namespace pinpoint::analysis {

struct RuleDef {
    QString id, name, category;
    std::vector<PpJointDof>      dofs;
    std::vector<PpSwingPosition> positions;
    QStringList ballFlight;
    QString explanation, coaching, protect;
    float  ruleBase = 0.8f;   // intrinsic confidence (before data / corroboration)
    double weight   = 1.0;    // clinical importance → score v2
    // Predicate: the {severity, magnitude°} when present, else nullopt.
    std::function<std::optional<std::pair<PpFindingSeverity, double>>(const RuleContext &)> test;
};

class PredicateRule : public IAssessmentRule {
public:
    explicit PredicateRule(RuleDef def) : m_def(std::move(def)) {}
    std::optional<PpWristFinding> evaluate(const RuleContext &ctx) const override;
    const RuleDef &def() const { return m_def; }
private:
    RuleDef m_def;
};

class AssessmentRuleRegistry {
public:
    static AssessmentRuleRegistry makeDefault();
    std::vector<PpWristFinding> run(const RuleContext &ctx, const RuleTuning &tuning) const;

private:
    std::vector<std::unique_ptr<PredicateRule>> m_rules;
};

} // namespace pinpoint::analysis
