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

#include "swing_coverage_model.h"

#include <algorithm>

namespace pinpoint {

namespace {
constexpr double kConfThreshold = 0.5;   // pose/club confidence floor
}

SwingCoverageModel::SwingCoverageModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int SwingCoverageModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_lanes.size());
}

QVariant SwingCoverageModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_lanes.size())
        return {};
    const Lane &l = m_lanes.at(index.row());
    switch (role) {
    case LabelRole:    return l.label;
    case ColorKeyRole: return l.colorKey;
    case BinsRole:     return l.bins;
    default:           return {};
    }
}

QHash<int, QByteArray> SwingCoverageModel::roleNames() const
{
    return {
        { LabelRole,    "label"    },
        { ColorKeyRole, "colorKey" },
        { BinsRole,     "bins"     },
    };
}

void SwingCoverageModel::setLanes(const QVector<SwingSeries> &lanes, qint64 windowStartUs,
                                  qint64 windowEndUs, int bins)
{
    beginResetModel();
    m_lanes.clear();
    const double span = double(windowEndUs - windowStartUs);
    const int N = (bins > 0) ? bins : 64;

    for (const SwingSeries &s : lanes) {
        Lane lane;
        lane.label    = s.label;
        lane.colorKey = s.colorKey;

        if (s.t.isEmpty() || span <= 0.0) {
            for (int b = 0; b < N; ++b)
                lane.bins.append(int(Gap));
            m_lanes.push_back(lane);
            continue;
        }

        const qint64 first = s.t.first();
        const qint64 last  = s.t.last();

        for (int b = 0; b < N; ++b) {
            const qint64 lo = windowStartUs + qint64(span * b / N);
            const qint64 hi = windowStartUs + qint64(span * (b + 1) / N);

            // Samples in [lo, hi).
            auto itLo = std::lower_bound(s.t.begin(), s.t.end(), lo);
            auto itHi = std::lower_bound(s.t.begin(), s.t.end(), hi);

            if (itLo != itHi) {
                // At least one sample — Present, or LowConf if any falls below the
                // confidence floor (IMU lanes carry no confidence → always Present).
                int state = int(Present);
                if (s.hasConf()) {
                    for (auto it = itLo; it != itHi; ++it) {
                        const int idx = int(it - s.t.begin());
                        if (idx < s.conf.size() && s.conf[idx] < kConfThreshold) {
                            state = int(LowConf);
                            break;
                        }
                    }
                }
                lane.bins.append(state);
                continue;
            }

            // Empty bucket: a Gap only if it is a real dropout — before the first
            // sample, after the last, or inside a sample-free interval longer than
            // the lane's BLE-batch-aware tolerance. A bucket that merely falls
            // between two normally-spaced batches is treated as Present.
            const qint64 center = (lo + hi) / 2;
            if (center < first || center > last) {
                lane.bins.append(int(Gap));
                continue;
            }
            auto right = std::upper_bound(s.t.begin(), s.t.end(), center);
            const qint64 rt = (right != s.t.end()) ? *right : last;
            const qint64 lt = (right != s.t.begin()) ? *(right - 1) : first;
            lane.bins.append(int((rt - lt) > s.gapToleranceUs ? Gap : Present));
        }
        m_lanes.push_back(lane);
    }
    endResetModel();
}

} // namespace pinpoint
