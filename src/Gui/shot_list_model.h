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
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

// ShotListModel — the session-global list of captured shots, shared by every
// mode screen's PpShotCarousel.  Exposed in main.cpp as the QML context
// property `shotModel`.  All shot mutation (rating, note, soft delete) goes
// through the Q_INVOKABLEs below — QML holds no business logic.
//
// Soft delete: moveToTrash() only flags the shot; ShotFilterProxyModel drops
// flagged rows from every carousel.  Nothing is removed from disk — the
// on-disk purge belongs to the Resource Monitor's future "empty trash".
//
// Shots arrive via ShotProcessor::maybeJoin() → addShot() at the
// analysis+export join of each detected shot.

class ShotListModel : public QAbstractListModel
{
    Q_OBJECT

    // Id of the most recently trashed shot — the Undo toast restores it.
    Q_PROPERTY(int lastTrashedId READ lastTrashedId NOTIFY lastTrashedIdChanged)
    // Non-trashed shot count — the "of M" in the carousel's "N of M" label.
    Q_PROPERTY(int activeCount   READ activeCount   NOTIFY activeCountChanged)

public:
    enum Roles {
        ShotIdRole = Qt::UserRole + 1,
        OrdinalRole,
        TimestampLabelRole,
        ClubRole,
        HasVideoRole,
        ThumbnailSourceRole,
        TracePointsRole,
        ScoreRole,
        RatingRole,
        NoteRole,
        TrashedRole,
        MetricsRole,
        AnalysisDetailRole,
    };

    explicit ShotListModel(QObject *parent = nullptr);   // starts empty

    int                    rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant               data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int lastTrashedId() const { return m_lastTrashedId; }
    int activeCount()   const;

    // ShotProcessor's entry point — prepends a shot (newest first) with the
    // next ordinal and a fresh id.
    void addShot(const QString &timestampLabel, const QString &club, bool hasVideo,
                 const QUrl &thumbnailSource, const QVariantList &tracePoints,
                 int score, const QVariantMap &metrics,
                 const QVariantMap &analysisDetail = {});

    // Reload a shot from disk (SwingDocReader): uses the stored ordinal and links the
    // row to its swingDir. rating/note aren't persisted yet, so they start cleared.
    void addPersistedShot(const QString &swingDir, int ordinal, const QString &timestampLabel,
                          const QString &club, bool hasVideo, const QUrl &thumbnailSource,
                          int score, const QVariantMap &metrics, const QVariantMap &analysisDetail);

    Q_INVOKABLE void setRating(int id, int n);
    Q_INVOKABLE void setNote(int id, const QString &text);
    Q_INVOKABLE void moveToTrash(int id);
    Q_INVOKABLE void moveAllToTrash(const QVariantList &ids);
    Q_INVOKABLE void restore(int id);
    // Restores the last moveToTrash/moveAllToTrash batch — the Undo toast's
    // single entry point for both the one-shot and bulk delete paths.
    Q_INVOKABLE void restoreLastTrashed();

signals:
    void lastTrashedIdChanged();
    void activeCountChanged();

private:
    struct Shot {
        int          id = 0;
        int          ordinal = 0;
        QString      timestampLabel;
        QString      club;
        bool         hasVideo = false;
        QUrl         thumbnailSource;
        QVariantList tracePoints;     // normalised QPointF list (0..1)
        int          score = 0;       // 0–100 quality (placeholder for now)
        int          rating = 0;      // 0–5 user stars
        QString      note;
        bool         trashed = false;
        QVariantMap  metrics;         // key → { label, value }
        QVariantMap  analysisDetail;  // { tier, overall, series:[…], phases:[…] } for the graph
        QString      swingDir;        // on-disk folder, for reloaded shots (replay-from-MP4 later)
    };

    int rowForId(int id) const;

    QVector<Shot> m_shots;
    int           m_nextId = 1;
    int           m_lastTrashedId = -1;
    QVector<int>  m_lastTrashBatch;   // ids of the most recent trash action
};
