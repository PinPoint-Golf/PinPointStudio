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

// The per-shot assembly of the readings from the 3-D body (Skeleton3dBodyStage,
// analysis_pipeline.cpp): skeleton3d_body_metrics.h's per-frame geometry turned into the series the
// analysis publishes, the trust record that goes with them, and the precedence rules that decide
// which of them reach detail.series. Header-only, so skeleton3d_body_metrics_test reaches all of
// it — values, σ, records and precedence — without the pipeline.
//
// THE SERIES, all `routeId = "faceOn+dtl"`, `routeDirect = false` (the fit is uncalibrated, so every
// rung they answer is Estimated), σ Propagated on the series and on every phase sample:
//   shoulderPlaneAngle3d  curve, read at the top
//   shoulderLineYaw       one reading at address (a PointInTime: no curve), σ = line σ ⊕ σ_yaw
//   leadKneeFlexion       curve, read at address, the top, impact and P8
//   spineSideBend         curve, read at address, the top and impact
//   thighTurn3dLead / thighTurn3dTrail   research-only curves (isResearchOnlySeries), read at
//                         address, the top and impact — W3's hip internal rotation reads them
//
// THE CURVE is the fit's frame grid from the first measured frame to the last; a frame with no
// reading between two that have one is BRIDGED (the straight line, valid = 0), never a measurement.
// A PHASE READING IS WHAT THE GRADER READS (measure_sample.cpp → series_reduce.h reduceAt): the
// median of the curve's valid frames within ±sampler::kWindowHalfUs of the ladder's instant, and its σ
// the median of those frames' σs. A position whose window holds no valid frame has NO reading — the
// grader would fall back to a phase sample, and there is none to fall back to.
//
// THE RECORD, Downgraded (the reading stands, is judged, and carries its ⚠), subject and `covers` the
// one reading it is true of: body3d.cameraAssumed — the cameras' directions are assumed, so where
// the shoulders aim carries ±σ_yaw. Fired only when the shoulderLineYaw reading was published. The
// tilts and the knee angle do not carry it: an inclination does not turn with the camera's heading.
//
// PRECEDENCE (mergeSkeleton3dBody; IMU > 3-D > single camera, the plan's rule):
//   · shoulderPlaneAngle3d / shoulderLineYaw: no other producer; published unless already present.
//   · leadKneeFlexion: the 3-D series REPLACES the down-the-line one when it has a reading in the
//     Impact → ShaftParallelThrough window (what m_leadKneeFlex grades). Otherwise the down-the-line
//     series stays exactly as its producer made it (dtl_posture.h), and the 3-D one is not published
//     beside it. With no down-the-line series at all, the 3-D one is published whatever it covers.
//   · spineSideBend: published only when the face-on series is absent (the face-on route is the
//     ladder's better rung and stays the answer wherever it read).
//   · the thigh turns: research only, published unless already present.
//
// THE SUBSTITUTE RECORD, body3d.fromFit (Downgraded, subject and covers the reading, evidence code /
// route / replaces — route_substitution.h's marker): fired when the 3-D route supplied a reading
// because the single-camera route had none — the side bend wherever it is published (the face-on
// series was absent), the lead knee when there was no down-the-line series. NOT when the 3-D lead
// knee replaces a down-the-line one: that is the ladder's better rung winning, not a fallback. After
// the run, reconcileSubstitutedReadings (runProfile) takes the failed route's refusals off the
// reading and lists them in this record's `replacedRecords`.

#include "skeleton3d_body_metrics.h"
#include "../Analysis/route_substitution.h"
#include "../Analysis/gate_record.h"
#include "../Analysis/swing_analysis.h"
#include "../Metrics/series_reduce.h"

#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

namespace pinpoint::analysis {

namespace body3d {

inline double medianOf(std::vector<double> v)
{
    if (v.empty()) return kNaN;
    std::sort(v.begin(), v.end());
    const size_t n = v.size();
    return (n & 1u) ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

inline std::optional<int64_t> ladderTime(const std::vector<PhaseEvent> &phases, Phase p)
{
    for (const PhaseEvent &e : phases)
        if (e.phase == p) return e.t_us;
    return std::nullopt;
}

// The frames a phase reading at `us` is the median of: valid, finite, within the grader's window.
// Returns the frames' σ median (NaN when no σ was assessed) in `sigma`.
inline Reduced readAt(const MetricSeries &m, const std::vector<double> &frameSigma, int64_t us, double &sigma)
{
    SeriesView view;
    view.t = m.t_us.data(); view.v = m.value.data(); view.n = m.t_us.size();
    view.valid = m.valid.size() == m.t_us.size() ? m.valid.data() : nullptr;
    const ReduceConfig rc{};
    const Reduced r = reduceAt(view, us, rc);
    std::vector<double> sg;
    if (r.ok)
        for (size_t i = 0; i < m.t_us.size(); ++i)
            if (std::llabs(m.t_us[i] - us) <= rc.atHalfWindowUs && view.isValid(i) && i < frameSigma.size()
                && std::isfinite(frameSigma[i]))
                sg.push_back(frameSigma[i]);
    sigma = medianOf(sg);
    return r;
}

// A curve over the fit's grid from per-frame readings (NaN = none). False when fewer than two
// frames read. `frameSigma` (parallel to the curve) is filled for the phase readings.
inline bool buildCurve(MetricSeries &m, std::vector<double> &frameSigma, const std::vector<int64_t> &t,
                       const std::vector<double> &val, const std::vector<double> &sig,
                       const std::vector<PhaseEvent> &phases, const std::vector<Phase> &readAtPhases,
                       bool withSigma)
{
    size_t first = val.size(), last = 0, nRead = 0;
    for (size_t i = 0; i < val.size() && i < t.size(); ++i)
        if (std::isfinite(val[i])) { first = std::min(first, i); last = i; ++nRead; }
    if (nRead < 2) return false;
    bool anyBridged = false;
    size_t prev = first;
    for (size_t i = first; i <= last; ++i) {
        m.t_us.push_back(t[i]);
        if (std::isfinite(val[i])) {
            m.value.push_back(val[i]);
            m.valid.push_back(1);
            frameSigma.push_back(i < sig.size() ? sig[i] : kNaN);
            prev = i;
            continue;
        }
        size_t next = i + 1;
        while (next <= last && !std::isfinite(val[next])) ++next;
        const double f = double(t[i] - t[prev]) / double(std::max<int64_t>(1, t[next] - t[prev]));
        m.value.push_back(val[prev] + (val[next] - val[prev]) * f);
        m.valid.push_back(0);
        frameSigma.push_back(kNaN);
        anyBridged = true;
    }
    if (!anyBridged) m.valid.clear();
    if (withSigma) {
        std::vector<double> sg;
        for (double s : frameSigma) if (std::isfinite(s)) sg.push_back(s);
        if (!sg.empty()) { m.sigma = medianOf(sg); m.sigmaKind = uint8_t(SigmaKind::Propagated); }
    }
    for (Phase p : readAtPhases) {
        const std::optional<int64_t> us = ladderTime(phases, p);
        if (!us) continue;
        double s = kNaN;
        const Reduced r = readAt(m, frameSigma, *us, s);
        if (!r.ok) continue;
        PhaseSample ps{ p, *us, r.value, QString() };
        if (withSigma && std::isfinite(s)) { ps.sigma = s; ps.sigmaKind = uint8_t(SigmaKind::Propagated); }
        m.phaseSamples.push_back(ps);
    }
    return true;
}

inline void stamp(MetricSeries &m, const char *key, const char *label)
{
    m.key   = QString::fromLatin1(key);
    m.label = QString::fromUtf8(label);
    m.unit  = QStringLiteral("°");
    m.routeId     = QStringLiteral("faceOn+dtl");
    m.routeDirect = false;
}

} // namespace body3d

struct Skeleton3dBodyInput {
    const skeleton3d::FitResult *fit = nullptr;   // a valid two-camera fit (the stage checks dtlUsed)
    std::vector<PhaseEvent> phases;                // the ladder
    bool leftHanded = false;
    // A measured calibration of the cameras reached the FIT. Never true today: skeleton3d solves its
    // own cameras (skeleton3d_json writes "calibrated": false) and no calibration reaches it, so the
    // stage passes false — the same statement skeletonLines() makes for the two-camera turn.
    bool calibrated = false;
    bool withSigma  = true;                        // uncertainty.enabled
    body3d::ThighTurnConfig thigh;
};

struct Skeleton3dBodyOutput {
    std::optional<MetricSeries> shoulderPlane, shoulderAim, leadKnee, sideBend, thighLead, thighTrail;
    std::vector<GateRecord> gates;
    bool   leadKneeAtImpact = false;   // the 3-D lead knee has a valid frame in Impact → ShaftParallelThrough
    double yawSigmaDeg = 0.0;
};

inline Skeleton3dBodyOutput buildSkeleton3dBody(const Skeleton3dBodyInput &in)
{
    namespace b = body3d;
    Skeleton3dBodyOutput out;
    out.yawSigmaDeg = in.calibrated ? tuned::dtlCamera::kDtlYawSigmaCalDeg : tuned::dtlCamera::kDtlYawSigmaUncalDeg;
    if (!in.fit) return out;
    const skeleton3d::FitResult &fr = *in.fit;
    const bool leadIsLeft = !in.leftHanded;
    const size_t n = fr.t_us.size();

    std::vector<double> plane(n), planeS(n), aim(n), aimS(n), knee(n), kneeS(n), bend(n), bendS(n);
    for (size_t i = 0; i < n; ++i) {
        const b::Reading p = b::shoulderPlaneDeg(fr, i, leadIsLeft);
        const b::Reading a = b::shoulderAimDeg(fr, i, leadIsLeft);
        const b::Reading k = b::kneeFlexDeg(fr, i, leadIsLeft);
        const b::Reading s = b::sideBendDeg(fr, i, leadIsLeft);
        plane[i] = p.deg; planeS[i] = p.sigma;
        aim[i]   = a.deg; aimS[i]   = a.sigma;
        knee[i]  = k.deg; kneeS[i]  = k.sigma;
        bend[i]  = s.deg; bendS[i]  = s.sigma;
    }

    const auto curve = [&](std::optional<MetricSeries> &slot, const char *key, const char *label,
                           const std::vector<double> &v, const std::vector<double> &s,
                           const std::vector<Phase> &at, std::vector<double> *frameSigmaOut = nullptr) {
        MetricSeries m;
        std::vector<double> fs;
        if (!b::buildCurve(m, fs, fr.t_us, v, s, in.phases, at, in.withSigma)) return;
        b::stamp(m, key, label);
        slot = std::move(m);
        if (frameSigmaOut) *frameSigmaOut = std::move(fs);
    };
    curve(out.shoulderPlane, "shoulderPlaneAngle3d", "Shoulder plane angle (3-D)", plane, planeS, { Phase::Top });
    curve(out.leadKnee, "leadKneeFlexion", "Lead knee flexion", knee, kneeS,
          { Phase::Address, Phase::Top, Phase::Impact, Phase::ShaftParallelThrough });
    curve(out.sideBend, "spineSideBend", "Spine side bend", bend, bendS,
          { Phase::Address, Phase::Top, Phase::Impact });

    // shoulderLineYaw: ONE reading at address — the median of the frames within the grader's window
    // of the ladder's Address, its σ the median line σ ⊕ σ_yaw.
    if (const std::optional<int64_t> addr = b::ladderTime(in.phases, Phase::Address)) {
        std::vector<double> v, s;
        for (size_t i = 0; i < n; ++i)
            if (std::isfinite(aim[i]) && std::llabs(fr.t_us[i] - *addr) <= tuned::sampler::kWindowHalfUs) {
                v.push_back(aim[i]);
                if (std::isfinite(aimS[i])) s.push_back(aimS[i]);
            }
        if (!v.empty()) {
            MetricSeries m;
            b::stamp(m, "shoulderLineYaw", "Shoulder line aim");
            PhaseSample ps{ Phase::Address, *addr, b::medianOf(v), QString() };
            if (in.withSigma && !s.empty()) {
                const double sg = std::hypot(b::medianOf(s), out.yawSigmaDeg);
                ps.sigma = sg; ps.sigmaKind = uint8_t(SigmaKind::Propagated);
                m.sigma = sg;  m.sigmaKind  = uint8_t(SigmaKind::Propagated);
            }
            m.phaseSamples.push_back(ps);
            out.shoulderAim = std::move(m);
        }
    }

    // The thigh turns (research only).
    if (const std::optional<int64_t> addr = b::ladderTime(in.phases, Phase::Address)) {
        for (bool lead : { true, false }) {
            const bool leftLeg = lead ? leadIsLeft : !leadIsLeft;
            const b::ThighTurn tt = b::thighTurn(fr, leftLeg, leadIsLeft, *addr, in.thigh);
            if (!tt.ok) continue;
            curve(lead ? out.thighLead : out.thighTrail, lead ? "thighTurn3dLead" : "thighTurn3dTrail",
                  lead ? "Lead thigh turn (3-D)" : "Trail thigh turn (3-D)", tt.turnDeg, tt.sigmaDeg,
                  { Phase::Address, Phase::Top, Phase::Impact });
        }
    }

    // Does the 3-D lead knee read where m_leadKneeFlex grades it (the peak over Impact → P8)?
    if (out.leadKnee) {
        const std::optional<int64_t> p7 = b::ladderTime(in.phases, Phase::Impact);
        const std::optional<int64_t> p8 = b::ladderTime(in.phases, Phase::ShaftParallelThrough);
        if (p7) {
            const int64_t from = p8 ? *p7 : *p7 - tuned::sampler::kWindowHalfUs;
            const int64_t to   = p8 ? *p8 : *p7 + tuned::sampler::kWindowHalfUs;
            const MetricSeries &m = *out.leadKnee;
            for (size_t i = 0; i < m.t_us.size(); ++i)
                if (m.t_us[i] >= from && m.t_us[i] <= to && (m.valid.empty() || m.valid[i]))
                    out.leadKneeAtImpact = true;
        }
    }

    if (out.shoulderAim && !in.calibrated)
        out.gates.push_back(GateRecord{ PP_GATE("body3d.cameraAssumed"), QStringLiteral("shoulderLineYaw"),
                                        GateEffect::Downgraded,
                                        QVariantMap{ { QStringLiteral("sigmaYawDeg"), out.yawSigmaDeg } }, {}, {},
                                        QStringList{ QStringLiteral("shoulderLineYaw") } });
    return out;
}

// What mergeSkeleton3dBody did, for the stage's log and the tests.
struct Skeleton3dBodyMerge {
    QStringList published;          // keys appended
    bool leadKneeReplaced = false;  // the down-the-line leadKneeFlexion was removed for the 3-D one
    bool leadKneeKeptDtl  = false;  // the down-the-line one stayed, the 3-D one dropped
    std::vector<GateRecord> gates;  // body3d.fromFit, one per reading the 3-D route supplied in another's place
};

// The substitute record for `key`, which the 3-D route supplied because route `replaces` had none.
inline GateRecord body3dFromFitRecord(const QString &key, const char *replaces)
{
    return GateRecord{ PP_GATE("body3d.fromFit"), key, GateEffect::Downgraded,
                       QVariantMap{ { QStringLiteral("code"), QString::fromLatin1(replaces) },
                                    { QStringLiteral("route"), QStringLiteral("faceOn+dtl") },
                                    { QStringLiteral("replaces"), QString::fromLatin1(replaces) } },
                       {}, {}, QStringList{ key } };
}

// Apply the precedence rules (top of file) to `series` — detail.series as the stage sees it.
inline Skeleton3dBodyMerge mergeSkeleton3dBody(std::vector<MetricSeries> &series, Skeleton3dBodyOutput &out)
{
    Skeleton3dBodyMerge res;
    const auto has = [&](const char *key) {
        return std::any_of(series.begin(), series.end(), [&](const MetricSeries &m) { return m.key == QLatin1String(key); });
    };
    const auto publish = [&](std::optional<MetricSeries> &m, const char *key) {
        if (!m || has(key)) return;
        res.published << QString::fromLatin1(key);
        series.push_back(std::move(*m));
        m.reset();
    };
    publish(out.shoulderPlane, "shoulderPlaneAngle3d");
    publish(out.shoulderAim, "shoulderLineYaw");
    if (out.leadKnee) {
        const bool dtl = has("leadKneeFlexion");
        if (dtl && out.leadKneeAtImpact) {
            series.erase(std::remove_if(series.begin(), series.end(),
                                        [](const MetricSeries &m) { return m.key == QLatin1String("leadKneeFlexion"); }),
                         series.end());
            res.leadKneeReplaced = true;
        }
        if (dtl && !out.leadKneeAtImpact) { res.leadKneeKeptDtl = true; out.leadKnee.reset(); }
        const bool fills = !dtl && out.leadKnee.has_value();
        publish(out.leadKnee, "leadKneeFlexion");
        if (fills && res.published.contains(QStringLiteral("leadKneeFlexion")))
            res.gates.push_back(body3dFromFitRecord(QStringLiteral("leadKneeFlexion"), "dtl"));
    }
    if (out.sideBend && !has("spineSideBend")) {
        publish(out.sideBend, "spineSideBend");
        res.gates.push_back(body3dFromFitRecord(QStringLiteral("spineSideBend"), "faceOn"));
    }
    publish(out.thighLead, "thighTurn3dLead");
    publish(out.thighTrail, "thighTurn3dTrail");
    return res;
}

} // namespace pinpoint::analysis
