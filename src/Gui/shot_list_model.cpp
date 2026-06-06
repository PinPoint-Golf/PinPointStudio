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

#include <QPointF>
#include <cmath>

namespace {

// Deterministic pseudo-random wrist trace, normalised to 0..1 — mirrors the
// mockup spark generator so each stub shot gets a distinct but stable curve.
QVariantList stubTracePoints(int seed)
{
    QVariantList pts;
    double y = 0.6;
    for (int i = 0; i <= 12; ++i) {
        const double noise = double((seed * 9301 + i * 49297) % 233280) / 233280.0 - 0.5;
        y += (std::sin(i * 1.2 + seed) * 0.5 + noise) * 0.16;
        y = std::clamp(y, 0.16, 0.84);
        pts.append(QPointF(i / 12.0, y));
    }
    return pts;
}

// Placeholder wrist metrics — keys mirror the Wrist goal vocabulary used by
// ScreenWrist's metricKeys; labels/values match the design mockups.
QVariantMap stubWristMetrics()
{
    auto metric = [](const QString &label, const QString &value) {
        return QVariantMap{ { QStringLiteral("label"), label },
                            { QStringLiteral("value"), value } };
    };
    return QVariantMap{
        { QStringLiteral("impactConditions"),    metric(QStringLiteral("Lead wrist · impact"), QStringLiteral("12° flex"))  },
        { QStringLiteral("wristAngleTop"),       metric(QStringLiteral("Wrist @ top"),         QStringLiteral("Cupped 7°")) },
        { QStringLiteral("trailWristExtension"), metric(QStringLiteral("Trail wrist"),         QStringLiteral("Retained"))  },
        { QStringLiteral("transition"),          metric(QStringLiteral("Transition"),          QStringLiteral("−4°→+11°")) },
    };
}

} // namespace

ShotListModel::ShotListModel(QObject *parent)
    : QAbstractListModel(parent)
{
    seedStubShots();
}

void ShotListModel::seedStubShots()
{
    // Mockup data: ordinals 7→1 (newest first), scores spanning all four
    // quality bands, two IMU-only shots (#4 and #1).
    struct Seed { int ordinal; const char *time; int score; int rating; bool hasVideo; };
    static const Seed seeds[] = {
        { 7, "12:03:48", 82, 3, true  },
        { 6, "11:58:12", 68, 4, true  },
        { 5, "11:52:31", 41, 2, true  },
        { 4, "11:47:05", 73, 3, false },
        { 3, "11:41:44", 55, 0, true  },
        { 2, "11:36:20", 88, 5, true  },
        { 1, "11:30:02", 23, 1, false },
    };

    for (const Seed &s : seeds) {
        Shot shot;
        shot.id             = m_nextId++;
        shot.ordinal        = s.ordinal;
        shot.timestampLabel = QString::fromLatin1(s.time);
        shot.club           = QStringLiteral("DRIVER");
        shot.hasVideo       = s.hasVideo;
        shot.tracePoints    = stubTracePoints(s.ordinal);
        shot.score          = s.score;
        shot.rating         = s.rating;
        shot.metrics        = stubWristMetrics();
        m_shots.append(shot);
    }
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
                            const QVariantMap &metrics)
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
