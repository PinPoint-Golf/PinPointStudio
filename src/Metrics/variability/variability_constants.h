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

#include <array>

// THE VARIABILITY CONSTANTS (docs/design/variability-mark/spec.md §8.10). Every number the grader,
// typicality and trend use, and nothing else: a threshold that is not here is not in the spec.
//
// All are PROVISIONAL. The spec says they are to be calibrated against logged sessions before being
// finalised, so each carries TODO(calibrate) and a calibration proposal is an edit to this file only.
// The spec's own SCREAMING names are in the comments, so a spec reader can find each one.

namespace pinpoint::variability {

// The five grade bands, as four edges between them. `edges[0]` divides grade 5 from 4, `edges[3]`
// divides 2 from 1. An edge value belongs to the TIGHTER side, as the spec's tables read:
//   LowerIsTighter  (ρ, ρm):  grade 5 is ρ ≤ edges[0] … grade 1 is ρ > edges[3]
//   HigherIsTighter (s′):     grade 5 is s′ ≥ edges[0] … grade 1 is s′ < edges[3]
struct GradeBands {
    enum class Sense { LowerIsTighter, HigherIsTighter };
    Sense                 sense;
    std::array<double, 4> edges;
};

// Every band table in ONE struct, so a calibration proposal is a one-place change.
struct VariabilityBands {
    GradeBands ratio;    // §8.2 scalar ρ, §8.3 trace ρₐ / ρₜ, §8.5 measurement ρm
    GradeBands ordinal;  // §8.4 chance-corrected modal share s′
};

// TODO(calibrate) "Scalar and measurement bands" 0.25 / 0.50 / 0.75 / 1.00; "Ordinal bands"
// 0.85 / 0.65 / 0.45 / 0.25.
inline constexpr VariabilityBands kBands{
    {GradeBands::Sense::LowerIsTighter,  {0.25, 0.50, 0.75, 1.00}},
    {GradeBands::Sense::HigherIsTighter, {0.85, 0.65, 0.45, 0.25}},
};

inline constexpr int    kNMin              = 8;     // N_MIN: swings for any grade, and typicality. TODO(calibrate)
inline constexpr int    kNWindow           = 20;    // N_WINDOW: Home and trend windows, in swings. TODO(calibrate)
inline constexpr double kTraceAmpFraction  = 0.10;  // TRACE_AMP_FRACTION × mean peak, §8.3. TODO(calibrate)
inline constexpr double kTimingScaleMs     = 20.0;  // TIMING_SCALE_MS, §8.3. TODO(calibrate)
inline constexpr double kMeasTolFraction   = 0.25;  // MEAS_TOL_FRACTION × corridor half-width, §8.5. TODO(calibrate)
inline constexpr double kHysteresis        = 0.05;  // HYSTERESIS past a band edge, §8.9. TODO(calibrate)
inline constexpr int    kTrendMinSessions  = 3;     // TREND_MIN_SESSIONS, §8.8. TODO(calibrate)
inline constexpr double kTrendSeMult       = 2.0;   // TREND_SE_MULT, §8.8. TODO(calibrate)
inline constexpr double kTypicalityUsualZ  = 1.0;   // |z| ≤ 1 is "Your usual", §7.4. TODO(calibrate)
inline constexpr double kTypicalityOffZ    = 2.0;   // |z| ≤ 2 is "A little off", above is "Unusual". TODO(calibrate)

// The trace grid (§8.3 step 1: top → impact resampled to 101 points). A grid, not a threshold, but
// it is a spec number and lives with the others.
inline constexpr int    kTraceGridPoints   = 101;   // TODO(calibrate)

// The most sessions a trend reads (§6.1: the spread funnel shows at most 12, and older ones roll off),
// so the trend word and the funnel it labels are computed over the same sessions. A layout number
// from the spec, not a statistical threshold, so it is not for calibration.
inline constexpr int    kFunnelMaxSessions = 12;

// Mark's rulings on the Session 5 audit (7 Nov; docs/design/variability-mark/decisions.md §2), not spec
// §8.10 thresholds, so not for calibration:
//   R1  a cell that pools readings takes the grade at its loosest third, the 67th-percentile ρ of its
//       graded readings (nearest rank: index ⌈0.67·n⌉ − 1 of the ρ sorted ascending).
//   §6.2 the Your-group Cell draws at most 12 ghosts, the most recent other swings.
inline constexpr double kCellLoosestPercentile = 0.67;
inline constexpr int    kTypicalityMaxGhosts   = 12;

} // namespace pinpoint::variability
