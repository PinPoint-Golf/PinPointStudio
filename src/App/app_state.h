/*
 * Copyright (C) 2026 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#pragma once

// AppState (OBJ-APPSTATE) — what the app is doing now, `app.state` in QML (information_model.md §6; R-QML-1;
// refactor step 4-a). Application ring (src/App: DEC-11): it includes no Gui type. What it drives — the capture
// service, the clock, the cameras, the replay, the review — reaches it as the composition root's callables
// (AppStateDoors; D18 (c), DEC-167) and the replay as an opaque QObject (main.cpp's ShotReplayController).
//
//   currentAthlete        R-APP-1: the athlete the app is on (AthleteController's, through a callable). A switch is
//                         refused while a session runs (mayChangeAthlete: the AppState rule that replaced Stage 0.8g's
//                         check; today's test is SessionController::running, not "a capture target is set": J-a5)
//   captureTarget         R-APP-2: the capture service's open session, read-only here (captureTargetFolder)
//   sessionOnScreen       R-APP-3: the reviewed session, else the capture target, else the live folder (J-a2; refactor step 5.2: kept here, no longer read off the carousel)
//   mode                  Capture | Replay | Analyse (the ints SessionMode.qml used to hold: 0, 1, 2)
//   selection             R-APP-4: a shot by its ShotRef, scoped to the session on screen: cleared when that
//                         session's identity changes (E-05), never matched by folder name
//   castSelection         R-APP-5: follows the selection (today's behaviour; Q-07 is Mark's: J-a1)
//   replay                R-APP-6: what is playing (the ShotReplayController). In the cast modes a new shot is
//                         replayed without being selected (Main.qml), as before (J-a3)
//   outcome               R-APP-7 (refactor 4-b; Mark, 22 Nov 2026, DEC-196): the shot whose outcome the carousel's
//                         summary shows — the selection's on every pick, move or clear; the newest shot when the session
//                         on screen adds one (EVT-01), even over an older selection. A ShotRef only, scoped to the
//                         session on screen and cleared with it. A re-analysis does not move it
//   shotOutcome           its summary (ShotOutcomeSummary below): a VIEW worked out on each read from `outcome` and that
//                         Shot's diagnostics; nothing of it is kept
//
// The transitions between modes (enterCapture … enterLoadedSession) are SessionMode.qml's, moved here unchanged in
// effect; the session's start and end are one command each (E-12's QML half: startSession, endSession), their order
// fixed here instead of in four QML copies.

#include "../DomainModel/athlete.h"
#include "../DomainModel/session.h"
#include "../Vocabulary/Identity/identity.h"

#include <QObject>
#include <QPointer>
#include <QString>

#include <functional>

namespace pinpoint {

// One selected shot (OBJ-SHOT-SELECTION): a ShotRef or none, with the folder it was picked with (for the replay and
// the folder readers; it is never matched against another session's). `shotRef` is ShotRef's own text form (the one
// QML spelling of it: identity.h).
class ShotSelection : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool    valid       READ valid       NOTIFY changed)
    Q_PROPERTY(QString shotRef     READ shotRef     NOTIFY changed)
    Q_PROPERTY(int     swingNumber READ swingNumber NOTIFY changed)
    Q_PROPERTY(QString swingDir    READ swingDir    NOTIFY changed)
public:
    using QObject::QObject;

    bool    valid() const { return !m_swingDir.isEmpty(); }
    ShotRef shot() const { return m_shot; }
    QString shotRef() const { return m_shot.toString(); }
    int     swingNumber() const { return m_shot.swingNumber(); }
    QString swingDir() const { return m_swingDir; }

    void set(const ShotRef &shot, const QString &swingDir);
    void clear() { set({}, {}); }

signals:
    void changed();

private:
    ShotRef m_shot;
    QString m_swingDir;
};

// THE CAROUSEL'S SHOT SUMMARY (refactor 4-b; Mark, 14 Nov 2026: "strike, direction and flight and carry", only for a
// shot with launch-monitor data). Which shot: AppState's `outcome` (R-APP-7, above). Worked out on each read by the
// composition root's door (src/App/outcome_summary.h) from that Shot's own diagnostics (ShotDiagnostics over its
// Session's owner): the summary itself is kept nowhere.
// Every string is final: a factor not read, or not judged yet, is "-" with no tone (DEC-96); the tones are
// shot_factor_words.h's kinds ("fault" | "watch" | "good" | ""), which QML maps to colours and nothing else.
struct ShotOutcomeSummary {
    Q_GADGET
    Q_PROPERTY(bool    shown         MEMBER shown)          // a launch-monitor shot to summarise
    Q_PROPERTY(bool    selected      MEMBER selected)       // it is the selection (else a newer shot: name it)
    Q_PROPERTY(QString shotRef       MEMBER shotRef)
    Q_PROPERTY(int     swingNumber   MEMBER swingNumber)
    Q_PROPERTY(bool    judged        MEMBER judged)         // its three factors are worked out
    Q_PROPERTY(QString strikeText    MEMBER strikeText)
    Q_PROPERTY(QString strikeTone    MEMBER strikeTone)
    Q_PROPERTY(QString directionText MEMBER directionText)
    Q_PROPERTY(QString directionTone MEMBER directionTone)
    Q_PROPERTY(QString flightText    MEMBER flightText)
    Q_PROPERTY(QString flightTone    MEMBER flightTone)
    Q_PROPERTY(QString carryText     MEMBER carryText)      // "164.5 yd", or "-"
public:
    bool    shown = false, selected = false, judged = false;
    QString shotRef;
    int     swingNumber = 0;
    QString strikeText = QStringLiteral("-"), strikeTone;
    QString directionText = QStringLiteral("-"), directionTone;
    QString flightText = QStringLiteral("-"), flightTone;
    QString carryText = QStringLiteral("-");
};

// The composition root's callables (main.cpp). Each is one call on today's objects; AppState fixes the order.
struct AppStateDoors {
    std::function<Athlete *()>                      currentAthlete;
    std::function<Session *(const QString &)>        sessionAt;         // App::sessionAt: the current athlete's
    std::function<bool()>                           sessionRunning;    // SessionController::running
    // The folders sessionOnScreen chooses between (today's three-way chain, E-08)
    std::function<QString()>                        reviewFolder;      // the loaded session's; "" when not reviewing
    std::function<QString()>                        captureFolder;     // ShotProcessor::activeSessionDir
    std::function<void()>                           resumeLive;        // leave a review
    // The replay
    std::function<void(int, const QString &, bool)> replayStart;       // swing number, folder, autoplay
    std::function<void()>                           replayStop;
    std::function<QString()>                        replayFolder;      // "" when nothing is loaded
    std::function<bool()>                           autoReplayAfterSelect;
    // A session's start and end (E-12)
    std::function<void(bool)>                       openCaptureSession;   // ShotProcessor::beginSessionFolder(type, extend)
    std::function<void()>                           startClock;           // SessionController::start(defaultType)
    std::function<void()>                           startCameras;
    std::function<void()>                           stopCameras;
    std::function<void()>                           stopClock;            // SessionController::endSession
    std::function<void()>                           closeCaptureSession;  // ShotProcessor::endSessionFolder
    std::function<QString()>                        todaysFolder;         // ShotProcessor::todaySessionDir(defaultType)
    // A shot is saving (#281 B, #188, #280): true from a shot's impact until its document write has landed
    // (ShotProcessor busy and not replaying: PostRoll, Gathering, Processing). Unset (the tests' default): never. And the
    // toolbar End's device release, run after its End: cameras deselected, sensors disconnected (CameraManager /
    // ImuManager::disconnectAll, whose deselect joins and drops a shot still being saved).
    std::function<bool()>                           shotInFlight;
    std::function<void()>                           releaseDevices;
    // The declared focus waiting for a Session (refactor 4-d; J-64: "until Start hands it to the Session"): the pick
    // ("" for none), dropping it, and its declaration on the capture service's open session (session_focus.h:
    // Session::setSessionIntent, EVT-10), true only when a session took it. The Session owns it from then on.
    std::function<QString()>                        pendingFocus;         // SessionController::focusConditionId
    std::function<void()>                           dropPendingFocus;     // …set to ""
    std::function<bool(const QString &)>            declareStartFocus;    // ShotProcessor::declareStartFocus
    // The shot summary (refactor 4-b; src/App/outcome_summary.h): worked out from the session on screen, the outcome's
    // shot and whether that is the selection, and the owners' change signals that refresh it, heard for as long as
    // `context` lives. Unset (the tests' default): no summary.
    std::function<ShotOutcomeSummary(Session *, const ShotRef &shot, bool isSelection)>       shotOutcome;
    std::function<void(Session *, QObject *context, std::function<void()> changed)>          watchOutcomeSummary;
};

class AppState : public QObject
{
    Q_OBJECT
    // The mode's three values, for comparing `mode` in QML (`replay` names the replay itself).
    Q_PROPERTY(int captureMode READ captureMode CONSTANT)
    Q_PROPERTY(int replayMode  READ replayMode  CONSTANT)
    Q_PROPERTY(int analyseMode READ analyseMode CONSTANT)
    Q_PROPERTY(int mode READ mode NOTIFY modeChanged)
    Q_PROPERTY(bool autoReturnToCapture READ autoReturnToCapture NOTIFY autoReturnChanged)
    Q_PROPERTY(pinpoint::Athlete *currentAthlete READ currentAthlete NOTIFY currentAthleteChanged)
    Q_PROPERTY(bool athleteLocked READ athleteLocked NOTIFY athleteLockedChanged)
    Q_PROPERTY(QString captureTargetFolder READ captureTargetFolder NOTIFY captureTargetChanged)
    Q_PROPERTY(pinpoint::Session *sessionOnScreen READ sessionOnScreen NOTIFY sessionOnScreenChanged)
    Q_PROPERTY(QString sessionOnScreenFolder READ sessionOnScreenFolder NOTIFY sessionOnScreenChanged)
    Q_PROPERTY(pinpoint::ShotSelection *selection READ selection CONSTANT)
    Q_PROPERTY(pinpoint::ShotSelection *castSelection READ castSelection CONSTANT)
    Q_PROPERTY(pinpoint::ShotSelection *outcome READ outcome CONSTANT)
    Q_PROPERTY(QObject *replay READ replay CONSTANT)
    Q_PROPERTY(pinpoint::ShotOutcomeSummary shotOutcome READ shotOutcome NOTIFY shotOutcomeChanged)
    Q_PROPERTY(bool shotSaving READ shotSaving NOTIFY shotSavingChanged)
public:
    enum Mode { Capture = 0, Replay = 1, Analyse = 2 };

    explicit AppState(QObject *parent = nullptr);

    void setDoors(AppStateDoors doors) { m_doors = std::move(doors); watchOutcome(); }
    void setReplay(QObject *replay) { m_replay = replay; }

    int  captureMode() const { return Capture; }
    int  replayMode() const { return Replay; }
    int  analyseMode() const { return Analyse; }
    int  mode() const { return m_mode; }
    bool autoReturnToCapture() const { return m_autoReturn; }

    Athlete *currentAthlete() const { return m_doors.currentAthlete ? m_doors.currentAthlete() : nullptr; }
    // R-APP-1's rule: no athlete switch while a session runs. AthleteController asks it before a switch, a delete
    // of the current athlete, a clear, and before a new athlete is made current.
    bool mayChangeAthlete() const { return !(m_doors.sessionRunning && m_doors.sessionRunning()); }
    bool athleteLocked() const { return !mayChangeAthlete(); }

    QString  captureTargetFolder() const { return m_doors.captureFolder ? m_doors.captureFolder() : QString(); }
    // THE LIVE SESSION (J-a2's rule, kept by its owner since refactor step 5.2; R-QML-5): the session the live carousel
    // shows — the capture target while one is set, else the live folder: the latest session at launch and on an
    // athlete switch (the Athlete's SessionIndex), today's after End or on entering the wrist screen, the capture
    // target's after it lets go (an End that does not reload keeps the ended session on screen, as before).
    QString  liveFolder() const { return m_liveFolder; }
    QString  liveSessionFolder() const;   // the capture target, else the live folder
    // The live carousel shows `folder` (the wrist screen's entry: today's session of its type, "" for none).
    Q_INVOKABLE void setLiveFolder(const QString &folder);
    // The current athlete's latest session (its SessionIndex, by start), "" for none: launch and an athlete switch.
    void showLatestSession();
    Session *sessionOnScreen() const { return m_onScreen; }
    QString  sessionOnScreenFolder() const { return m_onScreenFolder; }

    ShotSelection *selection() { return &m_selection; }
    ShotSelection *castSelection() { return &m_castSelection; }
    ShotSelection *outcome() { return &m_outcome; }
    QObject       *replay() const { return m_replay; }
    // The carousel's shot summary (above), worked out now.
    ShotOutcomeSummary shotOutcome() const;

    // ── The selection and the mode (SessionMode.qml's transitions, same effect) ──
    // `shotRef` is ShotRef's text form ("" for a shot whose session cannot be named: J-a4); `swingDir` the folder
    // the picking view holds for it.
    Q_INVOKABLE void pickShot(const QString &shotRef, const QString &swingDir);
    Q_INVOKABLE void enterCapture();
    Q_INVOKABLE void enterAnalyse();
    Q_INVOKABLE void showReplay();
    Q_INVOKABLE void enterReplay(const QString &shotRef, const QString &swingDir, bool fromCapture = false);
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE void moveSelection(const QString &shotRef, const QString &swingDir);
    Q_INVOKABLE void enterLoadedSession();
    // The replay played to its end: back to Capture if it was the post-shot auto-replay.
    Q_INVOKABLE void replayEnded();

    // ── A session's start and end, one command each (E-12) ──
    // Start: the capture service opens (or, extend, resumes) the session — the Athlete's startSession/resumeSession
    // — and a NEW one takes the wizard's declared focus, from its first shot (refactor 4-d: handOverFocus below;
    // Extend keeps the session's saved intent, and drops the pick); then the live carousel shows it (empty for a new
    // one), then the clock (sessionStarted: the preflight
    // notices, the trunk ceremony, the profiler), then the cameras.
    Q_INVOKABLE void startSession(bool extend);
    // End: cameras, then the clock (the queued work-ons unpause), then the capture service ends the Session
    // (Session::end, the empty discard), and only then — with `reloadCarousel` (the toolbar's End, not the close) — the
    // live carousel shows today's remaining session and the devices are released (releaseDevices), last: the release
    // joins and drops a shot still being saved, so it comes after the End and never before it. End is at once; a view
    // that must not drop a saving shot waits for `shotSaving` to fall first (below). Called while a shot is saving, it
    // drops it ("End anyway", "Close anyway": the golfer's choice; a session whose only shot it was is then empty and
    // discarded).
    Q_INVOKABLE void endSession(bool reloadCarousel);
    // A SHOT IS SAVING (#281 B, #188, #280; Solution Architect, 25 Nov 2026; product control AD-50): read-only, the capture service's own fact —
    // a shot between its impact and its saved file (its document write landed), queued shots included. The End card and
    // the close prompt watch it: they say "Saving the last shot…", and call End themselves when it falls. Its change is
    // announced queued, never inside the capture service's own state change, so a view may call End from it.
    bool shotSaving() const { return m_doors.shotInFlight && m_doors.shotInFlight(); }
    // The composition root calls it when the capture service's state moves (ShotProcessor::stateChanged).
    void refreshShotSaving();

    // The composition root calls these when what they read changes.
    void refreshSessionOnScreen();
    // The capture target moved (ShotProcessor::activeSessionDirChanged): every way a session comes into being — Start,
    // the first swing of a Start that opened none, D7's restart — so the pending focus is handed over here first.
    // The live folder takes the capture target's session whenever one is set, so the live carousel never passes
    // through no session when capture lets go of it (End).
    void refreshCaptureTarget();
    void refreshAthlete();   // a switch: the selection was the last athlete's, so it clears
    void refreshAthleteLock() { emit athleteLockedChanged(); }

signals:
    void modeChanged();         // only on a real change: PpModeStage rebuilds its panels on it
    void autoReturnChanged();
    void currentAthleteChanged();
    void athleteLockedChanged();
    void captureTargetChanged();
    void sessionOnScreenChanged();
    void liveSessionChanged();  // liveSessionFolder() moved: the live carousel is pointed at it (main.cpp)
    void shotOutcomeChanged();  // the outcome, the selection, the session on screen, or what its owners hold moved
    void shotSavingChanged();   // queued (refreshShotSaving)

private:
    void setMode(Mode m);
    void setAutoReturn(bool on);
    void select(const QString &shotRef, const QString &swingDir);
    void loadSelected(bool fromCapture);
    void dropSelection();
    QString           latestSessionFolder() const;
    void              announceLive();   // liveSessionChanged, when liveSessionFolder() moved
    QString           m_liveFolder;
    bool              m_ending = false;   // inside endSession: capture letting go settles nothing yet
    bool              m_announcedShotSaving = false;   // the value shotSavingChanged last announced
    QString           m_announcedLive;

    AppStateDoors     m_doors;
    QPointer<QObject> m_replay;
    Mode              m_mode = Capture;
    bool              m_autoReturn = false;
    QPointer<Session> m_onScreen;
    QMetaObject::Connection m_onScreenGone;
    QString           m_onScreenFolder;
    SessionId         m_scopeId;       // the session the selection is scoped to: the last one on screen
    QString           m_scopeFolder;
    ShotSelection     m_selection;
    ShotSelection     m_castSelection;
    ShotSelection     m_outcome;       // R-APP-7: the summary's shot
    QPointer<QObject> m_outcomeWatch;  // the connections heard for the session on screen (dropped with it)
    void              watchOutcome();
    void              shotLanded(const ShotRef &shot);   // EVT-01 of the session on screen: the outcome moves to it
    // THE ONE HAND-OVER of the pending focus (refactor 4-d, review B1): to the capture service's open session, and
    // dropped only once a session took it. A Start that opened no session keeps it for the session its first swing makes.
    void              handOverFocus();
};

} // namespace pinpoint
