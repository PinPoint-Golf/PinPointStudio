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

#include "../measures/measure_vocabulary.h"   // CompositeCombine

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

// THE ARITHMETIC OF A COMPOSITE MEASURE (docs/design/posture_metrics_design.md §4.3, §5.2, §6.3).
//
// Pure functions of the parts' z-scores, their weights and one correlation. Nothing here knows what
// a measure, a norm or a swing is: NormMeasureSource reads the parts, grades each against its own
// norm, and hands this file numbers. That split is what lets the null calibration be tested on a
// million synthetic draws without building a pack (norm_measure_source_test), and it keeps the one
// place values meet norms (norm_measure_source.h) free of special functions.
//
// BOTH MODES ARE CALIBRATED TO ONE TRIP RATE. If every part read at its norm with mean correlation
// ρ, the coherent Z is exactly standard normal and the distance Z_eq is standard normal to within
// Satterthwaite's approximation — so a composite past 2σ trips a clean swing about as often (2.3 %
// under the Standard policy) as one reading trips its own fault line, and its norm is always μ 0,
// σ 1. After that it is an ordinary measure.
//
// ρ IS ONE NUMBER, not a matrix: ρᵢⱼ = ρ for every i ≠ j, ρᵢᵢ = 1. Three golfers cannot support a
// 4 × 4 estimate, and with parts dropping in and out a full matrix would mostly be unread (§4.3).
// It is the mean correlation of the parts' LOSS-SIGNED z-scores (sᵢzᵢ), which is what makes the
// coherent denominator the standard deviation of the numerator; a distance composite squares, so
// its signs are moot and ρ there is the correlation of the raw zᵢ.

namespace pinpoint::analysis {

// ── Special functions ───────────────────────────────────────────────────────
//
// Written here rather than taken from a library: the build carries no statistics dependency and this
// file needs exactly three functions. Each is a textbook method with a stated accuracy, and the test
// reproduces the design's scipy tables to two decimals through them.

// Φ, the standard normal CDF.
inline double stdNormalCdf(double z) { return 0.5 * std::erfc(-z / std::sqrt(2.0)); }

// Φ⁻¹(p) for p in (0, 1): Acklam's rational approximation (relative error < 1.15e-9) followed by one
// Halley step against erfc, which takes it to near double precision. Accurate deep in the LOWER tail
// — the region a tail probability lives in — because the refinement there evaluates erfc of a large
// positive argument, which has no cancellation. ±infinity at 0 and 1; NaN outside.
inline double stdNormalQuantile(double p)
{
    if (!(p >= 0.0 && p <= 1.0)) return std::numeric_limits<double>::quiet_NaN();
    if (p == 0.0) return -std::numeric_limits<double>::infinity();
    if (p == 1.0) return  std::numeric_limits<double>::infinity();

    static const double a[] = { -3.969683028665376e+01,  2.209460984245205e+02, -2.759285104469687e+02,
                                 1.383577518672690e+02, -3.066479806614716e+01,  2.506628277459239e+00 };
    static const double b[] = { -5.447609879822406e+01,  1.615858368580409e+02, -1.556989798598866e+02,
                                 6.680131188771972e+01, -1.328068155288572e+01 };
    static const double c[] = { -7.784894002430293e-03, -3.223964580411365e-01, -2.400758277161838e+00,
                                -2.549732539343734e+00,  4.374664141464968e+00,  2.938163982698783e+00 };
    static const double d[] = {  7.784695709041462e-03,  3.224671290700398e-01,  2.445134137142996e+00,
                                 3.754408661907416e+00 };
    const double pLow = 0.02425, pHigh = 1.0 - pLow;

    double x;
    if (p < pLow) {
        const double q = std::sqrt(-2.0 * std::log(p));
        x = (((((c[0] * q + c[1]) * q + c[2]) * q + c[3]) * q + c[4]) * q + c[5])
            / ((((d[0] * q + d[1]) * q + d[2]) * q + d[3]) * q + 1.0);
    } else if (p <= pHigh) {
        const double q = p - 0.5, r = q * q;
        x = (((((a[0] * r + a[1]) * r + a[2]) * r + a[3]) * r + a[4]) * r + a[5]) * q
            / (((((b[0] * r + b[1]) * r + b[2]) * r + b[3]) * r + b[4]) * r + 1.0);
    } else {
        const double q = std::sqrt(-2.0 * std::log(1.0 - p));
        x = -(((((c[0] * q + c[1]) * q + c[2]) * q + c[3]) * q + c[4]) * q + c[5])
            / ((((d[0] * q + d[1]) * q + d[2]) * q + d[3]) * q + 1.0);
    }

    // One Halley step: e is the CDF's error at x, u its Newton correction.
    const double e = stdNormalCdf(x) - p;
    const double u = e * 2.50662827463100050242 * std::exp(x * x / 2.0);   // √(2π)
    return x - u / (1.0 + x * u / 2.0);
}

// Q(a, x) = Γ(a, x) / Γ(a), the regularised UPPER incomplete gamma function, for a > 0, x ≥ 0. The
// series for P below x < a + 1 and Lentz's continued fraction for Q above it (Numerical Recipes
// §6.2), each to a relative 1e-15. The split is the standard one: each form converges fast exactly
// where the other is slow, and the continued fraction returns Q directly in the far tail, where
// 1 − P would have cancelled to nothing.
inline double regularizedGammaQ(double a, double x)
{
    if (!(a > 0.0) || std::isnan(x)) return std::numeric_limits<double>::quiet_NaN();
    if (x <= 0.0) return 1.0;
    if (std::isinf(x)) return 0.0;

    const double logPrefix = a * std::log(x) - x - std::lgamma(a);
    constexpr int    kMaxIter = 1000;
    constexpr double kEps     = 1e-15;

    if (x < a + 1.0) {
        double ap = a, del = 1.0 / a, sum = del;
        for (int n = 0; n < kMaxIter; ++n) {
            ap += 1.0;
            del *= x / ap;
            sum += del;
            if (std::fabs(del) < std::fabs(sum) * kEps) break;
        }
        return std::clamp(1.0 - sum * std::exp(logPrefix), 0.0, 1.0);
    }

    constexpr double kTiny = 1e-300;
    double b = x + 1.0 - a, c = 1.0 / kTiny, d = 1.0 / b, h = d;
    for (int i = 1; i <= kMaxIter; ++i) {
        const double an = -i * (i - a);
        b += 2.0;
        d = an * d + b;
        if (std::fabs(d) < kTiny) d = kTiny;
        c = b + an / c;
        if (std::fabs(c) < kTiny) c = kTiny;
        d = 1.0 / d;
        const double del = d * c;
        h *= del;
        if (std::fabs(del - 1.0) < kEps) break;
    }
    return std::clamp(std::exp(logPrefix) * h, 0.0, 1.0);
}

// P(χ²ₖ ≥ x), k > 0 and not necessarily an integer — Satterthwaite's h never is.
inline double chiSquareSurvival(double x, double k) { return regularizedGammaQ(k / 2.0, x / 2.0); }

// ── The combination ─────────────────────────────────────────────────────────

// One part as the combination sees it: its z against its own norm (the σ of the side of μ the
// reading fell on), its weight, its loss sign (±1; ignored by the distance mode), and the ratio of
// its measurement σ to that norm σ — NaN when the reading stated no σ.
struct CompositeTerm {
    double z          = 0.0;
    double weight     = 0.0;
    int    sign       = 1;
    double sigmaRatio = std::numeric_limits<double>::quiet_NaN();
};

struct CompositeCombination {
    // The composite's value, on the scale its μ 0 σ 1 norm grades. NaN when nothing could be formed
    // (no terms, or a non-positive denominator).
    double z     = std::numeric_limits<double>::quiet_NaN();
    // Its measurement σ (§6.3 step 4). NaN = unquantified: some part read had no σ, and a partial
    // budget would claim a precision nobody measured.
    double sigma = std::numeric_limits<double>::quiet_NaN();
    // Distance only: Q = Σ wᵢzᵢ², the statistic before it is turned into a z. NaN for coherent.
    double q     = std::numeric_limits<double>::quiet_NaN();
    // Each term's share of the result, in term order (§6.3 step 5): coherent wᵢsᵢzᵢ / Σ wⱼsⱼzⱼ —
    // a NEGATIVE share is a part working against the finding, "against it" on the detail page —
    // and distance wᵢzᵢ² / Q. NaN when the denominator is zero (the parts cancel exactly, or every
    // part sits on its own norm), because no part can be said to carry a share of nothing.
    std::vector<double> shares;
};

// Z_eq is clamped to this range. Q = 0 (every part dead on its norm) would otherwise be −∞, and a
// non-finite number has nowhere to go: JSON has no spelling for it and a ledger z of −inf breaks
// every ordinal comparison downstream. ±8.3 is where Φ runs out of doubles anyway (Φ(−8.3) ≈ 5e−17),
// so nothing measurable is lost.
inline constexpr double kCompositeZClamp = 8.3;

namespace composite_detail {

// ΣᵢΣⱼ wᵢwⱼρᵢⱼ with one off-diagonal ρ — (Σw)²ρ + (1 − ρ)Σw², written out so the test can read it.
inline double weightedCorrelationSum(const std::vector<CompositeTerm> &t, double rhoOffDiagonal)
{
    double sw = 0.0, sw2 = 0.0;
    for (const CompositeTerm &x : t) { sw += x.weight; sw2 += x.weight * x.weight; }
    return sw2 + rhoOffDiagonal * (sw * sw - sw2);
}

inline bool allQuantified(const std::vector<CompositeTerm> &t)
{
    for (const CompositeTerm &x : t)
        if (!(std::isfinite(x.sigmaRatio) && x.sigmaRatio >= 0.0)) return false;
    return !t.empty();
}

} // namespace composite_detail

// ── Coherent: correlation-corrected weighted Stouffer (§4.3) ─────────────────
//
//     Z = Σ wᵢsᵢzᵢ / √(ΣᵢΣⱼ wᵢwⱼρᵢⱼ)
//
// The denominator is computed from the terms HANDED IN — the parts this swing read — so a reading
// taken from three parts is calibrated as a three-part reading, never as four with a zero in it.
//
// σ (§6.3 step 4): Z is linear in each xᵢ through zᵢ = (xᵢ − μᵢ)/σⁿᵢ, so σ_Z = √Σ(wᵢsᵢσᵐᵢ/σⁿᵢ)² /
// denominator. EXACT for independent measurement errors, which is the assumption every other
// quadrature in the engine makes; it is stated rather than corrected, as there (characteristic_
// engine.cpp's ANY/ALL product).
inline CompositeCombination combineCoherent(const std::vector<CompositeTerm> &terms, double rho)
{
    CompositeCombination out;
    if (terms.empty()) return out;
    const double denom2 = composite_detail::weightedCorrelationSum(terms, std::clamp(rho, 0.0, 1.0));
    if (!(denom2 > 0.0)) return out;
    const double denom = std::sqrt(denom2);

    double num = 0.0;
    for (const CompositeTerm &t : terms) num += t.weight * t.sign * t.z;
    out.z = num / denom;

    out.shares.reserve(terms.size());
    for (const CompositeTerm &t : terms)
        out.shares.push_back(num != 0.0 ? t.weight * t.sign * t.z / num
                                        : std::numeric_limits<double>::quiet_NaN());

    if (composite_detail::allQuantified(terms)) {
        double v = 0.0;
        for (const CompositeTerm &t : terms) {
            const double g = t.weight * t.sigmaRatio;   // |sᵢ| = 1
            v += g * g;
        }
        out.sigma = std::sqrt(v) / denom;
    }
    return out;
}

// ── Distance: Satterthwaite's gχ²ₕ (§5.2) ────────────────────────────────────
//
//     Q = Σ wᵢzᵢ²,   E[Q] = Σwᵢ,   Var[Q] = 2 ΣᵢΣⱼ wᵢwⱼρᵢⱼ²
//     Q ≈ g·χ²ₕ with g = Var/(2E), h = 2E²/Var        (the two moments matched)
//     Z_eq = Φ⁻¹(1 − P(gχ²ₕ ≥ Q))
//
// The moments are the terms' own, as the coherent denominator is. The approximation is slightly
// generous as ρ rises — the design's simulation puts the null trip rate at 2.35 / 2.43 / 2.47 % at
// ρ = 0 / 0.3 / 0.6 — which is accepted and written into the norm row.
//
// Φ⁻¹(1 − P) is evaluated as −Φ⁻¹(P): P is a small upper-tail probability exactly when the finding
// matters, and forming 1 − P first would throw its digits away.
inline double distanceZFromQ(double q, double meanQ, double varQ)
{
    if (!(meanQ > 0.0) || !(varQ > 0.0) || !(q >= 0.0)) return std::numeric_limits<double>::quiet_NaN();
    const double g = varQ / (2.0 * meanQ);
    const double h = 2.0 * meanQ * meanQ / varQ;
    const double p = chiSquareSurvival(q / g, h);
    return std::clamp(-stdNormalQuantile(p), -kCompositeZClamp, kCompositeZClamp);
}

// σ (§6.3 step 4): the delta method twice. Through Q first — ∂Q/∂xᵢ = 2wᵢzᵢ/σⁿᵢ, so
// σ_Q = √Σ(2wᵢzᵢσᵐᵢ/σⁿᵢ)² — then through Z_eq(Q), whose derivative is found numerically (a central
// difference; forward at Q ≈ 0, where Q cannot go negative). FIRST ORDER, and stated as such: at a
// part sitting exactly on its norm the linearisation says that part's error moves Q by nothing,
// which understates it. The test checks the budget against a Monte Carlo away from that point.
inline CompositeCombination combineDistance(const std::vector<CompositeTerm> &terms, double rho)
{
    CompositeCombination out;
    if (terms.empty()) return out;
    const double r = std::clamp(rho, 0.0, 1.0);

    double q = 0.0, meanQ = 0.0;
    for (const CompositeTerm &t : terms) { q += t.weight * t.z * t.z; meanQ += t.weight; }
    const double varQ = 2.0 * composite_detail::weightedCorrelationSum(terms, r * r);
    out.q = q;
    out.z = distanceZFromQ(q, meanQ, varQ);
    if (!std::isfinite(out.z)) return out;

    out.shares.reserve(terms.size());
    for (const CompositeTerm &t : terms)
        out.shares.push_back(q > 0.0 ? t.weight * t.z * t.z / q
                                     : std::numeric_limits<double>::quiet_NaN());

    if (composite_detail::allQuantified(terms)) {
        double vq = 0.0;
        for (const CompositeTerm &t : terms) {
            const double g = 2.0 * t.weight * t.z * t.sigmaRatio;
            vq += g * g;
        }
        const double step = std::max(1e-6, 1e-4 * q);
        const double dzdq = (q > step)
            ? (distanceZFromQ(q + step, meanQ, varQ) - distanceZFromQ(q - step, meanQ, varQ)) / (2.0 * step)
            : (distanceZFromQ(q + step, meanQ, varQ) - out.z) / step;
        out.sigma = std::fabs(dzdq) * std::sqrt(vq);
    }
    return out;
}

inline CompositeCombination combineComposite(CompositeCombine mode,
                                             const std::vector<CompositeTerm> &terms, double rho)
{
    return mode == CompositeCombine::Distance ? combineDistance(terms, rho)
                                              : combineCoherent(terms, rho);
}

} // namespace pinpoint::analysis
