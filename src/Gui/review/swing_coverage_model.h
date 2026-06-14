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

#include <QAbstractListModel>
#include <QQmlEngine>
#include <QVector>

#include "swing_data_source.h"

namespace pinpoint {

// One row per resolved (source,part) lane. `bins` is a fixed-N (default 64) summary
// across the current window: 0 present, 1 low-confidence, 2 gap. Each bucket takes
// its worst-case state so the strip stays stable as the window changes. Rebuilt by
// SwingDataSource on region/window change. Read-only.
class SwingCoverageModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by SwingDataSource")

public:
    enum Roles { LabelRole = Qt::UserRole + 1, ColorKeyRole, BinsRole };
    enum BinState { Present = 0, LowConf = 1, Gap = 2 };
    Q_ENUM(BinState)

    explicit SwingCoverageModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex & = {}) const override;
    QVariant data(const QModelIndex &, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Configured by SwingDataSource::rebuild(): bin every lane across the window.
    void setLanes(const QVector<SwingSeries> &lanes, qint64 windowStartUs,
                  qint64 windowEndUs, int bins = 64);

private:
    struct Lane {
        QString      label;
        QString      colorKey;
        QVariantList bins;   // list<int> of BinState
    };
    QVector<Lane> m_lanes;
};

} // namespace pinpoint
