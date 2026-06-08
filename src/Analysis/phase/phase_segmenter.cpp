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

#include "phase_segmenter.h"

#include <QQuaternion>
#include <algorithm>
#include <cmath>

namespace pinpoint::analysis {
namespace {

// Shortest-arc angle (rad) between two orientations.
double quatAngle(const QQuaternion &a, const QQuaternion &b)
{
    double d = std::abs(static_cast<double>(QQuaternion::dotProduct(a, b)));
    return 2.0 * std::acos(std::clamp(d, 0.0, 1.0));
}

int nearestIndex(const std::vector<int64_t> &grid, int64_t t)
{
    if (t <= grid.front()) return 0;
    if (t >= grid.back())  return static_cast<int>(grid.size()) - 1;
    const auto it = std::lower_bound(grid.begin(), grid.end(), t);
    const int hi = static_cast<int>(it - grid.begin());
    const int lo = hi - 1;
    return (t - grid[lo] <= grid[hi] - t) ? lo : hi;
}

} // namespace

std::vector<PhaseEvent> PhaseSegmenter::segment(const FusedStreams &s, int64_t impactUs)
{
    std::vector<PhaseEvent> out;
    const std::vector<int64_t> &grid = s.timeGrid;
    const int N = static_cast<int>(grid.size());
    if (N < 3)
        return out;

    const int64_t clampedImpact = std::clamp(impactUs, grid.front(), grid.back());
    const int idxImpact = nearestIndex(grid, clampedImpact);

    // Reference segment for motion: most-distal lead hand, else forearm.
    const SegmentStream *ref = s.streamFor(SegmentRole::LeadHand);
    if (!ref) ref = s.streamFor(SegmentRole::LeadForearm);
    if (!ref || static_cast<int>(ref->qAnat.size()) != N) {
        // No usable motion stream — anchor what we can.
        out.push_back({ Phase::Address, grid.front(), 0.3f });
        out.push_back({ Phase::Impact,  clampedImpact, 1.0f });
        out.push_back({ Phase::Finish,  grid.back(),   0.3f });
        return out;
    }
    const std::vector<QQuaternion> &q = ref->qAnat;
    const double dt = std::max(static_cast<double>(grid[1] - grid[0]) * 1e-6, 1e-6);

    // Smoothed angular speed (rad/s) for motion-onset detection.
    std::vector<double> w(N, 0.0);
    for (int i = 1; i < N; ++i)
        w[i] = quatAngle(q[i - 1], q[i]) / dt;
    std::vector<double> ws(N, 0.0);
    const int half = 2;
    for (int i = 0; i < N; ++i) {
        double sum = 0.0; int cnt = 0;
        for (int j = std::max(0, i - half); j <= std::min(N - 1, i + half); ++j) { sum += w[j]; ++cnt; }
        ws[i] = sum / cnt;
    }
    const double wmax = *std::max_element(ws.begin(), ws.end());
    const double onset = std::max(0.15 * wmax, 0.30);   // rad/s

    // Address = settle just before sustained motion onset (fallback: grid start).
    int addr = 0;
    for (int i = 0; i < idxImpact; ++i)
        if (ws[i] > onset) { addr = std::max(0, i - 1); break; }

    // Top = lead-hand orientation furthest from Address within (addr, impact].
    int top = addr;
    double maxDist = -1.0;
    for (int i = addr; i <= idxImpact; ++i) {
        const double d = quatAngle(q[addr], q[i]);
        if (d > maxDist) { maxDist = d; top = i; }
    }
    const float topConf = static_cast<float>(std::clamp(maxDist / 2.0, 0.0, 1.0));

    out.push_back({ Phase::Address, grid[addr],     1.0f });
    out.push_back({ Phase::Top,     grid[top],      topConf });
    out.push_back({ Phase::Impact,  clampedImpact,  1.0f });
    out.push_back({ Phase::Finish,  grid.back(),    1.0f });
    return out;
}

} // namespace pinpoint::analysis
