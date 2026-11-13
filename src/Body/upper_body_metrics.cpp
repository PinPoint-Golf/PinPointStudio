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

#include "upper_body_metrics.h"

#include "../Diagnostics/measures/anatomy_vocabulary.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <optional>
#include <utility>

namespace pinpoint::analysis {
namespace {

// The empty gated-instant list, at namespace scope because a default argument cannot name a
// local of the enclosing function (even a static one).
const std::vector<int64_t> kNoneGated;

constexpr double kRadToDeg = 57.29577951308232;
constexpr double kEps      = 1e-9;

// One frame de-normalized to PIXELS, ready to hand to the anatomy resolver.
//
// The resolver works in whatever units the caller's points are in and does not know the frame
// dimensions, so the de-normalization has to happen here — and it has to use BOTH dimensions,
// because kp arrive normalized by width and height separately. Scaling by width alone would make
// every angle a statement about the aspect ratio.
struct PxFrame {
    std::array<QPointF, kWholeBodyJoints> kp{};
    const PoseFrame2D *src = nullptr;

    KeypointFrame view() const
    {
        KeypointFrame f;
        f.kp    = kp.data();
        f.conf  = src->conf.data();
        f.count = kWholeBodyJoints;
        return f;
    }
};

PxFrame toPx(const PoseFrame2D &f, int frameW, int frameH)
{
    PxFrame out;
    out.src = &f;
    for (int i = 0; i < kWholeBodyJoints; ++i)
        out.kp[size_t(i)] = QPointF(f.kp[size_t(i)].x() * frameW, f.kp[size_t(i)].y() * frameH);
    return out;
}

double lengthOf(const QPointF &a, const QPointF &b)
{
    const double dx = b.x() - a.x(), dy = b.y() - a.y();
    return std::sqrt(dx * dx + dy * dy);
}

// ── σ helpers ───────────────────────────────────────────────────────────────────────────────
//
// One ROLE's smoother posterior σ (px), resolved through the vocabulary's own index map so a lead /
// trail role can never be paired with the other side's σ — the same discipline the rest of this file
// keeps by never touching a raw keypoint index. 0 = PoseKpAux::sigma's "no smoothed value" sentinel,
// and it stays a sentinel: nothing below reads it as a measured zero. Derived roles (Neck,
// PelvisCentre, ThoraxCentre) have no primary index and compose their σ in resolveFrame instead.
double sigOf(const PoseKpAux *aux, AnatomyRole r, bool leadIsLeft)
{
    if (!aux) return 0.0;
    const int i = rolePrimaryIndex(r, leadIsLeft);
    if (i < 0 || i >= kWholeBodyJoints) return 0.0;
    const double v = double(aux->sigma[size_t(i)]);
    return v > 0.0 ? v : 0.0;
}

// `quad2`, `lineTiltSigmaDeg` and `medianSigmaOverValid` all live in metric_channel.h, and this file
// owns no copy of any of them. The line-tilt σ is shared with lower_body_metrics deliberately: the two
// files measure four body lines between them under ONE sign convention (`lineTiltDeg`, spelled the
// same way in both), so they take one uncertainty convention too.

// Signed tilt (deg) of a body line, POSITIVE WHEN THE TRAIL END SITS ABOVE THE LEAD END.
//
// Image y grows downward, so "trail higher" is trailY < leadY and the numerator is leadY − trailY.
// The denominator is the ABSOLUTE horizontal separation, which is what makes the sign independent
// of which image side the lead is on. The alternative — a raw atan2 of the lead→trail vector, which
// is what `toeLineAngle` used until 18 Oct 2026 — flips its answer for a left-handed golfer or a
// mirrored camera while describing the same posture. This is the same function `hipLineTilt` is built on, spelled
// the same way on purpose: the hip line, the shoulder line and the elbow line are one convention.
double lineTiltDeg(const QPointF &lead, const QPointF &trail)
{
    const double dx = std::abs(trail.x() - lead.x());
    if (dx <= kEps) return 0.0;                   // ends vertically stacked — no line to measure
    return std::atan2(lead.y() - trail.y(), dx) * kRadToDeg;
}

// Vertical signed height of `p` above the infinite line through a→b, in pixels, positive UP.
//
// Deliberately a VERTICAL height rather than a perpendicular distance, and deliberately expressed
// through y only. A perpendicular distance needs a cross product, whose sign depends on whether the
// lead side is image +x — so it silently inverts on a mirrored camera, which is the exact failure
// `lineTiltDeg` exists to avoid. "How far above the shoulder line" is also what the coaching
// reading means: the elbow rising out of the turn, not its distance from an inclined axis.
bool heightAboveLine(const QPointF &p, const QPointF &a, const QPointF &b, double &outPx)
{
    const double dx = b.x() - a.x();
    if (std::abs(dx) <= kEps) return false;       // the line is vertical — no y for this x
    const double lineY = a.y() + (p.x() - a.x()) * (b.y() - a.y()) / dx;
    outPx = lineY - p.y();                        // y grows down, so above the line is positive
    return true;
}

// σ (px) of the `heightAboveLine` reading above, and it comes out CLEAN — the "height above a line"
// derivation is not the awkward one it looks like.
//
// Write the interpolation with the along-fraction t = (p.x − a.x)/dx and the line's slope
// s = (b.y − a.y)/dx, both dimensionless, dx = b.x − a.x:
//     lineY = a.y + t·(b.y − a.y)          h = lineY − p.y
// so h = (1 − t)·a.y + t·b.y − p.y. The y-partials are (1 − t), t and −1. The x-partials all enter
// through t, and ∂h/∂t = (b.y − a.y) = s·dx, which gives
//     ∂h/∂p.x = s        ∂h/∂a.x = s·(t − 1)      ∂h/∂b.x = −s·t
// — every x-term carrying the SAME factor s as its matching y-term. Summing in quadrature therefore
// factorises exactly:
//     var(h) = (1 + s²) · [ (1 − t)²σ_a² + t²σ_b² + σ_p² ]
//
// Two things that fall out of it and are worth knowing:
//   * sqrt(1 + s²) is 1 for a level line and grows as the line tilts — the reading is a VERTICAL
//     height, so a tilted line converts the endpoints' horizontal jitter into vertical error.
//   * t is not confined to [0, 1]. An elbow outside the shoulders EXTRAPOLATES the line, the weights
//     (1 − t) and t grow past one, and σ grows with them, which is the honest answer: the further
//     outside the span the elbow is, the more of the reading is the line's own slope.
// Unbounded as dx → 0, exactly as the value is — which is what upperBody.minShoulderSpanRatio
// refuses the frame for before either number is taken.
double heightAboveLineSigmaPx(const QPointF &p, const QPointF &a, const QPointF &b,
                              double sigP, double sigA, double sigB)
{
    if (!(sigP > 0.0) || !(sigA > 0.0) || !(sigB > 0.0))
        return 0.0;                               // an incomplete budget is unknown, not small
    const double dx = b.x() - a.x();
    if (std::abs(dx) <= kEps) return 0.0;         // vertical line: no reading, no σ
    const double t = (p.x() - a.x()) / dx;
    const double sl = (b.y() - a.y()) / dx;
    const double w = (1.0 - t) * (1.0 - t) * sigA * sigA + t * t * sigB * sigB + sigP * sigP;
    return std::sqrt((1.0 + sl * sl) * w);
}

// The clamped along-fraction of the foot of the perpendicular from `p` onto the SEGMENT a→b — the
// same τ `distToSegment` computes internally, exposed so the σ propagation can use the identical
// number rather than a second estimate of it. 0 or 1 means the clamp bit and the distance is to an
// endpoint, which the σ formula handles as the τ = 0 / τ = 1 limit of its interior case.
double segmentAlongFraction(const QPointF &p, const QPointF &a, const QPointF &b)
{
    const double vx = b.x() - a.x(), vy = b.y() - a.y();
    const double len2 = vx * vx + vy * vy;
    if (len2 <= kEps) return 0.0;                 // degenerate segment — distToSegment uses `a`
    return std::clamp(((p.x() - a.x()) * vx + (p.y() - a.y()) * vy) / len2, 0.0, 1.0);
}

// Shortest distance from `p` to the SEGMENT a→b (clamped to the endpoints), in pixels.
double distToSegment(const QPointF &p, const QPointF &a, const QPointF &b)
{
    const double vx = b.x() - a.x(), vy = b.y() - a.y();
    const double len2 = vx * vx + vy * vy;
    if (len2 <= kEps) return lengthOf(p, a);      // degenerate segment — fall back to the endpoint
    double t = ((p.x() - a.x()) * vx + (p.y() - a.y()) * vy) / len2;
    t = std::clamp(t, 0.0, 1.0);
    return lengthOf(p, QPointF(a.x() + t * vx, a.y() + t * vy));
}

// Unsigned angle (deg, 0–180) between two image-plane vectors.
double angleBetweenDeg(const QPointF &u, const QPointF &v)
{
    const double lu = std::sqrt(u.x() * u.x() + u.y() * u.y());
    const double lv = std::sqrt(v.x() * v.x() + v.y() * v.y());
    if (lu <= kEps || lv <= kEps) return 0.0;
    const double c = std::clamp((u.x() * v.x() + u.y() * v.y()) / (lu * lv), -1.0, 1.0);
    return std::acos(c) * kRadToDeg;
}

// σ (deg) of the face-on lead ELBOW BEND, 180° − θ with θ the angle at the elbow E between u = S − E
// (toward the lead shoulder) and v = W − E (toward the lead wrist). σ_b = σ_θ, and θ is the difference
// of the two segments' DIRECTION angles, so to first order
//     ∂θ/∂S = n̂_u/|u|    ∂θ/∂W = n̂_v/|v|    ∂θ/∂E = n̂_u/|u| − n̂_v/|v|
// (n̂ the unit normals; the signs of the first two do not matter in quadrature). THE ELBOW IS IN BOTH
// SEGMENTS, so its two terms are combined as vectors before squaring rather than summed as two
// independent variances — |n̂_u/|u| − n̂_v/|v||² = 1/|u|² + 1/|v|² − 2·cos θ/(|u||v|), since the angle
// between the normals is θ. With each keypoint's σ isotropic (exact here, see FrameGeom):
//     σ_b = sqrt( σ_S²/|u|² + σ_W²/|v|² + σ_E²·(1/|u|² + 1/|v|² − 2·cos θ/(|u||v|)) ) · 180/π
// A straight arm (θ = 180°, cos θ = −1) puts the elbow's whole jitter on the bend, (1/|u| + 1/|v|)·σ_E:
// the elbow is the hinge both levers turn about. Three DISTINCT keypoints, so no other covariance.
// ⚠ THE PUBLISHED BEND IS UNSIGNED (acos), folded at 0° like the wrist sensors' flexion magnitude, so
// on a near-straight arm (b within about 2σ of 0) the reading is biased away from 0 and this σ is the
// spread of the unfolded angle, not of the folded number. On the library the arm at impact images
// 9–22° bent (golfer medians) against a σ near 2.7°, so the fold is rarely in reach where the fold
// after impact is measured from; see upper_body_face_on_metrics.md §4.
double elbowBendSigmaDeg(const QPointF &S, const QPointF &E, const QPointF &W,
                         double sigS, double sigE, double sigW)
{
    if (!(sigS > 0.0) || !(sigE > 0.0) || !(sigW > 0.0))
        return 0.0;                               // an incomplete budget is unknown, not small
    const double lu = lengthOf(E, S), lv = lengthOf(E, W);
    if (lu <= kEps || lv <= kEps) return 0.0;
    const double cosT = std::clamp(((S.x() - E.x()) * (W.x() - E.x()) + (S.y() - E.y()) * (W.y() - E.y()))
                                       / (lu * lv), -1.0, 1.0);
    const double var = (sigS * sigS) / (lu * lu) + (sigW * sigW) / (lv * lv)
                       + sigE * sigE * (1.0 / (lu * lu) + 1.0 / (lv * lv) - 2.0 * cosT / (lu * lv));
    return std::sqrt(std::max(var, 0.0)) * kRadToDeg;
}

// Everything one frame can offer, resolved once. Each group carries its OWN validity: the shoulder
// line needs both shoulders, the axis tilt needs the spine, the drift needs both ankles and the
// chest. A frame that supplies some but not all contributes to the channels it can and is absent
// from the rest — never a zero standing in for a measurement.
struct FrameGeom {
    int64_t t_us = 0;
    bool shouldersValid = false, hipsValid = false, elbowsValid = false;
    bool spineValid = false, anklesValid = false, thoraxValid = false;
    bool leadArmValid = false, leadHandValid = false, trailElbowValid = false;

    // The three LINES, which is a stronger statement than "both joints were confident": a line
    // whose horizontal span has foreshortened below cfg.minShoulderSpanRatio / minHipSpanRatio of
    // its address span has no tilt to report. Resolved against the address reference, so they are
    // set in trackUpperBody's channel pass rather than here, and stay false when that reference
    // never resolved.
    bool shoulderLineValid = false, hipLineValid = false, elbowLineValid = false;

    QPointF leadShoulder, trailShoulder;
    QPointF leadHip, trailHip;
    QPointF leadElbow, trailElbow;
    QPointF leadWrist, leadHand;
    QPointF leadAnkle, trailAnkle;
    QPointF neck, pelvis, thorax;

    // ── Smoother posterior σ, PIXELS, per point above ────────────────────────────────────────
    //
    // From `PoseTrack2D::smoothedAux[frame].sigma[k]`, already in this same pixel domain: the
    // smoother filters kp.x·frameW / kp.y·frameH (pose_smoother.cpp) and `toPx` de-normalizes by the
    // identical pair, so nothing is rescaled.
    //
    // ONE SCALAR IS A TRUE PER-AXIS σ, and what makes it one is an EQUALITY rather than an average. It
    // is written there as sqrt(0.5·(var_x + var_y)), which reads like an RMS of two different numbers —
    // it is not, and that reading would only ever be an approximation. The two axis filters share q, dt
    // and R and share the ACCEPT FLAG (`gatePass` must pass on x AND y or the frame is rejected on
    // both), so they run identical recursions and var_x == var_y bit-for-bit; the invariant is pinned
    // by a comment at the site. Every isotropy step below — a line tilt reading only y, a projection
    // onto an arbitrary unit direction, a perpendicular offset — is therefore EXACT, not approximate.
    //
    // 0 means ABSENT, never "no error"; see UpperBodySigma. A channel's per-frame σ is computed only
    // when every joint it reads reports one.
    //
    // `leadWristSig` serves BOTH `leadWrist` and `leadHand`, because AnatomyRole::LeadHand resolves
    // to the lead WRIST keypoint — the WholeBody hand tail is finger joints and the vocabulary
    // excludes it by design (anatomy_vocabulary.cpp). One point, one σ.
    double leadShoulderSig = 0.0, trailShoulderSig = 0.0;
    double leadHipSig      = 0.0, trailHipSig      = 0.0;
    double leadElbowSig    = 0.0, trailElbowSig    = 0.0;
    double leadWristSig    = 0.0;
    double leadAnkleSig    = 0.0, trailAnkleSig    = 0.0;
    // The three DERIVED points compose theirs from their own inputs — see the end of resolveFrame.
    double neckSig = 0.0, pelvisSig = 0.0, thoraxSig = 0.0;

    // ── Is the lead arm really SEEN? (D5 round 3, the face-on elbow's occlusion gate) ─────────────
    // The pose model's RAW confidence for the lead shoulder, elbow and wrist (pose.frames, never the
    // smoother's: a predicted point is floored at 0.5 there, pose_smoother.cpp), and the centre of the
    // head's confident keypoints (nose, eyes, ears) in pixels. Set in trackUpperBody, which holds both
    // tracks; headValid false when no head keypoint was confident.
    float   leadShoulderRaw = 0.f, leadElbowRaw = 0.f, leadWristRaw = 0.f;
    bool    headValid = false;
    QPointF head;
};

// `aux` is the smoother's per-keypoint honesty record for THIS frame, or nullptr on a track that was
// never smoothed — in which case every σ below stays 0, every channel's σ stays absent, and the
// module behaves exactly as it did before σ existed. That is the only honest default: with no error
// budget we claim none.
FrameGeom resolveFrame(const PxFrame &px, bool leadIsLeft, double confMin, const PoseKpAux *aux)
{
    FrameGeom g;
    g.t_us = px.src->t_us;
    const KeypointFrame kf = px.view();
    const float mc = float(confMin);

    const auto pt = [&](AnatomyRole r) { return resolvePoint(r, kf, leadIsLeft, mc); };

    const ResolvedPoint ls = pt(AnatomyRole::LeadShoulder),  ts = pt(AnatomyRole::TrailShoulder);
    const ResolvedPoint lh = pt(AnatomyRole::LeadHip),       th = pt(AnatomyRole::TrailHip);
    const ResolvedPoint le = pt(AnatomyRole::LeadElbow),     te = pt(AnatomyRole::TrailElbow);
    const ResolvedPoint lw = pt(AnatomyRole::LeadWrist),     lhd = pt(AnatomyRole::LeadHand);
    const ResolvedPoint la = pt(AnatomyRole::LeadAnkle),     ta = pt(AnatomyRole::TrailAnkle);
    const ResolvedPoint nk = pt(AnatomyRole::Neck),          pc = pt(AnatomyRole::PelvisCentre);
    const ResolvedPoint tc = pt(AnatomyRole::ThoraxCentre);

    g.leadShoulder = ls.p;  g.trailShoulder = ts.p;
    g.leadHip      = lh.p;  g.trailHip      = th.p;
    g.leadElbow    = le.p;  g.trailElbow    = te.p;
    g.leadWrist    = lw.p;  g.leadHand      = lhd.p;
    g.leadAnkle    = la.p;  g.trailAnkle    = ta.p;
    g.neck         = nk.p;  g.pelvis        = pc.p;  g.thorax = tc.p;

    g.shouldersValid  = ls.valid && ts.valid;
    g.hipsValid       = lh.valid && th.valid;
    g.elbowsValid     = le.valid && te.valid;
    g.spineValid      = nk.valid && pc.valid;
    g.anklesValid     = la.valid && ta.valid;
    g.thoraxValid     = tc.valid;
    g.leadArmValid    = ls.valid && le.valid;
    g.leadHandValid   = lhd.valid;
    g.trailElbowValid = te.valid;

    // The raw keypoints' σ, by ROLE (sigOf resolves the index through the vocabulary, so lead / trail
    // and layout are handled in the one place they already are).
    g.leadShoulderSig  = sigOf(aux, AnatomyRole::LeadShoulder,  leadIsLeft);
    g.trailShoulderSig = sigOf(aux, AnatomyRole::TrailShoulder, leadIsLeft);
    g.leadHipSig       = sigOf(aux, AnatomyRole::LeadHip,       leadIsLeft);
    g.trailHipSig      = sigOf(aux, AnatomyRole::TrailHip,      leadIsLeft);
    g.leadElbowSig     = sigOf(aux, AnatomyRole::LeadElbow,     leadIsLeft);
    g.trailElbowSig    = sigOf(aux, AnatomyRole::TrailElbow,    leadIsLeft);
    g.leadWristSig     = sigOf(aux, AnatomyRole::LeadWrist,     leadIsLeft);
    g.leadAnkleSig     = sigOf(aux, AnatomyRole::LeadAnkle,     leadIsLeft);
    g.trailAnkleSig    = sigOf(aux, AnatomyRole::TrailAnkle,    leadIsLeft);

    // The three DERIVED points' σ, composed from their own inputs rather than read off a keypoint.
    // anatomy_vocabulary.cpp: Neck = mid(both shoulders), PelvisCentre = mid(both hips),
    // ThoraxCentre = mid(Neck, PelvisCentre) = 0.25·(both shoulders + both hips). Averaging N
    // independent estimates divides their combined σ by N, which is the arithmetic behind that file's
    // own claim that the midpoints are "often MORE reliable than a raw keypoint" — the thorax centre
    // is the quietest point in this module by a factor of two over the joints it is built from.
    // Each stays 0 unless EVERY input reported one, because a midpoint of one measured and one
    // unsmoothed keypoint has no honest error budget.
    g.neckSig   = 0.5 * quad2(g.leadShoulderSig, g.trailShoulderSig);
    g.pelvisSig = 0.5 * quad2(g.leadHipSig, g.trailHipSig);
    if (g.leadShoulderSig > 0.0 && g.trailShoulderSig > 0.0
        && g.leadHipSig > 0.0 && g.trailHipSig > 0.0) {
        g.thoraxSig = 0.25 * std::sqrt(g.leadShoulderSig * g.leadShoulderSig
                                       + g.trailShoulderSig * g.trailShoulderSig
                                       + g.leadHipSig * g.leadHipSig
                                       + g.trailHipSig * g.trailHipSig);
    }
    return g;
}

// What a whole-reference refusal takes down (trust stage 6: one cascade record, on the first
// reading it refused — secondaryAxisTilt — naming the rest). All eleven: both refusals return before
// any channel is pushed.
QStringList upperBodyCovers()
{
    return { QStringLiteral("spineSideBend"), QStringLiteral("thoraxLateralDrift"),
             QStringLiteral("shoulderPlaneAngle"), QStringLiteral("elbowAlignment"),
             QStringLiteral("trailElbowHeight"), QStringLiteral("leadHandWidth"),
             QStringLiteral("leadUpperArmToChest"), QStringLiteral("leadArmToTorso"),
             QStringLiteral("trunkRise"), QStringLiteral("leadArmFlexion") };
}

int countWithin(const std::vector<int64_t> &ts, int64_t fromUs, int64_t toUs)
{
    int n = 0;
    for (int64_t t : ts)
        if (t >= fromUs && t <= toUs) ++n;
    return n;
}

} // namespace

UpperBodyResult trackUpperBody(const PoseTrack2D &pose, int frameW, int frameH, bool leadIsLeft,
                               int64_t addressUs, const UpperBodyConfig &cfg)
{
    UpperBodyResult res;
    res.frameW = frameW;
    res.frameH = frameH;
    res.maxBridgeUs = cfg.maxBridgeUs;   // the builder reads these off the result, not off a config
    res.bridgeSpacingFactor = cfg.bridgeSpacingFactor;
    res.minShoulderSpanRatio = cfg.minShoulderSpanRatio;
    res.minElbowSpanPx       = cfg.minElbowSpanPx;
    res.minHipSpanRatio      = cfg.minHipSpanRatio;
    res.minUpperArmRatio     = cfg.minUpperArmRatio;
    res.maxUpperArmRatio     = cfg.maxUpperArmRatio;
    res.minForearmRatio      = cfg.minForearmRatio;
    res.maxForearmRatio      = cfg.maxForearmRatio;
    res.headRadiusRatio      = cfg.headRadiusRatio;
    res.minRawConf           = cfg.minRawConf;
    if (frameW <= 0 || frameH <= 0)
        return res;

    // The smoothed companion track (parallel t_us, same normalized kp) is preferred, exactly like
    // head_track, foot_metrics and lower_body_metrics — falls back to raw frames on swings analysed
    // before the smoother existed.
    const std::vector<PoseFrame2D> &frames = pose.smoothed.empty() ? pose.frames : pose.smoothed;
    if (frames.empty())
        return res;

    // THE SMOOTHER'S σ IS ATTACHED ONLY WHERE IT DESCRIBES THE CURVE BEING MEASURED. `smoothedAux` is
    // documented as parallel to `pose.smoothed`, and smoothPoseTrack fills the two together, so
    // `frames` IS the track those σ belong to exactly when `smoothed` is non-empty. Requiring both is
    // not defensive noise: a hand-built track with an aux array and no smoothed track would otherwise
    // stamp a smoother's posterior onto raw passthrough values — a confident statement about a curve
    // nobody drew.
    const bool haveAux = !pose.smoothed.empty() && pose.smoothedAux.size() == frames.size();

    std::vector<FrameGeom> geom;
    geom.reserve(frames.size());
    res.grid.reserve(frames.size());
    // The pose model's RAW confidences (D5 round 3). On a smoothed track they are the raw frames', which
    // the smoother keeps parallel to its output; a predicted point's smoothed confidence is floored at 0.5
    // and says nothing about whether the model saw it. On an unsmoothed track the frames ARE the raw ones.
    // A raw track that is not parallel (a hand-built pair) falls back to the measured track's own scores.
    const bool rawParallel = !pose.smoothed.empty() && pose.frames.size() == frames.size();
    const int iSh = rolePrimaryIndex(AnatomyRole::LeadShoulder, leadIsLeft);
    const int iEl = rolePrimaryIndex(AnatomyRole::LeadElbow, leadIsLeft);
    const int iWr = rolePrimaryIndex(AnatomyRole::LeadWrist, leadIsLeft);
    for (size_t i = 0; i < frames.size(); ++i) {
        geom.push_back(resolveFrame(toPx(frames[i], frameW, frameH), leadIsLeft, cfg.confMin,
                                    haveAux ? &pose.smoothedAux[i] : nullptr));
        res.grid.push_back(frames[i].t_us);
        FrameGeom &g = geom.back();
        const bool rawHere = pose.smoothed.empty() || (rawParallel && pose.frames[i].t_us == frames[i].t_us);
        if (!rawHere) ++res.rawConfFallbackFrames;   // judged on the smoothed score: warned by the stage
        const PoseFrame2D &raw = (rawHere && !pose.smoothed.empty()) ? pose.frames[i] : frames[i];
        const auto rawOf = [&](int k) { return (k >= 0 && k < kWholeBodyJoints) ? raw.conf[size_t(k)] : 0.f; };
        g.leadShoulderRaw = rawOf(iSh);
        g.leadElbowRaw    = rawOf(iEl);
        g.leadWristRaw    = rawOf(iWr);
        // The head: the COCO head keypoints 0–4 (nose, eyes, ears) the model scored at least confMin on
        // its raw confidence, at the measured track's positions. Handedness-free.
        double hx = 0.0, hy = 0.0;
        int    nh = 0;
        for (int k = 0; k <= 4; ++k) {
            if (rawOf(k) < float(cfg.confMin)) continue;
            hx += frames[i].kp[size_t(k)].x() * frameW;
            hy += frames[i].kp[size_t(k)].y() * frameH;
            ++nh;
        }
        if (nh > 0) { g.headValid = true; g.head = QPointF(hx / nh, hy / nh); }
    }

    // Robust (median) address reference, over the confident frames inside the Address-event window;
    // if there is no event, or nothing usable inside it, the first N usable frames. The admission
    // test is EVERYTHING the reference supplies — shoulders, hips and ankles together — for the
    // reason lower_body_metrics states: this reference sets three denominators, and a frame that
    // contributed only one of them would leave the channels scaled against different instants,
    // which is a class of error that produces plausible numbers.
    const auto usable = [](const FrameGeom &g) {
        return g.shouldersValid && g.anklesValid && g.leadArmValid;
    };

    std::vector<const FrameGeom *> ref;
    if (addressUs >= 0) {
        for (const FrameGeom &g : geom)
            if (usable(g) && std::llabs(g.t_us - addressUs) <= cfg.addrWindowUs)
                ref.push_back(&g);
    }
    if (ref.empty()) {
        for (const FrameGeom &g : geom) {
            if (!usable(g)) continue;
            ref.push_back(&g);
            if (int(ref.size()) >= cfg.addrMinFrames) break;
        }
    }
    if (ref.empty()) {
        // no usable upper body anywhere — leave valid == false
        GateLog gates;
        gates.fireCovering(PP_GATE("body.upperAddress"), QStringLiteral("secondaryAxisTilt"), GateEffect::Refused,
                           upperBodyCovers(),
                           { { QStringLiteral("posedFrames"), int(frames.size()) },
                             { QStringLiteral("minConf"), cfg.confMin } });
        res.gates = gates.take();
        return res;
    }

    std::vector<double> shoulderSpans, stanceSpans, armLens, leadAnkX, trailAnkX;
    // The body lines' HORIZONTAL address spans, the foreshortening ratios' denominators. Same
    // reference frames and same median as everything else above, so a ratio cannot inherit a scale
    // difference from the window it was measured over. The hip list is conditional because the
    // reference admission test does not require the hips (it asks for shoulders, ankles and the lead
    // arm) — exactly how armLens is already collected.
    //
    // NB these are median(|dx_i|), the span measured per reference frame and then reduced, not
    // |median(trailX) − median(leadX)|. Under a pixel of difference on a still address, and the
    // per-frame form is the same quantity the live frames are compared against, measured the same
    // way. There is no elbow entry: that line takes an absolute pixel floor, for the reason given at
    // the gates below.
    std::vector<double> shoulderDx, hipDx;
    // The trunk chord, trunkRise's denominator: neck (shoulder midpoint) to pelvis centre (hip
    // midpoint), the same two derived points secondaryAxisTilt reads. Conditional on the hips for the
    // reason hipDx is, and over the same reference frames, so the ratio's two ends are measured alike.
    std::vector<double> trunkLens;
    // The lead upper arm's address length, leadArmToTorso's gate denominator. Unconditional: the
    // admission test asks for the lead arm, so every reference frame has one.
    std::vector<double> upperArmLens;
    // The lead forearm's address length, the face-on lead elbow's forearm-gate denominator. Conditional
    // on the wrist, as armLens is: the admission test does not ask for it.
    std::vector<double> forearmLens;
    for (const FrameGeom *g : ref) {
        // % shoulder width means the EUCLIDEAN shoulder separation, because that is what it already
        // means in the shipped `stanceWidth` metric (foot_metrics' shoulderWidthPxAt). % stance
        // width means the ABSOLUTE HORIZONTAL ankle span, because that is what it already means in
        // the shipped `pelvisSway` (lower_body_metrics' addrSpanPx). Two units, two established
        // denominators; a new producer does not get to redefine either.
        shoulderSpans.push_back(lengthOf(g->leadShoulder, g->trailShoulder));
        stanceSpans.push_back(std::abs(g->trailAnkle.x() - g->leadAnkle.x()));
        if (g->leadHandValid)
            armLens.push_back(lengthOf(g->leadShoulder, g->leadElbow)
                              + lengthOf(g->leadElbow, g->leadHand));
        leadAnkX.push_back(g->leadAnkle.x());
        trailAnkX.push_back(g->trailAnkle.x());
        shoulderDx.push_back(std::abs(g->trailShoulder.x() - g->leadShoulder.x()));
        if (g->hipsValid)
            hipDx.push_back(std::abs(g->trailHip.x() - g->leadHip.x()));
        if (g->spineValid)
            trunkLens.push_back(lengthOf(g->neck, g->pelvis));
        upperArmLens.push_back(lengthOf(g->leadShoulder, g->leadElbow));
        if (g->leadHandValid)
            forearmLens.push_back(lengthOf(g->leadElbow, g->leadHand));
    }

    res.ref.shoulderSpanPx = medianOfCopy(shoulderSpans);
    res.ref.stanceSpanPx   = medianOfCopy(stanceSpans);
    res.ref.leadArmLenPx   = medianOfCopy(armLens);
    res.ref.shoulderDxPx   = medianOfCopy(shoulderDx);
    res.ref.hipDxPx        = medianOfCopy(hipDx);      // 0 when address hips were never confident
    res.ref.trunkLenPx     = medianOfCopy(trunkLens);  // likewise
    res.ref.leadUpperArmLenPx = medianOfCopy(upperArmLens);
    res.ref.leadForearmLenPx  = medianOfCopy(forearmLens);   // 0 when no address wrist was confident

    // Which image direction the lead side is, resolved from the address geometry rather than
    // assumed — a camera can be mirrored and an operator can flip the preview.
    res.ref.leadSign = (medianOfCopy(leadAnkX) <= medianOfCopy(trailAnkX)) ? -1.0 : 1.0;

    // The denominator floor. Below it every percentage is noise divided by noise, and the honest
    // answer is that nothing was measured — not a large number.
    if (res.ref.shoulderSpanPx < cfg.minShoulderSpanPx) {
        GateLog gates;
        gates.fireCovering(PP_GATE("body.shoulderSpan"), QStringLiteral("secondaryAxisTilt"), GateEffect::Refused,
                           upperBodyCovers(),
                           { { QStringLiteral("spanPx"), res.ref.shoulderSpanPx },
                             { QStringLiteral("minPx"), cfg.minShoulderSpanPx } });
        res.gates = gates.take();
        return res;
    }
    res.ref.valid = true;

    const double toPctShoulder = 100.0 / res.ref.shoulderSpanPx;
    const double toPctStance   = res.ref.stanceSpanPx > kEps ? 100.0 / res.ref.stanceSpanPx : 0.0;
    const double toPctArm      = res.ref.leadArmLenPx > kEps ? 100.0 / res.ref.leadArmLenPx : 0.0;
    const double toPctTrunk    = res.ref.trunkLenPx > kEps ? 100.0 / res.ref.trunkLenPx : 0.0;

    // The body LINES' validity gates, resolved per frame against the address spans above.
    //
    // `lineTiltDeg` divides by the live horizontal separation, so as the golfer turns the two ends of
    // a line foreshorten toward the same image column and the angle runs to ±90° while the posture it
    // is supposed to describe has not changed. On the swing that prompted this work `shoulderPlane`
    // hits +88° AT THE TOP — a GRADED phase sample, not a chart curiosity — and the hip line does the
    // same thing just after impact. Below the ratio the frame HAS no such line and the channel is
    // ABSENT for it: never a sentinel, and in particular never the 0.0 `lineTiltDeg` returns for a
    // vertically stacked pair, which would read as "perfectly level".
    //
    // A ratio needs a denominator: a line whose ADDRESS span was never measured (0 px) cannot be
    // gated, so it is absent for the whole swing instead — the same refusal `toPctArm > 0.0` already
    // makes for leadHandWidth, and for the same reason (we do not know, so we do not say). For the
    // hips that is a REAL and visible case: an address whose hips were never confident leaves
    // spineSideBend absent for the entire swing while the lower-body module, which resolves its own
    // reference from its own admission test, still produces hipLineTilt. One ratio, two references.
    //
    // WHAT IS GATED IS EXACTLY WHAT DIVIDES BY A LINE'S LIVE dx, which was audited channel by
    // channel rather than assumed:
    //
    //   shoulderPlaneAngle  atan2(dy, |dx_shoulders|)                     → gated, RATIO
    //   elbowAlignment      atan2(dy, |dx_elbows|)                        → gated, PIXEL FLOOR
    //   spineSideBend       a DIFFERENCE of two such tilts                → gated on BOTH lines
    //   trailElbowHeight    heightAboveLine ÷ dx_shoulders (see below)    → gated, the shoulder ratio
    //
    // and the rest are not, because their divisors are address CONSTANTS or Euclidean lengths that
    // do not vanish as the body turns:
    //
    //   secondaryAxisTilt    ÷ the VERTICAL neck→pelvis rise (own kEps guard)
    //   thoraxLateralDrift   ÷ the Euclidean ankle-line length, × the address ankle |dx|
    //   leadHandWidth        ÷ the address lead-arm length
    //   leadUpperArmToChest  ÷ the address Euclidean shoulder span (distToSegment is a length)
    //
    // Those channels are still distorted by the projection of a turn — that is a phase-DOMAIN
    // question, answered one layer up in the descriptor — but they are not divisions by a vanishing
    // separation, and gating them would withhold measurements that were actually made.
    //
    // leadArmToTorso (÷ two Euclidean vector lengths) WAS on that list, and since 13 Nov 2026 it is
    // gated for a different reason: not a vanishing divisor but two DIRECTIONS that stop being
    // measured. Its torso direction starts at the shoulder midpoint, which is guesswork once the
    // shoulders overlap (the shoulder-line ratio, as trunkRise), and its arm direction is noise once
    // the upper arm points along the view ray (the upper-arm length band). See its block below.
    //
    // ⚠ THE ELBOW LINE TAKES A PIXEL FLOOR, NOT A RATIO, and that is a correction rather than an
    // inconsistency. A ratio needs an address span that represents the line at its WIDEST; the elbows
    // are at their NARROWEST at address (the arms hang together and separate through the swing), so
    // |dx| / address |dx| is ≈1 at address and ≥1 after it. The gate would never fire — and it would
    // read exactly 1.0 at address, which is precisely where `elbowAlignment` is read and where a
    // 20 px elbow separation is pure keypoint noise. An absolute floor is the only form that can
    // refuse the frame the metric is actually graded on.
    const auto ratioValid = [](double liveDx, double addrDx, double minRatio) {
        return addrDx > kEps && (liveDx / addrDx) >= minRatio;
    };

    // Push a value AND its σ together. The two vectors are parallel by contract (UpperBodySigma) and
    // this is the only place they could fall out of step, so there is one way to append to them.
    const auto pushS = [](MetricChannel &ch, std::vector<double> &sig,
                          int64_t t, double v, double sigma) {
        ch.push(t, v);
        sig.push_back(sigma);
    };

    for (FrameGeom &g : geom) {
        // The frames each line gate could have refused — its summary record's denominator.
        if (g.shouldersValid) res.shouldersSeenUs.push_back(g.t_us);
        if (g.elbowsValid) res.elbowsSeenUs.push_back(g.t_us);
        if (g.shouldersValid && g.hipsValid) res.trunkSeenUs.push_back(g.t_us);
        if (g.shouldersValid)
            g.shoulderLineValid = ratioValid(std::abs(g.trailShoulder.x() - g.leadShoulder.x()),
                                             res.ref.shoulderDxPx, cfg.minShoulderSpanRatio);
        if (g.hipsValid)
            g.hipLineValid = ratioValid(std::abs(g.trailHip.x() - g.leadHip.x()),
                                        res.ref.hipDxPx, cfg.minHipSpanRatio);
        if (g.elbowsValid)
            g.elbowLineValid =
                std::abs(g.trailElbow.x() - g.leadElbow.x()) >= cfg.minElbowSpanPx;

        // secondaryAxisTilt — the mid-hip→mid-shoulder line from vertical, POSITIVE AWAY FROM THE
        // TARGET. This is the one lateral channel that is trail-positive rather than lead-positive:
        // the quantity is NAMED for the lean away from the target, and flipping it to satisfy the
        // lead-positive rule would leave every coach-facing sentence about it backwards.
        if (g.spineValid) {
            const double rise = g.pelvis.y() - g.neck.y();       // y grows down: neck above ⇒ > 0
            if (rise > kEps) {
                const double towardTrail = -res.ref.leadSign * (g.neck.x() - g.pelvis.x());
                // σ: this is an atan2 of the DIFFERENCE OF TWO POINTS, exactly like a line tilt — the
                // spine's direction angle, read from vertical instead of from horizontal, which
                // swaps which axis is the numerator and leaves the magnitude of the propagation
                // untouched. So lineTiltSigmaDeg applies verbatim:
                //     σ = sqrt(σ_neck² + σ_pelvis²) / |neck − pelvis| · 180/π
                // — a keypoint σ over the SPINE's lever arm, which is the longest lever in this
                // module and is why the axis tilt is its quietest angle.
                //
                // Neck and PelvisCentre are built from DISJOINT keypoint sets (shoulders vs hips), so
                // they are independent and the quadrature sum has no covariance term to drop. That is
                // not true of every pair in this file — see leadUpperArmToChest and leadArmToTorso.
                pushS(res.axisTilt, res.sigma.axisTilt, g.t_us,
                      std::atan2(towardTrail, rise) * kRadToDeg,
                      lineTiltSigmaDeg(g.neck, g.pelvis, g.neckSig, g.pelvisSig));
            }
        }

        // shoulderPlaneAngle / elbowAlignment — body lines against the horizontal, one convention.
        // Gated on the LINE, not on the joints: both shoulders can be perfectly confident and still
        // not describe a line the camera can measure a tilt on.
        //
        // The `else if (joints were confident)` arms record a REFUSED instant, which the resample must
        // not bridge — as distinct from a frame the detector simply lost, which it may.
        //
        // σ for both: the line-tilt rule, sqrt(σ_lead² + σ_trail²) over that line's own Euclidean
        // length. The ELBOW line is the shorter of the two on every real swing, so it is the noisier
        // angle by the ratio of the two lengths — which is the same fact upperBody.minElbowSpanPx
        // exists for, arriving here as a number rather than as a gate.
        if (g.shoulderLineValid)
            pushS(res.shoulderPlane, res.sigma.shoulderPlane, g.t_us,
                  lineTiltDeg(g.leadShoulder, g.trailShoulder),
                  lineTiltSigmaDeg(g.leadShoulder, g.trailShoulder,
                                   g.leadShoulderSig, g.trailShoulderSig));
        else if (g.shouldersValid)
            res.gatedShoulderLine.push_back(g.t_us);
        if (g.elbowLineValid)
            pushS(res.elbowLine, res.sigma.elbowLine, g.t_us,
                  lineTiltDeg(g.leadElbow, g.trailElbow),
                  lineTiltSigmaDeg(g.leadElbow, g.trailElbow,
                                   g.leadElbowSig, g.trailElbowSig));
        else if (g.elbowsValid)
            res.gatedElbowLine.push_back(g.t_us);

        // spineSideBend — the THORAX RELATIVE TO THE PELVIS, which is what side bend means. With no
        // keypoint between the shoulders and the hips, the honest reading of "thorax relative to
        // pelvis" is the shoulder line against the hip line: two segments that both exist, and a
        // difference that cancels the whole-body tilt secondaryAxisTilt already reports. Reading a
        // single neck-to-pelvis line here instead would make this metric a copy of that one.
        // POSITIVE = SIDE BEND TOWARD THE TRAIL SIDE (the trail shoulder dropping under the turn).
        //
        // It needs BOTH lines valid, because it is a DIFFERENCE of two tilts and either one going
        // degenerate poisons it on its own — a −88° hip line under a good shoulder line would report
        // a side bend of nearly 90° at the moment the golfer is most square.
        if (g.shoulderLineValid && g.hipLineValid) {
            const double hipTilt      = lineTiltDeg(g.leadHip, g.trailHip);
            const double shoulderTilt = lineTiltDeg(g.leadShoulder, g.trailShoulder);
            // σ: a DIFFERENCE of two independent line tilts (hips and shoulders share no keypoint),
            // so σ = sqrt(σ_hipTilt² + σ_shoulderTilt²), each term the line-tilt rule on its own line
            // and its own length. This is the noisiest angle in the module by construction — it adds
            // two tilts' worth of noise and the HIP line is the shorter of the two, so on a typical
            // framing the hip half contributes roughly twice what the shoulder half does.
            pushS(res.sideBend, res.sigma.sideBend, g.t_us, hipTilt - shoulderTilt,
                  quad2(lineTiltSigmaDeg(g.leadHip, g.trailHip, g.leadHipSig, g.trailHipSig),
                        lineTiltSigmaDeg(g.leadShoulder, g.trailShoulder,
                                         g.leadShoulderSig, g.trailShoulderSig)));
        } else if (g.shouldersValid && g.hipsValid) {
            // Both joint pairs confident, at least one LINE refused — a gated instant, not a dropout.
            res.gatedSideBend.push_back(g.t_us);
        }

        // thoraxLateralDrift — the chest along the stance line, measured FROM THE TRAIL ANKLE, per
        // the `thoraxCentre · distance · trailAnkle` facet the content authored. NOT
        // address-referenced: the measure over it is a Delta, which does the referencing, and a
        // producer that pre-subtracted address would make that Delta a difference of differences.
        // The stance unit vector points trail→lead, so the projection is lead-positive by
        // construction and needs no leadSign.
        if (g.thoraxValid && g.anklesValid && toPctStance > 0.0) {
            const double ux = g.leadAnkle.x() - g.trailAnkle.x();
            const double uy = g.leadAnkle.y() - g.trailAnkle.y();
            const double ul = std::sqrt(ux * ux + uy * uy);
            if (ul > kEps) {
                const double along = ((g.thorax.x() - g.trailAnkle.x()) * ux
                                      + (g.thorax.y() - g.trailAnkle.y()) * uy) / ul;
                // ── σ: THE FULL FIRST-ORDER PROPAGATION, rotation term included ──────────────
                //
                // Same geometry as lower_body_metrics.cpp's comOverLeadFoot — whose block carries the
                // derivation in full, including why differentiating the WHOLE expression settles the
                // shared-ankle correlation instead of assuming it away — with the TRAIL ankle as the
                // reference rather than the lead one. `along` = (T − B)·û, û = (A − B)/L, A the lead
                // ankle, B the trail ankle, T the thorax centre:
                //
                //   ∂/∂T = û                      |·|² = 1     (T is the 4-joint centroid ⇒ 0.0625 each)
                //   ∂/∂B = −û − (h/L)·n̂            |·|² = 1 + (h/L)²
                //   ∂/∂A =       (h/L)·n̂           |·|² = (h/L)²
                //   var  = σ_thorax² + (1 + (h/L)²)σ_tAnk² + (h/L)²σ_lAnk²
                //   σ    = sqrt(var) · 100/stanceSpanPx
                //
                // (0.0625 × the four joints' variances IS σ_thorax², since σ_thorax = 0.25·sqrt of that
                // sum — resolveFrame composes it that way, so the two spellings agree by construction.)
                //
                // h is the thorax centre's PERPENDICULAR offset from the live ankle line, and here it is
                // the largest in the product: the chest sits well over TWO stance widths above the
                // ankles, so h/L ≈ 2.4 and the rotation terms dominate everything else outright.
                //
                // ⚠ THE READING A `sqrt(σ_thorax² + σ_tAnk²)` FORM GIVES — the (h/L) = 0 case, and the
                // shape C11 pins for this channel's two lower-body twins — is 1.12 % on §12's fixture
                // against 3.54 % for the full form, a factor of 3.2 LOW. Design principle 3 rules out
                // shipping the smaller number, so all three channels of this geometry now carry the
                // full propagation and agree with each other.
                //
                // h/L from the 2D cross product: |r × u| / L², r = T − B. Exact, no normal constructed.
                const double lever = std::abs((g.thorax.x() - g.trailAnkle.x()) * uy
                                              - (g.thorax.y() - g.trailAnkle.y()) * ux) / (ul * ul);
                const double lev2  = lever * lever;
                double sigDrift = 0.0;
                if (g.thoraxSig > 0.0 && g.leadAnkleSig > 0.0 && g.trailAnkleSig > 0.0) {
                    sigDrift = std::sqrt(g.thoraxSig * g.thoraxSig
                                         + (1.0 + lev2) * g.trailAnkleSig * g.trailAnkleSig
                                         + lev2 * g.leadAnkleSig * g.leadAnkleSig) * toPctStance;
                }
                pushS(res.thoraxDrift, res.sigma.thoraxDrift, g.t_us, along * toPctStance, sigDrift);
            }
        }

        // trailElbowHeight — how far the trail elbow has risen above the shoulder line.
        //
        // GATED ON THE SHOULDER LINE even though it is a height, not a tilt, because heightAboveLine
        // interpolates the line's y AT the elbow's x and therefore divides by the same dx:
        // `lineY = a.y + (p.x − a.x)·(b.y − a.y)/dx`. As the shoulders foreshorten toward one image
        // column that quotient runs away, and where a tilt at least saturates at 90° a % shoulder
        // width does not — it is unbounded. Its own kEps guard below stays as the last-resort check
        // on an exactly vertical line; this is the honest floor above it. Same mask as
        // shoulderPlaneAngle, from the same line, for the same reason.
        if (g.shoulderLineValid && g.trailElbowValid) {
            double hPx = 0.0;
            if (heightAboveLine(g.trailElbow, g.trailShoulder, g.leadShoulder, hPx)) {
                // σ: heightAboveLineSigmaPx carries the derivation. It factorises exactly to
                //     sqrt(1 + s²) · sqrt((1 − t)²σ_trailSh² + t²σ_leadSh² + σ_trailElbow²)
                // with t the elbow's along-fraction of the shoulder span and s the line's slope, then
                // ×100/shoulderSpanPx for the unit. The three keypoints are distinct and independent,
                // so there is no covariance term here — unlike leadUpperArmToChest below, whose point
                // is derived from one of its own line's endpoints.
                pushS(res.trailElbowHeight, res.sigma.trailElbowHeight, g.t_us,
                      hPx * toPctShoulder,
                      heightAboveLineSigmaPx(g.trailElbow, g.trailShoulder, g.leadShoulder,
                                             g.trailElbowSig, g.trailShoulderSig,
                                             g.leadShoulderSig) * toPctShoulder);
            }
        }

        // leadHandWidth — the swing's width, against the golfer's OWN arm length so it reads as a
        // fraction of the width actually available to them rather than as an absolute distance.
        if (g.leadHandValid && g.thoraxValid && toPctArm > 0.0) {
            // σ: a DISTANCE between two points. Its gradient with respect to either point is the unit
            // vector along the line joining them, so an isotropic noise contributes its full σ and
            //     σ = sqrt(σ_leadHand² + σ_thorax²) · 100/leadArmLenPx.
            // Independent: LeadHand resolves to the lead WRIST keypoint and the thorax centre is
            // built from the shoulders and hips, so they share nothing. The denominator is an ADDRESS
            // median, a fixed scale rather than per-frame noise.
            pushS(res.leadHandWidth, res.sigma.leadHandWidth, g.t_us,
                  lengthOf(g.leadHand, g.thorax) * toPctArm,
                  quad2(g.leadWristSig, g.thoraxSig) * toPctArm);
        }

        // leadUpperArmToChest — connection. The chest centre's distance to the upper-arm SEGMENT
        // (clamped to its endpoints), not to the infinite line: past the elbow the arm has ended,
        // and an infinite line would keep reporting a gap to something that is not there.
        if (g.leadArmValid && g.thoraxValid) {
            // σ: THE ONE DERIVATION HERE THAT HAS TO ACCOUNT FOR A SHARED KEYPOINT, and the sharing
            // changes the answer rather than decorating it.
            //
            // Write the distance as a projection onto the segment's unit normal n̂: d = n̂·(thorax −
            // leadShoulder). Only displacements ALONG n̂ move d at first order (a displacement along
            // the segment slides the foot of the perpendicular and leaves the distance alone), so one
            // measurement direction serves for every input and the variances add as plain squares.
            // Two facts then do the work:
            //   * ThoraxCentre is 0.25·(leadSh + trailSh + leadHip + trailHip) — anatomy_vocabulary
            //     builds it as the mid of the shoulder mid and the hip mid — so the LEAD SHOULDER
            //     appears in BOTH terms of d and part of its jitter CANCELS.
            //   * Perturbing an endpoint also ROTATES the segment. Rotating about the far end moves d
            //     by τ·δ (near end) or (1−τ)·δ (far end), τ the clamped along-fraction of the foot of
            //     the perpendicular — which is exactly what distToSegment computes, and τ = 0 / 1
            //     reproduces the endpoint-distance case the clamp falls back to.
            // Collecting the coefficients of a displacement along n̂:
            //     leadShoulder  0.25 − (1 − τ) = τ − 0.75        leadElbow  −τ
            //     trailShoulder 0.25      leadHip 0.25      trailHip 0.25
            //     σ_d = sqrt((τ−0.75)²σ_leadSh² + 0.0625·(σ_trailSh² + σ_leadHip² + σ_trailHip²)
            //                + τ²σ_leadElbow²) · 100/shoulderSpanPx
            //
            // Treating the thorax as independent of the shoulder instead would OVERSTATE σ — the lead
            // shoulder's coefficient would be 1−τ ≈ 0.2 plus a separate 0.25 in quadrature rather
            // than the τ−0.75 ≈ 0.03 they combine to at the τ ≈ 0.78 a hanging arm gives. Keeping the
            // shared term is what makes this a propagation rather than a bound.
            //
            // ⚠ WHY THIS UNSIGNED DISTANCE GETS A ± WHEN leadArmToTorso'S UNSIGNED ANGLE DOES NOT. Both
            // are folded at zero, so both would misstate centre and spread NEAR zero — but only one of
            // them ever goes there. The chest centre sits about 70 px from the lead upper arm against a
            // σ near 1.8 px, so d/σ ≈ 40: the fold is some forty standard deviations away and the
            // distribution at the reported value is plainly symmetric. The centroid cannot approach the
            // segment either, being a fixed average of the shoulders and hips while the segment starts
            // at one of those shoulders — the geometry has a floor under it. leadArmToTorso's angle has
            // no such floor: a lead arm hanging alongside the torso at address reads a few degrees, and
            // that IS the instant the metric is read.
            const double tau = segmentAlongFraction(g.thorax, g.leadShoulder, g.leadElbow);
            double sigGap = 0.0;
            if (g.leadShoulderSig > 0.0 && g.trailShoulderSig > 0.0 && g.leadHipSig > 0.0
                && g.trailHipSig > 0.0 && g.leadElbowSig > 0.0) {
                const double cLeadSh = tau - 0.75;
                const double var = cLeadSh * cLeadSh * g.leadShoulderSig * g.leadShoulderSig
                                   + 0.0625 * (g.trailShoulderSig * g.trailShoulderSig
                                               + g.leadHipSig * g.leadHipSig
                                               + g.trailHipSig * g.trailHipSig)
                                   + tau * tau * g.leadElbowSig * g.leadElbowSig;
                sigGap = std::sqrt(var) * toPctShoulder;
            }
            pushS(res.leadArmGap, res.sigma.leadArmGap, g.t_us,
                  distToSegment(g.thorax, g.leadShoulder, g.leadElbow) * toPctShoulder, sigGap);
        }

        // leadArmToTorso — the lead upper arm against the torso, per the `leadUpperArm · angle ·
        // spine` facet. Measured against the spine pointing DOWN the torso (neck→pelvis) so that
        // an arm hanging alongside the body reads near zero and HIGHER MEANS FURTHER FROM THE
        // TORSO, which is the direction the content's `highMeans` states. Unsigned: the frontal
        // projection cannot say which side of the torso the arm left on, and pretending otherwise
        // would put a sign on a quantity the camera did not resolve.
        //
        // ⚠ NO σ IS PROPAGATED FOR THIS CHANNEL, and it is left UNSET rather than guessed. The
        // derivation that looks obvious — the angle between two vectors is the DIFFERENCE of their
        // two direction angles, each with σ = (endpoint σ in quadrature) / (that vector's length), so
        // σ_φ = sqrt(σ_arm² + σ_torso²) — fails on two counts, both real:
        //
        //   * THE REPORTED VALUE IS UNSIGNED (0–180, out of an acos). The frontal projection cannot
        //     say which side of the torso the arm left on, which is why the value is folded — but a
        //     folded quantity's published number near zero is E|Δ|, not Δ, and it is biased AWAY from
        //     zero by about 0.8σ. A symmetric "± σ" chip around it would then misstate both the
        //     centre and the spread, and the small angles are exactly where the metric is read (an arm
        //     hanging alongside the body at address). acos's derivative is singular there too.
        //   * `Neck` IS THE SHOULDER MIDPOINT, so it contains the LEAD SHOULDER — the same keypoint
        //     the arm vector starts from. The two direction angles are therefore CORRELATED, with a
        //     covariance of −0.5·cos φ·σ_leadSh²/(L_arm·L_torso), and a plain quadrature sum ignores
        //     it. The sign is known, so this half is fixable; the fold above is not.
        //
        // C11's rule for exactly this case is "derive it properly or leave it unset — never guess",
        // and the honest reading is that the quantity as published is not a symmetric ±σ quantity.
        // What would fix it is a SIGNED lead-arm-to-torso angle, which is a producer change and a
        // content change, not an uncertainty one.
        //
        // ⚠ GATED ON BOTH ITS DIRECTIONS (13 Nov 2026, plane stack FA), and a refused instant goes on
        // gatedArmTorso, never a silent drop:
        //   * THE TORSO: `Neck` is the shoulder midpoint, and once the shoulders foreshorten toward one
        //     image column which keypoint is which — and so where their midpoint sits — is guesswork.
        //     The shoulder-line ratio (upperBody.minShoulderSpanRatio, 0.40), as trunkRise. At a
        //     correctly timed P8 the reading had been mostly measuring chest turn (ρ −0.76 with the
        //     shoulder-span ratio, arm-readings report).
        //   * THE ARM: its image length against its address length must lie in
        //     [upperBody.minUpperArmRatio, upperBody.maxUpperArmRatio] (0.45 … 1.4). Below it the
        //     upper arm points along the view ray and its image direction is keypoint noise — on
        //     Jess's late P8s it imaged 13–43 px against ~110 at address and read anything from 2° to
        //     179°; above it the elbow has jumped off the arm.
        // A frame that passes both reads exactly what it read before.
        if (g.leadArmValid && g.spineValid) {
            const double armLen   = lengthOf(g.leadShoulder, g.leadElbow);
            const double armRatio = res.ref.leadUpperArmLenPx > kEps ? armLen / res.ref.leadUpperArmLenPx : 0.0;
            const bool   armInView = armRatio >= cfg.minUpperArmRatio && armRatio <= cfg.maxUpperArmRatio;
            if (g.shoulderLineValid && armInView) {
                const QPointF armDir(g.leadElbow.x() - g.leadShoulder.x(),
                                     g.leadElbow.y() - g.leadShoulder.y());
                const QPointF torsoDir(g.pelvis.x() - g.neck.x(), g.pelvis.y() - g.neck.y());
                res.leadArmToTorso.push(g.t_us, angleBetweenDeg(armDir, torsoDir));
            } else {
                res.gatedArmTorso.push_back(g.t_us);
                res.gatedArmTorsoShoulderRatio.push_back(
                    res.ref.shoulderDxPx > kEps
                        ? std::abs(g.trailShoulder.x() - g.leadShoulder.x()) / res.ref.shoulderDxPx : 0.0);
                res.gatedArmTorsoArmRatio.push_back(armRatio);
            }
        }

        // trunkRise — the trunk chord's image length against its address length, % (see the header).
        // POSITIVE = THE CHORD IMAGES LONGER = THE TRUNK MORE UPRIGHT. ℓ = L·cos β, so standing up out
        // of a 34° bend reads about +1.2 % per degree.
        //
        // GATED ON THE SHOULDER LINE, by the same ratio as shoulderPlaneAngle, though nothing here
        // divides by the line's dx. The chord's top end is the shoulder MIDPOINT, and once the two
        // shoulders foreshorten toward one image column (the top, on most swings) which keypoint is
        // which, and so where their midpoint sits, is guesswork: a pose that swaps or merges them
        // moves the midpoint by a large fraction of the span while the trunk has not moved. A refused
        // instant goes on gatedShoulderLine with the others, so the resample never bridges it.
        //
        // σ: a DISTANCE between two points, so its gradient with respect to either is the unit vector
        // joining them and an isotropic σ enters whole:
        //     σ = sqrt(σ_neck² + σ_pelvis²) · 100/trunkLenPx.
        // Neck and PelvisCentre are built from disjoint keypoints (shoulders vs hips), so independent.
        // The address reference's own noise is left out, as pelvisLift leaves its out
        // (lower_body_metrics.cpp carries the size of that omission) — the same reference rule, the
        // same omission.
        // leadArmFlexion, the faceOn rung (13 Nov 2026, plane stack D5) — the LEAD ELBOW BEND, 180° − the
        // angle at the lead elbow between the lead shoulder and the lead wrist. 0 = a straight arm, + =
        // more bend: the sign and the unsigned magnitude of the wrist sensors' route (metric_extractor.cpp
        // flexRad, acos of the upper-arm and forearm axes), so one corridor means one thing on both rungs.
        // Mark, 13 Nov: chicken wing "should be easily visible from the face-on camera".
        //
        // GATED ON BOTH ARM SEGMENTS AND NOTHING ELSE. A segment pointing toward or away from the camera
        // images short and HIDES the bend (a folded arm seen end-on reads straight), so the upper arm
        // (shoulder → elbow) must image within [minUpperArmRatio, maxUpperArmRatio] of its address length
        // — leadArmToTorso's band — and the forearm (elbow → wrist) within [minForearmRatio,
        // maxForearmRatio] of its own. There is NO shoulder-line half: the reading never uses the shoulder
        // midpoint, so a chest turned edge-on to the camera does not refuse it. A refused instant goes on
        // gatedLeadElbow with both ratios, never a silent drop.
        //
        // σ: the three keypoints through the angle — elbowBendSigmaDeg above carries the derivation,
        // including the elbow's shared term and the caveat on the fold at 0°.
        if (g.leadArmValid && g.leadHandValid) {
            res.leadElbowSeenUs.push_back(g.t_us);
            // OCCLUSION FIRST (D5 round 3): a point that is not really seen places no bend, whatever its
            // segment lengths say. The model's own detection of a hidden joint lands somewhere plausible
            // — on Jess's chin when her head covers her lead shoulder after impact — so the detector's
            // say-so is not enough: a point on the head, or one the model scored low on its RAW
            // confidence, is unseen. See tuned::upperBody::kHeadRadiusRatio / kMinRawConf.
            uint8_t why = 0;
            if (g.headValid) {
                const double r = cfg.headRadiusRatio * res.ref.shoulderSpanPx;
                if (lengthOf(g.leadShoulder, g.head) < r) why |= hidden_why::kShoulderOnHead;
                if (lengthOf(g.leadElbow, g.head) < r)    why |= hidden_why::kElbowOnHead;
                if (lengthOf(g.leadHand, g.head) < r)     why |= hidden_why::kWristOnHead;
            }
            const float rawMin = float(cfg.minRawConf);
            if (g.leadShoulderRaw < rawMin || g.leadElbowRaw < rawMin || g.leadWristRaw < rawMin)
                why |= hidden_why::kLowConfidence;
            if (why != 0) {
                res.hiddenLeadElbow.push_back(g.t_us);
                res.hiddenLeadElbowWhy.push_back(why);   // one list per instant
            } else {
            const double upper = lengthOf(g.leadShoulder, g.leadElbow);
            const double fore  = lengthOf(g.leadElbow, g.leadHand);
            const double uRatio = res.ref.leadUpperArmLenPx > kEps ? upper / res.ref.leadUpperArmLenPx : 0.0;
            const double fRatio = res.ref.leadForearmLenPx > kEps ? fore / res.ref.leadForearmLenPx : 0.0;
            const bool inView = uRatio >= cfg.minUpperArmRatio && uRatio <= cfg.maxUpperArmRatio
                             && fRatio >= cfg.minForearmRatio && fRatio <= cfg.maxForearmRatio;
            if (inView) {
                const QPointF toShoulder(g.leadShoulder.x() - g.leadElbow.x(), g.leadShoulder.y() - g.leadElbow.y());
                const QPointF toWrist(g.leadHand.x() - g.leadElbow.x(), g.leadHand.y() - g.leadElbow.y());
                pushS(res.leadElbowFlex, res.sigma.leadElbowFlex, g.t_us,
                      180.0 - angleBetweenDeg(toShoulder, toWrist),
                      elbowBendSigmaDeg(g.leadShoulder, g.leadElbow, g.leadHand,
                                        g.leadShoulderSig, g.leadElbowSig, g.leadWristSig));
            } else {
                res.gatedLeadElbow.push_back(g.t_us);
                res.gatedLeadElbowArmRatio.push_back(uRatio);
                res.gatedLeadElbowForearmRatio.push_back(fRatio);
            }
            }
        }

        if (g.leadArmValid && g.spineValid) res.armTorsoSeenUs.push_back(g.t_us);
        if (g.shoulderLineValid && g.spineValid && toPctTrunk > 0.0) {
            pushS(res.trunkRise, res.sigma.trunkRise, g.t_us,
                  (lengthOf(g.neck, g.pelvis) * toPctTrunk) - 100.0,
                  quad2(g.neckSig, g.pelvisSig) * toPctTrunk);
        }
    }

    res.valid = !res.axisTilt.empty() || !res.shoulderPlane.empty() || !res.thoraxDrift.empty();
    return res;
}

std::vector<MetricSeries> buildUpperBodySeries(const UpperBodyResult &res,
                                               const std::vector<PhaseEvent> &phases)
{
    std::vector<MetricSeries> out;
    if (!res.valid || res.grid.empty())
        return out;

    const QString deg     = QStringLiteral("°");
    const QString pctSh   = QStringLiteral("% shoulder width");
    const QString pctSt   = QStringLiteral("% stance width");
    const QString pctArm  = QStringLiteral("% arm length");

    // res.maxBridgeUs is what turns the mask ON for this producer: buildChannelSeries fills every
    // grid sample as it always has, and marks the ones it had to bridge across a gated or absent run
    // longer than that. Empty mask when it never had to, which is the common case and is what keeps a
    // swing with no gated frame byte-identical. An invalid instant emits no phase sample.
    //
    // NOT named `emit`: that is a Qt keyword macro, and a lambda called `emit` does not survive
    // the preprocessor.
    //
    // The last argument is THIS channel's gated instants — refused geometry, which no bridge budget
    // may cover. A channel with no geometric gate passes none.
    // Which instants a channel is SAMPLED at is a domain statement, and it has to agree with the
    // catalogue's: five of these channels are authored Address->Impact (metric_presentation_honesty
    // design §5.1 — past impact the frontal projection of a turned trunk is rotation, not the
    // quantity), so they take the P1-P7 list and never emit a Finish sample. The default list still
    // carries Finish for the channels whose domain is the whole swing. Before 2026-09-04 all nine
    // sampled Finish, which put an out-of-domain reading into swing.json on every swing.
    static const std::vector<Phase> kP1toP7Samples{ Phase::Address, Phase::Top, Phase::Impact };

    // `p1toP7Domain` says the channel is an ADDRESS->IMPACT quantity, and it is the same statement
    // kP1toP7Samples makes about which instants it may be READ at, one layer down: PAST IMPACT the
    // sample is not a measurement of this quantity, so it is marked invalid and no reducer at any
    // query can draw on it. The pre-Address head is deliberately left open — applyPhaseDomainMask
    // carries the whole argument, including why this cannot be done inside a reducer instead. Behind
    // the same off-switch as the bridge mask: maxBridgeUs < 0 means "emit no validity mask at all",
    // for a parity run.
    //
    // `chSigma` is the channel's parallel per-sample σ (UpperBodySigma), or NULLPTR for a channel that
    // propagates none — `leadArmToTorso` is the one, and its series' sigma then stays unset, which is
    // the field's own meaning of "not characterised". A pointer rather than an empty vector so that
    // "none by design" and "the producer forgot to push one" cannot look the same; the second is
    // asserted in medianSigmaOverValid.
    const auto push = [&](const MetricChannel &ch, const std::vector<double> *chSigma,
                          const char *key, const char *label,
                          const QString &unit,
                          const std::vector<int64_t> &gated = kNoneGated,
                          const std::vector<Phase> &sampleAt = defaultPhaseSamples(),
                          bool p1toP7Domain = false, const char *routeId = nullptr) {
        MetricSeries m = buildChannelSeries(res.grid, ch, QString::fromLatin1(key),
                                            QString::fromUtf8(label), unit, phases,
                                            sampleAt, res.maxBridgeUs,
                                            res.bridgeSpacingFactor, gated);
        if (res.maxBridgeUs >= 0 && p1toP7Domain)
            applyPhaseDomainMask(m, phases);

        // ── The series' σ, LAST, because it is a median over what survived the masks above ────
        //
        // Both masks have run by here, so `m.valid` is final and medianSigmaOverValid skips exactly
        // the frames no reducer will read. Set only when at least one frame contributed: absent means
        // "not characterised" and a 0 would mean "measured perfectly", which is never true — the same
        // discipline body_rotation.cpp's IMU tier keeps when it leaves sigma unset.
        //
        // ⚠ σ NEVER TOUCHES value[] OR phaseSamples. It is read off a parallel track the value pass
        // filled, and it writes one optional field; a swing whose track carries no `smoothedAux`
        // serialises exactly as it did before this existed.
        //
        // ⚠ BEHIND THE SAME OFF-SWITCH AS BOTH MASKS, and that is not tidiness. `channel.maxBridgeUs`
        // < 0 means "emit no validity mask at all", and its ONLY purpose is to restore the pre-mask
        // bytes for a parity run. A σ written with no mask would be a median over the gated and
        // post-Impact frames too — a DIFFERENT number from the masked one, on a NEW key, in the one run
        // whose whole job is to reproduce the old bytes. Half-restoring is not restoring.
        //
        // `chSigma` is a POINTER because nullptr means "this channel propagates no σ by design" — an
        // empty vector would be indistinguishable from a producer that forgot to fill one. See
        // medianSigmaOverValid.
        if (!m.key.isEmpty() && res.maxBridgeUs >= 0) {
            if (const std::optional<double> sg = medianSigmaOverValid(m, ch.t_us, chSigma))
                m.sigma = *sg;
        }
        // The producer route, stated only on a key another producer also writes (leadArmFlexion: the
        // wrist sensors' elbow beside this face-on one), so applyLeadElbowSensorPrecedence and the
        // wrist assessment can tell the two apart. Every other channel keeps its bytes.
        if (routeId && !m.key.isEmpty()) m.routeId = QString::fromLatin1(routeId);
        appendIfProduced(out, std::move(m));
    };

    push(res.axisTilt,         &res.sigma.axisTilt,
         "secondaryAxisTilt",   "Secondary axis tilt",     deg,
         kNoneGated, kP1toP7Samples, /*p1toP7Domain=*/true);
    push(res.sideBend,         &res.sigma.sideBend,
         "spineSideBend",       "Spine side bend",         deg,
         res.gatedSideBend, kP1toP7Samples, /*p1toP7Domain=*/true);
    push(res.thoraxDrift,      &res.sigma.thoraxDrift,
         "thoraxLateralDrift",  "Thorax lateral drift",    pctSt,
         kNoneGated, kP1toP7Samples, /*p1toP7Domain=*/true);
    push(res.shoulderPlane,    &res.sigma.shoulderPlane,
         "shoulderPlaneAngle",  "Shoulder plane angle",    deg,
         res.gatedShoulderLine, kP1toP7Samples, /*p1toP7Domain=*/true);
    push(res.elbowLine,        &res.sigma.elbowLine,
         "elbowAlignment",      "Elbow alignment",         deg,
         res.gatedElbowLine, kP1toP7Samples, /*p1toP7Domain=*/true);
    // trailElbowHeight divides by the SHOULDER line's dx, so it is refused on exactly the instants
    // the shoulder line is (see the gate block in trackUpperBody).
    push(res.trailElbowHeight, &res.sigma.trailElbowHeight,
         "trailElbowHeight",    "Trail elbow height",      pctSh,
         res.gatedShoulderLine);
    push(res.leadHandWidth,    &res.sigma.leadHandWidth,
         "leadHandWidth",       "Swing width at the top",  pctArm);
    push(res.leadArmGap,       &res.sigma.leadArmGap,
         "leadUpperArmToChest", "Lead arm connection",     pctSh);
    // NO σ TRACK: leadArmToTorso propagates none on purpose — see the block above its push in
    // trackUpperBody, and UpperBodySigma. An empty vector leaves the series' sigma unset.
    // Refused where the shoulder line or the upper arm is out of view (body.armTorsoLines).
    push(res.leadArmToTorso,   nullptr,
         "leadArmToTorso",      "Lead arm to torso angle", deg,
         res.gatedArmTorso);
    // trunkRise reads the shoulder MIDPOINT, which stops being one point when the shoulder line
    // collapses, so it is refused on the shoulder line's instants; and it is an ADDRESS->IMPACT
    // quantity (posture_metrics_design.md §3.2) — past impact the chord is turning out of the image.
    push(res.trunkRise,        &res.sigma.trunkRise,
         "trunkRise",           "Trunk rise",              QStringLiteral("%"),
         res.gatedShoulderLine, kP1toP7Samples, /*p1toP7Domain=*/true);
    // leadArmFlexion's faceOn rung: refused where either arm segment is foreshortened
    // (body.leadElbowLines). The WHOLE SWING, not domain-masked — the fold after impact is its reading.
    // Estimated, not Direct (routeDirect stays false): a projection of a 3-D angle.
    // Both of its gates' instants: a foreshortened segment and a point not seen (D5 round 3).
    std::vector<int64_t> elbowRefused = res.gatedLeadElbow;
    elbowRefused.insert(elbowRefused.end(), res.hiddenLeadElbow.begin(), res.hiddenLeadElbow.end());
    std::sort(elbowRefused.begin(), elbowRefused.end());
    push(res.leadElbowFlex,    &res.sigma.leadElbowFlex,
         "leadArmFlexion",      "Lead arm (elbow), face-on", deg,
         elbowRefused, defaultPhaseSamples(), /*p1toP7Domain=*/false, "faceOn");
    return out;
}

std::vector<GateRecord> upperBodyGates(const UpperBodyResult &res, const std::vector<PhaseEvent> &phases)
{
    GateLog gates;
    gates.append(res.gates);

    const std::optional<int64_t> addrT = phaseTimeOpt(phases, Phase::Address);
    const std::optional<int64_t> impT  = phaseTimeOpt(phases, Phase::Impact);
    const int64_t impactUs = impT ? *impT : -1;
    // The Address → Impact domain the three tilts are read over (applyPhaseDomainMask); the whole
    // swing for trailElbowHeight, which is not domain-masked.
    const int64_t fromUs = addrT ? *addrT : INT64_MIN;
    const int64_t toUs   = impT ? *impT : INT64_MAX;

    const auto summary = [&](const QString &gate, const char *subject, const std::vector<int64_t> &gated,
                             const std::vector<int64_t> &seen, int64_t lo, int64_t hi,
                             const char *thresholdKey, double threshold) {
        QVariantMap ev = maskEvidence(gated, countWithin(seen, lo, hi), impactUs, lo, hi);
        if (ev.isEmpty()) return;
        ev.insert(QString::fromLatin1(thresholdKey), threshold);
        gates.fire(gate, QString::fromLatin1(subject), GateEffect::Masked, ev);
    };
    summary(PP_GATE("body.shoulderLine"), "shoulderPlaneAngle", res.gatedShoulderLine, res.shouldersSeenUs,
            fromUs, toUs, "minRatio", res.minShoulderSpanRatio);
    summary(PP_GATE("body.shoulderLine"), "trailElbowHeight", res.gatedShoulderLine, res.shouldersSeenUs,
            INT64_MIN, INT64_MAX, "minRatio", res.minShoulderSpanRatio);
    summary(PP_GATE("body.shoulderLine"), "trunkRise", res.gatedShoulderLine, res.shouldersSeenUs,
            fromUs, toUs, "minRatio", res.minShoulderSpanRatio);
    summary(PP_GATE("body.elbowLine"), "elbowAlignment", res.gatedElbowLine, res.elbowsSeenUs,
            fromUs, toUs, "minPx", res.minElbowSpanPx);
    // leadArmToTorso (body.armTorsoLines): the Masked summary over the whole swing — the channel is
    // not domain-masked, like trailElbowHeight — then a REFUSED record at each instant its grade
    // reads where the gate leaves the grader nothing. A Masked record names no reason for a missing
    // grade (gate_catalogue.h primaryRecord: "a masked band refuses nothing"); this one does.
    // The instants are the catalogue's (metric_catalogue_manifest.cpp `.phases`: Impact and P8 —
    // m_leadArmToTorso is the change P7 → P8), and "nothing" is the grader's own rule: fewer than
    // sampler::kMinValidSamples ungated frames within ±sampler::kWindowHalfUs (measure_sample.cpp).
    {
        const auto withThresholds = [&](QVariantMap ev) {
            ev.insert(QStringLiteral("minRatio"), res.minShoulderSpanRatio);
            ev.insert(QStringLiteral("minArmRatio"), res.minUpperArmRatio);
            ev.insert(QStringLiteral("maxArmRatio"), res.maxUpperArmRatio);
            return ev;
        };
        const QVariantMap ev = maskEvidence(res.gatedArmTorso, countWithin(res.armTorsoSeenUs, INT64_MIN, INT64_MAX),
                                            impactUs);
        if (!ev.isEmpty())
            gates.fire(PP_GATE("body.armTorsoLines"), QStringLiteral("leadArmToTorso"), GateEffect::Masked,
                       withThresholds(ev));
        const int64_t h = tuned::sampler::kWindowHalfUs;
        for (const auto &[phase, code] : { std::pair{ Phase::Impact, "P7" },
                                           std::pair{ Phase::ShaftParallelThrough, "P8" } }) {
            const std::optional<int64_t> t = phaseTimeOpt(phases, phase);
            if (!t) continue;
            const int nGated = countWithin(res.gatedArmTorso, *t - h, *t + h);
            if (nGated == 0) continue;
            // The frames that COULD have read (lead arm and spine confident), as the Masked summary
            // counts them — not every grid frame: an unreadable frame pushes no value and is not
            // gated, so the grader has nothing there either.
            const int nFrames = countWithin(res.armTorsoSeenUs, *t - h, *t + h);
            if (nFrames - nGated >= tuned::sampler::kMinValidSamples) continue;
            QVariantMap at = maskEvidence(res.gatedArmTorso, nFrames, impactUs, *t - h, *t + h);
            if (at.isEmpty()) continue;
            // The two ratios at the refused frame nearest the instant: which half refused it.
            size_t best = 0;
            for (size_t i = 1; i < res.gatedArmTorso.size(); ++i)
                if (std::llabs(res.gatedArmTorso[i] - *t) < std::llabs(res.gatedArmTorso[best] - *t)) best = i;
            at.insert(QStringLiteral("phase"), QString::fromLatin1(code));
            at.insert(QStringLiteral("shoulderRatio"), res.gatedArmTorsoShoulderRatio[best]);
            at.insert(QStringLiteral("armRatio"), res.gatedArmTorsoArmRatio[best]);
            gates.fire(PP_GATE("body.armTorsoLines"), QStringLiteral("leadArmToTorso"), GateEffect::Refused,
                       withThresholds(at));
        }
    }
    // leadArmFlexion's faceOn rung has TWO gates: body.leadElbowLines (a segment foreshortened) and, since
    // D5 round 3, body.leadArmHidden (a point on the head, or scored low by the pose model). Each leaves:
    //   * a Masked summary over the whole swing;
    //   * a REFUSED record at Impact (phase P7) — the instant m_leadElbowFoldAfterImpact is measured from —
    //     when the two gates together leave fewer than sampler::kMinValidSamples readable frames within
    //     ±sampler::kWindowHalfUs (FA's rule), from the gate that refused the most frames there;
    //   * a REFUSED record for the span after impact (phase afterImpact, tuned::upperBody::kElbowFoldSpanUs)
    //     when the frames the gates left there leave a stretch longer than tuned::reduce::kMsSpanMaxGapUs
    //     unread, ends included — the grader's own coverage rule (measure_sample.cpp), so the record names
    //     the reason the fold is not assessable — from the gate that refused the most frames in the span.
    // "Left" means every grid frame neither gate refused: a frame the detector lost is bridged by the
    // series, as the grader sees it.
    {
        std::vector<int64_t> refusedAll = res.gatedLeadElbow;
        refusedAll.insert(refusedAll.end(), res.hiddenLeadElbow.begin(), res.hiddenLeadElbow.end());
        std::sort(refusedAll.begin(), refusedAll.end());
        const auto refusedAt = [&](int64_t t) { return std::binary_search(refusedAll.begin(), refusedAll.end(), t); };
        const int64_t h = tuned::sampler::kWindowHalfUs;
        // Impact: does the grader have a reading there?
        bool impactEmpty = false;
        if (impT) {
            const int nFrames = countWithin(res.leadElbowSeenUs, *impT - h, *impT + h);
            const int nRefused = countWithin(refusedAll, *impT - h, *impT + h);
            impactEmpty = nRefused > 0 && nFrames - nRefused < tuned::sampler::kMinValidSamples;
        }
        // The span after impact: the longest stretch with no frame the gates left.
        double gapMs = 0.0;
        if (impT) {
            const int64_t lo = *impT, hi = *impT + tuned::upperBody::kElbowFoldSpanUs;
            int64_t prev = lo, worst = 0;
            for (int64_t t : res.grid) {
                if (t < lo || t > hi || refusedAt(t)) continue;
                worst = std::max(worst, t - prev);
                prev = t;
            }
            worst = std::max(worst, hi - prev);
            gapMs = double(worst) / 1000.0;
        }
        const bool spanUncovered = impT && gapMs * 1000.0 > double(tuned::reduce::kMsSpanMaxGapUs);
        // ONE GATE NAMES EACH REFUSAL: the one that refused the most frames in that stretch, ties to
        // body.leadArmHidden (it is checked first per frame, and an unseen arm is the likelier cause).
        // primaryRecord keeps the first of equal rank, so firing both would let the order of these calls,
        // not the frames, decide which reason a golfer reads (review, D5 round 3).
        const auto ownerOf = [&](int64_t lo, int64_t hi) {
            return countWithin(res.hiddenLeadElbow, lo, hi) >= countWithin(res.gatedLeadElbow, lo, hi)
                       ? QStringLiteral("body.leadArmHidden") : QStringLiteral("body.leadElbowLines");
        };
        const QString impactOwner = impT ? ownerOf(*impT - h, *impT + h) : QString();
        const QString spanOwner   = impT ? ownerOf(*impT, *impT + tuned::upperBody::kElbowFoldSpanUs) : QString();

        const auto fireGate = [&](const QString &gate, const std::vector<int64_t> &mine,
                                  const std::function<void(QVariantMap &, size_t)> &atNearest,
                                  const std::function<void(QVariantMap &)> &thresholds) {
            QVariantMap ev = maskEvidence(mine, countWithin(res.leadElbowSeenUs, INT64_MIN, INT64_MAX), impactUs);
            if (ev.isEmpty()) return;
            thresholds(ev);
            gates.fire(gate, QStringLiteral("leadArmFlexion"), GateEffect::Masked, ev);
            if (!impT) return;
            const auto nearest = [&](int64_t t) {
                size_t best = 0;
                for (size_t i = 1; i < mine.size(); ++i)
                    if (std::llabs(mine[i] - t) < std::llabs(mine[best] - t)) best = i;
                return best;
            };
            if (impactEmpty && gate == impactOwner && countWithin(mine, *impT - h, *impT + h) > 0) {
                QVariantMap at = maskEvidence(mine, countWithin(res.leadElbowSeenUs, *impT - h, *impT + h), impactUs,
                                              *impT - h, *impT + h);
                at.insert(QStringLiteral("phase"), QStringLiteral("P7"));
                atNearest(at, nearest(*impT));
                thresholds(at);
                gates.fire(gate, QStringLiteral("leadArmFlexion"), GateEffect::Refused, at);
            }
            const int64_t hi = *impT + tuned::upperBody::kElbowFoldSpanUs;
            if (spanUncovered && gate == spanOwner && countWithin(mine, *impT, hi) > 0) {
                QVariantMap sp = maskEvidence(mine, countWithin(res.leadElbowSeenUs, *impT, hi), impactUs, *impT, hi);
                sp.insert(QStringLiteral("phase"), QStringLiteral("afterImpact"));
                sp.insert(QStringLiteral("gapMs"), gapMs);
                sp.insert(QStringLiteral("maxGapMs"), double(tuned::reduce::kMsSpanMaxGapUs) / 1000.0);
                // the refused frame nearest the middle of the span: the stretch the fold is read in
                atNearest(sp, nearest(*impT + tuned::upperBody::kElbowFoldSpanUs / 2));
                thresholds(sp);
                gates.fire(gate, QStringLiteral("leadArmFlexion"), GateEffect::Refused, sp);
            }
        };
        fireGate(PP_GATE("body.leadElbowLines"), res.gatedLeadElbow,
                 [&](QVariantMap &ev, size_t i) {
                     ev.insert(QStringLiteral("armRatio"), res.gatedLeadElbowArmRatio[i]);
                     ev.insert(QStringLiteral("forearmRatio"), res.gatedLeadElbowForearmRatio[i]);
                 },
                 [&](QVariantMap &ev) {
                     ev.insert(QStringLiteral("minArmRatio"), res.minUpperArmRatio);
                     ev.insert(QStringLiteral("maxArmRatio"), res.maxUpperArmRatio);
                     ev.insert(QStringLiteral("minForearmRatio"), res.minForearmRatio);
                     ev.insert(QStringLiteral("maxForearmRatio"), res.maxForearmRatio);
                 });
        // body.leadArmHidden's evidence: how many of its frames each reason refused (a frame may have
        // several), and at the record's frame, the code of the first reason (shoulder on the head first:
        // it is the case the gate exists for).
        const auto countWhy = [&](uint8_t bit, int64_t lo, int64_t hi) {
            int n = 0;
            for (size_t i = 0; i < res.hiddenLeadElbow.size(); ++i)
                if (res.hiddenLeadElbow[i] >= lo && res.hiddenLeadElbow[i] <= hi && (res.hiddenLeadElbowWhy[i] & bit)) ++n;
            return n;
        };
        fireGate(PP_GATE("body.leadArmHidden"), res.hiddenLeadElbow,
                 [&](QVariantMap &ev, size_t i) {
                     const uint8_t w = res.hiddenLeadElbowWhy[i];
                     ev.insert(QStringLiteral("code"),
                               (w & hidden_why::kShoulderOnHead) ? QStringLiteral("shoulderOnHead")
                               : (w & (hidden_why::kElbowOnHead | hidden_why::kWristOnHead)) ? QStringLiteral("armOnHead")
                                                                                             : QStringLiteral("lowConfidence"));
                 },
                 [&](QVariantMap &ev) {
                     // the reasons' counts over the record's own stretch (the whole swing for the summary)
                     const QString ph = ev.value(QStringLiteral("phase")).toString();
                     int64_t lo = INT64_MIN, hi = INT64_MAX;
                     if (impT && ph == QLatin1String("P7"))          { lo = *impT - h; hi = *impT + h; }
                     if (impT && ph == QLatin1String("afterImpact")) { lo = *impT; hi = *impT + tuned::upperBody::kElbowFoldSpanUs; }
                     const auto put = [&](const char *key, int n) { if (n > 0) ev.insert(QString::fromLatin1(key), n); };
                     put("shoulderOnHead", countWhy(hidden_why::kShoulderOnHead, lo, hi));
                     put("elbowOnHead",    countWhy(hidden_why::kElbowOnHead, lo, hi));
                     put("wristOnHead",    countWhy(hidden_why::kWristOnHead, lo, hi));
                     put("lowConfidence",  countWhy(hidden_why::kLowConfidence, lo, hi));
                     ev.insert(QStringLiteral("headRadius"), res.headRadiusRatio);
                     ev.insert(QStringLiteral("minRawConf"), res.minRawConf);
                 });
    }
    {
        // Either line may refuse spineSideBend; the evidence carries both thresholds.
        QVariantMap ev = maskEvidence(res.gatedSideBend, countWithin(res.trunkSeenUs, fromUs, toUs), impactUs,
                                      fromUs, toUs);
        if (!ev.isEmpty()) {
            ev.insert(QStringLiteral("minShoulderRatio"), res.minShoulderSpanRatio);
            ev.insert(QStringLiteral("minHipRatio"), res.minHipSpanRatio);
            gates.fire(PP_GATE("body.sideBendLines"), QStringLiteral("spineSideBend"), GateEffect::Masked, ev);
        }
    }
    return gates.take();
}

int applyLeadElbowSensorPrecedence(std::vector<MetricSeries> &series, std::vector<GateRecord> &gates)
{
    const auto isCamera = [](const MetricSeries &m) {
        return m.key == QLatin1String("leadArmFlexion") && m.routeId == QLatin1String("faceOn");
    };
    const bool sensor = std::any_of(series.begin(), series.end(), [&](const MetricSeries &m) {
        return m.key == QLatin1String("leadArmFlexion") && !isCamera(m);
    });
    if (!sensor) return 0;
    const auto before = series.size();
    series.erase(std::remove_if(series.begin(), series.end(), isCamera), series.end());
    // The 3-D fit's measured lead elbow (leadArmFlexion3d, Skeleton3dBodyStage; plane stack D2) is the rung
    // between the two: wristImus > 3-D > face-on. It writes no records of its own.
    // bent_lead_arm's change from the address hold (leadElbowBendChange3d, D6) is built from it and goes with it.
    series.erase(std::remove_if(series.begin(), series.end(),
                                [](const MetricSeries &m) {
                                    return m.key == QLatin1String("leadArmFlexion3d")
                                        || m.key == QLatin1String("leadElbowBendChange3d");
                                }),
                 series.end());
    const int dropped = int(before - series.size());
    // The camera's records stop speaking for the key the sensor now answers — WHETHER OR NOT a face-on
    // series was written: a face-on address refusal (body.upperAddress / body.shoulderSpan) writes no
    // series and still covers leadArmFlexion, and must not stand beside the sensors' graded value (the
    // shape W4 fixed for the trunk sensors).
    gates.erase(std::remove_if(gates.begin(), gates.end(), [](const GateRecord &g) {
                    return g.gate == QLatin1String("body.leadElbowLines")
                        || g.gate == QLatin1String("body3d.leadElbowUnmeasured");   // D6: the 3-D change's refusal
                }),
                gates.end());
    for (GateRecord &g : gates)
        if (g.gate == QLatin1String("body.upperAddress") || g.gate == QLatin1String("body.shoulderSpan"))
            g.covers.removeAll(QStringLiteral("leadArmFlexion"));
    return dropped;
}

} // namespace pinpoint::analysis
