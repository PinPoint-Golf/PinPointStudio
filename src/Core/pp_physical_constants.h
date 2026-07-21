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

// Real-world physical constants — quantities fixed by the rules of golf or by
// physics, NOT by us.
//
// Deliberately SEPARATE from pp_tuned_constants.h. That header is for parameters
// tuned during the validation programme and then frozen — values we chose, that a
// SwingLab sweep may legitimately override. Nothing here is tunable or sweepable:
// a golf ball's diameter is not a knob, and an override mechanism pointed at one
// would be a bug surface, not a feature. Anything nominal-until-calibrated (the
// inter-ear ruler, club length defaults) belongs in pp_tuned_constants.h instead,
// precisely because it IS a guess we expect to replace.
//
// LAYERING: same contract as pp_tuned_constants.h — src/Core is the lowest common
// layer, and this header carries ONLY numeric literals. No Qt, no OpenCV, no
// module types, so the Qt-only analysis modules and their standalone unit tests
// can include it freely.

namespace pinpoint::physical {

// Golf ball diameter, mm. The R&A / USGA Rules of Golf specify a MINIMUM diameter
// of 1.680 in (42.67 mm); balls are manufactured at essentially that minimum, so
// it doubles as the actual size.
//
// THE px→mm SCALE PRIMITIVE. This is the one real-world ruler a face-on capture
// gets for free, with no camera calibration:
//
//     mmPerPx = kGolfBallDiameterMm / (2 · radiusPx)
//
// Exact only at the BALL'S ground-plane depth — which face-on is essentially the
// feet's depth, making it the right ruler for stance geometry and the wrong one
// for anything at a different distance from the camera. It also rests on a small
// radius measurement (~9.5 px at 1280 wide), so ±1 px of radius error is roughly
// 10 % of scale: median it over many frames, and treat the result as an estimate,
// not a calibration.
inline constexpr double kGolfBallDiameterMm = 42.67;

} // namespace pinpoint::physical
