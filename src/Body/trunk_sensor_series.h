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

// THE READINGS FROM THE TRUNK SENSORS beyond the turn (planned-metrics promotion W3, 12 Nov 2026):
// the pieces body_rotation.cpp and HipInternalRotationStage (analysis_pipeline.cpp) share, header-only
// so trunk_e2e_test reaches all of them — values, σ, records and precedence — without the pipeline.
//
//   · the §8.2 σ budget (trunk_imu_sequence_grf_design.md) per sample, tuned::bodyRotation::kImu*;
//   · the GRADED phase readings: a phase sample is series_reduce.h reduceAt over the published curve
//     at the ladder instant — the grader's own call (measure_sample.cpp: the median of the valid
//     frames within ±sampler::kWindowHalfUs) — and its σ the median σ of those same frames, so what is
//     stamped is what is graded;
//   · hip internal rotation, a lead / trail pair: the pelvis sensor's signed turn less that thigh's
//     own turn off the two-camera fit (skeleton3d_body_metrics.h thighTurn), the thigh taken as fixed
//     — with a wide σ and the Downgraded body.thighTurnAssumed record — where the fit did not read it;
//   · the forward bend's definition (ballPlaneBendDeg): the long axis in the vertical plane through
//     the ball, as the down-the-line camera reads it;
//   · the precedence rule: a trunk-sensor spineForwardBend / spineSideBend beats the down-the-line,
//     face-on and 3-D readings — unless the sensor is worn face-in or reads fewer positions than the
//     camera (applyTrunkSensorPrecedence, run once after every stage).
//
// HIP INTERNAL ROTATION, the definition. Both turns are LEAD-RELATIVE (+ toward the target, for either
// hand): the pelvis sensor's pelvisRotationSigned (body_rotation.cpp) and the fit's thigh turn
// (thighTurn3dLead / thighTurn3dTrail). Internal rotation of a hip is the pelvis turning over that
// femur toward the femur's own inside, or the femur turning inward under the pelvis — the same
// relative rotation. So, + = internal rotation of that hip, for either hand:
//     lead  hip IR = pelvis turn − lead thigh turn     (the pelvis opening over the lead leg)
//     trail hip IR = trail thigh turn − pelvis turn    (the pelvis closing over the trail leg)
// A backswing turn loads the trail hip (+), a downswing the lead hip (+).

#include "../Analysis/gate_record.h"
#include "../Analysis/route_substitution.h"
#include "../Analysis/swing_analysis.h"
#include "../Core/pp_tuned_constants.h"
#include "../Metrics/series_reduce.h"

#include <QQuaternion>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVector3D>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <optional>
#include <vector>

namespace pinpoint::analysis {

namespace trunk_sensor {

inline constexpr double kNaN   = std::numeric_limits<double>::quiet_NaN();
inline constexpr double kR2D   = 57.29577951308232;
// The trunk sensors' route on the bends (the catalogue's rung id): Direct, measured on the segments.
inline const QString &bendRouteId() { static const QString r = QStringLiteral("trunkImus"); return r; }
// The pelvis sensor's route on pelvisRotationSigned (body_rotation.cpp sets it on the IMU tier).
inline const QString &pelvisRouteId() { static const QString r = QStringLiteral("pelvisImu"); return r; }

// ── σ, design §8.2, per sample (degrees) ────────────────────────────────────────────────────────
namespace R = tuned::bodyRotation;

// Bias drift: residual bias × time from address.
inline double driftDeg(int64_t tUs, int64_t addressUs)
{
    return R::kImuResidualBiasDps * double(std::llabs(tUs - addressUs)) * 1e-6;
}
// The mount-yaw error a segment bent forward by `bendDeg` puts on its obliquity: asin(sin θ·sin σ_δ)
// (design §6.2; 4.2° at θ = 25°, 6.4° at θ = 40°, at σ_δ = 10°).
inline double mountYawObliquityDeg(double bendDeg)
{
    const double sd = double(tuned::calib::kTrunkMountYawSigmaDeg) / kR2D;
    return std::asin(std::clamp(std::fabs(std::sin(bendDeg / kR2D)) * std::sin(sd), 0.0, 1.0)) * kR2D;
}
// spineForwardBend: the chest sensor's garment, gyro scale on the change from address, bias drift, and
// the mount yaw's term (ballPlaneMountYawDeg, below; 0 where the caller has none).
inline double forwardBendSigmaDeg(double bendDeg, double addrBendDeg, int64_t tUs, int64_t addressUs,
                                  double mountYawDeg = 0.0)
{
    const double t = R::kImuTissueSigmaThoraxDeg, g = R::kImuGyroScaleFrac * std::fabs(bendDeg - addrBendDeg);
    const double b = driftDeg(tUs, addressUs);
    return std::sqrt(t * t + g * g + b * b + mountYawDeg * mountYawDeg);
}
// The gyro-scale error of a segment's TURN leaking into its obliquity: a turn of ψ about a spine bent
// by θ tips the medio-lateral axis by ≈ sin θ · ψ, so a scale error of kImuGyroScaleFrac on the turn
// moves the obliquity by ≈ sin θ · kImuGyroScaleFrac · |ψ| (design §8.2's 1.5 % of the turn).
inline double turnLeakObliquityDeg(double turnDeg, double bendDeg)
{
    return R::kImuGyroScaleFrac * std::fabs(turnDeg) * std::fabs(std::sin(bendDeg / kR2D));
}
// spineSideBend: both sensors' garment, each one's δ term at its own forward bend, gyro scale on the
// change from address and on each sensor's turn (its leak through that sensor's bend), and each
// sensor's bias drift.
inline double sideBendSigmaDeg(double sideDeg, double addrSideDeg, double thoraxBendDeg, double pelvisBendDeg,
                               double thoraxTurnDeg, double pelvisTurnDeg, int64_t tUs, int64_t addressUs)
{
    const double tt = R::kImuTissueSigmaThoraxDeg, tp = R::kImuTissueSigmaPelvisDeg;
    const double dt = mountYawObliquityDeg(thoraxBendDeg), dp = mountYawObliquityDeg(pelvisBendDeg);
    const double g = R::kImuGyroScaleFrac * std::fabs(sideDeg - addrSideDeg);
    const double lt = turnLeakObliquityDeg(thoraxTurnDeg, thoraxBendDeg), lp = turnLeakObliquityDeg(pelvisTurnDeg, pelvisBendDeg);
    const double b = driftDeg(tUs, addressUs);
    return std::sqrt(tt * tt + tp * tp + dt * dt + dp * dp + g * g + lt * lt + lp * lp + 2.0 * b * b);
}
// The pelvis sensor's turn from address: garment, gyro scale, bias drift (δ: "near zero for turn").
inline double pelvisTurnSigmaDeg(double turnDeg, int64_t tUs, int64_t addressUs)
{
    const double t = R::kImuTissueSigmaPelvisDeg, g = R::kImuGyroScaleFrac * std::fabs(turnDeg);
    const double b = driftDeg(tUs, addressUs);
    return std::sqrt(t * t + g * g + b * b);
}

// THE FORWARD BEND AS THE DOWN-THE-LINE CAMERA READS IT (W3 review, 12 Nov 2026). The segment's long
// axis s = q·ẑ in the VERTICAL PLANE THROUGH THE BALL: atan2(s·Ŷ, s·ẑ), Ŷ the golfer's horizontal
// front at THIS swing's address — square to the segment's medio-lateral axis there, ẑ × the ML axis's
// ground direction (the stance frame's +Y anterior, toward the ball; the turn's own address reference,
// so a fusion-yaw drift since stand tall drops out). + toward the ball.
// Why not trunk_angles::forwardBendRad (D13's "inclination of the long axis, anterior +"): it takes its
// SIGN from the segment's own anterior axis, which turns with the chest — past ~90° of turn (earlier
// with a side tilt or a mount yaw) a chest still bent over the ball reads −40° — and its SIZE is the
// whole inclination, so a side tilt inflates it. The ball-plane projection is sign-stable through the
// swing, and is what the down-the-line camera and the norms read. A mount yaw δ moves it through the
// address front's heading: second order while the long axis stays in the ball plane, FIRST ORDER once a
// side tilt leans it out (ballPlaneMountYawDeg, in its σ and body.trunkMountYawAssumed). D13 stays for the face-in cue at
// address, where the segment has not turned (body_rotation.cpp addressBendDeg).
// `front` = ballPlaneFront(q at address); returns NaN when the ML axis there has no ground direction.
struct BallFront { double x = 0.0, y = 1.0; bool ok = false; };
inline BallFront ballPlaneFront(const QQuaternion &qAddr)
{
    const QVector3D ml = qAddr.rotatedVector(QVector3D(1.0f, 0.0f, 0.0f));
    const double h = std::hypot(double(ml.x()), double(ml.y()));
    BallFront f;
    if (!(h > 0.05)) return f;                       // the ML axis within ~3° of vertical: no front
    f.x = -double(ml.y()) / h;                       // ẑ × (ml_x, ml_y, 0) / h
    f.y =  double(ml.x()) / h;
    f.ok = true;
    return f;
}
inline double ballPlaneBendDeg(const QQuaternion &q, const BallFront &front)
{
    if (!front.ok) return kNaN;
    const QVector3D si = q.rotatedVector(QVector3D(0.0f, 0.0f, 1.0f));
    return std::atan2(double(si.x()) * front.x + double(si.y()) * front.y, double(si.z())) * kR2D;
}
// WHAT A MOUNT YAW δ DOES TO IT (W3 re-review). δ turns the sensor's believed axes about its own long
// axis; the address front Ŷ, taken from the medio-lateral axis there, then sits ε ≈ atan(tan δ·cos θ_addr)
// off the long axis's own heading (7.7° at δ 10°, θ 40°). Rotating Ŷ by ε moves the bend by
// d/dε atan2(s·Ŷ, s_z) = ε · s_z·(s·X̂) / ((s·Ŷ)² + s_z²), X̂ the horizontal square to Ŷ: nothing where the
// long axis stays in the ball plane (s·X̂ = 0, a bend with no side tilt), FIRST ORDER in ε once it leans
// out of it — −3.3° on a 35° chest, 45° open, with a 30° side tilt at impact (−0.28° without the tilt).
// That term at σ_δ (tuned::calib::kTrunkMountYawSigmaDeg), in degrees.
inline double ballPlaneMountYawDeg(const QQuaternion &q, const BallFront &front, double addrBendDeg)
{
    if (!front.ok) return 0.0;
    const double sd = double(tuned::calib::kTrunkMountYawSigmaDeg) / kR2D;
    const double eps = std::atan(std::tan(sd) * std::cos(addrBendDeg / kR2D));
    const QVector3D si = q.rotatedVector(QVector3D(0.0f, 0.0f, 1.0f));
    const double sy = double(si.x()) * front.x + double(si.y()) * front.y;
    const double sx = double(si.x()) * front.y - double(si.y()) * front.x;     // X̂ = (Ŷ_y, −Ŷ_x, 0)
    const double sz = double(si.z());
    const double den = sy * sy + sz * sz;
    if (!(den > 1e-9)) return 0.0;
    return std::fabs(eps * sz * sx / den) * kR2D;
}

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

// The frames a reading at `us` is the median of (valid, finite, within the grader's window), and the
// reading itself — reduceAt, the grader's call. `sigma` = the median of those frames' σ (NaN: none).
inline Reduced gradedAt(const MetricSeries &m, const std::vector<double> &frameSigma, int64_t us, double &sigma,
                        std::vector<size_t> *frames = nullptr)
{
    SeriesView view;
    view.t = m.t_us.data(); view.v = m.value.data(); view.n = m.t_us.size();
    view.valid = m.valid.size() == m.t_us.size() ? m.valid.data() : nullptr;
    const ReduceConfig rc{};
    const Reduced r = reduceAt(view, us, rc);
    std::vector<double> sg;
    if (r.ok)
        for (size_t i = 0; i < m.t_us.size(); ++i)
            if (std::llabs(m.t_us[i] - us) <= rc.atHalfWindowUs && view.isValid(i)) {
                if (frames) frames->push_back(i);
                if (i < frameSigma.size() && std::isfinite(frameSigma[i])) sg.push_back(frameSigma[i]);
            }
    sigma = medianOf(sg);
    return r;
}

// Stamp the series σ (the median over its valid frames) and one phase sample per ladder phase in
// `at`, each the graded reading with its own σ. A phase off the ladder, or with no valid frame in its
// window, gets no sample: the grader then has no reading there either. With `withSigma` false
// (uncertainty.enabled off) no σ is written at all.
inline void stampGradedPhases(MetricSeries &m, const std::vector<double> &frameSigma,
                              const std::vector<PhaseEvent> &phases, const std::vector<Phase> &at, bool withSigma)
{
    if (withSigma) {
        std::vector<double> sg;
        for (size_t i = 0; i < m.t_us.size() && i < frameSigma.size(); ++i)
            if ((m.valid.empty() || m.valid[i]) && std::isfinite(frameSigma[i])) sg.push_back(frameSigma[i]);
        if (!sg.empty()) { m.sigma = medianOf(sg); m.sigmaKind = uint8_t(SigmaKind::Propagated); }
    }
    for (Phase p : at) {
        const std::optional<int64_t> us = ladderTime(phases, p);
        if (!us) continue;
        double s = kNaN;
        const Reduced r = gradedAt(m, frameSigma, *us, s);
        if (!r.ok) continue;
        PhaseSample ps{ p, *us, r.value, QString() };
        if (withSigma && std::isfinite(s)) { ps.sigma = s; ps.sigmaKind = uint8_t(SigmaKind::Propagated); }
        m.phaseSamples.push_back(ps);
    }
}

} // namespace trunk_sensor

// ── Hip internal rotation ───────────────────────────────────────────────────────────────────────

// One thigh's own turn from address off the two-camera fit (body3d::thighTurn): the fit's frames,
// each turn lead-relative in degrees with its σ; NaN = not read at that frame.
struct HipIrThigh {
    std::vector<int64_t> t_us;
    std::vector<double>  turnDeg, sigmaDeg;
};

struct HipInternalRotationInput {
    // pelvisRotationSigned from the PELVIS SENSOR (routeId "pelvisImu"): lead-relative, + open. The
    // two-camera pelvis turn is not an input — its error is the same fit's as the thigh's, and the
    // rung this answers is the pelvis sensor's.
    const MetricSeries *pelvisSigned = nullptr;
    std::optional<HipIrThigh> leadThigh, trailThigh;   // absent: no two-camera fit, or no thigh at address
    bool   fitUsed = false;                             // a valid two-camera fit reached the stage (the record's code)
    std::vector<PhaseEvent> phases;
    bool   withSigma = true;                            // uncertainty.enabled
    double fixedThighSigmaDeg = tuned::bodyRotation::kHipIrThighFixedSigmaDeg;
};

// One side's outcome. `assumedAt*`: the graded reading at that position took the thigh as fixed on at
// least one of its frames; `measuredAt*`: on at least one it read the fit's thigh.
struct HipIrSide {
    std::optional<MetricSeries> series;
    bool assumedAtTop = false, assumedAtImpact = false;
    bool measuredAtTop = false, measuredAtImpact = false;
    int  measuredFrames = 0, frames = 0;                // over the whole curve, for the log
};

struct HipInternalRotationOutput {
    HipIrSide lead, trail;
    std::vector<GateRecord> gates;                      // body.thighTurnAssumed, when a graded reading assumed it
};

namespace trunk_sensor {

// The thigh's turn at instant t: linear between the two fit frames around it, both read (or the frame
// itself). Outside the fit's frames, or next to an unread frame, it is not read.
inline bool thighAt(const HipIrThigh &th, int64_t t, double &v, double &s)
{
    const std::vector<int64_t> &T = th.t_us;
    if (T.empty() || th.turnDeg.size() != T.size() || th.sigmaDeg.size() != T.size()) return false;
    const size_t hi = size_t(std::lower_bound(T.begin(), T.end(), t) - T.begin());
    if (hi < T.size() && T[hi] == t) {
        if (!std::isfinite(th.turnDeg[hi])) return false;
        v = th.turnDeg[hi]; s = th.sigmaDeg[hi];
        return true;
    }
    if (hi == 0 || hi >= T.size()) return false;
    const size_t lo = hi - 1;
    if (!std::isfinite(th.turnDeg[lo]) || !std::isfinite(th.turnDeg[hi])) return false;
    const double f = double(t - T[lo]) / double(std::max<int64_t>(1, T[hi] - T[lo]));
    v = th.turnDeg[lo] + (th.turnDeg[hi] - th.turnDeg[lo]) * f;
    s = th.sigmaDeg[lo] + (th.sigmaDeg[hi] - th.sigmaDeg[lo]) * f;   // NaN when either was unassessed
    return true;
}

} // namespace trunk_sensor

inline HipInternalRotationOutput buildHipInternalRotation(const HipInternalRotationInput &in)
{
    namespace ts = trunk_sensor;
    HipInternalRotationOutput out;
    if (!in.pelvisSigned || in.pelvisSigned->t_us.size() < 2
        || in.pelvisSigned->value.size() != in.pelvisSigned->t_us.size())
        return out;
    const MetricSeries &p = *in.pelvisSigned;
    const size_t n = p.t_us.size();
    const int64_t addressUs = ts::ladderTime(in.phases, Phase::Address).value_or(p.t_us.front());
    const std::vector<Phase> graded{ Phase::Top, Phase::Impact };

    const auto side = [&](bool lead) {
        HipIrSide s;
        const std::optional<HipIrThigh> &th = lead ? in.leadThigh : in.trailThigh;
        MetricSeries m;
        m.key   = lead ? QStringLiteral("leadHipInternalRotation") : QStringLiteral("trailHipInternalRotation");
        m.label = lead ? QStringLiteral("Lead hip internal rotation") : QStringLiteral("Trail hip internal rotation");
        m.unit  = QStringLiteral("°");
        m.t_us  = p.t_us;
        m.valid = p.valid.size() == n ? p.valid : std::vector<uint8_t>{};
        m.value.resize(n);
        std::vector<double> sigma(n, ts::kNaN);
        std::vector<char> measured(n, 0);
        for (size_t i = 0; i < n; ++i) {
            const double psi = p.value[i];
            double thigh = 0.0, thighSigma = in.fixedThighSigmaDeg;
            if (th && ts::thighAt(*th, p.t_us[i], thigh, thighSigma)) measured[i] = 1;
            else { thigh = 0.0; thighSigma = in.fixedThighSigmaDeg; }
            m.value[i] = lead ? psi - thigh : thigh - psi;
            const double sp = ts::pelvisTurnSigmaDeg(psi, p.t_us[i], addressUs);
            sigma[i] = std::isfinite(thighSigma) ? std::hypot(sp, thighSigma) : ts::kNaN;
            if (m.valid.empty() || m.valid[i]) {
                ++s.frames;
                if (measured[i]) ++s.measuredFrames;
            }
        }
        ts::stampGradedPhases(m, sigma, in.phases, graded, in.withSigma);
        // Which thigh each GRADED reading rests on: the frames in its window.
        for (const PhaseSample &ps : m.phaseSamples) {
            double sg = ts::kNaN;
            std::vector<size_t> frames;
            ts::gradedAt(m, sigma, ps.t_us, sg, &frames);
            bool anyAssumed = false, anyMeasured = false;
            for (size_t i : frames) (measured[i] ? anyMeasured : anyAssumed) = true;
            if (ps.phase == Phase::Top)    { s.assumedAtTop = anyAssumed;    s.measuredAtTop = anyMeasured; }
            if (ps.phase == Phase::Impact) { s.assumedAtImpact = anyAssumed; s.measuredAtImpact = anyMeasured; }
        }
        // The route this side answers: the fused rung where a graded reading read the fit's thigh.
        m.routeId     = (s.measuredAtTop || s.measuredAtImpact) ? QStringLiteral("pelvisImu+faceOn+dtl")
                                                                : QStringLiteral("pelvisImu");
        m.routeDirect = false;
        s.series = std::move(m);
        return s;
    };
    out.lead  = side(true);
    out.trail = side(false);

    // THE RECORD, only when it is true of a graded reading: the thigh taken as fixed on a frame the Top
    // or Impact reading is the median of. Covers exactly those readings.
    QStringList covers;
    if (out.trail.assumedAtTop || out.trail.assumedAtImpact) covers << QStringLiteral("trailHipInternalRotation");
    if (out.lead.assumedAtTop || out.lead.assumedAtImpact)   covers << QStringLiteral("leadHipInternalRotation");
    if (!covers.isEmpty()) {
        // noFit only when there was no fit at all; with a fit, the fit did not read that thigh there.
        out.gates.push_back(GateRecord{ PP_GATE("body.thighTurnAssumed"), covers.front(), GateEffect::Downgraded,
                                        QVariantMap{ { QStringLiteral("code"), in.fitUsed ? QStringLiteral("thighUnread")
                                                                                          : QStringLiteral("noFit") },
                                                     { QStringLiteral("sigmaDeg"), in.fixedThighSigmaDeg } },
                                        {}, {}, covers });
    }
    return out;
}

// ── Precedence: the trunk sensors' bends win — when they can be trusted to ─────────────────────
//
// The plan's rule (IMU > 3-D > single camera): a spineForwardBend / spineSideBend from the trunk
// sensors (routeId "trunkImus") is THE reading of the key. The down-the-line forward bend
// (DtlPostureStage runs AFTER BodyRotationStage), the face-on side bend (UpperBodyMetricsStage, before
// it) and the 3-D side bend (Skeleton3dBodyStage, which already steps aside when any side bend is
// present) are erased, and the records that spoke for them stop speaking for the key
// (route_substitution.h supersedeReadings). No record is fired: the better rung winning is not a
// fallback. Run ONCE AFTER EVERY STAGE (runProfile, before reconcileSubstitutedReadings), so it holds
// whatever order the stages run in.
//
// TWO EXCEPTIONS, where another route also read the key (W3 review rulings, 12 Nov 2026) — the camera
// reading stays the answer, the sensor bend is NOT published for the key, and the sensor's own records
// stop speaking for it (the face-in covers lose the key; body.chestBendSite / body.trunkMountYawAssumed
// for the key are dropped):
//   · a sensor the key is read from is probably worn face-in (body.thoraxFaceIn / body.pelvisFaceIn
//     covers the key): a backward-reading sensor must not beat a good camera reading. With no other
//     route, the sensor bend is published with the face-in ⚠, as before;
//   · the sensor reading has no stamped reading at a phase the camera series would be graded at (a
//     ladder phase where the camera curve reads, or a phase it labels — pass the ladder as `ladder`):
//     superseding it would leave a gap with no reason, where the camera had a reading.
struct TrunkPrecedence {
    int seriesDropped  = 0;   // camera / 3-D series erased because the sensor won
    int recordsCleaned = 0;   // (record, key) pairs taken off a key
    int sensorDropped  = 0;   // sensor bends withdrawn because the camera stayed
};
inline TrunkPrecedence applyTrunkSensorPrecedence(std::vector<MetricSeries> &series, std::vector<GateRecord> &gates,
                                                  const std::vector<PhaseEvent> &ladder = {})
{
    TrunkPrecedence total;
    const QString &route = trunk_sensor::bendRouteId();
    // The phases a camera series WOULD BE GRADED AT (measure_sample.cpp): every ladder phase where its
    // curve has a reading (reduceAt), plus every phase it labels.
    const auto gradedPhases = [&](const MetricSeries &m) {
        std::vector<Phase> out;
        for (const PhaseSample &ps : m.phaseSamples) out.push_back(ps.phase);
        for (const PhaseEvent &e : ladder) {
            double sg = trunk_sensor::kNaN;
            if (m.t_us.size() == m.value.size() && !m.t_us.empty()
                && trunk_sensor::gradedAt(m, {}, e.t_us, sg).ok)
                out.push_back(e.phase);
        }
        return out;
    };
    for (const QString &key : { QStringLiteral("spineForwardBend"), QStringLiteral("spineSideBend") }) {
        const auto sensorIt = std::find_if(series.begin(), series.end(), [&](const MetricSeries &m) {
            return m.key == key && m.routeId == route;
        });
        if (sensorIt == series.end()) continue;
        bool others = false, gapFree = true;
        for (const MetricSeries &m : series) {
            if (m.key != key || m.routeId == route) continue;
            others = true;
            for (Phase ph : gradedPhases(m))
                if (std::none_of(sensorIt->phaseSamples.begin(), sensorIt->phaseSamples.end(),
                                 [&](const PhaseSample &q) { return q.phase == ph; }))
                    gapFree = false;
        }
        if (!others) continue;   // the sensor alone: published, with whatever records it carries
        const bool faceIn = std::any_of(gates.begin(), gates.end(), [&](const GateRecord &g) {
            return (g.gate == QLatin1String("body.thoraxFaceIn") || g.gate == QLatin1String("body.pelvisFaceIn"))
                && g.covers.contains(key);
        });
        if (faceIn || !gapFree) {
            series.erase(std::remove_if(series.begin(), series.end(),
                                        [&](const MetricSeries &m) { return m.key == key && m.routeId == route; }),
                         series.end());
            ++total.sensorDropped;
            for (GateRecord &g : gates)
                if ((g.gate == QLatin1String("body.thoraxFaceIn") || g.gate == QLatin1String("body.pelvisFaceIn"))
                    && g.covers.removeAll(key) > 0)
                    ++total.recordsCleaned;
            // The sensor's assumption records lose the key; one left covering nothing is dropped, one that
            // still covers the other bend keeps it (its subject moves to that bend).
            std::vector<char> drop(gates.size(), 0);
            for (size_t gi = 0; gi < gates.size(); ++gi) {
                GateRecord &g = gates[gi];
                if (g.gate != QLatin1String("body.chestBendSite") && g.gate != QLatin1String("body.trunkMountYawAssumed"))
                    continue;
                if (g.covers.removeAll(key) == 0 && g.subject != key) continue;
                ++total.recordsCleaned;
                if (g.covers.isEmpty()) drop[gi] = 1;
                else if (g.subject == key) g.subject = g.covers.front();
                // The side-bend terms are evidence about the side bend only; a record that no longer
                // speaks for it must not state them (gate_catalogue_body.h's optional blocks drop them).
                if (key == QLatin1String("spineSideBend")) {
                    g.evidence.remove(QStringLiteral("thoraxAddrDeg"));
                    g.evidence.remove(QStringLiteral("pelvisAddrDeg"));
                }
            }
            std::vector<GateRecord> kept;
            for (size_t gi = 0; gi < gates.size(); ++gi)
                if (!drop[gi]) kept.push_back(std::move(gates[gi]));
            gates = std::move(kept);
            continue;
        }
        const Supersession s = supersedeReadings(series, gates, key, route);
        total.seriesDropped  += s.seriesDropped;
        total.recordsCleaned += s.recordsCleaned;
    }
    return total;
}

} // namespace pinpoint::analysis
