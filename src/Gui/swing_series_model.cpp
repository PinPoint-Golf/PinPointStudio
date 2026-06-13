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

#include "swing_series_model.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace pinpoint {

namespace {
constexpr double  kConfThreshold = 0.5;
constexpr int     kMaxRows       = 20000;   // virtualised; a hard backstop only

// Resolution of one (series, time, sub-channel) under a fill mode.
struct Match {
    bool   has   = false;   // a value to display
    double val   = 0.0;
    int    state = SwingSeriesModel::Missing;
    int    idx   = -1;      // index of the matched / carried / nearest sample
};

double pick(const SwingSeries &s, int j, int sub)
{
    if (j < 0) return 0.0;
    if (sub == 1) return s.gyroZ.value(j);
    if (sub == 2) return s.fusedRateZ.value(j);
    return s.value.value(j);
}

int nearestIndex(const SwingSeries &s, qint64 t)
{
    const int n = s.t.size();
    if (n == 0) return -1;
    auto it = std::lower_bound(s.t.begin(), s.t.end(), t);
    if (it == s.t.end())   return n - 1;
    if (it == s.t.begin()) return 0;
    const int hi = int(it - s.t.begin());
    const int lo = hi - 1;
    return (std::llabs(s.t[hi] - t) < std::llabs(s.t[lo] - t)) ? hi : lo;
}

// Core cell resolution (see header / impl-prompt §3.3). `primaryHeld` is whether the
// primary IMU is carrying through a gap at `t` — used to flag metric DerivedHeld.
Match resolveValue(const SwingSeries &s, qint64 t, int sub,
                   const QString &fill, bool primaryHeld)
{
    Match m;
    const int n = s.t.size();
    if (n == 0) return m;

    const int j = nearestIndex(s, t);
    const qint64 tol = std::max<qint64>(1, s.nominalPeriodUs / 2);

    // 1. Exact sample within half the nominal period.
    if (std::llabs(s.t[j] - t) <= tol) {
        m.has = true;                     // raw + fused both present at a real sample
        m.idx = j;
        m.val = pick(s, j, sub);
        const bool low   = s.hasConf() && j < s.conf.size() && s.conf[j] < kConfThreshold;
        const bool resync = s.isImu() && j > 0 && (s.t[j] - s.t[j - 1]) > s.gapToleranceUs;
        // A metric is resampled onto a dense grid, so it always has a value — but a
        // value computed while its input IMU was held through a dropout is suspect.
        const bool derived = (s.kind == SwingSeries::Metric) && primaryHeld;
        m.state = derived ? SwingSeriesModel::DerivedHeld
                : low ? SwingSeriesModel::LowConf
                : resync ? SwingSeriesModel::Resync
                : SwingSeriesModel::Ok;
        return m;
    }

    // 3. nearest fill carries the closest sample.
    if (fill == QLatin1String("nearest")) {
        m.has = true; m.idx = j; m.val = pick(s, j, sub);
        m.state = (s.kind == SwingSeries::Metric && primaryHeld)
                      ? SwingSeriesModel::DerivedHeld : SwingSeriesModel::Ok;
        return m;
    }

    // 2. fill off — leave a hole, except the documented IMU/metric inferences.
    auto right = std::upper_bound(s.t.begin(), s.t.end(), t);
    const int ri = (right == s.t.end())   ? -1 : int(right - s.t.begin());
    const int li = (right == s.t.begin()) ? -1 : int(right - s.t.begin() - 1);

    if (s.isImu()) {
        const bool insideGap = (li >= 0 && ri >= 0)
                            && (s.t[ri] - s.t[li]) > s.gapToleranceUs;
        // Only the fused channel survives a dropout (the quaternion stream keeps
        // advancing); raw gyro (sub 0 / sub 1) is genuinely absent → a hole.
        if (insideGap && sub == 2) {
            m.has = true; m.idx = li;
            m.val = s.fusedRateZ.value(li);
            m.state = SwingSeriesModel::Held;
            return m;
        }
        m.state = SwingSeriesModel::Missing;
        return m;
    }

    if (s.kind == SwingSeries::Metric) {
        if (primaryHeld) {                        // metric computed from a held source
            m.has = true; m.idx = j; m.val = s.value.value(j);
            m.state = SwingSeriesModel::DerivedHeld;
            return m;
        }
        m.state = SwingSeriesModel::Missing;
        return m;
    }

    m.state = SwingSeriesModel::Missing;          // pose / club hole
    return m;
}

QString msText(qint64 us)        { return QString::number(double(us) / 1000.0, 'f', 1); }
QString valText(double v)        { return QString::number(v, 'f', 1); }
QString confText(double c)       { return QString::number(c, 'f', 2); }

QString stateWord(int st)
{
    switch (st) {
    case SwingSeriesModel::Ok:          return QStringLiteral("ok");
    case SwingSeriesModel::Held:        return QStringLiteral("held");
    case SwingSeriesModel::Resync:      return QStringLiteral("resync");
    case SwingSeriesModel::LowConf:     return QStringLiteral("low");
    case SwingSeriesModel::DerivedHeld: return QStringLiteral("held*");
    default:                            return QStringLiteral("—");
    }
}
} // namespace

SwingSeriesModel::SwingSeriesModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

int SwingSeriesModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

int SwingSeriesModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_cols.size());
}

QVariant SwingSeriesModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const QVector<Cell> &row = m_rows.at(index.row());
    if (index.column() < 0 || index.column() >= row.size())
        return {};
    const Cell &c = row.at(index.column());
    switch (role) {
    case Qt::DisplayRole:
    case DisplayRole2: return c.text;
    case StateRole:    return c.state;
    case AlignRole:    return c.align;
    default:           return {};
    }
}

QVariant SwingSeriesModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal) return {};
    if (section < 0 || section >= m_cols.size()) return {};
    if (role == Qt::DisplayRole || role == DisplayRole2)
        return m_cols.at(section).header;
    return {};
}

QHash<int, QByteArray> SwingSeriesModel::roleNames() const
{
    return {
        { DisplayRole2, "display" },
        { StateRole,    "state"   },
        { AlignRole,    "align"   },
    };
}

QString SwingSeriesModel::columnColorKey(int col) const
{
    if (col < 0 || col >= m_cols.size()) return {};
    return m_cols.at(col).colorKey;
}

int SwingSeriesModel::rowForTimeUs(qint64 us) const
{
    if (m_rowTimeUs.isEmpty())                       return -1;
    if (us < m_windowStartUs || us > m_windowEndUs)  return -1;

    const int n = m_rowTimeUs.size();
    if (us <= m_rowTimeUs.first()) return 0;
    if (us >= m_rowTimeUs.last())  return n - 1;

    // Ascending — binary search for the first row at/after us, then pick the nearer
    // of it and its predecessor (ties go to the earlier row).
    int lo = 0, hi = n - 1;
    while (lo < hi) {
        const int mid = (lo + hi) / 2;
        if (m_rowTimeUs.at(mid) < us) lo = mid + 1;
        else                          hi = mid;
    }
    const qint64 tHi = m_rowTimeUs.at(lo);
    const qint64 tLo = m_rowTimeUs.at(lo - 1);
    return (us - tLo) <= (tHi - us) ? lo - 1 : lo;
}

void SwingSeriesModel::configure(const QVector<SwingSeries> &series, qint64 windowStartUs,
                                 qint64 windowEndUs, const QString &fillMode)
{
    beginResetModel();
    m_cols.clear();
    m_rows.clear();

    // ── columns ──────────────────────────────────────────────────────────────
    m_cols.push_back({ Time, -1, 0, QStringLiteral("t · ms"), QString(), Qt::AlignRight });

    int primaryIdx = -1;
    for (int i = 0; i < series.size(); ++i)
        if (series[i].primary) { primaryIdx = i; break; }

    for (int i = 0; i < series.size(); ++i) {
        const SwingSeries &s = series[i];
        if (s.primary) {
            m_cols.push_back({ Value, i, 1, s.header + QStringLiteral(" raw"),   s.colorKey, Qt::AlignRight });
            m_cols.push_back({ Value, i, 2, s.header + QStringLiteral(" fused"), s.colorKey, Qt::AlignRight });
        } else if (s.isImu()) {
            m_cols.push_back({ Value, i, 0, s.header + QStringLiteral(" ω"), s.colorKey, Qt::AlignRight });
        } else {
            m_cols.push_back({ Value, i, 0, s.header, s.colorKey, Qt::AlignRight });
        }
        if (s.hasConf())
            m_cols.push_back({ Conf, i, 0, QStringLiteral("conf"), s.colorKey, Qt::AlignRight });
    }

    QString primaryColor;
    if (primaryIdx >= 0) {
        primaryColor = series[primaryIdx].colorKey;
        m_cols.push_back({ DeltaT, primaryIdx, 0, QStringLiteral("Δt ms"), primaryColor, Qt::AlignRight });
        m_cols.push_back({ State,  primaryIdx, 0, QStringLiteral("state"),      primaryColor, Qt::AlignLeft  });
    }

    // ── row grid: union of resolved sample times in window, deduped ───────────
    QVector<qint64> times;
    qint64 dedupTol = std::numeric_limits<qint64>::max();
    for (const SwingSeries &s : series) {
        if (s.medianDeltaUs > 0) dedupTol = std::min(dedupTol, s.medianDeltaUs);
        for (qint64 t : s.t)
            if (t >= windowStartUs && t <= windowEndUs) times.push_back(t);
    }
    std::sort(times.begin(), times.end());
    dedupTol = (dedupTol == std::numeric_limits<qint64>::max()) ? 1 : std::max<qint64>(1, dedupTol / 2);

    QVector<qint64> rowTimes;
    rowTimes.reserve(times.size());
    for (qint64 t : times) {
        if (rowTimes.isEmpty() || (t - rowTimes.last()) > dedupTol) {
            rowTimes.push_back(t);
            if (rowTimes.size() >= kMaxRows) break;
        }
    }

    // Retain the row timeline + window so rowForTimeUs() can map the replay playhead
    // to a row (and reject positions outside the window).
    m_rowTimeUs     = rowTimes;
    m_windowStartUs = windowStartUs;
    m_windowEndUs   = windowEndUs;

    // ── precompute cells ──────────────────────────────────────────────────────
    const SwingSeries *primary = (primaryIdx >= 0) ? &series[primaryIdx] : nullptr;

    m_rows.reserve(rowTimes.size());
    for (qint64 t : rowTimes) {
        // Primary truth (fused col, fill off) drives State, Δt and metric held-flag.
        Match pm;
        if (primary) pm = resolveValue(*primary, t, 2, QStringLiteral("off"), false);
        const bool primaryHeld = primary && pm.state == Held;

        QVector<Cell> row;
        row.reserve(m_cols.size());
        for (const Column &col : m_cols) {
            Cell cell; cell.align = col.align;
            switch (col.kind) {
            case Time:
                cell.text = msText(t); cell.state = Ok;
                break;
            case Value: {
                const Match m = resolveValue(series[col.src], t, col.sub, fillMode, primaryHeld);
                cell.text  = m.has ? valText(m.val) : QStringLiteral("—");
                cell.state = m.state;
                break;
            }
            case Conf: {
                const SwingSeries &s = series[col.src];
                const Match m = resolveValue(s, t, 0, fillMode, primaryHeld);
                if (m.has && m.idx >= 0 && m.idx < s.conf.size()) {
                    const double c = s.conf[m.idx];
                    cell.text  = confText(c);
                    cell.state = (c < kConfThreshold) ? LowConf : Ok;
                } else {
                    cell.text = QStringLiteral("—"); cell.state = Missing;
                }
                break;
            }
            case DeltaT: {
                const bool exact = primary && pm.idx >= 0
                                && (pm.state == Ok || pm.state == Resync || pm.state == LowConf);
                if (exact) {
                    const qint64 d = (pm.idx > 0) ? (primary->t[pm.idx] - primary->t[pm.idx - 1]) : 0;
                    cell.text  = msText(d);
                    cell.state = (d > primary->gapToleranceUs) ? Resync : Ok;
                } else {
                    cell.text = QStringLiteral("—"); cell.state = Missing;
                }
                break;
            }
            case State:
                cell.text  = primary ? stateWord(pm.state) : QStringLiteral("—");
                cell.state = primary ? pm.state : Missing;
                break;
            }
            row.push_back(cell);
        }
        m_rows.push_back(row);
    }
    endResetModel();
}

} // namespace pinpoint
