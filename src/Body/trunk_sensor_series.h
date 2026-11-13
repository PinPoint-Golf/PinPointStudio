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
//   · the three tilts (W4, 13 Nov 2026): shoulderPlaneAngle3d (the chest's ML axis against level),
//     hipLineTilt and secondaryAxisTilt (the hip sensor's ML axis and the chest's long axis in the
//     face-on plane, from the address front) — definitions, signs and σ at TrailAxis below;
//   · the precedence rule: a trunk-sensor spineForwardBend / spineSideBend / shoulderPlaneAngle3d /
//     hipLineTilt / secondaryAxisTilt beats the down-the-line, face-on and 3-D readings — unless the
//     sensor is worn face-in or reads fewer positions than the camera (applyTrunkSensorPrecedence, run
//     once after every stage).
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
// The pelvis sensor's route on pelvisRotationSigned (body_rotation.cpp sets it on the IMU tier), and on
// hipLineTilt (W4, 13 Nov 2026).
inline const QString &pelvisRouteId() { static const QString r = QStringLiteral("pelvisImu"); return r; }
// The chest sensor's route on thoraxRotationSigned, and on shoulderPlaneAngle3d and secondaryAxisTilt (W4).
inline const QString &thoraxRouteId() { static const QString r = QStringLiteral("thoraxImu"); return r; }

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

// ── THE THREE TILTS (planned-metrics second pass W4, 13 Nov 2026) ───────────────────────────────
//
// shoulderPlaneAngle3d, hipLineTilt and secondaryAxisTilt from the trunk sensors. Each keeps the
// definition and sign of the camera reading it stands in for, so that reading's norms still apply.
//
// THE SHOULDER PLANE needs no plane: it is the 3-D fit's quantity, the line's inclination out of the
// horizontal in its OWN vertical plane (skeleton3d_body_metrics.h shoulderPlaneDeg, atan2(z_trail −
// z_lead, |horizontal|)), and for a unit axis that is asin of its height — trunk_angles::obliquityRad,
// signed + with the TRAIL side up (s below, the side bend's factor).
//
// THE HIP TILT AND THE AXIS TILT are FACE-ON readings: an angle in the vertical plane that contains
// the target line, which the face-on camera looks square onto (Mark, 12 Nov: a camera fixed to a wall
// is square to the target line). The sensors have no target line; the address front stands in for it
// (ballPlaneFront: Ŷ = ẑ × the segment's ML axis at THIS swing's address, toward the ball), and the
// target line is the horizontal square to it — the segment's own ML axis's ground direction at address.
// So the plane assumes the segment was SQUARE to the target line at address, the assumption the sensor
// forward bend already makes (there it is second order; here it is first order for the axis tilt —
// the address front's heading term below). That assumption is never hidden: every reading taken against
// an address front carries the Downgraded body.targetFromAddress (W4c, body_rotation.cpp
// recordTrunkAssumptions) until the target line comes from the alignment stick (#178).
//   `TrailAxis` is that horizontal unit axis, pointing to the golfer's TRAIL side: s·(golfer's right),
//   s = +1 for a right-hander (lead side left) and −1 for a left-hander.
//   hipLineTilt       — lower_body_metrics.cpp lineTiltDeg(lead, trail) = atan2(leadY − trailY, |Δx|) in
//                       the image (y down), i.e. atan2(z_trail − z_lead, |the hip line along the target
//                       line|). With d = s·(q·x̂) the lead → trail direction of the hip sensor's ML axis:
//                       atan2(d_z, |d·T|). + = the TRAIL hip higher.
//                       AS THE LINE TURNS EDGE-ON the face-on producer drops the frame: |Δx| below
//                       minHipSpanRatio × its address |Δx| has no hip line (body.hipLine). The sensor
//                       keeps the same rule on the same ratio — |d·T| against |d_addr·T| — so it reads
//                       where the camera's definition reads and not where it runs to ±90°.
//   secondaryAxisTilt — upper_body_metrics.cpp atan2(towardTrail, rise): the mid-hip → mid-shoulder line
//                       from vertical, + AWAY FROM THE TARGET. On the chest sensor's long axis s = q·ẑ:
//                       atan2(s·T, s_z), and no reading when s_z ≤ 0 (the face-on producer's `rise > kEps`).
//                       It is the upper back's lean, not the hip-to-shoulder line (body.chestBendSite).
// THE FRONT'S HEADING moves both readings, the axis tilt at FIRST ORDER even at address (a long axis
// bent θ toward the ball reads atan(tan θ·sin ε) of lean in a plane turned by ε: 8.3° at θ 40°, ε 10°),
// the hip tilt first order only once the hips have turned. Its σ is propagated NUMERICALLY:
// the reading re-evaluated with T turned by ±σ_δ about vertical, half the spread
// (faceOnFrontTermDeg). σ_δ (tuned::calib::kTrunkMountYawSigmaDeg) is what a mount yaw does to the
// front — ε ≈ atan(tan δ·cos θ_addr), 9.4° on the hips at 20°, 7.7° on the chest at 40° — and the
// rest is the golfer's own address alignment, which no constant measures (planned_metrics_promotion.md
// §6, W4).
struct TrailAxis { double x = 1.0, y = 0.0; bool ok = false; };
inline TrailAxis trailAxisOf(const BallFront &f, bool leadIsLeft)
{
    TrailAxis a;
    if (!f.ok) return a;
    const double s = leadIsLeft ? 1.0 : -1.0;     // the golfer's right IS the trail side of a right-hander
    a.x =  s * f.y;                               // the golfer's right at address = (Ŷ_y, −Ŷ_x): Ŷ = ẑ × it
    a.y = -s * f.x;
    a.ok = true;
    return a;
}
inline TrailAxis turnedBy(const TrailAxis &a, double epsDeg)
{
    const double e = epsDeg / kR2D, c = std::cos(e), s = std::sin(e);
    return TrailAxis{ a.x * c - a.y * s, a.x * s + a.y * c, a.ok };
}
// The handedness factor of every lateral sensor reading here: + with the golfer's RIGHT side, which is
// the TRAIL side of a right-hander (fillTrunkBends' side bend uses the same s).
inline double trailSign(bool leadIsLeft) { return leadIsLeft ? 1.0 : -1.0; }

// shoulderPlaneAngle3d: the chest's ML axis against level, + the TRAIL shoulder higher (°).
inline double shoulderPlaneDeg(const QQuaternion &qThorax, bool leadIsLeft)
{
    const QVector3D ml = qThorax.rotatedVector(QVector3D(1.0f, 0.0f, 0.0f));
    return trailSign(leadIsLeft) * std::asin(std::clamp(double(ml.z()), -1.0, 1.0)) * kR2D;
}
// hipLineTilt: the hip sensor's ML axis in the face-on plane, + the TRAIL hip higher (°); `epsDeg` turns
// the plane's target line about vertical (the front term). NaN without a plane.
inline double faceOnHipTiltDeg(const QQuaternion &qPelvis, const TrailAxis &a, bool leadIsLeft, double epsDeg = 0.0)
{
    if (!a.ok) return kNaN;
    const TrailAxis t = epsDeg == 0.0 ? a : turnedBy(a, epsDeg);
    const QVector3D ml = qPelvis.rotatedVector(QVector3D(1.0f, 0.0f, 0.0f));
    const double s = trailSign(leadIsLeft);
    const double dz = s * double(ml.z());
    const double along = std::fabs(s * (double(ml.x()) * t.x + double(ml.y()) * t.y));
    return std::atan2(dz, along) * kR2D;
}
// The hip line's length along the target line (the face-on camera's |Δx|, for a unit line).
inline double faceOnHipSpan(const QQuaternion &qPelvis, const TrailAxis &a)
{
    if (!a.ok) return 0.0;
    const QVector3D ml = qPelvis.rotatedVector(QVector3D(1.0f, 0.0f, 0.0f));
    return std::fabs(double(ml.x()) * a.x + double(ml.y()) * a.y);
}
// secondaryAxisTilt: the chest's long axis in the face-on plane from vertical, + AWAY from the target
// (°); NaN where the long axis does not point up (the camera's rise ≤ 0) or without a plane.
inline double faceOnAxisTiltDeg(const QQuaternion &qThorax, const TrailAxis &a, double epsDeg = 0.0)
{
    if (!a.ok) return kNaN;
    const TrailAxis t = epsDeg == 0.0 ? a : turnedBy(a, epsDeg);
    const QVector3D si = qThorax.rotatedVector(QVector3D(0.0f, 0.0f, 1.0f));
    const double rise = double(si.z());
    if (!(rise > 1e-9)) return kNaN;
    return std::atan2(double(si.x()) * t.x + double(si.y()) * t.y, rise) * kR2D;
}
// The address front's heading term: half the spread of `reading(ε)` at ε = ±σ_δ (degrees; 0 when either
// side has no reading).
template <class F>
inline double faceOnFrontTermDeg(F reading)
{
    const double sd = double(tuned::calib::kTrunkMountYawSigmaDeg);
    const double hi = reading(sd), lo = reading(-sd);
    if (!std::isfinite(hi) || !std::isfinite(lo)) return 0.0;
    return 0.5 * std::fabs(hi - lo);
}

// ── σ of the tilts, design §8.2 per sample (degrees) ──
// ONE SENSOR'S OBLIQUITY BUDGET — that sensor's share of sideBendSigmaDeg: its garment, the mount yaw δ
// through its own forward bend (§6.2, asin(sin θ·sin σ_δ)), the gyro scale on the obliquity's change
// from address, its turn's scale error leaking through its bend, and its bias drift. No new term.
inline double obliquitySigmaDeg(double tissueDeg, double oblDeg, double addrOblDeg, double bendDeg, double turnDeg,
                                int64_t tUs, int64_t addressUs)
{
    const double d = mountYawObliquityDeg(bendDeg), g = R::kImuGyroScaleFrac * std::fabs(oblDeg - addrOblDeg);
    const double l = turnLeakObliquityDeg(turnDeg, bendDeg), b = driftDeg(tUs, addressUs);
    return std::sqrt(tissueDeg * tissueDeg + d * d + g * g + l * l + b * b);
}
// shoulderPlaneAngle3d: the chest's obliquity budget, as it stands — the reading IS the obliquity.
inline double shoulderPlaneSigmaDeg(double planeDeg, double addrPlaneDeg, double thoraxBendDeg, double thoraxTurnDeg,
                                    int64_t tUs, int64_t addressUs)
{
    return obliquitySigmaDeg(R::kImuTissueSigmaThoraxDeg, planeDeg, addrPlaneDeg, thoraxBendDeg, thoraxTurnDeg,
                             tUs, addressUs);
}
// hipLineTilt: the hip sensor's obliquity budget CARRIED THROUGH THE FACE-ON PROJECTION — an obliquity
// error e moves atan2(sin φ, cos φ·|cos ρ|) (φ the hips' obliquity, ρ their heading off the target line)
// by half the spread at φ ± e, which is e at address (ρ = 0) and ≈ e/|cos ρ| as the hips turn: 1.4× at a
// 45° turn, 2.5× at the edge-on gate — ⊕ the address front's heading term.
inline double hipTiltSigmaDeg(const QQuaternion &qPelvis, const TrailAxis &a, bool leadIsLeft, double oblSigmaDeg)
{
    const QVector3D ml = qPelvis.rotatedVector(QVector3D(1.0f, 0.0f, 0.0f));
    const double z = std::clamp(double(ml.z()), -1.0, 1.0), h = std::hypot(double(ml.x()), double(ml.y()));
    const double phi = std::asin(z), c = h > 1e-9 ? faceOnHipSpan(qPelvis, a) / h : 0.0;   // |cos ρ|
    const double e = oblSigmaDeg / kR2D;
    const auto proj = [&](double p) { return std::atan2(std::sin(p), std::cos(p) * c); };
    const double obl = 0.5 * std::fabs(proj(phi + e) - proj(phi - e)) * kR2D;
    const double front = faceOnFrontTermDeg([&](double eps) { return faceOnHipTiltDeg(qPelvis, a, leadIsLeft, eps); });
    return std::hypot(obl, front);
}
// secondaryAxisTilt: the chest's LONG-AXIS budget — garment, gyro scale on the reading's change from
// address, bias drift (forwardBendSigmaDeg's terms). No δ term: δ is a turn about the long axis and does
// not move it (trunk_angles.h forwardBendRad; design §6.2's forward-bend row), so δ reaches this reading
// only through the address front, which the front term carries. No turn leak, for the same reason. An
// error within the face-on plane moves the reading one-for-one ⊕ the address front's heading term.
inline double axisTiltSigmaDeg(const QQuaternion &qThorax, const TrailAxis &a, double tiltDeg, double addrTiltDeg,
                               int64_t tUs, int64_t addressUs)
{
    const double t = R::kImuTissueSigmaThoraxDeg, g = R::kImuGyroScaleFrac * std::fabs(tiltDeg - addrTiltDeg);
    const double b = driftDeg(tUs, addressUs);
    const double front = faceOnFrontTermDeg([&](double eps) { return faceOnAxisTiltDeg(qThorax, a, eps); });
    return std::sqrt(t * t + g * g + b * b + front * front);
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

// ── Precedence: the trunk sensors' readings win — when they can be trusted to ─────────────────
//
// The plan's rule (IMU > 3-D > single camera): a trunk-sensor reading of one of these keys is THE
// reading of the key —
//   spineForwardBend, spineSideBend  routeId "trunkImus" (12 Nov 2026);
//   shoulderPlaneAngle3d             routeId "thoraxImu" (W4, 13 Nov 2026), over the 3-D fit's;
//   hipLineTilt                      routeId "pelvisImu" (W4), over the face-on hip line;
//   secondaryAxisTilt                routeId "thoraxImu" (W4), over the face-on spine line.
// The down-the-line forward bend (DtlPostureStage runs AFTER BodyRotationStage), the face-on readings
// (the upper and lower body stages) and the 3-D readings (Skeleton3dBodyStage: its side bend and its
// shoulder plane are published beside the sensor's — W4 / W5, 13 Nov 2026 — so they are there to fall
// back on) are erased, and the records that spoke for them stop speaking for the key
// (route_substitution.h supersedeReadings) — also when no other series was published, so a camera
// route's refusal (body.upperAddress, body.lowerAddress) does not stand beside the sensor's graded
// value. No record is fired: the better rung winning is not a fallback. Run ONCE AFTER EVERY STAGE
// (runProfile, before reconcileSubstitutedReadings), so it holds whatever order the stages run in.
// shoulderTiltVsBend (W7, shoulder_tilt_vs_bend.h) is derived from the two halves this leaves, after both.
//
// TWO EXCEPTIONS, where another route also read the key (W3 review rulings, 12 Nov 2026) — the camera
// reading stays the answer, the sensor reading is NOT published for the key, and the sensor's own records
// stop speaking for it (the face-in covers lose the key; body.chestBendSite / body.chestShoulderSite /
// body.trunkMountYawAssumed / body.targetFromAddress (W4c) lose the key, and one left covering nothing is
// dropped):
//   · a sensor the key is read from is probably worn face-in (body.thoraxFaceIn / body.pelvisFaceIn
//     covers the key): a backward-reading sensor must not beat a good camera reading. With no other
//     route, the sensor bend is published with the face-in ⚠, as before;
//   · the sensor reading has no stamped reading at a phase the camera series would be graded at (a
//     ladder phase where the camera curve reads, or a phase it labels — pass the ladder as `ladder`):
//     superseding it would leave a gap with no reason, where the camera had a reading.
struct TrunkPrecedence {
    int seriesDropped  = 0;   // camera / 3-D series erased because the sensor won
    int recordsCleaned = 0;   // (record, key) pairs taken off a key
    int sensorDropped  = 0;   // sensor readings withdrawn because the camera stayed
};
// The keys the rule covers, each with the trunk-sensor route that reads it.
struct TrunkSensorKey { const char *key; const QString &(*route)(); };
inline const std::vector<TrunkSensorKey> &trunkSensorKeys()
{
    static const std::vector<TrunkSensorKey> k{
        { "spineForwardBend", &trunk_sensor::bendRouteId },     { "spineSideBend", &trunk_sensor::bendRouteId },
        { "shoulderPlaneAngle3d", &trunk_sensor::thoraxRouteId }, { "hipLineTilt", &trunk_sensor::pelvisRouteId },
        { "secondaryAxisTilt", &trunk_sensor::thoraxRouteId } };
    return k;
}
inline TrunkPrecedence applyTrunkSensorPrecedence(std::vector<MetricSeries> &series, std::vector<GateRecord> &gates,
                                                  const std::vector<PhaseEvent> &ladder = {})
{
    TrunkPrecedence total;
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
    for (const TrunkSensorKey &tk : trunkSensorKeys()) {
        const QString key = QString::fromLatin1(tk.key);
        const QString &route = tk.route();
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
        const bool faceIn = std::any_of(gates.begin(), gates.end(), [&](const GateRecord &g) {
            return (g.gate == QLatin1String("body.thoraxFaceIn") || g.gate == QLatin1String("body.pelvisFaceIn"))
                && g.covers.contains(key);
        });
        // The two exceptions need another route's reading to fall back on; the sensor alone is
        // published, with whatever records it carries (a face-in sensor with its ⚠).
        if (others && (faceIn || !gapFree)) {
            series.erase(std::remove_if(series.begin(), series.end(),
                                        [&](const MetricSeries &m) { return m.key == key && m.routeId == route; }),
                         series.end());
            ++total.sensorDropped;
            for (GateRecord &g : gates)
                if ((g.gate == QLatin1String("body.thoraxFaceIn") || g.gate == QLatin1String("body.pelvisFaceIn"))
                    && g.covers.removeAll(key) > 0)
                    ++total.recordsCleaned;
            // The sensor's assumption records lose the key; one left covering nothing is dropped, one that
            // still covers another sensor reading keeps it (its subject moves to that reading).
            std::vector<char> drop(gates.size(), 0);
            for (size_t gi = 0; gi < gates.size(); ++gi) {
                GateRecord &g = gates[gi];
                if (g.gate != QLatin1String("body.chestBendSite") && g.gate != QLatin1String("body.chestShoulderSite")
                    && g.gate != QLatin1String("body.trunkMountYawAssumed") && g.gate != QLatin1String("body.targetFromAddress"))
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
                // The same for an address front: a sensor whose readings no longer speak through the
                // record does not state the heading its front was taken from (W4c).
                if (g.gate == QLatin1String("body.targetFromAddress")) {
                    if (!g.covers.contains(QStringLiteral("hipLineTilt")))
                        g.evidence.remove(QStringLiteral("hipAddressHeadingDeg"));
                    if (!g.covers.contains(QStringLiteral("spineForwardBend"))
                        && !g.covers.contains(QStringLiteral("secondaryAxisTilt")))
                        g.evidence.remove(QStringLiteral("chestAddressHeadingDeg"));
                }
            }
            std::vector<GateRecord> kept;
            for (size_t gi = 0; gi < gates.size(); ++gi)
                if (!drop[gi]) kept.push_back(std::move(gates[gi]));
            gates = std::move(kept);
            continue;
        }
        // The sensor reading stands: every other route's series under the key is erased, and the
        // Refused / Bounded / Masked records and weaker stand-ins that spoke for the key stop speaking
        // for it — with no other series too (W4): a camera route that refused at address leaves its
        // refusal covering the key, which is not true of the reading that stands.
        const Supersession s = supersedeReadings(series, gates, key, route);
        total.seriesDropped  += s.seriesDropped;
        total.recordsCleaned += s.recordsCleaned;
    }
    return total;
}

} // namespace pinpoint::analysis
