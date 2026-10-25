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

#include "gap_bridge.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <random>

namespace pinpoint::analysis {

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

// ── the kernel and the dense linear algebra the fit needs (n ≤ ~60 samples, m ≤ ~200 grid points) ──

double matern52(double r, double ell, double sf)
{
    const double s = std::sqrt(5.0) * std::abs(r) / ell;
    return sf * sf * (1.0 + s + s * s / 3.0) * std::exp(-s);
}

// In-place Cholesky of a row-major n×n symmetric matrix into its lower factor. False on a pivot ≤ 0.
bool cholesky(std::vector<double> &a, size_t n)
{
    for (size_t j = 0; j < n; ++j) {
        double d = a[j * n + j];
        for (size_t k = 0; k < j; ++k) d -= a[j * n + k] * a[j * n + k];
        if (!(d > 0.0)) return false;
        const double l = std::sqrt(d);
        a[j * n + j] = l;
        for (size_t i = j + 1; i < n; ++i) {
            double s = a[i * n + j];
            for (size_t k = 0; k < j; ++k) s -= a[i * n + k] * a[j * n + k];
            a[i * n + j] = s / l;
        }
        for (size_t k = j + 1; k < n; ++k) a[j * n + k] = 0.0;
    }
    return true;
}

// The factor of K + jitter·I, the jitter growing tenfold until it factors (at most four tries).
bool factor(const std::vector<double> &k, size_t n, double jitter, std::vector<double> *l)
{
    for (int attempt = 0; attempt < 4; ++attempt, jitter *= 10.0) {
        *l = k;
        for (size_t i = 0; i < n; ++i) (*l)[i * n + i] += jitter;
        if (cholesky(*l, n)) return true;
    }
    return false;
}

void solveLower(const std::vector<double> &l, size_t n, std::vector<double> &b)
{
    for (size_t i = 0; i < n; ++i) {
        double s = b[i];
        for (size_t k = 0; k < i; ++k) s -= l[i * n + k] * b[k];
        b[i] = s / l[i * n + i];
    }
}

void solveUpperT(const std::vector<double> &l, size_t n, std::vector<double> &b)   // Lᵀ x = b
{
    for (size_t ii = n; ii-- > 0;) {
        double s = b[ii];
        for (size_t k = ii + 1; k < n; ++k) s -= l[k * n + ii] * b[k];
        b[ii] = s / l[ii * n + ii];
    }
}

double meanOf(const std::vector<double> &v)
{
    double s = 0;
    for (double x : v) s += x;
    return v.empty() ? 0.0 : s / double(v.size());
}

// Type-7 quantile (R's default) over the finite values; NaN when there are none.
double quantile7(std::vector<double> v, double p)
{
    v.erase(std::remove_if(v.begin(), v.end(), [](double x) { return !std::isfinite(x); }), v.end());
    if (v.empty()) return kNaN;
    std::sort(v.begin(), v.end());
    const double h = (double(v.size()) - 1.0) * p;
    const size_t lo = size_t(std::floor(h));
    const size_t hi = std::min(v.size() - 1, lo + 1);
    return v[lo] + (h - double(lo)) * (v[hi] - v[lo]);
}

// Standard normals from mt19937_64 by Box–Muller: the same numbers on every standard library.
struct Normals {
    std::mt19937_64 g;
    bool   have = false;
    double spare = 0;
    explicit Normals(uint64_t seed) : g(seed) {}
    double uniform() { return (double(g() >> 11) + 0.5) * (1.0 / 9007199254740992.0); }   // (0, 1)
    double next()
    {
        if (have) { have = false; return spare; }
        const double u1 = uniform(), u2 = uniform();
        const double r = std::sqrt(-2.0 * std::log(u1)), a = 6.283185307179586 * u2;
        spare = r * std::sin(a);
        have = true;
        return r * std::cos(a);
    }
};

// 2-D Nelder–Mead (reflection 1, expansion 2, contraction ½, shrink ½).
template <typename F>
std::array<double, 2> nelderMead(F f, std::array<double, 2> x0, double step, int maxIter)
{
    std::array<std::array<double, 2>, 3> p{ { x0, { x0[0] + step, x0[1] }, { x0[0], x0[1] + step } } };
    std::array<double, 3> fv{ f(p[0]), f(p[1]), f(p[2]) };
    for (int it = 0; it < maxIter; ++it) {
        std::array<int, 3> o{ 0, 1, 2 };
        std::sort(o.begin(), o.end(), [&](int a, int b) { return fv[size_t(a)] < fv[size_t(b)]; });
        const auto best = p[size_t(o[0])], mid = p[size_t(o[1])], worst = p[size_t(o[2])];
        const double fb = fv[size_t(o[0])], fm = fv[size_t(o[1])], fw = fv[size_t(o[2])];
        if (std::abs(fw - fb) < 1e-7 * (std::abs(fb) + 1.0)) break;
        const std::array<double, 2> c{ (best[0] + mid[0]) / 2, (best[1] + mid[1]) / 2 };
        const auto at = [&](double k) { return std::array<double, 2>{ c[0] + k * (worst[0] - c[0]), c[1] + k * (worst[1] - c[1]) }; };
        const auto xr = at(-1.0);
        const double fr = f(xr);
        const size_t w = size_t(o[2]);
        if (fr < fb) {
            const auto xe = at(-2.0);
            const double fe = f(xe);
            if (fe < fr) { p[w] = xe; fv[w] = fe; } else { p[w] = xr; fv[w] = fr; }
        } else if (fr < fm) {
            p[w] = xr; fv[w] = fr;
        } else {
            const auto xc = fr < fw ? at(-0.5) : at(0.5);
            const double fc = f(xc);
            if (fc < std::min(fr, fw)) { p[w] = xc; fv[w] = fc; }
            else {
                for (int k : { o[1], o[2] }) {
                    p[size_t(k)] = { best[0] + 0.5 * (p[size_t(k)][0] - best[0]), best[1] + 0.5 * (p[size_t(k)][1] - best[1]) };
                    fv[size_t(k)] = f(p[size_t(k)]);
                }
            }
        }
    }
    size_t b = 0;
    for (size_t i = 1; i < 3; ++i) if (fv[i] < fv[b]) b = i;
    return p[b];
}

} // namespace

// ── the gap and its training samples ──────────────────────────────────────────────────────────────

ImpactGap findImpactGap(const std::vector<double> &t, const std::vector<uint8_t> &ok, const GapBridgeOptions &o)
{
    ImpactGap out;
    const size_t n = std::min(t.size(), ok.size());
    double bestMid = 1e300;
    for (size_t i = 0; i < n;) {
        if (ok[i]) { ++i; continue; }
        size_t j = i;
        while (j + 1 < n && !ok[j + 1]) ++j;
        if (i > 0 && j + 1 < n && t[i] <= o.nearToMs && t[j] >= o.nearFromMs) {
            const double mid = std::abs((t[i - 1] + t[j + 1]) / 2.0);
            if (mid < bestMid) { bestMid = mid; out.found = true; out.fromMs = t[i - 1]; out.toMs = t[j + 1]; }
        }
        i = j + 1;
    }
    return out;
}

bool gapTrainingSet(const std::vector<double> &t, const std::vector<double> &v, const std::vector<uint8_t> &ok,
                    const ImpactGap &gap, const GapBridgeOptions &o, std::vector<double> *tt, std::vector<double> *vv)
{
    tt->clear(); vv->clear();
    if (!gap.found) return false;
    int before = 0, after = 0;
    const size_t n = std::min({ t.size(), v.size(), ok.size() });
    for (size_t i = 0; i < n; ++i) {
        if (!ok[i] || !std::isfinite(v[i])) continue;
        if (t[i] < gap.fromMs - o.beforeMs - 1e-9 || t[i] > gap.toMs + o.afterMs + 1e-9) continue;
        tt->push_back(t[i]); vv->push_back(v[i]);
        if (t[i] <= gap.fromMs + 1e-9) ++before;
        if (t[i] >= gap.toMs - 1e-9) ++after;
    }
    return before >= o.minEachSide && after >= o.minEachSide;
}

// ── the fit ───────────────────────────────────────────────────────────────────────────────────────

double gpNegLogMarginal(const std::vector<std::vector<double>> &ts, const std::vector<std::vector<double>> &vs,
                        double ellMs, double sfDps, double noiseDps)
{
    double total = 0;
    for (size_t c = 0; c < ts.size() && c < vs.size(); ++c) {
        const auto &t = ts[c];
        const size_t n = t.size();
        if (n == 0) continue;
        const double mu = meanOf(vs[c]);
        std::vector<double> k(n * n);
        for (size_t i = 0; i < n; ++i)
            for (size_t j = 0; j < n; ++j) k[i * n + j] = matern52(t[i] - t[j], ellMs, sfDps) + (i == j ? noiseDps * noiseDps : 0.0);
        std::vector<double> l;
        if (!factor(k, n, 1e-9 * sfDps * sfDps, &l)) return 1e30;
        std::vector<double> a(n);
        for (size_t i = 0; i < n; ++i) a[i] = vs[c][i] - mu;
        solveLower(l, n, a);                 // a = L⁻¹ y; ½ yᵀK⁻¹y = ½ |a|²
        double q = 0, ld = 0;
        for (size_t i = 0; i < n; ++i) { q += a[i] * a[i]; ld += std::log(l[i * n + i]); }
        total += 0.5 * q + ld + 0.5 * double(n) * std::log(6.283185307179586);
    }
    return total;
}

GapBridgeKernel fitGapBridgeKernel(const std::vector<std::vector<double>> &ts, const std::vector<std::vector<double>> &vs,
                                   const GapBridgeKernel &start, bool *ok)
{
    const double lo0 = std::log(10.0), hi0 = std::log(200.0), lo1 = std::log(20.0), hi1 = std::log(2000.0);
    const auto f = [&](const std::array<double, 2> &x) {
        if (x[0] < lo0 || x[0] > hi0 || x[1] < lo1 || x[1] > hi1) return 1e30;
        return gpNegLogMarginal(ts, vs, std::exp(x[0]), std::exp(x[1]), start.noiseDps);
    };
    const auto x = nelderMead(f, { std::log(start.ellMs), std::log(start.sfDps) }, 0.4, 200);
    GapBridgeKernel k = start;
    const bool good = !ts.empty() && f(x) < 1e29;
    if (good) { k.ellMs = std::exp(x[0]); k.sfDps = std::exp(x[1]); }
    if (ok) *ok = good;
    return k;
}

bool gpPeakDraws(const std::vector<double> &t, const std::vector<double> &v, const std::vector<double> &grid,
                 const GapBridgeKernel &kernel, int draws, uint64_t seed, GpPeakDraws *out)
{
    const size_t n = t.size(), m = grid.size();
    if (n < 2 || m < 2 || !out) return false;
    const double ell = kernel.ellMs, sf = kernel.sfDps, sn = kernel.noiseDps;
    const double mu = meanOf(v);
    std::vector<double> k(n * n);
    for (size_t i = 0; i < n; ++i)
        for (size_t j = 0; j < n; ++j) k[i * n + j] = matern52(t[i] - t[j], ell, sf) + (i == j ? sn * sn : 0.0);
    std::vector<double> l;
    if (!factor(k, n, 1e-9 * sf * sf, &l)) return false;
    std::vector<double> alpha(n);
    for (size_t i = 0; i < n; ++i) alpha[i] = v[i] - mu;
    solveLower(l, n, alpha);
    solveUpperT(l, n, alpha);
    // the posterior over the grid: mean = μ + K*ᵀα, cov = K** − VᵀV with V = L⁻¹K*
    std::vector<double> vcols(n * m);                 // column j at [j*n, j*n + n)
    out->mean.assign(m, mu);
    for (size_t j = 0; j < m; ++j) {
        std::vector<double> col(n);
        for (size_t i = 0; i < n; ++i) col[i] = matern52(t[i] - grid[j], ell, sf);
        for (size_t i = 0; i < n; ++i) out->mean[j] += col[i] * alpha[i];
        solveLower(l, n, col);
        std::copy(col.begin(), col.end(), vcols.begin() + std::ptrdiff_t(j * n));
    }
    std::vector<double> cov(m * m);
    for (size_t a = 0; a < m; ++a)
        for (size_t b = a; b < m; ++b) {
            double s = matern52(grid[a] - grid[b], ell, sf);
            const double *va = &vcols[a * n], *vb = &vcols[b * n];
            for (size_t i = 0; i < n; ++i) s -= va[i] * vb[i];
            cov[a * m + b] = cov[b * m + a] = s;
        }
    out->sd.resize(m);
    for (size_t j = 0; j < m; ++j) out->sd[j] = std::sqrt(std::max(0.0, cov[j * m + j]));
    std::vector<double> lp;
    if (!factor(cov, m, 1e-6 * sf * sf, &lp)) return false;
    Normals rng(seed);
    out->peakT.clear(); out->peakV.clear(); out->peakIndex.clear();
    out->peakT.reserve(size_t(draws)); out->peakV.reserve(size_t(draws)); out->peakIndex.reserve(size_t(draws));
    std::vector<double> z(m);
    for (int d = 0; d < draws; ++d) {
        for (size_t j = 0; j < m; ++j) z[j] = rng.next();
        size_t best = 0;
        double bestV = -1e300;
        for (size_t a = 0; a < m; ++a) {
            double f = out->mean[a];
            const double *row = &lp[a * m];
            for (size_t b = 0; b <= a; ++b) f += row[b] * z[b];
            if (f > bestV) { bestV = f; best = a; }
        }
        out->peakT.push_back(grid[best]);
        out->peakV.push_back(bestV);
        out->peakIndex.push_back(int(best));
    }
    return true;
}

GapBridge bridgeImpactGap(const std::vector<double> &t, const std::vector<double> &v, const std::vector<uint8_t> &ok,
                          double topMs, double p8Ms, const GapBridgeKernel &kernel, uint64_t seed, const GapBridgeOptions &o)
{
    GapBridge b;
    const ImpactGap gap = findImpactGap(t, ok, o);
    if (!gap.found) return b;
    b.fromMs = gap.fromMs;
    b.toMs = gap.toMs;
    if (gap.toMs - gap.fromMs > o.maxGapMs) { b.outcome = GapBridgeOutcome::GapTooLong; return b; }
    std::vector<double> tt, vv;
    if (!gapTrainingSet(t, v, ok, gap, o, &tt, &vv)) { b.outcome = GapBridgeOutcome::TooLittleCurve; return b; }
    b.searchFromMs = gap.fromMs - o.searchMarginMs;
    b.searchToMs = gap.toMs + o.searchMarginMs;
    if (std::isfinite(p8Ms) && p8Ms > gap.toMs) b.searchToMs = std::min(b.searchToMs, p8Ms);
    std::vector<double> grid;
    for (double x = b.searchFromMs; x <= b.searchToMs + 1e-6; x += o.gridStepMs) grid.push_back(x);
    GpPeakDraws d;
    if (!gpPeakDraws(tt, vv, grid, kernel, o.draws, seed, &d)) { b.outcome = GapBridgeOutcome::TooLittleCurve; return b; }
    for (size_t j = 0; j < grid.size(); ++j)
        if (grid[j] >= gap.fromMs - 1e-9 && grid[j] <= gap.toMs + 1e-9) {
            b.tMs.push_back(grid[j]); b.mean.push_back(d.mean[j]); b.sd.push_back(d.sd[j]);
        }
    int left = 0, right = 0;
    for (int k : d.peakIndex) { left += k == 0 ? 1 : 0; right += k == int(grid.size()) - 1 ? 1 : 0; }
    const double nd = double(std::max<size_t>(1, d.peakIndex.size()));
    b.edgeShare = double(left + right) / nd;
    if (double(right) / nd > o.edgeShare) { b.outcome = GapBridgeOutcome::StillRising; return b; }
    if (double(left) / nd > o.edgeShare) { b.outcome = GapBridgeOutcome::FellIntoGap; return b; }
    b.outcome = GapBridgeOutcome::Bridged;
    b.peakMs = quantile7(d.peakT, 0.5);
    b.peakSigmaMs = (quantile7(d.peakT, 0.84) - quantile7(d.peakT, 0.16)) / 2.0;
    b.peakValue = quantile7(d.peakV, 0.5);
    b.peakSigmaValue = (quantile7(d.peakV, 0.84) - quantile7(d.peakV, 0.16)) / 2.0;
    // a taller measured value earlier in the downswing, from Top up to the search window
    const double from = std::isfinite(topMs) && topMs < b.searchFromMs ? topMs : b.searchFromMs - 400.0;
    const size_t n = std::min({ t.size(), v.size(), ok.size() });
    for (size_t i = 0; i < n; ++i)
        if (ok[i] && std::isfinite(v[i]) && t[i] >= from && t[i] < b.searchFromMs && v[i] > b.peakValue
            && (!std::isfinite(b.earlierValue) || v[i] > b.earlierValue)) {
            b.earlierMs = t[i];
            b.earlierValue = v[i];
        }
    return b;
}

} // namespace pinpoint::analysis
