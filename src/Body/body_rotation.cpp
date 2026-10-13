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
#include <limits>

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

// ── The triangulated tier ──────────────────────────────────────────────────────────────────────
//
// The reference: the circular median of the usable bearings within cfg.addrWindowUs of Address,
// read only if the nearest usable frame sits within cfg.triAddrMaxGapUs of it. Its σ is the median
// frame σ over √n, × 1.2533 for a median. Returns false when there is no honest reference.
bool triReference(const std::vector<int64_t> &t, const std::vector<double> &b, const std::vector<double> &s,
                  int64_t addressUs, const BodyRotationConfig &cfg, double &ref, double &refSigma)
{
    std::vector<size_t> idx;
    int64_t nearest = std::numeric_limits<int64_t>::max();
    for (size_t i = 0; i < t.size(); ++i) {
        if (!std::isfinite(b[i])) continue;
        const int64_t d = std::llabs(t[i] - addressUs);
        nearest = std::min(nearest, d);
        if (d <= cfg.addrWindowUs) idx.push_back(i);
    }
    if (idx.empty() || nearest > cfg.triAddrMaxGapUs) return false;
    // Circular: unwrap every bearing against the first before taking the median.
    std::vector<double> v, sg;
    for (size_t i : idx) { v.push_back(b[idx[0]] + wrapPi(b[i] - b[idx[0]])); sg.push_back(s[i]); }
    ref = medianOfCopy(v);
    refSigma = 1.2533 * medianOfCopy(sg) / std::sqrt(double(v.size()));
    return true;
}

// One segment's triangulated turn: |Δ| into `mag`, leadSign·Δ into `sgn` (when given), each with its
// per-sample σ. Frames with no usable bearing are skipped; the series builder masks the gaps.
bool fillTriangulated(RotationChannel &mag, RotationChannel *sgn, const TriangulatedTurnInput &in,
                      const std::vector<double> &b, const std::vector<double> &s, int64_t addressUs,
                      double leadSign, const BodyRotationConfig &cfg)
{
    double ref = 0.0, refSigma = 0.0;
    if (!triReference(in.t_us, b, s, addressUs, cfg, ref, refSigma)) return false;
    const double frac = in.camerasCalibrated ? tuned::bodyRotation::kTriScaleFracCalibrated : cfg.triScaleFrac;
    std::vector<double> sig;
    for (size_t i = 0; i < in.t_us.size(); ++i) {
        if (!std::isfinite(b[i]) || !std::isfinite(s[i])) continue;
        const double d   = wrapPi(b[i] - ref);
        const double sd  = std::sqrt(s[i] * s[i] + refSigma * refSigma + (frac * d) * (frac * d)) * kRadToDeg;
        mag.turn.push(in.t_us[i], std::abs(d) * kRadToDeg);
        mag.sigmaPerSample.push_back(sd);
        if (sgn) { sgn->turn.push(in.t_us[i], leadSign * d * kRadToDeg); sgn->sigmaPerSample.push_back(sd); }
        sig.push_back(sd);
    }
    if (mag.turn.empty()) return false;
    mag.tier     = RotationTier::Triangulated;
    mag.sigmaDeg = medianOfCopy(sig);
    if (sgn) { sgn->tier = RotationTier::Triangulated; sgn->sigmaDeg = mag.sigmaDeg; }
    return true;
}

} // namespace

BodyRotationResult trackBodyRotationTriangulated(const TriangulatedTurnInput &in, bool leadIsLeft,
                                                 const std::vector<PhaseEvent> &phases,
                                                 bool wantPelvis, bool wantThorax,
                                                 const BodyRotationConfig &cfg)
{
    BodyRotationResult res;
    const size_t n = in.t_us.size();
    if (!cfg.triangulated || n < 2 || in.pelvisBearing.size() != n || in.thoraxBearing.size() != n
        || in.pelvisSigma.size() != n || in.thoraxSigma.size() != n)
        return res;
    res.grid = in.t_us;
    const int64_t addressUs = phaseTime(phases, Phase::Address, in.t_us.front());
    // LEAD-RELATIVE (sign conventions doc): the bearing of left → right turns one way for a golfer
    // whose lead side is the left and the other way for one whose lead side is the right, while
    // describing the same movement. Checked on right-handed captures (2026-07-04: the pelvis reads
    // negative at the top, positive after impact); no left-handed capture exists to check the mirror.
    const double leadSign = leadIsLeft ? 1.0 : -1.0;

    if (wantPelvis)
        fillTriangulated(res.pelvis, &res.pelvisSigned, in, in.pelvisBearing, in.pelvisSigma, addressUs, leadSign, cfg);
    if (wantThorax)
        fillTriangulated(res.thorax, nullptr, in, in.thoraxBearing, in.thoraxSigma, addressUs, leadSign, cfg);

    // A gap is three frame periods with no usable bearing (the series builder masks it).
    {
        std::vector<int64_t> dts;
        for (size_t i = 1; i < n; ++i) if (in.t_us[i] > in.t_us[i - 1]) dts.push_back(in.t_us[i] - in.t_us[i - 1]);
        std::sort(dts.begin(), dts.end());
        res.triGapUs = dts.empty() ? -1 : 3 * dts[dts.size() / 2];
    }

    // X-factor from THIS route only, both halves (see the header).
    if (!res.pelvis.turn.empty() && !res.thorax.turn.empty()) {
        for (int64_t t : res.grid) {
            const double p = interpChannel(res.pelvis.turn.t_us, res.pelvis.turn.value, t);
            const double x = interpChannel(res.thorax.turn.t_us, res.thorax.turn.value, t);
            res.xFactor.push(t, x - p);
        }
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
        // The triangulated route masks its gaps — a frame whose bearing was unusable is bridged,
        // never graded. The IMU route keeps its unmasked behaviour.
        const bool tri = ch.tier == RotationTier::Triangulated;
        MetricSeries m = buildChannelSeries(res.grid, ch.turn, QString::fromLatin1(key),
                                            QString::fromUtf8(label), deg, phases,
                                            defaultPhaseSamples(), tri ? res.triGapUs : -1);
        if (m.key.isEmpty())
            return;
        // Carry the propagated uncertainty ONLY where one was actually computed. The field's
        // contract is explicit that absent means "not characterised" rather than "zero error", so
        // the IMU tier — which has no error budget through this path — leaves it unset rather than
        // claiming a perfect measurement.
        if (ch.sigmaDeg > 0.0)
            m.sigma = ch.sigmaDeg;
        // Triangulated: each phase reading carries ITS OWN σ (session_diagnostics_design.md §A8.3
        // reads it), tagged propagated — an assumed camera, no truth to calibrate it against.
        if (tri && ch.sigmaPerSample.size() == ch.turn.size()) {
            m.sigmaKind = uint8_t(SigmaKind::Propagated);
            for (PhaseSample &ps : m.phaseSamples) {
                ps.sigma     = interpChannel(ch.turn.t_us, ch.sigmaPerSample, ps.t_us);
                ps.sigmaKind = uint8_t(SigmaKind::Propagated);
            }
        }
        out.push_back(std::move(m));
    };

    emitRotation(res.pelvis, "pelvisRotation", "Pelvis rotation");
    emitRotation(res.thorax, "thoraxRotation", "Thorax rotation");
    emitRotation(res.pelvisSigned, "pelvisRotationSigned", "Pelvis rotation (signed)");

    // From the triangulated route both halves carry σ, so the separation carries both in quadrature
    // (a difference of two turns is no better known than either); the IMU route stays as it was.
    const bool triPair = res.pelvis.tier == RotationTier::Triangulated && res.thorax.tier == RotationTier::Triangulated
                      && res.pelvis.sigmaPerSample.size() == res.pelvis.turn.size()
                      && res.thorax.sigmaPerSample.size() == res.thorax.turn.size();
    const auto emitSeparation = [&](const MetricChannel &ch, const char *key, const char *label) {
        MetricSeries m = buildChannelSeries(res.grid, ch, QString::fromLatin1(key), QString::fromUtf8(label),
                                            deg, phases, defaultPhaseSamples(), triPair ? res.triGapUs : -1);
        if (m.key.isEmpty()) return;
        if (triPair) {
            m.sigma     = std::hypot(res.pelvis.sigmaDeg, res.thorax.sigmaDeg);
            m.sigmaKind = uint8_t(SigmaKind::Propagated);
            for (PhaseSample &ps : m.phaseSamples) {
                ps.sigma = std::hypot(interpChannel(res.pelvis.turn.t_us, res.pelvis.sigmaPerSample, ps.t_us),
                                      interpChannel(res.thorax.turn.t_us, res.thorax.sigmaPerSample, ps.t_us));
                ps.sigmaKind = uint8_t(SigmaKind::Propagated);
            }
        }
        out.push_back(std::move(m));
    };
    emitSeparation(res.xFactor, "xFactor", "X-factor");
    emitSeparation(res.xFactorStretch, "xFactorStretch", "X-factor stretch");
    return out;
}

} // namespace pinpoint::analysis
