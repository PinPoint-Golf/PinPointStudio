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

// WHAT KIND OF NUMBER A METRIC IS, as the variability grader needs to know it
// (docs/design/variability-mark/spec.md §8.2, §8.4, §8.10). The grade basis is not one formula:
//
//   Interval    — the default. A plain sample SD over a corridor half-width (or `variabilityScale`).
//                 No CV fallback, because an interval scale has no true zero to divide by.
//   Ratio       — a strictly positive quantity with a TRUE zero: a speed, a distance, a duration,
//                 smash factor. The one scale on which the CV rung of §8.2 step 4 is allowed.
//   Circular    — an angle that WRAPS at ±180°, whose readings can cross the seam. Spread is the
//                 circular SD √(−2 ln R̄); a linear SD over 179° and −179° would read a 2° spread as
//                 a 358° one. A bounded joint angle is NOT circular — it never reaches the seam.
//   Ordinal     — values are an ORDER (the kinematic sequence). Graded on chance-corrected modal
//                 share (§8.4), never on an SD.
//   Categorical — values are CLASSES (a classified outcome). Graded as Ordinal is (§8.4).
//
// The scale type is a fact about the QUANTITY, never about a producer, so it lives on the
// MetricDescriptor beside the unit. Std-only on purpose: the grader beside this header and the
// catalogue both include it, and neither should have to pull Qt in to say what kind of number
// something is. The tokens are the spec's own lower-case spellings, so a spec reader and a log
// reader see the same word.

#include <optional>
#include <string_view>

namespace pinpoint::variability {

enum class ScaleType { Interval, Ratio, Circular, Ordinal, Categorical };

// The spec's token (§8.10): "interval" | "ratio" | "circular" | "ordinal" | "categorical".
constexpr std::string_view scaleTypeToken(ScaleType t)
{
    switch (t) {
    case ScaleType::Interval:    return "interval";
    case ScaleType::Ratio:       return "ratio";
    case ScaleType::Circular:    return "circular";
    case ScaleType::Ordinal:     return "ordinal";
    case ScaleType::Categorical: return "categorical";
    }
    return "interval";
}

// Inverse of scaleTypeToken(). Exact lower-case match only, as the spec spells them; anything else
// (including "Ratio") is nullopt rather than a guess, so a mistyped token is caught where it is
// read instead of quietly grading as Interval.
constexpr std::optional<ScaleType> scaleTypeFromToken(std::string_view token)
{
    if (token == "interval")    return ScaleType::Interval;
    if (token == "ratio")       return ScaleType::Ratio;
    if (token == "circular")    return ScaleType::Circular;
    if (token == "ordinal")     return ScaleType::Ordinal;
    if (token == "categorical") return ScaleType::Categorical;
    return std::nullopt;
}

} // namespace pinpoint::variability
