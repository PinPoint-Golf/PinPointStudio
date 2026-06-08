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

#include "metric_extractor.h"

#include <QString>
#include <algorithm>

#include "wrist_angles.h"

namespace pinpoint::analysis {
namespace {

int nearestIndex(const std::vector<int64_t> &grid, int64_t t)
{
    if (t <= grid.front()) return 0;
    if (t >= grid.back())  return static_cast<int>(grid.size()) - 1;
    const auto it = std::lower_bound(grid.begin(), grid.end(), t);
    const int hi = static_cast<int>(it - grid.begin());
    const int lo = hi - 1;
    return (t - grid[lo] <= grid[hi] - t) ? lo : hi;
}

int64_t phaseTime(const std::vector<PhaseEvent> &phases, Phase p, int64_t fallback)
{
    for (const PhaseEvent &e : phases)
        if (e.phase == p) return e.t_us;
    return fallback;
}

MetricSeries buildSeries(const QString &key, const QString &label, const QString &unit,
                         const std::vector<int64_t> &grid, std::vector<double> value,
                         const std::vector<PhaseEvent> &phases)
{
    MetricSeries m;
    m.key   = key;
    m.label = label;
    m.unit  = unit;
    m.t_us  = grid;
    m.value = std::move(value);
    for (const Phase p : { Phase::Address, Phase::Top, Phase::Impact }) {
        const int idx = nearestIndex(grid, phaseTime(phases, p, grid.front()));
        m.phaseSamples.push_back({ p, grid[idx], m.value[idx], QString() });
    }
    return m;
}

} // namespace

std::vector<MetricSeries> MetricExtractor::extract(const FusedStreams &s,
                                                   const std::vector<PhaseEvent> &phases,
                                                   int handedness)
{
    std::vector<MetricSeries> out;
    const std::vector<int64_t> &grid = s.timeGrid;
    const int N = static_cast<int>(grid.size());
    if (N < 2)
        return out;

    // Lead arm = left unless the golfer is left-handed (provisional sign convention,
    // confirmed on the wizard "check your sensors" page — see wrist_angles.h).
    const bool leftArm = (handedness != 2);

    const SegmentStream *fore  = s.streamFor(SegmentRole::LeadForearm);
    const SegmentStream *hand  = s.streamFor(SegmentRole::LeadHand);
    const SegmentStream *upper = s.streamFor(SegmentRole::LeadUpperArm);

    const int addr = nearestIndex(grid, phaseTime(phases, Phase::Address, grid.front()));

    // --- wrist flex/ext + radial/ulnar (forearm + hand) ---
    if (fore && hand && static_cast<int>(fore->qAnat.size()) == N
                     && static_cast<int>(hand->qAnat.size()) == N) {
        const QQuaternion addrWrist =
            (fore->qAnat[addr].conjugated() * hand->qAnat[addr]).normalized();
        std::vector<double> fe(N), rud(N);
        for (int i = 0; i < N; ++i) {
            const QQuaternion rel = wristRel(fore->qAnat[i], hand->qAnat[i], addrWrist);
            const WristAngles wa = wristFlexExtDeviation(rel, leftArm);
            fe[i]  = radToDeg(wa.feRad);
            rud[i] = radToDeg(wa.rudRad);
        }
        out.push_back(buildSeries(QStringLiteral("leadWristFlexExt"),
                                  QStringLiteral("Lead wrist (bow/cup)"), QStringLiteral("°"),
                                  grid, std::move(fe), phases));
        out.push_back(buildSeries(QStringLiteral("leadWristRadUln"),
                                  QStringLiteral("Lead wrist hinge"), QStringLiteral("°"),
                                  grid, std::move(rud), phases));
    }

    // --- forearm pronation + elbow flexion (upper arm + forearm) ---
    if (upper && fore && static_cast<int>(upper->qAnat.size()) == N
                      && static_cast<int>(fore->qAnat.size()) == N) {
        const QQuaternion addrElbow =
            (upper->qAnat[addr].conjugated() * fore->qAnat[addr]).normalized();
        std::vector<double> pron(N), elbow(N);
        for (int i = 0; i < N; ++i) {
            const QQuaternion rel = elbowRel(upper->qAnat[i], fore->qAnat[i], addrElbow);
            const ForearmElbow ef = forearmPronElbowFlex(rel, leftArm);
            pron[i]  = radToDeg(ef.pronRad);
            elbow[i] = radToDeg(ef.flexRad);
        }
        out.push_back(buildSeries(QStringLiteral("forearmPronation"),
                                  QStringLiteral("Lead forearm roll"), QStringLiteral("°"),
                                  grid, std::move(pron), phases));
        out.push_back(buildSeries(QStringLiteral("leadArmFlexion"),
                                  QStringLiteral("Lead arm (elbow)"), QStringLiteral("°"),
                                  grid, std::move(elbow), phases));
    }

    return out;
}

} // namespace pinpoint::analysis
