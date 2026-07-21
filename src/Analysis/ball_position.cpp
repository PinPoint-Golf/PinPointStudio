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

#include "ball_position.h"

#include "../Core/pp_physical_constants.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

namespace pinpoint::analysis {

namespace {

// Median by value (copies — these vectors are at most a few hundred entries and
// this runs once per shot). nth_element would mutate the caller's data, which
// matters here because the same sample set is walked twice (cluster, then accept).
double medianOf(std::vector<double> v)
{
    if (v.empty())
        return 0.0;
    std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end());
    return v[v.size() / 2];
}

} // namespace

BallPositionResult computeBallPosition(const BallTrack2D &ball,
                                       QPointF leadHeelPx, QPointF trailHeelPx,
                                       int64_t addressUs, int frameW, int frameH,
                                       const BallPositionConfig &cfg)
{
    BallPositionResult res;
    if (!cfg.enabled || ball.frames.empty() || frameW <= 0 || frameH <= 0)
        return res;

    // Sample selection. With an Address event, take the samples around it. With
    // none (seg.conf == 0, or a ladder missing Address), fall back to everything
    // before launch — the ball is stationary by construction until it is struck,
    // so the whole pre-launch period is one address measurement.
    const auto inWindow = [&](const BallSample2D &s) {
        if (!s.found)
            return false;
        if (addressUs >= 0)
            return std::llabs(s.t_us - addressUs) <= cfg.addrWindowUs;
        return ball.launchTUs < 0 || s.t_us < ball.launchTUs;
    };

    std::vector<double> xs, ys;
    for (const BallSample2D &s : ball.frames) {
        if (!inWindow(s))
            continue;
        xs.push_back(double(s.center.x()) * frameW);
        ys.push_back(double(s.center.y()) * frameH);
    }
    if (int(xs.size()) < cfg.minSamples)
        return res;

    // Component-wise median — order-independent, so a detector warm-up mis-lock
    // on the opening frames cannot poison the estimate (ball_anchor.cpp's pass-1
    // rationale, and the failure it was written against).
    const double medX = medianOf(xs), medY = medianOf(ys);

    // Second pass: keep only the in-cluster samples, and take the radius from the
    // SAME set — a mis-locked sample's radius is as wrong as its centre, so the
    // ruler must not be built from rejected frames.
    std::vector<double> cx, cy, radii;
    for (const BallSample2D &s : ball.frames) {
        if (!inWindow(s))
            continue;
        const double bx = double(s.center.x()) * frameW;
        const double by = double(s.center.y()) * frameH;
        if (std::hypot(bx - medX, by - medY) > cfg.maxJumpPx)
            continue;                                    // off-cluster — mis-lock
        cx.push_back(bx);
        cy.push_back(by);
        if (s.radiusNorm > 0.f)
            radii.push_back(double(s.radiusNorm) * frameW);
    }
    if (int(cx.size()) < cfg.minSamples)
        return res;

    res.samples       = int(cx.size());
    res.addressBallPx = QPointF(medianOf(cx), medianOf(cy));

    // The ball-diameter ruler, resolved independently of the heel geometry: a
    // swing with a good ball lock but unusable feet still yields a scale, which
    // is exactly what foot_metrics wants for stance width in mm.
    const double medRadiusPx = medianOf(radii);
    if (medRadiusPx > 0.0)
        res.mmPerPx = pinpoint::physical::kGolfBallDiameterMm / (2.0 * medRadiusPx);

    // Project onto the heel line. |v|² is the squared stance width, so `frac`
    // is the ball's position along the stance in units of stance width — the
    // same denominator foot_metrics reports as stanceWidth.
    const double vx = trailHeelPx.x() - leadHeelPx.x();
    const double vy = trailHeelPx.y() - leadHeelPx.y();
    const double vv = vx * vx + vy * vy;
    if (vv <= 0.0)
        return res;   // degenerate heel pair — ruler survives, position does not

    const double frac = ((res.addressBallPx.x() - leadHeelPx.x()) * vx
                       + (res.addressBallPx.y() - leadHeelPx.y()) * vy) / vv;

    // Plausibility. A ball well outside the stance is a detector failure, not an
    // exotic setup — refuse rather than publish it. Inside the band the value is
    // NOT clamped: forward of the lead heel is a real driver position.
    if (frac < cfg.fracLo || frac > cfg.fracHi)
        return res;

    res.fracOfStance = frac;
    res.valid        = true;
    return res;
}

} // namespace pinpoint::analysis
