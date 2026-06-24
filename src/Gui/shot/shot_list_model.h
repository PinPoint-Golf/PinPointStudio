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
// property `shotModel`.  All shot mutation (rating, note, trash) goes
// through the Q_INVOKABLEs below — QML holds no business logic.
//
// Trash: moveToTrash() moves the shot's swing_NNNN/ folder to the operating
// system trash (QFile::moveToTrash — cross-platform, recoverable there) and
// removes the row.  There is no in-app undo: recovery is the OS trash's job.
//
// Shots arrive via ShotProcessor::maybeJoin() → addShot() at the
// analysis+export join of each detected shot.

class ShotListModel : public QAbstractListModel
{
    Q_OBJECT

    // Total shot count — the "of M" in the carousel's "N of M" label.
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
        MetricsRole,
        AnalysisDetailRole,
        SwingDirRole,
    };

    explicit ShotListModel(QObject *parent = nullptr);   // starts empty

    int                    rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant               data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int activeCount()   const;

    // ShotProcessor's entry point — prepends a shot (newest first) with the
    // next ordinal and a fresh id. swingDir links the row to its on-disk folder
    // so rating/note edits write through to swing.json (empty when the export
    // produced no directory). Returns the new shot's id (the carousel row
    // identity) so the caller can promote the just-captured shot into Review.
    int addShot(const QString &swingDir, const QString &timestampLabel, const QString &club,
                bool hasVideo, const QUrl &thumbnailSource, const QVariantList &tracePoints,
                int score, const QVariantMap &metrics,
                const QVariantMap &analysisDetail = {});

    // Reload a shot from disk (SwingDocReader): uses the stored ordinal and links the
    // row to its swingDir; rating/note are restored from the persisted "review" block.
    void addPersistedShot(const QString &swingDir, int ordinal, const QString &timestampLabel,
                          const QString &club, bool hasVideo, const QUrl &thumbnailSource,
                          int score, int rating, const QString &note,
                          const QVariantMap &metrics, const QVariantMap &analysisDetail);

    // Re-read <swingDir>/swing.json (SwingDocReader) and update the matching row's
    // score / metrics / analysisDetail in place — emits dataChanged for just those
    // roles, so the row keeps its identity, focus and ordering. Called from QML on
    // the carousel's ACTIVE model after ReanalysisController writes a fresh analysis
    // back. No-op if the dir isn't in this model or the doc can't be read.
    Q_INVOKABLE void refreshShot(const QString &swingDir);

    // Drop every row (model reset). Used by SessionReviewController when swapping
    // the loaded session into its private review instance; the live shotModel
    // never calls this.
    void clear();

    Q_INVOKABLE void setRating(int id, int n);
    Q_INVOKABLE void setNote(int id, const QString &text);
    // Move the shot's swing_NNNN/ folder to the OS trash and remove its row.
    // Returns false (and keeps the row) if the move fails — e.g. no trash is
    // available — so a shot is never lost from view while its files remain.
    // An analysis-only shot (empty swingDir) just drops from the model.
    Q_INVOKABLE bool moveToTrash(int id);
    // Trash each id; returns the number actually moved to the OS trash.
    Q_INVOKABLE int  moveAllToTrash(const QVariantList &ids);

    // Resolve a list of shot ids (e.g. ShotFilterProxyModel::visibleShotIds()) to
    // their absolute swing_NNNN directories, skipping analysis-only shots with no
    // on-disk folder. Backs the carousel's bulk export-to-zip flow.
    Q_INVOKABLE QVariantList swingDirsForIds(const QVariantList &ids) const;

    // Focused-shot metadata for the carousel's PpShotActionBar (the scope-aware
    // action header). Returns a map with the identity fields the bar renders:
    //   { valid, ordinal, club, timestampLabel, score, rating, hasVideo, swingDir }
    // Model-scoped, NOT proxy-scoped: a row filtered out of a carousel's proxy
    // still resolves here (the focused swing on the stage may not be in view).
    // An unknown / -1 id returns { valid: false } — present and false so QML can
    // bind `summary.valid` safely without a null guard.
    Q_INVOKABLE QVariantMap shotSummary(int id) const;

    // The analysisDetail of the swing captured just BEFORE the one at `swingDir` (the next-older
    // sibling; rows are newest-first). Empty if `swingDir` isn't in this model or has no predecessor.
    // Backs the wrist diagnostics "compare to previous" ghost.
    Q_INVOKABLE QVariantMap previousAnalysisDetail(const QString &swingDir) const;

    // The analysisDetail of the swing at `swingDir` (empty if not in this model). Backs the wrist
    // diagnostics "compare to reference" ghost.
    Q_INVOKABLE QVariantMap analysisDetailForSwingDir(const QString &swingDir) const;

signals:
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
        QVariantMap  metrics;         // key → { label, value }
        QVariantMap  analysisDetail;  // { tier, overall, series:[…], phases:[…] } for the graph
        QString      swingDir;        // on-disk folder, for reloaded shots (replay-from-MP4 later)
    };

    int  rowForId(int id) const;
    void persistReview(int row);   // write-through rating/note to swing.json

    QVector<Shot> m_shots;
    int           m_nextId = 1;
};
