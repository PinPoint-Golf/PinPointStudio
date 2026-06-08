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

#include "shot_list_model.h"

ShotListModel::ShotListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int ShotListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_shots.size();
}

QVariant ShotListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_shots.size())
        return {};
    const Shot &s = m_shots.at(index.row());
    switch (role) {
    case ShotIdRole:          return s.id;
    case OrdinalRole:         return s.ordinal;
    case TimestampLabelRole:  return s.timestampLabel;
    case ClubRole:            return s.club;
    case HasVideoRole:        return s.hasVideo;
    case ThumbnailSourceRole: return s.thumbnailSource;
    case TracePointsRole:     return s.tracePoints;
    case ScoreRole:           return s.score;
    case RatingRole:          return s.rating;
    case NoteRole:            return s.note;
    case TrashedRole:         return s.trashed;
    case MetricsRole:         return s.metrics;
    case AnalysisDetailRole:  return s.analysisDetail;
    default:                  return {};
    }
}

QHash<int, QByteArray> ShotListModel::roleNames() const
{
    return {
        { ShotIdRole,          "shotId"          },
        { OrdinalRole,         "ordinal"         },
        { TimestampLabelRole,  "timestampLabel"  },
        { ClubRole,            "club"            },
        { HasVideoRole,        "hasVideo"        },
        { ThumbnailSourceRole, "thumbnailSource" },
        { TracePointsRole,     "tracePoints"     },
        { ScoreRole,           "score"           },
        { RatingRole,          "rating"          },
        { NoteRole,            "note"            },
        { TrashedRole,         "trashed"         },
        { MetricsRole,         "metrics"         },
        { AnalysisDetailRole,  "analysisDetail"  },
    };
}

int ShotListModel::activeCount() const
{
    int n = 0;
    for (const Shot &s : m_shots)
        if (!s.trashed)
            ++n;
    return n;
}

int ShotListModel::rowForId(int id) const
{
    for (int i = 0; i < m_shots.size(); ++i)
        if (m_shots.at(i).id == id)
            return i;
    return -1;
}

void ShotListModel::addShot(const QString &timestampLabel, const QString &club,
                            bool hasVideo, const QUrl &thumbnailSource,
                            const QVariantList &tracePoints, int score,
                            const QVariantMap &metrics,
                            const QVariantMap &analysisDetail)
{
    int maxOrdinal = 0;
    for (const Shot &s : m_shots)
        maxOrdinal = std::max(maxOrdinal, s.ordinal);

    Shot shot;
    shot.id              = m_nextId++;
    shot.ordinal         = maxOrdinal + 1;
    shot.timestampLabel  = timestampLabel;
    shot.club            = club;
    shot.hasVideo        = hasVideo;
    shot.thumbnailSource = thumbnailSource;
    shot.tracePoints     = tracePoints;
    shot.score           = score;
    shot.metrics         = metrics;
    shot.analysisDetail  = analysisDetail;

    // Newest first, matching the mockup's right-of-cap ordering.
    beginInsertRows(QModelIndex(), 0, 0);
    m_shots.prepend(shot);
    endInsertRows();
    emit activeCountChanged();
}

void ShotListModel::setRating(int id, int n)
{
    const int row = rowForId(id);
    if (row < 0)
        return;
    n = std::clamp(n, 0, 5);
    if (m_shots[row].rating == n)
        return;
    m_shots[row].rating = n;
    emit dataChanged(index(row), index(row), { RatingRole });
}

void ShotListModel::setNote(int id, const QString &text)
{
    const int row = rowForId(id);
    if (row < 0 || m_shots.at(row).note == text)
        return;
    m_shots[row].note = text;
    emit dataChanged(index(row), index(row), { NoteRole });
}

void ShotListModel::moveToTrash(int id)
{
    const int row = rowForId(id);
    if (row < 0 || m_shots.at(row).trashed)
        return;
    m_shots[row].trashed = true;
    emit dataChanged(index(row), index(row), { TrashedRole });
    m_lastTrashedId = id;
    m_lastTrashBatch = { id };
    emit lastTrashedIdChanged();
    emit activeCountChanged();
}

void ShotListModel::moveAllToTrash(const QVariantList &ids)
{
    QVector<int> batch;
    for (const QVariant &v : ids) {
        const int row = rowForId(v.toInt());
        if (row < 0 || m_shots.at(row).trashed)
            continue;
        m_shots[row].trashed = true;
        emit dataChanged(index(row), index(row), { TrashedRole });
        batch.append(v.toInt());
    }
    if (batch.isEmpty())
        return;
    m_lastTrashedId  = batch.last();
    m_lastTrashBatch = batch;
    emit lastTrashedIdChanged();
    emit activeCountChanged();
}

void ShotListModel::restoreLastTrashed()
{
    const QVector<int> batch = m_lastTrashBatch;
    m_lastTrashBatch.clear();
    for (int id : batch)
        restore(id);
}

void ShotListModel::restore(int id)
{
    const int row = rowForId(id);
    if (row < 0 || !m_shots.at(row).trashed)
        return;
    m_shots[row].trashed = false;
    emit dataChanged(index(row), index(row), { TrashedRole });
    if (m_lastTrashedId == id) {
        m_lastTrashedId = -1;
        emit lastTrashedIdChanged();
    }
    emit activeCountChanged();
}
