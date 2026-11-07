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

#include "session_consistency.h"                            // ConsistencyEnv, ConsistencyReading

#include "../../Metrics/variability/variability_result.h"     // ScaleSource
#include "../../Metrics/variability/variability_scale_type.h" // ScaleType

#include <QString>

#include <functional>
#include <memory>
#include <optional>

// THE VARIABILITY SCALE PROVIDER (docs/design/variability-mark/spec.md §8.2 step 4, §8.5, §8.6): what
// the variability grader is graded AGAINST, for one reading in one context. The grader is pure maths
// and takes numbers; this is the one place those numbers reach the norms and the catalogue.
//
// THE SCALE LADDER IT ANSWERS is the first two rungs of §8.2 step 4, in order:
//   1. Corridor — the norm corridor half-width for (key, phase, club). That is (σLo + σHi)/2 (the §8.2
//      4.1 amendment, Mark 7 Nov), which is exactly consistencyEnvFromPacks().normSigma: the same σ the
//      Consistency matrix and Posture repeatability already read. It is REUSED, never recomputed, so the
//      two surfaces can never disagree about a corridor. normSigma has its own fallbacks: with no measure
//      at the phase carrying a norm, the median over the key's measures in the club's context, then in
//      `any`. So "the corridor for the context" may be a key-wide median, or the `any` context's.
//   2. VariabilityScale — MetricDescriptor::variabilityScale.
//   Otherwise None. The third rung (CV, Ratio only) is the GRADER's: it needs the values, and it reads
//   scaleType() to decide.
//
// noiseSigma() is the reading's own measurement σ, ConsistencyReading::ownSigma, which consistencyInput
// already works out (a position's median phase σ, else the series σ). It is a PROPAGATED σ — the
// producer's error budget — not a validation RMSE and not σnoise. It goes to the grader as
// VariabilityInput::propagatedSigma, the measurement-limited test (Mark's ruling R3, 7 Nov: an observed
// spread no larger than it is measurement-limited), and NEVER as VariabilityInput::noiseSd: grades are
// taken on the observed spread, and a propagated σ is never subtracted.
//
// Assembled ON DEMAND (makeVariabilityScaleProvider), mirroring makeNormProvider(): no self-registering
// statics. The production provider holds a ConsistencyEnv by value, so it keeps that env's threading
// contract (consistency_env.h): MAKE IT ON THE GUI THREAD, then it is safe on any thread, at once, for
// the life of the process.

namespace pinpoint::analysis { struct MetricDescriptor; }

namespace pinpoint::charts {

// One scale, and which rung of §8.2 step 4 it came from. `source` is Corridor, VariabilityScale or None;
// `scale` is NaN with None.
struct VariabilityScaleAnswer {
    double                   scale  = kNaN;
    variability::ScaleSource source = variability::ScaleSource::None;

    bool found() const { return source != variability::ScaleSource::None; }
};

class IVariabilityScaleProvider {
public:
    virtual ~IVariabilityScaleProvider() = default;

    // The scale for `key` at `phase` (a Phase id, as ConsistencyEnv::normSigma takes it) for `club`.
    virtual VariabilityScaleAnswer scaleFor(const QString &key, int phase, const QString &club) const = 0;

    // The norm corridor half-width alone (rung 1), NaN without one: the §8.5 MEAS_TOL_FRACTION rung needs
    // the corridor itself, never a variabilityScale standing in for it.
    virtual double corridorHalfWidth(const QString &key, int phase, const QString &club) const = 0;

    // MetricDescriptor::scaleType; Interval for a key the catalogue does not know (the descriptor default).
    virtual variability::ScaleType scaleType(const QString &key) const = 0;

    // MetricDescriptor::variabilityScale and ::measurementTolerance; nullopt when not stated.
    virtual std::optional<double> variabilityScale(const QString &key) const = 0;
    virtual std::optional<double> measurementTolerance(const QString &key) const = 0;

    // A reading's propagated σ: its ownSigma when finite and positive, else NaN (not known). For
    // VariabilityInput::propagatedSigma (R3), never for VariabilityInput::noiseSd (see the header comment).
    virtual double noiseSigma(const ConsistencyReading &reading) const = 0;
};

// The descriptor of a key, or nullptr. The pointer must outlive the provider.
using VariabilityDescriptorLookup = std::function<const pinpoint::analysis::MetricDescriptor *(const QString &key)>;

// PRODUCTION: consistencyEnvFromPacks() and the shipped metric manifest. Make it on the GUI thread (above).
std::unique_ptr<IVariabilityScaleProvider> makeVariabilityScaleProvider();

// The same provider over any env and descriptor source: a stub env and in-memory descriptors in a test.
std::unique_ptr<IVariabilityScaleProvider> makeVariabilityScaleProvider(ConsistencyEnv env,
                                                                        VariabilityDescriptorLookup descriptors);

} // namespace pinpoint::charts
