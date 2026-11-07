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

#include "session_series_types.h"                             // kNaN

#include "../../Metrics/variability/variability_grader.h"     // VariabilityInput

#include <QString>

#include <vector>

// CLUBS AND THE VARIABILITY GRADE (Mark's rulings R2a and R2b, 7 Nov; docs/design/system/variability.md,
// "Where a consistency mark is allowed: Clubs"). Spread is never graded across clubs, so a display that
// holds several clubs' swings is graded one of two ways:
//
//   R2a  POOLED WITHIN-CLUB. Each value less its club's median, pooled across clubs: within-club spread,
//        one grade. The Consistency matrix's sampling, exactly: consistencyResiduals() does the
//        centring, so a club with fewer than kConsistencyMinClubShots (3) swings carrying its label is
//        left out, and the club's median includes the swing itself.
//   R2b  PER CLUB. One window per club, each graded on its own and badged with the club's name. A club
//        under N_MIN still gets its window: the grader gives it grade 0, "Not enough swings".
//
// These only build the grader's input; the caller still supplies the scale (a corridor is per club, so
// for R2b it is set on each window) and grades it. `base` is copied into every input it returns, so the
// scale type, a propagated σ or a Needle grade set there carry through; only the readings are replaced.
//
// A swing's club label is compared exactly; a blank label is a club of its own (as consistencyResiduals
// groups). Swings are in strike order and keep it.

namespace pinpoint::charts {

// One swing's scalar reading. A non-finite value is a missing reading.
struct VariabilityClubValue {
    QString club;
    double  value = kNaN;
};

// One swing's time series on the variability::kTraceGridPoints grid (top → impact) and its time-to-peak in
// real ms. A curve off the grid is a missing reading here (not the grader's MalformedInput: one bad swing
// does not cost the window its grade).
struct VariabilityClubCurve {
    QString             club;
    std::vector<double> curve;
    double              peakTimeMs = kNaN;
};

// One club's window, for R2b.
struct VariabilityClubWindow {
    QString                       club;
    int                           readings = 0;   // swings with a reading; what the order sorts by
    variability::VariabilityInput input;
};

// R2a. `values` holds the residuals of the swings that count (finite, of a club with ≥ 3 swings), strike
// order. ScaleType::Circular in `base`: each club is first centred on its circular mean, so a club that
// straddles ±180° is not split by the seam.
variability::VariabilityInput pooledWithinClubInput(const std::vector<VariabilityClubValue> &swings,
                                                    const variability::VariabilityInput &base = {});

// R2a for a trace: residual curves about each club's pointwise median curve, and residual peak times
// about its median peak time. `peakValues` are the ORIGINAL curves' peaks (variability::tracePeak), so the
// TRACE_AMP_FRACTION rung scales against the swing, not against a residual that peaks near zero. A swing
// is a reading only with a whole curve on the grid and a finite peak time; one without is left out of
// both medians (it still counts towards its club's 3).
variability::VariabilityInput pooledWithinClubTraceInput(const std::vector<VariabilityClubCurve> &swings,
                                                         const variability::VariabilityInput &base = {});

// R2b. Clubs ordered by readings descending, then club label ascending. A club with no reading at all is
// left out (nothing to badge). `input.values` holds the club's values in strike order, missing ones
// included (the grader drops them).
std::vector<VariabilityClubWindow> perClubInputs(const std::vector<VariabilityClubValue> &swings,
                                                 const variability::VariabilityInput &base = {});

// R2b for a trace: `input.curves` and `input.peakTimesMs` per club; a curve off the grid is replaced by
// a grid of NaN, so the grader drops that swing. `input.peakValues` is left empty (each curve's own peak).
std::vector<VariabilityClubWindow> perClubTraceInputs(const std::vector<VariabilityClubCurve> &swings,
                                                      const variability::VariabilityInput &base = {});

} // namespace pinpoint::charts
