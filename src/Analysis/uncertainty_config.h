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

// "uncertainty.*" switches (docs/design/shaft_uncertainty_propagation_design.md §7). One
// struct, parsed once per consumer from the job's tuning overrides; defaults from
// tuned::uncertainty. enabled = false ⇒ no σ field is written anywhere and every output is
// byte-identical to the pre-design pipeline. Within enabled, synthSoftAnchors and oneImpact
// are the only switches that may change a VALUE (design principle 3); everything else only
// adds σ.

#include <QVariantMap>

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "analysis_tuning.h"
#include "../Core/pp_tuned_constants.h"

namespace pinpoint::analysis {

struct UncertaintyConfig {
    bool     enabled          = tuned::uncertainty::kEnabled;
    bool     shaftTable       = true;    // U1 per-sample σθ / pGross
    bool     synthPosterior   = true;    // U3 synth σ + Monte Carlo
    bool     synthSoftAnchors = tuned::uncertainty::kSynthSoftAnchors;   // U3 value change (own gate)
    bool     oneImpact        = tuned::uncertainty::kOneImpact;          // U4 value change (own gate)
    bool     fbPosterior      = tuned::uncertainty::kFbPosterior;        // U5 replaces the table in-span
    bool     planeBootstrap   = true;    // U6
    int      mcDraws          = tuned::uncertainty::kMcDraws;
    uint64_t seed             = tuned::uncertainty::kSeed;
    double   fbTemperature    = tuned::uncertainty::kFbTemperature;
    double   synthKappa       = tuned::uncertainty::kSynthKappa;
    double   synthSigmaScale  = tuned::uncertainty::kSynthSigmaScale;
    double   rho              = tuned::uncertainty::kRho;
    int      bootstrapN       = tuned::uncertainty::kBootstrapN;
    int      bootstrapBlock   = tuned::uncertainty::kBootstrapBlock;

    static UncertaintyConfig fromOverrides(const QVariantMap &ov)
    {
        using tuning::apply;
        UncertaintyConfig c;
        apply(ov, "uncertainty.enabled",          c.enabled);
        apply(ov, "uncertainty.shaftTable",       c.shaftTable);
        apply(ov, "uncertainty.synthPosterior",   c.synthPosterior);
        apply(ov, "uncertainty.synthSoftAnchors", c.synthSoftAnchors);
        apply(ov, "uncertainty.oneImpact",        c.oneImpact);
        apply(ov, "uncertainty.fbPosterior",      c.fbPosterior);
        apply(ov, "uncertainty.planeBootstrap",   c.planeBootstrap);
        apply(ov, "uncertainty.mcDraws",          c.mcDraws);
        apply(ov, "uncertainty.fbTemperature",    c.fbTemperature);
        apply(ov, "uncertainty.synthKappa",       c.synthKappa);
        apply(ov, "uncertainty.synthSigmaScale",  c.synthSigmaScale);
        apply(ov, "uncertainty.rho",              c.rho);
        apply(ov, "uncertainty.bootstrapN",       c.bootstrapN);
        apply(ov, "uncertainty.bootstrapBlock",   c.bootstrapBlock);
        double seedD = -1.0;
        apply(ov, "uncertainty.seed", seedD);
        if (seedD >= 0.0) c.seed = uint64_t(seedD);
        return c;
    }

    // Effective sample size of n consecutive, lag-1-correlated readings (design §5.2).
    double nEff(double n) const
    {
        const double r = std::clamp(rho, 0.0, 0.95);
        return std::max(1.0, n * (1.0 - r) / (1.0 + r));
    }
};

// Tier rows of the calibrated table (tuned::uncertainty::kSigBaseDeg). The first six match
// the face-on decide tiers (shaft_track_assembly.cpp enum Tier); Ball is the address paint.
enum class SigTier : int { Pred = 0, Ray = 1, Band = 2, Recon = 3, Wedge = 4, Seg = 5, Ball = 6 };
// Phase groups (design §4.1).
enum class SigGroup : int { Address = 0, EarlyBs = 1, Backswing = 2, Top = 3, Downswing = 4,
                            Impact = 5, Through = 6, Finish = 7 };

// The table lookup: σθ (deg) at rotation rate |θ̇| (deg/frame), and pGross.
inline double sigTableDeg(SigTier t, SigGroup g, double absThetaDotDegPerFrame)
{
    const int ti = std::clamp(int(t), 0, tuned::uncertainty::kTiers - 1);
    const int gi = std::clamp(int(g), 0, tuned::uncertainty::kGroups - 1);
    return tuned::uncertainty::kSigBaseDeg[ti][gi]
         + tuned::uncertainty::kSigSlope[ti] * std::max(0.0, absThetaDotDegPerFrame);
}
inline double pGrossTable(SigTier t, SigGroup g)
{
    const int ti = std::clamp(int(t), 0, tuned::uncertainty::kTiers - 1);
    const int gi = std::clamp(int(g), 0, tuned::uncertainty::kGroups - 1);
    return tuned::uncertainty::kPGross[ti][gi];
}

} // namespace pinpoint::analysis
