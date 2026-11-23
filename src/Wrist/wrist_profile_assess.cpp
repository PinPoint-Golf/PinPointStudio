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

#include "wrist_profile_assess.h"

#include "reference_bands.h"
#include "wrist_analysis_adapter.h"
#include "wrist_assessment_engine.h"
#include "../Vocabulary/Analysis/wrist_assessment_types.h"
#include "../Diagnostics/norms/norm.h"
#include "../Diagnostics/norms/norm_provider.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>

#include <cmath>

namespace pinpoint::analysis {

namespace {

// The display order: lag → face → forearm roll → trail tray → the grid-only lead elbow.
const PpJointDof kDisplayOrder[] = {
    PpJointDof::LeadWristRadUln,
    PpJointDof::LeadWristFlexExt,
    PpJointDof::LeadForearmRot,
    PpJointDof::TrailWristFlexExt,
    PpJointDof::LeadElbowFlex,
};

// No corridor anywhere: the Δ values only (the ★ reference of another session).
struct NoBands final : IReferenceBandProvider {
    Band band(PpJointDof, PpSwingPosition, const BandContext &) const override { return {}; }
};

QString ragWord(PpRag r) { return r == PpRag::Grey ? QStringLiteral("none") : QString::fromLatin1(ragName(r)); }

struct Picked {
    std::vector<MetricSeries> series;
    std::vector<PhaseEvent>   phases;
};

Picked pick(const WristInput &in)
{
    Picked out;
    QSet<QString> seen;
    for (const WristInput::Series &s : in.series) {
        if (s.key.isEmpty() || seen.contains(s.key)) continue;   // the first copy of a key (the series model's rule)
        seen.insert(s.key);
        if (!dofForMetricKey(s.key)) continue;
        if (s.tUs.empty()) continue;
        if (isCameraWristDofSeries(s.routeId)) continue;          // the face-on elbow is not the sensors' grid
        MetricSeries m;
        m.key = s.key;
        const size_t n = std::min(s.tUs.size(), s.value.size());
        m.t_us.assign(s.tUs.begin(), s.tUs.begin() + qsizetype(n));
        m.value.assign(s.value.begin(), s.value.begin() + qsizetype(n));
        if (m.t_us.empty()) continue;
        out.series.push_back(std::move(m));
    }
    out.phases.reserve(in.phases.size());
    for (const WristInput::Phase &p : in.phases) {
        if (p.phase < 0) continue;
        PhaseEvent e;
        e.phase = Phase(p.phase);
        e.t_us  = p.tUs;
        e.conf  = std::isfinite(p.conf) ? float(p.conf) : 1.0f;
        out.phases.push_back(e);
    }
    return out;
}

// A missing sample is a gap, never a value: a NaN reads unavailable, which the sampler would otherwise median in.
InMemoryWristAngleSource sourceOf(const Picked &p)
{
    InMemoryWristAngleSource source = buildWristAngleSource(p.series, p.phases);
    for (int d = 0; d < kNumDof; ++d) {
        const PpJointAngleSeries *s = source.series(PpJointDof(d));
        if (!s) continue;
        bool any = false;
        for (const PpJointAngleSample &x : s->samples) any = any || !std::isfinite(x.valueDeg) || !std::isfinite(x.pitchProxyDeg);
        if (!any) continue;
        PpJointAngleSeries fixed = *s;
        for (PpJointAngleSample &x : fixed.samples) {
            if (!std::isfinite(x.valueDeg)) { x.valueDeg = 0.0; x.available = false; }
            if (!std::isfinite(x.pitchProxyDeg)) x.pitchProxyDeg = 0.0;
        }
        source.setSeries(fixed);
    }
    return source;
}

WristProfile assessWith(const WristInput &input, const IReferenceBandProvider &bands)
{
    WristProfile out;
    const Picked p = pick(input);
    if (p.series.empty()) return out;
    const InMemoryWristAngleSource source = sourceOf(p);
    QHash<int, QString> keyOf;   // per DOF the last key wins, as buildWristAngleSource keeps it
    for (const MetricSeries &m : p.series) keyOf.insert(int(*dofForMetricKey(m.key)), m.key);

    WristAssessmentConfig autoCfg;
    autoCfg.band.archetype = -1;
    const PpWristAssessmentResult a = WristAssessmentEngine::assess(source, bands, autoCfg);
    std::array<PpWristAssessmentResult, kWristProfileStyles> byStyle;
    for (int s = 0; s < kWristProfileStyles; ++s) {
        WristAssessmentConfig cfg;
        cfg.band.archetype = s;
        byStyle[size_t(s)] = s == a.archetype ? a : WristAssessmentEngine::assess(source, bands, cfg);
    }
    out.resolved = std::clamp(a.archetype, 0, kWristProfileStyles - 1);
    for (PpJointDof dof : kDisplayOrder) {
        const PpDofRow &row = a.row(dof);
        if (!row.present) continue;
        WristProfileRow r;
        r.dof        = QString::fromLatin1(dofName(dof));
        r.metricKey  = keyOf.value(int(dof));
        r.confidence = double(row.confidence);
        for (int c = 0; c < kNumPos && c < kWristProfilePositions; ++c) {
            const PpRagCell &cell = row.cells[size_t(c)];
            const bool ok = cell.status == PpCellStatus::Ok && std::isfinite(cell.deltaDeg);
            r.deltaDeg[size_t(c)] = ok ? cell.deltaDeg : std::numeric_limits<double>::quiet_NaN();
            for (int s = 0; s < kWristProfileStyles; ++s) {
                const PpRagCell &sc = byStyle[size_t(s)].row(dof).cells[size_t(c)];
                WristProfileGrade &g = r.grades[size_t(s)][size_t(c)];
                g.rag    = ragWord(sc.rag);
                g.banded = sc.banded;
                g.lo     = sc.banded ? sc.bandLo : std::numeric_limits<double>::quiet_NaN();
                g.hi     = sc.banded ? sc.bandHi : std::numeric_limits<double>::quiet_NaN();
            }
        }
        out.rows.push_back(std::move(r));
    }
    out.present = !out.rows.empty();
    return out;
}

} // namespace

WristProfile assessWristProfile(const WristInput &input, const std::shared_ptr<const INormProvider> &norms,
                                const GradePolicy &policy)
{
    if (!norms) return wristDeltasOnly(input);
    const NormBandProvider bands(norms, policy);
    return assessWith(input, bands);
}

WristProfile wristDeltasOnly(const WristInput &input)
{
    const NoBands bands;
    return assessWith(input, bands);
}

} // namespace pinpoint::analysis
