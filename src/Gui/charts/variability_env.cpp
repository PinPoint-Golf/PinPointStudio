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
    VariabilityScaleProvider(ConsistencyEnv env, VariabilityDescriptorLookup descriptors)
        : m_env(std::move(env)), m_descriptors(std::move(descriptors)) {}

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

    ConsistencyEnv              m_env;
    VariabilityDescriptorLookup m_descriptors;
};

// The manifest alone, as ConsistencyPacks reads it: only the descriptors are needed, and
// makeMetricCatalogue() also writes a process-wide labeller. Built once; never changed after, so its
// readers take no lock.
struct VariabilityManifest {
    VariabilityManifest() { pa::installMetricManifest(catalogue); }
    pa::MetricCatalogue catalogue;
};

const pa::MetricCatalogue &manifestCatalogue()
{
    static const VariabilityManifest manifest;     // C++ builds a function-local static once, under its own lock
    return manifest.catalogue;
}

} // namespace

std::unique_ptr<IVariabilityScaleProvider> makeVariabilityScaleProvider()
{
    // consistencyEnvFromPacks() checks and logs its own first-call thread rule.
    const pa::MetricCatalogue *catalogue = &manifestCatalogue();
    return makeVariabilityScaleProvider(consistencyEnvFromPacks(),
                                        [catalogue](const QString &key) { return catalogue->descriptor(key); });
}

std::unique_ptr<IVariabilityScaleProvider> makeVariabilityScaleProvider(ConsistencyEnv env,
                                                                        VariabilityDescriptorLookup descriptors)
{
    return std::make_unique<VariabilityScaleProvider>(std::move(env), std::move(descriptors));
}

} // namespace pinpoint::charts
