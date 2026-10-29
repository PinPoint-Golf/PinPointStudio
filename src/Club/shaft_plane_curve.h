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

// THE SWING PLANE AS A CURVE — the plane through the target line and the shaft, at every instant,
// as one smooth line from address to just past impact (Mark, 29 Oct 2026: "the swing plane curve
// should be smooth so take an approach that seeks to bridge gaps plausibly").
//
// WHAT IS MEASURED. At each down-the-line frame, the plane that holds the shaft and the target
// direction. With the camera looking down the line that plane IS the camera's view plane of the
// shaft — the plane through the view ray and the shaft's image line — so its angle to the ground
// comes from the down-the-line image angle alone, exactly as the address plane does in
// shaft_fusion.h. Three things follow, all of them measured before this was written (29 Oct, three
// library swings):
//   · it is DENSE: every measured down-the-line frame, not only the fused ones (Jess 07-20 s4: 291
//     down-the-line frames against 68 fused-and-measured, four of them in the downswing);
//   · its angle does not change when a down-the-line band is read mirrored about the vertical
//     (|n.z| is symmetric under θ → π − θ for a level camera), which is what put ±40–50° spikes
//     into every per-frame 3-D reading of the same swings;
//   · at address it is the address plane, by construction.
// Where the shaft is on a plane through the target line — the coaching picture of an on-plane
// swing — this is that plane's angle. A plane fitted to the shaft's own MOTION was tried first
// and dropped: over a 20–40° sweep it moved ±10° on noise its σ could not see, and it needs fused
// frames the downswing rarely has.
//
// WHERE IT IS BLIND, AND HOW THAT IS BRIDGED. The plane through the target line and the shaft is
// undefined when the shaft points along the line (P2, the top for a parallel club) and is not the
// swing plane when the shaft is horizontal (any top: a laid-off club at the top makes that plane
// flat). Both show in one number per frame, the CONDITIONING q = ρ·|u_z| — the shaft's
// foreshortening in the down-the-line view times its vertical component. Each frame enters the
// fit with weight (q / σ)², σ the plane angle's own σ from the tracker's σθ, so a well-seen
// inclined shaft speaks loudly and a horizontal one barely; below minCond it does not enter at
// all. The curve itself is a penalised spline on a uniform knot grid (second-difference penalty,
// the fitEtaCurve form in shaft_fusion.h with weights), refitted with Tukey's biweight so a frame
// far off the curve stops pulling it. Where no accepted frame lies within bridgeMs the curve is
// BRIDGED — smooth, from the frames either side — and says so: `supported` is 0 there, which the
// series carries as its validity mask (drawn dashed, read by no reducer).
//
// THE σ. Each frame's effective noise is σ/q, so the normal-equations matrix is a precision and
// its inverse a covariance; the curve's σ is that posterior standard error scaled by the robust
// residual scale (≥ 1). It ignores the frame-to-frame correlation of the tracker's angle and the
// bias the smoothing itself introduces, so it is a floor, not a bound.
//
// Pure std header — no Qt, no tracker types. The caller hands it observations.

#include "shaft_fusion.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace pinpoint::analysis::planecurve {

using fusion::kNan;
using fusion::kPi;

struct Config {
    bool   enabled       = true;
    double knotMs        = 20.0;   // knot spacing
    double lambda        = 5.0;    // second-difference penalty, in the units of the weights
    double tukeyK        = 3.0;    // biweight cut, in robust residual scales
    int    iterations    = 6;
    double minCond       = 0.25;   // q below this: the frame does not enter
    double bridgeMs      = 20.0;   // a grid point with no accepted frame this close is bridged
    double sigmaFloorDeg = 0.5;    // a frame's plane-angle σ is never taken below this
    double acceptFrac    = 0.25;   // an accepted frame keeps at least this share of its weight
    double stepMs        = 5.0;    // output grid
    int    minFrames     = 12;
};

// One down-the-line frame, as the fit sees it.
struct Obs {
    int64_t t_us     = 0;
    double  inclDeg  = kNan;   // plane through the target line and the shaft, to the ground
    double  sigmaDeg = kNan;   // its σ
    double  cond     = kNan;   // q = ρ·|u_z|
};

struct Curve {
    bool fitted = false;
    std::vector<int64_t> t_us;
    std::vector<double>  inclDeg;
    std::vector<double>  sigmaDeg;
    std::vector<uint8_t> supported;   // 1 = an accepted frame within bridgeMs; 0 = bridged
    int    n         = 0;             // frames offered (after minCond)
    int    nRejected = 0;             // frames the biweight finally gave < 10 % of their weight
    double scale     = kNan;          // robust residual scale, in σ units
};

// The plane through the camera's view ray and the image line at θ, as an angle to the ground.
inline double viewPlaneInclDeg(const fusion::Camera &cam, double theta)
{
    const fusion::Vec3 n = cam.d.cross(fusion::imageDir(cam, theta)).unit();
    return std::acos(std::clamp(std::fabs(n.z), 0.0, 1.0)) * 180.0 / kPi;
}

// … and its σ from the image angle's, by central difference.
inline double viewPlaneInclSigmaDeg(const fusion::Camera &cam, double theta, double sigmaThetaDeg)
{
    const double s = sigmaThetaDeg * kPi / 180.0;
    return 0.5 * std::fabs(viewPlaneInclDeg(cam, theta + s) - viewPlaneInclDeg(cam, theta - s));
}

// THE HAND PLANE — the plane through the ball's target line and the hands (Mark, 29 Oct 2026: "the
// hand path moves forward at the top towards the ball which obviously steepens the plane — that is
// how we diagnose over the top. Can that be fused or taken into account?").
//
// Down the line, the plane through the ball line and a point is the view plane of the image line
// from the ball to that point, so it is the same view-plane angle as the shaft's, read off the
// image direction ball → grip. Two things make it the shaft curve's partner:
//   · at address the shaft runs from the ball to the hands, so the two are the same plane;
//   · wherever the shaft lies ALONG the target line — the top, and the end-on stretch after it,
//     where the shaft plane is undefined — the plane through the ball line that holds the shaft is
//     the plane through the ball line and the hands, because a line parallel to the ball line lies
//     in every plane through it that holds one of its points. The hands are seen there; the shaft
//     is not.
// So the hand frames fill the shaft curve exactly where the shaft frames go quiet (their weight is
// the shaft frame's missing share, 1 − q), and as their own curve they show where the hands go:
// a downswing that comes down on a steeper hand plane than it went up on is the over-the-top move,
// which the shaft alone hides when the club re-routes under the hands before delivery.
inline double handPlaneInclDeg(const fusion::Camera &cam, double gx, double gy, double bx, double by)
{
    return viewPlaneInclDeg(cam, std::atan2(gy - by, gx - bx));
}

// … and its σ: a grip placed to ±sigmaPx at distance d turns the image angle by sigmaPx / d.
inline double handPlaneInclSigmaDeg(const fusion::Camera &cam, double gx, double gy, double bx, double by,
                                    double sigmaPx)
{
    const double d = std::hypot(gx - bx, gy - by);
    if (!(d > 0)) return kNan;
    return viewPlaneInclSigmaDeg(cam, std::atan2(gy - by, gx - bx), std::atan(sigmaPx / d) * 180.0 / kPi);
}

// THE SHOULDER PLANE — the plane through the ball's target line and the golfer's TRAIL shoulder at
// address (club_delivery_chart_design.md §2.3, where it is the "shoulder line"; named the plane here
// so it is not read as the shoulders' alignment to the target. The trail shoulder is Mark's answer
// to the design's Q1, 29 Oct 2026). The coaching slot is the wedge between the address plane and this one: a club above it
// is steep, a club below the address line is under the plane. It is the hand plane's maths on a
// shoulder point — the view plane of the image line from the down-the-line ball to the shoulder —
// so it carries the hand plane's caution too: an absolute angle off an uncalibrated camera, read
// against the same golfer's own curve on the same camera.
inline double shoulderPlaneInclDeg(const fusion::Camera &cam, double sx, double sy, double bx, double by)
{
    return handPlaneInclDeg(cam, sx, sy, bx, by);
}

inline double shoulderPlaneInclSigmaDeg(const fusion::Camera &cam, double sx, double sy, double bx, double by,
                                        double sigmaPx)
{
    return handPlaneInclSigmaDeg(cam, sx, sy, bx, by, sigmaPx);
}

// The trail shoulder's COCO body index: the right shoulder (6) for a right-handed golfer, the left
// (5) for a left-handed one. `handedness` as the job carries it (2 = left); unknown reads as right,
// as leadIsLeft does everywhere else in the pipeline.
inline int trailShoulderJoint(int handedness) { return handedness == 2 ? 5 : 6; }

// The reading from the address hold: the trail shoulder's image points (pixels, time order, only
// frames where the pose saw it), of which the last `lastN` are read — the end of the hold, the ball
// finder's window. The point is the per-axis median. Its σ is the shoulder's placement scatter
// ACROSS the ball → shoulder line (1.4826 · MAD; the scatter along the line does not turn it),
// not divided by √n: the frames of a still hold are one pose of one image, not independent
// placements, so their median is placed no better than one of them. Floored at minSigmaPx — a
// pose that repeats itself to the pixel has not placed the joint to the pixel.
struct ShoulderPlaneReading {
    bool   ok = false;
    double deg = kNan, sigmaDeg = kNan;
    double sx = kNan, sy = kNan, sigmaPx = kNan;
    int    n = 0;
};

inline ShoulderPlaneReading shoulderPlaneFromHold(const fusion::Camera &cam,
                                                  const std::vector<std::pair<double, double>> &pts,
                                                  double bx, double by, size_t lastN = 15,
                                                  double minSigmaPx = 1.0, size_t minFrames = 3)
{
    ShoulderPlaneReading out;
    auto median = [](std::vector<double> v) {
        std::sort(v.begin(), v.end());
        const size_t n = v.size();
        return n ? (n % 2 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2])) : kNan;
    };
    const size_t from = pts.size() > lastN ? pts.size() - lastN : 0;
    std::vector<double> xs, ys;
    for (size_t i = from; i < pts.size(); ++i)
        if (std::isfinite(pts[i].first) && std::isfinite(pts[i].second)) {
            xs.push_back(pts[i].first);
            ys.push_back(pts[i].second);
        }
    out.n = int(xs.size());
    if (xs.size() < minFrames || !std::isfinite(bx) || !std::isfinite(by)) return out;
    out.sx = median(xs);
    out.sy = median(ys);
    const double d = std::hypot(out.sx - bx, out.sy - by);
    if (!(d > 0)) return out;
    const double nx = -(out.sy - by) / d, ny = (out.sx - bx) / d;   // across the line
    std::vector<double> across;
    for (size_t i = 0; i < xs.size(); ++i) across.push_back((xs[i] - out.sx) * nx + (ys[i] - out.sy) * ny);
    const double mid = median(across);
    for (double &a : across) a = std::fabs(a - mid);
    out.sigmaPx  = std::max(1.4826 * median(across), minSigmaPx);
    out.deg      = shoulderPlaneInclDeg(cam, out.sx, out.sy, bx, by);
    out.sigmaDeg = shoulderPlaneInclSigmaDeg(cam, out.sx, out.sy, bx, by, out.sigmaPx);
    out.ok       = std::isfinite(out.deg);
    return out;
}

// How much steeper the hands come down than they went up, at the same hand height: the median hand
// plane over 40–70 % of the hands' rise in the downswing, less the same in the backswing — the
// band handPathLoop reads, for the same reason (near the top every swing loops, near the ball every
// swing converges). Height is the grip's image row, so it needs no ruler.
struct HandFrame { int64_t t_us = 0; double gripY = kNan, inclDeg = kNan; };
struct Steepening { bool ok = false; double deg = kNan, sigmaDeg = kNan; int nUp = 0, nDown = 0; };

inline Steepening handPlaneSteepening(const std::vector<HandFrame> &fr, int64_t takeawayUs, int64_t topUs,
                                      int64_t impactUs, double lo = 0.4, double hi = 0.7, double minRisePx = 40.0)
{
    Steepening out;
    auto median = [](std::vector<double> v) {
        std::sort(v.begin(), v.end());
        const size_t n = v.size();
        return n ? (n % 2 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2])) : kNan;
    };
    std::vector<double> addrY;
    double topY = kNan;
    for (const HandFrame &f : fr) {
        if (!std::isfinite(f.gripY)) continue;
        if (f.t_us < takeawayUs) addrY.push_back(f.gripY);
        else if (f.t_us <= topUs + 30000) topY = std::isfinite(topY) ? std::min(topY, f.gripY) : f.gripY;
    }
    if (addrY.size() > 5) addrY.erase(addrY.begin(), addrY.end() - 5);   // the last of the hold
    const double y0 = median(addrY);
    const double rise = y0 - topY;
    if (!std::isfinite(rise) || rise < minRisePx) return out;
    std::vector<double> up, down;
    for (const HandFrame &f : fr) {
        if (!std::isfinite(f.gripY) || !std::isfinite(f.inclDeg)) continue;
        const double frac = (y0 - f.gripY) / rise;
        if (frac < lo || frac > hi) continue;
        if (f.t_us >= takeawayUs && f.t_us < topUs) up.push_back(f.inclDeg);
        else if (f.t_us >= topUs && f.t_us <= impactUs) down.push_back(f.inclDeg);
    }
    out.nUp = int(up.size()); out.nDown = int(down.size());
    if (out.nUp < 3 || out.nDown < 3) return out;
    const double mu = median(up), md = median(down);
    // σ of each median from its own spread (1.2533 · 1.4826 · MAD / √n).
    auto seMed = [&](const std::vector<double> &v, double m) {
        std::vector<double> d;
        for (double x : v) d.push_back(std::fabs(x - m));
        return 1.2533 * 1.4826 * median(d) / std::sqrt(double(v.size()));
    };
    out.deg = md - mu;
    out.sigmaDeg = std::hypot(seMed(up, mu), seMed(down, md));
    out.ok = true;
    return out;
}

namespace detail {
// Solve A x = b for SPD A (dense, row-major K×K) by Cholesky; also returns diag(A⁻¹) when asked.
inline bool choleskySolve(std::vector<double> A, int K, const std::vector<double> &b,
                          std::vector<double> &x, std::vector<double> *diagInv)
{
    auto at = [&](int i, int j) -> double & { return A[size_t(i) * size_t(K) + size_t(j)]; };
    for (int i = 0; i < K; ++i)
        for (int j = 0; j <= i; ++j) {
            double sum = at(i, j);
            for (int m = 0; m < j; ++m) sum -= at(i, m) * at(j, m);
            if (i == j) { if (sum <= 0) return false; at(i, i) = std::sqrt(sum); }
            else at(i, j) = sum / at(j, j);
        }
    auto solve = [&](const std::vector<double> &rhs, std::vector<double> &out) {
        std::vector<double> y(static_cast<size_t>(K));
        for (int i = 0; i < K; ++i) {
            double sum = rhs[size_t(i)];
            for (int m = 0; m < i; ++m) sum -= at(i, m) * y[size_t(m)];
            y[size_t(i)] = sum / at(i, i);
        }
        out.assign(size_t(K), 0.0);
        for (int i = K - 1; i >= 0; --i) {
            double sum = y[size_t(i)];
            for (int m = i + 1; m < K; ++m) sum -= at(m, i) * out[size_t(m)];
            out[size_t(i)] = sum / at(i, i);
        }
    };
    solve(b, x);
    if (diagInv) {
        diagInv->assign(size_t(K), kNan);
        std::vector<double> e(size_t(K), 0.0), col;
        for (int k = 0; k < K; ++k) {
            e[size_t(k)] = 1.0;
            solve(e, col);
            (*diagInv)[size_t(k)] = col[size_t(k)];
            e[size_t(k)] = 0.0;
        }
    }
    return true;
}
} // namespace detail

// Fit the curve over [fromUs, toUs]. Observations outside a knot of the span are ignored.
inline Curve fitCurve(const std::vector<Obs> &all, int64_t fromUs, int64_t toUs, const Config &cfg)
{
    Curve c;
    if (!cfg.enabled || toUs <= fromUs || cfg.knotMs <= 0 || cfg.stepMs <= 0) return c;
    const int64_t knotUs = int64_t(std::llround(cfg.knotMs * 1000.0));
    std::vector<Obs> obs;
    for (const Obs &o : all)
        if (o.t_us >= fromUs - knotUs && o.t_us <= toUs + knotUs && std::isfinite(o.inclDeg)
            && std::isfinite(o.sigmaDeg) && std::isfinite(o.cond) && o.cond >= cfg.minCond)
            obs.push_back(o);
    c.n = int(obs.size());
    if (c.n < cfg.minFrames) return c;

    const int K = std::max(3, int(std::ceil(double(toUs - fromUs) / double(knotUs))) + 1);
    const double h = double(knotUs);
    const size_t N = obs.size();
    std::vector<int> k0(N);
    std::vector<double> a1(N), sig(N), w0(N), w(N);
    for (size_t i = 0; i < N; ++i) {
        int k = int(std::floor(double(obs[i].t_us - fromUs) / h));
        k = std::max(0, std::min(K - 2, k));
        k0[i] = k;
        a1[i] = std::clamp(double(obs[i].t_us - (fromUs + int64_t(k) * knotUs)) / h, 0.0, 1.0);
        sig[i] = std::max(cfg.sigmaFloorDeg, obs[i].sigmaDeg);
        const double q = obs[i].cond;
        w0[i] = (q * q) / (sig[i] * sig[i]);   // effective σ is σ/q
        w[i] = w0[i];
    }

    std::vector<double> v, diagInv;
    std::vector<double> A;
    double scale = 1.0;
    const int iters = std::max(1, cfg.iterations);
    for (int it = 0; it < iters; ++it) {
        A.assign(size_t(K) * size_t(K), 0.0);
        std::vector<double> b(size_t(K), 0.0);
        auto at = [&](int i, int j) -> double & { return A[size_t(i) * size_t(K) + size_t(j)]; };
        for (size_t i = 0; i < N; ++i) {
            const int k = k0[i];
            const double c0 = 1.0 - a1[i], c1 = a1[i];
            at(k, k) += w[i] * c0 * c0; at(k, k + 1) += w[i] * c0 * c1;
            at(k + 1, k) += w[i] * c1 * c0; at(k + 1, k + 1) += w[i] * c1 * c1;
            b[size_t(k)] += w[i] * c0 * obs[i].inclDeg; b[size_t(k) + 1] += w[i] * c1 * obs[i].inclDeg;
        }
        for (int k = 1; k < K - 1; ++k) {
            const int idx[3] = { k - 1, k, k + 1 }; const double co[3] = { 1.0, -2.0, 1.0 };
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j) at(idx[i], idx[j]) += cfg.lambda * co[i] * co[j];
        }
        for (int k = 0; k < K; ++k) at(k, k) += 1e-6;   // keeps a frameless end SPD
        const bool last = (it == iters - 1);
        if (!detail::choleskySolve(A, K, b, v, last ? &diagInv : nullptr)) return c;
        if (last) break;
        // Tukey's biweight on the standardised residuals, the scale a MAD never below 1.
        std::vector<double> r(N);
        for (size_t i = 0; i < N; ++i) {
            const double fit = (1.0 - a1[i]) * v[size_t(k0[i])] + a1[i] * v[size_t(k0[i]) + 1];
            r[i] = std::fabs(obs[i].inclDeg - fit) / sig[i];
        }
        std::vector<double> rs = r;
        std::nth_element(rs.begin(), rs.begin() + long(N / 2), rs.end());
        scale = std::max(1.0, 1.4826 * rs[N / 2]);
        for (size_t i = 0; i < N; ++i) {
            const double u = r[i] / (cfg.tukeyK * scale);
            w[i] = u < 1.0 ? w0[i] * (1.0 - u * u) * (1.0 - u * u) : 0.0;
        }
    }

    std::vector<int64_t> accepted;
    for (size_t i = 0; i < N; ++i) {
        if (w[i] >= cfg.acceptFrac * w0[i]) accepted.push_back(obs[i].t_us);
        if (w[i] < 0.1 * w0[i]) ++c.nRejected;
    }
    std::sort(accepted.begin(), accepted.end());
    c.scale = scale;
    const int64_t stepUs = int64_t(std::llround(cfg.stepMs * 1000.0));
    const int64_t bridgeUs = int64_t(std::llround(cfg.bridgeMs * 1000.0));
    for (int64_t t = fromUs; t <= toUs; t += stepUs) {
        int k = int(std::floor(double(t - fromUs) / h));
        k = std::max(0, std::min(K - 2, k));
        const double a = std::clamp(double(t - (fromUs + int64_t(k) * knotUs)) / h, 0.0, 1.0);
        c.t_us.push_back(t);
        c.inclDeg.push_back((1.0 - a) * v[size_t(k)] + a * v[size_t(k) + 1]);
        const double se = (1.0 - a) * std::sqrt(std::max(0.0, diagInv[size_t(k)]))
                        + a * std::sqrt(std::max(0.0, diagInv[size_t(k) + 1]));
        c.sigmaDeg.push_back(se * scale);
        auto it = std::lower_bound(accepted.begin(), accepted.end(), t - bridgeUs);
        c.supported.push_back(it != accepted.end() && *it <= t + bridgeUs ? 1 : 0);
    }
    c.fitted = true;
    return c;
}

} // namespace pinpoint::analysis::planecurve
