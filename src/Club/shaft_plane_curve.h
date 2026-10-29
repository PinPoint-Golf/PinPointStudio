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
