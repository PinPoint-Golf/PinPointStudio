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
//   trailKneeFlexion      curve, read at address, the top and impact (W5, 13 Nov 2026) — the same
//                         kneeFlexDeg on the TRAIL leg (right for a right-hander, left for a
//                         left-hander), the down-the-line series' definition and sign (dtl_posture.h:
//                         180° − the angle at the knee, + = more bend) and its stamped positions
//   spineSideBend         curve, read at address, the top and impact
//   thighTurn3dLead / thighTurn3dTrail   research-only curves (isResearchOnlySeries), read at
//                         address, the top and impact — W3's hip internal rotation reads them
//   armLiftTop            (D2, 13 Nov 2026) ONE reading at the top (a PointInTime, as shoulderLineYaw is
//                         at address): the median over the fit frames within the grader's ±window of the
//                         ladder's Top of body3d::armLiftDeg — the lead UPPER ARM (shoulder → ELBOW, never
//                         the wrist: a bent lead arm makes the arm look lifted and is its own fault) against
//                         the shoulder-turn plane — σ the frames' median σ. NOT BUILT on a swing where
//                         body3d.shoulderViewsDisagree fired (Skeleton3dBodyInput::shoulderViewsDisagree):
//                         the plane it is measured against is the fit's shoulder line at the top, which the
//                         two cameras then disagree about.
//   leadArmFlexion3d      (D2) curve over the frames where the fit MEASURED the lead elbow (TierMeasured,
//                         FB's honest tier: both cameras saw it), read at address and the top (D6: bent_lead_arm
//                         grades the CHANGE, m_leadElbowBendTop3d = delta P1 → P4, so both ends must read on
//                         measured frames; an address with no measured elbow within the grader's window leaves
//                         the change unread) — body3d::leadElbowBendDeg,
//                         180° − the angle at the lead elbow, the wrist sensors' sign. Its OWN key, not a rung
//                         of leadArmFlexion: that key's face-on series is chicken_wing's reading (D5) and
//                         would be replaced by this sparse one, and its m_leadElbowFlex_* measures are held
//                         per measure, not per route (plane_stack_design.md §11 D2). Shown; bent_lead_arm grades
//                         the change below, not this curve.
//   leadElbowBendChange3d (D6 round 2, Mark 13 Nov 2026) ONE reading at the top (a PointInTime): the measured bend
//                         at the Top as the grader reads leadArmFlexion3d there (grader_sample.h) LESS the median
//                         of the measured bend over this swing's ADDRESS HOLD — the fit frames from P1 −
//                         body3d.elbowHoldUs (150 ms) to P1, where the arm is still (P1 is the end of the hold),
//                         at least body3d.elbowHoldMinFrames of them. + = more bend at the top than at address.
//                         σ: the top reading's ⊕ the hold frames' median σ (not ÷ √n: one still pose, as
//                         shoulderPlaneFromHold argues). bent_lead_arm grades it through m_leadElbowBendTop3d. Where
//                         either end has no measured elbow, NOT BUILT, and the Refused body3d.leadElbowUnmeasured
//                         (subject and covers leadElbowBendChange3d; code top / address / both; evidence frames,
//                         measured at each end) says why.
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
//   · shoulderLineYaw: no other producer; published unless already present.
//   · shoulderPlaneAngle3d: published unless another 3-D series is already present — BESIDE the chest
//     sensor's (routeId thoraxImu, BodyRotationStage, W4 13 Nov 2026), which applyTrunkSensorPrecedence
//     (trunk_sensor_series.h, after every stage) makes the answer, this one the fallback.
//   · leadKneeFlexion: the 3-D series REPLACES the down-the-line one when it has a reading in the
//     Impact → ShaftParallelThrough window (what m_leadKneeFlex grades). Otherwise the down-the-line
//     series stays exactly as its producer made it (dtl_posture.h), and the 3-D one is not published
//     beside it. With no down-the-line series at all, the 3-D one is published whatever it covers.
//   · trailKneeFlexion (W5, 13 Nov 2026; Mark's rule, 13 Nov): WITH THE CAMERAS UNCALIBRATED — every
//     swing today: Skeleton3dBodyInput::calibrated, the flag body3d.cameraAssumed reads — the
//     down-the-line series is the answer whenever it exists, and the 3-D one only FILLS when there is
//     none. On 2026-07-04 s8 the self-solved fit pulled the trail knee straight at impact (21.8°
//     against the camera's 30.5°): the hands and the shaft cover the trail knee in the face-on view
//     there, and the fit followed that view. WITH THE CAMERAS CALIBRATED: every trail-knee measure is
//     a CHANGE FROM ADDRESS (m_trailKneeFlex = P1→P4, m_trailKneeFlex_p2 … _p8) or the address
//     reading itself (m_kneeFlexAddress), so both ends of a change must come from ONE route — a 3-D
//     address against a down-the-line top would grade the two routes' disagreement as a knee moving.
//     The 3-D series REPLACES the down-the-line one only when it has a reading (a valid frame within
//     the grader's ±kWindowHalfUs) at Address AND at the Top, the window m_trailKneeFlex grades.
//     Otherwise the down-the-line series stays exactly as its producer made it, and the 3-D one is
//     not published beside it. With no down-the-line series at all, the 3-D one is published
//     whatever it covers. On a replace, a position between that the down-the-line series read and
//     the 3-D one does not is reported in Skeleton3dBodyMerge::trailKneeLost (the stage logs it):
//     that change from address is then not assessed on this swing.
//     The lead knee keeps its rule calibrated or not: the down-the-line camera cannot see the lead leg.
//   · spineSideBend: published only when the face-on series is absent (the face-on route is the
//     ladder's better rung and stays the answer wherever it read) — and BESIDE a trunk sensor's
//     (routeId trunkImus, BodyRotationStage, W5 13 Nov 2026), as the shoulder plane is: the precedence
//     makes the sensor's the answer and this one the fallback when it withdraws the sensor's (worn
//     face-in, or fewer positions).
//   · the thigh turns: research only, published unless already present.
//   · armLiftTop (D2, 13 Nov 2026; Mark's rule, 13 Nov): WITH THE CAMERAS UNCALIBRATED — every swing
//     today — the down-the-line IMAGE reading (DtlPostureStage, routeId dtl) is the answer whenever it
//     exists, and the 3-D one only FILLS when there is none (with body3d.fromFit, replaces dtl). The 13 Nov
//     frame sheet showed the 3-D gap between two golfers was mostly the fit's steep shoulder line. WITH
//     THE CAMERAS CALIBRATED the 3-D reading REPLACES the image one. Never built where the shoulder views
//     disagree; if that leaves no reading at all, body3d.armLiftShoulders (Refused) says why. Wherever the
//     3-D reading stands it is judged against the corridor seated on the IMAGE reading — the pack keys a
//     norm by context, never by route — so it carries body3d.armLiftImageCorridor (Downgraded).
//   · leadArmFlexion3d (D2): no other producer; published unless already present. The wrist sensors'
//     leadArmFlexion beats it (applyLeadElbowSensorPrecedence, after every stage: wristImus > 3-D > face-on).
//   · leadElbowBendChange3d (D6) and its body3d.leadElbowUnmeasured record go with it: published unless already
//     present, and erased by the same precedence.
//
// THE SUBSTITUTE RECORD, body3d.fromFit (Downgraded, subject and covers the reading, evidence code /
// route / replaces — route_substitution.h's marker): fired when the 3-D route supplied a reading
// because the single-camera route had none — the side bend wherever it is published (the face-on
// series was absent; a trunk sensor's beside it erases the record with the 3-D series when the sensor
// wins, route_substitution.h supersedeReadings), either knee when there was no down-the-line series.
// NOT when a 3-D knee replaces a down-the-line one: that is the ladder's better rung winning, not a
// fallback. After
// the run, reconcileSubstitutedReadings (runProfile) takes the failed route's refusals off the
// reading and lists them in this record's `replacedRecords`.

#include "skeleton3d_body_metrics.h"
#include "../Analysis/route_substitution.h"
#include "../Analysis/gate_record.h"
#include "../Analysis/swing_analysis.h"
#include "../Metrics/series_reduce.h"
#include "grader_sample.h"   // grader::graderReadingAt — the top end of leadElbowBendChange3d, as graded (D6)

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

// Whether the grader has a reading of `m` at ladder position `ph`, instant `us` (measure_sample.cpp):
// the curve's windowed median, else the phase sample labelled `ph`. The rule that decides which
// trail-knee route stands, and what Skeleton3dBodyMerge::trailKneeLost counts.
inline bool readsAt(const MetricSeries &m, Phase ph, int64_t us)
{
    if (!m.t_us.empty() && m.t_us.size() == m.value.size()) {
        double unused = kNaN;
        if (readAt(m, {}, us, unused).ok) return true;
    }
    return std::any_of(m.phaseSamples.begin(), m.phaseSamples.end(),
                       [&](const PhaseSample &p) { return p.phase == ph && std::isfinite(p.value); });
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
    // body3d.shoulderViewsDisagree fired on this swing (Skeleton3DStage, FB): the 3-D armLiftTop is not built.
    bool shoulderViewsDisagree = false;
    // bent_lead_arm's address hold (D6): P1 − elbowHoldUs … P1, at least elbowHoldMinFrames measured frames.
    int64_t elbowHoldUs        = tuned::body3d::kElbowHoldUs;          // body3d.elbowHoldUs
    int     elbowHoldMinFrames = tuned::body3d::kElbowHoldMinFrames;   // body3d.elbowHoldMinFrames
};

struct Skeleton3dBodyOutput {
    std::optional<MetricSeries> shoulderPlane, shoulderAim, leadKnee, trailKnee, sideBend, thighLead, thighTrail;
    std::optional<MetricSeries> armLift, leadElbow3d;   // armLiftTop, leadArmFlexion3d (D2)
    std::optional<MetricSeries> leadElbowChange;        // leadElbowBendChange3d (D6)
    std::optional<GateRecord>   leadElbowUnmeasured;    // body3d.leadElbowUnmeasured, where the change is not built
    bool   armLiftWithheld = false;    // the fit read an arm lift at the top, not built: the shoulder views disagree
    std::vector<GateRecord> gates;
    bool   leadKneeAtImpact = false;   // the 3-D lead knee has a valid frame in Impact → ShaftParallelThrough
    bool   trailKneeAtAddressAndTop = false;   // the 3-D trail knee reads (grader's window) at Address AND Top
    bool   calibrated = false;                 // Skeleton3dBodyInput::calibrated, for the trail knee's rule
    double yawSigmaDeg = 0.0;
};

inline Skeleton3dBodyOutput buildSkeleton3dBody(const Skeleton3dBodyInput &in)
{
    namespace b = body3d;
    Skeleton3dBodyOutput out;
    out.yawSigmaDeg = in.calibrated ? tuned::dtlCamera::kDtlYawSigmaCalDeg : tuned::dtlCamera::kDtlYawSigmaUncalDeg;
    out.calibrated  = in.calibrated;
    if (!in.fit) return out;
    const skeleton3d::FitResult &fr = *in.fit;
    const bool leadIsLeft = !in.leftHanded;
    const size_t n = fr.t_us.size();

    std::vector<double> plane(n), planeS(n), aim(n), aimS(n), knee(n), kneeS(n), tknee(n), tkneeS(n), bend(n), bendS(n);
    std::vector<double> lift(n), liftS(n), elbow(n), elbowS(n);
    for (size_t i = 0; i < n; ++i) {
        const b::Reading al = b::armLiftDeg(fr, i, leadIsLeft);
        const b::Reading eb = b::leadElbowBendDeg(fr, i, leadIsLeft);
        lift[i]  = al.deg; liftS[i]  = al.sigma;
        elbow[i] = eb.deg; elbowS[i] = eb.sigma;
        const b::Reading p = b::shoulderPlaneDeg(fr, i, leadIsLeft);
        const b::Reading a = b::shoulderAimDeg(fr, i, leadIsLeft);
        const b::Reading k = b::kneeFlexDeg(fr, i, leadIsLeft);
        const b::Reading tk = b::kneeFlexDeg(fr, i, !leadIsLeft);   // the TRAIL leg
        const b::Reading s = b::sideBendDeg(fr, i, leadIsLeft);
        plane[i] = p.deg; planeS[i] = p.sigma;
        aim[i]   = a.deg; aimS[i]   = a.sigma;
        knee[i]  = k.deg; kneeS[i]  = k.sigma;
        tknee[i] = tk.deg; tkneeS[i] = tk.sigma;
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
    // The trail knee: stamped where the down-the-line series stamps it (dtl_posture.h); the grader reads
    // the curve at every other position (m_trailKneeFlex_p2 … _p8).
    curve(out.trailKnee, "trailKneeFlexion", "Trail knee flexion", tknee, tkneeS,
          { Phase::Address, Phase::Top, Phase::Impact });
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

    // armLiftTop (D2): ONE reading at the top, the median of the frames within the grader's window of the
    // ladder's Top, its σ their median σ — unless the two cameras disagree about the shoulders there.
    if (const std::optional<int64_t> top = b::ladderTime(in.phases, Phase::Top)) {
        std::vector<double> v, s;
        for (size_t i = 0; i < n; ++i)
            if (std::isfinite(lift[i]) && std::llabs(fr.t_us[i] - *top) <= tuned::sampler::kWindowHalfUs) {
                v.push_back(lift[i]);
                if (std::isfinite(liftS[i])) s.push_back(liftS[i]);
            }
        if (!v.empty() && in.shoulderViewsDisagree) {
            out.armLiftWithheld = true;
        } else if (!v.empty()) {
            MetricSeries m;
            b::stamp(m, "armLiftTop", "Arm lift at the top (3-D)");
            PhaseSample ps{ Phase::Top, *top, b::medianOf(v), QString() };
            if (in.withSigma && !s.empty()) {
                const double sg = b::medianOf(s);
                ps.sigma = sg; ps.sigmaKind = uint8_t(SigmaKind::Propagated);
                m.sigma = sg;  m.sigmaKind  = uint8_t(SigmaKind::Propagated);
            }
            m.phaseSamples.push_back(ps);
            out.armLift = std::move(m);
        }
    }
    // leadArmFlexion3d (D2): the fit's lead elbow, only where it was measured, read at the top.
    curve(out.leadElbow3d, "leadArmFlexion3d", "Lead arm (elbow), 3-D", elbow, elbowS, { Phase::Top });

    // leadElbowBendChange3d (D6 round 2): the measured top bend, as graded, less the median over the address hold.
    {
        const std::optional<int64_t> top = b::ladderTime(in.phases, Phase::Top);
        const std::optional<int64_t> p1  = b::ladderTime(in.phases, Phase::Address);
        if (top && p1) {
            grader::GradedReading t;
            if (out.leadElbow3d) t = grader::graderReadingAt(*out.leadElbow3d, Phase::Top, in.phases);
            int topFrames = 0, holdFrames = 0;
            std::vector<double> hv, hs;
            for (size_t i = 0; i < n; ++i) {
                if (std::llabs(fr.t_us[i] - *top) <= tuned::sampler::kWindowHalfUs) ++topFrames;
                if (fr.t_us[i] < *p1 - in.elbowHoldUs || fr.t_us[i] > *p1) continue;
                ++holdFrames;
                if (!std::isfinite(elbow[i])) continue;
                hv.push_back(elbow[i]);
                if (std::isfinite(elbowS[i])) hs.push_back(elbowS[i]);
            }
            const bool topOk = t.ok, holdOk = int(hv.size()) >= std::max(1, in.elbowHoldMinFrames);
            if (topOk && holdOk) {
                const double hold = b::medianOf(hv);
                MetricSeries m;
                b::stamp(m, "leadElbowBendChange3d", "Lead elbow bend added by the top (3-D)");
                PhaseSample ps{ Phase::Top, *top, t.value - hold, QString() };
                if (in.withSigma && t.sigmaFrom != grader::SigmaFrom::None && !hs.empty()) {
                    const double sg = std::hypot(t.sigma, b::medianOf(hs));
                    if (t.sigmaFrom == grader::SigmaFrom::Noise) {
                        m.sigma = sg;   // a noise floor stays one: a series σ with no kind
                    } else {
                        ps.sigma = sg; ps.sigmaKind = uint8_t(SigmaKind::Propagated);
                        m.sigma = sg;  m.sigmaKind  = uint8_t(SigmaKind::Propagated);
                    }
                }
                m.phaseSamples.push_back(ps);
                out.leadElbowChange = std::move(m);
            } else {
                const QString code = !topOk && !holdOk ? QStringLiteral("both") : !topOk ? QStringLiteral("top")
                                                                                         : QStringLiteral("address");
                out.leadElbowUnmeasured = GateRecord{
                    PP_GATE("body3d.leadElbowUnmeasured"), QStringLiteral("leadElbowBendChange3d"), GateEffect::Refused,
                    QVariantMap{ { QStringLiteral("code"), code },
                                 { QStringLiteral("topFrames"), topFrames },
                                 { QStringLiteral("holdFrames"), holdFrames },
                                 { QStringLiteral("holdMeasured"), int(hv.size()) },
                                 { QStringLiteral("holdMs"), double(in.elbowHoldUs) / 1000.0 } },
                    {}, {}, QStringList{ QStringLiteral("leadElbowBendChange3d") } };
            }
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

    // Does the 3-D trail knee read at BOTH ends of m_trailKneeFlex (Address and the Top)?
    if (out.trailKnee) {
        const std::optional<int64_t> p1 = b::ladderTime(in.phases, Phase::Address);
        const std::optional<int64_t> p4 = b::ladderTime(in.phases, Phase::Top);
        out.trailKneeAtAddressAndTop = p1 && p4 && b::readsAt(*out.trailKnee, Phase::Address, *p1)
                                     && b::readsAt(*out.trailKnee, Phase::Top, *p4);
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
    bool trailKneeReplaced = false; // the down-the-line trailKneeFlexion was removed for the 3-D one
    bool trailKneeKeptDtl  = false; // the down-the-line one stayed (uncalibrated, or no 3-D reading at Address / the Top)
    bool trailKneeKeptUncalibrated = false;   // …because the cameras are uncalibrated (Mark's rule, 13 Nov 2026)
    // When the 3-D trail knee replaced the down-the-line one: the P2 … P8 positions the down-the-line
    // series had a reading at and the 3-D one has none — each change from address there is not assessed
    // on this swing — and the reverse (positions the 3-D one reads that the down-the-line one did not).
    std::vector<Phase> trailKneeLost, trailKneeGained;
    bool armLiftReplaced = false;   // calibrated: the 3-D armLiftTop replaced the image one
    bool armLiftKeptDtl  = false;   // uncalibrated: the image armLiftTop stayed, the 3-D one dropped
    bool armLiftWithheld = false;   // the shoulder views disagree: no 3-D armLiftTop
    std::vector<GateRecord> gates;  // body3d.fromFit, one per reading the 3-D route supplied in another's place;
                                    // body3d.armLiftImageCorridor / body3d.armLiftShoulders (D2)
};

// The positions the trail-knee measures grade at (core.json m_kneeFlexAddress, m_trailKneeFlex*): P1 … P8.
inline const std::vector<Phase> &trailKneeGradedPhases()
{
    static const std::vector<Phase> p{ Phase::Address, Phase::ShaftParallelBack, Phase::MidBackswing, Phase::Top,
                                       Phase::ArmParallelDown, Phase::Delivery, Phase::Impact,
                                       Phase::ShaftParallelThrough };
    return p;
}

// The substitute record for `key`, which the 3-D route supplied because route `replaces` had none.
inline GateRecord body3dFromFitRecord(const QString &key, const char *replaces)
{
    return GateRecord{ PP_GATE("body3d.fromFit"), key, GateEffect::Downgraded,
                       QVariantMap{ { QStringLiteral("code"), QString::fromLatin1(replaces) },
                                    { QStringLiteral("route"), QStringLiteral("faceOn+dtl") },
                                    { QStringLiteral("replaces"), QString::fromLatin1(replaces) } },
                       {}, {}, QStringList{ key } };
}

// THE ASSUMPTION a 3-D armLiftTop is judged under (D2): its corridor was seated on the down-the-line
// IMAGE reading (norms.json m_armLiftTop), because a norm row is keyed by context and cohort, never by
// route. On the library the two read within a degree of each other at the median (image − 3-D −1.3°,
// IQR −5.7 … +4.3), but the 3-D reading spreads the golfers wider (Jess +21 against +15.5 in the image).
inline GateRecord armLiftImageCorridorRecord()
{
    return GateRecord{ PP_GATE("body3d.armLiftImageCorridor"), QStringLiteral("armLiftTop"), GateEffect::Downgraded,
                       QVariantMap{ { QStringLiteral("route"), QStringLiteral("faceOn+dtl") },
                                    { QStringLiteral("normRoute"), QStringLiteral("dtl") } },
                       {}, {}, QStringList{ QStringLiteral("armLiftTop") } };
}

// Apply the precedence rules (top of file) to `series` — detail.series as the stage sees it.
inline Skeleton3dBodyMerge mergeSkeleton3dBody(std::vector<MetricSeries> &series, Skeleton3dBodyOutput &out,
                                               const std::vector<PhaseEvent> &ladder = {})
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
    // THE SHOULDER PLANE IS PUBLISHED BESIDE THE CHEST SENSOR'S (W4, 13 Nov 2026). BodyRotationStage runs
    // first and may already have written the chest sensor's shoulderPlaneAngle3d (routeId thoraxImu);
    // stepping aside for it would leave nothing to fall back on when applyTrunkSensorPrecedence (after
    // every stage) withdraws that reading — a chest worn face-in, or no reading at a position this one
    // reads. So only ANOTHER route's series stops this one; the precedence decides between the two.
    {
        const bool other = std::any_of(series.begin(), series.end(), [](const MetricSeries &m) {
            return m.key == QLatin1String("shoulderPlaneAngle3d") && m.routeId != QLatin1String("thoraxImu");
        });
        if (out.shoulderPlane && !other) {
            res.published << QStringLiteral("shoulderPlaneAngle3d");
            series.push_back(std::move(*out.shoulderPlane));
            out.shoulderPlane.reset();
        }
    }
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
    // THE TRAIL KNEE (W5, 13 Nov 2026): uncalibrated, the down-the-line series wherever it exists;
    // calibrated, both ends of every change from address on one route (top of file).
    if (out.trailKnee) {
        const auto dtlIt = std::find_if(series.begin(), series.end(), [](const MetricSeries &m) {
            return m.key == QLatin1String("trailKneeFlexion");
        });
        const bool dtl = dtlIt != series.end();
        const bool replaces = out.calibrated && out.trailKneeAtAddressAndTop;
        if (dtl && replaces) {
            for (Phase ph : trailKneeGradedPhases()) {
                const std::optional<int64_t> us = body3d::ladderTime(ladder, ph);
                if (!us) continue;
                const bool d = body3d::readsAt(*dtlIt, ph, *us), f = body3d::readsAt(*out.trailKnee, ph, *us);
                if (d && !f) res.trailKneeLost.push_back(ph);
                if (f && !d) res.trailKneeGained.push_back(ph);
            }
            series.erase(std::remove_if(series.begin(), series.end(),
                                        [](const MetricSeries &m) { return m.key == QLatin1String("trailKneeFlexion"); }),
                         series.end());
            res.trailKneeReplaced = true;
        }
        if (dtl && !replaces) {
            res.trailKneeKeptDtl = true;
            res.trailKneeKeptUncalibrated = !out.calibrated;
            out.trailKnee.reset();
        }
        const bool fills = !dtl && out.trailKnee.has_value();
        publish(out.trailKnee, "trailKneeFlexion");
        if (fills && res.published.contains(QStringLiteral("trailKneeFlexion")))
            res.gates.push_back(body3dFromFitRecord(QStringLiteral("trailKneeFlexion"), "dtl"));
    }
    // THE SIDE BEND IS PUBLISHED BESIDE A TRUNK SENSOR'S (W5, 13 Nov 2026; W4's shoulder-plane fix).
    // BodyRotationStage runs first and may already have written the sensors' spineSideBend (routeId
    // trunkImus). Stepping aside for it left nothing to fall back on when applyTrunkSensorPrecedence
    // (after every stage) withdraws that reading — a sensor worn face-in, or no reading at a position
    // this one reads — and with no face-on reading there was then no side bend at all. So only ANOTHER
    // route's series stops this one: the face-on series (the better rung, the answer wherever it read)
    // or another 3-D one. Beside the sensor's, the precedence decides; if the sensor's wins, this series
    // and its body3d.fromFit record are erased with it (supersedeReadings: a weaker stand-in).
    {
        const bool other = std::any_of(series.begin(), series.end(), [](const MetricSeries &m) {
            return m.key == QLatin1String("spineSideBend") && m.routeId != QLatin1String("trunkImus");
        });
        if (out.sideBend && !other) {
            res.published << QStringLiteral("spineSideBend");
            series.push_back(std::move(*out.sideBend));
            out.sideBend.reset();
            res.gates.push_back(body3dFromFitRecord(QStringLiteral("spineSideBend"), "faceOn"));
        }
    }
    // THE ARM LIFT AT THE TOP (D2, 13 Nov 2026; top of file): uncalibrated, the image reading wherever it
    // exists; calibrated, the 3-D one; never the 3-D one where the shoulder views disagree.
    {
        const bool dtl = has("armLiftTop");
        res.armLiftWithheld = out.armLiftWithheld;
        if (out.armLift) {
            if (dtl && out.calibrated) {
                series.erase(std::remove_if(series.begin(), series.end(),
                                            [](const MetricSeries &m) { return m.key == QLatin1String("armLiftTop"); }),
                             series.end());
                res.armLiftReplaced = true;
            } else if (dtl) {
                res.armLiftKeptDtl = true;
                out.armLift.reset();
            }
            publish(out.armLift, "armLiftTop");
            if (res.published.contains(QStringLiteral("armLiftTop"))) {
                if (!dtl) res.gates.push_back(body3dFromFitRecord(QStringLiteral("armLiftTop"), "dtl"));
                res.gates.push_back(armLiftImageCorridorRecord());
            }
        } else if (out.armLiftWithheld && !dtl) {
            res.gates.push_back(GateRecord{ PP_GATE("body3d.armLiftShoulders"), QStringLiteral("armLiftTop"),
                                            GateEffect::Refused,
                                            QVariantMap{ { QStringLiteral("code"), QStringLiteral("shoulderViews") } },
                                            {}, {}, QStringList{ QStringLiteral("armLiftTop") } });
        }
    }
    publish(out.leadElbow3d, "leadArmFlexion3d");
    // bent_lead_arm's change from the address hold (D6), or the record saying which end had no measured elbow.
    if (!has("leadElbowBendChange3d")) {
        if (out.leadElbowChange) publish(out.leadElbowChange, "leadElbowBendChange3d");
        else if (out.leadElbowUnmeasured) res.gates.push_back(*out.leadElbowUnmeasured);
    }
    publish(out.thighLead, "thighTurn3dLead");
    publish(out.thighTrail, "thighTurn3dTrail");
    return res;
}

// ── body3d.shoulderViewsDisagree (FB, 13 Nov 2026) ─────────────────────────────────────────────────
// DOWNGRADED, never a refusal: the two cameras disagree about where the shoulders are at the top
// (body3d::shoulderViewsAtTop), so every 3-D reading built on the fit's shoulder joints there is kept and
// judged a step lower in trust. Fired by Skeleton3DStage straight after the fit, before any reading
// exists, so it names every reading that MAY be built on the fit's shoulders:
//   shoulderPlaneAngle3d (and so shoulderTiltVsBend, which inherits its halves' Downgraded records,
//   shoulder_tilt_vs_bend.h) and shoulderLineYaw (Skeleton3dBodyStage), and the two-camera thorax turn
//   — thoraxRotation, thoraxRotationSigned and the separations xFactor / xFactorStretch
//   (BodyRotationTriangulatedStage, the fit's shoulder line's bearing). NOT the 3-D spineSideBend: it is
//   graded at Impact, and this is evidence about the shoulders at the TOP (FB review).
// scopeShoulderViewsRecord (runProfile, after the trunk-sensor precedence and before the tilt is
// derived) then keeps only the covers whose standing reading IS the fit's.
inline const QStringList &shoulderViewsCovers()
{
    static const QStringList k{ QStringLiteral("shoulderPlaneAngle3d"), QStringLiteral("shoulderLineYaw"),
                                QStringLiteral("thoraxRotation"),       QStringLiteral("thoraxRotationSigned"),
                                QStringLiteral("xFactor"),              QStringLiteral("xFactorStretch") };
    return k;
}
inline std::optional<GateRecord> shoulderViewsRecord(const body3d::ShoulderViews &sv, double limitDeg)
{
    if (!sv.disagree) return std::nullopt;
    const auto r1 = [](double x) { return std::round(x * 10.0) / 10.0; };
    return GateRecord{ PP_GATE("body3d.shoulderViewsDisagree"), QStringLiteral("shoulderPlaneAngle3d"),
                       GateEffect::Downgraded,
                       QVariantMap{ { QStringLiteral("medianDeg"), r1(sv.medianDeg) },
                                    { QStringLiteral("limitDeg"), limitDeg },
                                    { QStringLiteral("frames"), sv.frames },
                                    { QStringLiteral("fromMs"), double(sv.fromUs) / 1000.0 },
                                    { QStringLiteral("toMs"), double(sv.toUs) / 1000.0 } },
                       {}, {}, shoulderViewsCovers() };
}
// Which readings the record still speaks for, once every stage and the trunk-sensor precedence have
// settled what stands: a key stays covered only when its standing reading came from the fit —
//   the 3-D body's keys: routeId faceOn+dtl (a chest sensor's shoulder plane is not the fit's);
//   the thorax turn: not the chest sensor's (routeId thoraxImu) — the two-camera tier is the only other;
//   the separations: only while the thorax turn they subtract is still covered (not routeId trunkImus).
// A key with no reading at all leaves the covers. The record's subject moves to its first remaining
// cover; one left covering nothing is dropped. Returns how many (record, key) pairs it took off.
template <class Series>
inline int scopeShoulderViewsRecord(const std::vector<Series> &series, std::vector<GateRecord> &gates)
{
    const auto standing = [&](const QString &key) -> const Series * {
        for (const Series &m : series)
            if (m.key == key) return &m;
        return nullptr;
    };
    const auto fromFit = [&](const QString &key) {
        const Series *m = standing(key);
        if (!m) return false;
        if (key == QLatin1String("thoraxRotation") || key == QLatin1String("thoraxRotationSigned"))
            return m->routeId != QLatin1String("thoraxImu");
        if (key == QLatin1String("xFactor") || key == QLatin1String("xFactorStretch")) {
            const Series *th = standing(QStringLiteral("thoraxRotation"));
            return m->routeId != QLatin1String("trunkImus") && th && th->routeId != QLatin1String("thoraxImu");
        }
        return m->routeId == QLatin1String("faceOn+dtl");
    };
    int cleaned = 0;
    std::vector<GateRecord> kept;
    kept.reserve(gates.size());
    for (GateRecord &g : gates) {
        if (g.gate != QLatin1String("body3d.shoulderViewsDisagree")) { kept.push_back(std::move(g)); continue; }
        QStringList keep;
        for (const QString &k : g.covers)
            if (fromFit(k)) keep << k; else ++cleaned;
        if (keep.isEmpty()) continue;
        g.covers = keep;
        if (!keep.contains(g.subject)) g.subject = keep.front();
        kept.push_back(std::move(g));
    }
    gates = std::move(kept);
    return cleaned;
}

} // namespace pinpoint::analysis
