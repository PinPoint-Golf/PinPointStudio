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

#include <QObject>
#include <QString>
#include <QVariantList>

#include "session_list_model.h"
#include "shot_list_model.h"
#include "../launchmonitor/lm_session_model.h"   // the board of the shots on screen (refactor 6.1b)

#include <QSet>

class AppSettings;
class AthleteController;
namespace pinpoint { class RecordingRepository; class SessionListPresenter; struct SessionListRow; }

// SessionReviewController — the single source of truth for "am I reviewing a
// saved session, and if so which one". Global (one instance, a QML context
// property `sessionReviewController`), so all four mode screens show the same
// loaded session: switching Swing↔Wrist↔GRF↔Coach while reviewing keeps the
// selection. Sibling to SessionController (which stays clock-only) — it owns the
// disk enumeration and load that a clock controller has no business holding.
//
//  • sessionsModel — every session of the current athlete, as the one session list holds it (SessionListPresenter:
//    a view over the Athlete's SessionIndex, refactor step 5.7a, R-QML-6; rebuilt when it changes, so a trash, a move or
//    a club corrected anywhere shows here at once), plus the live session synthesized from the in-memory shotModel
//    (pinned first, isLive). The live session's own folder is excluded so it never appears twice.
//  • shots         — the review's carousel: a view of the loaded Session (refactor step 5.2; R-QML-5) — the same
//    roles and invokables as the live shotModel, so the carousel's edits and trash work unchanged.
//
// Entering review (loadSession) flips reviewActive true; main.cpp reacts by
// stopping live capture (which disarms the shot trigger) and gating ShotController
// — the live session/clock is left running, so resumeLive() returns to it.
class SessionReviewController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool     reviewActive   READ reviewActive   NOTIFY reviewActiveChanged)
    Q_PROPERTY(QObject *sessionsModel  READ sessionsModel  CONSTANT)
    Q_PROPERTY(QObject *shots          READ shots          CONSTANT)
    // THE SHOTS THE APP IS ACTUALLY SHOWING: the loaded session's while reviewing, the
    // live carousel's otherwise. One answer, in one place, because every panel that
    // boards "the session" has to agree with the carousel about which session that is —
    // the launch monitor board took the live model unconditionally and so drew an empty
    // board over a loaded session that was full of readings.
    Q_PROPERTY(ShotListModel *activeShots READ activeShots NOTIFY reviewActiveChanged)
    // THE LAUNCH MONITOR BOARD OF THE SHOTS ON SCREEN (refactor 6.1b; lint A2, information model C10: no view makes it).
    // One presenter, beside the list it boards: it boards `activeShots`, as a reviewed session while reviewing, but only
    // while a panel holds it (holdLmBoard / releaseLmBoard), so with the panel off (the default) it rebuilds on no shot.
    // The panel binds what it is asked that only a view knows: the focused shot, the live connector's gate and name, the
    // grade policy and the hand.
    Q_PROPERTY(LmSessionModel *lmBoard READ lmBoard CONSTANT)
    // THE SESSION ITSELF, not its labels: the absolute directory of the loaded session, empty
    // while live. loadSession() takes the same string as `sessionId` — a session's id IS its
    // directory path — so a panel that has to point something else at the reviewed session
    // (the diagnostics ledger reads its swing_* dirs and diagnostics.json off disk) has one
    // place to read it from instead of re-deriving it from a shot's swingDir.
    Q_PROPERTY(QString  activeSessionId READ activeSessionId NOTIFY reviewActiveChanged)
    // TODAY'S SESSION AS THE CAROUSEL SHOWS IT: the session the live carousel is a view of (app.state's live session:
    // refactor step 5.2), empty while it shows no saved shot. This is the live session whether or not capture is
    // running — app.state names today's on entry, at session start and at session end — where
    // ShotProcessor::activeSessionDir is only set between a session's start and its end.
    // The diagnostics panel reads this when no session is running, so a session that was ended,
    // or the app relaunched on, still shows its ledger.
    Q_PROPERTY(QString  liveSessionDir READ liveSessionDir NOTIFY liveSessionDirChanged)
    // Loaded-session header bits for the toolbar review strip + carousel chip.
    Q_PROPERTY(QString  activeDayLabel  READ activeDayLabel  NOTIFY reviewActiveChanged)
    Q_PROPERTY(QString  activeTimeLabel READ activeTimeLabel NOTIFY reviewActiveChanged)
    // The loaded session's clubs, summed over its carousel's rows (the Shots' clubs, first-seen in swing order): a club
    // changed in review shows here at once (refactor step 5.2).
    Q_PROPERTY(QString  activeClubMix   READ activeClubMix   NOTIFY activeClubMixChanged)
    Q_PROPERTY(int      activeShotCount READ activeShotCount NOTIFY activeShotCountChanged)
    // THE REASON A REVIEW WAS OPENED ON ("See these shots →" on Home, shot_outcomes_design.md §7.4): a
    // condition id, empty for none. Set it BEFORE loadSession(); the next loadSession keeps it, any other
    // session being loaded, or leaving review, clears it, so it can never carry over to a session nobody
    // opened through a reason.
    Q_PROPERTY(QString  reasonFilter READ reasonFilter WRITE setReasonFilter NOTIFY reasonFilterChanged)

public:
    SessionReviewController(ShotListModel     *liveModel,
                            AppSettings       *settings,
                            AthleteController *athlete,
                            QObject           *parent = nullptr);
    ~SessionReviewController() override;

    // The one door to the swing files (main.cpp's): a JSON-era swing's summary sidecar is written through it when the
    // review load indexes a session. (The rows' edits go to their Shots, never through this.)
    void setRecordingRepository(pinpoint::RecordingRepository *repo) { m_repo = repo; }
    // The one session list (main.cpp's; it outlives this controller). The drawer's rows are its current athlete's.
    void setSessionList(pinpoint::SessionListPresenter *list);

    bool     reviewActive()   const { return m_reviewActive; }
    QObject *sessionsModel()        { return &m_sessionsModel; }
    QObject *shots()                { return &m_reviewModel; }
    ShotListModel *activeShots()    { return m_reviewActive ? &m_reviewModel : m_liveModel; }
    LmSessionModel *lmBoard();
    // A panel showing the board holds it while it is up (Component.onCompleted) and lets it go when it is destroyed (or
    // says so). Held by any view, the board boards the shots on screen; held by none, it boards nothing and forgets its
    // live grades, as a board made and destroyed with its panel did.
    Q_INVOKABLE void holdLmBoard(QObject *view);
    Q_INVOKABLE void releaseLmBoard(QObject *view);
    QString  activeSessionId() const { return m_loadedSessionDir; }
    // Absolute path of the session the live model holds (parent of any live shot's
    // swingDir), or empty when no live shot has a folder.
    QString  liveSessionDir() const;
    QString  activeDayLabel()  const { return m_activeDayLabel; }
    QString  activeTimeLabel() const { return m_activeTimeLabel; }
    QString  activeClubMix()   const;
    int      activeShotCount() const { return m_reviewModel.activeCount(); }
    QString  reasonFilter()    const { return m_reasonFilter; }
    void     setReasonFilter(const QString &conditionId);

    // Rebuild sessionsModel from the one session list + the live model. It follows the list's changes; also run on
    // construction and whenever review state changes.
    Q_INVOKABLE void refresh();

    // Load a past session (sessionId == its absolute dir path) into the review
    // model and enter review. Empty/the live row's id falls through to resumeLive().
    Q_INVOKABLE void loadSession(const QString &sessionId);

    // Return to the live model. Does not resume capture — the user presses
    // Capture again; the session clock kept running throughout.
    Q_INVOKABLE void resumeLive();

    // The session's swing_NNNN dirs (absolute) for the bulk export sheet — the
    // same list the carousel feeds to swingExporter. Empty for the live row's
    // sentinel id (its shots export through the carousel itself).
    Q_INVOKABLE QVariantList swingDirsForSession(const QString &sessionId) const;

    // Move a whole saved session folder to the OS trash (recoverable there — the
    // same recovery path as the carousel's per-shot trash) and rebuild the list,
    // through SessionRepository::trash by the session's SessionId (step 2.6).
    // No-op (false) for the live row, the live session's own folder (logged), a
    // session its session file says is in progress, or one that can't be trashed;
    // if the trashed session was being reviewed, falls back to live. True on success.
    Q_INVOKABLE bool trashSession(const QString &sessionId);

public slots:
    // The session list is athlete-scoped. Nothing used to react to an athlete switch, so
    // the drawer kept showing the previous athlete's sessions until something else forced
    // a rebuild. Drops any loaded session (it belongs to the previous athlete) and rebuilds.
    // Connected to currentAthleteChanged, which fires on a SWITCH only (refactor step 2.3): an edit
    // (a club length, the form's Save) no longer drops the review the coach is in (E-03).
    void onAthleteChanged();

signals:
    void reviewActiveChanged();
    void activeShotCountChanged();
    void liveSessionDirChanged();
    void activeClubMixChanged();
    void reasonFilterChanged();
    // An ARCHIVED session was opened (SessionArchiver): its stubs are on screen — rows, scores,
    // chips — but no replay. main.cpp routes this to ArchiveController::restoreSession, whose
    // sessionRestored() re-opens it complete.
    void archivedSessionOpened(const QString &sessionDir);

private:
    SessionListModel::Row buildLiveRow(qint64 nowMs) const;
    SessionListModel::Row buildDiskRow(const pinpoint::SessionListRow &session, qint64 nowMs) const;
    void refreshSoon();                // coalesced: the list's changes arrive in bursts
    bool m_refreshArmed = false;
    pinpoint::SessionListPresenter *m_list = nullptr;
    void updateLiveSessionDir();
    QString m_liveSessionDir;      // last value announced; see liveSessionDir()
    void clearReasonFilter();
    QString m_reasonFilter;
    bool    m_reasonFilterArmed = false;   // set, and the loadSession it is meant for has not run yet

    ShotListModel     *m_liveModel = nullptr;
    AppSettings       *m_settings  = nullptr;
    AthleteController *m_athlete   = nullptr;

    SessionListModel   m_sessionsModel;
    pinpoint::RecordingRepository *m_repo = nullptr;   // stateless: used from the pool too
    ShotListModel      m_reviewModel;     // a view of the loaded Session

    // The board (lmBoard): made on first use, after the rule data is installed; boards activeShots while held.
    LmSessionModel           *m_lmBoard = nullptr;
    QSet<QObject *>           m_lmBoardViews;
    void                      followLmBoard();

    bool    m_reviewActive = false;
    QString m_activeDayLabel;
    QString m_activeTimeLabel;
    QString m_loadedSessionDir;        // absolute dir of the reviewed session, empty when live
};
