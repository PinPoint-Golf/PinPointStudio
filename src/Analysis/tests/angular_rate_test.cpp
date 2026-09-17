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

// Standalone test for angular_rate.h — the one derivative and the one peak finder every
// kinematic-sequence route ends in.
//
// THE ASSERTION THAT MATTERS is §4: the timing σ the peak finder reports must COVER the realised
// timing error at the 68 % level across a thousand noisy draws. The sequence's verdict is gated on
// that σ (kinematic_sequence.h: an order is declared only when the gaps exceed it), so a σ that
// does not cover would let the product declare orders the data cannot support — quietly, on every
// swing, with a number beside it. The other sections pin the derivative's exactness on a quadratic
// (the local fit IS a quadratic, so this is a no-tolerance check), its behaviour on an irregular
// grid, and the domain edge flag.

#include "../angular_rate.h"

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <vector>

using namespace pinpoint::analysis;

static int g_fail = 0;
#define CHECK(label, cond)                                        \
    do {                                                          \
        const bool ok = (cond);                                   \
        std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", label);  \
        if (!ok) ++g_fail;                                        \
    } while (0)

static bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

// Deterministic LCG → N(0,1) by Box–Muller. No <random> so the draws are identical on every
// platform the suite runs on.
struct Rng {
    uint64_t s = 0x9E3779B97F4A7C15ull;
    double uniform() { s = s * 6364136223846793005ull + 1442695040888963407ull; return double((s >> 11) & ((1ull << 53) - 1)) / double(1ull << 53); }
    double normal()
    {
        const double u1 = std::max(uniform(), 1e-12), u2 = uniform();
        return std::sqrt(-2.0 * std::log(u1)) * std::cos(2.0 * 3.14159265358979323846 * u2);
    }
};

static std::vector<int64_t> grid(double fromS, double toS, double hz)
{
    std::vector<int64_t> t;
    for (double s = fromS; s <= toS + 1e-9; s += 1.0 / hz) t.push_back(int64_t(std::llround(s * 1e6)));
    return t;
}

int main()
{
    std::printf("angular_rate_test\n");

    // ── §1 A quadratic differentiates EXACTLY (the local fit is a quadratic) ─────────────────
    {
        const std::vector<int64_t> t = grid(0.0, 1.0, 120.0);
        std::vector<double> v(t.size());
        for (size_t i = 0; i < t.size(); ++i) { const double s = t[i] * 1e-6; v[i] = 3.0 * s * s - 2.0 * s + 0.5; }
        const auto r = localRates(t, v, {}, {}, 25000);
        bool exact = true, curv = true;
        for (size_t i = 2; i + 2 < t.size(); ++i) {
            const double s = t[i] * 1e-6;
            if (!r[i].ok || !near(r[i].rate, 6.0 * s - 2.0, 1e-6)) exact = false;
            if (!near(r[i].curvature, 6.0, 1e-6)) curv = false;
        }
        CHECK("§1 quadratic: rate = 6s − 2 to 1e-6 at every interior sample", exact);
        CHECK("§1 quadratic: curvature = 6 to 1e-6", curv);
        CHECK("§1 endpoints still fit (one-sided windows hold ≥ 3 samples at 120 Hz / 25 ms)",
              r.front().ok && r.back().ok);
    }

    // ── §2 A sine differentiates to a cosine, and the window's blur is small ─────────────────
    {
        const std::vector<int64_t> t = grid(0.0, 1.0, 120.0);
        std::vector<double> v(t.size());
        const double w = 2.0 * 3.14159265358979323846 * 3.0;   // 3 Hz — a downswing-speed feature
        for (size_t i = 0; i < t.size(); ++i) v[i] = std::sin(w * t[i] * 1e-6);
        const auto r = localRates(t, v, {}, {}, 25000);
        double worst = 0.0;
        for (size_t i = 3; i + 3 < t.size(); ++i)
            worst = std::max(worst, std::fabs(r[i].rate - w * std::cos(w * t[i] * 1e-6)));
        CHECK("§2 sine: rate within 2 % of ω·cos over a 25 ms window", worst < 0.02 * w);
    }

    // ── §3 An irregular grid (4 ms and 8 ms spacing interleaved) still fits ──────────────────
    {
        std::vector<int64_t> t;
        int64_t cur = 0;
        for (int i = 0; i < 200; ++i) { t.push_back(cur); cur += (i % 3 == 0) ? 8000 : 4000; }
        std::vector<double> v(t.size());
        for (size_t i = 0; i < t.size(); ++i) v[i] = 100.0 * t[i] * 1e-6;
        const auto r = localRates(t, v, {}, {}, 25000);
        // (Not named `ok`: the CHECK macro declares its own `ok`, and a variable of that name
        // passed as the condition would be shadowed by the macro's uninitialised copy of itself.)
        bool exactEverywhere = true;
        for (size_t i = 0; i < t.size(); ++i)
            if (!r[i].ok || !near(r[i].rate, 100.0, 1e-6)) exactEverywhere = false;
        CHECK("§3 irregular grid: a straight line's slope is exact everywhere", exactEverywhere);
    }

    // ── §4 Peak placement, and the σ_t COVERAGE test ─────────────────────────────────────────
    {
        const std::vector<int64_t> t = grid(0.0, 1.0, 200.0);
        const double t0 = 0.5, width = 0.045, amp = 480.0;   // a pelvis-like peak: 480 °/s, ~45 ms wide
        const auto bump = [&](double s) { const double x = (s - t0) / width; return amp * std::exp(-0.5 * x * x); };

        // Clean placement first.
        {
            std::vector<double> v(t.size());
            for (size_t i = 0; i < t.size(); ++i) v[i] = bump(t[i] * 1e-6);
            SeriesView sv{ t.data(), v.data(), nullptr, t.size() };
            const PeakPlacement p = placePeak(sv, {}, 300000, 700000, 25000);
            CHECK("§4 clean peak: placed", p.ok);
            CHECK("§4 clean peak: within 3 ms of the true instant", p.ok && std::llabs(p.tPeakUs - 500000) <= 3000);
            // The value is the reducer's 40 ms WINDOWED MEAN (the chart's PEAK tile convention),
            // which on a 45 ms-wide bump sits a few percent under the true amplitude. That is
            // the shared-reducer contract, not an error, and it is why the benchmark comparison
            // in the strip is direction-of-difference rather than a decimal.
            CHECK("§4 clean peak: windowed-mean value within 10 % of the amplitude", p.ok && near(p.peak, amp, 0.10 * amp));
            CHECK("§4 clean peak: curvature negative (a maximum)", p.ok && p.curvature < 0.0);
            CHECK("§4 clean peak: not flagged at the domain edge", p.ok && !p.atEdge);
            const PeakPlacement e = placePeak(sv, {}, 480000, 700000, 25000);
            CHECK("§4 a peak within one extremum window of the domain edge is flagged", e.ok && e.atEdge);
        }

        // Coverage: 1000 draws at three noise levels. The realised |Δt| must fall inside the
        // reported σ_t at least 68 % of the time — and σ_t must GROW with the noise.
        Rng rng;
        double prevSigma = 0.0;
        bool monotone = true, covered = true;
        for (const double noise : { 10.0, 30.0, 60.0 }) {
            int inside = 0, placed = 0;
            double sumSigma = 0.0;
            for (int d = 0; d < 1000; ++d) {
                std::vector<double> v(t.size()), sg(t.size(), noise);
                for (size_t i = 0; i < t.size(); ++i) v[i] = bump(t[i] * 1e-6) + noise * rng.normal();
                SeriesView sv{ t.data(), v.data(), nullptr, t.size() };
                const PeakPlacement p = placePeak(sv, sg, 300000, 700000, 25000);
                if (!p.ok) continue;
                ++placed;
                sumSigma += p.tSigmaMs;
                if (std::fabs(double(p.tPeakUs - 500000)) / 1000.0 <= p.tSigmaMs) ++inside;
            }
            const double frac = placed ? double(inside) / placed : 0.0;
            const double meanSigma = placed ? sumSigma / placed : 0.0;
            std::printf("    noise %5.1f °/s: placed %4d  covered %.3f  mean σ_t %.1f ms\n",
                        noise, placed, frac, meanSigma);
            if (frac < 0.68) covered = false;
            if (meanSigma <= prevSigma) monotone = false;
            prevSigma = meanSigma;
        }
        CHECK("§4 COVERAGE: |Δt| ≤ σ_t on ≥ 68 % of draws at every noise level", covered);
        CHECK("§4 σ_t grows with the rate noise", monotone);
    }

    // ── §5 Degenerate inputs refuse rather than invent ───────────────────────────────────────
    {
        SeriesView empty{ nullptr, nullptr, nullptr, 0 };
        CHECK("§5 empty series: no placement", !placePeak(empty, {}, 0, 1000000, 25000).ok);
        const std::vector<int64_t> t = { 0, 10000 };
        const std::vector<double>  v = { 1.0, 2.0 };
        const auto r = localRates(t, v, {}, {}, 25000);
        CHECK("§5 two samples: a line's slope, not a quadratic's", r[0].ok && near(r[0].rate, 100.0, 1e-9));
        const std::vector<int64_t> t1 = { 0 };
        const std::vector<double>  v1 = { 1.0 };
        CHECK("§5 one sample: refused", !localRates(t1, v1, {}, {}, 25000)[0].ok);
    }

    std::printf(g_fail ? "FAILED (%d)\n" : "OK\n", g_fail);
    return g_fail ? 1 : 0;
}
