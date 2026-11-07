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

#include "variability_result.h"   // Basis

#include <QString>

#include <vector>

// THE CONSISTENCY TREND (docs/design/variability-mark/spec.md §7.3, §8.8): is ρ falling session by
// session by more than the noise?
//
// At most the latest kFunnelMaxSessions (12) points of that run are read, the sessions the §6.1
// funnel draws, so the trend word never describes sessions its funnel has rolled off.
//
// Like-for-like only. A session point carries its context, basis and scale keys; a change in any of
// them is a BREAK, and the trend reads only the latest unbroken run — a ρ against a driver corridor
// and a ρ against a wedge corridor are not one series.
//
// The arithmetic. Sessions in the run are indexed 0..m−1 (equal spacing: a session is the unit, not
// a day). The change is the least-squares slope × span (m − 1). Its standard error comes from the
// per-session SEs the spec gives, SE_i ≈ ρ_i/√(2(n_i − 1)), by weighted propagation through the
// unweighted least-squares slope:
//     Var(slope)  = Σ (t_i − t̄)² SE_i² / (Σ (t_i − t̄)²)²
//     SE(change)  = span · √Var(slope)
// Tightening or Loosening only when |change| > TREND_SE_MULT × SE(change); otherwise Holding.
//
// Ratio bases only (Scalar, Trace, Measurement): the SE formula is the SE of an SD ratio and means
// nothing for an ordinal s′, so an ordinal series is reported Insufficient with `ordinalBasis` set.

namespace pinpoint::variability {

struct TrendPoint {
    double  rho = 0.0;       // the session's ρ (VariabilityResult::rho or ::rhoM)
    int     n   = 0;         // the swings behind it
    QString contextKey;      // the norm context (club, shot type)
    Basis   basisKind = Basis::Scalar;
    QString scaleKey;        // which scale ρ was taken against (corridor id, "variabilityScale", "cv")
};

enum class TrendDirection { Insufficient, Holding, Tightening, Loosening };

struct TrendResult {
    TrendDirection direction = TrendDirection::Insufficient;
    int    sessions  = 0;     // points in the latest unbroken run that were used
    int    runStart  = -1;    // index in the input of the run's first point
    double firstRho  = 0.0;
    double lastRho   = 0.0;
    double slope     = 0.0;   // ρ per session
    double change    = 0.0;   // slope × span
    double changeSe  = 0.0;
    bool   ordinalBasis = false;

    // "ρ 0.62 → 0.38 over 6 sessions" (§7.5); empty when Insufficient. The arrow is an argument.
    QString basisString(int decimals = 2) const;
};

// `points` oldest first. A point with a non-finite or negative ρ, or n < 2 (no SE), is not a session
// with a reading and is skipped; it does not break the run.
TrendResult variabilityTrend(const std::vector<TrendPoint> &points);

} // namespace pinpoint::variability
