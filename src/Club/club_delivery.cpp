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

#include "club_delivery.h"

#include "../Analysis/det_rng.h" // deterministic Monte Carlo (uncertainty design principle 7)
#include "shaft_sigma.h"
#include "shaft_synthesis.h"   // synthDraws

#include <algorithm>
#include <cmath>
#include <limits>

namespace pinpoint::analysis {
namespace {

constexpr double kRadToDeg = 57.29577951308232;
constexpr double kEps      = 1e-9;
constexpr double kMmPerIn  = 25.4;

// A sample whose headPx is a real measurement. `ShaftHeadProjected` means headPx was reconstructed
// from the grip along θ at an assumed club length; differentiating that yields the grip's motion
// wearing the clubhead's name. `headConf < 0` means the Stage-2 head pass never ran at all, which
// is a different failure from a low-confidence measurement and is refused the same way.
bool headMeasured(const ShaftSample2D &s, double headConfMin)
{
    if (s.flags & ShaftHeadProjected) return false;
    if (s.headConf < 0.f)             return false;
    return double(s.headConf) >= headConfMin;
}

// Where the arc bottoms out, in image x: the CENTRE of the circle through the arc's last stretch
// before impact.
//
// A clubhead near the bottom of the swing travels a near-circular arc about the swing centre, and
// the bottom of a circle lies directly below its centre — so the low point is the centre's x, and
// finding it does not need the arc to have been seen turning over. That is the point of fitting
// rather than searching: the old reading took the LARGEST y inside ±60 ms of impact and refused when
// it sat on an end of the window, and on 65 of 160 library swings it did (issue #92) — either the
// synthesized arc stops at impact (no measured P8 to bridge to, synth.measuredEndAfterImpact) or it
// carries on falling after impact along something that is not the club. Only the arc BEFORE impact
// is used here, so neither can reach the answer.
//
// Algebraic (Kåsa) least-squares fit in coordinates centred on the samples' mean, for conditioning.
// Refused (false) when the samples are collinear or the fit is degenerate, when the radius is not a
// swing's (outside [minRadiusPx, maxRadiusPx]: a near-straight run fits a circle of any size, and its
// centre is noise), or when the centre is BELOW the arc in the image — an arc curving the wrong way
// has no bottom to report.
bool arcCircleX(const std::vector<const ShaftSample2D *> &win, double minRadiusPx, double maxRadiusPx,
                double &lowX)
{
    if (win.size() < 3)
        return false;
    double mx = 0.0, my = 0.0;
    for (const ShaftSample2D *s : win) { mx += s->headPx.x(); my += s->headPx.y(); }
    mx /= double(win.size());
    my /= double(win.size());

    // Normal equations of  u² + v² = 2·a·u + 2·b·v + c  over the centred points (u, v).
    double suu = 0, suv = 0, svv = 0, su = 0, sv = 0, n = 0, szu = 0, szv = 0, sz = 0;
    for (const ShaftSample2D *s : win) {
        const double u = s->headPx.x() - mx, v = s->headPx.y() - my, z = u * u + v * v;
        suu += u * u; suv += u * v; svv += v * v; su += u; sv += v; n += 1.0;
        szu += z * u; szv += z * v; sz += z;
    }
    // [4suu 4suv 2su; 4suv 4svv 2sv; 2su 2sv n] · [a b c]ᵀ = [2szu 2szv sz]ᵀ, solved by Cramer.
    const double M[3][3] = { { 4 * suu, 4 * suv, 2 * su }, { 4 * suv, 4 * svv, 2 * sv }, { 2 * su, 2 * sv, n } };
    const double r[3]    = { 2 * szu, 2 * szv, sz };
    const auto det3 = [](const double m[3][3]) {
        return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1])
             - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
             + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    };
    const double D = det3(M);
    if (!std::isfinite(D) || std::abs(D) <= kEps)
        return false;
    double Ma[3][3], Mb[3][3], Mc[3][3];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) { Ma[i][j] = M[i][j]; Mb[i][j] = M[i][j]; Mc[i][j] = M[i][j]; }
    for (int i = 0; i < 3; ++i) { Ma[i][0] = r[i]; Mb[i][1] = r[i]; Mc[i][2] = r[i]; }
    const double ca = det3(Ma) / D, cb = det3(Mb) / D, cc = det3(Mc) / D;
    const double R2 = cc + ca * ca + cb * cb;
    if (!std::isfinite(ca) || !std::isfinite(cb) || !(R2 > 0.0))
        return false;
    const double R = std::sqrt(R2);
    if (R < minRadiusPx || R > maxRadiusPx)
        return false;
    if (!(cb < 0.0))                                    // image y grows DOWN: the centre must sit above
        return false;
    lowX = mx + ca;
    return true;
}

} // namespace

ClubDeliveryResult trackClubDelivery(const ShaftTrack2D &shaft, const std::vector<PhaseEvent> &phases,
                                     QPointF addressBallPx, bool ballValid, double mmPerPx,
                                     const ClubDeliveryConfig &cfg)
{
    ClubDeliveryResult res;
    if (!shaft.valid)
        return res;

    // The measured-head subset, in time order. The top-of-swing angle reads this and only this;
    // the attack angle reads it only as the fallback when the synth arc is not continuous through
    // impact; the low point deliberately does not (see the header). An empty subset is therefore not a refusal —
    // it silences the angles and leaves the arc channel to answer for itself.
    std::vector<const ShaftSample2D *> m;
    m.reserve(shaft.samples.size());
    for (const ShaftSample2D &s : shaft.samples)
        if (headMeasured(s, cfg.headConfMin))
            m.push_back(&s);

    res.grid.reserve(m.size());
    for (const ShaftSample2D *s : m)
        res.grid.push_back(s->t_us);

    // ── shaftAngleVsHorizontal ─────────────────────────────────────────────────────────────────
    // The grip→head vector against the image horizontal. ZERO IS PARALLEL TO THE GROUND and
    // POSITIVE IS PAST PARALLEL: image y grows downward, so a head sitting BELOW the grip — which
    // at the top of the backswing means the club has travelled past horizontal — gives a positive
    // numerator. The denominator is the ABSOLUTE horizontal separation, so the reading does not
    // invert for a left-handed golfer or a mirrored camera; this is the same sign-stability rule
    // `hipLineTilt` and the upper-body body lines are built on.
    //
    // Taken from the head rather than from θ because θ carries a 180° line ambiguity that the
    // endpoint pair resolves for free.
    for (const ShaftSample2D *s : m) {
        const double dx = std::abs(s->headPx.x() - s->gripPx.x());
        if (dx <= kEps) continue;                       // shaft vertical — no angle to horizontal
        res.shaftVsHorizontal.push(s->t_us,
                                   std::atan2(s->headPx.y() - s->gripPx.y(), dx) * kRadToDeg);
    }

    // THE HEAD-CONFIDENCE GATE'S RECORD (trust stage 6): the samples the top angle could not read
    // because the clubhead was not measured with confidence (below headConfMin, projected from the
    // grip, or never searched for) — one summary; refused when it left the reading nothing.
    GateLog gates;
    const int64_t gateImpactUs = phaseTime(phases, Phase::Impact, -1);
    {
        std::vector<int64_t> unseen;
        int lowConf = 0;
        for (const ShaftSample2D &s : shaft.samples)
            if (!headMeasured(s, cfg.headConfMin)) {
                unseen.push_back(s.t_us);
                if (!(s.flags & ShaftHeadProjected) && s.headConf >= 0.f) ++lowConf;
            }
        QVariantMap ev = maskEvidence(unseen, int(shaft.samples.size()), gateImpactUs);
        if (!ev.isEmpty()) {
            ev.insert(QStringLiteral("lowConfFrames"), lowConf);
            ev.insert(QStringLiteral("minHeadConf"), cfg.headConfMin);
            gates.fire(PP_GATE("club.headConfidence"), QStringLiteral("shaftAngleVsHorizontal"),
                       res.shaftVsHorizontal.empty() ? GateEffect::Refused : GateEffect::Masked, ev);
        }
    }

    // ── attackAngle ────────────────────────────────────────────────────────────────────────────
    // The vertical angle of the head's velocity, POSITIVE FOR A MORE UPWARD STRIKE. A centred
    // difference over ±velHalfSpan samples: one frame either side of a ~9 px head is mostly
    // quantisation, and this quantity is read at a single instant where that noise would land
    // squarely on the answer.
    //
    // The denominator is |dx| again. The head's horizontal travel at impact is along the target
    // line, and which way along it the golfer is swinging has no bearing on whether the strike is
    // descending — folding the sign out is what makes this handedness- and mirror-free.
    //
    // WHICH HEADS (2026-09-23). The synthesized arc when it covers impact, else the measured heads.
    // The measured head is a blur at impact: on the 13 launch-monitor-paired corpus swings the median
    // number of measured heads within ±2 frames of impact was zero, so the difference spanned samples
    // ~220 px apart, well away from the strike, and read a median 36° from the GC Quad. The arc is the
    // dense interpolation between the located P-anchors (the same channel lowPointAhead reads) and
    // read within a median 3.6° (tools/metrics/club_arc_sensitivity.py; swing_storage_impl.md
    // Phase 2 stage 5). A swing whose arc does not reach impact keeps the measured estimate rather
    // than losing the metric.
    std::vector<const ShaftSample2D *> src = m;
    bool attackFromArc = false;          // the record below: which heads the attack angle read
    int  attackArcNear = 0;
    {
        const int64_t aimUs = phaseTime(phases, Phase::Impact, -1);
        std::vector<const ShaftSample2D *> arc;
        int near = 0;
        for (const ShaftSample2D &s : shaft.synth) {
            arc.push_back(&s);
            if (aimUs >= 0 && std::llabs(s.t_us - aimUs) <= cfg.attackSynthWinUs) ++near;
        }
        attackArcNear = near;
        if (near >= cfg.attackSynthMinSamples) {
            std::sort(arc.begin(), arc.end(),
                      [](const ShaftSample2D *x, const ShaftSample2D *y) { return x->t_us < y->t_us; });
            // …and only where the arc is CONTINUOUS through the steps the difference spans. The arc
            // is an interpolation between located anchors, and a mislocated anchor near impact puts
            // a jump in it: on 16 Sept Wrist_01 swing_0001 the head leaps ~310 px in one 4 ms step,
            // and the difference across it read +82° against the GC Quad's −3°. A step more than
            // attackSynthMaxStepRatio × the window's median step is a jump, not motion; the swing
            // then keeps the measured estimate. (25 of the 143 library swings, 23 Sept.)
            std::vector<double> steps;
            size_t i0 = 0;
            for (size_t i = 0; i < arc.size(); ++i) {
                if (std::llabs(arc[i]->t_us - aimUs) < std::llabs(arc[i0]->t_us - aimUs)) i0 = i;
                if (i > 0 && std::llabs(arc[i]->t_us - aimUs) <= cfg.attackSynthWinUs
                          && std::llabs(arc[i - 1]->t_us - aimUs) <= cfg.attackSynthWinUs)
                    steps.push_back(std::hypot(arc[i]->headPx.x() - arc[i - 1]->headPx.x(),
                                               arc[i]->headPx.y() - arc[i - 1]->headPx.y()));
            }
            bool continuous = !steps.empty();
            if (continuous) {
                std::vector<double> sorted = steps;
                std::nth_element(sorted.begin(), sorted.begin() + sorted.size() / 2, sorted.end());
                const double med = sorted[sorted.size() / 2];
                const size_t lo = i0 > size_t(cfg.velHalfSpan + 1) ? i0 - size_t(cfg.velHalfSpan + 1) : 0;
                const size_t hi = std::min(arc.size() - 1, i0 + size_t(cfg.velHalfSpan + 1));
                for (size_t i = lo + 1; i <= hi && continuous; ++i) {
                    const double st = std::hypot(arc[i]->headPx.x() - arc[i - 1]->headPx.x(),
                                                 arc[i]->headPx.y() - arc[i - 1]->headPx.y());
                    continuous = med > kEps && st <= cfg.attackSynthMaxStepRatio * med;
                }
            }
            if (continuous) {
                src = std::move(arc);
                attackFromArc = true;
            }
        }
    }
    if (int(src.size()) > 2 * cfg.velHalfSpan) {
        for (int i = cfg.velHalfSpan; i < int(src.size()) - cfg.velHalfSpan; ++i) {
            const ShaftSample2D *a = src[size_t(i - cfg.velHalfSpan)];
            const ShaftSample2D *b = src[size_t(i + cfg.velHalfSpan)];
            const double dx = std::abs(b->headPx.x() - a->headPx.x());
            const double dy = b->headPx.y() - a->headPx.y();
            if (dx <= kEps && std::abs(dy) <= kEps) continue;   // stationary — no direction
            // −dy because image y grows downward: rising in the world is falling in y.
            res.attackAngle.push(src[size_t(i)]->t_us, std::atan2(-dy, dx) * kRadToDeg);
        }
    }
    // THE ATTACK ANGLE'S FALLBACK (trust stage 6): the arc did not reach impact densely enough
    // (`fewArc`) or jumped through it (`jump`), so the measured heads answered — a lower trust
    // (a median 36° from the launch monitor against the arc's 3.6°), or nothing when they could not.
    if (!attackFromArc) {
        QVariantMap ev{ { QStringLiteral("code"), attackArcNear >= cfg.attackSynthMinSamples ? QStringLiteral("jump")
                                                                                              : QStringLiteral("fewArc") },
                        { QStringLiteral("arcSamples"), attackArcNear },
                        { QStringLiteral("minArcSamples"), cfg.attackSynthMinSamples },
                        { QStringLiteral("winMs"), double(cfg.attackSynthWinUs) / 1000.0 },
                        { QStringLiteral("maxStepRatio"), cfg.attackSynthMaxStepRatio },
                        { QStringLiteral("measuredHeads"), int(m.size()) } };
        gates.fire(PP_GATE("club.attackFallback"), QStringLiteral("attackAngle"),
                   res.attackAngle.empty() ? GateEffect::Refused : GateEffect::Downgraded, ev);
    }

    // ── lowPointAhead ──────────────────────────────────────────────────────────────────────────
    // Where the arc bottoms out, relative to the ball, along the target line, in signed inches.
    //
    // READ OFF THE SYNTHESIZED ARC, not the measured heads — `shaft.synth`, the dense Hermite
    // interpolation between the located P-anchors that the club overlay draws. The header carries
    // the corpus evidence for why; the short version is that the head detector does not hold a lock
    // through impact on real swings.
    //
    // FITTED, NOT SEARCHED (issue #92): the circle through the arc's last lowPointWinUs BEFORE
    // impact, whose centre is above the bottom of the swing (arcCircleX). Nothing after impact is
    // read, so the arc stopping at impact or wandering after it cannot take the reading away.
    //
    // Needs the ball (the reference the answer is stated against) and the ruler (the unit it is
    // stated in). Missing either suppresses THIS metric only — the two angles above are scale-free
    // and are unaffected.
    const int64_t impactUs = phaseTime(phases, Phase::Impact, -1);
    // THE LOW POINT'S REFUSALS (trust stage 6), each with its code: no ball or no ruler to state it
    // against (`noBall`), too few arc samples before impact (`fewSamples`), no circle a swing could
    // make through them — straight, the wrong way up, or an implausible radius (`badArc`), no sweep to
    // give the target direction (`noSweep`). Only with an Impact on the ladder: without one there is
    // no window to ask about.
    const auto refuseLowPoint = [&](const char *code, int samples) {
        gates.fire(PP_GATE("club.lowPoint"), QStringLiteral("lowPointAhead"), GateEffect::Refused,
                   QVariantMap{ { QStringLiteral("code"), QString::fromLatin1(code) },
                     { QStringLiteral("samples"), samples },
                     { QStringLiteral("minSamples"), cfg.lowPointMinSamples },
                     { QStringLiteral("winMs"), double(cfg.lowPointWinUs) / 1000.0 } });
    };
    if (impactUs >= 0 && !(ballValid && mmPerPx > 0.0))
        refuseLowPoint("noBall", 0);
    else if (impactUs >= 0 && shaft.synth.empty())
        refuseLowPoint("fewSamples", 0);
    if (ballValid && mmPerPx > 0.0 && impactUs >= 0 && !shaft.synth.empty()) {
        std::vector<const ShaftSample2D *> win;
        for (const ShaftSample2D &s : shaft.synth)
            if (s.t_us <= impactUs && impactUs - s.t_us <= cfg.lowPointWinUs)
                win.push_back(&s);

        double lowX = 0.0;
        const bool enough = int(win.size()) >= cfg.lowPointMinSamples;
        const bool fitted = enough
            && arcCircleX(win, cfg.lowPointMinRadiusM * 1000.0 / mmPerPx,
                          cfg.lowPointMaxRadiusM * 1000.0 / mmPerPx, lowX);
        if (!enough)
            refuseLowPoint("fewSamples", int(win.size()));
        else if (!fitted)
            refuseLowPoint("badArc", int(win.size()));
        if (fitted) {
            // Which image direction the target is. Taken from the CLUBHEAD ITSELF — approaching
            // impact the head is travelling toward the target, so the sign of its horizontal
            // displacement across the window IS the target direction. Deliberately not taken from
            // pose handedness: this module never sees a skeleton, and the head's own motion is both
            // more direct and immune to a mirrored camera.
            const double sweep = win.back()->headPx.x() - win.front()->headPx.x();
            if (std::abs(sweep) > kEps) {
                const double targetSign = sweep > 0.0 ? 1.0 : -1.0;
                res.lowPointIn      = targetSign * (lowX - addressBallPx.x()) * mmPerPx / kMmPerIn;
                res.lowPointTUs     = impactUs;
                res.lowPointSigmaIn = cfg.lowPointSigmaIn;
                res.lowPointValid   = true;
            } else {
                refuseLowPoint("noSweep", int(win.size()));
            }
        }
    }

    res.valid = !res.shaftVsHorizontal.empty() || !res.attackAngle.empty() || res.lowPointValid;
    res.gates = gates.take();
    return res;
}

std::vector<MetricSeries> buildClubDeliverySeries(const ClubDeliveryResult &res,
                                                  const std::vector<PhaseEvent> &phases)
{
    std::vector<MetricSeries> out;
    if (!res.valid)
        return out;

    const QString deg = QStringLiteral("°");

    appendIfProduced(out, buildChannelSeries(res.grid, res.shaftVsHorizontal,
                                             QStringLiteral("shaftAngleVsHorizontal"),
                                             QStringLiteral("Shaft angle at the top"), deg, phases));

    // attackAngle and lowPointAhead are PointInTime metrics: an empty curve carrying one Impact
    // phaseSample, the representation foot_metrics' setup scalars already use and that every
    // reader and the swing.json writer already handle without a non-empty-curve assumption.
    const int64_t fallbackUs = res.grid.empty() ? 0 : res.grid.front();
    const int64_t impactUs   = phaseTime(phases, Phase::Impact, fallbackUs);

    // `sigma` <= 0 leaves the field ABSENT, which is the "not characterised" state — never 0,
    // which would read as an exact measurement.
    const auto pushScalar = [&](bool ok, const QString &key, const QString &label,
                                const QString &unit, double value, int64_t atUs,
                                double sigma = 0.0) {
        if (!ok) return;
        MetricSeries m;
        m.key   = key;
        m.label = label;
        m.unit  = unit;
        if (sigma > 0.0)
            m.sigma = sigma;
        m.phaseSamples.push_back({ Phase::Impact, atUs, value, QString() });
        out.push_back(std::move(m));
    };

    if (!res.attackAngle.empty()) {
        const double aa = interpChannel(res.attackAngle.t_us, res.attackAngle.value, impactUs);
        pushScalar(true, QStringLiteral("attackAngle"), QStringLiteral("Attack angle"), deg, aa,
                   impactUs);
    }
    // The health warning rides WITH the number: an estimate off a fitted arc, ±lowPointSigmaIn.
    pushScalar(res.lowPointValid, QStringLiteral("lowPointAhead"), QStringLiteral("Low point"),
               QStringLiteral("in"), res.lowPointIn, res.lowPointTUs, res.lowPointSigmaIn);
    return out;
}

namespace {
double robustSdCd(std::vector<double> v)
{
    v.erase(std::remove_if(v.begin(), v.end(), [](double x) { return !std::isfinite(x); }), v.end());
    if (v.size() < 5) return std::numeric_limits<double>::quiet_NaN();
    std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end());
    const double med = v[v.size() / 2];
    for (double &x : v) x = std::abs(x - med);
    std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end());
    return 1.4826 * v[v.size() / 2];
}
} // namespace

void addClubDeliverySigma(std::vector<MetricSeries> &series, const ShaftTrack2D &shaft,
                          const std::vector<PhaseEvent> &phases, QPointF addressBallPx, bool ballValid,
                          double mmPerPx, double ballSigmaPx, const PoseTrack2D *pose,
                          const UncertaintyConfig &unc, const ClubDeliveryConfig &cfg)
{
    if (!unc.enabled) return;
    // ── the top angle: atan2(dy, |dx|) of the measured head against the grip ─────────────────
    for (MetricSeries &m : series) {
        if (m.key != QStringLiteral("shaftAngleVsHorizontal")) continue;
        for (PhaseSample &ps : m.phaseSamples) {
            const ShaftSample2D *best = nullptr;
            int64_t bd = std::numeric_limits<int64_t>::max();
            for (const ShaftSample2D &s : shaft.samples) {
                if (!headMeasured(s, cfg.headConfMin)) continue;
                const int64_t d = std::llabs(s.t_us - ps.t_us);
                if (d < bd) { bd = d; best = &s; }
            }
            if (!best || best->headSigmaPx <= 0.f) continue;
            const double sGrip = pose ? shaftsigma::gripSigmaPx(*pose, ps.t_us) : tuned::uncertainty::kGripSigmaFloorPx;
            const double L = std::hypot(best->headPx.x() - best->gripPx.x(), best->headPx.y() - best->gripPx.y());
            const double sig = shaftsigma::lineAngleSigmaDeg(best->headSigmaPx, sGrip, L) * tuned::uncertainty::kInflateTopAngle;
            if (!std::isfinite(sig)) continue;
            ps.sigma     = sig;
            ps.sigmaKind = shaftsigma::kindOf(tuned::uncertainty::kCalTopAngle);
            if (best->pGross >= 0.f) ps.grossRisk = best->pGross;
            if (ps.phase == Phase::Top) { m.sigma = sig; m.sigmaKind = ps.sigmaKind; }
        }
    }
    // ── attack angle and low point: the readings recomputed on each posterior draw ────────────
    if (!shaft.synthPost || !unc.synthPosterior || shaft.synth.empty()) return;
    DetRng rng(unc.seed ^ 0xC1DDE1u);
    const std::vector<std::vector<ShaftSample2D>> draws =
        synthDraws(shaft.synth, shaft.positions, *shaft.synthPost, unc.mcDraws, rng);
    if (draws.empty()) return;
    ShaftTrack2D work = shaft;
    std::vector<double> aa, lp;
    for (const auto &d : draws) {
        work.synth = d;
        const ClubDeliveryResult r = trackClubDelivery(work, phases, addressBallPx, ballValid, mmPerPx, cfg);
        const std::vector<MetricSeries> sr = buildClubDeliverySeries(r, phases);
        for (const MetricSeries &m : sr) {
            if (m.phaseSamples.empty()) continue;
            if (m.key == QStringLiteral("attackAngle"))   aa.push_back(m.phaseSamples.front().value);
            if (m.key == QStringLiteral("lowPointAhead")) lp.push_back(m.phaseSamples.front().value);
        }
    }
    const double n = double(draws.size());
    for (MetricSeries &m : series) {
        if (m.phaseSamples.empty()) continue;
        PhaseSample &ps = m.phaseSamples.front();
        if (m.key == QStringLiteral("attackAngle")) {
            const double sd = robustSdCd(aa);
            // No spread at all ⇒ the reading did not come from the synth curve (the measured-head
            // estimator: too few synth heads near impact), so the draws say nothing about it.
            // Publish NO σ — absent is "not characterised"; 0 would claim the reading is exact
            // (the 1 Oct sigma sweep: 18 of 90 swings, every one an implausible −33…−86°).
            if (!std::isfinite(sd) || sd <= 1e-9) continue;
            ps.sigma     = sd * tuned::uncertainty::kInflateAttack;
            ps.sigmaKind = shaftsigma::kindOf(tuned::uncertainty::kCalAttack);
            ps.grossRisk = float(1.0 - double(aa.size()) / n);
            m.sigma = ps.sigma; m.sigmaKind = ps.sigmaKind;
        } else if (m.key == QStringLiteral("lowPointAhead")) {
            // No usable draws still leaves the model term, which is most of the budget.
            double sd = robustSdCd(lp);
            if (!std::isfinite(sd)) sd = 0.0;
            const double sBall  = ballSigmaPx * mmPerPx / kMmPerIn;
            const double sRuler = std::abs(ps.value) * tuned::uncertainty::kRulerRelSigma;
            // The model term is the spread the fit shows against a launch monitor (kLowPointSigmaIn's
            // note): the draws only say how far the synthesized arc itself could move, and on its
            // own that understated the error several-fold.
            ps.sigma     = std::sqrt(sd * sd + sBall * sBall + sRuler * sRuler
                                     + cfg.lowPointSigmaIn * cfg.lowPointSigmaIn)
                         * tuned::uncertainty::kInflateLowPoint;
            ps.sigmaKind = shaftsigma::kindOf(tuned::uncertainty::kCalLowPoint);
            ps.grossRisk = float(1.0 - double(lp.size()) / n);
            m.sigma = ps.sigma; m.sigmaKind = ps.sigmaKind;
        }
    }
}

} // namespace pinpoint::analysis
