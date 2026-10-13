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

#include "wrist_assessment_contract.h"

#include <QString>
#include <QStringList>

#include <array>
#include <vector>

// The Tier-1 assessment RESULT (design §9.1) — a per-cell RAG matrix + a simple composite score and
// its explainable breakdown. Plain value types so the engine is unit-tested without Qt-GUI; the
// QML-facing model converts these into QVariant lists. Findings/strengths (Tier 2) are added to this
// struct in Phase 2.

namespace pinpoint::analysis {

// One assessed cell: the sampled value, its Δ-from-address, the Tier-1 RAG, the sampling status, the
// confidence, and the band corridor used (for drawing the strip's shaded corridor).
struct PpRagCell {
    double       valueDeg   = 0.0;   // neutral-relative degrees (from the sampler)
    double       deltaDeg   = 0.0;   // Δ from address (value@Pn − value@P1)
    PpRag        rag        = PpRag::Grey;
    PpCellStatus status     = PpCellStatus::Gap;
    float        confidence = 0.0f;
    bool         banded     = false; // a valid band existed for this cell
    double       bandLo     = 0.0;   // green corridor (Δ) — meaningful when banded
    double       bandHi     = 0.0;
};

// One DOF's row of P1–P8 cells. `present` = the DOF had a series this swing (else every cell is a
// gap). `confidence` is the DOF's source-aware base confidence (design §4), surfaced for the strip
// confidence pips.
struct PpDofRow {
    PpJointDof dof = PpJointDof::LeadWristFlexExt;
    bool       present    = false;
    float      confidence = 0.0f;
    std::array<PpRagCell, kNumPos> cells{};
};

// One contribution to the composite score — a fault/watch finding and the penalty it cost (design
// §7.6, score v2 = findings-weighted).
struct PpScoreContribution {
    QString id;
    QString name;
    double  penalty = 0.0;
};

// The composite-score breakdown (design §7.6) — base minus the summed penalties, fully explainable
// (the contributions sum to base − total).
struct PpScoreBreakdown {
    int                              base  = 100;
    int                              total = 100;   // the displayed score (clamped 0..100)
    std::vector<PpScoreContribution> contributions;
};

// ── Tier-2 finding (design §7.2) — a named fault or strength ─────────────────────────────────
enum class PpFindingSeverity { Good, Watch, Fault };

inline const char *severityName(PpFindingSeverity s)
{
    switch (s) {
    case PpFindingSeverity::Good:  return "good";
    case PpFindingSeverity::Watch: return "watch";
    case PpFindingSeverity::Fault: return "fault";
    }
    return "watch";
}

// `protect` is set for strengths (severity == Good); `linkedTo` carries a sibling id (cast↔flip);
// `corroboratedBy` lists independent confirming signals; `lowConfidence` demotes (never drops).
struct PpWristFinding {
    QString                      id;
    QString                      name;
    QString                      category;     // face | strike | power | sequence | structure
    PpFindingSeverity            severity     = PpFindingSeverity::Fault;
    double                       magnitudeDeg = 0.0;
    double                       weight       = 1.0;   // clinical importance → score v2
    float                        confidence   = 1.0f;
    bool                         lowConfidence = false;
    std::vector<PpJointDof>      dofs;
    std::vector<PpSwingPosition> positions;
    QStringList                  ballFlight;
    QString                      explanation;
    QString                      coaching;     // fault: feel/drill
    QString                      protect;      // strength: what to keep
    QStringList                  corroboratedBy;
    QString                      linkedTo;
};

// The whole result (Tier-1 RAG matrix + score + Tier-2 findings).
struct PpWristAssessmentResult {
    PpHandedness                          handedness = PpHandedness::Unknown;
    int                                   archetype  = 0;   // the RESOLVED band model (0/1/2) — what
                                                            // Auto picked, or the manual selection
    std::array<PpDofRow, kNumDof>         rows{};       // indexed by (int)PpJointDof
    PpScoreBreakdown                      score;
    std::vector<PpWristFinding>           findings;     // faults + strengths (Tier 2)

    const PpDofRow &row(PpJointDof d) const { return rows[static_cast<int>(d)]; }
};

} // namespace pinpoint::analysis
