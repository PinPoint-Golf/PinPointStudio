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

#include "characteristic_engine.h"

#include <algorithm>

namespace pinpoint::analysis {

// Confidence multiplier applied when the shot declared no context and the norm was resolved against
// the default. Chosen to land a full-confidence reading below assessment_rules' confidence floor,
// so an inferred-context finding is kept and marked rather than dropped — the deviation is real,
// what is uncertain is whether the right norm was used to judge it.
constexpr float kInferredContextConfidence = 0.7f;

const Finding *DetectionResult::find(const QString &conditionId) const
{
    const auto it = std::find_if(findings.begin(), findings.end(),
                                 [&](const Finding &f) { return f.conditionId == conditionId; });
    return it == findings.end() ? nullptr : &*it;
}

QStringList DetectionResult::fired() const
{
    QStringList out;
    for (const Finding &f : findings)
        if (f.state == FindingState::Fired) out << f.conditionId;
    return out;
}

QStringList DetectionResult::unavailable() const
{
    QStringList out;
    for (const Finding &f : findings)
        if (f.state == FindingState::Unavailable) out << f.conditionId;
    return out;
}

namespace {

// One signal's verdict. `available == false` propagates all the way to the finding — it is never
// collapsed into "did not fire".
struct SignalVerdict {
    bool        available = false;
    bool        fired     = false;
    float       confidence = 0.0f;
    QStringList missing;
    // WHY it is unavailable — None while it is available. See SignalRefusal.
    SignalRefusal refusal = SignalRefusal::None;

    // The reading this verdict was reached on, for the finding's evidence. Set only when the signal
    // was actually evaluated — an unavailable verdict has no number to report and must not pretend
    // otherwise.
    std::optional<MeasureReading> driving;
    QString                       drivingMeasureId;
    // An order test that fell back down its chain: the link it compared with (Signal::fallback).
    QString                       comparedWith;

    // The verdict as a probability (session_diagnostics_design.md §A8.4). Defaults restate the hard
    // verdict; a branch with a σ and an edge to measure it against replaces them.
    float pFire      = -1.f;
    float grossRisk  = -1.f;
    bool  quantified = false;
};

// Φ, the standard normal CDF.
double normCdf(double z) { return 0.5 * std::erfc(-z / std::sqrt(2.0)); }

// P(the TRUE value lies beyond `edge` on tail `d`) for a reading with measurement σ. A non-finite
// edge is an open tail: nothing there can deviate, with any σ.
float pBeyond(double value, double edge, double sigma, Direction d)
{
    if (!std::isfinite(edge)) return 0.f;
    const double z = (d == Direction::High) ? (value - edge) / sigma : (edge - value) / sigma;
    return float(normCdf(z));
}

bool hasSigma(const MeasureReading &r) { return std::isfinite(r.measSigma) && r.measSigma > 0.0; }

// THE SAME-SCALE RULE for a ratio (trunk_imu_sequence_grf_design.md §10.7, E3). A quotient of two
// readings is dimensionless only in name when they come off two scales: a pelvis peak from the IMU
// over a thorax peak from the camera pair route divides one instrument's error by another's, and
// the camera's scale error (kTriScaleFrac) cancels only when it sits on BOTH halves. So: both from
// Direct rungs, or both from one named route.
//
// TWO READINGS THAT SAY NOTHING ABOUT THEIR ROUTE ARE ALLOWED, and that is a compatibility rule, not
// a loophole. Every measure that predates the route field (a tempo ratio of two durations off one
// clock) carries none, and darking those for want of a field their producer never had would turn a
// rule about mixed scales into a rule about old producers. The rule bites the moment route
// information is present on either side: one side stating a route the other lacks is as unknown a
// pairing as two that disagree.
bool sameScale(const MeasureReading &a, const MeasureReading &b)
{
    const bool aSays = !a.route.isEmpty() || a.direct;
    const bool bSays = !b.route.isEmpty() || b.direct;
    if (!aSays && !bSays) return true;                    // neither carries a route: today's behaviour
    if (a.direct && b.direct) return true;                // two Direct rungs: each on its own true scale
    return !a.route.isEmpty() && a.route == b.route;      // one route: its scale error cancels
}

SignalVerdict evaluate(const Signal &sig, const IMeasureSource &src)
{
    SignalVerdict v;

    std::vector<MeasureReading> readings;
    readings.reserve(size_t(sig.measures.size()));
    // A reading its producer marked drawn-but-not-judged (MeasureReading::withheld) is read as
    // absent — so an order chain walks past it like an unplaced link — and if nothing stands in for
    // it the refusal names it as withheld, not missing: the value exists, it just cannot carry a verdict.
    QStringList withheld;
    // A composite read from too few of its parts (MeasureReading::compositeShort) is absent in the
    // same way, and says so in its own words: the parts it could read are real, there were just not
    // enough of them to stand for the whole.
    QStringList shortComposite;
    for (const QString &mid : sig.measures) {
        const auto r = src.read(mid);
        if (!r || !r->withheld.isEmpty() || r->compositeShort) {
            if (r && r->compositeShort) shortComposite << mid;
            else if (r)                 withheld << mid;
            v.missing << mid;
            continue;
        }
        readings.push_back(*r);
    }
    // THE CHAIN DOES NOT END AT A LINK NOBODY PLACED. An order test whose later event produced no
    // value compares its subject with the next link of the chain that did (Signal::fallback, in
    // chain order). The sequence runs hips → chest → lead arm → club → impact, and a capture that
    // never placed the chest still knows whether the hips peaked before the arm: "not assessable"
    // there would hide the one fault the swing plainly shows. Only when no later link was placed
    // is the test unavailable, naming the link it wanted first.
    if (sig.test == SignalTest::Order && readings.size() == 1 && v.missing.size() == 1
        && v.missing.front() == sig.measures.value(1)) {
        for (const QString &fid : sig.fallback) {
            if (fid == QLatin1String(kImpactLink)) {
                // Impact itself: always placed, at 0 ms, its σ carried by the subject's σ_t (which
                // is already a time FROM impact). Confidence 1 — it is the clock's origin.
                MeasureReading at;
                at.value = 0.0;
                readings.push_back(at);
                v.comparedWith = fid;
                v.missing.clear();
                break;
            }
            if (const auto r = src.read(fid); r && r->withheld.isEmpty() && !r->compositeShort) {
                readings.push_back(*r);
                v.comparedWith = fid;
                v.missing.clear();
                break;
            }
        }
    }
    if (!v.missing.isEmpty()) {   // stays unavailable
        const auto any = [&](const QStringList &which) {
            return std::any_of(v.missing.cbegin(), v.missing.cend(),
                               [&](const QString &m) { return which.contains(m); });
        };
        // gate rule: PP_GATE("rule.diag.compositeCoverage")
        v.refusal = any(withheld)       ? SignalRefusal::Withheld
                  : any(shortComposite) ? SignalRefusal::CompositeCoverage
                                        : SignalRefusal::MeasureMissing;
        return v;
    }

    // A signal that only means something while a peak precedes impact (Signal::requiresBeforeImpact).
    // Hip braking compares the hips' speed at impact with their peak; with the peak AFTER impact the
    // hips are still speeding up through the ball, and the quotient's "they slowed" is false. That
    // lateness is a sequence fault in its own right, reported by the hips-before-impact check; here
    // it is the reason this check cannot be read, never a pass.
    if (!sig.requiresBeforeImpact.isEmpty()) {
        const auto t = src.read(sig.requiresBeforeImpact);
        if (!t) { v.missing << sig.requiresBeforeImpact; v.refusal = SignalRefusal::MeasureMissing; return v; }
        if (!t->withheld.isEmpty()) { v.missing << sig.requiresBeforeImpact; v.refusal = SignalRefusal::Withheld; return v; }
        if (!(t->value < 0.0)) {
            v.missing << sig.requiresBeforeImpact;
            // gate rule: PP_GATE("rule.diag.peakAfterImpact")
            v.refusal = SignalRefusal::PeakAfterImpact;
            return v;
        }
    }

    const int want = (sig.test == SignalTest::Order || sig.test == SignalTest::Ratio) ? 2 : 1;
    if (int(readings.size()) != want) {
        // A malformed signal reaching the engine is a pack bug the validator should have caught.
        // Report unavailable rather than guessing: a wrong answer here is indistinguishable from a
        // real finding downstream.
        v.missing = sig.measures;
        v.refusal = SignalRefusal::Malformed;
        return v;
    }

    v.available  = true;
    v.confidence = readings.front().confidence;
    for (const MeasureReading &r : readings) v.confidence = std::min(v.confidence, r.confidence);

    switch (sig.test) {
    case SignalTest::OutsideCorridor: {
        const MeasureReading &r = readings.front();
        if (!r.hasCorridor) {
            // The corridor is the whole test. Without one there is nothing to compare against, so
            // this is unavailable — NOT a pass. This is the single most important branch in the
            // engine: most of the seed pack has no corridor yet.
            v.available = false;
            // gate rule: PP_GATE("rule.diag.noCorridor")
            v.refusal   = SignalRefusal::NoCorridor;
            v.missing << sig.measures.value(0);
            return v;
        }
        // A reading the norm does not believe was NOT ASSESSED, and the finding must say so. It is
        // not "we looked and this is fine" — the value is a capture fault, and reporting it as
        // NotFired would be a false negative wearing a clean bill of health. Unavailable is the
        // state that exists for exactly this, and `implausible` is what lets the UI say which of
        // the two reasons applies.
        // gate rule: PP_GATE("rule.diag.implausible")
        if (r.implausible) {
            v.available = false;
            v.refusal   = SignalRefusal::Implausible;
            v.missing << sig.measures.value(0);
            return v;
        }

        const Direction d = sig.direction.value_or(Direction::High);

        // A signal pointing at a tail the norm never grades CANNOT fire, and says so outright
        // rather than reaching the same answer by arithmetic. On a floor the high edge is mu and
        // everything above it grades Ideal, so `deviated` would already be false — but that is a
        // coincidence of two other rules, and an author who wrote this signal has misunderstood
        // the measure. `diagnostics_health` reports it as `signalOnOpenTail` at validation time.
        const bool tailOpen = (d == Direction::High) ? r.highOpen : r.lowOpen;

        // Two conditions, both required. The GRADE decides whether this is a deviation at all
        // (Watch or Action — see MeasureReading::grade for why not merely "outside Ideal"), and the
        // SIDE decides whether it is this tail's deviation. An axis has two conditions on one norm,
        // so without the side check both tails would fire on any deviation in either direction.
        const bool deviated = isDeviation(r.grade);
        const bool onTail   = (d == Direction::High) ? (r.value > r.greenHi) : (r.value < r.greenLo);
        v.fired = !tailOpen && deviated && onTail;

        // §A8.4: the edge the verdict crossed is the LATER of the deviation edge and the Ideal edge
        // on this tail (onTail asks for both), so P(fire) is measured against that one.
        if (hasSigma(r) && r.hasDevEdges) {
            const double edge = tailOpen ? std::numeric_limits<double>::quiet_NaN()
                              : (d == Direction::High) ? std::max(r.devHi, r.greenHi)
                                                       : std::min(r.devLo, r.greenLo);
            v.pFire      = tailOpen ? 0.f : pBeyond(r.value, edge, r.measSigma, d);
            v.quantified = true;
        }
        v.grossRisk = r.grossRisk;

        // A context the shot never declared is a weaker basis for a finding than one it did. Reuse
        // the confidence channel rather than inventing a second signal for it — assessment_rules
        // already demotes on low confidence, and a parallel mechanism would need its own UI.
        // gate rule: PP_GATE("rule.diag.inferredContext")
        if (r.contextInferred) v.confidence *= kInferredContextConfidence;
        break;
    }
    case SignalTest::Threshold: {
        const MeasureReading &r = readings.front();

        // The two refusals its siblings make, made here too — and their absence was not a
        // simplification. A threshold test reads the SAME reading off the same norm-joined source
        // as a corridor test; only the number it compares against differs. So every argument for
        // refusing over there holds here word for word, and a branch that skipped both was the one
        // place in the engine where an authored number was trusted more than the norm that said the
        // reading was not real.
        //
        // A reading the norm does not believe was NOT ASSESSED. That an author typed the number
        // rather than inheriting it changes nothing: a smash of 1.62 is a mis-tracked ball, and
        // answering "yes, above 1.55" about it launders a capture fault into a confident diagnosis
        // — exactly what Norm::plausibleLo exists to prevent. Unavailable is the state for it.
        if (r.implausible) {
            v.available = false;
            v.refusal   = SignalRefusal::Implausible;
            v.missing << sig.measures.value(0);
            return v;
        }

        const double    t = sig.threshold.value_or(0.0);
        const Direction d = sig.direction.value_or(Direction::High);

        // A signal watching a tail the measure's own shape says never grades cannot fire, and says
        // so outright rather than reaching the answer by arithmetic — the corridor branch's rule,
        // and the same authoring mistake behind it. A floor has no upper fault whoever writes the
        // number.
        //
        // NOTE WHAT IS NOT REQUIRED: a corridor. Threshold is the test kind that exists to work
        // where no norm has been authored, so demanding hasCorridor here would dark precisely the
        // signals this branch is for. With no norm both flags are false and this costs nothing;
        // with a norm behind the reading, its shape is believed.
        const bool tailOpen = (d == Direction::High) ? r.highOpen : r.lowOpen;

        v.fired = !tailOpen && ((d == Direction::High) ? (r.value > t) : (r.value < t));
        if (hasSigma(r)) {
            v.pFire      = tailOpen ? 0.f : pBeyond(r.value, t, r.measSigma, d);
            v.quantified = true;
        }
        v.grossRisk = r.grossRisk;
        break;
    }
    case SignalTest::Order:
        // The corridor branch's refusal, made here too: an order built from a reading the norm
        // does not believe was not assessed, whichever of the two events was wrong. Answering
        // "in order" about a peak time that is a capture fault is a clean bill of health nobody
        // measured.
        if (readings[0].implausible || readings[1].implausible) {
            v.available = false;
            v.refusal   = SignalRefusal::Implausible;
            v.missing   = sig.measures;
            return v;
        }
        // Measures are ordered: the first is expected to peak before the second. Values carry the
        // event time, so "out of order" is first >= second.
        v.fired = readings[0].value >= readings[1].value;
        // §A8.4: P(first ≥ second) with both readings' σ in quadrature. Against impact the second
        // event is the clock's origin and the subject's σ_t is the whole budget.
        if (v.comparedWith == QLatin1String(kImpactLink) && hasSigma(readings[0])) {
            v.pFire      = float(normCdf(readings[0].value / readings[0].measSigma));
            v.quantified = true;
        } else if (hasSigma(readings[0]) && hasSigma(readings[1])) {
            const double s = std::hypot(readings[0].measSigma, readings[1].measSigma);
            v.pFire      = float(normCdf((readings[0].value - readings[1].value) / s));
            v.quantified = true;
        }
        v.grossRisk = std::max(readings[0].grossRisk, readings[1].grossRisk);
        break;
    case SignalTest::Ratio: {
        // THE RATIO CONTRACT, and why it is not the obvious one.
        //
        // This branch divides one measure by another, and the quotient is DIMENSIONLESS. It used to
        // grade that quotient against readings.front()'s corridor — a norm keyed on
        // sig.measures[0], whose unit norm.h requires to match THAT measure's unit ("MUST match the
        // measure's unit; load fails if not"). So the corridor was in degrees, or seconds, while
        // the value was a pure number. That is not a strict test or a lenient one, it is a category
        // error: no author could write a ratio signal that meant what they thought it meant, and
        // the reason the shipped pack contains none of them may simply be that nobody could.
        //
        // THE TEMPTING REPAIR DOES NOT WORK, and the reason is structural rather than a matter of
        // taste. Let the ratio carry its own norm — name a measure whose unit is dimensionless and
        // author a corridor on it. But a norm reaches this engine only through IMeasureSource::read
        // and A READING REQUIRES A VALUE: NormMeasureSource returns nullopt the instant the value
        // source cannot produce one, corridor or no corridor. Nothing produces the quotient. That
        // is the entire reason this test kind exists rather than one corridor signal on a produced
        // ratio measure — which is what m_tempoRatio already is, unit ":1", norm and all. So the
        // stand-in measure would resolve to nullopt, the signal would report Unavailable forever,
        // and the corridor authored for it would never once be read.
        //
        // So: THE NUMBER THAT GRADES A DERIVED QUANTITY IS AUTHORED ON THE SIGNAL THAT DERIVES IT,
        // because the pack has nowhere else to put a number for something that is not a measure. A
        // norm keys on a measure; a quotient is not one and cannot be made into one without a
        // producer. The mechanism already exists and already carries the right doctrine —
        // Signal::threshold, "an authored number; needs a citation to be more than an opinion",
        // which is exactly the standing of a published 3:1 tempo figure. Ratio is Threshold applied
        // to a quotient, and the validator demands the number and the direction from it the same
        // way (signalThreshold, signalDirection).
        //
        // What the two inputs must share is a UNIT, and `signalRatioUnit` enforces that at load:
        // seconds over seconds cancels and the authored figure means something, while degrees over
        // seconds is a rate wearing a ratio's name and the figure would be in a unit nothing in the
        // model records.
        //
        // A zero denominator is no ratio at all, and a NEGATIVE one is not a ratio this test can
        // grade: dividing by it turns "above the number" into "below it", so the authored direction
        // would mean the opposite of what was written. Every ratio the pack carries is of two
        // positive quantities (durations; the KS peak speeds are emitted as magnitudes), so a
        // non-positive denominator is a producer fault, and Unavailable is the state for it. The
        // negated form also refuses a NaN.
        const double denom = readings[1].value;
        // gate rule: PP_GATE("rule.diag.nonPositiveDenominator")
        if (!(denom > 0.0)) { v.available = false; v.refusal = SignalRefusal::NonPositiveDenominator; v.missing = sig.measures; return v; }

        if (readings[0].implausible || readings[1].implausible) {
            // Same rule as the corridor branch: a ratio built from a reading nobody believes was
            // not assessed, whichever half of it was wrong.
            v.available = false;
            v.refusal   = SignalRefusal::Implausible;
            v.missing   = sig.measures;
            return v;
        }

        // Two scales: not assessed. See sameScale() for what counts as one, and why two readings
        // that state no route at all still pass.
        // gate rule: PP_GATE("rule.diag.mixedScale")
        if (!sameScale(readings[0], readings[1])) {
            v.available = false;
            v.refusal   = SignalRefusal::MixedScale;
            v.missing   = sig.measures;
            return v;
        }

        const double    ratio = readings[0].value / denom;
        const double    t     = sig.threshold.value_or(0.0);
        const Direction d     = sig.direction.value_or(Direction::High);
        v.fired = (d == Direction::High) ? (ratio > t) : (ratio < t);

        // §10.7 E1: the quotient's σ by the delta method, σ_q = |q|·√((σa/a)² + (σb/b)²), and
        // P(fire) = Φ(±(q − t)/σ_q) like the threshold test it is. Only with BOTH σ stated — the
        // Order rule: a half-known budget would claim a precision nobody measured.
        //
        // Written as √((σa/b)² + (q·σb/b)²), which is the same number (|q|·σa/|a| = σa/b with b > 0)
        // and stays defined on a zero numerator, where σa/a is not.
        if (hasSigma(readings[0]) && hasSigma(readings[1])) {
            const double sq = std::hypot(readings[0].measSigma / denom,
                                         ratio * readings[1].measSigma / denom);
            v.pFire      = pBeyond(ratio, t, sq, d);
            v.quantified = true;
        }
        v.grossRisk = std::max(readings[0].grossRisk, readings[1].grossRisk);

        // NO corridor requirement, and NO open-tail check. Both belong to a measure's norm and the
        // quotient has neither. Requiring a corridor would dark every ratio signal for want of a
        // band it does not consult; and the numerator's shape describes which tail of the NUMERATOR
        // faults, which says nothing whatever about the quotient — a small enough denominator sends
        // it high however one-sided the measure above the line is. `diagnostics_health` follows the
        // same line: signalOnOpenTail now reads corridor and threshold signals, not ratios.
        //
        // Nor is confidence demoted for an inferred context. That demotion asks "was the right norm
        // used to judge this?", and here no norm judges it — the number came from the author.
        break;
    }
    }

    // THE DRIVING READING IS measures[0], for every test kind — reached only on the paths that
    // actually evaluated the signal, since every unavailable branch above returns early. The
    // corridor and threshold tests read exactly one measure, so there is no choice to make; Order
    // and Ratio name their subject first (the event expected to LEAD, the numerator), and the pack
    // validator enforces that order. Its corridor may be absent — a threshold or ratio signal grades
    // against an authored number — and MeasureEvidence handles that without inventing a band.
    v.driving          = readings.front();
    v.drivingMeasureId = sig.measures.value(0);

    // No σ on a reading the test needed one from: the probability is the hard verdict restated.
    if (!v.quantified) v.pFire = v.fired ? 1.f : 0.f;

    return v;
}

// One signal's contribution to a finding's evidence, in c.detectedBy order.
struct EvidenceCandidate {
    QString        signalId;
    QString        measureId;
    MeasureReading reading;
    float          confidence = 0.0f;
    bool           fired      = false;
};

// WHICH READING SPEAKS FOR THE FINDING. The most confident signal, ties broken by pack order (the
// order the author wrote c.detectedBy in), restricted to the signals that FIRED when the finding
// fired and drawn from every assessed signal when it did not.
//
// The SAME rule in both states, deliberately. The alternative for NotFired — pick whichever measure
// sat closest to its edge, the near-miss — reads better on one swing and ruins the ledger, because
// the measure it names changes from swing to swing and a trend line would silently splice two
// different quantities together. Confidence is a property of the CAPTURE, not of the swing, so it
// picks the same measure every time a condition is assessed the same way, which is what makes
// "this corridor is being cleared by less and less" a sentence about the golfer.
//
// Confidence here is the SIGNAL's — after the inferred-context demotion — not the raw reading's, so
// a signal graded against a norm the shot never declared yields to a sibling judged on solid ground.
MeasureEvidence pickEvidence(const std::vector<EvidenceCandidate> &candidates, bool firedOnly)
{
    const EvidenceCandidate *best = nullptr;
    for (const EvidenceCandidate &c : candidates) {
        if (firedOnly && !c.fired) continue;
        if (best == nullptr || c.confidence > best->confidence) best = &c;
    }
    if (best == nullptr) return {};
    MeasureEvidence e = MeasureEvidence::fromReading(best->signalId, best->measureId, best->reading);
    e.fired = best->fired;
    return e;
}

// Every assessed signal's reading, in pack order — Finding::readings.
std::vector<MeasureEvidence> allEvidence(const std::vector<EvidenceCandidate> &candidates)
{
    std::vector<MeasureEvidence> out;
    out.reserve(candidates.size());
    for (const EvidenceCandidate &c : candidates) {
        out.push_back(MeasureEvidence::fromReading(c.signalId, c.measureId, c.reading));
        out.back().fired = c.fired;
    }
    return out;
}

} // namespace

SignalOutcome evaluateSignal(const CharacteristicPack &pack, const QString &signalId,
                             const IMeasureSource &source)
{
    SignalOutcome out;
    out.signalId = signalId;
    const Signal *sig = pack.signal(signalId);
    if (!sig) {
        out.refusal = SignalRefusal::UnknownSignal;
        return out;
    }
    // THE SAME evaluate() detect() runs for a condition's signal, on whatever source the caller
    // hands in — so a caller that passes detect()'s own source reads the verdict detect() read.
    const SignalVerdict v = evaluate(*sig, source);
    out.available  = v.available;
    out.fired      = v.available && v.fired;
    out.refusal    = v.available ? SignalRefusal::None : v.refusal;
    out.missing    = v.missing;
    out.comparedWith = v.available ? v.comparedWith : QString();
    if (v.available) {
        out.pFire      = v.pFire;
        out.quantified = v.quantified;
        out.grossRisk  = v.grossRisk;
    }
    return out;
}

DetectionResult detect(const CharacteristicPack &pack, const IMeasureSource &source,
                       const ContextTree *contexts, const QString &contextId)
{
    DetectionResult out;

    for (const Condition &c : pack.conditions) {
        // Latent conditions have no signals by definition — they are resolved by the explanation
        // pass from what they explain, not detected here.
        if (c.observability == Observability::Latent) continue;

        // Withdrawn content does not diagnose. Retired and Superseded are the two states that mean
        // "do not use this any more", and until now the engine read all six identically — a
        // retired characteristic would have gone on firing exactly as it did the day it was sound.
        //
        // ONLY those two. Draft and Candidate are editorial confidence, not withdrawal, and 62 of
        // the 112 shipped conditions are Draft — gating on them would dark more than half the
        // library on a reading of the field nobody has agreed to. A Superseded condition is
        // additionally required by the validator to name its successor, so the replacement is
        // already in the pack and evaluates in its place; dropping it here loses nothing.
        if (c.state == ConditionState::Retired || c.state == ConditionState::Superseded) continue;

        // Does this condition apply to this kind of shot? Resolved through the context tree, so an
        // author writes one row at `partial` rather than four beneath it.
        bool material = true;
        if (contexts != nullptr && !contextId.isEmpty()) {
            const BindingResolution br = resolveContextBinding(c, *contexts, contextId);
            if (!br.applicable) continue;
            material = br.material;
        }

        Finding f;
        f.conditionId = c.id;
        f.material    = material;
        f.sharp       = c.sharp;

        if (c.detectedBy.isEmpty()) {
            f.state = FindingState::Unavailable;
            out.findings.push_back(std::move(f));
            continue;
        }

        bool  anyFired      = false;
        bool  anyUnavailable = false;
        bool  anyAssessedNotFired = false;   // ALL mode only — see the verdict block below
        float conf          = 1.0f;

        // §A8.4 — the finding's probability from its signals'. ANY: 1 − Π(1 − p); ALL: Π p. The
        // signals of one capture are correlated and the product treats them as independent, so it
        // overstates certainty; stated, not corrected (nothing models that correlation).
        double notAny = 1.0, all = 1.0;
        bool   quantifiedAll = true, anyAvailable = false;
        float  gross = -1.f;

        std::vector<EvidenceCandidate> candidates;   // in c.detectedBy order — see pickEvidence()
        // the first refusal met, stamped on the finding only if it ends Unavailable (Finding::refusal)
        SignalRefusal firstRefusal = SignalRefusal::None;

        for (const QString &sid : c.detectedBy) {
            const Signal *sig = pack.signal(sid);
            if (!sig) {
                anyUnavailable = true;
                f.missingMeasures << sid;
                if (firstRefusal == SignalRefusal::None) firstRefusal = SignalRefusal::UnknownSignal;
                continue;
            }

            const SignalVerdict v = evaluate(*sig, source);
            if (!v.available) {
                // FIRST: an unreadable preferred signal is not a verdict, it is the cue to try the
                // next one. Only when none can be read is the finding unavailable (below the loop).
                if (c.detection != DetectionMode::First) anyUnavailable = true;
                f.missingMeasures << v.missing;
                if (firstRefusal == SignalRefusal::None) firstRefusal = v.refusal;
                continue;
            }
            if (v.fired) {
                anyFired = true;
                f.firedSignals << sid;
                f.direction = sig->direction.value_or(Direction::High);
            } else {
                anyAssessedNotFired = true;
            }
            conf = std::min(conf, v.confidence);
            anyAvailable   = true;
            notAny        *= 1.0 - double(v.pFire);
            all           *= double(v.pFire);
            quantifiedAll  = quantifiedAll && v.quantified;
            gross          = std::max(gross, v.grossRisk);

            if (v.driving) candidates.push_back({ sid, v.drivingMeasureId, *v.driving,
                                                  v.confidence, v.fired });

            // FIRST: the first signal that could be read decides alone; the rest are fallbacks
            // for a capture that could not read it, never a second opinion.
            if (c.detection == DetectionMode::First) break;
        }
        // ...and with nothing readable, FIRST is exactly ANY's "could not be assessed".
        if (c.detection == DetectionMode::First && !anyAvailable) anyUnavailable = true;

        // ── ALL: a conjunction, and the precedence INVERTS ───────────────────
        //
        // Every signal must have fired. What is worth spelling out is the order of the two failure
        // cases, because it is the mirror image of the ANY rule below and for the same reason.
        //
        // A CONJUNCT THAT WAS ASSESSED AND DID NOT FIRE SETTLES IT. The conjunction is false, and
        // it is false whatever the signals we could not read would have said — an AND with one
        // known-false term needs no other term. So that is NotFired, a real negative answer, and
        // reporting Unavailable instead would throw away a verdict we actually hold. This is the
        // common case by a distance: `top` asks for a thin strike AND a low point behind the ball
        // AND an upward attack, and the overwhelming majority of swings settle it on the first.
        //
        // ONLY WITH NOTHING FALSE does an unreadable conjunct make it unassessable, because then
        // the answer really does turn on what we could not see.
        const auto stampProbability = [&](Finding &fd, bool conjunction) {
            if (!anyAvailable) return;
            fd.pFire      = float(conjunction ? all : 1.0 - notAny);
            fd.grossRisk  = gross;
            fd.quantified = quantifiedAll;
        };

        if (c.detection == DetectionMode::All) {
            if (anyAssessedNotFired) {
                f.state      = FindingState::NotFired;
                f.confidence = conf;
                f.evidence   = pickEvidence(candidates, /*firedOnly*/ false);
                f.readings   = allEvidence(candidates);
                stampProbability(f, true);
            } else if (anyUnavailable) {
                f.state      = FindingState::Unavailable;
                f.confidence = 0.0f;
                f.refusal    = firstRefusal;
            } else {
                f.state      = FindingState::Fired;
                f.confidence = conf;
                f.evidence   = pickEvidence(candidates, /*firedOnly*/ true);
                f.readings   = allEvidence(candidates);
                stampProbability(f, true);
            }
            out.findings.push_back(std::move(f));
            continue;
        }

        // Precedence: a signal that fired is a positive observation and stands even if a sibling
        // signal could not be evaluated. Only when NOTHING fired does an unavailable signal make
        // the whole finding unassessable — otherwise a partially-producible condition would be
        // silently downgraded to "not present".
        if (anyFired) {
            f.state      = FindingState::Fired;
            f.confidence = conf;
            f.evidence   = pickEvidence(candidates, /*firedOnly*/ true);
            f.readings   = allEvidence(candidates);
            stampProbability(f, false);
        } else if (anyUnavailable) {
            f.state      = FindingState::Unavailable;
            f.confidence = 0.0f;
            f.refusal    = firstRefusal;
            // NO evidence, even when a sibling signal did read a number. The finding's verdict is
            // "could not be assessed", and attaching a reading to it would invite a ledger row that
            // reads as an assessment. See MeasureEvidence.
        } else {
            f.state      = FindingState::NotFired;
            f.confidence = conf;
            f.evidence   = pickEvidence(candidates, /*firedOnly*/ false);
            f.readings   = allEvidence(candidates);
            stampProbability(f, false);
        }

        out.findings.push_back(std::move(f));
    }

    return out;
}

} // namespace pinpoint::analysis
