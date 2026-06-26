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

#include "wrist_analysis_adapter.h"

#include <QString>
#include <QVariantList>

#include <algorithm>
#include <optional>

namespace pinpoint::analysis {

namespace {

// Source-aware base confidence per DOF (design §4) — wrist IMU high, forearm rotation lower.
float baseConfFor(PpJointDof dof)
{
    switch (dof) {
    case PpJointDof::LeadWristRadUln:  return 0.86f;
    case PpJointDof::LeadWristFlexExt: return 0.84f;
    case PpJointDof::LeadForearmRot:   return 0.72f;
    case PpJointDof::LeadElbowFlex:    return 0.60f;
    default:                           return 0.50f;
    }
}

} // namespace

InMemoryWristAngleSource parseAnalysisDetail(const QVariantMap &analysisDetail)
{
    InMemoryWristAngleSource src;
    src.setHandedness(PpHandedness::Right);   // producer values are lead-arm convention — no mirror

    // series[] → per-DOF angle time series (continuous; the sampler medians at each checkpoint).
    const QVariantList series = analysisDetail.value(QStringLiteral("series")).toList();
    for (const QVariant &sv : series) {
        const QVariantMap m = sv.toMap();
        const std::optional<PpJointDof> dof =
            dofForMetricKey(m.value(QStringLiteral("key")).toString());
        if (!dof)
            continue;                          // not a wrist DOF (or unknown key)

        const QVariantList tUs = m.value(QStringLiteral("t_us")).toList();
        const QVariantList val = m.value(QStringLiteral("value")).toList();
        const int n = std::min(tUs.size(), val.size());
        if (n == 0)
            continue;

        PpJointAngleSeries ser;
        ser.dof            = *dof;
        ser.present        = true;
        ser.baseConfidence = baseConfFor(*dof);
        ser.samples.reserve(n);
        for (int i = 0; i < n; ++i) {
            PpJointAngleSample s;
            s.t_us          = tUs.at(i).toLongLong();
            s.valueDeg      = val.at(i).toDouble();
            s.available     = true;
            s.confidence    = 1.0f;
            s.pitchProxyDeg = 0.0;             // no gimbal proxy in live MetricSeries yet
            ser.samples.push_back(s);
        }
        src.setSeries(ser);
    }

    // phases[] → checkpoint timeline: locate each checkpoint's Phase event (absent → unavailable).
    const QVariantList phases = analysisDetail.value(QStringLiteral("phases")).toList();
    const WristCheckpoint *cps = wristCheckpoints();
    PpSwingPositionTimeline tl;
    for (int c = 0; c < kNumPos; ++c) {
        for (const QVariant &pv : phases) {
            const QVariantMap pm = pv.toMap();
            if (pm.value(QStringLiteral("phase")).toInt() != cps[c].phase)
                continue;
            PpSwingPositionTimeline::Entry e;
            e.present = true;
            e.t_us    = pm.value(QStringLiteral("t_us")).toLongLong();
            e.conf    = static_cast<float>(pm.value(QStringLiteral("conf")).toDouble());
            tl.positions[c] = e;
            break;
        }
    }
    src.setTimeline(tl);
    return src;
}

InMemoryWristAngleSource buildWristAngleSource(const std::vector<MetricSeries> &series,
                                               const std::vector<PhaseEvent> &phases)
{
    InMemoryWristAngleSource src;
    src.setHandedness(PpHandedness::Right);   // producer values are lead-arm convention — no mirror

    for (const MetricSeries &m : series) {
        const std::optional<PpJointDof> dof = dofForMetricKey(m.key);
        if (!dof)
            continue;                          // not a wrist DOF (or unknown key)
        const int n = static_cast<int>(std::min(m.t_us.size(), m.value.size()));
        if (n == 0)
            continue;

        PpJointAngleSeries ser;
        ser.dof            = *dof;
        ser.present        = true;
        ser.baseConfidence = baseConfFor(*dof);
        ser.samples.reserve(n);
        for (int i = 0; i < n; ++i) {
            PpJointAngleSample s;
            s.t_us          = m.t_us[i];
            s.valueDeg      = m.value[i];
            s.available     = true;
            s.confidence    = 1.0f;
            s.pitchProxyDeg = 0.0;             // no gimbal proxy in MetricSeries yet
            ser.samples.push_back(s);
        }
        src.setSeries(ser);
    }

    const WristCheckpoint *cps = wristCheckpoints();
    PpSwingPositionTimeline tl;
    for (int c = 0; c < kNumPos; ++c) {
        for (const PhaseEvent &e : phases) {
            if (static_cast<int>(e.phase) != cps[c].phase)
                continue;
            PpSwingPositionTimeline::Entry en;
            en.present = true;
            en.t_us    = e.t_us;
            en.conf    = e.conf;
            tl.positions[c] = en;
            break;
        }
    }
    src.setTimeline(tl);
    return src;
}

} // namespace pinpoint::analysis
