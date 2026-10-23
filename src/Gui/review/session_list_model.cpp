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

#include "session_list_model.h"

#include <QVariantMap>

#include "session_summary.h"

SessionListModel::SessionListModel(QObject *parent)
    : DeviceFilterModel(parent)
{
}

int SessionListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

QVariant SessionListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const Row &r = m_rows.at(index.row());
    switch (role) {
    case SessionIdRole:     return r.sessionId;
    case DayLabelRole:      return r.dayLabel;
    case TimeLabelRole:     return r.timeLabel;
    case ClubMixRole:       return r.clubMix;
    case ShotCountRole:     return r.shotCount;
    case LengthLabelRole:   return r.lengthLabel;
    case AvgQualityRole:    return r.avgQuality;
    case IsLiveRole:        return r.isLive;
    case PreviewThumbsRole: return r.previewThumbs;
    case IndexedRole:       return r.indexed;
    case DevicesRole:       return r.devices;
    case DeviceLabelsRole: {
        QStringList labels;
        labels.reserve(r.devices.size());
        for (const QString &d : r.devices)
            labels.append(pinpoint::deviceLabel(d));
        return labels;
    }
    default:                return {};
    }
}

QHash<int, QByteArray> SessionListModel::roleNames() const
{
    return {
        { SessionIdRole,     "sessionId"     },
        { DayLabelRole,      "dayLabel"      },
        { TimeLabelRole,     "timeLabel"     },
        { ClubMixRole,       "clubMix"       },
        { ShotCountRole,     "shotCount"     },
        { LengthLabelRole,   "lengthLabel"   },
        { AvgQualityRole,    "avgQuality"    },
        { IsLiveRole,        "isLive"        },
        { PreviewThumbsRole, "previewThumbs" },
        { IndexedRole,       "indexed"       },
        { DevicesRole,       "devices"       },
        { DeviceLabelsRole,  "deviceLabels"  },
    };
}

void SessionListModel::setRows(const QVector<Row> &rows)
{
    m_all = rows;
    rebuildShown();
    emit availableDevicesChanged();
}

bool SessionListModel::passes(const Row &r) const
{
    return r.isLive                         // the way back to live is never filtered away
        || passesDeviceFilter(r.devices);
}

void SessionListModel::rebuildShown()
{
    beginResetModel();
    m_rows.clear();
    for (const Row &r : std::as_const(m_all))
        if (passes(r))
            m_rows.append(r);
    endResetModel();
    emit countsChanged();
}

QVector<QStringList> SessionListModel::savedDeviceSets() const
{
    QVector<QStringList> out;
    for (const Row &r : m_all)
        if (!r.isLive)
            out.append(r.devices);
    return out;
}
