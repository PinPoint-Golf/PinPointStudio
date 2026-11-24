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
#include <QElapsedTimer>
#include <QSet>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

#include <functional>
#include <memory>

#include "../../Identity/identity.h"   // ShotRef (a row's key)
#include "../../App/shot_source.h"     // the port onto one Session (step 5.2)

// ShotListModel — the carousel: a VIEW of one Session's shots (refactor step 5.2; R-QML-5, DEC-199). Three instances:
// the live one (`shotModel`, over the session app.state names live: the capture target, else its live folder), the
// review's (SessionReviewController::shots, over the reviewed Session) and the report's temporary one (over the
// session it makes). All shot mutation (rating, note, club, trash) goes through the Q_INVOKABLEs below — QML holds no
// business logic.
//
// THE ROWS ARE THE SESSION'S SHOTS. A row is made when the view is pointed at a session (its Shots that have a
// document, newest first) or when the Session announces one (EVT-01), changed when one of its Shot's events arrives
// (EVT-21, 04, 05, 06, 07; EVT-22 and EVT-02 re-read the summary), and removed when the Session trashes it (EVT-03).
// Pointing the view at the session it already shows changes nothing: no reset on End, on re-entering a screen, or on
// a reload of the same session (4-a's B3, fixed by design). Every fact a row shows is read from its Shot's summary
// when it is shown; a row keeps only its id, its ShotRef, its folder and presentation (a live shot's thumbnail and
// trace, which End lets go of, as its reload always did).
//
// ONE ROW WITH NO SHOT FACTS: a shot capture saved nothing for (no document) still lands a card — "the shot happened"
// (landShot). Its values are the capture's, there being no document; the row becomes its Shot's in place when a
// document lands for it later (a launch monitor's device-only document: EVT-01 for the same ShotRef, one row, never
// two). End lets it go, as End's reload did.
//
// Trash: trashShots() asks the Session to trash each shot (Session::trashShot — the OS trash, or the library's own on a
// volume with none) and the row leaves on its EVT-03. There is no in-app undo: recovery is the trash's job.
// ⚠ A FOLDER WHOSE VIDEOS ARE OPEN CANNOT BE MOVED ON WINDOWS (#56), and selecting a swing opens them, so the move
// first asks main.cpp's releaser to let go of the folder and then retries for a few seconds while the player closes.

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
        SwingDirRole,
        DataWarningRole,   // an integrity block warns (IMU re-fusion, or frames lost in capture)
        LmDeviceRole,      // the DEVICE that measured it (lm_device.h key, "gcquad" whichever connector); empty for none
        DataWarningDetailRole,  // the facts behind dataWarning (swing_doc.h dataWarningDetailFrom)
        // The launch monitor board's per-shot annotations (review.excluded / review.clubOverride). Read by
        // LmSessionModel; written through setLmExcluded / setLmClubOverride. Nothing else in the app interprets them.
        LmExcludedRole,         // bool — left out of the board's statistics
        LmClubOverrideRole,     // QString — the club the board treats the shot as; "" = default
        // THE CLUBS (refactor 6.2; club_vocabulary.h, R-CLUB-2). ClubRole ("club") is the shot's ONE club, the one every
        // view shows: the board's correction, else the uncorrected club. RecordedClubRole ("recordedClub") is the
        // uncorrected club (the popover's, else the device's, else the captured club): what a correction is made against
        // and what the popover edits. Both "" for an undeclared club, never a stub.
        RecordedClubRole,
        // bool — a stored "DRIVER" stub (club_vocabulary.h swingDocIsLegacyClubStub; J-106, INTERIM pending the Solution
        // Architect): read by the launch monitor board alone, its one exception to the one club rule. False for a shot
        // not yet saved: new captures never write a stub.
        LegacyClubStubRole,
    };

    explicit ShotListModel(QObject *parent = nullptr);   // starts empty, over no session
    ~ShotListModel() override;

    int                    rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant               data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int activeCount()   const;

    // The standard golf bag, driver → putter — the shared canonical club
    // vocabulary (club_vocabulary.h), also used by the Markup Lab picker.
    // Backs the swing-edit popover's club combo.
    static QStringList clubOptions();

    // ── the session this view shows ──
    // How a session folder becomes its source (main.cpp: app.sessionAt, then a SessionShotSource; a test's fake).
    void setSourceFactory(pinpoint::ShotSourceFor factory) { m_sourceFor = std::move(factory); }
    // Show the session in `folder` ("" for none). The session already shown: nothing happens (no reset). Another: one
    // reset, then its Shots that have a document, newest first.
    void showSession(const QString &folder);
    // The same, with the source in hand (the tests; showSession's own path). Null: none.
    void setSource(std::unique_ptr<pinpoint::ShotSource> source);
    QString sessionFolder() const;   // the folder of the session shown, "" for none
    bool    hasSavedShots() const;   // a row of a Shot's (a document), not only a capture's card
    // Capture has let go of its session (End): the cards of shots it saved nothing for leave, and the live rows'
    // presentation (a live shot's trace and thumbnail) with them — the rows then show what the Shots hold, as End's
    // reload of the carousel always did. No reset.
    void forgetCapture();
    // Every row reads its Shot's summary again, in place (an archived session restored under the review: its media
    // are back). No reset.
    void rereadShots();

    // ── the capture path (ShotProcessor at the join of each detected shot) ──
    // What the capture had for the shot: its ShotRef (null when no swing was allocated) and its presentation; the rest
    // is shown only for a shot capture saved nothing for (no document: no Shot facts to show).
    struct CapturedShot {
        pinpoint::ShotRef shot;
        QString      timestampLabel;
        QString      club;
        bool         hasVideo = false;
        QUrl         thumbnailSource;
        QVariantList tracePoints;     // normalised QPointF list (0..1)
        int          score = 0;
        QVariantMap  metrics;
        bool         dataWarning = false;
        QVariantMap  dataWarningDetail;
    };
    // The shot's card, found or made: its row (made at EVT-01 when the document landed), else one made now — from the
    // Shot when the session shown holds it, else from the capture's values (nothing saved). Returns the row's id (the
    // carousel row identity), which the caller promotes into Review.
    int landShot(const CapturedShot &captured, bool documentWritten);

    Q_INVOKABLE void setRating(int id, int n);
    Q_INVOKABLE void setNote(int id, const QString &text);
    // Set the shot's declared club (from clubOptions): the Shot's command (EVT-21). No-op if unchanged / id unknown.
    Q_INVOKABLE void setClub(int id, const QString &club);
    // The launch monitor board's two annotations (LmSessionModel::setShotExcluded / setShotClub call these): the Shot's
    // commands (EVT-05, EVT-04). A card with no document keeps them in memory only, like a rating.
    //   setLmExcluded      leave the shot out of the board's statistics, or restore it
    //   setLmClubOverride  "" restores the default; otherwise a club from clubOptions, or the
    //                      shot's own recorded club (which may predate the vocabulary) —
    //                      anything else is refused, for the reason setClub refuses it
    Q_INVOKABLE void setLmExcluded(int id, bool excluded);
    Q_INVOKABLE void setLmClubOverride(int id, const QString &club);
    // Trash the shot in ONE attempt, now. Returns false (and keeps the row) if the move fails, so a shot is never lost
    // from view while its files remain. A card with no document just drops. The carousel uses trashShots() below; this
    // is the synchronous form for callers that can take a refusal.
    Q_INVOKABLE bool moveToTrash(int id);
    // Trash each id with moveToTrash(); returns the number actually moved.
    Q_INVOKABLE int  moveAllToTrash(const QList<int> &ids);

    // THE CAROUSEL'S DELETE: any set of shots, by id, focused or not.
    //   1. A shot the busy check refuses (it is being re-analysed) stays, with
    //      the reason.
    //   2. The releaser is asked to let go of every other folder (the replay
    //      stops if it is playing one).
    //   3. Each is trashed now; one that is refused is tried again on a
    //      back-off for a few seconds, because a player closes its files a
    //      moment after it is told to and Windows will not move a folder with
    //      a file open in it.
    // Each row leaves as its shot does (shotTrashed); trashFinished reports
    // the whole request once nothing is left to try. A call while one is still
    // retrying joins it, and is reported with it.
    Q_INVOKABLE void trashShots(const QList<int> &ids);
    bool trashPending() const { return !m_trashPending.isEmpty(); }

    // The hooks trashShots() runs through, set in main.cpp (and by the tests).
    //   releaser   — told each folder about to be moved; lets go of it.
    //   busyCheck  — "" when the folder may go now, else why not ("is being
    //                re-analysed"), which the request reports as the reason.
    using FolderFn  = std::function<void(const QString &swingDir)>;
    using ReasonFn  = std::function<QString(const QString &swingDir)>;
    void setFolderReleaser(FolderFn fn) { m_releaser = std::move(fn); }
    void setFolderBusyCheck(ReasonFn fn) { m_busyCheck = std::move(fn); }
    // The retry's first interval (it doubles) and how long a refused folder is
    // tried for in all. Tests shorten them.
    void setTrashRetry(int firstIntervalMs, int giveUpMs)
    { m_trashFirstRetryMs = firstIntervalMs; m_trashGiveUpMs = giveUpMs; }

    // Resolve a list of shot ids (e.g. ShotFilterProxyModel::visibleShotIds()) to
    // their absolute swing_NNNN directories, skipping cards with no on-disk folder.
    // Backs the carousel's bulk export-to-zip flow.
    Q_INVOKABLE QStringList swingDirsForIds(const QList<int> &ids) const;

    // Focused-shot metadata for the carousel's PpShotActionBar (the scope-aware
    // action header). Returns a map with the identity fields the bar renders:
    //   { valid, ordinal, club, timestampLabel, score, rating, note, hasVideo, swingDir }
    // Model-scoped, NOT proxy-scoped: a row filtered out of a carousel's proxy
    // still resolves here (the focused swing on the stage may not be in view).
    // An unknown / -1 id returns { valid: false } — present and false so QML can
    // bind `summary.valid` safely without a null guard.
    Q_INVOKABLE QVariantMap shotSummary(int id) const;

    // THE SELECTION'S EDGE (refactor step 4-a; R-APP-4). app.state.selection holds a shot by its ShotRef (its text
    // form, identity.h); this model numbers its rows with its own ids. shotRefOf: the row's ShotRef ("" for an
    // unknown id, or a card with none). idOfShot: the row holding that ShotRef, else -1; a shot with no ShotRef ("")
    // is found by its exact folder in THIS model's rows.
    Q_INVOKABLE QString shotRefOf(int id) const;
    Q_INVOKABLE int     idOfShot(const QString &shotRef, const QString &swingDir) const;
    int idOfShotRef(const pinpoint::ShotRef &shot) const;
    // The row's Shot's measurements, the one source-tagged set (Shot::measurements, through the port; refactor 6.1a): the
    // launch monitor board reads a shot's readings here, never its document. Empty for a card with no document, an
    // unknown row, or a source over no documents.
    pinpoint::analysis::MeasurementSet measurementsOf(int row) const;

    // The number the carousel SHOWS for this shot.  Deliberately not the id:
    // the id is this view's and the ordinal is the shot's swing number, and the two diverge the moment a shot is
    // trashed.  A notification saying "Shot 7" must mean the row the user can see.  0 when the id is unknown.
    int ordinalForId(int id) const;

signals:
    void activeCountChanged();
    // A shot's row left because its shot went to the trash (swingDir is ""
    // for a card that had none). The session panels forget the swing on this.
    void shotTrashed(int id, const QString &swingDir);
    // A trashShots() request is done: `moved` of `requested` shots went, and
    // `reason` says why the rest did not ("" when they all went).
    void trashFinished(int requested, int moved, const QString &reason);
    // A shot's edit (its rating, note, club, the reading's exclusion or the club correction) was NOT saved (DEC-126;
    // refactor step 3.7-a): the row shows the shot's saved value, and the composition root tells the golfer. `what`
    // names the edit ("the rating"); `why` is the error.
    void saveFailed(const QString &what, const QString &why);

private:
    // A row: the view's id for it, its Shot's key and folder, and presentation. Facts are read from the Shot.
    struct Row {
        int               id = 0;
        pinpoint::ShotRef shot;
        QString           swingDir;          // the Shot's folder ("" for a card with no document)
        QUrl              thumbnailSource;   // presentation: a live shot's thumbnail ("" → the Shot's)
        QVariantList      tracePoints;       // presentation: a live shot's trace
        // A card capture saved nothing for: no Shot facts exist, so it shows the capture's values.
        bool              unsaved = false;
        CapturedShot      captured;
        int               ordinal = 0;       // the card's number (unsaved only)
        int               rating = 0;        // the card's own edits (unsaved only: nothing to save them to)
        QString           note;
        bool              lmExcluded = false;
        QString           lmClubOverride;
    };

    const pinpoint::ShotSummary *factsOf(const Row &row) const;   // the Shot's summary (kept), else null
    int  rowForId(int id) const;
    int  rowForShot(const pinpoint::ShotRef &shot) const;
    int  insertPosition(const pinpoint::ShotRef &shot) const;   // newest first, by swing number
    void insertRow(const Row &row);
    int  addShotRow(const pinpoint::ShotRef &shot, pinpoint::SummaryRead how);   // a Shot's row, or -1 (no document)

    // The Session's events.
    void onAdded(const pinpoint::ShotRef &shot);
    void onChanged(const pinpoint::ShotRef &shot, bool rewritten);
    void onTrashed(const pinpoint::ShotRef &shot);
    void onGone();

    // A golfer's edit of a row: the Shot's command, a refusal (no Shot: said so), or the card's own (no document).
    enum class Edit { Shot, Refused, Card };
    Edit forwarded(int row, const char *what,
                   const std::function<pinpoint::WriteResult(pinpoint::ShotSource &, const pinpoint::ShotRef &)> &command);

    bool trashOne(const QString &dir, const pinpoint::ShotRef &shot);
    void removeTrashedRow(int id, const QString &dir);
    void attemptPendingTrash();
    void finishTrash();

    // trashShots() state: the folders still to move, and the request's tally.
    struct PendingTrash { int id = 0; int ordinal = 0; QString dir; int attempts = 0; pinpoint::ShotRef shot; };
    QVector<PendingTrash> m_trashPending;
    QSet<int>     m_leftByEvent;   // rows the Session's EVT-03 removed (a pending trash of one is done)
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

    QVector<Row>  m_rows;
    int           m_nextId = 1;
    // Next number to hand a card that has no document of its own. Monotonic for the session shown: moved past every
    // row's number, so "Shot 7" names one shot for the life of the session.
    int           m_nextOrdinal = 1;
    pinpoint::ShotRef m_editing;   // the shot whose edit is being forwarded (its event is that edit's, said once)
    pinpoint::ShotSourceFor               m_sourceFor;
    // Last: destroyed first, so its callbacks never reach a half-destroyed view.
    std::unique_ptr<pinpoint::ShotSource> m_source;
};
