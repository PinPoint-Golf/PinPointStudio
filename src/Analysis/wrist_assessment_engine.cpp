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

#include "wrist_assessment_engine.h"

#include "assessment_rules.h"

#include <algorithm>

namespace pinpoint::analysis {

namespace {
// Auto-detect the band archetype from the lead-wrist face reading at the Top (design §12 Q3).
// Δ-from-address of flex-ext @P4: strongly bowed → Bowed, strongly cupped → Cupped, else Neutral.
int detectArchetype(const PpWristAngleSet &set, double topDeltaDeg)
{
    const PpWristAngleCell &p1 = set.cell(PpJointDof::LeadWristFlexExt, PpSwingPosition::P1);
    const PpWristAngleCell &p4 = set.cell(PpJointDof::LeadWristFlexExt, PpSwingPosition::P4);
    if (p1.status != PpCellStatus::Ok || p4.status != PpCellStatus::Ok)
        return 0;                                  // no Top reading → neutral
    const double topDelta = p4.valueDeg - p1.valueDeg;
    if (topDelta >  topDeltaDeg) return 1;         // bowed
    if (topDelta < -topDeltaDeg) return 2;         // cupped
    return 0;                                      // neutral
}
} // namespace

PpWristAssessmentResult WristAssessmentEngine::assess(const IWristAngleSource &source,
                                                      const IReferenceBandProvider &provider,
                                                      const WristAssessmentConfig &cfg)
{
    PpWristAssessmentResult result;
    result.handedness = source.handedness();

    const PpWristAngleSet set = WristAngleSampler::sample(source, cfg.sampling);

    // Resolve the band model: archetype < 0 = Auto (detect from this swing), else the manual choice.
    BandContext bandCtx = cfg.band;
    bandCtx.archetype   = (cfg.band.archetype < 0)
                              ? detectArchetype(set, cfg.rules.archetypeTopDeltaDeg) : cfg.band.archetype;
    result.archetype    = bandCtx.archetype;

    for (int d = 0; d < kNumDof; ++d) {
        const auto dof = static_cast<PpJointDof>(d);
        PpDofRow &rowOut = result.rows[d];
        rowOut.dof = dof;

        const PpJointAngleSeries *series = source.series(dof);
        rowOut.present    = (series != nullptr && series->present);
        rowOut.confidence = rowOut.present ? series->baseConfidence : 0.0f;

        // Δ-from-address needs the P1 (address) value; fall back to treating address as 0 if the
        // reference cell itself is missing (never fabricate the cell value, only the reference).
        const PpWristAngleCell &p1 = set.cell(dof, PpSwingPosition::P1);
        const double p1Value = (p1.status == PpCellStatus::Ok) ? p1.valueDeg : 0.0;

        for (int p = 0; p < kNumPos; ++p) {
            const auto pos = static_cast<PpSwingPosition>(p);
            const PpWristAngleCell &in = set.cell(dof, pos);
            PpRagCell &c = rowOut.cells[p];

            c.valueDeg   = in.valueDeg;
            c.status     = in.status;
            c.confidence = in.confidence;
            c.deltaDeg   = (in.status == PpCellStatus::Ok) ? (in.valueDeg - p1Value) : 0.0;

            const Band band = provider.band(dof, pos, bandCtx);
            c.banded = band.valid;
            c.bandLo = band.greenLo;
            c.bandHi = band.greenHi;

            if (pos == PpSwingPosition::P1)
                c.rag = PpRag::Ref;                              // the reference cell (Δ ≡ 0)
            else if (in.status != PpCellStatus::Ok)
                c.rag = PpRag::Grey;                             // gap / indeterminate
            else if (!band.valid)
                c.rag = PpRag::Grey;                             // no corridor → no assessment
            else
                c.rag = classifyDelta(c.deltaDeg, band);
        }
    }

    // Tier-2 rule engine → findings (faults + strengths), with corroboration / suppression /
    // confidence / linkage applied in the registry post-pass.
    const PpSwingPositionTimeline timeline = source.timeline();
    const RuleContext ctx{ result, timeline, cfg.rules };
    const AssessmentRuleRegistry registry = AssessmentRuleRegistry::makeDefault();
    result.findings = registry.run(ctx, cfg.rules);

    // Composite score v2 — telemetry only (the Wrist headline is the resemblance score, §B.0).
    // Penalty per fault/watch finding = severity × rule weight × scale. Confidence is DELIBERATELY
    // NOT a factor: the old severity×confidence central term let a noisier swing score higher
    // (validation B2); confidence now widens the score interval (§B.7, score_uncertainty), never
    // the central value. Strengths don't penalise. Explainable + clamped.
    PpScoreBreakdown &b = result.score;
    b.base = 100;
    double penalty = 0.0;
    for (const PpWristFinding &f : result.findings) {
        if (f.severity == PpFindingSeverity::Good)
            continue;
        const double sevW = (f.severity == PpFindingSeverity::Fault)
                            ? cfg.rules.severityWeightFault : cfg.rules.severityWeightWatch;
        const double pen = sevW * f.weight * cfg.rules.scoreScale;
        penalty += pen;
        b.contributions.push_back({ f.id, f.name, pen });
    }
    b.total = std::clamp(static_cast<int>(std::lround(b.base - penalty)), 0, 100);
    return result;
}

} // namespace pinpoint::analysis
