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

// AN ANGLE OF A LINE, read once (issue #116). Shaft lean is the angle of the grip→head line from
// straight down, so as a READING it has one range, (−180°, 180°]. Its curve does not: the producer
// unwraps it through the top of the swing (analysis_pipeline.cpp buildShaftLeanSeries) so the line
// does not jump 360° where the shaft passes straight up, and a glitch frame can leave the whole curve
// a turn out away from impact — 2026-10-08 Wrist_01 s3 read 354.5° at its (early) ladder impact and
// 383° at address. So the curve is left continuous and every reader that takes a SINGLE number from
// a series marked MetricSeries::lineAngle folds it here: the phase grid's values
// (measure_sample.cpp) and the review chart's reading tiles (charts_traces_presenter.cpp).
//
// Why a fourth wrap helper beside impact_geom_detail::wrap180, position_fit_detail::wrap180 and
// kin_detail::wrapDeg: those three are each a private detail of their own Club header (one of them
// behind OpenCV), and all three are [−180°, 180°) — they fold a DIFFERENCE, where which end owns
// ±180° does not matter. This one is a public reading in a std-only header both the Diagnostics
// grid and the Gui charts can include, and it is (−180°, 180°] so a lean of exactly 180° — the
// shaft straight up — reads +180°, the sign the producer's own impact anchor gives it.

#include <cmath>

namespace pinpoint::analysis {

// 354.5° → −5.5°, −180° → 180°, ±540° → 180°; anything already in range is returned unchanged.
inline double wrapLeanDeg(double deg)
{
    const double r = std::remainder(deg, 360.0);   // [−180, 180]
    return r <= -180.0 ? r + 360.0 : r;
}

} // namespace pinpoint::analysis
