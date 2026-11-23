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

// A Tier-2 wrist finding: the value type the shot's analysis record holds (SwingAnalysis::findings,
// swing_analysis.h). Vocabulary, so the record does not include the wrist assessment's rules; the
// Tier-1 result around it stays with the assessment (Wrist/wrist_assessment_result.h, which
// includes this). Moved out of wrist_assessment_result.h unchanged (refactor 7.3, A-11).

#include "wrist_assessment_types.h"   // PpJointDof, PpSwingPosition

#include <QString>
#include <QStringList>

#include <vector>

namespace pinpoint::analysis {

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

} // namespace pinpoint::analysis
