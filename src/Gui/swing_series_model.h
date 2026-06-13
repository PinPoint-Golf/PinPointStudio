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

#include <QAbstractTableModel>
#include <QQmlEngine>
#include <QVector>

#include "swing_data_source.h"

namespace pinpoint {

// Windowed, gap-aware grid. Rows = union of resolved sources' t_us within the window
// (deduped within half-min-period). Columns are dynamic: [Time] + per resolved source
// a Value (raw+fused for the primary IMU) and a Conf where the source has one, then
// [DeltaT, State] for the primary source. Cells carry a display string and a CellState
// the delegate colours. fillMode "off" leaves holes; "nearest" carries the nearest
// sample. Read-only. Fully precomputed by configure() so data() stays O(1).
class SwingSeriesModel : public QAbstractTableModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by SwingDataSource")

public:
    enum Roles { DisplayRole2 = Qt::UserRole + 1, StateRole, AlignRole };
    enum CellState {
        Ok = 0, Missing, Held, Resync, LowConf, DerivedHeld /* metric from held source */, PhaseChange
    };
    Q_ENUM(CellState)
    enum ColKind { Time, Value, Conf, DeltaT, State };
    Q_ENUM(ColKind)

    explicit SwingSeriesModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex & = {}) const override;
    int columnCount(const QModelIndex & = {}) const override;
    QVariant data(const QModelIndex &, int role) const override;
    QVariant headerData(int section, Qt::Orientation, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE QString columnColorKey(int col) const;  // header top-border tint

    // Nearest row index to a window-relative timestamp `us` (same µs domain as
    // shotReplay.positionUs), or -1 when `us` falls outside [windowStartUs,
    // windowEndUs] — lets the replay playhead drive the table's current row while a
    // phase-windowed table leaves the keyboard cursor alone when the playhead is
    // elsewhere. O(log n).
    Q_INVOKABLE int rowForTimeUs(qint64 us) const;

    // Configured by SwingDataSource::rebuild().
    void configure(const QVector<SwingSeries> &series, qint64 windowStartUs,
                   qint64 windowEndUs, const QString &fillMode);

private:
    struct Cell { QString text; int state = Ok; int align = Qt::AlignRight; };
    struct Column {
        ColKind kind = Time;
        int     src  = -1;   // index into the configured series (Value/Conf)
        int     sub  = 0;    // 0 value · 1 raw · 2 fused (primary IMU value cols)
        QString header;
        QString colorKey;
        int     align = Qt::AlignRight;
    };

    QVector<Column>          m_cols;
    QVector<QVector<Cell>>   m_rows;     // [row][col]
    QVector<qint64>          m_rowTimeUs;          // parallel to m_rows; ascending
    qint64                   m_windowStartUs = 0;  // inclusive bounds for rowForTimeUs
    qint64                   m_windowEndUs   = 0;
};

} // namespace pinpoint
