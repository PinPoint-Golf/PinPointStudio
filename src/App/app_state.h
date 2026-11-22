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
//   sessionOnScreen       R-APP-3: the reviewed session, else the capture target, else the live carousel's (J-a2)
//   mode                  Capture | Replay | Analyse (the ints SessionMode.qml used to hold: 0, 1, 2)
//   selection             R-APP-4: a shot by its ShotRef, scoped to the session on screen: cleared when that
//                         session's identity changes (E-05), never matched by folder name
//   castSelection         R-APP-5: follows the selection (today's behaviour; Q-07 is Mark's: J-a1)
//   replay                R-APP-6: what is playing (the ShotReplayController). In the cast modes a new shot is
//                         replayed without being selected (Main.qml), as before (J-a3)
//
// The transitions between modes (enterCapture … enterLoadedSession) are SessionMode.qml's, moved here unchanged in
// effect; the session's start and end are one command each (E-12's QML half: startSession, endSession), their order
// fixed here instead of in four QML copies.

#include "../DomainModel/athlete.h"
#include "../DomainModel/session.h"
#include "../Identity/identity.h"

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

// The composition root's callables (main.cpp). Each is one call on today's objects; AppState fixes the order.
struct AppStateDoors {
    std::function<Athlete *()>                      currentAthlete;
    std::function<Session *(const QString &)>        sessionAt;         // App::sessionAt: the current athlete's
    std::function<bool()>                           sessionRunning;    // SessionController::running
    // The folders sessionOnScreen chooses between (today's three-way chain, E-08)
    std::function<QString()>                        reviewFolder;      // the loaded session's; "" when not reviewing
    std::function<QString()>                        captureFolder;     // ShotProcessor::activeSessionDir
    std::function<QString()>                        liveFolder;        // SessionReviewController::liveSessionDir
    std::function<void()>                           resumeLive;        // leave a review
    // The replay
    std::function<void(int, const QString &, bool)> replayStart;       // swing number, folder, autoplay
    std::function<void()>                           replayStop;
    std::function<QString()>                        replayFolder;      // "" when nothing is loaded
    std::function<bool()>                           autoReplayAfterSelect;
    // A session's start and end (E-12)
    std::function<void(bool)>                       openCaptureSession;   // ShotProcessor::beginSessionFolder(type, extend)
    std::function<void(const QString &)>            showLiveShots;        // the live carousel loads a folder
    std::function<void()>                           startClock;           // SessionController::start(defaultType)
    std::function<void()>                           startCameras;
    std::function<void()>                           stopCameras;
    std::function<void()>                           stopClock;            // SessionController::endSession
    std::function<void()>                           closeCaptureSession;  // ShotProcessor::endSessionFolder
    std::function<QString()>                        todaysFolder;         // ShotProcessor::todaySessionDir(defaultType)
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
    Q_PROPERTY(QObject *replay READ replay CONSTANT)
public:
    enum Mode { Capture = 0, Replay = 1, Analyse = 2 };

    explicit AppState(QObject *parent = nullptr);

    void setDoors(AppStateDoors doors) { m_doors = std::move(doors); }
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
    Session *sessionOnScreen() const { return m_onScreen; }
    QString  sessionOnScreenFolder() const { return m_onScreenFolder; }

    ShotSelection *selection() { return &m_selection; }
    ShotSelection *castSelection() { return &m_castSelection; }
    QObject       *replay() const { return m_replay; }

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
    // — then the live carousel shows it (empty for a new one), then the clock (sessionStarted: the preflight
    // notices, the trunk ceremony, the profiler), then the cameras.
    Q_INVOKABLE void startSession(bool extend);
    // End: cameras, then the clock (the queued work-ons unpause), then the capture service ends the Session
    // (Session::end, the empty discard), and only then — with `reloadCarousel` (the toolbar's End, not the close) —
    // the live carousel shows today's remaining session.
    Q_INVOKABLE void endSession(bool reloadCarousel);

    // The composition root calls these when what they read changes.
    void refreshSessionOnScreen();
    void refreshCaptureTarget() { emit captureTargetChanged(); refreshSessionOnScreen(); }
    void refreshAthlete();   // a switch: the selection was the last athlete's, so it clears
    void refreshAthleteLock() { emit athleteLockedChanged(); }

signals:
    void modeChanged();         // only on a real change: PpModeStage rebuilds its panels on it
    void autoReturnChanged();
    void currentAthleteChanged();
    void athleteLockedChanged();
    void captureTargetChanged();
    void sessionOnScreenChanged();

private:
    void setMode(Mode m);
    void setAutoReturn(bool on);
    void select(const QString &shotRef, const QString &swingDir);
    void loadSelected(bool fromCapture);
    void dropSelection();

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
};

} // namespace pinpoint
