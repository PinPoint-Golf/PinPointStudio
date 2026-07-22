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

#include "metric_type.h"
#include "swing_analysis.h"          // Phase, SegmentRole, ReconstructionTier
#include "wrist_assessment_types.h"  // PpJointDof

#include <QString>
#include <QStringList>

#include <optional>
#include <vector>

// MetricDescriptor — the stable identity + metadata of a metric (design §3.4). Constant for every
// shot; never names a producer (design principle 1). Per-shot availability is resolved separately
// (metric_provider.h). All header-only value types, Qt-only, no Qt-GUI.

namespace pinpoint::analysis {

// What a metric needs before any producer can satisfy it (design §3.2). The descriptor states
// requirements; it never names a provider. When no provider claims a key, the resolver renders the
// unmet requirement into a human-readable availability reason ("needs face-on camera").
struct MetricRequirement {
    bool                     faceOnCamera = false;                     // pose / shaft / ball products
    std::vector<SegmentRole> imuRoles;                                 // required anatomical IMU roles
    bool                     clubTrack   = false;                      // ShaftTrack2D present
    bool                     ballTrack   = false;                      // BallTrack2D present
    ReconstructionTier       minTier     = ReconstructionTier::Angles2D;
};

// One phase's normative corridor — same semantics as reference_bands::Band (a GREEN core with an
// AMBER margin either side; outside amber is RED). Used inline for non-DOF metrics (speeds, setup
// scalars) until a dedicated band provider lands; DOF metrics resolve through reference_bands.
struct NormativeCorridor {
    Phase  phase = Phase::Impact;
    double greenLo = 0.0, greenHi = 0.0;   // "tour core"
    double amberLo = 0.0, amberHi = 0.0;   // core ± margin
    bool   deltaFromAddress = false;       // wrist DOFs are Δ-from-address; speeds are absolute
};

// How a metric's normative / comparative values are sourced (design §3.3).
struct MetricNormative {
    // Preferred: delegate to the reference-band provider (wrist DOFs).
    std::optional<PpJointDof>      dof;
    // Fallback: inline corridors per phase (speeds, setup scalars) until a provider lands.
    std::vector<NormativeCorridor> inlineCorridors;
    QString                        contextNote;       // "mid-iron · neutral archetype" (BandContext)
    bool                           heuristic = true;  // reference_bands: every number is expected to move
};

// The metric descriptor. `key` equals the existing MetricSeries.key / ScoredMetric.key.
struct MetricDescriptor {
    QString    key;                        // == MetricSeries.key (stable identity)
    MetricType type = MetricType::TimeSeries;
    QString    label;                      // "Lead wrist — bow / cup"
    QString    shortLabel;                 // "Bow/cup" (mirrors ChartMetrics::shortLabel)
    QString    unit;                       // "°", "mph", "×frame", …
    QString    group;                      // "Wrist & forearm" | "Club & speed" | "Setup" | …

    QString    description;                // what it means (consolidated from docs/)
    QString    howToRead;                  // sign convention, when to read, what good looks like
    bool       flexPositive = true;        // sign polarity, mirrors MetricSeries.flexPositive

    std::vector<Phase> phases;             // phases sampled (PointInTime) / where peak matters (TimeSeries)
    bool               scored = false;     // has a band and contributes to a score
    // Roadmap placeholder: in the design catalogue but no producer yet (always resolves Unavailable).
    // Surfaced so the directory can badge it "Planned" — the requirement then reads as "will need …".
    bool               planned = false;

    MetricNormative    normative;
    // NB: named `requirement`, NOT `requires` — `requires` is a C++20 keyword and cannot be an identifier.
    MetricRequirement  requirement;

    // Reverse index of consumers — static, hand-authored in the manifest (design §13.2 decision).
    // Powers the detail page's "Where it's used". e.g. {"dashboard:motion","score:wrist","fault:cuppedAtTop"}.
    QStringList        usedBy;
};

} // namespace pinpoint::analysis
