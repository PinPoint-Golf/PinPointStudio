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


#include "variability_clubs.h"

#include "session_consistency.h"    // consistencyResiduals

#include <QStringList>

#include <algorithm>
#include <cmath>
#include <map>

namespace pinpoint::charts {

namespace {

namespace pv = pinpoint::variability;

constexpr int kGrid = pv::kTraceGridPoints;

bool finite(double x) { return std::isfinite(x); }

bool onGrid(const std::vector<double> &curve) { return int(curve.size()) == kGrid; }

bool wholeCurve(const std::vector<double> &curve)
{
    return onGrid(curve) && std::all_of(curve.begin(), curve.end(), finite);
}

// Circular only: every value of each club wrapped about that club's circular mean at each sample, so the
// medians and residuals below are taken on one side of the seam. `values` is clubs.size() × samples.
void centreCircular(std::vector<double> &values, int samples, const QStringList &clubs)
{
    std::map<QString, std::vector<int>> groups;
    for (int i = 0; i < clubs.size(); ++i) groups[clubs.at(i)].push_back(i);
    std::vector<double> column;
    for (const auto &[club, rows] : groups) {
        for (int j = 0; j < samples; ++j) {
            column.clear();
            for (int i : rows) {
                const double x = values[size_t(i) * size_t(samples) + size_t(j)];
                if (finite(x)) column.push_back(x);
            }
            const double centre = pv::circularMeanDeg(column);
            for (int i : rows) {
                double &x = values[size_t(i) * size_t(samples) + size_t(j)];
                x = finite(centre) && finite(x) ? pv::wrapDeg(x - centre) : kNaN;
            }
        }
    }
}

std::vector<double> residualsOf(std::vector<double> values, int samples, const QStringList &clubs, pv::ScaleType type)
{
    if (type == pv::ScaleType::Circular) centreCircular(values, samples, clubs);
    return consistencyResiduals(values, samples, clubs);
}

// Each club with a reading and the indices of its swings (strike order), sorted by readings descending,
// then label ascending. `readings` gets each club's count, in the same order.
template <typename Swing, typename HasReading>
std::vector<std::pair<QString, std::vector<size_t>>> clubsByReadings(const std::vector<Swing> &swings,
                                                                    HasReading hasReading,
                                                                    std::vector<int> &readings)
{
    std::map<QString, std::vector<size_t>> groups;
    for (size_t i = 0; i < swings.size(); ++i) groups[swings[i].club].push_back(i);
    std::vector<std::pair<QString, std::vector<size_t>>> out;
    std::vector<int> counts;
    for (auto &[club, rows] : groups) {
        const int n = int(std::count_if(rows.begin(), rows.end(), [&](size_t i) { return hasReading(swings[i]); }));
        if (!n) continue;
        out.emplace_back(club, std::move(rows));
        counts.push_back(n);
    }
    std::vector<size_t> order(out.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    // std::map already orders by label, so a stable sort on the count keeps label ascending within a count.
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return counts[a] > counts[b]; });
    std::vector<std::pair<QString, std::vector<size_t>>> sorted;
    readings.clear();
    for (size_t i : order) { sorted.push_back(std::move(out[i])); readings.push_back(counts[i]); }
    return sorted;
}

} // namespace

pv::VariabilityInput pooledWithinClubInput(const std::vector<VariabilityClubValue> &swings,
                                           const pv::VariabilityInput &base)
{
    std::vector<double> values;
    QStringList clubs;
    for (const VariabilityClubValue &s : swings) { values.push_back(s.value); clubs << s.club; }
    const std::vector<double> residuals = residualsOf(std::move(values), 1, clubs, base.scaleType);

    pv::VariabilityInput in = base;
    in.values.clear();
    for (double r : residuals) if (finite(r)) in.values.push_back(r);
    return in;
}

pv::VariabilityInput pooledWithinClubTraceInput(const std::vector<VariabilityClubCurve> &swings,
                                                const pv::VariabilityInput &base)
{
    // A swing is a reading only with a whole curve AND a peak time; otherwise both halves are missing, so
    // its peak time does not move its club's median peak while its curve is left out (and vice versa). It
    // still counts towards its club's 3, as consistencyResiduals counts every swing with the label.
    std::vector<double> flat, peaks;
    QStringList clubs;
    flat.reserve(swings.size() * size_t(kGrid));
    for (const VariabilityClubCurve &s : swings) {
        const bool reading = wholeCurve(s.curve) && finite(s.peakTimeMs);
        if (reading) flat.insert(flat.end(), s.curve.begin(), s.curve.end());
        else         flat.insert(flat.end(), size_t(kGrid), kNaN);
        peaks.push_back(reading ? s.peakTimeMs : kNaN);
        clubs << s.club;
    }
    const std::vector<double> curveRes = residualsOf(std::move(flat), kGrid, clubs, base.scaleType);
    // Peak times are real milliseconds, never an angle: linear whatever the curve's scale type.
    const std::vector<double> peakRes = consistencyResiduals(peaks, 1, clubs);

    pv::VariabilityInput in = base;
    in.curves.clear();
    in.peakTimesMs.clear();
    in.peakValues.clear();
    for (size_t i = 0; i < swings.size(); ++i) {
        std::vector<double> c(curveRes.begin() + std::ptrdiff_t(i * size_t(kGrid)),
                              curveRes.begin() + std::ptrdiff_t((i + 1) * size_t(kGrid)));
        if (!wholeCurve(c) || !finite(peakRes[i])) continue;
        in.curves.push_back(std::move(c));
        in.peakTimesMs.push_back(peakRes[i]);
        in.peakValues.push_back(pv::tracePeak(swings[i].curve, base.scaleType));
    }
    return in;
}

std::vector<VariabilityClubWindow> perClubInputs(const std::vector<VariabilityClubValue> &swings,
                                                 const pv::VariabilityInput &base)
{
    std::vector<int> readings;
    const auto clubs = clubsByReadings(swings, [](const VariabilityClubValue &s) { return finite(s.value); },
                                       readings);
    std::vector<VariabilityClubWindow> out;
    for (size_t c = 0; c < clubs.size(); ++c) {
        VariabilityClubWindow w;
        w.club     = clubs[c].first;
        w.readings = readings[c];
        w.input    = base;
        w.input.values.clear();
        for (size_t i : clubs[c].second) w.input.values.push_back(swings[i].value);
        out.push_back(std::move(w));
    }
    return out;
}

std::vector<VariabilityClubWindow> perClubTraceInputs(const std::vector<VariabilityClubCurve> &swings,
                                                      const pv::VariabilityInput &base)
{
    std::vector<int> readings;
    const auto clubs = clubsByReadings(
        swings, [](const VariabilityClubCurve &s) { return wholeCurve(s.curve) && finite(s.peakTimeMs); }, readings);
    std::vector<VariabilityClubWindow> out;
    for (size_t c = 0; c < clubs.size(); ++c) {
        VariabilityClubWindow w;
        w.club     = clubs[c].first;
        w.readings = readings[c];
        w.input    = base;
        w.input.curves.clear();
        w.input.peakTimesMs.clear();
        w.input.peakValues.clear();
        for (size_t i : clubs[c].second) {
            const VariabilityClubCurve &s = swings[i];
            w.input.curves.push_back(onGrid(s.curve) ? s.curve : std::vector<double>(size_t(kGrid), kNaN));
            w.input.peakTimesMs.push_back(s.peakTimeMs);
        }
        out.push_back(std::move(w));
    }
    return out;
}

} // namespace pinpoint::charts
