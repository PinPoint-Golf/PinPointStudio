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

#include "session_bridge.h"
#include "session_warp.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <random>

namespace pinpoint::charts {

namespace {

// ── the kernel and the few pieces of dense linear algebra the fit needs (n ≤ ~60, m ≤ ~200) ──────

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

// Standard normals from mt19937_64 by Box–Muller: the same numbers on every standard library.
struct Normals {
    std::mt19937_64 g;
    bool   have = false;
    double spare = 0;
    explicit Normals(quint64 seed) : g(seed) {}
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
        size_t w = size_t(o[2]);
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

// One shot's hole and training samples, found before the session's kernel is known.
struct Candidate {
    size_t index = 0;
    std::vector<double> t, v;            // measured training samples (shot clock)
    std::vector<double> allT, allV;      // every measured sample (for the earlier hump)
    std::vector<uint8_t> allOk;
    double topMs = kNaN, p8Ms = kNaN;
};

double phaseMs(const ShotSeries &s, int ph)
{
    for (const PhaseMark &m : s.phases)
        if (m.phase == ph && s.impactUs >= 0) return double(m.tUs - s.impactUs) / 1000.0;
    return kNaN;
}

} // namespace

// ── the fit ─────────────────────────────────────────────────────────────────────────────────────

double gpNegLogMarginal(const std::vector<std::vector<double>> &ts, const std::vector<std::vector<double>> &vs,
                        double ellMs, double sfDps, double noiseDps)
{
    double total = 0;
    for (size_t c = 0; c < ts.size(); ++c) {
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

bool gpPeakDraws(const std::vector<double> &t, const std::vector<double> &v, const std::vector<double> &grid,
                 const BridgeKernel &kernel, int draws, quint64 seed, GpPeakDraws *out)
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

// ── the session ─────────────────────────────────────────────────────────────────────────────────

SessionBridges bridgeChest(const SessionSnapshot &session, const SessionSequence &sequence, const BridgeOptions &o)
{
    SessionBridges out;
    out.shots.resize(session.shots.size());
    const QString key = sequenceSeriesKey(kChestSegment);
    std::vector<Candidate> cands;
    for (size_t si = 0; si < session.shots.size(); ++si) {
        const ShotSeries &shot = *session.shots[si];
        ChestBridge &b = out.shots[si];
        b.ordinal = shot.ordinal;
        if (shot.impactUs < 0) continue;
        if (si < sequence.shots.size() && sequence.shots[si].segments[size_t(kChestSegment)].placed) continue;
        const TraceCurve c = shotCurve(shot, key);
        if (c.empty()) continue;
        const size_t n = c.t.size();
        // the run of unmeasured samples nearest impact that has a measured sample either side
        long bestA = -1, bestB = -1;
        double bestMid = 1e300;
        for (size_t i = 0; i < n;) {
            if (c.ok[i]) { ++i; continue; }
            size_t j = i;
            while (j + 1 < n && !c.ok[j + 1]) ++j;
            if (i > 0 && j + 1 < n && c.t[i] <= o.nearToMs && c.t[j] >= o.nearFromMs) {
                const double mid = std::abs((c.t[i - 1] + c.t[j + 1]) / 2.0);
                if (mid < bestMid) { bestMid = mid; bestA = long(i); bestB = long(j); }
            }
            i = j + 1;
        }
        if (bestA < 0) continue;
        b.holeFromMs = c.t[size_t(bestA) - 1];
        b.holeToMs = c.t[size_t(bestB) + 1];
        if (b.holeToMs - b.holeFromMs > o.maxHoleMs) { b.outcome = BridgeOutcome::HoleTooLong; continue; }
        Candidate cd;
        cd.index = si;
        int before = 0, after = 0;
        for (size_t i = 0; i < n; ++i) {
            if (!c.ok[i] || !std::isfinite(c.v[i])) continue;
            cd.allT.push_back(c.t[i]); cd.allV.push_back(c.v[i]);
            if (c.t[i] < b.holeFromMs - o.beforeMs - 1e-9 || c.t[i] > b.holeToMs + o.afterMs + 1e-9) continue;
            cd.t.push_back(c.t[i]); cd.v.push_back(c.v[i]);
            if (c.t[i] <= b.holeFromMs + 1e-9) ++before;
            if (c.t[i] >= b.holeToMs - 1e-9) ++after;
        }
        if (before < o.minEachSide || after < o.minEachSide) { b.outcome = BridgeOutcome::TooLittleCurve; continue; }
        cd.topMs = phaseMs(shot, phase::Top);
        cd.p8Ms = phaseMs(shot, phase::P8);
        cands.push_back(std::move(cd));
    }

    // the session's kernel: the length and height that best explain every bridged shot's curve at once
    BridgeKernel kern;
    kern.noiseDps = o.noiseDps;
    kern.ellMs = o.ellStartMs;
    kern.sfDps = o.sfStartDps;
    kern.shots = int(cands.size());
    if (cands.size() >= 3) {
        std::vector<std::vector<double>> ts, vs;
        for (const Candidate &cd : cands) { ts.push_back(cd.t); vs.push_back(cd.v); }
        const double lo0 = std::log(o.ellMinMs), hi0 = std::log(o.ellMaxMs), lo1 = std::log(o.sfMinDps), hi1 = std::log(o.sfMaxDps);
        const auto f = [&](const std::array<double, 2> &x) {
            if (x[0] < lo0 || x[0] > hi0 || x[1] < lo1 || x[1] > hi1) return 1e30;
            return gpNegLogMarginal(ts, vs, std::exp(x[0]), std::exp(x[1]), o.noiseDps);
        };
        const auto x = nelderMead(f, { std::log(o.ellStartMs), std::log(o.sfStartDps) }, 0.4, 200);
        if (f(x) < 1e29) { kern.ellMs = std::exp(x[0]); kern.sfDps = std::exp(x[1]); kern.fitted = true; }
    }
    out.kernel = kern;

    // each shot conditioned on its own samples
    for (const Candidate &cd : cands) {
        ChestBridge &b = out.shots[cd.index];
        b.searchFromMs = b.holeFromMs - o.searchMarginMs;
        b.searchToMs = b.holeToMs + o.searchMarginMs;
        if (std::isfinite(cd.p8Ms) && cd.p8Ms > b.holeToMs) b.searchToMs = std::min(b.searchToMs, cd.p8Ms);
        const std::vector<double> grid = timeGrid(b.searchFromMs, b.searchToMs, o.gridStepMs);
        GpPeakDraws d;
        const quint64 seed = 0x9E3779B97F4A7C15ULL * quint64(std::max(1, b.ordinal)) + 1ULL;
        if (!gpPeakDraws(cd.t, cd.v, grid, kern, o.draws, seed, &d)) { b.outcome = BridgeOutcome::TooLittleCurve; continue; }
        for (size_t j = 0; j < grid.size(); ++j)
            if (grid[j] >= b.holeFromMs - 1e-9 && grid[j] <= b.holeToMs + 1e-9) {
                b.t.push_back(grid[j]); b.mean.push_back(d.mean[j]); b.sd.push_back(d.sd[j]);
            }
        int left = 0, right = 0;
        for (int k : d.peakIndex) { left += k == 0 ? 1 : 0; right += k == int(grid.size()) - 1 ? 1 : 0; }
        const double nd = double(std::max<size_t>(1, d.peakIndex.size()));
        b.edgeShare = double(left + right) / nd;
        if (double(right) / nd > o.edgeShare) { b.outcome = BridgeOutcome::StillRising; continue; }
        if (double(left) / nd > o.edgeShare) { b.outcome = BridgeOutcome::FellIntoHole; continue; }
        b.outcome = BridgeOutcome::Bridged;
        b.peakMs = quantile7(d.peakT, 0.5);
        b.peakLoMs = quantile7(d.peakT, 0.16);
        b.peakHiMs = quantile7(d.peakT, 0.84);
        b.peakSigmaMs = (b.peakHiMs - b.peakLoMs) / 2.0;
        b.peakDps = quantile7(d.peakV, 0.5);
        b.peakSigmaDps = (quantile7(d.peakV, 0.84) - quantile7(d.peakV, 0.16)) / 2.0;
        // a taller measured hump earlier in the downswing, from Top up to the search window
        const double from = std::isfinite(cd.topMs) && cd.topMs < b.searchFromMs ? cd.topMs : b.searchFromMs - 400.0;
        for (size_t i = 0; i < cd.allT.size(); ++i)
            if (cd.allT[i] >= from && cd.allT[i] < b.searchFromMs && cd.allV[i] > b.peakDps
                && (!std::isfinite(b.earlierDps) || cd.allV[i] > b.earlierDps)) {
                b.earlierMs = cd.allT[i];
                b.earlierDps = cd.allV[i];
            }
    }
    return out;
}

void applyBridges(SessionSequence &sequence, const SessionBridges &bridges)
{
    const auto &ids = sequenceSegmentIds();
    for (size_t i = 0; i < sequence.shots.size() && i < bridges.shots.size(); ++i) {
        const ChestBridge &b = bridges.shots[i];
        ShotSequence &q = sequence.shots[i];
        SegmentPeak &p = q.segments[size_t(kChestSegment)];
        if (!b.bridged() || p.placed) continue;
        p.probable = true;
        p.probableMs = b.peakMs;
        p.probableSigmaMs = b.peakSigmaMs;
        p.probableDps = b.peakDps;
        p.probableSigmaDps = b.peakSigmaDps;
        rankSequence(q);
        std::vector<int> order;
        for (int s = 0; s < kSequenceSegments; ++s) if (q.segments[size_t(s)].timed()) order.push_back(s);
        std::stable_sort(order.begin(), order.end(), [&](int a, int c) { return q.segments[size_t(a)].timeMs() < q.segments[size_t(c)].timeMs(); });
        q.order.clear();
        for (int s : order) q.order << ids[size_t(s)];
    }
    summariseSequence(sequence);
}

int SessionBridges::bridgedCount() const
{
    int k = 0;
    for (const ChestBridge &b : shots) k += b.bridged() ? 1 : 0;
    return k;
}

int SessionBridges::attemptedCount() const
{
    int k = 0;
    for (const ChestBridge &b : shots) k += b.outcome != BridgeOutcome::NoHole ? 1 : 0;
    return k;
}

int SessionBridges::earlierCount() const
{
    int k = 0;
    for (const ChestBridge &b : shots) k += b.bridged() && std::isfinite(b.earlierMs) ? 1 : 0;
    return k;
}

double SessionBridges::holeMinMs() const
{
    double v = kNaN;
    for (const ChestBridge &b : shots)
        if (b.bridged()) v = std::isfinite(v) ? std::min(v, b.holeToMs - b.holeFromMs) : b.holeToMs - b.holeFromMs;
    return v;
}

double SessionBridges::holeMaxMs() const
{
    double v = kNaN;
    for (const ChestBridge &b : shots)
        if (b.bridged()) v = std::isfinite(v) ? std::max(v, b.holeToMs - b.holeFromMs) : b.holeToMs - b.holeFromMs;
    return v;
}

const ChestBridge *SessionBridges::forOrdinal(int ordinal) const
{
    for (const ChestBridge &b : shots) if (b.ordinal == ordinal) return &b;
    return nullptr;
}

} // namespace pinpoint::charts
