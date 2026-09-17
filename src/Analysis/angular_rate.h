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

// Angular-rate derivation and peak placement — the ONE code path every kinematic-sequence route
// ends in (docs/design/kinematic_sequence_design.md §6). Header-only, pure, no Qt-GUI, in the
// series_reduce.h mould.
//
// WHY ONE PATH. A sequence node is "when this segment peaked, ± how much". An IMU hands us a rate
// at 200 Hz; a face-on camera hands us an angle at 120 fps that has to be differentiated; the
// synth club tier is 240 Hz. If each route smoothed and peak-picked its own way, a pelvis node from
// an IMU and a thorax node from a camera would not be comparable to the millisecond the sequence
// claims, and "the routes share a common measure" would be a sentence rather than a property of
// the code. So: every route produces an angle or a rate with a per-sample σ, and everything after
// that — the derivative, the smoothing, the extremum, the timing σ — is here.
//
// THE WINDOW IS FIXED IN TIME, NOT IN SAMPLES. A Savitzky–Golay kernel of N samples is a
// different filter at 120 fps and at 240 Hz. localFit() fits a quadratic by least squares to the
// samples inside ±window/2 of each sample's own instant, which is the SG derivative generalised to
// an irregular grid, and it is what makes a 25 ms window mean 25 ms on every route.
//
// THE TIMING σ IS PROPAGATED, NOT ASSUMED. For a peak of a curve with rate noise σ_r and curvature
// r̈ at the peak, the half-width over which the curve sits within one σ_r of its maximum is
// sqrt(2 σ_r / |r̈|) — the interval inside which the true peak could sit without the data being
// able to tell. That is what the verdict gates on (kinematic_sequence.h), and angular_rate_test
// checks that it COVERS the realised timing error across noisy draws rather than merely being
// computed, because a σ that does not cover is a lie with decimals.

#include "series_reduce.h"      // SeriesView, Reduced, reduceExtremum, ReduceConfig

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace pinpoint::analysis {

// A local least-squares quadratic v ≈ c0 + c1·(t − t0) + c2·(t − t0)² over the samples within
// ±halfUs of t0, in SECONDS for the abscissa so c1 is per-second and c2 per-second². `ok` needs at
// least three distinct instants spanning something; a window that collapses to one or two samples
// cannot support a quadratic and says so rather than returning a slope through two points.
struct LocalFit {
    bool   ok    = false;
    int    n     = 0;
    double c0    = 0.0;     // smoothed value at t0
    double c1    = 0.0;     // first derivative at t0
    double c2    = 0.0;     // half the second derivative at t0 (r̈ = 2·c2)
    double sumSq = 0.0;     // Σ (t_i − t0)² in s² — the slope's noise lever arm (σ_c1 ≈ σ_v/√sumSq)
};

namespace detail_rate {

inline LocalFit fitAt(const int64_t *t, const double *v, const uint8_t *valid, size_t n,
                      size_t i0, int64_t halfUs)
{
    LocalFit f;
    const int64_t t0 = t[i0];
    // Normal equations for a quadratic in x = (t − t0) seconds. Sums up to x⁴.
    double s0 = 0, s1 = 0, s2 = 0, s3 = 0, s4 = 0, sy = 0, sxy = 0, sx2y = 0;
    int    n_ = 0, distinct = 0;
    int64_t lastT = std::numeric_limits<int64_t>::min();
    const auto add = [&](size_t i) {
        const double x = double(t[i] - t0) * 1e-6, y = v[i];
        s0 += 1; s1 += x; s2 += x * x; s3 += x * x * x; s4 += x * x * x * x;
        sy += y; sxy += x * y; sx2y += x * x * y; ++n_;
        if (t[i] != lastT) { ++distinct; lastT = t[i]; }
    };
    // Walk the window in ascending order; the grid is ascending so both scans stop at the edge.
    size_t lo = i0;
    while (lo > 0 && t0 - t[lo - 1] <= halfUs) --lo;
    for (size_t i = lo; i < n && t[i] - t0 <= halfUs; ++i) {
        if ((valid && !valid[i]) || !std::isfinite(v[i])) continue;
        add(i);
    }
    f.n = n_;
    if (distinct < 2) return f;   // one instant carries no slope
    // Solve the 3×3 symmetric system [s0 s1 s2; s1 s2 s3; s2 s3 s4]·c = [sy sxy sx2y] by Cramer.
    // RANK IS DECIDED BY COUNTING INSTANTS, not by testing the determinant against a constant: the
    // abscissa is seconds, so x⁴ terms are ~1e-8 and a "small" determinant threshold is either
    // always or never met depending on the frame rate. Two distinct instants fit a line, exactly.
    const double det = s0 * (s2 * s4 - s3 * s3) - s1 * (s1 * s4 - s3 * s2) + s2 * (s1 * s3 - s2 * s2);
    if (distinct < 3 || !(std::fabs(det) > 0.0)) {
        const double ld = s0 * s2 - s1 * s1;
        if (!(std::fabs(ld) > 1e-30)) return f;
        f.c0 = (sy * s2 - s1 * sxy) / ld;
        f.c1 = (s0 * sxy - s1 * sy) / ld;
        f.c2 = 0.0;
        f.sumSq = s2 - s1 * s1 / s0;
        f.ok = true;
        return f;
    }
    const double d0 = sy * (s2 * s4 - s3 * s3) - s1 * (sxy * s4 - s3 * sx2y) + s2 * (sxy * s3 - s2 * sx2y);
    const double d1 = s0 * (sxy * s4 - s3 * sx2y) - sy * (s1 * s4 - s3 * s2) + s2 * (s1 * sx2y - s2 * sxy);
    const double d2 = s0 * (s2 * sx2y - sxy * s3) - s1 * (s1 * sx2y - sxy * s2) + sy * (s1 * s3 - s2 * s2);
    f.c0 = d0 / det;
    f.c1 = d1 / det;
    f.c2 = d2 / det;
    f.sumSq = s2 - s1 * s1 / s0;
    f.ok = true;
    return f;
}

} // namespace detail_rate

// One derived sample: the smoothed value, its first derivative, and the derivative's 1σ given the
// input's per-sample σ. `ok` false ⇒ the window held too few samples (the output is NaN there and
// the caller's validity mask should say so).
struct RateSample {
    bool   ok        = false;
    int    n         = 0;     // samples the local fit used
    double smoothed  = 0.0;
    double rate      = 0.0;   // per second
    double rateSigma = 0.0;   // per second, 1σ
    double curvature = 0.0;   // second derivative, per second²
};

// Differentiate an angle (or smooth a rate: read `smoothed`) on its own grid. `sigma` is the
// per-sample 1σ of `v` in v's unit (may be empty ⇒ 0). The slope's σ over a window is
// σ_v · sqrt(1/Σx²) for a straight-line fit — the quadratic's is slightly larger, and the
// difference is inside the σ floor the caller applies, so the simpler lever arm is used.
inline std::vector<RateSample> localRates(const std::vector<int64_t> &t, const std::vector<double> &v,
                                          const std::vector<double> &sigma,
                                          const std::vector<uint8_t> &valid, int64_t windowUs)
{
    const size_t n = std::min(t.size(), v.size());
    std::vector<RateSample> out(n);
    if (n == 0) return out;
    const int64_t half = std::max<int64_t>(windowUs / 2, 1);
    const uint8_t *vm = (valid.size() >= n) ? valid.data() : nullptr;
    for (size_t i = 0; i < n; ++i) {
        const LocalFit f = detail_rate::fitAt(t.data(), v.data(), vm, n, i, half);
        if (!f.ok) { out[i].rate = out[i].smoothed = std::nan(""); continue; }
        // Representative input σ over the window: the sample's own, or the median of the window's
        // when the caller supplied one per sample. The median is what a robust reader wants; the
        // mean would let one spike own the derivative's error bar.
        double sv = 0.0;
        if (sigma.size() >= n) {
            std::vector<double> ws;
            const int64_t t0 = t[i];
            for (size_t j = 0; j < n; ++j) {
                if (std::llabs(t[j] - t0) > half) continue;
                if (vm && !vm[j]) continue;
                if (std::isfinite(sigma[j])) ws.push_back(sigma[j]);
            }
            if (!ws.empty()) {
                std::nth_element(ws.begin(), ws.begin() + long(ws.size() / 2), ws.end());
                sv = ws[ws.size() / 2];
            }
        }
        out[i].ok        = true;
        out[i].n         = f.n;
        out[i].smoothed  = f.c0;
        out[i].rate      = f.c1;
        out[i].curvature = 2.0 * f.c2;
        out[i].rateSigma = (f.sumSq > 0.0) ? sv / std::sqrt(f.sumSq) : 0.0;
    }
    return out;
}

// Where a rate series peaked inside [fromUs, toUs], and how sure the placement is.
struct PeakPlacement {
    bool    ok         = false;
    bool    atEdge     = false;    // the extremum sits within one extremum window of the domain edge
    int64_t tPeakUs    = 0;
    double  peak       = 0.0;      // the reduced (40 ms windowed-mean) value at the peak
    double  peakSigma  = 0.0;      // 1σ on `peak` — the reducer's window σ, or the rate σ if larger
    double  tSigmaMs   = 0.0;      // 1σ on tPeakUs, from curvature (see header note)
    double  curvature  = 0.0;      // r̈ at the peak (per s²), signed
};

// Place the (maximum) peak of a signed rate series over a domain. `rateSigma` is the per-sample
// rate σ (parallel to s.t; may be empty). The extremum itself is the shared reducer's — the same
// windowed mean the chart's PEAK tile and the diagnostics engine grade with, so the strip and the
// chart agree to display precision. The timing σ needs the curvature, which is read off a local
// quadratic over ±curvatureHalfUs about the extremum.
inline PeakPlacement placePeak(const SeriesView &s, const std::vector<double> &rateSigma,
                               int64_t fromUs, int64_t toUs, int64_t curvatureHalfUs,
                               const ReduceConfig &cfg = {})
{
    PeakPlacement p;
    if (s.n == 0 || toUs <= fromUs) return p;
    const Reduced r = reduceExtremum(s, fromUs, toUs, /*wantMax*/ true, cfg);
    if (!r.ok) return p;
    p.ok      = true;
    p.tPeakUs = r.atUs;
    p.peak    = r.value;
    p.peakSigma = r.sigma;

    // Curvature at the peak: nearest sample index, then the local quadratic about it.
    size_t i0 = 0;
    int64_t best = std::numeric_limits<int64_t>::max();
    for (size_t i = 0; i < s.n; ++i) {
        const int64_t d = std::llabs(s.t[i] - r.atUs);
        if (d < best) { best = d; i0 = i; }
    }
    const LocalFit f = detail_rate::fitAt(s.t, s.v, s.valid, s.n, i0, std::max<int64_t>(curvatureHalfUs, 1));
    p.curvature = f.ok ? 2.0 * f.c2 : 0.0;

    // Rate σ at the peak: the route's own per-sample σ (median over the curvature window) when
    // the caller propagated one. The reducer's window σ is the SPREAD of the samples inside the
    // 40 ms window, which on a smooth, curved peak is the peak's shape rather than the
    // measurement's noise — a clean IMU peak would report ±7 ms of timing from its own curvature.
    // It stands in only when the route characterised nothing.
    double sr = r.sigma;
    if (rateSigma.size() >= s.n) {
        std::vector<double> ws;
        for (size_t j = 0; j < s.n; ++j) {
            if (std::llabs(s.t[j] - r.atUs) > curvatureHalfUs) continue;
            if (s.valid && !s.valid[j]) continue;
            if (std::isfinite(rateSigma[j]) && rateSigma[j] > 0.0) ws.push_back(rateSigma[j]);
        }
        if (!ws.empty()) {
            std::nth_element(ws.begin(), ws.begin() + long(ws.size() / 2), ws.end());
            sr = ws[ws.size() / 2];
        }
    }
    p.peakSigma = sr;

    // σ_t = sqrt(2 σ_r / |r̈|). A flat top (|r̈| → 0) has no locatable peak, and the σ goes to the
    // domain width rather than infinity so the caller's "unresolved" threshold can read it.
    const double domainS = double(toUs - fromUs) * 1e-6;
    if (std::fabs(p.curvature) > 1e-12 && sr >= 0.0) {
        const double st = std::sqrt(2.0 * sr / std::fabs(p.curvature));
        p.tSigmaMs = std::min(st, domainS) * 1000.0;
    } else {
        p.tSigmaMs = domainS * 1000.0;
    }
    p.atEdge = (r.atUs - fromUs) < cfg.extremumWindowUs || (toUs - r.atUs) < cfg.extremumWindowUs;
    return p;
}

} // namespace pinpoint::analysis
