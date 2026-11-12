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

// The per-shot assembly of `shaftDirection` (ShaftDirectionStage, analysis_pipeline.cpp): the
// shaft_direction.h curve and its two position readings turned into the MetricSeries the analysis
// publishes, and the trust records that go with it. Header-only, so shaft_direction_test reaches the
// whole of it — route, σ, records — without the pipeline.
//
// THE RECORDS, both Downgraded (never refused: the reading stands, is judged, and carries its ⚠),
// subject the reading itself, `covers` naming it:
//   · club3d.cameraAssumed — the down-the-line camera is uncalibrated, so its direction is assumed
//     and every bearing carries σ_yaw (evidence sigmaYawDeg);
//   · club3d.shaftDirectionFromFit — a GRADED position reading came from the 3-D skeleton's shaft
//     rather than the fused one (code p2 / top / both). Skeleton frames elsewhere on the curve are
//     not a graded reading and do not fire it; the curve's share is in the evidence and the log.

#include "shaft_direction.h"
#include "../Analysis/swing_analysis.h"
#include "../Analysis/gate_record.h"

#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <cmath>
#include <optional>
#include <vector>

namespace pinpoint::analysis {

struct ShaftDirectionInput {
    std::vector<fusion::Sample3D> fused;        // the fused track's samples (empty when fusion did not run)
    std::vector<shaftdir::FitFrame> fit;        // the 3-D skeleton's shaft (empty without a two-camera fit)
    std::optional<int64_t> p2Us, topUs;         // ShaftParallelBack and Top on the ladder, when present
    bool calibrated = false;                    // the down-the-line camera's calibration reached the job
    bool leftHanded = false;
    bool withSigma  = true;                     // uncertainty.enabled
};

struct ShaftDirectionOutput {
    bool               ok = false;              // a series came out
    MetricSeries       series;
    std::vector<GateRecord> gates;
    shaftdir::Assembly assembly;                // the curve and the two readings, for the log and tests
    shaftdir::Reading  p2, top;                 // src None = no reading at that position
    double             yawSigmaDeg = 0.0;
};

inline ShaftDirectionOutput buildShaftDirection(const ShaftDirectionInput &in)
{
    ShaftDirectionOutput out;
    shaftdir::Config cfg;
    cfg.leftHanded  = in.leftHanded;
    cfg.withSigma   = in.withSigma;
    cfg.yawSigmaDeg = in.calibrated ? tuned::dtlCamera::kDtlYawSigmaCalDeg : tuned::dtlCamera::kDtlYawSigmaUncalDeg;
    out.yawSigmaDeg = cfg.yawSigmaDeg;

    std::vector<shaftdir::PhaseIn> phases;
    if (in.p2Us)  phases.push_back({ int(Phase::ShaftParallelBack), *in.p2Us });
    if (in.topUs) phases.push_back({ int(Phase::Top), *in.topUs });
    out.assembly = shaftdir::assemble(in.fused, in.fit, phases, cfg);
    const shaftdir::Curve &curve = out.assembly.curve;
    if (curve.points.size() < 2) return out;

    MetricSeries &m = out.series;
    m.key   = QStringLiteral("shaftDirection");
    m.label = QStringLiteral("Shaft direction");
    m.unit  = QStringLiteral("°");
    m.routeId     = QStringLiteral("faceOn+dtl");
    m.routeDirect = false;
    bool anyBridged = false;
    for (const shaftdir::Point &p : curve.points) {
        m.t_us.push_back(p.t_us);
        m.value.push_back(p.deg);
        m.valid.push_back(p.valid ? 1 : 0);
        anyBridged |= !p.valid;
    }
    if (!anyBridged) m.valid.clear();
    if (std::isfinite(curve.medianSigmaDeg)) {
        m.sigma = curve.medianSigmaDeg;
        m.sigmaKind = uint8_t(SigmaKind::Propagated);
    }
    // The readings sit at the positions' instants; assemble() made each one the grader's own median
    // over the curve there (or, with no valid frame in that window, the frame the grader falls back to).
    QStringList fromFit;
    for (const shaftdir::PhaseOut &po : out.assembly.phases) {
        const Phase ph = Phase(po.id);
        (ph == Phase::Top ? out.top : out.p2) = po.r;
        if (po.r.src == shaftdir::Source::None || !std::isfinite(po.r.deg)) continue;
        PhaseSample ps{ ph, po.tUs, po.r.deg, QString() };
        if (std::isfinite(po.r.sigmaDeg)) { ps.sigma = po.r.sigmaDeg; ps.sigmaKind = uint8_t(SigmaKind::Propagated); }
        m.phaseSamples.push_back(ps);
        if (po.r.src == shaftdir::Source::Fit) fromFit << (ph == Phase::Top ? QStringLiteral("top") : QStringLiteral("p2"));
    }
    out.ok = true;

    const QStringList covers{ QStringLiteral("shaftDirection") };
    if (!in.calibrated)
        out.gates.push_back(GateRecord{ PP_GATE("club3d.cameraAssumed"), QStringLiteral("shaftDirection"),
                                        GateEffect::Downgraded,
                                        QVariantMap{ { QStringLiteral("sigmaYawDeg"), cfg.yawSigmaDeg } }, {}, {}, covers });
    if (!fromFit.isEmpty())
        out.gates.push_back(GateRecord{ PP_GATE("club3d.shaftDirectionFromFit"), QStringLiteral("shaftDirection"),
                                        GateEffect::Downgraded,
                                        QVariantMap{ { QStringLiteral("code"), fromFit.size() == 2 ? QStringLiteral("both")
                                                                                                   : fromFit.front() },
                                                     { QStringLiteral("fitFrames"), curve.nFit },
                                                     { QStringLiteral("ofFrames"), curve.nFit + curve.nFusion } },
                                        {}, {}, covers });
    return out;
}

} // namespace pinpoint::analysis
