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
    : QAbstractListModel(parent)
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
    if (r.isLive)
        return true;                        // the way back to live is never filtered away
    for (const QString &d : m_deviceFilter)
        if (!r.devices.contains(d))
            return false;
    return true;
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

void SessionListModel::setDeviceFilter(const QStringList &tokens)
{
    QStringList t = tokens;
    t.removeDuplicates();
    pinpoint::sortDeviceTokens(t);
    if (t == m_deviceFilter)
        return;
    m_deviceFilter = t;
    rebuildShown();
    emit deviceFilterChanged();
    emit availableDevicesChanged();         // a token kept only because it was selected may go
}

void SessionListModel::toggleDevice(const QString &token)
{
    QStringList t = m_deviceFilter;
    if (!t.removeAll(token))
        t.append(token);
    setDeviceFilter(t);
}

QVariantList SessionListModel::availableDevices() const
{
    QStringList tokens = m_deviceFilter;
    for (const Row &r : m_all)
        if (!r.isLive)
            for (const QString &d : r.devices)
                if (!tokens.contains(d))
                    tokens.append(d);
    pinpoint::sortDeviceTokens(tokens);

    QVariantList out;
    for (const QString &d : std::as_const(tokens)) {
        // Counted within the CURRENT filter, so each chip says how many sessions adding it would
        // leave (AND) — a fixed library count read as if chips widened the list. For a chip that is
        // already on, this is simply the number shown.
        int n = 0;
        for (const Row &r : m_all)
            if (!r.isLive && passes(r) && r.devices.contains(d))
                ++n;
        out.append(QVariantMap{ { QStringLiteral("id"),    d },
                                { QStringLiteral("label"), pinpoint::deviceLabel(d) },
                                { QStringLiteral("count"), n } });
    }
    return out;
}

int SessionListModel::savedCount() const
{
    int n = 0;
    for (const Row &r : m_all)
        if (!r.isLive) ++n;
    return n;
}

int SessionListModel::shownSavedCount() const
{
    int n = 0;
    for (const Row &r : m_rows)
        if (!r.isLive) ++n;
    return n;
}
