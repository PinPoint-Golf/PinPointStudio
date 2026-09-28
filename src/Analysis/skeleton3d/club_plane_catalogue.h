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

// The club's catalogue swing plane: the downswing shaft plane's inclination to the ground, by
// the club's LENGTH (skeleton3d_shaft_branch_design.md §3.1a). Keyed on length from the club record,
// not the club's name: the corpus club labels are unreliable (a July "DRIVER" was a taped 7-iron).
//
// The two measured points are the fused downswing planes of the 24 two-camera corpus swings
// (shaft_fusion_design.md): 7-iron (0.94 m) 60.3°, wedge (0.89 m) 62.8°. Longer clubs are typical
// values, not measured here — `calibrated` says which side of that line a length falls.

#include <algorithm>
#include <array>
#include <utility>

namespace pinpoint::skeleton3d {

struct CataloguePlane {
    double inclDeg = 60.3;
    bool   calibrated = false;   // inside the measured pair (wedge 0.89 m … 7-iron 0.94 m)
};

inline CataloguePlane clubPlaneInclDeg(double lengthM)
{
    // (length m, inclination °), ascending length.
    static constexpr std::array<std::pair<double, double>, 4> kTable { {
        { 0.89, 62.8 },     // wedge — measured
        { 0.94, 60.3 },     // 7-iron — measured
        { 1.02, 56.0 },     // fairway wood / hybrid — typical
        { 1.12, 50.0 },     // driver — typical
    } };
    CataloguePlane out;
    if (!(lengthM > 0.5)) return out;                      // unknown: the 7-iron, uncalibrated
    const double L = std::clamp(lengthM, kTable.front().first, kTable.back().first);
    for (size_t i = 0; i + 1 < kTable.size(); ++i)
        if (L <= kTable[i + 1].first) {
            const double w = (L - kTable[i].first) / (kTable[i + 1].first - kTable[i].first);
            out.inclDeg = kTable[i].second + w * (kTable[i + 1].second - kTable[i].second);
            break;
        }
    out.calibrated = lengthM >= kTable[0].first - 1e-9 && lengthM <= kTable[1].first + 1e-9;
    return out;
}

} // namespace pinpoint::skeleton3d
