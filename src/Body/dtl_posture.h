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

// Down-the-line POSTURE metrics — docs/design/dtl_posture_design.md.
//
// THE RULE THAT DECIDES WHAT IS IN HERE is lower_body_metrics.h's, turned through ninety
// degrees: a down-the-line camera resolves the SAGITTAL plane — image x is toward-and-away
// from the ball, image y is up — and nothing along the target line. Every channel below is a
// sagittal displacement or a sagittal angle, which is exactly the set lower_body_metrics.h
// refuses to produce from face-on ("would put a confident number on a quantity the camera
// cannot see").
//
//   pelvisThrust      cm   hip-MIDPOINT travel toward the ball, from address. The midpoint is
//                          what makes it a thrust and not a turn: rotation about the vertical
//                          carries one hip toward the ball and the other away by the same amount.
//   headRise          cm   EAR-MIDPOINT travel upward, from address. Standing up out of the posture
//                          read as what it does to the head's height (posture_metrics_design.md §4.5).
//                          The ears, both of them, and nothing else: they sit near the axis the skull
//                          turns about, so their midpoint holds its height while the head rotates. The
//                          nose, the eyes or the near ear alone climb 5–10 cm at the top on a head that
//                          has not risen (Nick Henn 20 Oct: near ear +6.3, nose +4.9, ear midpoint +1.9
//                          against face-on −0.8 and 3-D +1.0).
//   spineForwardBend  °    hip-midpoint → shoulder-midpoint against the vertical, + over the ball.
//   leadKneeFlexion   °    180° − (hip, knee, ankle), by side. The trail leg is nearer the camera
//   trailKneeFlexion  °    and occludes the lead one through much of the swing; both are emitted
//                          and the confidence gate is what speaks for the lead knee.
//   ballBodyDistance  % shoulder width   ball to the toe line at address; the shoulder width is
//                          end-on here, so it is face-on's, carried across by the ratio of the
//                          body's vertical extent in the two views (segment_rates' pair scale).
//   balanceHeelToe    % foot length      a PROXY and it says so: a mass-weighted point
//                          (0.45 pelvis, 0.35 shoulders, 0.20 knees) over the heel→toe span. No
//                          pressure, no segment-mass model.
//   handPathLoop      % hand rise   where the lead wrist comes DOWN against where it went UP, at
//                          the same height, + toward the ball. The over-the-top move seen the way a
//                          coach sees it on a down-the-line hand trace: the loop at the top that
//                          sends the downswing out over the backswing path.
//   armHang           °    shoulder midpoint → wrist midpoint against the vertical, at ADDRESS only;
//                          + = the hands further toward the ball than the shoulders (reaching),
//                          − = tucked in under the body. "Let the arms hang" (posture_metrics_design.md §3.1).
//   headPitch         °    the ear → nose line against the square to the trunk line (hip midpoint →
//                          shoulder midpoint), at ADDRESS only; + = chin down toward the chest. Body
//                          keypoints 0 and 3/4 only — never the 68 face landmarks (WholeBody's fine
//                          keypoints score high and can still be wrong; see the hands).
//
// WHY THE HAND LOOP IS A RATIO AND NOT CENTIMETRES. Both paths are the same wrist in the same image,
// so the offset needs no calibration at all if it is scaled by something in that image — the
// hands' own rise from address to the top. That cancels the camera's distance, and it keeps the
// measure alive on the swings with no ruler (06-11: every one). Heights are matched in the image,
// so a camera above hand height tilts both paths the same way and the difference survives it.
// Read over 40–70 % of the rise: above that the path is turning over at the top, where every
// swing loops; below it the hands are converging on the ball, where every swing must meet.
//
// WHICH WAY IS THE BALL. Taken from the golfer's own feet — toes are ball-side of heels — never
// from a handedness setting or "image-right". The DTL ball, when found, must agree, and a
// disagreement refuses everything: a posture read with the sign backwards is early extension
// reported as its opposite.
//
// THE RULER. Centimetres need one. At the golfer's distance down the line the ball is a 42.67 mm
// ruler lying in the image, so the DTL tracker's measured ball radius is used when the ball was
// found by its BRIGHT cue (the shadow cue's radius is implied from a scale, so it is not a
// ruler). Otherwise the face-on ruler is carried across by the same vertical-extent ratio. No
// ruler ⇒ no pelvisThrust and no headRise: the unit never switches at runtime.
//
// WHAT IS NOT CLAIMED. The 2026 rigs put this camera behind the BALL and above hand height, not
// on the hands line. Address-referenced displacements and angle CHANGES are tolerant of that;
// the ABSOLUTE forward bend carries the camera's pitch as a bias. There is no truth for any of
// these yet: they were graded on repeatability and plausibility (design §4).
//
// Qt-only (no OpenCV/ORT), deterministic, unit-tested standalone.

#include <QPointF>
#include <QString>
#include <QVariantMap>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

#include "../Analysis/analysis_tuning.h"
#include "../Analysis/gate_record.h"   // GateRecord (trust stage 6)
#include "../Metrics/metric_channel.h"
#include "../Analysis/swing_analysis.h"

namespace pinpoint::analysis {

struct DtlPostureConfig {
    bool    enabled       = true;     // dtlPosture.enabled
    double  confMin       = 0.30;     // dtlPosture.confMin — a keypoint below this is not a keypoint
    int64_t addrWindowUs  = 150000;   // dtlPosture.addrWindowUs — half-window of the address reference
    int     addrMinFrames = 5;        // dtlPosture.addrMinFrames
    int64_t maxBridgeUs   = 60000;    // dtlPosture.maxBridgeUs — beyond this a resample is a bridge (< 0 = no mask)
    double  ballMm        = 42.67;
    double  shoulderMinCm = 30.0;     // dtlPosture.shoulderMinCm — a ruler must measure the golfer plausibly
    double  shoulderMaxCm = 50.0;     // dtlPosture.shoulderMaxCm
    double  loopFracLo    = 0.40;     // dtlPosture.loopFracLo — lowest height read, fraction of the hand rise
    double  loopFracHi    = 0.70;     // dtlPosture.loopFracHi — highest
    int     loopLevels    = 4;        // dtlPosture.loopLevels — heights read between them, inclusive
    int     loopMinLevels = 3;        // dtlPosture.loopMinLevels — heights that must resolve on both paths
    int64_t loopTopWindowUs = 80000;  // dtlPosture.loopTopWindowUs — the top of the hand path is sought ± this of Top
    double  loopMinRisePx = 40.0;     // dtlPosture.loopMinRisePx — a rise shorter than this is not a swing seen

    static DtlPostureConfig fromOverrides(const QVariantMap &ov)
    {
        using namespace tuning;
        DtlPostureConfig c;
        apply(ov, "dtlPosture.enabled",       c.enabled);
        apply(ov, "dtlPosture.confMin",       c.confMin);
        apply(ov, "dtlPosture.addrWindowUs",  c.addrWindowUs);
        apply(ov, "dtlPosture.addrMinFrames", c.addrMinFrames);
        apply(ov, "dtlPosture.maxBridgeUs",   c.maxBridgeUs);
        apply(ov, "dtlPosture.shoulderMinCm", c.shoulderMinCm);
        apply(ov, "dtlPosture.shoulderMaxCm", c.shoulderMaxCm);
        apply(ov, "dtlPosture.loopFracLo",    c.loopFracLo);
        apply(ov, "dtlPosture.loopFracHi",    c.loopFracHi);
        apply(ov, "dtlPosture.loopLevels",    c.loopLevels);
        apply(ov, "dtlPosture.loopMinLevels", c.loopMinLevels);
        apply(ov, "dtlPosture.loopTopWindowUs", c.loopTopWindowUs);
        apply(ov, "dtlPosture.loopMinRisePx", c.loopMinRisePx);
        return c;
    }
};

struct DtlPostureInputs {
    const PoseTrack2D *poseDtl = nullptr;   int dtlW = 0, dtlH = 0;
    const PoseTrack2D *poseFo  = nullptr;   int foW  = 0, foH  = 0;   // optional: shoulder width, fallback ruler
    const std::vector<PhaseEvent> *phases = nullptr;
    bool    leadIsLeft = true;
    // The DTL ball (DtlShaftTrack2D::ball). `ballBright` = found by the bright cue, so radiusPx
    // is a measurement.
    bool    ballFound = false, ballBright = false;
    double  ballX = 0, ballY = 0, ballRadiusPx = 0;
    double  foMmPerPx = 0;                   // face-on ball-diameter ruler; ≤ 0 = none
};

struct DtlPostureResult {
    bool    valid = false;
    QString reason;                  // why not, when not
    double  toward = 0;              // +1 = the ball is image-right, −1 = image-left
    double  cmPerPx = 0;             // 0 = no ruler
    QString ruler;                   // "dtlBall" | "faceOnBall" | "none"
    QString rulerRefused;            // why a ruler that resolved was dropped; empty otherwise
    double  scaleRatio = 0;          // DTL px per face-on px at the golfer (0 = not measured)
    double  shoulderWidthCm = 0, footLenCm = 0;
    std::vector<MetricSeries> series;
    // Trust stage 6: what the gates below left on the swing — the address refusal as ONE cascade
    // over every reading here, the ruler, the size floors and the hand loop each on its reading.
    // Kept on a refusal too (`valid` false): the stage hands them on either way.
    std::vector<GateRecord> gates;
};

namespace dtl_posture_detail {

constexpr int kNose = 0, kLEar = 3, kREar = 4, kLSh = 5, kRSh = 6, kLEl = 7, kREl = 8, kLWr = 9, kRWr = 10, kLHip = 11, kRHip = 12, kLKnee = 13, kRKnee = 14,
              kLAnk = 15, kRAnk = 16, kLBigToe = 17, kLHeel = 19, kRBigToe = 20, kRHeel = 22;

struct View {
    const std::vector<PoseFrame2D> *val = nullptr;    // smoothed when it is parallel to frames
    const std::vector<PoseFrame2D> *raw = nullptr;    // confidence always comes from here
    int w = 0, h = 0;
    size_t size() const { return raw->size(); }
    int64_t t(size_t i) const { return (*raw)[i].t_us; }
    bool ok(size_t i, int k, double confMin) const { return (*raw)[i].conf[size_t(k)] >= confMin; }
    QPointF px(size_t i, int k) const
    { const QPointF &p = (*val)[i].kp[size_t(k)]; return QPointF(p.x() * w, p.y() * h); }
};

inline View viewOf(const PoseTrack2D &p, int w, int h)
{
    View v;
    v.raw = &p.frames;
    v.val = (p.smoothed.size() == p.frames.size()) ? &p.smoothed : &p.frames;
    v.w = w; v.h = h;
    return v;
}

// Median of f over the frames within ±half of `tUs` where every keypoint in `ks` is confident.
template <typename F>
inline std::optional<double> medianAt(const View &v, int64_t tUs, int64_t half, double confMin,
                                      std::initializer_list<int> ks, int minN, F f)
{
    std::vector<double> xs;
    for (size_t i = 0; i < v.size(); ++i) {
        if (std::llabs(v.t(i) - tUs) > half) continue;
        bool ok = true;
        for (int k : ks) ok = ok && v.ok(i, k, confMin);
        if (ok) xs.push_back(f(i));
    }
    if (int(xs.size()) < minN) return std::nullopt;
    return medianOfCopy(xs);
}

inline QPointF mid(const QPointF &a, const QPointF &b) { return (a + b) * 0.5; }

// The midpoint of whichever of a left/right pair is confident on frame i — both, one, or neither.
// Down the line at address the two shoulders, the two wrists and the two ears each sit nearly on top
// of one another in the image (the pair lies along the camera's axis), so either one stands for the
// midpoint to within a few pixels, and a frame that lost one of the pair to occlusion is not lost.
inline std::optional<QPointF> seenMid(const View &v, size_t i, int a, int b, double confMin)
{
    const bool okA = v.ok(i, a, confMin), okB = v.ok(i, b, confMin);
    if (okA && okB) return mid(v.px(i, a), v.px(i, b));
    if (okA) return v.px(i, a);
    if (okB) return v.px(i, b);
    return std::nullopt;
}

// medianAt for a reading that decides its own admission: `f` returns nullopt for a frame it refuses.
template <typename F>
inline std::optional<double> medianOfSeen(const View &v, int64_t tUs, int64_t half, int minN, F f)
{
    std::vector<double> xs;
    for (size_t i = 0; i < v.size(); ++i) {
        if (std::llabs(v.t(i) - tUs) > half) continue;
        if (const std::optional<double> x = f(i)) xs.push_back(*x);
    }
    if (int(xs.size()) < minN) return std::nullopt;
    return medianOfCopy(xs);
}

inline double kneeFlexDeg(const QPointF &hip, const QPointF &knee, const QPointF &ank)
{
    const QPointF a = hip - knee, b = ank - knee;
    const double la = std::hypot(a.x(), a.y()), lb = std::hypot(b.x(), b.y());
    if (la < 1e-6 || lb < 1e-6) return 0.0;
    const double c = std::clamp((a.x() * b.x() + a.y() * b.y()) / (la * lb), -1.0, 1.0);
    return 180.0 - std::acos(c) * 180.0 / M_PI;
}

} // namespace dtl_posture_detail

inline DtlPostureResult buildDtlPosture(const DtlPostureInputs &in, const DtlPostureConfig &cfg)
{
    using namespace dtl_posture_detail;
    DtlPostureResult res;
    auto refuse = [&res](const QString &why) { res.reason = why; return res; };
    // A refusal of the whole address reference: one record on the first reading, the rest covered.
    auto refuseAll = [&res](const QString &why, const QVariantMap &ev) {
        res.reason = why;
        res.gates.push_back(GateRecord{ PP_GATE("posture.address"), QStringLiteral("pelvisThrust"), GateEffect::Refused,
                                        ev, {}, {},
                                        { QStringLiteral("headRise"),
                                          QStringLiteral("spineForwardBend"), QStringLiteral("leadKneeFlexion"),
                                          QStringLiteral("trailKneeFlexion"), QStringLiteral("trailForearmAngle"),
                                          QStringLiteral("balanceHeelToe"), QStringLiteral("ballBodyDistance"),
                                          QStringLiteral("handPathLoop"), QStringLiteral("armHang"),
                                          QStringLiteral("headPitch") } });
        return res;
    };
    auto code = [](const char *c) { return QVariantMap{ { QStringLiteral("code"), QString::fromLatin1(c) } }; };
    if (!cfg.enabled)                                   return refuse(QStringLiteral("disabled (dtlPosture.enabled)"));
    if (!in.poseDtl || in.poseDtl->frames.size() < 2 || in.dtlW <= 0 || in.dtlH <= 0 || !in.phases)
        return refuse(QStringLiteral("no down-the-line pose"));
    const std::optional<int64_t> addrT = phaseTimeOpt(*in.phases, Phase::Address);
    if (!addrT)                                         return refuse(QStringLiteral("no Address on the ladder"));

    const View v = viewOf(*in.poseDtl, in.dtlW, in.dtlH);
    const int64_t half = cfg.addrWindowUs;
    const int     minN = cfg.addrMinFrames;
    const double  cm   = cfg.confMin;

    // ── which way is the ball: toes are ball-side of heels ─────────────────────────────────
    const auto toeX  = medianAt(v, *addrT, half, cm, { kLBigToe, kRBigToe }, minN,
                                [&](size_t i) { return mid(v.px(i, kLBigToe), v.px(i, kRBigToe)).x(); });
    const auto heelX = medianAt(v, *addrT, half, cm, { kLHeel, kRHeel }, minN,
                                [&](size_t i) { return mid(v.px(i, kLHeel), v.px(i, kRHeel)).x(); });
    if (!toeX || !heelX)
        return refuseAll(QStringLiteral("feet not resolved at address — cannot tell which way the ball is"),
                         code("feet"));
    if (std::fabs(*toeX - *heelX) < 4.0) {
        QVariantMap ev = code("feetEndOn");
        ev.insert(QStringLiteral("toeHeelPx"), std::fabs(*toeX - *heelX));
        ev.insert(QStringLiteral("minPx"), 4.0);
        return refuseAll(QStringLiteral("feet not resolved at address — cannot tell which way the ball is"), ev);
    }
    res.toward = (*toeX > *heelX) ? 1.0 : -1.0;
    const double tw = res.toward;

    const auto hipAddrX = medianAt(v, *addrT, half, cm, { kLHip, kRHip }, minN,
                                   [&](size_t i) { return mid(v.px(i, kLHip), v.px(i, kRHip)).x(); });
    if (!hipAddrX) return refuseAll(QStringLiteral("hips not resolved at address"), code("hips"));
    if (in.ballFound && tw * (in.ballX - *hipAddrX) <= 0.0)
        return refuseAll(QStringLiteral("the ball and the feet disagree on which way the golfer faces"),
                         code("ballDisagrees"));

    // ── the two-view scale ratio and the ruler ─────────────────────────────────────────────
    auto extent = [&](const View &w, double confMin) {
        return medianAt(w, *addrT, half, confMin, { kNose, kLAnk, kRAnk }, minN, [&](size_t i) {
            return std::fabs(mid(w.px(i, kLAnk), w.px(i, kRAnk)).y() - w.px(i, kNose).y()); });
    };
    std::optional<double> foShoulderPx;
    if (in.poseFo && in.poseFo->frames.size() >= 2 && in.foW > 0 && in.foH > 0) {
        const View f = viewOf(*in.poseFo, in.foW, in.foH);
        const auto ed = extent(v, cm), ef = extent(f, cm);
        if (ed && ef && *ef > 1.0) res.scaleRatio = *ed / *ef;
        foShoulderPx = medianAt(f, *addrT, half, cm, { kLSh, kRSh }, minN, [&](size_t i) {
            const QPointF d = f.px(i, kLSh) - f.px(i, kRSh); return std::hypot(d.x(), d.y()); });
    }
    if (in.ballFound && in.ballBright && in.ballRadiusPx > 1.0) {
        res.cmPerPx = (cfg.ballMm / 10.0) / (2.0 * in.ballRadiusPx);
        res.ruler   = QStringLiteral("dtlBall");
    } else if (in.foMmPerPx > 0.0 && res.scaleRatio > 0.0) {
        res.cmPerPx = (in.foMmPerPx / 10.0) / res.scaleRatio;
        res.ruler   = QStringLiteral("faceOnBall");
    } else {
        res.ruler   = QStringLiteral("none");
    }
    // A RULER HAS TO MEASURE THE GOLFER TOO. Shoulder width is the one length both views agree a
    // human has, and an adult's is 30–50 cm. On 06-11 the face-on ruler carried across read 52–55 cm
    // of shoulders and 31 cm of foot on a golfer the DTL ball measures at 35 cm and 24 cm — forty
    // per cent long, which would have published a 9 cm thrust as 13. A ruler that fails this is
    // dropped, the thrust goes with it, and the angles are untouched.
    if (res.cmPerPx > 0.0 && foShoulderPx && res.scaleRatio > 0.0) {
        const double sw = *foShoulderPx * res.scaleRatio * res.cmPerPx;
        if (sw < cfg.shoulderMinCm || sw > cfg.shoulderMaxCm) {
            res.rulerRefused = QStringLiteral("%1 implied %2 cm of shoulders").arg(res.ruler).arg(sw, 0, 'f', 1);
            QVariantMap ev = code("implausible");
            ev.insert(QStringLiteral("ruler"), res.ruler);
            ev.insert(QStringLiteral("shoulderCm"), sw);
            ev.insert(QStringLiteral("minCm"), cfg.shoulderMinCm);
            ev.insert(QStringLiteral("maxCm"), cfg.shoulderMaxCm);
            res.gates.push_back(GateRecord{ PP_GATE("posture.ruler"), QStringLiteral("pelvisThrust"),
                                            GateEffect::Refused, ev, {}, {}, { QStringLiteral("headRise") } });
            res.cmPerPx = 0.0;
            res.ruler   = QStringLiteral("none");
        }
    } else if (res.cmPerPx <= 0.0) {
        // No ruler at all — neither a bright DTL ball nor a face-on ruler carried across — so no
        // centimetres: the thrust is absent rather than published in another unit.
        res.gates.push_back(GateRecord{ PP_GATE("posture.ruler"), QStringLiteral("pelvisThrust"),
                                        GateEffect::Refused, code("none"), {}, {}, { QStringLiteral("headRise") } });
    }

    // The head's address height: the ear midpoint, both ears confident (headRise above).
    const auto earAddrY = medianAt(v, *addrT, half, cm, { kLEar, kREar }, minN,
                                   [&](size_t i) { return mid(v.px(i, kLEar), v.px(i, kREar)).y(); });

    // ── per-frame channels ─────────────────────────────────────────────────────────────────
    std::vector<int64_t> grid;
    grid.reserve(v.size());
    MetricChannel thrust, headRise, bend, kneeLead, kneeTrail, forearm;
    const int lHip = in.leadIsLeft ? kLHip : kRHip, lKnee = in.leadIsLeft ? kLKnee : kRKnee,
              lAnk = in.leadIsLeft ? kLAnk : kRAnk;
    const int tHip = in.leadIsLeft ? kRHip : kLHip, tKnee = in.leadIsLeft ? kRKnee : kLKnee,
              tAnk = in.leadIsLeft ? kRAnk : kLAnk;
    const int tEl = in.leadIsLeft ? kREl : kLEl, tWr = in.leadIsLeft ? kRWr : kLWr;
    for (size_t i = 0; i < v.size(); ++i) {
        const int64_t t = v.t(i);
        grid.push_back(t);
        const bool hips = v.ok(i, kLHip, cm) && v.ok(i, kRHip, cm);
        if (hips && res.cmPerPx > 0.0)
            thrust.push(t, tw * (mid(v.px(i, kLHip), v.px(i, kRHip)).x() - *hipAddrX) * res.cmPerPx);
        if (earAddrY && res.cmPerPx > 0.0 && v.ok(i, kLEar, cm) && v.ok(i, kREar, cm))
            headRise.push(t, (*earAddrY - mid(v.px(i, kLEar), v.px(i, kREar)).y()) * res.cmPerPx);
        if (hips && v.ok(i, kLSh, cm) && v.ok(i, kRSh, cm)) {
            const QPointF d = mid(v.px(i, kLSh), v.px(i, kRSh)) - mid(v.px(i, kLHip), v.px(i, kRHip));
            bend.push(t, std::atan2(tw * d.x(), -d.y()) * 180.0 / M_PI);
        }
        if (v.ok(i, lHip, cm) && v.ok(i, lKnee, cm) && v.ok(i, lAnk, cm))
            kneeLead.push(t, kneeFlexDeg(v.px(i, lHip), v.px(i, lKnee), v.px(i, lAnk)));
        if (v.ok(i, tHip, cm) && v.ok(i, tKnee, cm) && v.ok(i, tAnk, cm))
            kneeTrail.push(t, kneeFlexDeg(v.px(i, tHip), v.px(i, tKnee), v.px(i, tAnk)));
        // WHICH WAY THE TRAIL ELBOW POINTS: the wrist→elbow vector from straight down, + when the
        // elbow is away from the ball (behind the golfer). 0° is the elbow under the hands, pointing
        // at the ground — the TPI "normal" top; a flying elbow points well behind. Read here because
        // the face-on trailElbowHeight is refused at the top on most swings (the shoulder line
        // collapses with the turn), while this view sees the elbow at the top on 111 of 113 swings
        // (docs/research/data/dtl_posture/trail_elbow_dtl_20261008.md).
        if (v.ok(i, tEl, cm) && v.ok(i, tWr, cm)) {
            const QPointF d = v.px(i, tEl) - v.px(i, tWr);
            forearm.push(t, std::atan2(-tw * d.x(), d.y()) * 180.0 / M_PI);
        }
    }

    const std::vector<PhaseEvent> &ph = *in.phases;
    auto pushSeries = [&](const MetricChannel &ch, const char *key, const char *label, const char *unit,
                    const std::vector<Phase> &at, bool toImpactOnly) {
        MetricSeries m = buildChannelSeries(grid, ch, QString::fromLatin1(key), QString::fromUtf8(label),
                                            QString::fromUtf8(unit), ph, at, cfg.maxBridgeUs);
        if (m.key.isEmpty()) return;
        if (toImpactOnly && cfg.maxBridgeUs >= 0) applyPhaseDomainMask(m, ph);
        res.series.push_back(std::move(m));
    };
    // Thrust and forward bend are ADDRESS→IMPACT quantities: past impact the golfer is standing up
    // and walking through, which is not early extension. The knees are read into the follow-through.
    pushSeries(thrust, "pelvisThrust", "Pelvis thrust", "cm",
         { Phase::Top, Phase::ArmParallelDown, Phase::Impact }, true);
    pushSeries(headRise, "headRise", "Head rise", "cm",
         { Phase::Address, Phase::Top, Phase::Impact }, true);
    pushSeries(bend, "spineForwardBend", "Spine forward bend", "°",
         { Phase::Address, Phase::Top, Phase::Impact }, true);
    pushSeries(kneeLead, "leadKneeFlexion", "Lead knee flexion", "°",
         { Phase::Address, Phase::Top, Phase::Impact, Phase::ShaftParallelThrough }, false);
    pushSeries(kneeTrail, "trailKneeFlexion", "Trail knee flexion", "°",
         { Phase::Address, Phase::Top, Phase::Impact }, false);
    pushSeries(forearm, "trailForearmAngle", "Trail elbow direction", "°",
         { Phase::Top }, false);

    // ── address scalars ────────────────────────────────────────────────────────────────────
    auto scalar = [&](const char *key, const char *label, const char *unit, double value) {
        MetricSeries m;
        m.key = QString::fromLatin1(key); m.label = QString::fromUtf8(label); m.unit = QString::fromUtf8(unit);
        m.phaseSamples.push_back({ Phase::Address, *addrT, value, QString() });
        res.series.push_back(std::move(m));
    };
    // The toe and heel LINES are the ball-most toe and the rear-most heel, not the midpoints: the
    // stance line a golfer stands to is the front of the feet.
    const auto toeLine = medianAt(v, *addrT, half, cm, { kLBigToe, kRBigToe }, minN, [&](size_t i) {
        return tw * std::max(tw * v.px(i, kLBigToe).x(), tw * v.px(i, kRBigToe).x()); });
    const auto heelLine = medianAt(v, *addrT, half, cm, { kLHeel, kRHeel }, minN, [&](size_t i) {
        return tw * std::min(tw * v.px(i, kLHeel).x(), tw * v.px(i, kRHeel).x()); });
    if (toeLine && heelLine) {
        const double foot = tw * (*toeLine - *heelLine);
        res.footLenCm = foot * res.cmPerPx;
        const auto com = medianAt(v, *addrT, half, cm, { kLHip, kRHip, kLSh, kRSh, kLKnee, kRKnee }, minN,
            [&](size_t i) {
                return 0.45 * mid(v.px(i, kLHip), v.px(i, kRHip)).x() + 0.35 * mid(v.px(i, kLSh), v.px(i, kRSh)).x()
                     + 0.20 * mid(v.px(i, kLKnee), v.px(i, kRKnee)).x(); });
        if (com && foot > 8.0)
            scalar("balanceHeelToe", "Balance heel to toe", "% foot length", 100.0 * tw * (*com - *heelLine) / foot);
        else if (com) {
            QVariantMap ev = code("foot");
            ev.insert(QStringLiteral("lengthPx"), foot);
            ev.insert(QStringLiteral("minPx"), 8.0);
            res.gates.push_back(GateRecord{ PP_GATE("posture.sizeFloor"), QStringLiteral("balanceHeelToe"),
                                            GateEffect::Refused, ev, {}, {}, {} });
        }
    }
    if (toeLine && in.ballFound && foShoulderPx && res.scaleRatio > 0.0) {
        const double shoulderDtlPx = *foShoulderPx * res.scaleRatio;
        res.shoulderWidthCm = shoulderDtlPx * res.cmPerPx;
        if (shoulderDtlPx > 8.0)
            scalar("ballBodyDistance", "Ball distance from the body", "% shoulder width",
                   100.0 * tw * (in.ballX - *toeLine) / shoulderDtlPx);
        else {
            QVariantMap ev = code("shoulder");
            ev.insert(QStringLiteral("lengthPx"), shoulderDtlPx);
            ev.insert(QStringLiteral("minPx"), 8.0);
            res.gates.push_back(GateRecord{ PP_GATE("posture.sizeFloor"), QStringLiteral("ballBodyDistance"),
                                            GateEffect::Refused, ev, {}, {}, {} });
        }
    }

    // ── the address angles: arm hang and head pitch (posture_metrics_design.md §3.1) ─────────
    //
    // Angles, so no ruler; signed toward the ball by the feet like everything above. Address
    // scalars, the balance proxy's shape: the pack reads them `at p1`, and neither means anything
    // once the club has left the ball — the arms are swinging and the head is turning.
    const double kDeg = 180.0 / M_PI;
    // ARM HANG: the frame is refused when NEITHER wrist is confident, and a shoulder likewise; one of
    // either pair is enough (seenMid). Hands at or above the shoulders are not a hang at all.
    if (const auto hang = medianOfSeen(v, *addrT, half, minN, [&](size_t i) -> std::optional<double> {
            const auto sh = seenMid(v, i, kLSh, kRSh, cm), wr = seenMid(v, i, kLWr, kRWr, cm);
            if (!sh || !wr) return std::nullopt;
            const QPointF d = *wr - *sh;
            if (d.y() <= 1.0) return std::nullopt;
            return std::atan2(tw * d.x(), d.y()) * kDeg;
        }))
        scalar("armHang", "Arm hang", "°", *hang);
    // HEAD PITCH: e = ear → nose, u = hip mid → shoulder mid (up the trunk). In a frame with x toward
    // the ball and y up, a head held square to the trunk has e at −90° from u; chin down turns e
    // further round toward the feet. headPitch = atan2(−u·e, tw·(u × e)) in image coordinates is that
    // turn, 0 when square, + chin down, and the same number for a golfer facing either way (tw).
    // The trunk needs both shoulders and both hips, exactly as spineForwardBend does; the ear is
    // whichever is confident (the lead ear is usually behind the head from this camera).
    if (const auto pitch = medianOfSeen(v, *addrT, half, minN, [&](size_t i) -> std::optional<double> {
            if (!v.ok(i, kNose, cm) || !v.ok(i, kLSh, cm) || !v.ok(i, kRSh, cm)
                || !v.ok(i, kLHip, cm) || !v.ok(i, kRHip, cm))
                return std::nullopt;
            const auto ear = seenMid(v, i, kLEar, kREar, cm);
            if (!ear) return std::nullopt;
            const QPointF u = mid(v.px(i, kLSh), v.px(i, kRSh)) - mid(v.px(i, kLHip), v.px(i, kRHip));
            const QPointF e = v.px(i, kNose) - *ear;
            if (std::hypot(u.x(), u.y()) < 1.0 || std::hypot(e.x(), e.y()) < 1.0) return std::nullopt;
            const double dot = u.x() * e.x() + u.y() * e.y(), cross = u.x() * e.y() - u.y() * e.x();
            return std::atan2(-dot, tw * cross) * kDeg;
        }))
        scalar("headPitch", "Head pitch", "°", *pitch);

    // ── the hand-path loop ─────────────────────────────────────────────────────────────────
    //
    // The backswing path is Address → the top of the hands; the downswing path is the top →
    // Impact. At each height the backswing's LAST crossing (nearest the top) is compared with the
    // downswing's FIRST, which is the pair a hand trace puts side by side. Emitted at Top: the
    // loop is made in the transition, and Top is the one event this needs anyway.
    const std::optional<int64_t> topT = phaseTimeOpt(ph, Phase::Top);
    const std::optional<int64_t> impT = phaseTimeOpt(ph, Phase::Impact);
    const int lWr = in.leadIsLeft ? kLWr : kRWr;
    const auto wristAddrY = medianAt(v, *addrT, half, cm, { lWr }, minN,
                                     [&](size_t i) { return v.px(i, lWr).y(); });
    // A refused hand-path loop, by code (trust stage 6). Not recorded when the ladder has no Top or
    // Impact — a missing event is the ladder's own record, not this reading's.
    auto refuseLoop = [&res](QVariantMap ev) {
        res.gates.push_back(GateRecord{ PP_GATE("posture.handLoop"), QStringLiteral("handPathLoop"),
                                        GateEffect::Refused, std::move(ev), {}, {}, {} });
    };
    if (topT && impT && !wristAddrY)
        refuseLoop(code("wristAtAddress"));
    if (topT && impT && wristAddrY && cfg.loopLevels >= 1) {
        std::optional<size_t> kTop;
        for (size_t i = 0; i < v.size(); ++i) {
            if (std::llabs(v.t(i) - *topT) > cfg.loopTopWindowUs || !v.ok(i, lWr, cm)) continue;
            if (!kTop || v.px(i, lWr).y() < v.px(*kTop, lWr).y()) kTop = i;
        }
        const double rise = kTop ? *wristAddrY - v.px(*kTop, lWr).y() : 0.0;
        if (!kTop) {
            QVariantMap ev = code("wristAtTop");
            ev.insert(QStringLiteral("windowMs"), double(cfg.loopTopWindowUs) / 1000.0);
            refuseLoop(std::move(ev));
        } else if (rise < cfg.loopMinRisePx) {
            QVariantMap ev = code("rise");
            ev.insert(QStringLiteral("risePx"), rise);
            ev.insert(QStringLiteral("minPx"), cfg.loopMinRisePx);
            refuseLoop(std::move(ev));
        }
        if (kTop && rise >= cfg.loopMinRisePx) {
            const int64_t tTop = v.t(*kTop);
            std::vector<QPointF> up, down;
            for (size_t i = 0; i < v.size(); ++i) {
                if (!v.ok(i, lWr, cm)) continue;
                const int64_t t = v.t(i);
                if (t >= *addrT && t <= tTop) up.push_back(v.px(i, lWr));
                if (t >= tTop && t <= *impT)  down.push_back(v.px(i, lWr));
            }
            // x where the polyline crosses height y — the last crossing, or the first.
            auto crossX = [](const std::vector<QPointF> &p, double y, bool last) -> std::optional<double> {
                std::optional<double> hit;
                for (size_t j = 0; j + 1 < p.size(); ++j) {
                    const double y0 = p[j].y(), y1 = p[j + 1].y();
                    if ((y0 - y) * (y1 - y) > 0.0 || y0 == y1) continue;
                    const double u = (y - y0) / (y1 - y0);
                    hit = p[j].x() + u * (p[j + 1].x() - p[j].x());
                    if (!last) return hit;
                }
                return hit;
            };
            std::vector<double> offs;
            const int n = cfg.loopLevels;
            for (int l = 0; l < n; ++l) {
                const double f = n == 1 ? cfg.loopFracLo
                                        : cfg.loopFracLo + (cfg.loopFracHi - cfg.loopFracLo) * l / (n - 1);
                const double y = *wristAddrY - f * rise;
                const auto xb = crossX(up, y, true), xd = crossX(down, y, false);
                if (xb && xd) offs.push_back(100.0 * tw * (*xd - *xb) / rise);
            }
            if (int(offs.size()) >= cfg.loopMinLevels) {
                double sum = 0.0;
                for (double o : offs) sum += o;
                MetricSeries m;
                m.key = QStringLiteral("handPathLoop"); m.label = QStringLiteral("Hand path loop");
                m.unit = QStringLiteral("% hand rise");
                m.phaseSamples.push_back({ Phase::Top, *topT, sum / double(offs.size()), QString() });
                res.series.push_back(std::move(m));
            } else {
                QVariantMap ev = code("levels");
                ev.insert(QStringLiteral("levels"), int(offs.size()));
                ev.insert(QStringLiteral("minLevels"), cfg.loopMinLevels);
                refuseLoop(std::move(ev));
            }
        }
    }

    res.valid = !res.series.empty();
    if (!res.valid) res.reason = QStringLiteral("no channel resolved");
    return res;
}

} // namespace pinpoint::analysis
