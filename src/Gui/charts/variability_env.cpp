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

#include "variability_env.h"

#include "consistency_env.h"                        // consistencyEnvFromPacks
#include "../../Diagnostics/norms/context_tree.h"   // contextIdForClub
#include "../../Diagnostics/norms/norm_provider.h"  // sharedNormProvider
#include "../variability/variability_measures.h"       // variabilityManifest: the one manifest every lookup reads
#include "../../Metrics/metric_catalogue.h"

#include <cmath>
#include <utility>

namespace pinpoint::charts {

namespace {

namespace pa = pinpoint::analysis;
namespace pv = pinpoint::variability;

bool positive(double x) { return std::isfinite(x) && x > 0.0; }

bool positive(const std::optional<double> &x) { return x && positive(*x); }

class VariabilityScaleProvider final : public IVariabilityScaleProvider {
public:
    VariabilityScaleProvider(ConsistencyEnv env, VariabilityDescriptorLookup descriptors,
                             VariabilityMeasureNormLookup measureNorms)
        : m_env(std::move(env)), m_descriptors(std::move(descriptors)), m_measureNorms(std::move(measureNorms)) {}

    VariabilityScaleAnswer scaleFor(const QString &key, int phase, const QString &club) const override
    {
        VariabilityScaleAnswer a;
        const double corridor = corridorHalfWidth(key, phase, club);
        if (positive(corridor)) {
            a.scale = corridor;
            a.source = pv::ScaleSource::Corridor;
            return a;
        }
        const std::optional<double> vs = variabilityScale(key);
        if (positive(vs)) {
            a.scale = *vs;
            a.source = pv::ScaleSource::VariabilityScale;
        }
        return a;
    }

    double corridorHalfWidth(const QString &key, int phase, const QString &club) const override
    {
        // normSigma IS 0.5·(σLo + σHi) for the club's context (consistency_env.h), the agreed half-width.
        const double sigma = m_env.normSigma ? m_env.normSigma(key, phase, club) : kNaN;
        return positive(sigma) ? sigma : kNaN;
    }

    double traceCorridorHalfWidth(const QString &key, const QString &club) const override
    {
        const pa::MetricDescriptor *d = descriptor(key);
        if (!d || d->traceCorridorMeasure.isEmpty() || !m_measureNorms) return kNaN;
        const std::optional<VariabilityMeasureNorm> norm = m_measureNorms(d->traceCorridorMeasure, club);
        if (!norm) return kNaN;
        // R31: never a norm measured from golfers' own swings — read off the library by a run (Measured) or fitted
        // from swings the user marked (Seated). Grading a golfer against a yardstick made of their own spread
        // would say nothing.
        if (norm->source == pa::NormSource::Measured || norm->source == pa::NormSource::Seated) return kNaN;
        return positive(norm->halfWidth) ? norm->halfWidth : kNaN;
    }

    bool traceTimingGraded(const QString &key) const override
    {
        const pa::MetricDescriptor *d = descriptor(key);
        return d && d->traceTimingGraded;
    }

    pv::ScaleType scaleType(const QString &key) const override
    {
        const pa::MetricDescriptor *d = descriptor(key);
        return d ? d->scaleType : pv::ScaleType::Interval;
    }

    std::optional<double> variabilityScale(const QString &key) const override
    {
        const pa::MetricDescriptor *d = descriptor(key);
        return d ? d->variabilityScale : std::nullopt;
    }

    std::optional<double> measurementTolerance(const QString &key) const override
    {
        const pa::MetricDescriptor *d = descriptor(key);
        return d ? d->measurementTolerance : std::nullopt;
    }

    double noiseSigma(const ConsistencyReading &reading) const override
    {
        return positive(reading.ownSigma) ? reading.ownSigma : kNaN;
    }

private:
    const pa::MetricDescriptor *descriptor(const QString &key) const
    {
        return m_descriptors ? m_descriptors(key) : nullptr;
    }

    ConsistencyEnv               m_env;
    VariabilityDescriptorLookup  m_descriptors;
    VariabilityMeasureNormLookup m_measureNorms;
};

} // namespace

VariabilityMeasureNormLookup variabilityMeasureNorms()
{
    // Taken here, on the GUI thread (sharedNormProvider()'s first build is not thread-safe); resolve() is const and
    // safe from any thread after that, as consistency_env.cpp relies on.
    const std::shared_ptr<const pa::INormProvider> norms = pa::sharedNormProvider();
    if (!norms) return {};
    return [norms](const QString &measureId, const QString &club) -> std::optional<VariabilityMeasureNorm> {
        const pa::NormResolution res = norms->resolve(measureId, pa::contextIdForClub(club));
        if (!res.found()) return std::nullopt;
        // (σLo + σHi)/2 whatever the norm's shape (R12), as consistency_env.cpp's normSigma.
        return VariabilityMeasureNorm{ 0.5 * (res.norm->sigmaLo + res.norm->sigmaHi), res.norm->source };
    };
}

std::unique_ptr<IVariabilityScaleProvider> makeVariabilityScaleProvider()
{
    // consistencyEnvFromPacks() checks and logs its own first-call thread rule.
    // The manifest alone, the one every variability lookup reads (variability_measures.h): built once, never changed.
    const pa::MetricCatalogue *catalogue = &pv::variabilityManifest();
    return makeVariabilityScaleProvider(consistencyEnvFromPacks(),
                                        [catalogue](const QString &key) { return catalogue->descriptor(key); },
                                        variabilityMeasureNorms());
}

std::unique_ptr<IVariabilityScaleProvider> makeVariabilityScaleProvider(ConsistencyEnv env,
                                                                        VariabilityDescriptorLookup descriptors,
                                                                        VariabilityMeasureNormLookup measureNorms)
{
    return std::make_unique<VariabilityScaleProvider>(std::move(env), std::move(descriptors), std::move(measureNorms));
}

} // namespace pinpoint::charts
