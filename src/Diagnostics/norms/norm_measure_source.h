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

#include "../engine/characteristic_engine.h"
#include "composite_reading.h"
#include "norm_provider.h"

#include <memory>

// The join between measured VALUES and the norms that judge them.
//
// This is what finally makes MeasureReading::hasCorridor true. The seed pack's 30 corridor signals
// have been dark since they were authored, not because nothing produced their values but because
// nothing produced their corridors — the engine correctly reported Unavailable and the whole
// library detected nothing.
//
// The split is deliberate: a VALUE source knows what the swing did and knows nothing about what is
// normal; this decorator knows what is normal and nothing about the swing. Neither has to grow the
// other's dependencies, and a test can swap either half.

namespace pinpoint::analysis {

// Values only — no corridor, no grade. Implemented by whatever can read a measure off a swing.
class IMeasureValueSource {
public:
    virtual ~IMeasureValueSource() = default;

    struct Value {
        double value      = 0.0;
        float  confidence = 1.0f;
        // Measurement uncertainty (session_diagnostics_design.md §A8.3): NaN = none stated, and the
        // engine then keeps the verdict hard. grossRisk −1 = not assessed.
        double      sigma     = std::numeric_limits<double>::quiet_NaN();
        float       grossRisk = -1.f;
        SigmaSource sigmaSrc  = SigmaSource::None;
        // The producer route the value came from, and whether it is a Direct rung — the ratio
        // test's same-scale rule (MeasureReading::route). Empty + false = none stated.
        QString     route;
        bool        direct    = false;
        // Drawn but not judged (MeasureReading::withheld). Empty = judge it as usual.
        QString     withheld;
        // For a value read BETWEEN two instants (a Delta, or a peak deviation from an anchor): the
        // series at the anchor and at the end instant, so a composite's part can be shown as
        // "address 34° → impact 24°" (CompositePart::atStart). NaN = not such a value, or a source
        // that does not say; nothing grades on them.
        double      atStart   = std::numeric_limits<double>::quiet_NaN();
        double      atEnd     = std::numeric_limits<double>::quiet_NaN();
    };

    // nullopt => this measure could not be produced for this swing. Distinct from a value of zero,
    // and the distinction is the difference between "not assessed" and "assessed and fine".
    virtual std::optional<Value> value(const QString &measureId) const = 0;
};

// Wraps a value source and attaches the norm for the shot's context.
//
// Context handling, in the three cases that matter:
//   * declared and known   — resolved up the tree from there; `contextInferred` false.
//   * NOT declared         — resolved from the default (full swing) and MARKED INFERRED, which
//                            demotes the finding's confidence in the engine. The deviation is real;
//                            what is uncertain is whether the right norm judged it.
//   * declared but unknown — resolves to NOTHING. Grading an unrecognised context against the
//                            full-swing norm would be a wrong answer wearing a right answer's
//                            clothes, so the measure reports as having no corridor and the
//                            characteristic reports Unavailable.
class NormMeasureSource final : public IMeasureSource {
public:
    // `pack` supplies the measure's SHAPE, which lives on the measure and nowhere else. Optional
    // and null-safe: without it every measure reads as a Target, which is what this source did
    // before shapes existed and is right for 105 of the 106 shipped measures. A caller that has a
    // pack should pass it — a floor graded as a target penalises its good tail. It is also where a
    // COMPOSITE's parts are written down, so without a pack a composite id reads as whatever the
    // value source says about it (nothing: it has no metric).
    //
    // `athlete` is the GOLFER's cohort — their sex and the age band their date of birth puts them in
    // on the day of the swing. Unqualified by default, which resolves exactly as everything did
    // before cohorts existed and is the honest answer while the athlete record carries neither
    // field. An axis with no answer is skipped rather than guessed, and the reading STILL GRADES,
    // against the universal corridor: a demographics gap is not a capture gap.
    NormMeasureSource(const IMeasureValueSource          &values,
                      std::shared_ptr<const INormProvider> norms,
                      QString                              contextId = QString(),
                      GradePolicy                          policy    = {},
                      const CharacteristicPack            *pack      = nullptr,
                      Cohort                               athlete   = {})
        : m_values(values), m_norms(std::move(norms)),
          m_contextId(std::move(contextId)), m_policy(policy), m_pack(pack),
          m_athlete(athlete)
    {
    }

    std::optional<MeasureReading> read(const QString &measureId) const override
    {
        // A COMPOSITE is read from its parts, here, because this is the one place values meet norms:
        // each part is graded under the shot's own context and cohort exactly as it would be on its
        // own, and inherits every refusal it would meet there (posture_metrics_design.md §6.3).
        if (const Measure *m = m_pack ? m_pack->measure(measureId) : nullptr;
            m && m->kind == MeasureKind::Composite)
            return readComposite(*m);
        return readOne(measureId, nullptr, nullptr);
    }

private:
    // One ordinary measure: its value, and the norm resolved for the shot. `normOut` receives the
    // row that graded it (null when none resolved) and `valueOut` the raw value-source answer — the
    // two things a composite needs from a part beyond the graded reading itself.
    std::optional<MeasureReading> readOne(const QString &measureId, const Norm **normOut,
                                          IMeasureValueSource::Value *valueOut) const
    {
        if (normOut) *normOut = nullptr;
        const std::optional<IMeasureValueSource::Value> v = m_values.value(measureId);
        if (!v)
            return std::nullopt;          // no value: unavailable, and never a pass
        if (valueOut) *valueOut = *v;

        MeasureReading r;
        r.value      = v->value;
        r.confidence = v->confidence;
        r.measSigma  = v->sigma;
        r.grossRisk  = v->grossRisk;
        r.sigmaSrc   = v->sigmaSrc;
        r.route      = v->route;
        r.direct     = v->direct;
        r.withheld   = v->withheld;

        if (!m_norms)
            return r;                     // no norms wired: hasCorridor stays false, engine greys it

        const NormResolution res = m_norms->resolve(measureId, m_contextId, m_athlete);
        if (!res.found())
            return r;                     // no norm on the chain — same outcome, reported honestly

        const Measure *m     = m_pack ? m_pack->measure(measureId) : nullptr;
        const Shape    shape = m ? m->shape : Shape::Target;
        attachNorm(r, res, shape);
        if (normOut) *normOut = res.norm;
        return r;
    }

    // Grade `r.value` against a resolved norm. One body for an ordinary measure and a composite, so
    // the composite's corridor is drawn and graded by exactly the code every other corridor is.
    void attachNorm(MeasureReading &r, const NormResolution &res, Shape shape) const
    {
        // The POLICY's Ideal band, not the norm's bare claim. The engine tests `onTail` against
        // these edges and `deviated` against grade(), and those two must derive from one scale or
        // a reading can be a deviation that is on neither tail. They agreed before this only
        // because every shipped preset has goodMaxZ >= 1 — an accident, now an asserted invariant
        // (gradePolicyIsOrdered, norm.h).
        const NormBandEdges e = bandEdgesOf(*res.norm, shape, m_policy, -1.0);

        r.hasCorridor     = true;
        r.greenLo         = e.idealLo;
        r.greenHi         = e.idealHi;
        r.lowOpen         = e.lowOpen;
        r.highOpen        = e.highOpen;
        r.grade           = grade(r.value, *res.norm, shape, m_policy);
        deviationEdges(*res.norm, shape, m_policy, r.devLo, r.devHi);
        r.hasDevEdges     = true;
        r.implausible     = res.norm->isImplausible(r.value);
        r.normContextId   = res.contextId;
        r.normCohort      = res.cohort();
        r.contextInferred = m_contextId.isEmpty();
    }

    // ── A composite (posture_metrics_design.md §6.3) ───────────────────────
    //
    //   1. Read the parts: each part's alternatives in order, keeping the first that reads WITH a
    //      corridor — a value nobody can grade, a value its norm does not believe and a value its
    //      producer withheld are all a part not read. z from the part's own norm, side σ.
    //   2. Coverage: the read parts must carry `minWeight` of the authored weight, and every
    //      `required` part must be among them; otherwise the reading is `compositeShort`.
    //   3. Combine over the parts READ — denominator or moments from those alone.
    //   4. σ: propagated (composite_reading.h); unquantified the moment any part read has none.
    //   5. Record each part's contribution, and the parts that were not read.
    //
    // Then grade the result against the composite's OWN norm (μ 0, σ 1, its ρ), shape from the
    // measure, like any other reading. The composite's norm is resolved under the same context as
    // its parts, so a club-keyed ρ row (§6.2) is picked up exactly as a club-keyed corridor is.
    std::optional<MeasureReading> readComposite(const Measure &m) const
    {
        const CompositeSpec &spec     = m.composite;
        const bool           coherent = spec.combine == CompositeCombine::Coherent;

        MeasureReading out;
        out.confidence       = 1.0f;
        out.partsWeightTotal = 0.0;
        out.partsWeightRead  = 0.0;

        // A COMPOSITE THAT IS NOT LIVE is not graded, the rule every non-live status carries
        // (measure_vocabulary.h: Held is "treated exactly as Planned by everything that grades"). It
        // is not composed at all: no number, no corridor, no parts — the engine reports it like any
        // other measure with nothing to judge it against.
        if (m.status != MeasureStatus::Live) {
            out.value = std::numeric_limits<double>::quiet_NaN();
            for (const CompositePartSpec &p : spec.parts) out.partsWeightTotal += p.weight;
            return out;
        }

        std::vector<CompositeTerm> terms;
        bool        requiredMissing = false;
        bool        anyInferred     = false;
        float       gross           = -1.f;
        SigmaSource weakest         = SigmaSource::Reading;

        for (const CompositePartSpec &p : spec.parts) {
            out.partsWeightTotal += p.weight;

            bool    read = false;
            QString lastId, lastWhy, lastStatus;
            for (int k = 0; k < p.measures.size() && !read; ++k) {
                const QString &alt = p.measures.at(k);
                lastId = alt;
                lastStatus.clear();

                const Measure *am = m_pack ? m_pack->measure(alt) : nullptr;
                if (am && am->kind == MeasureKind::Composite) { lastWhy = QStringLiteral("nested"); continue; }
                // A NON-LIVE ALTERNATIVE IS NOT READ, whatever its producer emitted. Held, planned and
                // the rest are "treated exactly as Planned by everything that grades", and a composite
                // grades its parts: a held face-on trunk rise (failed its V1 gate) must not stand in
                // for the DTL bend and carry a face-on-only swing past the coverage line. The ladder
                // moves on; if nothing else answers, the part is unread as `notLive` with the status,
                // so the trust fold can say "trunk rise is held".
                if (am && am->status != MeasureStatus::Live) {
                    lastWhy    = QStringLiteral("notLive");
                    lastStatus = measureStatusName(am->status);
                    continue;
                }

                const Norm                *norm = nullptr;
                IMeasureValueSource::Value raw;
                const std::optional<MeasureReading> r = readOne(alt, &norm, &raw);
                if (!r || !std::isfinite(r->value))   { lastWhy = QStringLiteral("notMeasured"); continue; }
                if (!r->withheld.isEmpty())           { lastWhy = QStringLiteral("withheld");    continue; }
                if (!r->hasCorridor || !norm)         { lastWhy = QStringLiteral("noCorridor");  continue; }
                if (r->implausible)                   { lastWhy = QStringLiteral("implausible"); continue; }

                // The σ of the side of μ the reading fell on — the asymmetric norm's own rule
                // (normZ), but SIGNED on both sides of a one-sided norm: a part read on the good
                // side of a floor is evidence AGAINST the finding and must be able to lower it,
                // which normZ's zero there would hide.
                const double sideSigma = r->value < norm->mu ? norm->sigmaLo : norm->sigmaHi;
                if (!(sideSigma > 0.0))               { lastWhy = QStringLiteral("noCorridor");  continue; }

                CompositeTerm t;
                t.z      = (r->value - norm->mu) / sideSigma;
                t.weight = p.weight;
                t.sign   = coherent ? (p.signs.size() > size_t(k) && p.signs[size_t(k)] < 0.0 ? -1 : 1) : 1;
                if (std::isfinite(r->measSigma) && r->measSigma > 0.0)
                    t.sigmaRatio = r->measSigma / sideSigma;
                terms.push_back(t);

                CompositePart cp;
                cp.name      = p.name;
                cp.measureId = alt;
                cp.value     = r->value;
                cp.unit      = am ? am->unit : QString();
                cp.z         = t.z;
                cp.weight    = p.weight;
                cp.sign      = coherent ? t.sign : 0;
                cp.atStart   = raw.atStart;
                cp.atEnd     = raw.atEnd;
                out.parts.push_back(cp);

                out.partsWeightRead += p.weight;
                out.confidence = std::min(out.confidence, r->confidence);
                anyInferred    = anyInferred || r->contextInferred;
                gross          = std::max(gross, r->grossRisk);
                weakest        = std::min(weakest, r->sigmaSrc);
                read = true;
            }
            if (!read) {
                out.partsUnread.push_back({ p.name, lastId,
                                            lastWhy.isEmpty() ? QStringLiteral("notMeasured") : lastWhy,
                                            lastStatus });
                if (p.required) requiredMissing = true;
            }
        }

        // ── 2. coverage ──────────────────────────────────────────────────────────
        //
        // A hair of tolerance on the share, so a composite authored to need exactly half and read
        // from exactly half is not refused for a rounding error in a sum of decimals.
        const bool covered = out.partsWeightTotal > 0.0 && !terms.empty() && !requiredMissing
                          && out.partsWeightRead >= spec.minWeight * out.partsWeightTotal - 1e-9;
        out.grossRisk = gross;
        if (!covered) {
            // gate rule: rule.diag.compositeCoverage — refused by the engine, which sees this flag
            out.compositeShort = true;
            out.value          = std::numeric_limits<double>::quiet_NaN();
            return out;
        }

        // ── 3–5. combine under the composite's own norm ───────────────────────────
        //
        // ρ comes from that norm, so a composite with no norm — or a norm with no ρ, which the
        // validator refuses (`compositeNorm`) — has no calibration: it reads with no corridor
        // (rule.diag.noCorridor) and a NaN value rather than one combined under a ρ nobody measured.
        const NormResolution res = m_norms ? m_norms->resolve(m.id, m_contextId, m_athlete)
                                           : NormResolution{};
        if (!res.found() || !res.norm->rho.has_value()) {
            out.value = std::numeric_limits<double>::quiet_NaN();
            return out;
        }

        const CompositeCombination c = combineComposite(spec.combine, terms, *res.norm->rho);
        for (size_t i = 0; i < out.parts.size() && i < c.shares.size(); ++i)
            out.parts[i].share = c.shares[i];
        if (!std::isfinite(c.z)) {
            out.value = std::numeric_limits<double>::quiet_NaN();
            return out;
        }

        out.value     = c.z;
        out.measSigma = c.sigma;
        out.sigmaSrc  = std::isfinite(c.sigma) ? weakest : SigmaSource::None;
        attachNorm(out, res, m.shape);
        // A part graded against a context the shot never declared is as weak a basis here as on
        // its own; attachNorm() already says so whenever the composite's own lookup was inferred,
        // which is the same context, so this is belt and braces for a future per-part context.
        out.contextInferred = out.contextInferred || anyInferred;
        return out;
    }

private:
    const IMeasureValueSource           &m_values;
    std::shared_ptr<const INormProvider> m_norms;
    QString                              m_contextId;
    GradePolicy                          m_policy;
    const CharacteristicPack            *m_pack = nullptr;   // shape + composites; may be null
    Cohort                               m_athlete;          // unqualified => the universal corridor
};

} // namespace pinpoint::analysis
