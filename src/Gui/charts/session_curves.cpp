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

#include "session_curves.h"

#include "../../Metrics/series_reduce.h"   // windowedMeans — the one windowed mean (header-only)

#include <algorithm>
#include <cmath>
#include <type_traits>

namespace pinpoint::charts {

namespace pa = pinpoint::analysis;

TraceCurve shotCurve(const ShotSeries &shot, const QString &key)
{
    TraceCurve c;
    c.ordinal = shot.ordinal;
    c.key     = key;
    c.axis    = TimeAxis::ShotMs;
    const MetricSeries *m = shot.metric(key);
    if (!m || shot.impactUs < 0)
        return c;
    const size_t n = std::min(m->tUs.size(), m->value.size());
    if (n < 2)
        return c;

    // The chart's short-mask rule (chart_metrics.cpp haveMask): honoured only when it covers the curve.
    const bool masked = m->valid.size() >= n;

    // SeriesView borrows int64_t; qint64 is the same type on macOS and Windows but not on Linux
    // (long vs long long), so copy only where the two differ.
    std::vector<int64_t> tCopy;
    const int64_t *tp = nullptr;
    if constexpr (std::is_same_v<qint64, int64_t>) {
        // Not a template, so the discarded branch is still type-checked: cast, though it is a no-op here.
        tp = reinterpret_cast<const int64_t *>(m->tUs.data());
    } else {
        tCopy.assign(m->tUs.begin(), m->tUs.begin() + std::ptrdiff_t(n));
        tp = tCopy.data();
    }
    pa::SeriesView view;
    view.t     = tp;
    view.v     = m->value.data();
    view.valid = masked ? m->valid.data() : nullptr;
    view.n     = n;
    std::vector<pa::WindowedMean> wm;
    pa::windowedMeans(view, wm, pa::ReduceConfig{});

    c.t.resize(n); c.v.resize(n); c.raw.resize(n); c.ok.resize(n);
    for (size_t i = 0; i < n; ++i) {
        // (t − impact) in integer µs first, then to ms: the exact expression the mocks evaluate.
        c.t[i]   = double(m->tUs[i] - shot.impactUs) / 1000.0;
        c.v[i]   = wm[i].value;
        c.raw[i] = m->value[i];
        c.ok[i]  = (!masked || m->valid[i] != 0) ? 1 : 0;
    }
    c.sigma = m->sigma;
    return c;
}

double valueAt(const TraceCurve &c, double x)
{
    const size_t n = c.t.size();
    if (n == 0 || !(x >= c.t.front()) || !(x <= c.t.back()))
        return kNaN;
    size_t lo = 0, hi = n - 1;
    while (hi - lo > 1) {
        const size_t mid = (lo + hi) >> 1;
        if (c.t[mid] <= x) lo = mid; else hi = mid;
    }
    if (!c.ok[lo] || !c.ok[hi])
        return kNaN;
    if (c.t[hi] == c.t[lo])
        return c.v[lo];
    const double f = (x - c.t[lo]) / (c.t[hi] - c.t[lo]);
    return c.v[lo] + f * (c.v[hi] - c.v[lo]);
}

std::vector<double> timeGrid(double t0, double t1, double step)
{
    std::vector<double> g;
    if (!(step > 0.0) || !std::isfinite(t0) || !std::isfinite(t1))
        return g;
    for (double x = t0; x <= t1 + 1e-6; x += step)
        g.push_back(x);
    return g;
}

std::vector<double> sessionGrid(double t0, double t1)
{
    return timeGrid(t0, t1, std::max(2.0, (t1 - t0) / 220.0));
}

double quantile7(std::vector<double> values, double q)
{
    values.erase(std::remove_if(values.begin(), values.end(), [](double x) { return !std::isfinite(x); }),
                 values.end());
    if (values.empty())
        return kNaN;
    std::sort(values.begin(), values.end());
    const double h  = double(values.size() - 1) * std::clamp(q, 0.0, 1.0);
    const size_t lo = size_t(std::floor(h));
    const size_t hi = size_t(std::ceil(h));
    return values[lo] + (values[hi] - values[lo]) * (h - double(lo));
}

} // namespace pinpoint::charts
