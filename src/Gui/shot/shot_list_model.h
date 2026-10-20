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
#include "qml_payload.h"

#include <QAbstractListModel>
#include <QElapsedTimer>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

#include <functional>

// ShotListModel — the session-global list of captured shots, shared by every
// mode screen's PpShotCarousel.  Exposed in main.cpp as the QML context
// property `shotModel`.  All shot mutation (rating, note, trash) goes
// through the Q_INVOKABLEs below — QML holds no business logic.
//
// Trash: trashShots() moves each shot's swing_NNNN/ folder to the trash
// (SwingPaths::trashPath — the OS trash, or the library's own on a volume with
// none) and removes its row.  There is no in-app undo: recovery is the trash's
// job.  ⚠ A FOLDER WHOSE VIDEOS ARE OPEN CANNOT BE MOVED ON WINDOWS (#56), and
// selecting a swing opens them, so the move first asks main.cpp's releaser to
// let go of the folder and then retries for a few seconds while the player
// closes its files.
//
// Shots arrive via ShotProcessor::maybeJoin() → addShot() at the
// analysis+export join of each detected shot.

class ShotListModel : public QAbstractListModel
{
    Q_OBJECT

    // Total shot count — the "of M" in the carousel's "N of M" label.
    Q_PROPERTY(int activeCount   READ activeCount   NOTIFY activeCountChanged)
    // The canonical golf-bag club list backing the swing-edit picker. Constant —
    // one source of truth shared by every carousel/model instance.
    Q_PROPERTY(QStringList clubOptions READ clubOptions CONSTANT)

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
        DataWarningRole,   // an integrity block warns (IMU re-fusion, or frames lost in capture)
        LmDeviceKindRole,  // launchMonitor.kind ("gcquad"); empty for a shot no device measured
        DataWarningDetailRole,  // the facts behind dataWarning (swing_doc.h dataWarningDetailFrom)
        // The launch monitor board's per-shot annotations (review.excluded / review.clubOverride,
        // PersistedShot::excluded). Read by LmSessionModel; written through setLmExcluded /
        // setLmClubOverride. Nothing else in the app interprets them.
        LmExcludedRole,         // bool — left out of the board's statistics
        LmClubOverrideRole,     // QString — the club the board treats the shot as; "" = default
    };

    explicit ShotListModel(QObject *parent = nullptr);   // starts empty

    int                    rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant               data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int activeCount()   const;

    // The standard golf bag, driver → putter — the shared canonical club
    // vocabulary (club_vocabulary.h), also used by the Markup Lab picker.
    // Backs the swing-edit popover's club combo.
    static QStringList clubOptions();

    // ShotProcessor's entry point — prepends a shot (newest first) with the
    // next ordinal and a fresh id. swingDir links the row to its on-disk folder
    // so rating/note edits write through to swing.json (empty when the export
    // produced no directory). Returns the new shot's id (the carousel row
    // identity) so the caller can promote the just-captured shot into Review.
    int addShot(const QString &swingDir, const QString &timestampLabel, const QString &club,
                bool hasVideo, const QUrl &thumbnailSource, const QVariantList &tracePoints,
                int score, const QVariantMap &metrics,
                const QVariantMap &analysisDetail = {}, bool dataWarning = false,
                const QVariantMap &dataWarningDetail = {});

    // Reload a shot from disk (SwingDocReader): uses the stored ordinal and links the
    // row to its swingDir; rating/note are restored from the persisted "review" block.
    void addPersistedShot(const QString &swingDir, int ordinal, const QString &timestampLabel,
                          const QString &club, bool hasVideo, const QUrl &thumbnailSource,
                          int score, int rating, const QString &note,
                          const QVariantMap &metrics, const QVariantMap &analysisDetail,
                          bool dataWarning = false, const QString &lmDeviceKind = {},
                          const QVariantMap &dataWarningDetail = {},
                          bool lmExcluded = false, const QString &lmClubOverride = {});

    // Re-read <swingDir>/swing.json (SwingDocReader) and update the matching row's
    // score / metrics / analysisDetail in place — emits dataChanged for just those
    // roles, so the row keeps its identity, focus and ordering. Called from QML on
    // the carousel's ACTIVE model after ReanalysisController writes a fresh analysis
    // back. No-op if the dir isn't in this model or the doc can't be read.
    Q_INVOKABLE void refreshShot(const QString &swingDir);

    // Give an EXISTING row a swingDir it did not have, and re-read it from there.
    //
    // A shot whose analysis and export both produced nothing still gets a carousel row,
    // with an empty swingDir — it is in-memory only. When a launch monitor later fills
    // that shot's folder with a device-only document, the row has to be pointed at it
    // rather than a second row appearing: two rows for one swing is worse than none.
    // No-op when the id is unknown or the row already has a different swingDir.
    Q_INVOKABLE void attachSwingDir(int id, const QString &swingDir);

    // Point the (live) carousel at one session folder: clear(), then reload every
    // swing.json under `dir` via SwingDocReader (as addPersistedShot). An empty
    // `dir` just clears — an empty carousel. Called on wrist-screen entry and at
    // session start/end to keep the carousel today-scoped; never during live
    // capture (guarded in QML), so it can't clobber freshly captured in-memory shots.
    Q_INVOKABLE void loadSessionDir(const QString &dir);

    // Drop every row (model reset). Used by SessionReviewController when swapping
    // the loaded session into its private review instance, and by loadSessionDir()
    // above when the live shotModel is re-pointed at a different session folder.
    void clear();

    Q_INVOKABLE void setRating(int id, int n);
    Q_INVOKABLE void setNote(int id, const QString &text);
    // Set the shot's club (from clubOptions); write-through to swing.json's
    // review block so it survives a restart. No-op if unchanged / id unknown.
    Q_INVOKABLE void setClub(int id, const QString &club);
    // The launch monitor board's two annotations (LmSessionModel::setShotExcluded / setShotClub
    // call these). Write-through to the review block (SwingDocWriter::updateShotAnnotations), so
    // they survive a reload. A shot with no swingDir keeps them in memory only, like a rating.
    //   setLmExcluded      leave the shot out of the board's statistics, or restore it
    //   setLmClubOverride  "" restores the default; otherwise a club from clubOptions, or the
    //                      shot's own recorded club (which may predate the vocabulary) —
    //                      anything else is refused, for the reason setClub refuses it
    Q_INVOKABLE void setLmExcluded(int id, bool excluded);
    Q_INVOKABLE void setLmClubOverride(int id, const QString &club);
    // Move the shot's swing_NNNN/ folder to the trash and remove its row, in ONE
    // attempt, now. Returns false (and keeps the row) if the move fails, so a
    // shot is never lost from view while its files remain. An analysis-only
    // shot (empty swingDir) just drops from the model. The carousel uses
    // trashShots() below; this is the synchronous form for callers that can
    // take a refusal.
    Q_INVOKABLE bool moveToTrash(int id);
    // Trash each id with moveToTrash(); returns the number actually moved.
    Q_INVOKABLE int  moveAllToTrash(const QVariantList &ids);

    // THE CAROUSEL'S DELETE: any set of shots, by id, focused or not.
    //   1. A shot the busy check refuses (it is being re-analysed) stays, with
    //      the reason.
    //   2. The releaser is asked to let go of every other folder (the replay
    //      stops if it is playing one).
    //   3. Each folder is moved now; one that is refused is tried again on a
    //      back-off for a few seconds, because a player closes its files a
    //      moment after it is told to and Windows will not move a folder with
    //      a file open in it.
    // Each row leaves as its folder does (shotTrashed); trashFinished reports
    // the whole request once nothing is left to try. A call while one is still
    // retrying joins it, and is reported with it.
    Q_INVOKABLE void trashShots(const QVariantList &ids);
    bool trashPending() const { return !m_trashPending.isEmpty(); }

    // The hooks trashShots() runs through, set in main.cpp (and by the tests).
    //   releaser   — told each folder about to be moved; lets go of it.
    //   busyCheck  — "" when the folder may go now, else why not ("is being
    //                re-analysed"), which the request reports as the reason.
    //   trashFn    — the move itself; SwingPaths::trashPath by default.
    using FolderFn  = std::function<void(const QString &swingDir)>;
    using ReasonFn  = std::function<QString(const QString &swingDir)>;
    using TrashFn   = std::function<bool(const QString &swingDir)>;
    void setFolderReleaser(FolderFn fn) { m_releaser = std::move(fn); }
    void setFolderBusyCheck(ReasonFn fn) { m_busyCheck = std::move(fn); }
    void setTrashFunction(TrashFn fn)    { m_trashFn = std::move(fn); }
    // The retry's first interval (it doubles) and how long a refused folder is
    // tried for in all. Tests shorten them.
    void setTrashRetry(int firstIntervalMs, int giveUpMs)
    { m_trashFirstRetryMs = firstIntervalMs; m_trashGiveUpMs = giveUpMs; }

    // Resolve a list of shot ids (e.g. ShotFilterProxyModel::visibleShotIds()) to
    // their absolute swing_NNNN directories, skipping analysis-only shots with no
    // on-disk folder. Backs the carousel's bulk export-to-zip flow.
    Q_INVOKABLE QVariantList swingDirsForIds(const QVariantList &ids) const;

    // Focused-shot metadata for the carousel's PpShotActionBar (the scope-aware
    // action header). Returns a map with the identity fields the bar renders:
    //   { valid, ordinal, club, timestampLabel, score, rating, note, hasVideo, swingDir }
    // Model-scoped, NOT proxy-scoped: a row filtered out of a carousel's proxy
    // still resolves here (the focused swing on the stage may not be in view).
    // An unknown / -1 id returns { valid: false } — present and false so QML can
    // bind `summary.valid` safely without a null guard.
    Q_INVOKABLE QVariantMap shotSummary(int id) const;

    // The analysisDetail of the swing captured just BEFORE the one at `swingDir` (the next-older
    // sibling; rows are newest-first). Empty if `swingDir` isn't in this model or has no predecessor.
    // Backs the wrist diagnostics "compare to previous" ghost.
    Q_INVOKABLE QVariantMap previousAnalysisDetail(const QString &swingDir) const;

    // The analysisDetail of the swing at `swingDir`, from this model if it holds that row and
    // FROM DISK IF IT DOES NOT. Backs the wrist diagnostics "compare to reference" ghost, and
    // the fallback is what makes that ghost work at all: the reference is a swing the golfer
    // marked once and it persists in settings, so by tomorrow it belongs to a session no model
    // has loaded. Answering "not in this list" as "does not exist" made the reference curve
    // disappear overnight without a word. Empty only when the document cannot be read.
    // One full parse on the miss, and the callers bind it to a swingDir that rarely changes.
    Q_INVOKABLE QVariantMap analysisDetailForSwingDir(const QString &swingDir) const;

signals:
    void activeCountChanged();
    // A shot's row left because its folder went to the trash (swingDir is ""
    // for a shot that had none). The session panels forget the swing on this.
    void shotTrashed(int id, const QString &swingDir);
    // A trashShots() request is done: `moved` of `requested` shots went, and
    // `reason` says why the rest did not ("" when they all went).
    void trashFinished(int requested, int moved, const QString &reason);

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
        QmlPayload   analysisDetail;  // { tier, overall, series:[…], phases:[…] } — ~650k nodes on a
                                      // live shot; the role hands QML ONE JavaScript object per row
                                      // rather than a copy per read (see qml_payload.h)
        QString      swingDir;        // on-disk folder, for reloaded shots (replay-from-MP4 later)
        bool         dataWarning = false;  // an integrity block warns (swing_doc.h dataWarningDetailFrom)
        QVariantMap  dataWarningDetail;    // its facts: { capture, imu, framesLost, worstHoleMs, … }
        QString      lmDeviceKind;    // which device measured it; only reloaded shots carry it
        bool         lmExcluded = false;   // the launch monitor board's annotations (review block)
        QString      lmClubOverride;
    };

public:
    // The number the carousel SHOWS for this shot.  Deliberately not the id:
    // addShot() answers a monotonic id and the ordinal is a per-session count,
    // and the two diverge the moment a shot is trashed.  A notification saying
    // "Shot 7" must mean the row the user can see.  0 when the id is unknown.
    int ordinalForId(int id) const;

private:

    int  rowForId(int id) const;
    void persistReview(int row);   // write-through rating/note/club to swing.json
    void persistLmAnnotations(int row);   // write-through excluded/clubOverride
    bool trashFolder(const QString &dir) const;   // m_trashFn, or SwingPaths::trashPath
    void removeTrashedRow(int id, const QString &dir);
    void attemptPendingTrash();
    void finishTrash();

    // trashShots() state: the folders still to move, and the request's tally.
    struct PendingTrash { int id = 0; int ordinal = 0; QString dir; int attempts = 0; };
    QVector<PendingTrash> m_trashPending;
    int           m_trashRequested = 0;
    int           m_trashMoved     = 0;
    QStringList   m_trashReasons;
    QTimer        m_trashTimer;
    QElapsedTimer m_trashClock;
    int           m_trashNextRetryMs  = 0;
    int           m_trashFirstRetryMs = 150;
    int           m_trashGiveUpMs     = 6000;
    FolderFn      m_releaser;
    ReasonFn      m_busyCheck;
    TrashFn       m_trashFn;

    QVector<Shot> m_shots;
    int           m_nextId = 1;
    // Next carousel number to hand a shot that has no document of its own.
    // Monotonic within a load: a number this model has issued is never issued
    // again, so "Shot 7" names one shot for the life of the session. Reset by
    // clear(), and moved past every reloaded ordinal by addPersistedShot().
    int           m_nextOrdinal = 1;
};
