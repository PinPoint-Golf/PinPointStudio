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

#include "body_rotation.h"

#include <QVector3D>

#include <algorithm>
#include <cmath>

namespace pinpoint::analysis {
namespace {

constexpr double kRadToDeg = 57.29577951308232;
constexpr double kPi       = 3.14159265358979323846;
constexpr double kEps      = 1e-9;

// COCO body indices. Only the four span endpoints are needed, and both exist in either layout, so
// this module answers on a legacy 17-keypoint track exactly as it does on a WholeBody one.
constexpr int kLShoulder = 5, kRShoulder = 6, kLHip = 11, kRHip = 12;


// Wrap an angle difference into (−π, π]. Used only by the IMU tier, where the projected axis
// direction is a true angle and can cross the branch cut.
double wrapPi(double a)
{
    while (a >  kPi) a -= 2.0 * kPi;
    while (a <= -kPi) a += 2.0 * kPi;
    return a;
}

// ── THE CAMERA TIER IS GONE, and it is worth knowing what it was and why it went ───────────────
//
// It estimated turn from the collapse of an image span: `turn = acos(w(t) / w_address)`, the hip
// span for the pelvis and the shoulder span for the thorax. Honest in intent, reported as Bridged
// rather than Measured, and it carried a propagated sigma. It still could not do the job, and the
// arithmetic says so rather than any opinion about cameras.
//
// A COSINE IS FLAT WHERE THE SWING LIVES. dθ/dratio = 1/sin θ, so the sensitivity diverges as the
// body squares up. Measured on a real capture (2026-09-09, seven shots): the hip span scatters
// 2.1% over 59 STILL address frames — ordinary pose jitter — and that propagates to
//
//     ±1.9° at 40° of turn   ±3.5° at 20°   ±7.0° at 10°   ±13.9° at 5°
//
// Near the top it is fine. Near impact — where the pelvis is passing through square, which is the
// instant most of the interesting questions are asked about — it is not. Over a 41 ms P6→P7 window
// two endpoint errors give ±123 °/s on a rate whose whole graded corridor is 100 °/s wide. The
// noise was larger than the quantity.
//
// THE SAME CAPTURE SHOWED IT AS BIAS, not just noise: the address frame itself read 19° of turn,
// and address is 0° by definition. And the impact span measured 95% of the address span, which the
// cosine reads as 18°; a pelvis 40° open reads 77%. Whether that golfer was barely open or the
// method could not see it is NOT DECIDABLE from one view, and that ambiguity is the whole finding.
//
// A COSINE ALSO CARRIES NO SIGN. The magnitude convention below was built around that, and it is
// what made the series fold through zero at the square-up instead of crossing it — which quietly
// destroyed every derivative taken across impact (hip_stall fired on 7 of 7 shots for that reason
// alone, off |x|' = sign(x)·x').
//
// ROTATION ABOUT THE BODY'S VERTICAL AXIS NEEDS A ROUTE THAT READS GEOMETRY, not one that infers
// it from foreshortening: a bound segment IMU, or the hip/shoulder line's bearing triangulated
// from a calibrated pair. Both are declared in the catalogue. A single camera is not one of them,
// and a metric that cannot say which way the body turned was never going to be rescued by a better
// corridor.

// ── The IMU tier ───────────────────────────────────────────────────────────────────────────────
//
// The segment's medio-lateral axis is anatomical +X (imu_frame_contract.md §5: every segment shares
// the solveSegment construction, e_x = the flexion / medio-lateral axis). Carried into world by
// q_anat and projected into the horizontal plane — world is Z-up, so the horizontal plane is world
// XY — its direction angle IS the segment's axial orientation. Referenced to its own address
// direction and reported as a magnitude, the same convention the camera tier uses, so the two
// tiers are interchangeable to every reader.
void fillFromImu(RotationChannel &out, const SegmentStream &seg, const std::vector<int64_t> &timeGrid,
                 int64_t addressUs)
{
    if (seg.qAnat.size() != timeGrid.size() || timeGrid.empty())
        return;

    const auto axisAngle = [&](size_t i) {
        const QVector3D ml = seg.qAnat[i].rotatedVector(QVector3D(1.0f, 0.0f, 0.0f));
        return std::atan2(double(ml.y()), double(ml.x()));
    };

    // The address reference index: nearest grid sample to the Address instant, or the first sample
    // when the ladder never found one.
    size_t addrIdx = 0;
    if (addressUs >= 0)
        addrIdx = size_t(nearestIndex(timeGrid, addressUs));
    const double a0 = axisAngle(addrIdx);

    for (size_t i = 0; i < timeGrid.size(); ++i)
        out.turn.push(timeGrid[i], std::abs(wrapPi(axisAngle(i) - a0)) * kRadToDeg);

    if (!out.turn.empty())
        out.tier = RotationTier::Imu;   // sigma left unset: no error budget is propagated here
}

} // namespace

BodyRotationResult trackBodyRotation(const PoseTrack2D &pose, const FusedStreams &streams,
                                     int frameW, int frameH, bool leadIsLeft,
                                     const std::vector<PhaseEvent> &phases,
                                     const BodyRotationConfig &cfg)
{
    Q_UNUSED(leadIsLeft)   // the magnitude convention is handedness-free by construction

    BodyRotationResult res;

    const std::vector<PoseFrame2D> &frames = pose.smoothed.empty() ? pose.frames : pose.smoothed;
    for (const PoseFrame2D &f : frames)
        res.grid.push_back(f.t_us);

    const int64_t fallbackUs = res.grid.empty() ? -1 : res.grid.front();
    const int64_t addressUs  = phaseTime(phases, Phase::Address, fallbackUs);

    // ── Address spans, robustly ────────────────────────────────────────────────────────────────
    // ── Per segment: the IMU if it is bound, the camera if it is not ───────────────────────────
    // Resolved INDEPENDENTLY per segment. A swing with a pelvis IMU and no thorax IMU gets a
    // measured pelvis and an estimated chest, which is the right answer — refusing the pair because
    // half of it could be better measured would throw away the half that could not.
    // A BOUND IMU OR NOTHING. There is no camera fallback any more: a channel with no stream is
    // left absent, and every measure over it reports "metric not produced on this capture" —
    // which is the truthful answer for a single face-on view and was not the one being given.
    if (const SegmentStream *s = streams.streamFor(SegmentRole::Pelvis))
        fillFromImu(res.pelvis, *s, streams.timeGrid, addressUs);
    if (const SegmentStream *s = streams.streamFor(SegmentRole::Thorax))
        fillFromImu(res.thorax, *s, streams.timeGrid, addressUs);

    // The IMU tier writes onto the stream time grid, which is not the pose grid. Adopt whichever
    // grid actually carries samples so the resample target is never empty.
    if (res.grid.empty()) {
        if (!res.pelvis.turn.empty())      res.grid = res.pelvis.turn.t_us;
        else if (!res.thorax.turn.empty()) res.grid = res.thorax.turn.t_us;
    }

    // ── X-factor, and the stretch ──────────────────────────────────────────────────────────────
    // Both segments are needed: separation is a relationship, and one half of it is not a partial
    // answer, it is a different quantity. Evaluated on the shared grid through the same interpolator
    // the series resample uses, so the difference is taken between values at the SAME instant even
    // when the two tiers disagree about the sampling rate.
    if (!res.pelvis.turn.empty() && !res.thorax.turn.empty() && !res.grid.empty()) {
        for (int64_t t : res.grid) {
            const double p = interpChannel(res.pelvis.turn.t_us, res.pelvis.turn.value, t);
            const double x = interpChannel(res.thorax.turn.t_us, res.thorax.turn.value, t);
            res.xFactor.push(t, x - p);
        }

        // The stretch is measured FROM THE TOP, so it needs a segmented Top. Without one the
        // channel is absent rather than anchored to something arbitrary — a stretch referenced to
        // the wrong instant is a plausible number about nothing.
        const int64_t topUs = phaseTime(phases, Phase::Top, -1);
        if (topUs >= 0) {
            const double atTop = interpChannel(res.xFactor.t_us, res.xFactor.value, topUs);
            for (size_t i = 0; i < res.xFactor.t_us.size(); ++i)
                res.xFactorStretch.push(res.xFactor.t_us[i], res.xFactor.value[i] - atTop);
        }
    }

    res.valid = !res.pelvis.turn.empty() || !res.thorax.turn.empty();
    return res;
}

std::vector<MetricSeries> buildBodyRotationSeries(const BodyRotationResult &res,
                                                  const std::vector<PhaseEvent> &phases)
{
    std::vector<MetricSeries> out;
    if (!res.valid || res.grid.empty())
        return out;

    const QString deg = QStringLiteral("°");

    const auto emitRotation = [&](const RotationChannel &ch, const char *key, const char *label) {
        MetricSeries m = buildChannelSeries(res.grid, ch.turn, QString::fromLatin1(key),
                                            QString::fromUtf8(label), deg, phases);
        if (m.key.isEmpty())
            return;
        // Carry the propagated uncertainty ONLY where one was actually computed. The field's
        // contract is explicit that absent means "not characterised" rather than "zero error", so
        // the IMU tier — which has no error budget through this path — leaves it unset rather than
        // claiming a perfect measurement.
        if (ch.sigmaDeg > 0.0)
            m.sigma = ch.sigmaDeg;
        out.push_back(std::move(m));
    };

    emitRotation(res.pelvis, "pelvisRotation", "Pelvis rotation");
    emitRotation(res.thorax, "thoraxRotation", "Thorax rotation");

    appendIfProduced(out, buildChannelSeries(res.grid, res.xFactor, QStringLiteral("xFactor"),
                                             QStringLiteral("X-factor"), deg, phases));
    appendIfProduced(out, buildChannelSeries(res.grid, res.xFactorStretch,
                                             QStringLiteral("xFactorStretch"),
                                             QStringLiteral("X-factor stretch"), deg, phases));
    return out;
}

} // namespace pinpoint::analysis
