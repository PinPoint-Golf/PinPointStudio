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

// BOX STATISTICS — the ONE implementation behind every box plot in the app (PpBoxPlot.qml draws them):
// the Charts panel's rows, runs and traces (session_relations), and Session diagnostics' Spread box and
// across-sessions strip (session_diagnostics_model). Until 18 Oct 2026 the two diagnostics strips each
// worked their quartiles and whiskers out again in QML; now the model publishes these.
//
//   quantile7   type-7 (R's default): h = (n − 1)·q, linear between the order statistics either side.
//               Non-finite values are dropped first; empty → NaN.
//   boxStats    Q1, median, Q3 by quantile7; Tukey whiskers — the most extreme values within 1.5 × IQR
//               of the box, and never inside it (with interpolated quartiles the nearest value inside a
//               fence can lie between Q3 and the median, and then that whisker has no length); the
//               values past them are the outliers, as indices into the input.
// Header-only so the diagnostics model and its tests take it without the Charts sources.

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace pinpoint::box {

inline double quantile7(std::vector<double> values, double q)
{
    values.erase(std::remove_if(values.begin(), values.end(), [](double x) { return !std::isfinite(x); }),
                 values.end());
    if (values.empty())
        return std::numeric_limits<double>::quiet_NaN();
    std::sort(values.begin(), values.end());
    const double h  = double(values.size() - 1) * std::clamp(q, 0.0, 1.0);
    const size_t lo = size_t(std::floor(h));
    const size_t hi = size_t(std::ceil(h));
    return values[lo] + (values[hi] - values[lo]) * (h - double(lo));
}

struct BoxStats {
    static constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
    int    n = 0;                       // finite values
    double min = kNaN, max = kNaN, q1 = kNaN, med = kNaN, q3 = kNaN, iqr = kNaN;
    double lo = kNaN, hi = kNaN;        // the whiskers' ends
    std::vector<int> outliers;          // INDICES into the values (shot index = ordinal − 1)
};

inline BoxStats boxStats(const std::vector<double> &values)
{
    BoxStats b;
    std::vector<int> idx; std::vector<double> v;
    for (size_t i = 0; i < values.size(); ++i) if (std::isfinite(values[i])) { idx.push_back(int(i)); v.push_back(values[i]); }
    std::vector<double> s = v;
    std::sort(s.begin(), s.end());
    b.n = int(s.size());
    if (!b.n) return b;
    b.q1 = quantile7(s, 0.25); b.med = quantile7(s, 0.5); b.q3 = quantile7(s, 0.75);
    b.iqr = b.q3 - b.q1;
    b.min = s.front(); b.max = s.back();
    const double lf = b.q1 - 1.5 * b.iqr, hf = b.q3 + 1.5 * b.iqr;
    std::vector<double> inn;
    for (double x : s) if (x >= lf - 1e-9 && x <= hf + 1e-9) inn.push_back(x);
    b.lo = inn.empty() ? b.q1 : std::min(inn.front(), b.q1);
    b.hi = inn.empty() ? b.q3 : std::max(inn.back(), b.q3);
    for (size_t j = 0; j < idx.size(); ++j)
        if (v[j] < lf - 1e-9 || v[j] > hf + 1e-9) b.outliers.push_back(idx[j]);
    return b;
}

} // namespace pinpoint::box
