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

#include "../IMU/trunk_angles.h"   // D13: the turn is the ML axis heading from address

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


// Wrap an angle difference into (−π, π]. Used by the triangulated tier, where a bearing is a true
// angle and can cross the branch cut (the IMU tier wraps through trunk_angles::turnFromRad).
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
// The segment's medio-lateral axis is anatomical +X (imu_frame_contract.md §5 "Trunk segments":
// solveTrunkSegment keeps a gravity-aligned, Z-up, stance-headed world, segment +X the golfer's
// right). Carried into world by q_anat and projected into the horizontal plane — world XY — its
// heading IS the segment's axial orientation, and the change of that heading from its own address
// value is the turn: trunk design D13's projection, trunk_angles::turnFromRad, so this module and
// every other trunk reader share one definition.
//
// Two readings from the one difference d (rad, positive about +Z):
//   `mag`  |d| — the magnitude convention the corridors and the triangulated tier use.
//   `sgn`  leadSign·d — LEAD-RELATIVE, + open (pinpoint_sign_conventions.md "Trunk IMU readings"):
//          the trunk frame is the same for both hands, so +Z is opening for a right-hander and
//          closing for a left-hander, and the mirror is the producer's (design D15). This is the
//          series a rate through impact can be taken from (hip_stall): it crosses zero at the
//          square-up where |d| folds.
// No σ: the IMU error budget (design §8.2) is not built, so the channel leaves it unset rather than
// claiming a perfect measurement.
void fillFromImu(RotationChannel &mag, RotationChannel &sgn, const SegmentStream &seg,
                 const std::vector<int64_t> &timeGrid, int64_t addressUs, double leadSign)
{
    if (seg.qAnat.size() != timeGrid.size() || timeGrid.empty())
        return;

    // The address reference index: nearest grid sample to the Address instant, or the first sample
    // when the ladder never found one.
    size_t addrIdx = 0;
    if (addressUs >= 0)
        addrIdx = size_t(nearestIndex(timeGrid, addressUs));
    const QQuaternion &qAddr = seg.qAnat[addrIdx];

    for (size_t i = 0; i < timeGrid.size(); ++i) {
        const double d = trunk_angles::turnFromRad(seg.qAnat[i], qAddr) * kRadToDeg;
        mag.turn.push(timeGrid[i], std::abs(d));
        sgn.turn.push(timeGrid[i], leadSign * d);
    }

    if (!mag.turn.empty()) {
        mag.tier = RotationTier::Imu;
        sgn.tier = RotationTier::Imu;
    }
}

// ── The face-in cue (trunk design §6.3, W2.1d) ─────────────────────────────────────────────────
//
// A sensor worn face-in (label against the body, USB still up) is the right mount turned 180° about
// the spine. Gravity cannot see that and neither can the ceremony's turns (an upright turn is about
// the same axis), so stand tall solves it as valid. What it changes: the sagittal and lateral
// readings flip sign — a 40° address bend reads −40° — while the turn, its signed twin and the S-I
// rate still read correctly. So the check is the SIGN of the address forward bend (D13).
//
// EVERY SWING CHECKS ITS OWN ADDRESS. The design names the session's first address; a per-swing
// analysis cannot see the session, and checking each swing is strictly more coverage: a sensor
// re-seated wrong mid-session is caught on the next swing, and one put right stops warning.
//
// The bend is read at the stream sample nearest the ladder's Address, and only when there is one
// within addrWindowUs: without an Address the IMU tier's reference falls back to the first sample,
// which may be anywhere (stand tall, the walk in), and a bend read there says nothing about a mount.
double addressBendDeg(const SegmentStream &seg, const std::vector<int64_t> &timeGrid,
                      const std::vector<PhaseEvent> &phases, const BodyRotationConfig &cfg)
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const std::optional<int64_t> addr = phaseTimeOpt(phases, Phase::Address);
    if (!addr || seg.qAnat.size() != timeGrid.size() || timeGrid.empty())
        return nan;
    const size_t i = size_t(nearestIndex(timeGrid, *addr));
    if (std::llabs(timeGrid[i] - *addr) > cfg.addrWindowUs)
        return nan;
    return trunk_angles::forwardBendRad(seg.qAnat[i]) * kRadToDeg;
}

// One record per face-in sensor, DOWNGRADED — never refused, never withheld (Mark: every reading is
// shown with its ⚠). The subject is that segment's turn; it covers every reading this route made
// from the same sensor: the signed twin and, when both halves were read, the X-factor and stretch.
// They read correctly under a true face-in (a rotation about the spine), but the sensor is not on the
// mount the ceremony calibrated, so they drop from Measured to Estimated and carry the cue's words.
//
// NOT COVERED: the KS node (ks.pelvis / ks.thorax) and the angular-speed series. The S-I rate is
// gyro_segment · ẑ (D3), and a rotation about ẑ leaves it EXACTLY unchanged (trunk_e2e_test §f, to
// 0.01 °/s); the node carries its own route quality and gates, and a downgrade there would demote a
// Direct node and let §10.7 refuse ratios on a reading that is right. The readings face-in really
// flips — forward bend and obliquity — have no IMU producer yet (spineForwardBend's trunkImus route
// is PLANNED); when one lands it joins `covers` here.
void recordFaceIn(BodyRotationResult &res, const BodyRotationConfig &cfg)
{
    const auto one = [&](bool pelvis) {
        const double bend = pelvis ? res.pelvisAddressBendDeg : res.thoraxAddressBendDeg;
        if (!std::isfinite(bend) || bend >= cfg.faceInBendDeg)
            return;
        QStringList covers;
        const RotationChannel &sgn = pelvis ? res.pelvisSigned : res.thoraxSigned;
        if (!sgn.turn.empty())
            covers << (pelvis ? QStringLiteral("pelvisRotationSigned") : QStringLiteral("thoraxRotationSigned"));
        if (!res.xFactor.empty())        covers << QStringLiteral("xFactor");
        if (!res.xFactorStretch.empty()) covers << QStringLiteral("xFactorStretch");
        res.gates.push_back(GateRecord{ pelvis ? PP_GATE("body.pelvisFaceIn") : PP_GATE("body.thoraxFaceIn"),
                                        pelvis ? QStringLiteral("pelvisRotation") : QStringLiteral("thoraxRotation"),
                                        GateEffect::Downgraded,
                                        QVariantMap{ { QStringLiteral("bendDeg"), bend },
                                                     { QStringLiteral("limitDeg"), cfg.faceInBendDeg } },
                                        {}, {}, covers });
    };
    one(true);
    one(false);
}

// ── The triangulated tier ──────────────────────────────────────────────────────────────────────
//
// The reference: the circular median of the usable bearings within cfg.addrWindowUs of Address,
// read only if the nearest usable frame sits within cfg.triAddrMaxGapUs of it. Its σ is the median
// frame σ over √n, × 1.2533 for a median. Returns false when there is no honest reference.
bool triReference(const std::vector<int64_t> &t, const std::vector<double> &b, const std::vector<double> &s,
                  int64_t addressUs, const BodyRotationConfig &cfg, double &ref, double &refSigma,
                  int64_t &nearest)
{
    std::vector<size_t> idx;
    nearest = std::numeric_limits<int64_t>::max();
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
// On a refusal `why` gets the body.rotation code and its evidence (trust stage 6).
bool fillTriangulated(RotationChannel &mag, RotationChannel *sgn, const TriangulatedTurnInput &in,
                      const std::vector<double> &b, const std::vector<double> &s, int64_t addressUs,
                      double leadSign, const BodyRotationConfig &cfg, QVariantMap &why)
{
    double ref = 0.0, refSigma = 0.0;
    int64_t nearest = std::numeric_limits<int64_t>::max();
    if (!triReference(in.t_us, b, s, addressUs, cfg, ref, refSigma, nearest)) {
        if (nearest == std::numeric_limits<int64_t>::max())
            why = QVariantMap{ { QStringLiteral("code"), QStringLiteral("noFrames") } };
        else if (nearest > cfg.triAddrMaxGapUs)
            // THE gate the census names: the reference must be read within triAddrMaxGapUs of Address.
            why = QVariantMap{ { QStringLiteral("code"), QStringLiteral("addressGap") },
                    { QStringLiteral("nearestMs"), double(nearest) / 1000.0 },
                    { QStringLiteral("maxGapMs"), double(cfg.triAddrMaxGapUs) / 1000.0 } };
        else
            why = QVariantMap{ { QStringLiteral("code"), QStringLiteral("addressWindow") },
                    { QStringLiteral("windowMs"), double(cfg.addrWindowUs) / 1000.0 } };
        return false;
    }
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
    if (mag.turn.empty()) {
        why = QVariantMap{ { QStringLiteral("code"), QStringLiteral("noFrames") } };
        return false;
    }
    mag.tier     = RotationTier::Triangulated;
    mag.sigmaDeg = medianOfCopy(sig);
    if (sgn) { sgn->tier = RotationTier::Triangulated; sgn->sigmaDeg = mag.sigmaDeg; }
    return true;
}

// The readings a trunk segment's refusal takes down with it (trust stage 6: one cascade record):
// the segment's signed twin always — both routes fill it beside the magnitude — and the separation,
// which needs both halves, on the first refused segment's record.
QStringList rotationCovers(bool pelvis, bool separationStillOpen)
{
    QStringList c;
    c << (pelvis ? QStringLiteral("pelvisRotationSigned") : QStringLiteral("thoraxRotationSigned"));
    if (separationStillOpen) c << QStringLiteral("xFactor") << QStringLiteral("xFactorStretch");
    return c;
}

// The stretch is measured FROM THE TOP: both halves present and no segmented Top is a refusal.
void recordStretchWithoutTop(BodyRotationResult &res, bool topFound)
{
    if (!res.xFactor.empty() && !topFound)
        res.gates.push_back(GateRecord{ PP_GATE("body.xFactorStretchTop"), QStringLiteral("xFactorStretch"),
                                        GateEffect::Refused, {}, {}, {}, {} });
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
        || in.pelvisSigma.size() != n || in.thoraxSigma.size() != n) {
        // The two-camera fit handed over too little to read a bearing from (the switch itself is
        // checked by the stage before it runs, so `routeOff` here is a direct caller's).
        const QVariantMap ev{ { QStringLiteral("code"), cfg.triangulated ? QStringLiteral("input") : QStringLiteral("routeOff") },
                              { QStringLiteral("frames"), int(n) } };
        GateLog gates;
        if (wantPelvis)
            gates.fireCovering(PP_GATE("body.rotation"), QStringLiteral("pelvisRotation"), GateEffect::Refused,
                               rotationCovers(true, true), ev);
        if (wantThorax)
            gates.fireCovering(PP_GATE("body.rotation"), QStringLiteral("thoraxRotation"), GateEffect::Refused,
                               rotationCovers(false, !wantPelvis), ev);
        res.gates = gates.take();
        return res;
    }
    res.grid = in.t_us;
    const int64_t addressUs = phaseTime(phases, Phase::Address, in.t_us.front());
    // LEAD-RELATIVE (sign conventions doc): the bearing of left → right turns one way for a golfer
    // whose lead side is the left and the other way for one whose lead side is the right, while
    // describing the same movement. Checked on right-handed captures (2026-07-04: the pelvis reads
    // negative at the top, positive after impact); no left-handed capture exists to check the mirror.
    const double leadSign = leadIsLeft ? 1.0 : -1.0;

    GateLog gates;
    bool pelvisRefused = false;
    if (wantPelvis) {
        QVariantMap why;
        if (!fillTriangulated(res.pelvis, &res.pelvisSigned, in, in.pelvisBearing, in.pelvisSigma, addressUs,
                              leadSign, cfg, why)) {
            pelvisRefused = true;
            gates.fireCovering(PP_GATE("body.rotation"), QStringLiteral("pelvisRotation"), GateEffect::Refused,
                               rotationCovers(true, true), why);
        }
    }
    if (wantThorax) {
        QVariantMap why;
        if (!fillTriangulated(res.thorax, &res.thoraxSigned, in, in.thoraxBearing, in.thoraxSigma, addressUs,
                              leadSign, cfg, why))
            gates.fireCovering(PP_GATE("body.rotation"), QStringLiteral("thoraxRotation"), GateEffect::Refused,
                               rotationCovers(false, !pelvisRefused), why);
    }
    res.gates = gates.take();

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
        recordStretchWithoutTop(res, topUs >= 0);
    }
    res.valid = !res.pelvis.turn.empty() || !res.thorax.turn.empty();
    return res;
}

BodyRotationResult trackBodyRotation(const PoseTrack2D &pose, const FusedStreams &streams,
                                     int frameW, int frameH, bool leadIsLeft,
                                     const std::vector<PhaseEvent> &phases,
                                     const BodyRotationConfig &cfg)
{
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
    //
    // The magnitude is handedness-free by construction; the signed twin is lead-relative, and the
    // mirror is made here (design D15) — the same leadSign the two-camera route uses.
    const double leadSign = leadIsLeft ? 1.0 : -1.0;
    if (const SegmentStream *s = streams.streamFor(SegmentRole::Pelvis)) {
        fillFromImu(res.pelvis, res.pelvisSigned, *s, streams.timeGrid, addressUs, leadSign);
        res.pelvisAddressBendDeg = addressBendDeg(*s, streams.timeGrid, phases, cfg);
    }
    if (const SegmentStream *s = streams.streamFor(SegmentRole::Thorax)) {
        fillFromImu(res.thorax, res.thoraxSigned, *s, streams.timeGrid, addressUs, leadSign);
        res.thoraxAddressBendDeg = addressBendDeg(*s, streams.timeGrid, phases, cfg);
    }

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
        recordStretchWithoutTop(res, topUs >= 0);
    }

    // After the separation, so a face-in record knows whether it has an X-factor to cover.
    recordFaceIn(res, cfg);

    res.valid = !res.pelvis.turn.empty() || !res.thorax.turn.empty();
    return res;
}

std::vector<GateRecord> bodyRotationGates(const BodyRotationResult &res, const FusedStreams &streams,
                                          bool dtlCamera, bool triangulatedOn)
{
    GateLog gates;
    gates.append(res.gates);
    // The two-camera route fills what the IMU left empty; only where it cannot is this final.
    if (dtlCamera && triangulatedOn)
        return gates.take();
    const QString code = !dtlCamera ? QStringLiteral("noSensor") : QStringLiteral("routeOff");
    const auto evFor = [&](SegmentRole role) {
        // A bound stream that produced nothing (its samples off the swing's time grid) says so.
        return QVariantMap{ { QStringLiteral("code"),
                              streams.streamFor(role) ? QStringLiteral("sensorUnusable") : code } };
    };
    const bool pelvisRefused = res.pelvis.turn.empty();
    if (pelvisRefused)
        gates.fireCovering(PP_GATE("body.rotation"), QStringLiteral("pelvisRotation"), GateEffect::Refused,
                           rotationCovers(true, true), evFor(SegmentRole::Pelvis));
    if (res.thorax.turn.empty())
        gates.fireCovering(PP_GATE("body.rotation"), QStringLiteral("thoraxRotation"), GateEffect::Refused,
                           rotationCovers(false, !pelvisRefused), evFor(SegmentRole::Thorax));
    return gates.take();
}

std::vector<MetricSeries> buildBodyRotationSeries(const BodyRotationResult &res,
                                                  const std::vector<PhaseEvent> &phases)
{
    std::vector<MetricSeries> out;
    if (!res.valid || res.grid.empty())
        return out;

    const QString deg = QStringLiteral("°");
    // A PHASE THE FRAMES DO NOT REACH HAS NO READING. The triangulated grid is the skeleton's own
    // frames, which since skeleton3d v16 stop at P8 (+25 ms): the Finish would otherwise snap to
    // the last frame and be labelled the Finish 100+ ms early. A sample further from its phase than
    // the route's gap budget is dropped.
    const auto dropFarPhases = [&](MetricSeries &m) {
        if (res.triGapUs < 0) return;
        m.phaseSamples.erase(std::remove_if(m.phaseSamples.begin(), m.phaseSamples.end(), [&](const PhaseSample &ps) {
            const std::optional<int64_t> t = phaseTimeOpt(phases, ps.phase);
            return t && std::llabs(*t - ps.t_us) > res.triGapUs;
        }), m.phaseSamples.end());
    };

    const auto emitRotation = [&](const RotationChannel &ch, const char *key, const char *label) {
        // The triangulated route masks its gaps — a frame whose bearing was unusable is bridged,
        // never graded. The IMU route keeps its unmasked behaviour.
        const bool tri = ch.tier == RotationTier::Triangulated;
        MetricSeries m = buildChannelSeries(res.grid, ch.turn, QString::fromLatin1(key),
                                            QString::fromUtf8(label), deg, phases,
                                            defaultPhaseSamples(), tri ? res.triGapUs : -1);
        if (m.key.isEmpty())
            return;
        if (tri) dropFarPhases(m);
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
    emitRotation(res.thoraxSigned, "thoraxRotationSigned", "Thorax rotation (signed)");

    // From the triangulated route both halves carry σ, so the separation carries both in quadrature
    // (a difference of two turns is no better known than either); the IMU route stays as it was.
    const bool triPair = res.pelvis.tier == RotationTier::Triangulated && res.thorax.tier == RotationTier::Triangulated
                      && res.pelvis.sigmaPerSample.size() == res.pelvis.turn.size()
                      && res.thorax.sigmaPerSample.size() == res.thorax.turn.size();
    const auto emitSeparation = [&](const MetricChannel &ch, const char *key, const char *label) {
        MetricSeries m = buildChannelSeries(res.grid, ch, QString::fromLatin1(key), QString::fromUtf8(label),
                                            deg, phases, defaultPhaseSamples(), triPair ? res.triGapUs : -1);
        if (m.key.isEmpty()) return;
        if (triPair) dropFarPhases(m);
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
